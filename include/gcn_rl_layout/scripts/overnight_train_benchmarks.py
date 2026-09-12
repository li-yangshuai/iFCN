#!/usr/bin/env python3
import argparse
import csv
import datetime as dt
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[3]
GCN_RL_ROOT = PROJECT_ROOT / "include" / "gcn_rl_layout"
RUNNER = GCN_RL_ROOT / "scripts" / "gui_gcn_rl_runner.py"
DEFAULT_EXPERIENCE = GCN_RL_ROOT / "results" / "layout_memory" / "rl_action_experience.json"
DEFAULT_RESULTS_ROOT = GCN_RL_ROOT / "results" / "overnight_training"
SKIP_STEMS = {"FA", "FS", "HA", "HS"}


def parse_args():
    parser = argparse.ArgumentParser(
        description="Run long Graphviz+sifting + RL layout training over benchmark directories.",
    )
    parser.add_argument(
        "--benchmark-dir",
        action="append",
        default=[],
        help="Directory containing Verilog benchmarks. Can be repeated.",
    )
    parser.add_argument("--output-root", default=None)
    parser.add_argument("--experience-path", default=str(DEFAULT_EXPERIENCE))
    parser.add_argument("--runs", type=int, default=4)
    parser.add_argument("--max-workers", type=int, default=2)
    parser.add_argument("--base-seed", type=int, default=12000)
    parser.add_argument("--device", default="auto", choices=("auto", "cpu", "cuda"))
    parser.add_argument("--graphviz-timeout-sec", type=int, default=60)
    parser.add_argument("--sift-timeout-sec", type=int, default=20)
    parser.add_argument("--sift-evaluations", type=int, default=200000)
    parser.add_argument("--episodes", type=int, default=100)
    parser.add_argument("--steps-per-episode", type=int, default=8)
    parser.add_argument("--ppo-epochs", type=int, default=4)
    parser.add_argument("--minibatch-size", type=int, default=32)
    parser.add_argument("--exact-timeout-sec", type=int, default=60)
    parser.add_argument("--elite-start-probability", type=float, default=0.6)
    parser.add_argument(
        "--rollback-worse-actions",
        action=argparse.BooleanOptionalAction,
        default=True,
    )
    parser.add_argument("--force", action="store_true")
    parser.add_argument(
        "--include-auto-start",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="Also train one auto-start profile after the structural-start profiles.",
    )
    parser.add_argument(
        "--strict-memory-updates",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="Only update shared action memory when a run improves a legal routed layout.",
    )
    parser.add_argument(
        "--training-plots",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="Write per-run reward/cost/area training curve SVG files.",
    )
    return parser.parse_args()


def now_stamp():
    return dt.datetime.now().strftime("%Y%m%d_%H%M%S")


def resolve_path(path):
    candidate = Path(path)
    if not candidate.is_absolute():
        candidate = PROJECT_ROOT / candidate
    return candidate.resolve()


def discover_benchmarks(dirs):
    records = []
    seen = set()
    for directory in dirs:
        bench_dir = resolve_path(directory)
        if not bench_dir.is_dir():
            continue
        group = bench_dir.name
        for path in sorted(bench_dir.glob("*.v")):
            if path.stem in SKIP_STEMS or path.stem.endswith("_parser_safe"):
                continue
            key = str(path.resolve())
            if key in seen:
                continue
            seen.add(key)
            records.append({"group": group, "path": path.resolve()})
    return records


def profile_definitions(include_auto_start):
    profiles = [
        {
            "name": "graphviz_sift_phase3_pad1",
            "phase": 3,
            "padding": 1,
            "start": "structural",
            "area_reward": 4.5,
            "area_regression": 400,
            "span_weight": 12,
        },
        {
            "name": "graphviz_sift_phase4_pad1",
            "phase": 4,
            "padding": 1,
            "start": "structural",
            "area_reward": 4.0,
            "area_regression": 350,
            "span_weight": 10,
        },
        {
            "name": "graphviz_sift_phase4_pad2",
            "phase": 4,
            "padding": 2,
            "start": "structural",
            "area_reward": 3.5,
            "area_regression": 300,
            "span_weight": 8,
        },
    ]
    if include_auto_start:
        profiles.append(
            {
                "name": "auto_phase3_pad1",
                "phase": 3,
                "padding": 1,
                "start": "auto",
                "area_reward": 4.0,
                "area_regression": 350,
                "span_weight": 10,
            }
        )
    return profiles


def read_json(path, fallback=None):
    try:
        with open(path, "r", encoding="utf-8") as handle:
            return json.load(handle)
    except (OSError, json.JSONDecodeError):
        return fallback


