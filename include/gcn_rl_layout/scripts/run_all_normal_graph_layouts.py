#!/usr/bin/env python3
"""Run the production normal-graph placement/router on every Verilog benchmark.

Each circuit runs in an isolated process.  A timeout, parser failure, crash, or
illegal routing result is recorded as data instead of aborting the campaign.
The output directory is self-contained: per-circuit artifacts/logs plus CSV,
JSON, Markdown, and standalone LaTeX aggregate reports.
"""

import argparse
import csv
import datetime as dt
import json
import os
import platform
import re
import signal
import subprocess
import sys
import time
from collections import Counter, defaultdict
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path


GCN_RL_ROOT = Path(__file__).resolve().parents[1]
PROJECT_ROOT = GCN_RL_ROOT.parents[1]
DEFAULT_BENCHMARK_ROOT = PROJECT_ROOT / "tests" / "benchmarks_f"
LAYOUT_SCRIPT = GCN_RL_ROOT / "src" / "algorithm" / "main" / "test_normal_graph_draw.py"
DEFAULT_OUTPUT_ROOT = (
    GCN_RL_ROOT
    / "results"
    / ("graphviz_sifting_all_layouts_" + dt.datetime.now().strftime("%Y%m%d_%H%M%S"))
)
RESULT_FIELDS = (
    "index",
    "suite",
    "circuit",
    "benchmark",
    "status",
    "return_code",
    "timed_out",
    "source_bytes",
    "nodes",
    "edges",
    "layers",
    "crossings",
    "graphviz_seconds",
    "sifting_seconds",
    "sifting_evaluations",
    "graphviz_fallback",
    "seed",
    "seed_attempt_count",
    "failed_edge_count",
    "clock_template_ok",
    "clock_template_conflicts",
    "route_expansion_round_count",
    "route_expansion_exhausted",
    "route_incompatibility_reason",
    "high_fanin_node_count",
    "contraction_step_count",
    "contraction_evaluations",
    "contraction_global_evaluations",
    "contraction_recursive_evaluations",
    "contraction_empty_line_evaluations",
    "pre_contraction_area",
    "contraction_area_reduction",
    "contraction_area_reduction_percent",
    "pre_contraction_used_cell_count",
    "used_cell_count",
    "contraction_used_cell_reduction",
    "contraction_used_cell_reduction_percent",
    "pre_contraction_routed_wire_cells",
    "routed_wire_cells",
    "contraction_routed_wire_cell_reduction",
    "width",
    "height",
    "area",
    "layout_runtime_sec",
    "wall_time_sec",
    "parser_safe_generated",
    "ifcn_exists",
    "encoded_ifcn_exists",
    "output_dir",
    "log_path",
    "error",
)


def parse_args():
    parser = argparse.ArgumentParser(
        description="Run Graphviz+sifting normal-graph placement/routing on every .v benchmark."
    )
    parser.add_argument("--benchmark-root", default=str(DEFAULT_BENCHMARK_ROOT))
    parser.add_argument("--output-root", default=str(DEFAULT_OUTPUT_ROOT))
    parser.add_argument("--timeout-sec", type=float, default=300.0)
    parser.add_argument(
        "--jobs",
        type=int,
        default=1,
        help="Number of isolated circuit processes to run concurrently.",
    )
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--seed-retries", type=int, default=0)
    parser.add_argument("--compact-iters", type=int, default=64)
    parser.add_argument("--max-expansion-rounds", type=int, default=96)
    parser.add_argument("--route-expansion-timeout-sec", type=float, default=120.0)
    parser.add_argument("--contraction-evaluations", type=int, default=128)
    parser.add_argument("--contraction-timeout-sec", type=float, default=60.0)
    parser.add_argument(
        "--recursive-contraction-evaluations",
        type=int,
        default=0,
        help="Internal recursive-stage budget; 0 reuses --contraction-evaluations.",
    )
    parser.add_argument(
        "--recursive-contraction-timeout-sec",
        type=float,
        default=0.0,
        help="Internal recursive-stage time budget; 0 reuses --contraction-timeout-sec.",
    )
    parser.add_argument(
        "--empty-line-contraction-evaluations",
        type=int,
        default=0,
        help="Blank-row/column deletion budget; 0 reuses --contraction-evaluations.",
    )
    parser.add_argument(
        "--empty-line-contraction-timeout-sec",
        type=float,
        default=0.0,
        help="Blank-row/column deletion time budget; 0 reuses --contraction-timeout-sec.",
    )
    parser.add_argument("--graphviz-timeout-sec", type=float, default=60.0)
    parser.add_argument("--sift-timeout-sec", type=float, default=20.0)
    parser.add_argument("--sift-evaluations", type=int, default=200000)
    parser.add_argument(
        "--include-generated-sources",
        action="store_true",
        help="Include nested *_source_generate helper inputs. They are excluded by default.",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Rerun circuits that already have a per-circuit record.json.",
    )
    parser.add_argument(
        "--rerun-status",
        action="append",
        default=[],
        choices=(
            "pass",
            "timeout",
            "process_error",
            "missing_summary",
            "route_failed",
            "template_failed",
            "missing_ifcn",
        ),
        help="Rerun only existing records with this status; may be repeated.",
    )
    parser.add_argument(
        "--selection-results",
        default="",
        help=(
            "Optional prior layout_results.json used to select a cohort without "
            "copying its artifacts (default selected status: pass)."
        ),
    )
    parser.add_argument(
        "--selection-status",
        action="append",
        default=[],
        help="Status selected from --selection-results; may be repeated.",
    )
    return parser.parse_args()


