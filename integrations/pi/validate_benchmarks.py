#!/usr/bin/env python3
"""Bounded, reproducible coverage check of the installed adapter, not an LLM benchmark."""
from concurrent.futures import ThreadPoolExecutor, as_completed
from datetime import datetime, timezone
import argparse
import csv
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
RUNNER = ROOT / "scripts/ifcn_agent.py"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--timeout", type=int, default=60)
    parser.add_argument("--workers", type=int, default=3)
    parser.add_argument("--until", choices=["map", "simulate", "energy"], default="simulate")
    parser.add_argument("--case", action="append", help="Run only this relative benchmark path, e.g. MAJ/RCA2.v; repeatable")
    args = parser.parse_args()
    timestamp = datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    output = ROOT / "output/pi-validation" / f"coverage-{timestamp}"
    output.mkdir(parents=True)
    cases = sorted((ROOT / "tests/benchmarks_f/TOY").glob("*.v")) + sorted((ROOT / "tests/benchmarks_f/MAJ").glob("*.v"))
    if args.case:
        by_name = {str(path.relative_to(ROOT / "tests/benchmarks_f")): path for path in cases}
        unknown = sorted(set(args.case) - set(by_name))
        if unknown:
            parser.error(f"Unknown benchmark paths: {', '.join(unknown)}")
        cases = [by_name[name] for name in dict.fromkeys(args.case)]
    rows = []
    metadata = {"created_at": timestamp, "algorithm": "current Pi adapter: normal graph draw, four-phase 2DDWave",
                "real_llm_used": False, "seed": 1, "per_command_timeout_s": args.timeout,
                "workers": args.workers, "until": args.until, "model": "bistable", "samples": 512,
                "note": "Results measure this adapter configuration. Failure does not imply all native iFCN algorithms fail. Simulation compares numerical engines, not RTL functional equivalence."}

    def call(*values):
        process = subprocess.run([sys.executable, str(RUNNER), *map(str, values)], capture_output=True, text=True, timeout=15)
        return process.returncode, json.loads(process.stdout)

    def check(source):
        begin = time.monotonic()
        row = {"circuit": str(source.relative_to(ROOT / "tests/benchmarks_f")),
               "input_sha256": hashlib.sha256(source.read_bytes()).hexdigest()}
        run_id = None
        state = {}
        try:
            code, state = call("start", source, "--until", args.until, "--timeout", args.timeout,
                               "--seed", 1, "--samples", 512, "--model", "bistable")
            if code:
                row.update(status="rejected", stage="frontend", error=state.get("error"))
                return row
            run_id = state["run_id"]
            while state.get("active") and time.monotonic() - begin < args.timeout * 7 + 30:
                time.sleep(1)
                _, state = call("status", run_id)
            if state.get("active"):
                call("cancel", run_id)
                row.update(status="harness_timeout", stage=state.get("current_stage"))
            else:
                row.update(status=state["status"], stage=state.get("current_stage"), error=state.get("error"))
            row.update(run_id=run_id, metrics=state.get("metrics", {}), stages=state.get("stages", {}),
                       artifacts=state.get("artifacts", {}), frontend=state.get("frontend", {}))
        except Exception as exc:
            row.update(status="harness_error", error=str(exc))
            if run_id:
                call("cancel", run_id)
        finally:
            row["wall_time_s"] = round(time.monotonic() - begin, 3)
        return row

    def save():
        record = {**metadata, "finished": len(rows), "total": len(cases), "results": sorted(rows, key=lambda x: x["circuit"])}
        (output / "results.json").write_text(json.dumps(record, ensure_ascii=False, indent=2) + "\n")

    print(f"Writing coverage evidence to {output}", flush=True)
    with ThreadPoolExecutor(max_workers=args.workers) as pool:
        futures = [pool.submit(check, source) for source in cases]
        for future in as_completed(futures):
            row = future.result()
            rows.append(row)
            save()
            print(f"{len(rows)}/{len(cases)} {row['circuit']}: {row['status']} at {row.get('stage')} ({row['wall_time_s']} s)", flush=True)
    with (output / "results.csv").open("w", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=["circuit", "status", "stage", "wall_time_s", "area_tiles", "cell_count", "run_id"])
        writer.writeheader()
        for row in sorted(rows, key=lambda x: x["circuit"]):
            writer.writerow({**{key: row.get(key) for key in ["circuit", "status", "stage", "wall_time_s", "run_id"]},
                             "area_tiles": row.get("metrics", {}).get("pnr", {}).get("area_tiles"),
                             "cell_count": row.get("metrics", {}).get("mapping", {}).get("cell_count")})
    print(json.dumps({"output": str(output), "completed": sum(r["status"] == "completed" for r in rows), "total": len(rows)}), flush=True)


if __name__ == "__main__":
    main()
