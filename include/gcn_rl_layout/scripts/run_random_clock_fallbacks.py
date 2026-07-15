#!/usr/bin/env python3
"""Re-run failed 2DDWave circuits with the random-clock P&R closure."""

import argparse
import csv
import datetime as dt
import json
import os
import signal
import subprocess
import sys
import time
from collections import Counter
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path


GCN_RL_ROOT = Path(__file__).resolve().parents[1]
PROJECT_ROOT = GCN_RL_ROOT.parents[1]
RANDOM_SCRIPT = GCN_RL_ROOT / "src" / "algorithm" / "main" / "test_randomPhase.py"
FIELDS = (
    "index",
    "benchmark",
    "normal_status",
    "status",
    "timed_out",
    "return_code",
    "failed_edge_count",
    "direction_violation_count",
    "width",
    "height",
    "area",
    "pre_contraction_area",
    "contraction_area_reduction",
    "contraction_area_reduction_percent",
    "expansion_round_count",
    "contraction_step_count",
    "layout_runtime_sec",
    "wall_time_sec",
    "ifcn_exists",
    "encoded_ifcn_exists",
    "output_dir",
    "log_path",
    "error",
)


def parse_args():
    parser = argparse.ArgumentParser(
        description="Run random-clock placement/routing on non-passing 2DDWave circuits."
    )
    parser.add_argument("--normal-results", required=True)
    parser.add_argument("--output-root", default="")
    parser.add_argument("--timeout-sec", type=float, default=120.0)
    parser.add_argument("--jobs", type=int, default=1)
    parser.add_argument("--seed", type=int, default=7)
    parser.add_argument("--x-spacing", type=int, default=3)
    parser.add_argument("--y-spacing", type=int, default=4)
    parser.add_argument("--max-expansion-rounds", type=int, default=12)
    parser.add_argument("--expansion-timeout-sec", type=float, default=75.0)
    parser.add_argument("--contract-iters", type=int, default=16)
    parser.add_argument("--contraction-evaluations", type=int, default=32)
    parser.add_argument("--contraction-timeout-sec", type=float, default=30.0)
    parser.add_argument("--cpp-expansion-limit", type=int, default=1000)
    parser.add_argument("--force", action="store_true")
    return parser.parse_args()


def read_json(path):
    try:
        with Path(path).open("r", encoding="utf-8") as handle:
            return json.load(handle)
    except (OSError, TypeError, ValueError):
        return None


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


