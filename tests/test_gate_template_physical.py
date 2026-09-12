#!/usr/bin/env python3
"""Check native-mapped gate truth for all 40 cardinal port orientations.

Every input combination is held for eight cycles and repeated twice. The shared
physical regression helper checks the actual input traces and settled output
against the independent Verilog oracle. All devices and waveforms are generated
at runtime; this test needs only the energy mapper and physical simulator.
"""

from __future__ import annotations

import argparse
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
import itertools
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parent))
import test_combinational_physical_waveform as physical


DIRECTIONS = {"N": (0, -1), "E": (1, 0), "S": (0, 1), "W": (-1, 0)}
CASES = {
    f"{gate}__in_{''.join(inputs)}__out_{output}": (gate, inputs, output)
    for gate, arity in (("not", 1), ("and", 2), ("or", 2), ("maj", 3))
    for inputs in itertools.combinations(DIRECTIONS, arity)
    for output in DIRECTIONS if output not in inputs
}


def make_fixture(case, directory):
    """Place one gate at (3,3), with distinct neighboring PI and output tiles."""
    gate, input_directions, output_direction = CASES[case]
    inputs = list("abc"[:len(input_directions)])
    expression = {"not": "~a", "and": "a & b", "or": "a | b",
                  "maj": "(a & b) | (a & c) | (b & c)"}[gate]
    source_bytes = (f"module top({', '.join(inputs)}, out);\n"
                    f"input {', '.join(inputs)};\noutput out;\nwire g;\n"
                    f"assign g = {expression};\nassign out = g;\nendmodule\n").encode()
    (directory / "source.v").write_bytes(source_bytes)
    gate_id, output_id = len(inputs), len(inputs) + 1
    nodes = []
    for index, (name, direction) in enumerate(zip(inputs, input_directions)):
        dx, dy = DIRECTIONS[direction]
        nodes.append({"id": index, "name": name, "type": "input", "is_input": True,
                      "is_output": False, "x": 3 + dx, "y": 3 + dy})
    nodes.append({"id": gate_id, "name": "g", "type": gate, "is_input": False,
                  "is_output": False, "x": 3, "y": 3})
    dx, dy = DIRECTIONS[output_direction]
    nodes.append({"id": output_id, "name": "out", "type": "output", "is_input": False,
                  "is_output": True, "x": 3 + dx, "y": 3 + dy})
    edges = [[i, gate_id] for i in range(gate_id)] + [[gate_id, output_id]]
    candidate = {
        "nodes": nodes, "edges": edges,
        "routes": [{"source": a, "target": b,
                    "path": [[nodes[a]["x"], nodes[a]["y"]], [nodes[b]["x"], nodes[b]["y"]]]}
                   for a, b in edges],
        "cells": [{"x": n["x"], "y": n["y"],
                   "phase": 1 if n["is_input"] else (3 if n["is_output"] else 2)} for n in nodes],
        "layers": [list(range(gate_id)), [gate_id], [output_id]],
    }
    validation = physical.validate_candidate(candidate, source_crossings=False)
    physical.write_json(directory / "candidate.json", candidate)
    physical.write_json(directory / "layout_validation.json", validation)
    physical.require(validation["passed"], f"Invalid fixture geometry/clock: {validation['errors']}")
    dag = physical.dag_from_candidate(candidate, validation)
    logic = physical.verify_bytes(source_bytes, json.dumps(dag).encode(), max_inputs=3)
    physical.write_json(directory / "logic.json", logic)
    physical.require(logic["status"] == "equivalent", f"Fixture source/DAG mismatch: {logic}")

    def coord(node):
        return f"({node['x']},{node['y']})"

    # Keep (3,3) instead of origin-normalizing; IFCN phases are zero-based.
    lines = [f"#circuit name: {case}", "#algorithm: gate-template-regression",
             "#mapping mode: combinational", f"#primary output nodes: {output_id}",
             "#phase count: 4", "#clock scheme: irregular", "#clock scheme consistency: success",
             "#layout area: width: 5, height: 5, area: 25", "#nodes info",
             "### nodeIndex, nodeName, nodeType, nodePosition ###"]
    lines += [f"{n['id']}, {n['name']}, {n['type']}, {coord(n)};" for n in nodes]
    lines += ["#nodes info", "#paths info", "### {node1, node2} : path ###"]
    lines += [f"({a},{b}): {coord(nodes[a])},{coord(nodes[b])};" for a, b in edges]
    lines += ["#paths info", "#phase map", "### (x,y) : phase ###"]
    lines += [f"{coord(c)}:{c['phase'] - 1};" for c in candidate["cells"]]
    lines += ["#phase map", ""]
    (directory / "layout.ifcn").write_text("\n".join(lines))
    return physical.source_analysis(source_bytes, max_inputs=3)


def run_command(command, directory, name, timeout):
    command = [str(value) for value in command]
    physical.write_json(directory / f"{name}_command.json", command)
    result = subprocess.run(command, cwd=directory, capture_output=True, text=True, timeout=timeout,
                            env={**os.environ, "OMP_NUM_THREADS": "1", "OPENBLAS_NUM_THREADS": "1"})
    (directory / f"{name}.log").write_text(result.stdout + result.stderr)
    physical.require(result.returncode == 0,
                     f"{name} exited {result.returncode}: {command}\n{(result.stdout + result.stderr)[-4000:]}")