def write_json(path, payload):
    path.parent.mkdir(parents=True, exist_ok=True)
    temp_path = path.with_suffix(path.suffix + f".tmp.{os.getpid()}")
    with open(temp_path, "w", encoding="utf-8") as handle:
        json.dump(payload, handle, ensure_ascii=False, indent=2)
    os.replace(temp_path, path)


def result_key(summary):
    if not summary:
        return (1, 1_000_000, 1_000_000.0, 1_000_000, 1_000_000, 1_000_000.0)
    width = int(summary.get("best_width", 1_000_000))
    height = int(summary.get("best_height", 1_000_000))
    return (
        0,
        int(summary.get("best_failed_edges", 1_000_000)),
        float(summary.get("best_area", 1_000_000.0)),
        max(width, height),
        width + height,
        float(summary.get("best_cost", 1_000_000.0)),
    )


def copy_best_artifacts(profile_dir, best_dir, stem):
    suffixes = [
        "_rl_layout.ifcn",
        "_rl_layout_encoded.ifcn",
        "_rl_layout.svg",
        "_rl_layout.tex",
        "_rl_summary.json",
        "_rl_config.json",
        "_rl_training.csv",
        "_rl_training_curves.svg",
        "_rl_best_layout.json",
        "_rl_warm_start.json",
        "_rl_action_histogram.json",
        "_rl_policy.pt",
        "_rl_candidate_pool.json",
    ]
    best_dir.mkdir(parents=True, exist_ok=True)
    for suffix in suffixes:
        source = profile_dir / f"{stem}{suffix}"
        if source.exists():
            shutil.copy2(source, best_dir / f"{stem}{suffix}")


def latex_escape(value):
    text = str(value)
    replacements = {
        "\\": r"\textbackslash{}",
        "&": r"\&",
        "%": r"\%",
        "$": r"\$",
        "#": r"\#",
        "_": r"\_",
        "{": r"\{",
        "}": r"\}",
        "~": r"\textasciitilde{}",
        "^": r"\textasciicircum{}",
    }
    return "".join(replacements.get(char, char) for char in text)


def format_float(value, digits=2):
    if value == "" or value is None:
        return ""
    try:
        return f"{float(value):.{digits}f}"
    except (TypeError, ValueError):
        return str(value)


def best_run_duration(summary):
    best_index = summary.get("best_run_index")
    for record in summary.get("runs", []):
        if int(record.get("index", -1)) == int(best_index):
            return float(record.get("duration_sec", 0.0))
    return 0.0


def build_train_args(args, profile):
    strict_flag = "--strict-memory-updates" if args.strict_memory_updates else "--no-strict-memory-updates"
    train_args = [
        "--device", args.device,
        "--phase-cycle", str(profile["phase"]),
        "--x-spacing", "2",
        "--y-spacing", "2",
        "--padding", str(profile["padding"]),
        "--max-same-phase", "4",
        "--start-layout-strategy", profile["start"],
        "--start-layout-orientation", "auto",
        "--episodes", str(args.episodes),
        "--steps-per-episode", str(args.steps_per_episode),
        "--ppo-epochs", str(args.ppo_epochs),
        "--minibatch-size", str(args.minibatch_size),
        "--train-eval-mode", "exact",
        "--final-exact-validation-candidates", "1",
        "--exact-eval-timeout-sec", str(args.exact_timeout_sec),
        "--local-refine-rounds", "8",
        "--local-max-evaluations", "240",
        "--post-primary-pack-rounds", "8",
        "--post-area-pack-rounds", "14",
        "--post-pack-max-evaluations", "480",
        "--post-phase-strip-pack-rounds", "4",
        "--post-phase-strip-pack-max-evaluations", "240",
        "--best-selection-mode", "legal-area",
        "--area-reward-weight", str(profile["area_reward"]),
        "--area-regression-weight", str(profile["area_regression"]),
        "--max-span-weight", str(profile["span_weight"]),
        "--log-interval", "10",
        "--disable-step-log",
        "--final-exact-validation",
        "--elite-start-probability", str(args.elite_start_probability),
        strict_flag,
    ]
    train_args.append("--rollback-worse-actions" if args.rollback_worse_actions else "--no-rollback-worse-actions")
    if not args.training_plots:
        train_args.append("--disable-training-plots")
    return train_args


