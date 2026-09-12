#!/usr/bin/env python3
"""Diagnose missing routes from batch IFCN artifacts and aggregate evidence."""

import argparse
import csv
import json
import os
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path


GCN_RL_ROOT = Path(__file__).resolve().parents[1]
PROJECT_ROOT = GCN_RL_ROOT.parents[1]
ALGORITHM_ROOT = GCN_RL_ROOT / "src" / "algorithm"
if str(ALGORITHM_ROOT) not in sys.path:
    sys.path.insert(0, str(ALGORITHM_ROOT))

from src.circuit_parse import CircuitParser  # noqa: E402


NODE_RE = re.compile(r"^(\d+),.*?,\s*\((-?\d+),(-?\d+)\);$")
PATH_RE = re.compile(r"^\((\d+),(\d+)\):\s*(.*);$")
COORD_RE = re.compile(r"\((-?\d+),(-?\d+)\)")


def parse_args():
    parser = argparse.ArgumentParser(
        description="Explain failed routes in a full-layout batch result."
    )
    parser.add_argument("--layout-results", required=True)
    parser.add_argument("--output-dir", default="")
    return parser.parse_args()


def read_json(path):
    try:
        with Path(path).open("r", encoding="utf-8") as handle:
            return json.load(handle)
    except (OSError, TypeError, ValueError):
        return None


def parse_ifcn(path):
    node_positions = {}
    routed_paths = {}
    try:
        lines = Path(path).read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError:
        return node_positions, routed_paths
    for raw_line in lines:
        line = raw_line.strip()
        node_match = NODE_RE.match(line)
        if node_match:
            node_positions[int(node_match.group(1))] = (
                int(node_match.group(2)),
                int(node_match.group(3)),
            )
            continue
        path_match = PATH_RE.match(line)
        if path_match:
            routed_paths[(int(path_match.group(1)), int(path_match.group(2)))] = [
                (int(x), int(y))
                for x, y in COORD_RE.findall(path_match.group(3))
            ]
    return node_positions, routed_paths


def occupied_cells(node_positions, routed_paths):
    node_cells = defaultdict(set)
    for node_id, coord in node_positions.items():
        node_cells[coord].add(int(node_id))
    wire_cells = defaultdict(set)
    for edge, path in routed_paths.items():
        for coord in path[1:-1]:
            wire_cells[coord].add(edge)
    return node_cells, wire_cells


def coord_is_blocked(coord, allowed_nodes, node_cells, wire_cells):
    return bool(
        (node_cells.get(coord, set()) - set(allowed_nodes))
        or wire_cells.get(coord)
    )


def classify_missing_edge(
    edge,
    node_positions,
    node_cells,
    wire_cells,
    fanin_count,
    fanout_count,
):
    src, dst = edge
    src_x, src_y = node_positions[src]
    dst_x, dst_y = node_positions[dst]
    launch = (src_x, src_y + 1)
    sink_left = (dst_x - 1, dst_y)
    sink_top = (dst_x, dst_y - 1)
    launch_blocked = coord_is_blocked(
        launch, {src, dst}, node_cells, wire_cells
    ) and launch != (dst_x, dst_y)
    left_blocked = coord_is_blocked(
        sink_left, {src, dst}, node_cells, wire_cells
    ) and sink_left != (src_x, src_y)
    top_blocked = coord_is_blocked(
        sink_top, {src, dst}, node_cells, wire_cells
    ) and sink_top != (src_x, src_y)

    min_x, max_x = sorted((src_x, dst_x))
    min_y, max_y = sorted((src_y, dst_y))
    rectangle_cells = max(1, (max_x - min_x + 1) * (max_y - min_y + 1))
    occupied_in_rectangle = sum(
        1
        for x in range(min_x, max_x + 1)
        for y in range(min_y, max_y + 1)
        if (x, y) in node_cells or (x, y) in wire_cells
    )
    corridor_density = occupied_in_rectangle / rectangle_cells

    causes = []
    if launch_blocked:
        causes.append("source_escape_blocked")
    if left_blocked and top_blocked:
        causes.append("sink_ports_blocked")
    if int(fanin_count.get(dst, 0)) >= 3:
        causes.append("high_fanin_port_pressure")
    if int(fanout_count.get(src, 0)) >= 3:
        causes.append("high_fanout_trunk_pressure")
    if corridor_density >= 0.20:
        causes.append("monotone_corridor_congestion")
    if not causes:
        causes.append("route_order_contention")

    return {
        "src": int(src),
        "dst": int(dst),
        "src_coord": [int(src_x), int(src_y)],
        "dst_coord": [int(dst_x), int(dst_y)],
        "horizontal_span": int(dst_x - src_x),
        "vertical_span": int(dst_y - src_y),
        "launch_blocked": bool(launch_blocked),
        "left_sink_port_blocked": bool(left_blocked),
        "top_sink_port_blocked": bool(top_blocked),
        "src_fanout_count": int(fanout_count.get(src, 0)),
        "dst_fanin_count": int(fanin_count.get(dst, 0)),
        "corridor_density": float(corridor_density),
        "causes": causes,
        "primary_cause": causes[0],
    }


