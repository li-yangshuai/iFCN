#!/usr/bin/env python3
"""Evaluate frozen inputs through the production job API; retain every attempted case."""
import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import csv
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
RUNNER = ROOT / "scripts/ifcn_agent.py"


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=ROOT / "tests/benchmarks_pi/v1/manifest.json")
    parser.add_argument("--algorithm", action="append", choices=["normal_2ddwave", "compact", "june_random", "auto"])
    parser.add_argument("--case", action="append", help="Frozen case id; repeatable")
    parser.add_argument("--timeout", type=int, default=180)
    parser.add_argument("--workers", type=int, default=2)
    parser.add_argument("--until", choices=["map", "simulate", "energy"], default="map")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.timeout <= 0 or args.workers <= 0:
        parser.error("timeout and workers must be positive")
    document = json.loads(args.manifest.read_text())
    cases = document["cases"]
    if args.case:
        selected = set(args.case)
        if selected - {item["id"] for item in cases}:
            parser.error("Unknown frozen case id")
        cases = [item for item in cases if item["id"] in selected]
    if not cases:
        parser.error("No frozen cases selected")
    algorithms = list(dict.fromkeys(args.algorithm or ["normal_2ddwave", "compact", "june_random"]))
    for case in cases:
        if sha(ROOT / case["frozen_relpath"]) != case["frozen_sha256"]:
            raise ValueError(f"Frozen source changed: {case['id']}")
    output = args.output or ROOT / "output/pi-validation" / ("backend-comparison-" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ"))
    output.mkdir(parents=True, exist_ok=False)
    runs = output / "runs"
    code_paths = [RUNNER, ROOT / "scripts/ifcn_agent_backend.py", ROOT / "scripts/ifcn_agent_frontend.py",
                  ROOT / "scripts/ifcn_agent_layout_validation.py", ROOT / "src/app/ifcn_combinational_pnr.cpp"]
    code_hashes = {str(p.relative_to(ROOT)): sha(p) for p in code_paths if p.is_file()}
    probe = subprocess.run([sys.executable, str(RUNNER), "doctor"], capture_output=True, text=True, timeout=20, check=True)
    config = json.loads(probe.stdout)["configuration"]
    runtime_paths = {name: Path(config[name]).resolve() for name in
                     ("python", "backend", "native_pnr_binary", "mapping_binary", "simulation_binary", "energy_binary")}
    runtime_hashes = {name: {"path": str(path), "sha256": sha(path) if path.is_file() else None}
                      for name, path in runtime_paths.items()}
    normal_root = ROOT / "include/gcn_rl_layout/src/algorithm"
    normal_hashes = {str(path.relative_to(ROOT)): sha(path) for pattern in ("*.py", "*.so")
                     for path in sorted(normal_root.rglob(pattern))}
    protocol = {"schema": "ifcn.backend.comparison.v1", "created_at": datetime.now(timezone.utc).isoformat(),
                "manifest": str(args.manifest.resolve()), "manifest_sha256": sha(args.manifest),
                "algorithms": algorithms, "cases": [item["id"] for item in cases], "seed": 1,
                "timeout_s": args.timeout, "workers": args.workers, "until": args.until,
                "model": "bistable", "samples": 512, "code_sha256": code_hashes,
                "configuration": config, "runtime_sha256": runtime_hashes, "normal_implementation_sha256": normal_hashes,
                "seed_control": "normal exposes seed; compact/june internal seed control is not exposed",
                "comparison_unit": "end-to-end pipelines from identical frozen Verilog; normal may apply equivalent parity canonicalization, so these are not necessarily identical-DAG P&R comparisons",
                "scope": "Production API coverage and layout metrics; no LLM, RTL/device equivalence, or power-accuracy claim. Runtime includes shared-host concurrency.",
                "failure_policy": "One run per frozen input and explicit algorithm; no retry or omitted failures."}
    (output / "protocol.json").write_text(json.dumps(protocol, indent=2) + "\n")
    rows = []

    def call(*values):
        completed = subprocess.run([sys.executable, str(RUNNER), "--runs-dir", str(runs), *map(str, values)],
                                   capture_output=True, text=True, timeout=20)
        response = json.loads(completed.stdout)
        if completed.returncode:
            raise RuntimeError(response.get("error", completed.stderr))
        return response

    def check(case, algorithm):
        row = {"case": case["id"], "function_group": case["function_group"], "algorithm": algorithm,
               "input_sha256": case["frozen_sha256"]}
        state = {}
        start = time.monotonic()
        try:
            state = call("start", ROOT / case["frozen_relpath"], "--algorithm", algorithm, "--until", args.until,
                         "--timeout", args.timeout, "--seed", 1, "--samples", 512)
            row["run_id"] = state["run_id"]
            while state.get("active") and time.monotonic() - start < args.timeout * 7 + 30:
                time.sleep(1)
                state = call("status", row["run_id"])
            if state.get("active"):
                call("cancel", row["run_id"])
                raise TimeoutError("Evaluation harness time limit")
            mismatches = []
            for name, item in state.get("artifacts", {}).items():
                path = Path(state["run_directory"]) / item["path"]
                if not path.is_file() or sha(path) != item["sha256"]:
                    mismatches.append(name)
            completed = state.get("status") in {"completed", "completed_with_warnings"}
            row.update(status=state["status"], task_completed=completed and not mismatches,
                       stage=state.get("current_stage"), error=state.get("error"),
                       metrics=state.get("metrics", {}), validation=state.get("validation", {}),
                       warnings=state.get("warnings", []), artifact_mismatches=mismatches,
                       artifact_count=len(state.get("artifacts", {})), run_directory=state.get("run_directory"))
            if "pnr.json" in state.get("artifacts", {}):
                pnr = Path(state["run_directory"]) / state["artifacts"]["pnr.json"]["path"]
                candidate_path = pnr.parent / "layout_candidate.json"
                if candidate_path.is_file():
                    candidate = json.loads(candidate_path.read_text())
                    points = {(node["x"], node["y"]) for node in candidate["nodes"]}
                    points.update(tuple(point) for route in candidate["routes"] for point in route["path"])
                    if points:
                        width = max(p[0] for p in points) - min(p[0] for p in points) + 1
                        height = max(p[1] for p in points) - min(p[1] for p in points) + 1
                        row["occupied_bounds"] = {"width": width, "height": height, "area_tiles": width * height,
                            "definition": "bounding rectangle of every actual gate and routed grid point; excludes unused margins",
                            "candidate_path": str(candidate_path), "candidate_sha256": sha(candidate_path)}
        except Exception as exc:
            row.update(status="harness_error", task_completed=False, error=str(exc))
            if state.get("active"):
                try:
                    call("cancel", state["run_id"])
                except Exception as cancel_error:
                    row["cancel_error"] = str(cancel_error)
        row["wall_time_s"] = round(time.monotonic() - start, 3)
        return row

    def save():
        result = {"protocol": protocol, "finished": len(rows), "total": len(cases) * len(algorithms),
                  "by_algorithm": {name: {"completed": sum(r["task_completed"] for r in rows if r["algorithm"] == name),
                                            "attempted": sum(r["algorithm"] == name for r in rows)} for name in algorithms},
                  "rows": sorted(rows, key=lambda row: (row["case"], row["algorithm"]))}
        (output / "results.json").write_text(json.dumps(result, ensure_ascii=False, indent=2) + "\n")
        with (output / "results.csv").open("w", newline="") as handle:
            fields = ["case", "function_group", "algorithm", "status", "task_completed", "wall_time_s", "area_tiles", "occupied_bbox_area_tiles", "cell_count", "error", "run_id"]
            writer = csv.DictWriter(handle, fieldnames=fields)
            writer.writeheader()
            for row in result["rows"]:
                metrics = row.get("metrics", {})
                writer.writerow({**{name: row.get(name) for name in fields},
                                 "area_tiles": metrics.get("pnr", {}).get("area_tiles"),
                                 "occupied_bbox_area_tiles": row.get("occupied_bounds", {}).get("area_tiles"),
                                 "cell_count": metrics.get("mapping", {}).get("cell_count")})

    print(f"Evidence directory: {output}", flush=True)
    with ThreadPoolExecutor(max_workers=args.workers) as pool:
        pending = {pool.submit(check, case, algorithm): (case, algorithm) for case in cases for algorithm in algorithms}
        for future in as_completed(pending):
            try:
                row = future.result()
            except Exception as exc:
                case, algorithm = pending[future]
                row = {"case": case["id"], "function_group": case["function_group"], "algorithm": algorithm,
                       "status": "harness_error", "task_completed": False, "error": str(exc),
                       "input_sha256": case["frozen_sha256"]}
            rows.append(row)
            save()
            print(f"{len(rows)}/{len(pending)} {row['case']} {row['algorithm']}: {row['status']}", flush=True)
    final_checks = {"code_unchanged": all(sha(ROOT / name) == value for name, value in code_hashes.items()),
                    "frozen_inputs_unchanged": all(sha(ROOT / case["frozen_relpath"]) == case["frozen_sha256"] for case in cases),
                    "manifest_unchanged": sha(args.manifest) == protocol["manifest_sha256"],
                    "runtime_unchanged": all((sha(Path(item["path"])) if Path(item["path"]).is_file() else None) == item["sha256"] for item in runtime_hashes.values()),
                    "normal_implementation_unchanged": all((ROOT / name).is_file() and sha(ROOT / name) == value for name, value in normal_hashes.items())}
    final_checks["eligible_for_comparison"] = all(final_checks.values())
    (output / "final-checks.json").write_text(json.dumps(final_checks, indent=2) + "\n")
    print(json.dumps({"completed": sum(row["task_completed"] for row in rows), "total": len(rows), **final_checks}), flush=True)


if __name__ == "__main__":
    main()