def discover_benchmarks(root, include_generated_sources):
    paths = []
    for path in sorted(root.rglob("*.v")):
        relative = path.relative_to(root)
        if not include_generated_sources and any(
            part.endswith("_source_generate") for part in relative.parts[:-1]
        ):
            continue
        paths.append(path.resolve())
    # Finish the many small/medium circuits first; very large EPFL cases then
    # consume their explicit timeout without hiding broad coverage progress.
    paths.sort(key=lambda path: (path.stat().st_size, str(path)))
    return paths


def circuit_output_dir(output_root, benchmark_root, benchmark):
    relative = benchmark.relative_to(benchmark_root).with_suffix("")
    return output_root / "circuits" / relative


def parse_log_metrics(text):
    metrics = {
        "nodes": "",
        "edges": "",
        "layers": "",
        "crossings": "",
        "graphviz_seconds": "",
        "sifting_seconds": "",
        "sifting_evaluations": "",
        "graphviz_fallback": "",
    }
    matches = list(re.finditer(r"nodes number:\s*(\d+)", text))
    if matches:
        metrics["nodes"] = int(matches[-1].group(1))
    matches = list(re.finditer(r"edges number:\s*(\d+)", text))
    if matches:
        metrics["edges"] = int(matches[-1].group(1))
    match = re.search(r"total layers:\s*(\d+)", text)
    if match:
        metrics["layers"] = int(match.group(1))
    order_matches = list(
        re.finditer(
            r"\[Graphviz\+sifting\] crossings=(\d+).*?dot=([0-9.]+)s, "
            r"sift=([0-9.]+)s, evaluations=(\d+), fallback=(True|False)",
            text,
        )
    )
    if order_matches:
        match = order_matches[-1]
        metrics.update(
            {
                "crossings": int(match.group(1)),
                "graphviz_seconds": float(match.group(2)),
                "sifting_seconds": float(match.group(3)),
                "sifting_evaluations": int(match.group(4)),
                "graphviz_fallback": match.group(5) == "True",
            }
        )
    return metrics


def read_json(path):
    try:
        with path.open("r", encoding="utf-8") as handle:
            return json.load(handle)
    except (OSError, ValueError, TypeError):
        return None


def selected_seed_attempt(summary):
    selected_seed = int(summary.get("seed", -1))
    for attempt in summary.get("seed_attempts", []):
        if int(attempt.get("seed", -2)) == selected_seed:
            return attempt
    attempts = summary.get("seed_attempts", [])
    return attempts[-1] if attempts else {}


