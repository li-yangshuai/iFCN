#!/usr/bin/env python3
import argparse
import concurrent.futures
import datetime as dt
import json
import os
import subprocess
import time
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[3]
GCN_RL_ROOT = PROJECT_ROOT / "include" / "gcn_rl_layout"
DEFAULT_LOG_ROOT = GCN_RL_ROOT / "results" / "batch_runs"
SKIP_STEMS = {"FA", "FS", "HA", "HS"}


def parse_args():
    parser = argparse.ArgumentParser(
        description="Run iFCN power analysis in parallel as soon as GCN+RL layout files appear.",
    )
    parser.add_argument(
        "--benchmark-dir",
        action="append",
        default=[],
        help="Directory containing Verilog benchmarks. Can be repeated.",
    )
    parser.add_argument("--energy-exe", default=str(PROJECT_ROOT / "build" / "ifcn_energy_analysis"))
    parser.add_argument("--workers", type=int, default=4)
    parser.add_argument("--poll-sec", type=float, default=10.0)
    parser.add_argument("--timeout-sec", type=float, default=45 * 60)
    parser.add_argument("--idle-timeout-sec", type=float, default=12 * 60 * 60)
    parser.add_argument("--log-dir", default=str(DEFAULT_LOG_ROOT))
    parser.add_argument("--fast", action=argparse.BooleanOptionalAction, default=True)
    parser.add_argument("--waveform", action="store_true")
    return parser.parse_args()


def resolve_path(path):
    candidate = Path(path)
    if not candidate.is_absolute():
        candidate = PROJECT_ROOT / candidate
    return candidate.resolve()


def discover_benchmarks(dirs):
    benchmarks = []
    seen = set()
    for directory in dirs:
        bench_dir = resolve_path(directory)
        if not bench_dir.is_dir():
            continue
        for path in sorted(bench_dir.glob("*.v")):
            if path.stem in SKIP_STEMS or path.stem.endswith("_parser_safe"):
                continue
            resolved = path.resolve()
            if resolved in seen:
                continue
            seen.add(resolved)
            benchmarks.append(resolved)
    return benchmarks


def write_json(path, payload):
    path.parent.mkdir(parents=True, exist_ok=True)
    temp_path = path.with_suffix(path.suffix + f".tmp.{os.getpid()}")
    with open(temp_path, "w", encoding="utf-8") as handle:
        json.dump(payload, handle, ensure_ascii=False, indent=2)
    os.replace(temp_path, path)


def output_paths(benchmark):
    stem = benchmark.stem
    out_dir = benchmark.parent / f"{stem}_gcn_rl_layout"
    ifcn_path = out_dir / f"{stem}_rl_layout.ifcn"
    report_path = out_dir / f"{stem}_rl_layout_energy.txt"
    return out_dir, ifcn_path, report_path


