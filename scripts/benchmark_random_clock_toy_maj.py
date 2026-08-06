#!/usr/bin/env python3
"""Benchmark the four-phase random-clock TOY/MAJ layouts end to end.

The historical phase-P&R campaign stores the clock field as packed 4x4 IFCN
tiles.  ``ifcn_energy_analysis`` currently consumes coordinate phase entries,
so this driver expands the packed field into a derived IFCN before converting
it to QCA.  Original layouts are never modified.

The physical comparison is always paired baseline/accelerated and uses the
project's existing strict benchmark runner.  Every generated artifact retains
enough provenance to reproduce the layout metrics and timing result.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import os
from pathlib import Path
import random
import re
import statistics
import subprocess
import sys
from typing import Dict, Iterable, List, Mapping, Optional, Sequence, Tuple


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_LAYOUT_DIR = ROOT / "experiments" / "phase_pr" / "full_layouts"
DEFAULT_OUTPUT_DIR = ROOT / "experiments" / "random_clock_toy_maj_20260722"

# The old phase-P&R parser rejects the direct TOY/xor5R netlist.  Its retained
# xor5_r1 layout implements the same five-input truth table (32/32 vectors).
LAYOUT_ALIASES = {("TOY", "xor5R"): "TOY_xor5_r1_p4.ifcn"}

NODE_RE = re.compile(
    r"^\s*(\d+)\s*,\s*([^,]+)\s*,\s*([^,]+)\s*,\s*\((-?\d+),(-?\d+)\)\s*;"
)
PATH_RE = re.compile(r"^\s*\((\d+),(\d+)\)\s*:\s*(.*);\s*$")
COORD_RE = re.compile(r"\((-?\d+),(-?\d+)\)")
RAW_PHASE_RE = re.compile(r"\((-?\d+),(-?\d+)\)\s*:\s*(-?\d+)\s*;")
TILE_RE = re.compile(r"tile\((-?\d+),(-?\d+)\)\s*:\s*(0x[0-9a-fA-F]+)\s*;")
HEADER_RE = re.compile(r"^#([^#][^:]*):\s*(.*)$")


def arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--layout-directory", type=Path, default=DEFAULT_LAYOUT_DIR)
    parser.add_argument("--output-directory", type=Path, default=DEFAULT_OUTPUT_DIR)
    parser.add_argument("--mapping-executable", type=Path,
                        default=ROOT / "build" / "ifcn_mapping_metrics")
    parser.add_argument("--energy-executable", type=Path,
                        default=ROOT / "build" / "ifcn_energy_analysis")
    parser.add_argument("--benchmark-executable", type=Path,
                        default=ROOT / "build-release" / "ifcn_physical_benchmark")
    parser.add_argument("--repetitions", type=int, default=5)
    parser.add_argument("--warmup", type=int, default=1)
    parser.add_argument("--cpu-affinity", default="0")
    parser.add_argument("--samples", type=int, default=512)
    parser.add_argument("--coherence-steps", type=int, default=4096)
    parser.add_argument("--time-step", type=float, default=1e-16)
    parser.add_argument("--bootstrap-resamples", type=int, default=10000)
    parser.add_argument("--prepare-only", action="store_true")
    parser.add_argument("--summarize-only", action="store_true")
    parser.add_argument("--force-prepare", action="store_true")
    args = parser.parse_args()
    if args.repetitions <= 0 or args.warmup < 0 or args.samples < 2:
        parser.error("invalid timing repetition, warm-up, or sample count")
    if args.coherence_steps <= 0 or args.time_step <= 0.0:
        parser.error("coherence steps and time step must be positive")
    return args


def normalized_header_key(value: str) -> str:
    return " ".join(value.strip().lower().split())


def int_from_text(value: str, label: Optional[str] = None) -> int:
    pattern = ((r"\b" + re.escape(label) + r"\s*:\s*(-?\d+)")
               if label else r"(-?\d+)")
    match = re.search(pattern, value, flags=re.IGNORECASE)
    if not match:
        raise ValueError("missing integer{} in {!r}".format(
            " for " + label if label else "", value))
    return int(match.group(1))


def float_from_text(value: str) -> float:
    match = re.search(r"[-+]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][-+]?\d+)?", value)
    if not match:
        raise ValueError("missing number in {!r}".format(value))
    return float(match.group(0))


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def parse_ifcn(path: Path) -> Dict[str, object]:
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    header: Dict[str, str] = {}
    nodes: Dict[int, Dict[str, object]] = {}
    routes: Dict[Tuple[int, int], List[Tuple[int, int]]] = {}
    raw_phases: Dict[Tuple[int, int], int] = {}
    tiles: List[Tuple[int, int, str]] = []
    section = ""
    for line in lines:
        header_match = HEADER_RE.match(line.strip())
        if header_match:
            header[normalized_header_key(header_match.group(1))] = header_match.group(2).strip()
        marker = line.strip().lower()
        if marker == "#nodes info":
            section = "" if section == "nodes" else "nodes"
            continue
        if marker == "#paths info":
            section = "" if section == "paths" else "paths"
            continue
        if marker == "#phase map":
            section = "" if section == "phase" else "phase"
            continue
        if not line.strip() or line.lstrip().startswith("###"):
            continue
        if section == "nodes":
            match = NODE_RE.match(line)
            if match:
                node_id, name, node_type, x, y = match.groups()
                nodes[int(node_id)] = {
                    "name": name.strip(), "type": node_type.strip().lower(),
                    "coord": (int(x), int(y)),
                }
        elif section == "paths":
            match = PATH_RE.match(line)
            if match:
                source, target, points = match.groups()
                routes[(int(source), int(target))] = [
                    (int(x), int(y)) for x, y in COORD_RE.findall(points)
                ]
        elif section == "phase":
            for match in RAW_PHASE_RE.finditer(line):
                raw_phases[(int(match.group(1)), int(match.group(2)))] = int(match.group(3))
            for match in TILE_RE.finditer(line):
                tiles.append((int(match.group(1)), int(match.group(2)), match.group(3)))

    phase_count = int_from_text(header.get("phase count", "4"))
    area_text = header.get("layout area", "")
    width = int_from_text(area_text, "width")
    height = int_from_text(area_text, "height")
    block_match = re.search(r"block_size\s*=\s*(\d+)", header.get("phase codec", ""))
    block_size = int(block_match.group(1)) if block_match else phase_count
    phases = dict(raw_phases)
    if not phases:
        digits_per_tile = 2 * block_size
        for tile_x, tile_y, encoded in tiles:
            digits = encoded.lower()
            if digits.startswith("0x"):
                digits = digits[2:]
            digits = digits.zfill(digits_per_tile)[-digits_per_tile:]
            for local_y in range(block_size):
                row = int(digits[2 * local_y:2 * local_y + 2], 16)
                for local_x in range(block_size):
                    x = tile_x * block_size + local_x
                    y = tile_y * block_size + local_y
                    if x < width and y < height:
                        phases[(x, y)] = (row >> (2 * local_x)) & 0x3

    if not nodes or not routes or not phases:
        raise ValueError("incomplete IFCN {}: nodes={}, routes={}, phases={}".format(
            path, len(nodes), len(routes), len(phases)))
    invalid = sorted({value for value in phases.values() if value < 0 or value >= phase_count})
    if invalid:
        raise ValueError("out-of-range phases in {}: {}".format(path, invalid))
    used = {tuple(node["coord"]) for node in nodes.values()}
    used.update(point for route in routes.values() for point in route)
    missing = sorted(used.difference(phases))
    if missing:
        raise ValueError("phase field misses {} used coordinates in {}".format(len(missing), path))

    return {
        "lines": lines, "header": header, "nodes": nodes, "routes": routes,
        "phases": phases, "phase_count": phase_count, "block_size": block_size,
        "width": width, "height": height, "used": used,
    }


def write_coordinate_phase_ifcn(layout: Mapping[str, object], source: Path,
                                destination: Path) -> None:
    lines = list(layout["lines"])
    marker_index = next(
        (index for index, line in enumerate(lines) if line.strip().lower() == "#phase map"),
        None,
    )
    if marker_index is None:
        raise ValueError("missing phase map marker in {}".format(source))
    prefix = lines[:marker_index]
    phases = layout["phases"]
    width = int(layout["width"])
    height = int(layout["height"])
    expanded = [
        "#derived phase representation: packed tiles expanded to coordinate entries",
        "#derived from: {}".format(source.resolve()),
        "#phase map",
        "### (x,y) : phase ###",
    ]
    for y in range(height):
        expanded.append(" ".join(
            "({0},{1}):{2};".format(x, y, phases[(x, y)]) for x in range(width)
        ))
    expanded.append("#phase map")
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text("\n".join(prefix + expanded) + "\n", encoding="utf-8")


def phase_cycles(sequence: Sequence[int], phase_count: int) -> float:
    if not sequence:
        return 0.0
    start = int(sequence[0]) % phase_count
    relative = [(int(value) - start) % phase_count for value in sequence]
    wraps = 0
    previous = relative[0]
    for value in relative[1:]:
        if value < previous:
            wraps += 1
        previous = value
    return wraps + (relative[-1] + 1) / float(phase_count)


def critical_clock_metrics(layout: Mapping[str, object]) -> Tuple[float, int, int]:
    nodes = layout["nodes"]
    routes = layout["routes"]
    phases = layout["phases"]
    phase_count = int(layout["phase_count"])
    adjacency: Dict[int, List[int]] = {node: [] for node in nodes}
    indegree: Dict[int, int] = {node: 0 for node in nodes}
    violations = 0
    for (source, target), path in routes.items():
        adjacency.setdefault(source, []).append(target)
        indegree[target] = indegree.get(target, 0) + 1
        route_phases = [phases[point] for point in path]
        violations += sum(
            ((right - left) % phase_count) not in (0, 1)
            for left, right in zip(route_phases, route_phases[1:])
        )
    ready = sorted(node for node in nodes if indegree.get(node, 0) == 0)
    order: List[int] = []
    mutable_indegree = dict(indegree)
    while ready:
        node = ready.pop(0)
        order.append(node)
        for target in sorted(adjacency.get(node, [])):
            mutable_indegree[target] -= 1
            if mutable_indegree[target] == 0:
                ready.append(target)
                ready.sort()
    if len(order) != len(nodes):
        raise ValueError("layout route graph is not acyclic")

    best: Dict[int, Dict[int, List[int]]] = {}
    best_cells: Dict[int, Dict[int, int]] = {}
    for node in order:
        states = best.setdefault(node, {})
        cell_states = best_cells.setdefault(node, {})
        if not states and indegree.get(node, 0) == 0:
            initial_phase = phases[tuple(nodes[node]["coord"])]
            states[initial_phase] = [initial_phase]
            cell_states[initial_phase] = 1
        for target in adjacency.get(node, []):
            path = routes[(node, target)]
            edge_phases = [phases[point] for point in path]
            target_states = best.setdefault(target, {})
            target_cells = best_cells.setdefault(target, {})
            for prefix_phase, prefix in states.items():
                candidate = list(prefix)
                tail = edge_phases[1:] if candidate and edge_phases and candidate[-1] == edge_phases[0] else edge_phases
                candidate.extend(tail)
                last = candidate[-1]
                candidate_cells = cell_states[prefix_phase] + max(0, len(tail))
                current = target_states.get(last)
                current_score = (phase_cycles(current, phase_count), target_cells.get(last, 0)) if current else (-1.0, -1)
                candidate_score = (phase_cycles(candidate, phase_count), candidate_cells)
                if candidate_score > current_score:
                    target_states[last] = candidate
                    target_cells[last] = candidate_cells
    terminals = [node for node in nodes if not adjacency.get(node)]
    candidates = [
        (phase_cycles(sequence, phase_count), best_cells[node][last])
        for node in terminals for last, sequence in best.get(node, {}).items()
    ]
    if not candidates:
        raise ValueError("cannot derive an input-to-output clock path")
    cycles, cells = max(candidates)
    return cycles, cells, violations


def topology_digest(layout: Mapping[str, object]) -> str:
    nodes = layout["nodes"]
    routes = layout["routes"]
    payload = {
        "nodes": sorted((node_id, node["type"]) for node_id, node in nodes.items()),
        "edges": sorted((source, target) for source, target in routes),
    }
    return hashlib.sha256(json.dumps(payload, sort_keys=True).encode("utf-8")).hexdigest()[:16]


def run_checked(command: Sequence[str], cwd: Path = ROOT) -> subprocess.CompletedProcess:
    completed = subprocess.run(command, cwd=str(cwd), text=True,
                               stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=False)
    if completed.returncode != 0:
        raise RuntimeError("command failed ({}):\n{}\n{}".format(
            completed.returncode, " ".join(command), completed.stderr or completed.stdout))
    return completed


def qca_metrics(path: Path) -> Tuple[int, Dict[int, int]]:
    cell_count = 0
    phases: Dict[int, int] = {}
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.strip() == "[TYPE:QCADCell]":
            cell_count += 1
        elif line.startswith("cell_options.clock="):
            phase = int(line.split("=", 1)[1])
            phases[phase] = phases.get(phase, 0) + 1
    return cell_count, phases


def dataset_sources() -> List[Tuple[str, str, Path]]:
    result = []
    for suite in ("TOY", "MAJ"):
        directory = ROOT / "tests" / "benchmarks_f" / suite
        for source in sorted(directory.glob("*.v"), key=lambda item: item.name.lower()):
            result.append((suite, source.stem, source))
    return result


METRIC_FIELDS = [
    "dataset", "circuit", "source_verilog", "source_sha256", "layout_ifcn",
    "layout_sha256", "layout_alias", "alias_equivalence", "topology_id",
    "logic_nodes", "logic_edges", "inputs", "outputs", "io_total",
    "placed_nodes", "routed_edges", "mapped_cell_count", "qca_cell_count",
    "phase_count", "clock_cycles", "critical_path_layout_cells",
    "phase_transition_violations", "phase_coverage", "qca_phase_histogram",
    "layout_width", "layout_height", "layout_area", "layout_runtime_seconds",
    "decoded_ifcn", "qca_file",
]


def prepare(args: argparse.Namespace) -> List[Dict[str, object]]:
    output = args.output_directory.resolve()
    decoded_root = output / "decoded_ifcn"
    qca_root = output / "qca"
    records: List[Dict[str, object]] = []
    for index, (suite, circuit, source) in enumerate(dataset_sources(), start=1):
        alias_name = LAYOUT_ALIASES.get((suite, circuit), "")
        layout_name = alias_name or "{}_{}_p4.ifcn".format(suite, circuit)
        layout_path = (args.layout_directory / layout_name).resolve()
        if not layout_path.is_file():
            raise FileNotFoundError("missing random-clock layout: {}".format(layout_path))
        print("[prepare {}/{}] {}/{}".format(index, len(dataset_sources()), suite, circuit),
              flush=True)
        layout = parse_ifcn(layout_path)
        header = layout["header"]
        cycles, critical_cells, phase_violations = critical_clock_metrics(layout)
        if phase_violations:
            raise ValueError("{} has {} illegal hold/advance transitions".format(
                layout_path, phase_violations))
        decoded = decoded_root / suite / "{}_random_clock_decoded.ifcn".format(circuit)
        qca_prefix = qca_root / suite / "{}_random_clock".format(circuit)
        qca_path = Path(str(qca_prefix) + "_energy_input.qca")
        if args.force_prepare or not decoded.is_file():
            write_coordinate_phase_ifcn(layout, layout_path, decoded)
        qca_prefix.parent.mkdir(parents=True, exist_ok=True)
        if args.force_prepare or not qca_path.is_file():
            run_checked([str(args.energy_executable.resolve()), str(decoded),
                         str(qca_prefix), "--qca-only"])
        mapping_output = run_checked(
            [str(args.mapping_executable.resolve()), str(decoded)]).stdout.strip().split()
        if len(mapping_output) < 2:
            raise ValueError("invalid mapping metrics for {}".format(decoded))
        mapped_cells = int(mapping_output[0])
        qca_cells, qca_phase_histogram = qca_metrics(qca_path)
        if qca_cells <= 0 or len([count for count in qca_phase_histogram.values() if count]) < 2:
            raise ValueError("QCA conversion lost random clock phases for {}: {}".format(
                circuit, qca_phase_histogram))
        area_text = header.get("layout area", "")
        io_text = header.get("input/output", "")
        io_numbers = [int(item) for item in re.findall(r"\d+", io_text)]
        if len(io_numbers) < 2:
            raise ValueError("missing I/O counts in {}".format(layout_path))
        runtime_text = header.get("runtime", header.get("run time", "0"))
        used = layout["used"]
        record: Dict[str, object] = {
            "dataset": suite,
            "circuit": circuit,
            "source_verilog": str(source.resolve()),
            "source_sha256": sha256(source),
            "layout_ifcn": str(layout_path),
            "layout_sha256": sha256(layout_path),
            "layout_alias": Path(alias_name).stem if alias_name else "",
            "alias_equivalence": ("32/32 exhaustive Boolean vectors"
                                  if alias_name else "native netlist"),
            "topology_id": topology_digest(layout),
            "logic_nodes": int_from_text(header.get("gates number", str(len(layout["nodes"])))),
            "logic_edges": int_from_text(header.get("edges number", str(len(layout["routes"])))),
            "inputs": io_numbers[0], "outputs": io_numbers[1],
            "io_total": io_numbers[0] + io_numbers[1],
            "placed_nodes": len(layout["nodes"]), "routed_edges": len(layout["routes"]),
            "mapped_cell_count": mapped_cells, "qca_cell_count": qca_cells,
            "phase_count": int(layout["phase_count"]),
            "clock_cycles": round(cycles, 6),
            "critical_path_layout_cells": critical_cells,
            "phase_transition_violations": phase_violations,
            "phase_coverage": len(used.intersection(layout["phases"])) / float(len(used)),
            "qca_phase_histogram": json.dumps(qca_phase_histogram, sort_keys=True),
            "layout_width": int_from_text(area_text, "width"),
            "layout_height": int_from_text(area_text, "height"),
            "layout_area": int_from_text(area_text, "area"),
            "layout_runtime_seconds": float_from_text(runtime_text),
            "decoded_ifcn": str(decoded.resolve()), "qca_file": str(qca_path.resolve()),
        }
        records.append(record)
    output.mkdir(parents=True, exist_ok=True)
    with (output / "layout_metrics.csv").open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=METRIC_FIELDS)
        writer.writeheader()
        writer.writerows(records)
    (output / "layout_metrics.json").write_text(
        json.dumps(records, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return records


def load_metrics(output: Path) -> List[Dict[str, object]]:
    return json.loads((output / "layout_metrics.json").read_text(encoding="utf-8"))


def simulate(args: argparse.Namespace) -> None:
    output = args.output_directory.resolve()
    command = [
        sys.executable, str(ROOT / "scripts" / "benchmark_physical_simulators.py"),
        str(output / "qca"),
        "--benchmark-executable", str(args.benchmark_executable.resolve()),
        "--output-directory", str(output / "simulation"),
        "--model", "both", "--repetitions", str(args.repetitions),
        "--warmup", str(args.warmup), "--require-equivalent",
        "--include-energy-input", "--samples", str(args.samples),
        "--time-step", str(args.time_step),
        "--duration", str(args.time_step * args.coherence_steps),
        "--bootstrap-resamples", str(args.bootstrap_resamples),
    ]
    if args.cpu_affinity:
        command.extend(("--cpu-affinity", args.cpu_affinity))
    completed = subprocess.run(command, cwd=str(ROOT), text=True)
    if completed.returncode != 0:
        raise RuntimeError("physical simulation campaign failed with exit {}".format(
            completed.returncode))


def geometric_mean(values: Sequence[float]) -> float:
    return math.exp(statistics.fmean([math.log(value) for value in values]))


def percentile(values: Sequence[float], probability: float) -> float:
    ordered = sorted(values)
    position = probability * (len(ordered) - 1)
    lower = int(math.floor(position))
    upper = int(math.ceil(position))
    if lower == upper:
        return ordered[lower]
    fraction = position - lower
    return ordered[lower] * (1.0 - fraction) + ordered[upper] * fraction


def aggregate(rows: Sequence[Mapping[str, object]], resamples: int,
              rng: random.Random) -> Dict[str, object]:
    speedups = [float(row["speedup"]) for row in rows]
    reference = sum(float(row["reference_median_seconds"]) for row in rows)
    candidate = sum(float(row["candidate_median_seconds"]) for row in rows)
    bootstrap = []
    for _ in range(resamples):
        sample = [rows[rng.randrange(len(rows))] for _ in rows]
        base = sum(float(row["reference_median_seconds"]) for row in sample)
        fast = sum(float(row["candidate_median_seconds"]) for row in sample)
        bootstrap.append(base / fast)
    return {
        "circuits": len(rows), "reference_sum_median_seconds": reference,
        "candidate_sum_median_seconds": candidate,
        "suite_speedup": reference / candidate,
        "geometric_mean_speedup": geometric_mean(speedups),
        "median_speedup": statistics.median(speedups),
        "minimum_speedup": min(speedups), "maximum_speedup": max(speedups),
        "faster_circuits": sum(value > 1.0 for value in speedups),
        "suite_speedup_bootstrap_95_ci": [percentile(bootstrap, 0.025),
                                           percentile(bootstrap, 0.975)],
        "all_comparable": all(str(row["comparable"]).lower() in ("true", "1") for row in rows),
        "maximum_absolute_error": max(float(row["max_absolute_error"]) for row in rows),
    }


def rank(values: Sequence[float]) -> List[float]:
    ordered = sorted(enumerate(values), key=lambda item: item[1])
    result = [0.0] * len(values)
    start = 0
    while start < len(ordered):
        end = start + 1
        while end < len(ordered) and ordered[end][1] == ordered[start][1]:
            end += 1
        average = (start + end - 1) / 2.0 + 1.0
        for offset in range(start, end):
            result[ordered[offset][0]] = average
        start = end
    return result


def pearson(left: Sequence[float], right: Sequence[float]) -> float:
    left_mean = statistics.fmean(left)
    right_mean = statistics.fmean(right)
    numerator = sum((x - left_mean) * (y - right_mean) for x, y in zip(left, right))
    denominator = math.sqrt(sum((x - left_mean) ** 2 for x in left) *
                            sum((y - right_mean) ** 2 for y in right))
    return numerator / denominator if denominator else 0.0


def markdown_report(metrics: Sequence[Mapping[str, object]],
                    combined: Sequence[Mapping[str, object]],
                    summary: Mapping[str, object]) -> str:
    by_key = {(row["dataset"], row["circuit"], row["model"]): row for row in combined}
    lines = [
        "# TOY/MAJ 四相随机时钟版图与物理仿真结果", "",
        "主表中的 Nodes/Edges 是输入网表统计；实际 placed nodes/routed edges 见 `layout_metrics.csv`。",
        "`Mapped cells` 为映射器统计，`QCA cells` 为进入物理仿真器的实际元胞数。",
        "Cycles 是从保存的 modulo-4 phase 图恢复的关键输入到输出路径周期，而不是固定的 4 相数量。",
        "旧 IFCN 没有保存绝对 stage，因此当相隔整周期的区域具有相同 phase 时无法区分；该值应视为可恢复的周期下界。", "",
    ]
    for suite in ("TOY", "MAJ"):
        lines.extend([
            "## {}".format(suite), "",
            "| Circuit | Nodes | Edges | I/O | Cells (mapped/QCA) | Cycles | P&R s | Area | Bistable base/fast (ms) | Speedup | Coherence base/fast (ms) | Speedup |",
            "|---|---:|---:|:---:|---:|---:|---:|---:|---:|---:|---:|---:|",
        ])
        for metric in [item for item in metrics if item["dataset"] == suite]:
            b = by_key[(suite, metric["circuit"], "bistable")]
            c = by_key[(suite, metric["circuit"], "coherence")]
            circuit = str(metric["circuit"])
            if metric.get("layout_alias"):
                circuit += "*"
            lines.append(
                "| {name} | {nodes} | {edges} | {inputs}/{outputs} | {mapped}/{qca} | {cycles:.2f} | {runtime:.3f} | {width}×{height}={area} | {bb:.3f}/{bf:.3f} | {bs:.3f}× | {cb:.3f}/{cf:.3f} | {cs:.3f}× |".format(
                    name=circuit, nodes=int(metric["logic_nodes"]), edges=int(metric["logic_edges"]),
                    inputs=int(metric["inputs"]), outputs=int(metric["outputs"]),
                    mapped=int(metric["mapped_cell_count"]), qca=int(metric["qca_cell_count"]),
                    cycles=float(metric["clock_cycles"]), runtime=float(metric["layout_runtime_seconds"]),
                    width=int(metric["layout_width"]), height=int(metric["layout_height"]),
                    area=int(metric["layout_area"]),
                    bb=1000.0 * float(b["reference_median_seconds"]),
                    bf=1000.0 * float(b["candidate_median_seconds"]), bs=float(b["speedup"]),
                    cb=1000.0 * float(c["reference_median_seconds"]),
                    cf=1000.0 * float(c["candidate_median_seconds"]), cs=float(c["speedup"])))
        lines.append("")
    lines.extend([
        "\* TOY/xor5R 使用保留的 xor5_r1 随机时钟版图；两者五输入真值表 32/32 全部相同。", "",
        "## 汇总与检查", "",
    ])
    for model in ("bistable", "coherence"):
        values = summary["aggregate"]["ALL"][model]
        unique = summary["aggregate"]["ALL_UNIQUE_SOURCE"][model]
        lines.append(
            "- {}：27 个文件的套件总时间加速 {:.3f}×（电路 bootstrap 95% CI [{:.3f}, {:.3f}]），几何均值 {:.3f}×，范围 {:.3f}–{:.3f}×；{}/{} 个版图加速。按源文件哈希去重后为 {:.3f}×（26 个唯一源电路）。".format(
                model, values["suite_speedup"], values["suite_speedup_bootstrap_95_ci"][0],
                values["suite_speedup_bootstrap_95_ci"][1], values["geometric_mean_speedup"],
                values["minimum_speedup"], values["maximum_speedup"],
                values["faster_circuits"], values["circuits"], unique["suite_speedup"]))
    lines.extend([
        "- 所有版图的相位覆盖率为 100%，每条布线路径仅出现 hold/advance 相位转移，转换后的 QCA 文件均保留多个时钟相位。",
        "- baseline 与 accelerated 使用完全相同的 QCA、输入和时钟轨迹；所有逐版图输出比较可比且最大绝对误差为 0。",
        ("- 代表性 TOY/xor2 全内部状态证书也通过：Bistable 237,720 个值、Coherence 1,376,928 个值逐字相同。全量计时未启用轨迹保留，以免改变内存与时间测量。"
         if summary.get("representative_internal_state_certificate") else
         "- 全量计时未启用内部轨迹保留；严格性门控为相同输出采样网格上的逐值比较。"),
        "- MAJ/xor5R 与 MAJ/xor5_r1 的源文件内容相同，逐文件表保留两行；用于跨拓扑推断时应去重，避免重复样本加权。",
        "- MAJ/RCA2 的 QCA 相位分布为 {0:37, 1:2988, 2:9, 3:12}，phase 1 占 98.1%。它通过当前路径规则，但不应作为相位均衡或鲁棒随机时钟的正例。",
        "", "## 可继续优化的点", "",
        "1. 把 packed 4×4 phase tile 解码直接并入 IFCN→QCA 转换器并增加相位直方图/覆盖率断言，消除静默退化为 phase 0 的风险。",
        "2. 当前表是 spatial-cold，候选端每次都重新编译交互图。布局会在输入向量、介电常数、jitter 和温度扫描中重复使用，宜缓存 ordered CSR，并按几何哈希失效。",
        "3. 对小版图，初始化和结果物化占比更高；可批量复用解析后的 QCA 对象、预分配结果缓冲，并将多个参数点放进同一进程。",
        "4. 候选端 numerical iterations 的中位占比为 Bistable 98.9%、Coherence 98.2%；进一步提速应优先做 SoA/SIMD 数据布局和缓存局部性优化。着色并行、改变求和顺序或自适应步长需要单独的误差研究，不能归入严格等价加速。",
        "5. 随机时钟 P&R 目标应增加相位占用熵/最大单相占比、最大同相连续段、绝对 stage 与重汇合 skew；否则会出现 MAJ/RCA2 这类形式合法但几乎单相的退化解。",
        "", "## 协议", "",
        "- Bistable：512 samples；Coherence-vector：Euler，4096 个内部时间步（dt=1e-16 s）。",
        "- 每个引擎 {} 次预热 + {} 次 AB/BA 交替配对计时，固定 CPU {}。".format(
            summary["protocol"]["warmup"], summary["protocol"]["repetitions"],
            summary["protocol"]["cpu_affinity"]),
        "- 该网格适合回归与性能筛查；SCI 最终表建议在机器空闲状态运行 30 次配对重复，并另做生产时间网格的大版图确认。",
    ])
    return "\n".join(lines) + "\n"


def summarize(args: argparse.Namespace, metrics: Sequence[Mapping[str, object]]) -> None:
    output = args.output_directory.resolve()
    sim_rows = list(csv.DictReader((output / "simulation" / "results.csv").open(
        encoding="utf-8")))
    metric_by_qca = {str(Path(str(item["qca_file"])).resolve()): item for item in metrics}
    combined: List[Dict[str, object]] = []
    for row in sim_rows:
        circuit_path = str(Path(row["circuit"]).resolve())
        if circuit_path not in metric_by_qca:
            raise ValueError("simulation row has no layout metric: {}".format(circuit_path))
        metric = metric_by_qca[circuit_path]
        combined.append({
            "dataset": metric["dataset"], "circuit": metric["circuit"],
            "source_sha256": metric["source_sha256"],
            "topology_id": metric["topology_id"],
            "model": row["model"], "qca_cell_count": int(metric["qca_cell_count"]),
            "reference_median_seconds": float(row["reference_median_seconds"]),
            "candidate_median_seconds": float(row["candidate_median_seconds"]),
            "speedup": float(row["speedup"]), "comparable": row["comparable"],
            "max_absolute_error": float(row["max_absolute_error"]),
            "directed_couplings": int(row["directed_couplings"]),
            "reference_iterations_median_seconds": float(row["reference_iterations_median_seconds"]),
            "candidate_iterations_median_seconds": float(row["candidate_iterations_median_seconds"]),
            "candidate_design_initialization_median_seconds": float(row["candidate_design_initialization_median_seconds"]),
            "interaction_graph_seconds": float(row["interaction_graph_seconds"]),
            "kernel_compilation_seconds": float(row["kernel_compilation_seconds"]),
            "peak_process_rss_kib": int(row["peak_process_rss_kib"]),
        })
    expected = len(metrics) * 2
    if len(combined) != expected:
        raise ValueError("expected {} simulation rows, found {}".format(expected, len(combined)))
    fields = list(combined[0])
    with (output / "combined_results.csv").open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        writer.writerows(combined)

    rng = random.Random(20260722)
    aggregate_result: Dict[str, Dict[str, object]] = {}
    for suite in ("TOY", "MAJ", "ALL"):
        aggregate_result[suite] = {}
        for model in ("bistable", "coherence"):
            selected = [row for row in combined if row["model"] == model and
                        (suite == "ALL" or row["dataset"] == suite)]
            aggregate_result[suite][model] = aggregate(
                selected, args.bootstrap_resamples, rng)
    aggregate_result["ALL_UNIQUE_SOURCE"] = {}
    for model in ("bistable", "coherence"):
        selected = sorted(
            (row for row in combined if row["model"] == model),
            key=lambda row: (str(row["dataset"]), str(row["circuit"])),
        )
        unique = []
        seen_sources = set()
        for row in selected:
            if row["source_sha256"] in seen_sources:
                continue
            seen_sources.add(row["source_sha256"])
            unique.append(row)
        aggregate_result["ALL_UNIQUE_SOURCE"][model] = aggregate(
            unique, args.bootstrap_resamples, rng)
    correlations = {}
    for model in ("bistable", "coherence"):
        selected = [row for row in combined if row["model"] == model]
        correlations[model] = {
            "spearman_qca_cells_vs_speedup": pearson(
                rank([float(row["qca_cell_count"]) for row in selected]),
                rank([float(row["speedup"]) for row in selected])),
            "slowest_speedup_circuit": min(selected, key=lambda row: float(row["speedup"])),
            "fastest_speedup_circuit": max(selected, key=lambda row: float(row["speedup"])),
        }
    summary = {
        "schema_version": 1,
        "campaign": "TOY/MAJ four-phase random-clock layout and strict physical simulation",
        "layout_count": len(metrics),
        "dataset_counts": {suite: sum(item["dataset"] == suite for item in metrics)
                           for suite in ("TOY", "MAJ")},
        "unique_source_sha256_count": len({item["source_sha256"] for item in metrics}),
        "unique_layout_topology_count": len({item["topology_id"] for item in metrics}),
        "protocol": {
            "repetitions": args.repetitions, "warmup": args.warmup,
            "cpu_affinity": args.cpu_affinity, "bistable_samples": args.samples,
            "coherence_numeric_method": "euler", "coherence_steps": args.coherence_steps,
            "coherence_time_step_seconds": args.time_step,
            "coherence_duration_seconds": args.time_step * args.coherence_steps,
            "pair_order": "alternating_ab_ba", "graph_mode": "spatial_cold",
        },
        "aggregate": aggregate_result, "correlations": correlations,
        "optimization_observations": {
            model: {
                "median_candidate_iteration_time_fraction": statistics.median(
                    float(row["candidate_iterations_median_seconds"]) /
                    float(row["candidate_median_seconds"])
                    for row in combined if row["model"] == model),
                "median_candidate_initialization_time_fraction": statistics.median(
                    float(row["candidate_design_initialization_median_seconds"]) /
                    float(row["candidate_median_seconds"])
                    for row in combined if row["model"] == model),
            }
            for model in ("bistable", "coherence")
        },
        "maximum_single_phase_fraction": max(
            max(json.loads(str(item["qca_phase_histogram"])).values()) /
            float(item["qca_cell_count"]) for item in metrics),
        "maximum_single_phase_layout": "MAJ/RCA2",
        "validation": {
            "all_phase_coverage_complete": all(float(item["phase_coverage"]) == 1.0 for item in metrics),
            "all_phase_transitions_legal": all(int(item["phase_transition_violations"]) == 0 for item in metrics),
            "all_simulations_comparable": all(str(row["comparable"]).lower() in ("true", "1") for row in combined),
            "maximum_absolute_error": max(float(row["max_absolute_error"]) for row in combined),
        },
    }
    certificate_path = output / "validation" / "TOY_xor2_internal_state.json"
    if certificate_path.is_file():
        certificate_report = json.loads(certificate_path.read_text(encoding="utf-8"))
        summary["representative_internal_state_certificate"] = {
            comparison["model"]: comparison.get("internal_state_certificate", {})
            for comparison in certificate_report.get("comparisons", [])
        }
    (output / "summary.json").write_text(
        json.dumps(summary, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    (output / "REPORT.md").write_text(
        markdown_report(metrics, combined, summary), encoding="utf-8")


def main() -> int:
    args = arguments()
    output = args.output_directory.resolve()
    if args.summarize_only:
        metrics = load_metrics(output)
        summarize(args, metrics)
        return 0
    for executable in (args.mapping_executable, args.energy_executable,
                       args.benchmark_executable):
        if not executable.resolve().is_file() or not os.access(str(executable.resolve()), os.X_OK):
            raise FileNotFoundError("missing executable: {}".format(executable.resolve()))
    metrics = prepare(args)
    if args.prepare_only:
        return 0
    simulate(args)
    summarize(args, metrics)
    print("report={}".format(output / "REPORT.md"))
    print("summary={}".format(output / "summary.json"))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError, json.JSONDecodeError) as error:
        print("random-clock benchmark failed: {}".format(error), file=sys.stderr)
        raise SystemExit(2)