def contraction_metrics(summary):
    if not isinstance(summary, dict):
        return {
            "pre_contraction_area": "",
            "contraction_area_reduction": "",
            "contraction_area_reduction_percent": "",
            "pre_contraction_used_cell_count": "",
            "used_cell_count": "",
            "contraction_used_cell_reduction": "",
            "contraction_used_cell_reduction_percent": "",
            "pre_contraction_routed_wire_cells": "",
            "routed_wire_cells": "",
            "contraction_routed_wire_cell_reduction": "",
        }
    final_area = int(summary.get("area", 0) or 0)
    history = summary.get("contraction_history", []) or []
    if history:
        pre_area = int(history[0].get("old_width", 0)) * int(
            history[0].get("old_height", 0)
        )
    else:
        pre_area = final_area
    reduction = pre_area - final_area
    pre_used = int(summary.get("pre_contraction_used_cell_count", 0) or 0)
    final_used = int(summary.get("used_cell_count", pre_used) or 0)
    pre_wire = int(summary.get("pre_contraction_routed_wire_cells", 0) or 0)
    final_wire = int(summary.get("routed_wire_cells", pre_wire) or 0)
    return {
        "pre_contraction_area": int(pre_area),
        "contraction_area_reduction": int(reduction),
        "contraction_area_reduction_percent": (
            100.0 * reduction / pre_area if pre_area > 0 else 0.0
        ),
        "pre_contraction_used_cell_count": pre_used,
        "used_cell_count": final_used,
        "contraction_used_cell_reduction": pre_used - final_used,
        "contraction_used_cell_reduction_percent": (
            100.0 * (pre_used - final_used) / pre_used if pre_used > 0 else 0.0
        ),
        "pre_contraction_routed_wire_cells": pre_wire,
        "routed_wire_cells": final_wire,
        "contraction_routed_wire_cell_reduction": pre_wire - final_wire,
    }


def classify(summary, return_code, timed_out):
    if timed_out:
        return "timeout"
    if return_code != 0:
        return "process_error"
    if not isinstance(summary, dict):
        return "missing_summary"
    attempt = selected_seed_attempt(summary)
    if int(summary.get("failed_edge_count", 0)) != 0:
        return "route_failed"
    if not bool(attempt.get("clock_template_ok", False)):
        return "template_failed"
    if not Path(summary.get("ifcn", "")).is_file():
        return "missing_ifcn"
    return "pass"


def terminate_process_group(process):
    try:
        os.killpg(process.pid, signal.SIGTERM)
    except (ProcessLookupError, PermissionError):
        return
    try:
        process.wait(timeout=5)
    except subprocess.TimeoutExpired:
        try:
            os.killpg(process.pid, signal.SIGKILL)
        except (ProcessLookupError, PermissionError):
            pass
        process.wait()