def diagnose_record(record, benchmark_root, result_root):
    relative = Path(record["benchmark"])
    output_dir = result_root / "circuits" / relative.with_suffix("")
    if record.get("status") == "process_error":
        error = str(record.get("error", ""))
        return_code = int(record.get("return_code", 0) or 0)
        if "Unsupported Verilog shape for parser-safe fallback" in error:
            cause = "unsupported_verilog_parser_fallback"
        elif return_code < 0:
            cause = "native_process_crash"
        else:
            cause = "layout_process_error"
        return {
            "benchmark": str(relative),
            "status": "process_error",
            "failed_edge_count": None,
            "primary_cause": cause,
            "cause_counts": {cause: 1},
            "return_code": return_code,
            "error": error,
            "missing_edges": [],
        }
    if record.get("status") == "timeout":
        return {
            "benchmark": str(relative),
            "status": "timeout",
            "failed_edge_count": None,
            "primary_cause": "state_space_timeout",
            "cause_counts": {"state_space_timeout": 1},
            "missing_edges": [],
        }
    if record.get("status") != "route_failed":
        return None

    summary = read_json(
        output_dir / (relative.stem + "_normal_graph_draw_summary.json")
    )
    ifcn_candidates = sorted(output_dir.glob("*_normal_graph_draw.ifcn"))
    if not ifcn_candidates:
        failed_edges = (
            summary.get("failed_edges", [])
            if isinstance(summary, dict) else []
        )
        if failed_edges:
            circuit = CircuitParser(str(benchmark_root / relative), parse_mode="auto")
            expected_edges = {
                (int(src), int(dst)) for src, dst in circuit.effective_edges
            }
            fanin_count = Counter(int(dst) for _, dst in expected_edges)
            fanout_count = Counter(int(src) for src, _ in expected_edges)
            details = []
            for item in failed_edges:
                src = int(item["src"])
                dst = int(item["dst"])
                causes = ["monotone_capacity_exhaustion"]
                if int(fanin_count.get(dst, 0)) >= 3:
                    causes.append("high_fanin_port_pressure")
                if int(fanout_count.get(src, 0)) >= 3:
                    causes.append("high_fanout_trunk_pressure")
                details.append(
                    {
                        "src": src,
                        "dst": dst,
                        "direction": [
                            int(value) for value in item.get("direction", [])
                        ],
                        "src_fanout_count": int(fanout_count.get(src, 0)),
                        "dst_fanin_count": int(fanin_count.get(dst, 0)),
                        "causes": causes,
                        "primary_cause": "monotone_capacity_exhaustion",
                    }
                )
            cause_counts = Counter(
                cause for detail in details for cause in detail["causes"]
            )
            return {
                "benchmark": str(relative),
                "status": "route_failed",
                "failed_edge_count": len(details),
                "reconstructed_missing_edge_count": len(details),
                "primary_cause": "monotone_capacity_exhaustion",
                "cause_counts": dict(sorted(cause_counts.items())),
                "missing_edges": details,
            }
        return {
            "benchmark": str(relative),
            "status": "route_failed",
            "failed_edge_count": record.get("failed_edge_count"),
            "primary_cause": "missing_ifcn_diagnostic",
            "cause_counts": {"missing_ifcn_diagnostic": 1},
            "missing_edges": [],
        }

    node_positions, routed_paths = parse_ifcn(ifcn_candidates[0])
    circuit = CircuitParser(str(benchmark_root / relative), parse_mode="auto")
    expected_edges = {
        (int(src), int(dst)) for src, dst in circuit.effective_edges
    }
    missing_edges = sorted(expected_edges - set(routed_paths))
    node_cells, wire_cells = occupied_cells(node_positions, routed_paths)
    fanin_count = Counter(int(dst) for _, dst in expected_edges)
    fanout_count = Counter(int(src) for src, _ in expected_edges)
    details = []
    for edge in missing_edges:
        if edge[0] not in node_positions or edge[1] not in node_positions:
            details.append(
                {
                    "src": edge[0],
                    "dst": edge[1],
                    "causes": ["missing_endpoint_placement"],
                    "primary_cause": "missing_endpoint_placement",
                }
            )
            continue
        details.append(
            classify_missing_edge(
                edge,
                node_positions,
                node_cells,
                wire_cells,
                fanin_count,
                fanout_count,
            )
        )
    cause_counts = Counter(
        cause for detail in details for cause in detail.get("causes", [])
    )
    primary_counts = Counter(
        detail.get("primary_cause", "unknown") for detail in details
    )
    primary_cause = (
        primary_counts.most_common(1)[0][0]
        if primary_counts else "unknown_route_failure"
    )
    return {
        "benchmark": str(relative),
        "status": "route_failed",
        "failed_edge_count": int(record.get("failed_edge_count", len(details)) or 0),
        "reconstructed_missing_edge_count": int(len(details)),
        "primary_cause": primary_cause,
        "cause_counts": dict(sorted(cause_counts.items())),
        "missing_edges": details,
    }