def run_profile(args, benchmark, profile, run_root, profile_seed, log_handle):
    stem = benchmark["path"].stem
    group = benchmark["group"]
    profile_dir = run_root / "profiles" / group / stem / profile["name"]
    summary_path = profile_dir / "gui_parallel_summary.json"
    if summary_path.exists() and not args.force:
        summary = read_json(summary_path, {})
        if summary.get("status") == "ok":
            print(f"[skip] {group}/{stem}/{profile['name']} already complete", file=log_handle, flush=True)
            return summary

    command = [
        sys.executable,
        "-u",
        str(RUNNER),
        "--benchmark", str(benchmark["path"]),
        "--output-dir", str(profile_dir),
        "--runs", str(args.runs),
        "--max-workers", str(args.max_workers),
        "--base-seed", str(profile_seed),
        "--rl-experience-path", str(resolve_path(args.experience_path)),
        "--",
        *build_train_args(args, profile),
    ]
    env = os.environ.copy()
    env["MPLBACKEND"] = "Agg"
    env["IFCN_GRAPHVIZ_TIMEOUT"] = str(args.graphviz_timeout_sec)
    env["IFCN_SIFT_TIMEOUT"] = str(args.sift_timeout_sec)
    env["IFCN_SIFT_EVALUATIONS"] = str(args.sift_evaluations)
    profile_dir.mkdir(parents=True, exist_ok=True)
    command_path = profile_dir / "command.json"
    write_json(
        command_path,
        {
            "command": command,
            "env": {
                "IFCN_GRAPHVIZ_TIMEOUT": env["IFCN_GRAPHVIZ_TIMEOUT"],
                "IFCN_SIFT_TIMEOUT": env["IFCN_SIFT_TIMEOUT"],
                "IFCN_SIFT_EVALUATIONS": env["IFCN_SIFT_EVALUATIONS"],
            },
        },
    )

    start = time.perf_counter()
    print(
        f"[run] {group}/{stem}/{profile['name']} seed={profile_seed} "
        f"phase={profile['phase']} padding={profile['padding']} start={profile['start']}",
        file=log_handle,
        flush=True,
    )
    with open(profile_dir / "launcher.log", "w", encoding="utf-8") as profile_log:
        completed = subprocess.run(
            command,
            cwd=str(PROJECT_ROOT),
            env=env,
            stdout=profile_log,
            stderr=subprocess.STDOUT,
            text=True,
            check=False,
        )
    duration = time.perf_counter() - start
    summary = read_json(summary_path, {})
    status = summary.get("status", "missing") if completed.returncode == 0 else f"rc={completed.returncode}"
    best_summary = summary.get("best_summary", {}) if isinstance(summary, dict) else {}
    print(
        f"[done] {group}/{stem}/{profile['name']} status={status} "
        f"duration={duration:.1f}s failed={best_summary.get('best_failed_edges')} "
        f"area={best_summary.get('best_area')} seed={summary.get('best_seed') if isinstance(summary, dict) else None}",
        file=log_handle,
        flush=True,
    )
    return summary