def run_one(index, total, args, benchmark_root, output_root, benchmark):
    relative = benchmark.relative_to(benchmark_root)
    suite = relative.parts[0] if len(relative.parts) > 1 else "root"
    output_dir = circuit_output_dir(output_root, benchmark_root, benchmark)
    output_dir.mkdir(parents=True, exist_ok=True)
    record_path = output_dir / "record.json"
    if record_path.is_file() and not args.force:
        record = read_json(record_path)
        rerun_statuses = set(args.rerun_status or [])
        if (
            isinstance(record, dict)
            and record.get("status") not in rerun_statuses
        ):
            summary = read_json(
                output_dir / (benchmark.stem + "_normal_graph_draw_summary.json")
            )
            record.update(contraction_metrics(summary))
            with record_path.open("w", encoding="utf-8") as handle:
                json.dump(record, handle, ensure_ascii=False, indent=2)
            print(
                "[{}/{}] resume {}: {}".format(index, total, relative, record.get("status")),
                flush=True,
            )
            return record

    log_path = output_dir / "run.log"
    command = [
        sys.executable,
        str(LAYOUT_SCRIPT),
        "--benchmark",
        str(benchmark),
        "--output-dir",
        str(output_dir),
        "--seed",
        str(args.seed),
        "--seed-retries",
        str(args.seed_retries),
        "--compact-iters",
        str(args.compact_iters),
        "--max-expansion-rounds",
        str(args.max_expansion_rounds),
        "--route-expansion-timeout-sec",
        str(args.route_expansion_timeout_sec),
        "--skip-figures",
        "--skip-latex",
        "--skip-stage-snapshots",
        "--skip-training-curve",
    ]
    env = os.environ.copy()
    env.update(
        {
            "MPLBACKEND": "Agg",
            "PYTHONHASHSEED": "0",
            "PYTHONUNBUFFERED": "1",
            "IFCN_GRAPHVIZ_TIMEOUT": str(args.graphviz_timeout_sec),
            "IFCN_SIFT_TIMEOUT": str(args.sift_timeout_sec),
            "IFCN_SIFT_EVALUATIONS": str(args.sift_evaluations),
            "IFCN_CONTRACTION_EVALUATIONS": str(args.contraction_evaluations),
            "IFCN_CONTRACTION_TIMEOUT": str(args.contraction_timeout_sec),
            "IFCN_RECURSIVE_CONTRACTION_EVALUATIONS": str(
                args.recursive_contraction_evaluations
                if args.recursive_contraction_evaluations > 0
                else args.contraction_evaluations
            ),
            "IFCN_RECURSIVE_CONTRACTION_TIMEOUT": str(
                args.recursive_contraction_timeout_sec
                if args.recursive_contraction_timeout_sec > 0
                else args.contraction_timeout_sec
            ),
            "IFCN_EMPTY_LINE_CONTRACTION_EVALUATIONS": str(
                args.empty_line_contraction_evaluations
                if args.empty_line_contraction_evaluations > 0
                else args.contraction_evaluations
            ),
            "IFCN_EMPTY_LINE_CONTRACTION_TIMEOUT": str(
                args.empty_line_contraction_timeout_sec
                if args.empty_line_contraction_timeout_sec > 0
                else args.contraction_timeout_sec
            ),
        }
    )
    summary_path = output_dir / (benchmark.stem + "_normal_graph_draw_summary.json")
    try:
        previous_summary_mtime_ns = summary_path.stat().st_mtime_ns
    except OSError:
        previous_summary_mtime_ns = None
    started = time.perf_counter()
    timed_out = False
    return_code = -1
    print("[{}/{}] run {}".format(index, total, relative), flush=True)
    with log_path.open("w", encoding="utf-8") as log_handle:
        log_handle.write("COMMAND: {}\n".format(" ".join(command)))
        log_handle.flush()
        process = subprocess.Popen(
            command,
            cwd=str(PROJECT_ROOT),
            env=env,
            stdout=log_handle,
            stderr=subprocess.STDOUT,
            text=True,
            start_new_session=True,
        )
        try:
            return_code = process.wait(timeout=max(1.0, float(args.timeout_sec)))
        except subprocess.TimeoutExpired:
            timed_out = True
            terminate_process_group(process)
            return_code = process.returncode if process.returncode is not None else -9
            log_handle.write(
                "\nBATCH_TIMEOUT: exceeded {:.1f} seconds\n".format(args.timeout_sec)
            )
        except KeyboardInterrupt:
            terminate_process_group(process)
            raise
    wall_time = time.perf_counter() - started
    try:
        log_text = log_path.read_text(encoding="utf-8", errors="replace")
    except OSError:
        log_text = ""
    metrics = parse_log_metrics(log_text)
    stem = benchmark.stem
    try:
        current_summary_mtime_ns = summary_path.stat().st_mtime_ns
    except OSError:
        current_summary_mtime_ns = None
    # A killed rerun may leave the previous run's otherwise valid summary in
    # the output directory.  Never attribute those stale failure/area metrics
    # to the new timed-out process.
    summary_was_updated = (
        current_summary_mtime_ns is not None
        and current_summary_mtime_ns != previous_summary_mtime_ns
    )
    summary = read_json(summary_path) if summary_was_updated else None
    status = classify(summary, return_code, timed_out)
    attempt = selected_seed_attempt(summary) if isinstance(summary, dict) else {}
    error = ""
    if status != "pass":
        nonempty_lines = [line.strip() for line in log_text.splitlines() if line.strip()]
        error = " | ".join(nonempty_lines[-3:])[-1200:]
    record = {
        "index": index,
        "suite": suite,
        "circuit": stem,
        "benchmark": str(relative),
        "status": status,
        "return_code": return_code,
        "timed_out": timed_out,
        "source_bytes": benchmark.stat().st_size,
        **metrics,
        "seed": summary.get("seed", "") if isinstance(summary, dict) else "",
        "seed_attempt_count": len(summary.get("seed_attempts", [])) if isinstance(summary, dict) else 0,
        "failed_edge_count": summary.get("failed_edge_count", "") if isinstance(summary, dict) else "",
        "clock_template_ok": attempt.get("clock_template_ok", ""),
        "clock_template_conflicts": attempt.get("clock_template_conflicts", ""),
        "route_expansion_round_count": summary.get("route_expansion_round_count", "") if isinstance(summary, dict) else "",
        "route_expansion_exhausted": summary.get("route_expansion_exhausted", "") if isinstance(summary, dict) else "",
        "route_incompatibility_reason": summary.get("route_incompatibility_reason", "") if isinstance(summary, dict) else "",
        "high_fanin_node_count": len(summary.get("right_down_port_capacity_nodes", [])) if isinstance(summary, dict) else "",
        "contraction_step_count": summary.get("contraction_step_count", "") if isinstance(summary, dict) else "",
        "contraction_evaluations": summary.get("contraction_evaluations", "") if isinstance(summary, dict) else "",
        "contraction_global_evaluations": summary.get("contraction_global_evaluations", "") if isinstance(summary, dict) else "",
        "contraction_recursive_evaluations": summary.get("contraction_recursive_evaluations", "") if isinstance(summary, dict) else "",
        "contraction_empty_line_evaluations": summary.get("contraction_empty_line_evaluations", "") if isinstance(summary, dict) else "",
        **contraction_metrics(summary),
        "width": summary.get("width", "") if isinstance(summary, dict) else "",
        "height": summary.get("height", "") if isinstance(summary, dict) else "",
        "area": summary.get("area", "") if isinstance(summary, dict) else "",
        "layout_runtime_sec": summary.get("run_time_sec", "") if isinstance(summary, dict) else "",
        "wall_time_sec": round(wall_time, 6),
        "parser_safe_generated": summary.get("parser_safe_generated", "") if isinstance(summary, dict) else "",
        "ifcn_exists": bool(isinstance(summary, dict) and Path(summary.get("ifcn", "")).is_file()),
        "encoded_ifcn_exists": bool(
            isinstance(summary, dict) and Path(summary.get("encoded_ifcn", "")).is_file()
        ),
        "output_dir": str(output_dir),
        "log_path": str(log_path),
        "error": error,
    }
    with record_path.open("w", encoding="utf-8") as handle:
        json.dump(record, handle, ensure_ascii=False, indent=2)
    print(
        "[{}/{}] done {}: status={} failed={} area={} wall={:.1f}s".format(
            index,
            total,
            relative,
            status,
            record["failed_edge_count"],
            record["area"],
            wall_time,
        ),
        flush=True,
    )
    return record