def run_energy(args, benchmark, log):
    stem = benchmark.stem
    group = benchmark.parent.name
    out_dir, ifcn_path, report_path = output_paths(benchmark)
    lock_path = out_dir / f".{stem}_energy.lock"
    record = {
        "group": group,
        "benchmark": stem,
        "source": str(benchmark),
        "ifcn": str(ifcn_path),
        "report": str(report_path),
    }

    if report_path.exists():
        record["status"] = "skipped-existing"
        return record
    try:
        fd = os.open(lock_path, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
        os.write(fd, str(os.getpid()).encode("utf-8"))
        os.close(fd)
    except FileExistsError:
        record["status"] = "locked"
        return record

    command = [str(resolve_path(args.energy_exe)), str(ifcn_path)]
    if args.fast:
        command.append("--fast")
    if args.waveform:
        command.append("--waveform")

    stdout_path = out_dir / f"{stem}_energy_watcher_stdout.log"
    stderr_path = out_dir / f"{stem}_energy_watcher_stderr.log"
    env = os.environ.copy()
    env["MPLBACKEND"] = "Agg"

    log(f"{group}/{stem}: power start workers-fast={args.fast}")
    start = time.perf_counter()
    try:
        with open(stdout_path, "w", encoding="utf-8") as stdout_file, open(
            stderr_path,
            "w",
            encoding="utf-8",
        ) as stderr_file:
            completed = subprocess.run(
                command,
                cwd=str(PROJECT_ROOT),
                env=env,
                stdout=stdout_file,
                stderr=stderr_file,
                text=True,
                timeout=args.timeout_sec,
                check=False,
            )
        duration = time.perf_counter() - start
        record.update(
            {
                "status": "ok" if completed.returncode == 0 and report_path.exists() else "failed",
                "returncode": int(completed.returncode),
                "duration_sec": float(duration),
                "stdout_log": str(stdout_path),
                "stderr_log": str(stderr_path),
                "command": command,
            }
        )
    except subprocess.TimeoutExpired:
        duration = time.perf_counter() - start
        record.update(
            {
                "status": "timeout",
                "duration_sec": float(duration),
                "stdout_log": str(stdout_path),
                "stderr_log": str(stderr_path),
                "command": command,
            }
        )
    finally:
        try:
            lock_path.unlink()
        except FileNotFoundError:
            pass

    log(f"{group}/{stem}: power {record['status']} duration={record.get('duration_sec', 0.0):.1f}s")
    return record


def main():
    args = parse_args()
    benchmark_dirs = args.benchmark_dir or [
        "tests/benchmarks_f/TOY",
        "tests/benchmarks_f/MAJ",
    ]
    benchmarks = discover_benchmarks(benchmark_dirs)
    log_dir = resolve_path(args.log_dir)
    log_dir.mkdir(parents=True, exist_ok=True)
    stamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
    log_path = log_dir / f"parallel_power_analysis_{stamp}.log"
    summary_path = log_path.with_suffix(".json")

    def log(message):
        line = f"[{dt.datetime.now().strftime('%Y-%m-%d %H:%M:%S')}] {message}"
        print(line, flush=True)
        with open(log_path, "a", encoding="utf-8") as handle:
            handle.write(line + "\n")

    def completed_count():
        return sum(1 for benchmark in benchmarks if output_paths(benchmark)[2].exists())

    max_workers = max(1, int(args.workers))
    poll_sec = max(1.0, float(args.poll_sec))
    results = []
    running = {}
    finished_keys = set()
    last_progress = time.perf_counter()
    last_complete = completed_count()
    log(
        f"parallel power analysis start total={len(benchmarks)} "
        f"reports={last_complete}/{len(benchmarks)} workers={max_workers}"
    )

    with concurrent.futures.ThreadPoolExecutor(max_workers=max_workers) as executor:
        while True:
            for future in [item for item in running if item.done()]:
                benchmark = running.pop(future)
                key = f"{benchmark.parent.name}/{benchmark.stem}"
                try:
                    record = future.result()
                except Exception as exc:
                    record = {
                        "group": benchmark.parent.name,
                        "benchmark": benchmark.stem,
                        "source": str(benchmark),
                        "status": "exception",
                        "error": str(exc),
                    }
                    log(f"{key}: power exception {exc}")
                results.append(record)
                write_json(summary_path, results)
                if record.get("status") in {"ok", "skipped-existing"}:
                    finished_keys.add(key)
                    last_progress = time.perf_counter()

            current_complete = completed_count()
            if current_complete != last_complete:
                log(
                    f"parallel power analysis progress "
                    f"reports={current_complete}/{len(benchmarks)} active={len(running)}"
                )
                last_complete = current_complete
                last_progress = time.perf_counter()
            if current_complete >= len(benchmarks):
                break

            available = []
            for benchmark in benchmarks:
                key = f"{benchmark.parent.name}/{benchmark.stem}"
                if key in finished_keys or any(active == benchmark for active in running.values()):
                    continue
                _, ifcn_path, report_path = output_paths(benchmark)
                if report_path.exists():
                    finished_keys.add(key)
                elif ifcn_path.exists():
                    available.append(benchmark)

            free_slots = max_workers - len(running)
            for benchmark in available[:max(0, free_slots)]:
                running[executor.submit(run_energy, args, benchmark, log)] = benchmark

            if not running and not available and time.perf_counter() - last_progress > args.idle_timeout_sec:
                log("parallel power analysis idle timeout")
                break
            time.sleep(poll_sec)

        while running:
            time.sleep(2.0)
            for future in [item for item in running if item.done()]:
                benchmark = running.pop(future)
                try:
                    results.append(future.result())
                except Exception as exc:
                    results.append(
                        {
                            "group": benchmark.parent.name,
                            "benchmark": benchmark.stem,
                            "source": str(benchmark),
                            "status": "exception",
                            "error": str(exc),
                        }
                    )
                write_json(summary_path, results)

    log(
        f"parallel power analysis finish reports={completed_count()}/{len(benchmarks)} "
        f"summary={summary_path}"
    )


if __name__ == "__main__":
    main()
