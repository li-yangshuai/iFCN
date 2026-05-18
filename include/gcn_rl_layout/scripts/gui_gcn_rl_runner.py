#!/usr/bin/env python3
import argparse
import concurrent.futures
import json
import os
import shutil
import subprocess
import sys
import time


REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
TRAIN_SCRIPT = os.path.join(REPO_ROOT, "src", "algorithm", "main", "train_layout_ppo.py")
DEFAULT_EXPERIENCE_PATH = os.path.join(
    REPO_ROOT,
    "results",
    "layout_memory",
    "rl_action_experience.json",
)


ACTION_FIELDS = (
    "count",
    "reward_sum",
    "delta_sum",
    "positive_count",
    "improvement_bonus_count",
)


def parse_args():
    parser = argparse.ArgumentParser(
        description="GUI launcher for one or more parallel GCN+RL layout training runs.",
    )
    parser.add_argument("--benchmark", required=True)
    parser.add_argument("--output-dir", required=True)
    parser.add_argument("--runs", type=int, default=1)
    parser.add_argument("--max-workers", type=int, default=None)
    parser.add_argument("--base-seed", type=int, default=7)
    parser.add_argument("--rl-experience-path", default=DEFAULT_EXPERIENCE_PATH)
    parser.add_argument("train_args", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if args.train_args and args.train_args[0] == "--":
        args.train_args = args.train_args[1:]
    args.runs = max(1, int(args.runs))
    args.max_workers = max(1, int(args.max_workers or args.runs))
    return args


def read_json(path, fallback=None):
    if not path or not os.path.exists(path):
        return {} if fallback is None else fallback
    try:
        with open(path, "r", encoding="utf-8") as handle:
            return json.load(handle)
    except (OSError, json.JSONDecodeError):
        return {} if fallback is None else fallback


def write_json(path, payload):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    temp_path = f"{path}.tmp.{os.getpid()}"
    with open(temp_path, "w", encoding="utf-8") as handle:
        json.dump(payload, handle, ensure_ascii=False, indent=2)
    os.replace(temp_path, path)


def copy_json(src, dst):
    payload = read_json(src, {"version": 1, "actions": {}, "updates": []})
    write_json(dst, payload)


def run_one(index, args, initial_experience_path):
    benchmark_stem = os.path.splitext(os.path.basename(args.benchmark))[0]
    run_dir = args.output_dir if args.runs == 1 else os.path.join(args.output_dir, f"run_{index + 1}")
    os.makedirs(run_dir, exist_ok=True)

    local_experience_path = (
        args.rl_experience_path
        if args.runs == 1 else
        os.path.join(run_dir, "rl_action_experience.json")
    )
    if args.runs > 1:
        copy_json(initial_experience_path, local_experience_path)

    command = [
        sys.executable,
        "-u",
        TRAIN_SCRIPT,
        "--benchmark",
        os.path.abspath(args.benchmark),
        "--output-dir",
        os.path.abspath(run_dir),
        "--seed",
        str(int(args.base_seed) + index),
        "--rl-experience-path",
        os.path.abspath(local_experience_path),
        *args.train_args,
    ]

    stdout_path = os.path.join(run_dir, f"{benchmark_stem}_stdout.log")
    stderr_path = os.path.join(run_dir, f"{benchmark_stem}_stderr.log")
    start = time.perf_counter()
    with open(stdout_path, "w", encoding="utf-8") as stdout_file, open(
        stderr_path,
        "w",
        encoding="utf-8",
    ) as stderr_file:
        completed = subprocess.run(
            command,
            cwd=REPO_ROOT,
            stdout=stdout_file,
            stderr=stderr_file,
            text=True,
            check=False,
        )

    duration = time.perf_counter() - start
    summary_path = os.path.join(run_dir, f"{benchmark_stem}_rl_summary.json")
    summary = read_json(summary_path, None)
    return {
        "index": index,
        "seed": int(args.base_seed) + index,
        "run_dir": run_dir,
        "command": command,
        "returncode": int(completed.returncode),
        "duration_sec": float(duration),
        "summary_path": summary_path,
        "summary": summary,
        "stdout_path": stdout_path,
        "stderr_path": stderr_path,
        "local_experience_path": local_experience_path,
    }


def result_key(record):
    summary = record.get("summary") or {}
    if record.get("returncode") != 0 or not summary:
        return (1, 1_000_000, 1_000_000.0, 1_000_000, record.get("index", 0))
    failed_edges = int(summary.get("best_failed_edges", 1_000_000))
    area = float(summary.get("best_area", 1_000_000.0))
    width = int(summary.get("best_width", 1_000_000))
    height = int(summary.get("best_height", 1_000_000))
    cost = float(summary.get("best_cost", 1_000_000.0))
    return (0, failed_edges, area, max(width, height), width + height, cost, record.get("index", 0))


def copy_best_artifacts(best_record, output_dir, benchmark_stem):
    if best_record["run_dir"] == output_dir:
        return

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
    ]
    os.makedirs(output_dir, exist_ok=True)
    for suffix in suffixes:
        source = os.path.join(best_record["run_dir"], f"{benchmark_stem}{suffix}")
        if os.path.exists(source):
            shutil.copy2(source, os.path.join(output_dir, f"{benchmark_stem}{suffix}"))