def write_csv(path, records):
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=RESULT_FIELDS, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(records)


def escape_latex(value):
    text = str(value)
    for source, replacement in (
        ("\\", r"\textbackslash{}"),
        ("&", r"\&"),
        ("%", r"\%"),
        ("$", r"\$"),
        ("#", r"\#"),
        ("_", r"\_"),
        ("{", r"\{"),
        ("}", r"\}"),
    ):
        text = text.replace(source, replacement)
    return text


def write_reports(output_root, args, benchmark_root, records, complete):
    records = sorted(records, key=lambda item: int(item["index"]))
    write_csv(output_root / "layout_results.csv", records)
    status_counts = Counter(record["status"] for record in records)
    suite_counts = defaultdict(Counter)
    for record in records:
        suite_counts[record["suite"]][record["status"]] += 1
        suite_counts[record["suite"]]["total"] += 1
    aggregate = {
        "campaign": (
            "Phase-aware node contraction cohort validation"
            if args.selection_results
            else "Graphviz+sifting full normal-graph placement/routing validation"
        ),
        "created_at": dt.datetime.now().astimezone().isoformat(),
        "complete": bool(complete),
        "project_root": str(PROJECT_ROOT),
        "benchmark_root": str(benchmark_root),
        "python": sys.version,
        "platform": platform.platform(),
        "configuration": {
            "timeout_sec": args.timeout_sec,
            "jobs": args.jobs,
            "seed": args.seed,
            "seed_retries": args.seed_retries,
            "compact_iters": args.compact_iters,
            "max_expansion_rounds": args.max_expansion_rounds,
            "route_expansion_timeout_sec": args.route_expansion_timeout_sec,
            "contraction_evaluations": args.contraction_evaluations,
            "contraction_timeout_sec": args.contraction_timeout_sec,
            "recursive_contraction_evaluations": (
                args.recursive_contraction_evaluations
                if args.recursive_contraction_evaluations > 0
                else args.contraction_evaluations
            ),
            "recursive_contraction_timeout_sec": (
                args.recursive_contraction_timeout_sec
                if args.recursive_contraction_timeout_sec > 0
                else args.contraction_timeout_sec
            ),
            "empty_line_contraction_evaluations": (
                args.empty_line_contraction_evaluations
                if args.empty_line_contraction_evaluations > 0
                else args.contraction_evaluations
            ),
            "empty_line_contraction_timeout_sec": (
                args.empty_line_contraction_timeout_sec
                if args.empty_line_contraction_timeout_sec > 0
                else args.contraction_timeout_sec
            ),
            "graphviz_timeout_sec": args.graphviz_timeout_sec,
            "sift_timeout_sec": args.sift_timeout_sec,
            "sift_evaluations": args.sift_evaluations,
            "include_generated_sources": args.include_generated_sources,
            "selection_results": str(Path(args.selection_results).resolve())
            if args.selection_results else "",
            "selection_status": list(args.selection_status or ["pass"])
            if args.selection_results else [],
            "route_repair_iters": 3,
            "phase_repair_iters": 2,
            "global_place_iters": 5,
            "compact_iters_legacy_report": args.compact_iters,
        },
        "counts": {"total": len(records), **dict(sorted(status_counts.items()))},
        "suite_counts": {
            suite: dict(sorted(counts.items())) for suite, counts in sorted(suite_counts.items())
        },
        "records": records,
    }
    legal_records = [record for record in records if record["status"] == "pass"]
    contraction_percentages = [
        float(record.get("contraction_area_reduction_percent", 0.0) or 0.0)
        for record in legal_records
    ]
    used_cell_percentages = [
        float(record.get("contraction_used_cell_reduction_percent", 0.0) or 0.0)
        for record in legal_records
    ]
    aggregate["contraction_summary"] = {
        "legal_layout_count": len(legal_records),
        "improved_layout_count": sum(
            value > 0.0 for value in contraction_percentages
        ),
        "mean_area_reduction_percent": (
            sum(contraction_percentages) / len(contraction_percentages)
            if contraction_percentages else 0.0
        ),
        "used_cell_improved_layout_count": sum(
            value > 0.0 for value in used_cell_percentages
        ),
        "mean_used_cell_reduction_percent": (
            sum(used_cell_percentages) / len(used_cell_percentages)
            if used_cell_percentages else 0.0
        ),
    }
    with (output_root / "layout_results.json").open("w", encoding="utf-8") as handle:
        json.dump(aggregate, handle, ensure_ascii=False, indent=2)

    markdown = [
        "# Graphviz+sifting 全电路布局布线验证",
        "",
        "- 完成状态：{}".format("完整" if complete else "运行中"),
        "- 已测试电路：{}".format(len(records)),
        "- 通过：{}".format(status_counts.get("pass", 0)),
        "- 超时：{}".format(status_counts.get("timeout", 0)),
        "- 其他失败：{}".format(
            len(records) - status_counts.get("pass", 0) - status_counts.get("timeout", 0)
        ),
        "- 合法版图中收缩改善：{}/{}".format(
            aggregate["contraction_summary"]["improved_layout_count"],
            aggregate["contraction_summary"]["legal_layout_count"],
        ),
        "- 合法版图平均面积减少：{:.2f}%".format(
            aggregate["contraction_summary"]["mean_area_reduction_percent"]
        ),
        "- 合法版图中已用 cell 减少：{}/{}".format(
            aggregate["contraction_summary"]["used_cell_improved_layout_count"],
            aggregate["contraction_summary"]["legal_layout_count"],
        ),
        "- 合法版图平均已用 cell 减少：{:.2f}%".format(
            aggregate["contraction_summary"]["mean_used_cell_reduction_percent"]
        ),
        "",
        "| Suite | Total | Pass | Timeout | Route failed | Template failed | Other |",
        "|---|---:|---:|---:|---:|---:|---:|",
    ]
    for suite, counts in sorted(suite_counts.items()):
        known = sum(
            counts.get(key, 0)
            for key in ("pass", "timeout", "route_failed", "template_failed")
        )
        markdown.append(
            "| {} | {} | {} | {} | {} | {} | {} |".format(
                suite,
                counts["total"],
                counts.get("pass", 0),
                counts.get("timeout", 0),
                counts.get("route_failed", 0),
                counts.get("template_failed", 0),
                counts["total"] - known,
            )
        )
    markdown.extend(
        [
            "",
            "逐电路数据见 `layout_results.csv` 和 `layout_results.json`；每个电路的",
            "`run.log`、`record.json`、IFCN 和 encoded IFCN 位于 `circuits/`。",
            "",
        ]
    )
    (output_root / "README.md").write_text("\n".join(markdown), encoding="utf-8")

    latex = [
        r"\documentclass{article}",
        r"\usepackage[margin=1.5cm]{geometry}",
        r"\usepackage{booktabs,longtable}",
        r"\begin{document}",
        r"\small",
        r"\begin{longtable}{llrrrrrr}",
        r"\toprule",
        r"Suite & Circuit & Status & Failed & Before & After & Reduced (\%) & Steps \\",
        r"\midrule",
        r"\endfirsthead",
        r"\toprule",
        r"Suite & Circuit & Status & Failed & Before & After & Reduced (\%) & Steps \\",
        r"\midrule",
        r"\endhead",
    ]
    for record in records:
        latex.append(
            "{} & {} & {} & {} & {} & {} & {:.2f} & {} \\\\".format(
                escape_latex(record["suite"]),
                escape_latex(record["circuit"]),
                escape_latex(record["status"]),
                record["failed_edge_count"],
                record.get("pre_contraction_area", ""),
                record["area"],
                float(record.get("contraction_area_reduction_percent", 0.0) or 0.0),
                record.get("contraction_step_count", ""),
            )
        )
    latex.extend([r"\bottomrule", r"\end{longtable}", r"\end{document}", ""])
    (output_root / "layout_results.tex").write_text("\n".join(latex), encoding="utf-8")