def run_case(args, root, case):
    directory = root / case
    directory.mkdir()
    try:
        source = make_fixture(case, directory)
        run_command([args.energy_binary, directory / "layout.ifcn", directory / "mapped", "--qca-only"],
                    directory, "mapping", args.timeout)
        qca = directory / "mapped_energy_input.qca"
        terminals, output_labels, cells = physical.device_terminals(qca, source)
        physical.require(set(terminals["INPUT"].values()) == {0}, f"Wrong PI clocks: {terminals}")
        physical.require(terminals["OUTPUT"] == {"out": 2}, f"Wrong output terminal/clock: {terminals}")
        vectors = list(itertools.product((0, 1), repeat=len(source["input_ports"]))) * 2
        vector_path = directory / "inputs.vt"
        vector_path.write_text(",".join(source["input_ports"]) + "\n" + "".join(
            (",".join(map(str, vector)) + "\n") * physical.HOLD_CYCLES for vector in vectors))
        samples = len(vectors) * physical.HOLD_CYCLES * physical.SAMPLES_PER_CYCLE
        run_command([args.simulation_binary, qca, "--model", "bistable", "--vectors", vector_path,
                     "--samples", samples, "--repetitions", 1, "--warmup", 0, "--max-iterations", 1000,
                     "--seed", 1, "--require-equivalent", "--equivalence-tolerance", 0,
                     "--output-prefix", directory / "simulation", "--json", directory / "simulation.json"],
                    directory, "simulation", args.timeout)
        comparisons = physical.read_json(directory / "simulation.json")["comparisons"]
        physical.require(len(comparisons) == 1 and comparisons[0]["model"] == "bistable",
                         "Unexpected simulation model report")
        pair = comparisons[0]
        accuracy, work = pair["accuracy"], pair["work"]
        physical.require(accuracy["comparable"] and accuracy["max_absolute_error"] == 0
                         and accuracy["output_samples"] == samples,
                         f"Incomplete or unequal baseline/accelerated outputs: {accuracy}")
        physical.require(work["samples"] == samples and work["max_iteration_samples"] == 0,
                         f"Incomplete or unconverged simulation: {work}")
        truth = physical.check_waveform(directory / "simulation_bistable_baseline.rst", source, vectors,
                                        terminals, output_labels, samples)
        accelerated = physical.check_waveform(directory / "simulation_bistable_accelerated.rst", source, vectors,
                                              terminals, output_labels, samples)
        physical.require(accelerated["groups"] == truth["groups"], "Engine truth-table reports differ")
        truth.update(case=case, device_cells=cells, samples=samples, hold_cycles=physical.HOLD_CYCLES,
                     samples_per_cycle=physical.SAMPLES_PER_CYCLE, checked_tail_cycles=physical.CHECK_CYCLES)
        physical.write_json(directory / "physical_truth.json", truth)
        physical.require(truth["passed"], f"Physical truth mismatch: {truth['counterexamples']}")
        return {"case": case, "passed": True, "device_cells": cells,
                "hold_samples_checked": truth["hold_samples_checked"]}
    except Exception as error:
        report = {"case": case, "passed": False, "error": f"{type(error).__name__}: {error}"}
        physical.write_json(directory / "failure.json", report)
        return report


def run_all(args, directory):
    physical.require(Counter(value[0] for value in CASES.values()) ==
                     {"not": 12, "and": 12, "or": 12, "maj": 4}, "Incomplete orientation catalog")
    physical.HOLD_CYCLES, physical.SAMPLES_PER_CYCLE = 8, 128
    physical.CHECK_CYCLES, physical.POLARIZATION_THRESHOLD = 3, 0.5
    cases = args.case or list(CASES)
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        reports = list(pool.map(lambda case: run_case(args, directory, case), cases))
    physical.write_json(directory / "summary.json", reports)
    failures = [report for report in reports if not report["passed"]]
    print(json.dumps({"cases": len(reports), "passed": len(reports) - len(failures),
                      "hold_samples_checked": sum(r.get("hold_samples_checked", 0) for r in reports),
                      "failures": failures}, indent=2))
    return 1 if failures else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--energy-binary", type=Path, required=True)
    parser.add_argument("--simulation-binary", type=Path, required=True)
    parser.add_argument("--jobs", type=int, choices=range(1, 5), default=4)
    parser.add_argument("--case", choices=sorted(CASES), action="append", help="Rerun selected orientations")
    parser.add_argument("--timeout", type=int, default=30, help="Timeout per native command in seconds")
    parser.add_argument("--output-dir", type=Path, help="Optional empty directory to retain diagnostic artifacts")
    args = parser.parse_args()
    for name in ("energy_binary", "simulation_binary"):
        path = getattr(args, name).resolve()
        if not path.is_file():
            parser.error(f"Missing executable: {path}")
        setattr(args, name, path)
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    if args.case and len(set(args.case)) != len(args.case):
        parser.error("--case values must be unique")
    if args.output_dir is not None:
        directory = args.output_dir.resolve()
        if directory.exists() and (not directory.is_dir() or any(directory.iterdir())):
            parser.error("--output-dir must be empty")
        directory.mkdir(parents=True, exist_ok=True)
        return run_all(args, directory)
    with tempfile.TemporaryDirectory(prefix="ifcn-gate-template-") as temporary:
        return run_all(args, Path(temporary))


if __name__ == "__main__":
    sys.exit(main())
