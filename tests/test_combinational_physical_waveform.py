#!/usr/bin/env python3
"""Regress native combinational P&R against settled physical truth tables.

Only Verilog fixtures are read from the repository. Placement, IFCN/QCA mapping,
input vectors and paired Bistable waveforms are generated for each invocation.
The independent source oracle determines expected values, never the routed DAG
or agreement between the two simulation implementations.
"""

from __future__ import annotations

import argparse
import itertools
import json
import math
from pathlib import Path
import re
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / "src/python"), str(ROOT / "tests/support")]

from ifcn.frontend import normalize_verilog
from ifcn.layout_validation import dag_from_candidate, export_native_ifcn, validate_candidate
from validate_sequential_energy_waveform import parse_rst
from ifcn.verify_logic import source_analysis, verify_bytes


CASES = {
    name: ('irregular', ROOT / 'tests/benchmarks_f' / source)
    for name, source in [('xor2','TOY/xor2_demo.v'), ('xnor2','TOY/xnor2.v'),
                         ('mux21','TOY/mux21.v'), ('majority','MAJ/1bitAdderMaj.v')]
}

HOLD_CYCLES = 8
CHECK_CYCLES = 3
SAMPLES_PER_CYCLE = 128
POLARIZATION_THRESHOLD = 0.5


def require(condition, message):
    if not condition:
        raise AssertionError(message)


def read_json(path):
    def reject_nonfinite(value):
        raise ValueError(f"Non-finite JSON number {value} in {path}")
    def finite_float(value):
        parsed = float(value)
        if not math.isfinite(parsed):
            reject_nonfinite(value)
        return parsed
    return json.loads(path.read_text(), parse_constant=reject_nonfinite, parse_float=finite_float)


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, allow_nan=False) + "\n")


def run_command(command, directory, name, timeout):
    command = [str(value) for value in command]
    try:
        result = subprocess.run(command, cwd=directory, capture_output=True,
                                text=True, timeout=timeout, check=False)
    except subprocess.TimeoutExpired as error:
        raise AssertionError(f"{name} exceeded {timeout}s: {command}") from error
    (directory / f"{name}.log").write_text(result.stdout + result.stderr)
    require(result.returncode == 0,
            f"{name} exited {result.returncode}: {command}\n"
            f"{(result.stdout + result.stderr)[-4000:]}")


def device_terminals(path, source):
    """Match named QCA terminals; allow only explicit source output aliases."""
    terminals = {"INPUT": {}, "OUTPUT": {}}
    cells = path.read_text().split("[TYPE:QCADCell]")[1:]
    require(bool(cells), "Mapped QCA contains no cells")
    for block in cells:
        block = block.split("[#TYPE:QCADCell]", 1)[0]
        kind = re.search(r"^cell_function=QCAD_CELL_(INPUT|OUTPUT)$", block, re.M)
        if not kind:
            continue
        label = re.search(r"^psz=([^\n]+)$", block, re.M)
        clock = re.search(r"^cell_options.clock=(\d+)$", block, re.M)
        require(label is not None and clock is not None, "QCA terminal lacks its label or clock")
        label, phase = label[1].strip(), int(clock[1])
        require(0 <= phase < 4, f"QCA terminal {label} has invalid clock {phase}")
        require(label not in terminals[kind[1]], f"Duplicate QCA {kind[1]} terminal {label}")
        terminals[kind[1]][label] = phase
    require(set(terminals["INPUT"]) == set(source["input_ports"]),
            f"QCA input terminals differ from source: {terminals['INPUT']}")
    require(len(terminals["OUTPUT"]) == len(source["output_ports"]), "QCA output count differs from source")
    output_labels = {}
    for port in source["output_ports"]:
        label, visited = port, set()
        while label not in terminals["OUTPUT"] and label in source["pure_aliases"]:
            require(label not in visited, f"Cyclic output alias at {label}")
            visited.add(label)
            label = source["pure_aliases"][label]
        require(label in terminals["OUTPUT"], f"QCA has no named output for source port {port}")
        require(label not in output_labels.values(), f"Two source outputs map to the same QCA terminal {label}")
        output_labels[port] = label
    return terminals, output_labels, len(cells)


