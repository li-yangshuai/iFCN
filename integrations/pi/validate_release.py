#!/usr/bin/env python3
"""Real production checks for logic/interface rejection, auto recovery and all stages."""
from concurrent.futures import ThreadPoolExecutor, as_completed
import argparse
from datetime import datetime, timezone
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
RUNNER = ROOT / "scripts/ifcn_agent.py"
FROZEN = ROOT / "tests/benchmarks_pi/v1"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--case", action="append", help="Run only this predefined case id; repeatable")
    parser.add_argument("--timeout", type=int, default=180)
    args = parser.parse_args()
    output = ROOT / "output/pi-validation" / ("release-logic-io-" + datetime.now(timezone.utc).strftime("%Y%m%dT%H%M%SZ"))
    output.mkdir(parents=True)
    runs = output / "runs"
    cases = [{"id": "xor_full_" + name, "input": "TOY/xor2.v", "algorithm": name, "until": "energy", "expect": "completed"}
             for name in ("normal_2ddwave", "compact", "june_random")]
    cases += [
        {"id": "missing_logical_outputs_rejected", "input": "MAJ/clpl.v", "algorithm": "normal_2ddwave", "until": "map", "expect": "failed"},
        {"id": "auto_recovers_logical_outputs", "input": "MAJ/clpl.v", "algorithm": "auto", "until": "map", "expect": "completed", "selected": "june_random"},
        {"id": "all_missing_device_outputs_rejected", "input": "TOY/clpl.v", "algorithm": "auto", "until": "map", "expect": "failed"}]
    if args.case:
        if set(args.case) - {case["id"] for case in cases}:
            parser.error("Unknown release case id")
        cases = [case for case in cases if case["id"] in args.case]
    if args.timeout <= 0:
        parser.error("timeout must be positive")
    (output / "protocol.json").write_text(json.dumps({"cases": cases, "per_command_timeout_s": args.timeout,
        "model": "both", "energy_profile": "standard", "workers": 2, "note": "Targeted rechecks are separate records; original failures are retained."}, indent=2) + "\n")
    records = []

    def call(*args):
        process = subprocess.run([sys.executable, str(RUNNER), "--runs-dir", str(runs), *map(str, args)],
                                 capture_output=True, text=True, timeout=20)
        response = json.loads(process.stdout)
        if process.returncode:
            raise RuntimeError(response.get("error", process.stderr))
        return response

    def check(case):
        begin = time.monotonic()
        state = {}
        record = dict(case)
        try:
            state = call("start", FROZEN / case["input"], "--until", case["until"], "--algorithm", case["algorithm"],
                         "--model", "both", "--timeout", args.timeout, "--seed", 1)
            while state.get("active") and time.monotonic() - begin < args.timeout * 6 + 30:
                time.sleep(1)
                state = call("status", state["run_id"])
            if state.get("active"):
                raise TimeoutError("Release validation exceeded its total harness limit")
            okay = (state["status"] in {"completed", "completed_with_warnings"}
                    if case["expect"] == "completed" else state["status"] == "failed")
            selection = state.get("metrics", {}).get("algorithm_selection", {})
            if case.get("selected"):
                okay = okay and selection.get("selected_algorithm") == case["selected"] and any(
                    item["status"] == "failed" for item in selection["attempts"])
            if case["expect"] == "completed":
                okay = okay and state["validation"].get("rtl_equivalence") == "exhaustive_scalar_source_vs_routed_dag"
                okay = okay and state["validation"].get("device_terminal_counts") == "match_source_declarations"
                okay = okay and state["stages"][case["until"]]["status"] == "completed"
                if case["until"] == "energy":
                    okay = okay and len(state["metrics"].get("simulation", [])) == 2
            else:
                okay = okay and state.get("current_stage") == "pnr" and "map" not in state["stages"]
                if case["algorithm"] == "auto":
                    okay = okay and len(selection.get("attempts", [])) == 3
                    okay = okay and all("QCA interface incomplete" in item.get("error", "") for item in selection["attempts"])
                else:
                    okay = okay and "Routed logic validation unsupported" in state.get("error", "")
            mismatch = []
            for name, item in state["artifacts"].items():
                file = Path(state["run_directory"]) / item["path"]
                if hashlib.sha256(file.read_bytes()).hexdigest() != item["sha256"]:
                    mismatch.append(name)
            record.update(passed=bool(okay and not mismatch), run_id=state["run_id"], status=state["status"],
                          error=state.get("error"), validation=state.get("validation"), metrics=state.get("metrics"),
                          run_directory=state["run_directory"], artifact_mismatches=mismatch)
        except Exception as exc:
            record.update(passed=False, error=str(exc))
        finally:
            if state.get("active"):
                try:
                    call("cancel", state["run_id"])
                except Exception as exc:
                    record["cancel_error"] = str(exc)
        record["wall_time_s"] = round(time.monotonic() - begin, 3)
        return record

    print(f"Release evidence: {output}", flush=True)
    with ThreadPoolExecutor(max_workers=2) as pool:
        futures = [pool.submit(check, case) for case in cases]
        for future in as_completed(futures):
            record = future.result()
            records.append(record)
            (output / "results.json").write_text(json.dumps({"scope": "Real pipeline/rejection checks; no LLM; no physical waveform or converged power claim", "records": records}, ensure_ascii=False, indent=2) + "\n")
            print(f"{len(records)}/{len(cases)} {record['id']}: {'PASS' if record['passed'] else 'FAIL'} ({record.get('status')})", flush=True)
    if not all(item["passed"] for item in records):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