def write_best_tables(run_root, best_records):
    best_json = run_root / "best_summary.json"
    write_json(best_json, best_records)
    csv_path = run_root / "best_summary.csv"
    fieldnames = [
        "group",
        "benchmark",
        "source",
        "best_profile",
        "best_seed",
        "phase_cycle",
        "train_eval_mode",
        "width",
        "height",
        "area",
        "failed_edges",
        "layout_runtime_sec",
        "total_runtime_sec",
        "strict_success",
        "profile_dir",
        "best_dir",
    ]
    with open(csv_path, "w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(best_records)
    tex_path = run_root / "best_summary_table.tex"
    with open(tex_path, "w", encoding="utf-8") as handle:
        handle.write("% Auto-generated by overnight_train_benchmarks.py\n")
        handle.write("\\begin{tabular}{llrrrrrrr}\n")
        handle.write("\\hline\n")
        handle.write(
            "Set & Circuit & Phase & Width & Height & Area & Failed & Train(s) & Total(s) \\\\\n"
        )
        handle.write("\\hline\n")
        for record in best_records:
            handle.write(
                " & ".join(
                    [
                        latex_escape(record.get("group", "")),
                        latex_escape(record.get("benchmark", "")),
                        latex_escape(record.get("phase_cycle", "")),
                        latex_escape(record.get("width", "")),
                        latex_escape(record.get("height", "")),
                        format_float(record.get("area", ""), digits=0),
                        latex_escape(record.get("failed_edges", "")),
                        format_float(record.get("layout_runtime_sec", ""), digits=2),
                        format_float(record.get("total_runtime_sec", ""), digits=2),
                    ]
                )
                + " \\\\\n"
            )
        handle.write("\\hline\n")
        handle.write("\\end{tabular}\n")
    return best_json, csv_path, tex_path


def main():
    args = parse_args()
    benchmark_dirs = args.benchmark_dir or [
        "tests/benchmarks_f/TOY",
        "tests/benchmarks_f/MAJ",
    ]
    run_root = resolve_path(args.output_root) if args.output_root else DEFAULT_RESULTS_ROOT / now_stamp()
    run_root.mkdir(parents=True, exist_ok=True)
    latest_path = DEFAULT_RESULTS_ROOT / "latest_run.json"
    benchmarks = discover_benchmarks(benchmark_dirs)
    profiles = profile_definitions(args.include_auto_start)
    run_state = {
        "status": "running",
        "pid": os.getpid(),
        "started_at": dt.datetime.now().isoformat(timespec="seconds"),
        "run_root": str(run_root),
        "benchmark_dirs": [str(resolve_path(path)) for path in benchmark_dirs],
        "benchmark_count": len(benchmarks),
        "profiles": [profile["name"] for profile in profiles],
        "experience_path": str(resolve_path(args.experience_path)),
    }
    write_json(run_root / "run_state.json", run_state)
    write_json(latest_path, run_state)

    log_path = run_root / "overnight_train.log"
    best_records = []
    profile_seed = int(args.base_seed)
    with open(log_path, "a", encoding="utf-8") as log_handle:
        print(f"[start] run_root={run_root}", file=log_handle, flush=True)
        print(f"[start] benchmarks={len(benchmarks)} profiles={len(profiles)}", file=log_handle, flush=True)
        for bench_idx, benchmark in enumerate(benchmarks, start=1):
            group = benchmark["group"]
            stem = benchmark["path"].stem
            print(f"[bench] {bench_idx}/{len(benchmarks)} {group}/{stem}", file=log_handle, flush=True)
            profile_summaries = []
            for profile in profiles:
                summary = run_profile(args, benchmark, profile, run_root, profile_seed, log_handle)
                profile_seed += max(10, args.runs)
                profile_summaries.append((profile, summary))

            successful = [
                (profile, summary)
                for profile, summary in profile_summaries
                if isinstance(summary, dict) and summary.get("status") == "ok" and summary.get("best_summary")
            ]
            if not successful:
                best_records.append(
                    {
                        "group": group,
                        "benchmark": stem,
                        "source": str(benchmark["path"]),
                        "best_profile": "failed",
                        "best_seed": "",
                        "phase_cycle": "",
                        "train_eval_mode": "",
                        "width": "",
                        "height": "",
                        "area": "",
                        "failed_edges": "",
                        "strict_success": "",
                        "profile_dir": "",
                        "best_dir": "",
                    }
                )
                continue

            best_profile, best_summary = min(successful, key=lambda item: result_key(item[1]["best_summary"]))
            best = best_summary["best_summary"]
            total_runtime_sec = best_run_duration(best_summary)
            profile_dir = run_root / "profiles" / group / stem / best_profile["name"]
            best_dir = run_root / "best" / group / stem
            copy_best_artifacts(profile_dir, best_dir, stem)
            best_record = {
                "group": group,
                "benchmark": stem,
                "source": str(benchmark["path"]),
                "best_profile": best_profile["name"],
                "best_seed": int(best_summary.get("best_seed", -1)),
                "phase_cycle": best.get("phase_cycle_range", [""])[0],
                "train_eval_mode": best.get("train_eval_mode_resolved", ""),
                "width": int(best.get("best_width", 0)),
                "height": int(best.get("best_height", 0)),
                "area": float(best.get("best_area", 0.0)),
                "failed_edges": int(best.get("best_failed_edges", -1)),
                "layout_runtime_sec": float(best.get("training_runtime_sec", 0.0)),
                "total_runtime_sec": total_runtime_sec,
                "strict_success": bool(best.get("strict_success", False)),
                "profile_dir": str(profile_dir),
                "best_dir": str(best_dir),
            }
            best_records.append(best_record)
            write_best_tables(run_root, best_records)
            run_state.update(
                {
                    "last_completed": f"{group}/{stem}",
                    "completed_benchmarks": len(best_records),
                    "status": "running",
                }
            )
            write_json(run_root / "run_state.json", run_state)
            write_json(latest_path, run_state)
            print(
                f"[best] {group}/{stem} profile={best_record['best_profile']} "
                f"failed={best_record['failed_edges']} area={best_record['area']} "
                f"size={best_record['width']}x{best_record['height']}",
                file=log_handle,
                flush=True,
            )

    best_json, csv_path, tex_path = write_best_tables(run_root, best_records)
    final_state = dict(run_state)
    final_state.update(
        {
            "status": "finished",
            "finished_at": dt.datetime.now().isoformat(timespec="seconds"),
            "completed_benchmarks": len(best_records),
            "best_summary_json": str(best_json),
            "best_summary_csv": str(csv_path),
            "best_summary_tex": str(tex_path),
            "log_path": str(log_path),
        }
    )
    write_json(run_root / "run_state.json", final_state)
    write_json(latest_path, final_state)
    print(f"[finish] best_summary={best_json}")
    print(f"[finish] latex_table={tex_path}")
    print(f"[finish] log={log_path}")


if __name__ == "__main__":
    main()