def merge_experience_delta(global_path, initial_memory, local_paths):
    if not global_path:
        return None
    current = read_json(global_path, {"version": 1, "actions": {}, "updates": []})
    current.setdefault("version", 1)
    current.setdefault("actions", {})
    current.setdefault("updates", [])

    base_actions = initial_memory.get("actions", {}) if isinstance(initial_memory, dict) else {}
    base_updates = initial_memory.get("updates", []) if isinstance(initial_memory, dict) else []

    for local_path in local_paths:
        local = read_json(local_path, None)
        if not local:
            continue
        local_actions = local.get("actions", {})
        for key, local_entry in local_actions.items():
            base_entry = base_actions.get(key, {})
            delta = {}
            has_delta = False
            for field in ACTION_FIELDS:
                value = float(local_entry.get(field, 0.0)) - float(base_entry.get(field, 0.0))
                if abs(value) > 1e-12:
                    has_delta = True
                delta[field] = value
            if not has_delta:
                continue
            target = current["actions"].setdefault(
                key,
                {
                    "count": 0,
                    "reward_sum": 0.0,
                    "delta_sum": 0.0,
                    "positive_count": 0,
                    "improvement_bonus_count": 0,
                },
            )
            for field, value in delta.items():
                target[field] = target.get(field, 0) + value
            for integer_field in ("count", "positive_count", "improvement_bonus_count"):
                target[integer_field] = int(round(target.get(integer_field, 0)))

        local_updates = local.get("updates", [])
        if len(local_updates) > len(base_updates):
            current["updates"].extend(local_updates[len(base_updates):])

    write_json(global_path, current)
    return global_path


def main():
    args = parse_args()
    os.makedirs(args.output_dir, exist_ok=True)
    benchmark_stem = os.path.splitext(os.path.basename(args.benchmark))[0]
    global_experience_path = os.path.abspath(args.rl_experience_path)
    initial_memory = read_json(global_experience_path, {"version": 1, "actions": {}, "updates": []})
    initial_experience_path = os.path.join(args.output_dir, "_initial_rl_action_experience.json")
    write_json(initial_experience_path, initial_memory)

    print(
        "[GUI-RL] Starting parallel GCN+RL runs: "
        f"runs={args.runs}, max_workers={args.max_workers}, base_seed={args.base_seed}"
    )
    records = []
    with concurrent.futures.ThreadPoolExecutor(max_workers=min(args.max_workers, args.runs)) as executor:
        futures = [
            executor.submit(run_one, index, args, initial_experience_path)
            for index in range(args.runs)
        ]
        for future in concurrent.futures.as_completed(futures):
            record = future.result()
            records.append(record)
            status = "ok" if record["returncode"] == 0 and record.get("summary") else "failed"
            print(
                "[GUI-RL] Run finished: "
                f"run={record['index'] + 1}/{args.runs}, seed={record['seed']}, "
                f"status={status}, duration={record['duration_sec']:.2f}s"
            )

    records.sort(key=result_key)
    best = records[0] if records else None
    if best is None or best["returncode"] != 0 or not best.get("summary"):
        summary_path = os.path.join(args.output_dir, "gui_parallel_summary.json")
        write_json(summary_path, {"status": "failed", "runs": records})
        raise RuntimeError("All GCN+RL runs failed. See per-run stdout/stderr logs.")

    copy_best_artifacts(best, args.output_dir, benchmark_stem)
    merged_path = None
    if args.runs > 1:
        merged_path = merge_experience_delta(
            global_experience_path,
            initial_memory,
            [record["local_experience_path"] for record in records],
        )

    runner_summary = {
        "status": "ok",
        "benchmark": os.path.abspath(args.benchmark),
        "output_dir": os.path.abspath(args.output_dir),
        "runs": records,
        "best_run_index": int(best["index"]),
        "best_seed": int(best["seed"]),
        "best_summary": best["summary"],
        "merged_experience_path": merged_path or global_experience_path,
    }
    summary_path = os.path.join(args.output_dir, "gui_parallel_summary.json")
    write_json(summary_path, runner_summary)
    print(
        "[GUI-RL] Best run selected: "
        f"run={best['index'] + 1}, seed={best['seed']}, "
        f"failed_edges={best['summary'].get('best_failed_edges')}, "
        f"area={best['summary'].get('best_area')}"
    )
    print(f"[GUI-RL] Summary written to: {summary_path}")


if __name__ == "__main__":
    main()