def check_waveform(waveform, source, vectors, terminals, output_labels, expected_samples):
    count, traces = parse_rst(waveform)  # Rejects missing, malformed and non-finite traces.
    require(count == expected_samples, f"Bistable recorded {count} samples, expected {expected_samples}")
    expected_labels = set(source["input_ports"]) | set(output_labels.values()) | {f"Clock {i}" for i in range(4)}
    require(set(traces) == expected_labels, f"Unexpected waveform interface: {sorted(traces)}")
    samples_per_block = HOLD_CYCLES * SAMPLES_PER_CYCLE
    # Check the actual stimulus across every sample, not only the output windows.
    for block, vector in enumerate(vectors):
        for name, bit in zip(source["input_ports"], vector):
            for index in range(block * samples_per_block, (block + 1) * samples_per_block):
                require(traces[name][index] == 2 * bit - 1,
                        f"Stimulus {name} differs at sample {index}: {traces[name][index]} != {2 * bit - 1}")
    checked, errors, groups = 0, [], []
    truth_table = source["truth_table"]
    for block, vector in enumerate(vectors):
        group = {"inputs": dict(zip(source["input_ports"], vector)), "outputs": {}}
        start = (block * HOLD_CYCLES + HOLD_CYCLES - CHECK_CYCLES) * SAMPLES_PER_CYCLE
        stop = (block + 1) * samples_per_block
        for port, expected in zip(source["output_ports"], truth_table[block % len(truth_table)]):
            label = output_labels[port]
            clock = traces[f"Clock {terminals['OUTPUT'][label]}"]
            low, high = min(clock), max(clock)
            require(high > low > 0, f"Output {port} has no positive switching clock")
            indices = [index for index in range(start, stop) if clock[index] <= low * (1 + 1e-6)]
            require(bool(indices), f"No hold samples for output {port}, input block {block}")
            values = [traces[label][index] for index in indices]
            checked += len(indices)
            bad_count = 0
            for index, value in zip(indices, values):
                if abs(value) < POLARIZATION_THRESHOLD or (value > 0) != bool(expected):
                    bad_count += 1
                    if len(errors) < 16:
                        errors.append({"inputs": group["inputs"], "output": port, "sample": index,
                                       "expected": expected, "polarization": value})
            group["outputs"][port] = {"expected": expected, "hold_samples": len(indices),
                                      "incorrect_or_indeterminate": bad_count,
                                      "min_polarization": min(values), "max_polarization": max(values)}
        groups.append(group)
    return {"passed": not errors, "hold_samples_checked": checked,
            "counterexamples": errors, "groups": groups}