def run_one(index, total, item, args, benchmark_root, output_root):
    relative = Path(item["benchmark"])
    benchmark = benchmark_root / relative
    output_dir = output_root / "circuits" / relative.with_suffix("")
    output_dir.mkdir(parents=True, exist_ok=True)
    record_path = output_dir / "record.json"
    if record_path.is_file() and not args.force:
        record = read_json(record_path)
        if isinstance(record, dict):
            print(
                "[{}/{}] resume {}: {}".format(index, total, relative, record["status"]),
                flush=True,
            )
            return record

    log_path = output_dir / "run.log"
    command = [
        sys.executable,
        str(RANDOM_SCRIPT),
        "--benchmark",
        str(benchmark),
        "--output-dir",
        str(output_dir),
        "--seed",
        str(args.seed),
        "--layout-strategy",
        "adaptive",
        "--layout-orientation",
        "auto",
        "--single-spacing",
        "--x-spacing",
        str(args.x_spacing),
        "--y-spacing",
        str(args.y_spacing),
        "--local-refine-rounds",
        "0",
        "--max-expansion-rounds",
        str(args.max_expansion_rounds),
        "--expansion-timeout-sec",
        str(args.expansion_timeout_sec),
        "--outer-in-contract-iters",
        str(args.contract_iters),
        "--contraction-max-evaluations",
        str(args.contraction_evaluations),
        "--contraction-timeout-sec",
        str(args.contraction_timeout_sec),
        "--disable-layout-memory",
        "--skip-figures",
        "--skip-latex",
    ]
    env = os.environ.copy()
    env.update(
        {
            "MPLBACKEND": "Agg",
            "PYTHONHASHSEED": "0",
            "PYTHONUNBUFFERED": "1",
            "IFCN_RANDOM_CPP_EXPANSION_LIMIT": str(args.cpp_expansion_limit),
        }
    )
    started = time.perf_counter()
    timed_out = False
    return_code = -1
    print("[{}/{}] random-clock {}".format(index, total, relative), flush=True)
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
            log_handle.write("\nFALLBACK_TIMEOUT\n")
        except KeyboardInterrupt:
            terminate_process_group(process)
            raise

    wall_time = time.perf_counter() - started
    summary_path = output_dir / (benchmark.stem + "_phase_layout_summary.json")
    summary = read_json(summary_path)
    if timed_out:
        status = "timeout"
    elif return_code != 0:
        status = "process_error"
    elif not isinstance(summary, dict):
        status = "missing_summary"
    elif bool(summary.get("layout_legal", False)):
        status = "pass"
    else:
        status = "route_failed"

    error = ""
    if status != "pass":
        try:
            lines = [
                line.strip()
                for line in log_path.read_text(
                    encoding="utf-8", errors="replace"
                ).splitlines()
                if line.strip()
            ]
            error = " | ".join(lines[-4:])[-1200:]
        except OSError:
            pass

    def value(key, default=""):
        return summary.get(key, default) if isinstance(summary, dict) else default

    record = {
        "index": int(index),
        "benchmark": str(relative),
        "normal_status": str(item.get("status", "")),
        "status": status,
        "timed_out": bool(timed_out),
        "return_code": int(return_code),
        "failed_edge_count": value("failed_edge_count"),
        "direction_violation_count": value("direction_violation_count"),
        "width": value("width"),
        "height": value("height"),
        "area": value("area"),
        "pre_contraction_area": value("pre_contraction_area"),
        "contraction_area_reduction": value("contraction_area_reduction"),
        "contraction_area_reduction_percent": value(
            "contraction_area_reduction_percent"
        ),
        "expansion_round_count": value("expansion_round_count"),
        "contraction_step_count": value("contraction_step_count"),
        "layout_runtime_sec": value("run_time_sec"),
        "wall_time_sec": round(float(wall_time), 6),
        "ifcn_exists": bool(
            isinstance(summary, dict) and Path(summary.get("ifcn", "")).is_file()
        ),
        "encoded_ifcn_exists": bool(
            isinstance(summary, dict)
            and Path(summary.get("encoded_ifcn", "")).is_file()
        ),
        "output_dir": str(output_dir),
        "log_path": str(log_path),
        "error": error,
    }
    with record_path.open("w", encoding="utf-8") as handle:
        json.dump(record, handle, ensure_ascii=False, indent=2)
    print(
        "[{}/{}] done {}: {} failed={} area={} wall={:.1f}s".format(
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


def write_reports(output_root, source, args, records, complete):
    records = sorted(records, key=lambda item: int(item["index"]))
    with (output_root / "random_clock_results.csv").open(
        "w", encoding="utf-8", newline=""
    ) as handle:
        writer = csv.DictWriter(handle, fieldnames=FIELDS, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(records)
    counts = Counter(record["status"] for record in records)
    payload = {
        "campaign": "random-clock fallback for non-passing 2DDWave circuits",
        "created_at": dt.datetime.now().astimezone().isoformat(),
        "complete": bool(complete),
        "source_normal_results": str(source),
        "configuration": vars(args),
        "counts": {"total": len(records), **dict(sorted(counts.items()))},
        "records": records,
    }
    with (output_root / "random_clock_results.json").open(
        "w", encoding="utf-8"
    ) as handle:
        json.dump(payload, handle, ensure_ascii=False, indent=2)

    latex = [
        r"\documentclass{article}",
        r"\usepackage[margin=1.5cm]{geometry}",
        r"\usepackage{booktabs,longtable}",
        r"\begin{document}",
        r"\small",
        r"\begin{longtable}{llrrrrr}",
        r"\toprule",
        r"Circuit & Status & Failed & Width & Height & Area & Reduction (\%) \\",
        r"\midrule",
        r"\endfirsthead",
        r"\toprule",
        r"Circuit & Status & Failed & Width & Height & Area & Reduction (\%) \\",
        r"\midrule",
        r"\endhead",
    ]
    for record in records:
        latex.append(
            "{} & {} & {} & {} & {} & {} & {:.2f} \\\\".format(
                escape_latex(record["benchmark"]),
                escape_latex(record["status"]),
                record["failed_edge_count"],
                record["width"],
                record["height"],
                record["area"],
                float(record["contraction_area_reduction_percent"] or 0.0),
            )
        )
    latex.extend([r"\bottomrule", r"\end{longtable}", r"\end{document}", ""])
    (output_root / "random_clock_results.tex").write_text(
        "\n".join(latex), encoding="utf-8"
    )


def main():
    args = parse_args()
    normal_results_path = Path(args.normal_results).resolve()
    normal = read_json(normal_results_path)
    if not isinstance(normal, dict):
        raise ValueError("invalid normal results JSON: {}".format(normal_results_path))
    benchmark_root = Path(normal["benchmark_root"]).resolve()
    output_root = (
        Path(args.output_root).resolve()
        if args.output_root
        else normal_results_path.parent / "random_clock_fallback"
    )
    output_root.mkdir(parents=True, exist_ok=True)
    selected = [
        record
        for record in normal.get("records", [])
        if record.get("status") != "pass"
    ]
    selected.sort(key=lambda item: int(item.get("source_bytes", 0)))
    records = []
    try:
        with ThreadPoolExecutor(max_workers=max(1, int(args.jobs))) as executor:
            futures = {
                executor.submit(
                    run_one,
                    index,
                    len(selected),
                    item,
                    args,
                    benchmark_root,
                    output_root,
                ): index
                for index, item in enumerate(selected, 1)
            }
            for future in as_completed(futures):
                records.append(future.result())
                write_reports(
                    output_root,
                    normal_results_path,
                    args,
                    records,
                    complete=False,
                )
    finally:
        write_reports(
            output_root,
            normal_results_path,
            args,
            records,
            complete=len(records) == len(selected),
        )
    print("RESULT_DIR {}".format(output_root), flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