def write_outputs(output_dir, diagnostics, source_path):
    output_dir.mkdir(parents=True, exist_ok=True)
    aggregate_causes = Counter()
    primary_causes = Counter()
    for item in diagnostics:
        aggregate_causes.update(item.get("cause_counts", {}))
        primary_causes[item.get("primary_cause", "unknown")] += 1
    payload = {
        "source_layout_results": str(source_path),
        "diagnosed_circuit_count": len(diagnostics),
        "primary_circuit_cause_counts": dict(sorted(primary_causes.items())),
        "edge_cause_counts": dict(sorted(aggregate_causes.items())),
        "circuits": diagnostics,
    }
    with (output_dir / "failure_diagnostics.json").open(
        "w", encoding="utf-8"
    ) as handle:
        json.dump(payload, handle, ensure_ascii=False, indent=2)

    with (output_dir / "failure_diagnostics.csv").open(
        "w", encoding="utf-8", newline=""
    ) as handle:
        writer = csv.DictWriter(
            handle,
            fieldnames=(
                "benchmark",
                "status",
                "failed_edge_count",
                "reconstructed_missing_edge_count",
                "primary_cause",
                "cause_counts",
            ),
            extrasaction="ignore",
        )
        writer.writeheader()
        for item in diagnostics:
            row = dict(item)
            row["cause_counts"] = json.dumps(
                item.get("cause_counts", {}), sort_keys=True
            )
            writer.writerow(row)

    markdown = [
        "# 布线失败原因诊断",
        "",
        "- 诊断电路数：{}".format(len(diagnostics)),
        "- 逐边原因计数：{}".format(
            ", ".join(
                "{}={}".format(key, value)
                for key, value in aggregate_causes.most_common()
            )
        ),
        "",
        "| Circuit | Status | Failed | Primary cause |",
        "|---|---|---:|---|",
    ]
    for item in diagnostics:
        markdown.append(
            "| {} | {} | {} | {} |".format(
                item["benchmark"],
                item["status"],
                item.get("failed_edge_count", ""),
                item.get("primary_cause", ""),
            )
        )
    (output_dir / "failure_diagnostics.md").write_text(
        "\n".join(markdown) + "\n", encoding="utf-8"
    )


def main():
    args = parse_args()
    source_path = Path(args.layout_results).resolve()
    results = read_json(source_path)
    if not isinstance(results, dict):
        raise ValueError("invalid results JSON: {}".format(source_path))
    benchmark_root = Path(results["benchmark_root"]).resolve()
    result_root = source_path.parent
    output_dir = (
        Path(args.output_dir).resolve()
        if args.output_dir else result_root / "failure_analysis"
    )
    diagnostics = []
    for record in results.get("records", []):
        item = diagnose_record(record, benchmark_root, result_root)
        if item is not None:
            diagnostics.append(item)
    write_outputs(output_dir, diagnostics, source_path)
    print("DIAGNOSTIC_DIR {}".format(output_dir))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