def main():
    args = parse_args()
    benchmark_root = Path(args.benchmark_root).resolve()
    output_root = Path(args.output_root).resolve()
    if not benchmark_root.is_dir():
        raise FileNotFoundError("benchmark root not found: {}".format(benchmark_root))
    if not LAYOUT_SCRIPT.is_file():
        raise FileNotFoundError("layout script not found: {}".format(LAYOUT_SCRIPT))
    output_root.mkdir(parents=True, exist_ok=True)
    benchmarks = discover_benchmarks(benchmark_root, args.include_generated_sources)
    if args.selection_results:
        selection_path = Path(args.selection_results).resolve()
        with selection_path.open("r", encoding="utf-8") as handle:
            selection_data = json.load(handle)
        selected_statuses = set(args.selection_status or ["pass"])
        selected_benchmarks = {
            str(record.get("benchmark", ""))
            for record in selection_data.get("records", [])
            if str(record.get("status", "")) in selected_statuses
        }
        benchmarks = [
            path
            for path in benchmarks
            if str(path.relative_to(benchmark_root)) in selected_benchmarks
        ]
        if not benchmarks:
            raise RuntimeError(
                "selection produced no benchmarks from {}".format(selection_path)
            )
    manifest = {
        "benchmark_root": str(benchmark_root),
        "count": len(benchmarks),
        "benchmarks": [str(path.relative_to(benchmark_root)) for path in benchmarks],
        "selection_results": (
            str(Path(args.selection_results).resolve())
            if args.selection_results else ""
        ),
        "selection_status": list(args.selection_status or ["pass"])
        if args.selection_results else [],
    }
    with (output_root / "manifest.json").open("w", encoding="utf-8") as handle:
        json.dump(manifest, handle, ensure_ascii=False, indent=2)
    records = []
    try:
        indexed_benchmarks = list(enumerate(benchmarks, 1))
        if max(1, int(args.jobs)) == 1:
            for index, benchmark in indexed_benchmarks:
                records.append(
                    run_one(index, len(benchmarks), args, benchmark_root, output_root, benchmark)
                )
                write_reports(output_root, args, benchmark_root, records, complete=False)
        else:
            with ThreadPoolExecutor(max_workers=max(1, int(args.jobs))) as executor:
                futures = {
                    executor.submit(
                        run_one,
                        index,
                        len(benchmarks),
                        args,
                        benchmark_root,
                        output_root,
                        benchmark,
                    ): index
                    for index, benchmark in indexed_benchmarks
                }
                for future in as_completed(futures):
                    records.append(future.result())
                    write_reports(
                        output_root,
                        args,
                        benchmark_root,
                        records,
                        complete=False,
                    )
    finally:
        write_reports(
            output_root,
            args,
            benchmark_root,
            records,
            complete=len(records) == len(benchmarks),
        )
    print("RESULT_DIR {}".format(output_root), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