def run_case(args, directory):
    algorithm, fixture = CASES[args.case]
    source_bytes = fixture.read_bytes()
    source = source_analysis(source_bytes, max_inputs=3)
    normalized, _ = normalize_verilog(source_bytes.decode("utf-8-sig"))
    normalized_path = directory / "source_normalized.v"
    normalized_path.write_text(normalized)
    candidate_path = directory / "candidate.json"
    # The native solver stops cooperatively, then validates/serializes its best
    # candidate. Leave time for that work before the subprocess hard deadline.
    pnr_budget = args.timeout - min(5.0, args.timeout / 2.0)
    write_json(directory / "pnr_budget.json", {"solver_budget_seconds": pnr_budget,
                                              "process_timeout_seconds": args.timeout})
    run_command([args.pnr_binary, algorithm, normalized_path, candidate_path,
                 "--budget", pnr_budget], directory, "pnr", args.timeout)
    candidate = read_json(candidate_path)
    require(candidate.get("routed") is True and candidate.get("native_mapping_valid") is True,
            f"Native P&R rejected the candidate: {candidate.get('native_validation_error')}")
    validation = validate_candidate(candidate, source_crossings=False)
    write_json(directory / "layout_validation.json", validation)
    require(validation["passed"], f"Independent layout/clock DRC failed: {validation['errors']}")
    dag = dag_from_candidate(candidate, validation)
    logic = verify_bytes(source_bytes, json.dumps(dag).encode(), max_inputs=3)
    write_json(directory / "logic.json", logic)
    require(logic["status"] == "equivalent", f"Source versus routed DAG failed: {logic}")
    layout = directory / "layout.ifcn"
    width, height = export_native_ifcn(candidate, layout, algorithm)
    prefix = directory / "device"
    run_command([args.energy_binary, layout, prefix, "--qca-only"], directory, "mapping", args.timeout)
    qca = directory / "device_energy_input.qca"
    require(qca.is_file(), "Mapping did not produce a QCA device")
    terminals, output_labels, cell_count = device_terminals(qca, source)
    vectors = list(itertools.product((0, 1), repeat=len(source["input_ports"]))) * 2
    require(len(source["truth_table"]) * 2 == len(vectors), "Incomplete independent source truth table")
    vector_path = directory / "inputs.vt"
    vector_path.write_text(",".join(source["input_ports"]) + "\n" + "".join(
        (",".join(map(str, vector)) + "\n") * HOLD_CYCLES for vector in vectors))
    sample_count = len(vectors) * HOLD_CYCLES * SAMPLES_PER_CYCLE
    result_prefix = directory / "simulation"
    comparison_path = directory / "simulation.json"
    run_command([args.simulation_binary, qca, "--model", "bistable", "--vectors", vector_path,
                 "--samples", sample_count, "--repetitions", 1, "--warmup", 0,
                 "--max-iterations", 1000, "--seed", 1, "--require-equivalent",
                 "--equivalence-tolerance", 0, "--output-prefix", result_prefix, "--json", comparison_path],
                directory, "simulation", args.timeout)
    comparisons = read_json(comparison_path)["comparisons"]
    require(len(comparisons) == 1 and comparisons[0]["model"] == "bistable", "Wrong simulation model report")
    pair = comparisons[0]
    accuracy = pair["accuracy"]
    require(accuracy["comparable"] and accuracy["max_absolute_error"] == 0
            and accuracy["output_samples"] == sample_count * len(source["output_ports"]),
            f"Baseline/accelerated engines differ or have incomplete outputs: {accuracy}")
    require(pair["work"]["samples"] == sample_count and pair["work"]["max_iteration_samples"] == 0,
            f"Bistable simulation is incomplete or failed convergence: {pair['work']}")
    report = check_waveform(directory / "simulation_bistable_baseline.rst", source, vectors,
                            terminals, output_labels, sample_count)
    report.update(case=args.case, fixture=str(fixture.relative_to(ROOT)), algorithm=algorithm,
                  width=width, height=height, device_cells=cell_count, samples=sample_count,
                  solver_budget_seconds=pnr_budget, process_timeout_seconds=args.timeout,
                  hold_cycles=HOLD_CYCLES, checked_tail_cycles=CHECK_CYCLES,
                  polarization_threshold=POLARIZATION_THRESHOLD,
                  scope="Settled Bistable truth table under slow inputs; not single-cycle throughput or physical signoff")
    write_json(directory / "physical_truth.json", report)
    require(report["passed"], f"Physical truth-table mismatch:\n{json.dumps(report['counterexamples'], indent=2)}")
    print(json.dumps({key: report[key] for key in ("case", "passed", "device_cells", "samples", "hold_samples_checked")}))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--case", choices=sorted(CASES), required=True)
    parser.add_argument("--pnr-binary", type=Path, required=True)
    parser.add_argument("--energy-binary", type=Path, required=True)
    parser.add_argument("--simulation-binary", type=Path, required=True)
    parser.add_argument("--timeout", type=int, default=120, help="Timeout for each native command in seconds")
    parser.add_argument("--output-dir", type=Path, help="Optional fresh directory for debugging artifacts")
    args = parser.parse_args()
    for name in ("pnr_binary", "energy_binary", "simulation_binary"):
        path = getattr(args, name).resolve()
        if not path.is_file():
            parser.error(f"Missing executable: {path}")
        setattr(args, name, path)
    if args.timeout <= 0:
        parser.error("--timeout must be positive")
    if args.output_dir is not None:
        directory = args.output_dir.resolve()
        require(not directory.exists() or not any(directory.iterdir()), "--output-dir must be empty")
        directory.mkdir(parents=True, exist_ok=True)
        run_case(args, directory)
    else:
        with tempfile.TemporaryDirectory(prefix=f"ifcn-{args.case}-physical-") as temporary:
            run_case(args, Path(temporary))


if __name__ == "__main__":
    main()
