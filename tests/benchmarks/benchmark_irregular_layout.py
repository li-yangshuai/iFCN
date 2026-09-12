#!/usr/bin/env python3
"""Benchmark the single irregular-clock backend on every TOY/MAJ Verilog path.

The default scan preserves duplicate files. Only completed candidates passing
global routing/clock checks, exhaustive original-source truth, and native QCA
mapping/interface checks receive accepted area/cell metrics. Physical waveform
behavior is a separate check and is never inferred from this benchmark.
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
from concurrent.futures import ThreadPoolExecutor, as_completed
from datetime import datetime, timezone
import csv
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "src/python"))

import ifcn
from ifcn.frontend import normalize_verilog
from ifcn.layout_validation import dag_from_candidate, export_native_ifcn, validate_candidate
from ifcn.verify_logic import verify_bytes

# Freeze the actual imported tools even when callers select another input root.
PACKAGE_ROOT = Path(ifcn.__file__).resolve().parent
SNAPSHOT_SOURCES = {Path(__file__).name: Path(__file__).resolve(), **{
    "ifcn/" + name: PACKAGE_ROOT / name
    for name in ("__init__.py", "frontend.py", "layout_validation.py", "verify_logic.py", "logic.py")}}
BINARY_FILES = ("ifcn_combinational_pnr", "ifcn_energy_analysis")
TIMEOUT_GRACE_SECONDS = 10.0
SCOPE = ("Completed irregular-clock routing, independent geometry/global-clock DRC, exhaustive "
         "source-DAG truth up to 11 inputs, native device mapping DRC and exact QCA terminal labels; "
         "physical waveforms and single-cycle throughput are not certified.")


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def read_json(path):
    def number(value):
        result = float(value)
        if not math.isfinite(result):
            raise ValueError(f"Non-finite JSON value in {path}")
        return result
    def reject(value):
        raise ValueError(f"Non-finite JSON constant {value} in {path}")
    return json.loads(Path(path).read_text(), parse_float=number, parse_constant=reject)


def write_json(path, value):
    path = Path(path)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(value, indent=2, ensure_ascii=False, allow_nan=False) + "\n")
    temporary.replace(path)


def inventory(benchmark_root):
    """One record per path, including equal-content and equal-normalization files."""
    records = []
    for family in ("TOY", "MAJ"):
        folder = benchmark_root / family
        if not folder.is_dir():
            raise ValueError(f"Missing benchmark family: {folder}")
        for path in sorted(folder.rglob("*.v")):
            relative = path.relative_to(benchmark_root)
            records.append({"case": "__".join(relative.with_suffix("").parts),
                            "family": family, "source": str(Path("tests/benchmarks_f") / relative),
                            "source_path": str(path.resolve()), "source_sha256": sha256(path)})
    if not records or len({item["case"] for item in records}) != len(records):
        raise ValueError("Empty benchmark inventory or ambiguous case IDs")
    duplicates = defaultdict(list)
    for item in records:
        duplicates[item["source_sha256"]].append(item["case"])
    for item in records:
        item["same_content_cases"] = duplicates[item["source_sha256"]]
    return records


def command_result(command, directory, name, budget, env):
    """Kill the entire subprocess group on timeout, never accept a late file."""
    command = list(map(str, command))
    start = time.monotonic()
    metadata = {"command": command, "cwd": str(directory), "timeout_seconds": budget,
                "log": f"{name}.log", "timed_out": False}
    with (directory / f"{name}.log").open("w") as stream:
        process = subprocess.Popen(command, cwd=directory, env=env, stdout=stream,
                                   stderr=subprocess.STDOUT, start_new_session=True)
        try:
            metadata["exit_code"] = process.wait(timeout=budget)
        except subprocess.TimeoutExpired:
            metadata["timed_out"] = True
            try:
                os.killpg(process.pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
            try:
                process.wait(timeout=1)
            except subprocess.TimeoutExpired:
                os.killpg(process.pid, signal.SIGKILL)
                process.wait()
            metadata["exit_code"] = process.returncode
    metadata["seconds"] = round(time.monotonic() - start, 6)
    return metadata


def qca_interface(path, logic):
    """Count actual mapped cells and require an exact named terminal bijection."""
    text = Path(path).read_text()
    blocks = text.split("[TYPE:QCADCell]")[1:]
    if not blocks or text.count("[#TYPE:QCADCell]") != len(blocks):
        raise ValueError("Missing or incomplete QCA cells")
    terminals = {"INPUT": {}, "OUTPUT": {}}
    for block in blocks:
        block = block.split("[#TYPE:QCADCell]", 1)[0]
        functions = re.findall(r"^cell_function=QCAD_CELL_(\w+)\s*$", block, re.M)
        phases = re.findall(r"^cell_options.clock=(\d+)\s*$", block, re.M)
        if len(functions) != 1 or len(phases) != 1 or int(phases[0]) not in range(4):
            raise ValueError("Mapped cell has missing/invalid function or clock")
        # Validate numeric coordinates/options as well as JSON metrics.
        for value in re.findall(r"^(?:x|y|cell_options\.(?:cxCell|cyCell|dot_diameter))=([^\n]+)$", block, re.M):
            if not math.isfinite(float(value)):
                raise ValueError("Mapped QCA contains a non-finite coordinate or dimension")
        kind = functions[0]
        if kind not in terminals:
            continue
        labels = re.findall(r"^psz=([^\n]*)$", block, re.M)
        if len(labels) != 1 or not labels[0].strip():
            raise ValueError(f"Mapped {kind} terminal lacks one explicit label")
        label = labels[0].strip()
        if label in terminals[kind]:
            raise ValueError(f"Duplicate mapped {kind} label {label!r}")
        terminals[kind][label] = int(phases[0])
    expected_inputs = {item["source_port"] for item in logic["input_mapping"]}
    expected_outputs = {item["dag_node_name"] for item in logic["output_mapping"]}
    if set(terminals["INPUT"]) != expected_inputs or set(terminals["OUTPUT"]) != expected_outputs:
        raise ValueError(f"QCA terminals do not match proven source/DAG ports: {terminals}")
    return {"qca_cells": len(blocks), "qca_inputs": len(terminals["INPUT"]),
            "qca_outputs": len(terminals["OUTPUT"]), "terminals": terminals,
            "source_output_mapping": logic["output_mapping"], "qca_sha256": sha256(path)}


def true_value(value):
    return value is True or str(value).strip().lower() in ("true", "1", "yes")


def references(path):
    """Ignore failed/unknown references, including known physical failures."""
    if path is None:
        return {}
    best = {}
    with Path(path).open(newline="") as stream:
        for row in csv.DictReader(stream):
            corrected = "corrected_eligible" in row
            eligible = row["corrected_eligible"] if corrected else row.get("eligible_for_area_comparison", row.get("eligible", "false"))
            status = row.get("corrected_mapping_status" if corrected else "status", "validated")
            if not true_value(eligible) or status not in ("passed", "validated"):
                continue
            if row.get("physical_waveform_status", "").lower() in ("failed", "rejected"):
                continue
            if row.get("physical_passed", "") and not true_value(row["physical_passed"]):
                continue
            try:
                area = int(row["corrected_area_tiles" if corrected else "area_tiles"])
                cells = int(row["corrected_qca_cells" if corrected else "qca_cells"])
            except (KeyError, ValueError):
                continue
            if area <= 0 or cells <= 0:
                continue
            key = row.get("case") or row.get("source")
            if not key:
                continue
            value = {**row, "area_tiles": area, "qca_cells": cells}
            if key not in best or (area, cells) < (best[key]["area_tiles"], best[key]["qca_cells"]):
                best[key] = value
    return best


def run_case(item, output, binaries, budget, attempts, env, reference=None, *, export_candidates=False):
    directory = output / "cases" / item["case"]
    directory.mkdir(parents=True)
    started = time.monotonic()
    record = {key: value for key, value in item.items() if key != "source_path"}
    record.update(algorithm="irregular", status="running", eligible_for_area_comparison=False,
                  physical_waveform_status="not_run", commands=[], validation_scope=SCOPE,
                  solver_budget_seconds=budget, process_timeout_seconds=budget + TIMEOUT_GRACE_SECONDS)
    try:
        source = Path(item["source_path"]).read_bytes()
        (directory / "source.v").write_bytes(source)
        if sha256(directory / "source.v") != item["source_sha256"]:
            raise ValueError("Source changed after inventory creation")
        try:
            normalized, frontend = normalize_verilog(source.decode("utf-8-sig"))
        except (ValueError, UnicodeError) as error:
            record.update(status="normalization_failed", error=str(error))
            return record
        normalized_path = directory / "normalized.v"
        normalized_path.write_text(normalized)
        record.update(normalized_sha256=sha256(normalized_path), frontend=frontend)
        candidate_path = directory / "candidate.json"
        command = [binaries["ifcn_combinational_pnr"], "irregular", normalized_path, candidate_path,
                   "--budget", str(budget), "--attempts", str(attempts)]
        if export_candidates:
            candidates_directory = directory / "candidates"
            command += ["--candidates-dir", candidates_directory]
            record["candidate_pool_directory"] = str(candidates_directory)
            record["candidate_pool_requires_validation"] = True
        result = command_result(command, directory, "pnr", budget + TIMEOUT_GRACE_SECONDS, env)
        record["commands"].append(result)
        candidate = None
        if candidate_path.is_file():
            try:
                candidate = read_json(candidate_path)
                record["candidate_sha256"] = sha256(candidate_path)
                record["native_diagnostic"] = {key: candidate[key] for key in
                    ("routed", "native_mapping_valid", "native_validation_error", "run_time_s",
                     "selected_seed", "search_budget_expired", "metrics") if key in candidate}
                record["attempt_count"] = len(candidate.get("attempts", []))
                points = [(node["x"], node["y"]) for node in candidate.get("nodes", [])]
                points.extend(tuple(point) for route in candidate.get("routes", []) for point in route["path"])
                if points:
                    width = max(p[0] for p in points) - min(p[0] for p in points) + 1
                    height = max(p[1] for p in points) - min(p[1] for p in points) + 1
                    record.update(diagnostic_width=width, diagnostic_height=height,
                                  diagnostic_area_tiles=width * height)
            except (OSError, ValueError, KeyError, TypeError) as error:
                record["candidate_diagnostic_error"] = str(error)
        if result["timed_out"]:
            record["status"] = "timeout"
            return record
        if result["exit_code"] != 0:
            record["status"] = "native_failed"
            return record
        if candidate is None:
            raise ValueError("Native success did not produce a readable candidate")
        if candidate.get("routed") is not True or candidate.get("native_mapping_valid") is not True:
            record.update(status="native_rejected", error=candidate.get("native_validation_error", "Incomplete native candidate"))
            return record
        validation = validate_candidate(candidate, source_crossings=False)
        write_json(directory / "drc.json", validation)
        record["global_drc_passed"] = validation["passed"]
        if not validation["passed"]:
            record.update(status="drc_rejected", errors=validation["errors"])
            return record
        dag = dag_from_candidate(candidate, validation)
        write_json(directory / "routed_dag.json", dag)
        logic = verify_bytes(source, (directory / "routed_dag.json").read_bytes(), max_inputs=11)
        write_json(directory / "logic.json", logic)
        record.update(source_dag_status=logic["status"], vectors_checked=logic["vectors_checked"])
        if logic["status"] != "equivalent":
            record.update(status="logic_rejected", error=logic.get("reason", "Source/DAG truth mismatch"))
            return record
        layout = directory / "layout.ifcn"
        width, height = export_native_ifcn(candidate, layout, "irregular")
        result = command_result([binaries["ifcn_energy_analysis"], layout, directory / "mapped", "--qca-only"],
                                directory, "mapping", budget + TIMEOUT_GRACE_SECONDS, env)
        record["commands"].append(result)
        if result["timed_out"]:
            record["status"] = "timeout"
            return record
        if result["exit_code"] != 0:
            record["status"] = "mapping_failed"
            return record
        qca = directory / "mapped_energy_input.qca"
        try:
            interface = qca_interface(qca, logic)
        except (OSError, ValueError) as error:
            record.update(status="device_rejected", error=str(error))
            return record
        write_json(directory / "device.json", interface)
        record.update(status="validated", eligible_for_area_comparison=True,
                      width=width, height=height, area_tiles=width * height,
                      occupied_tiles=len({tuple(point) for route in candidate["routes"] for point in route["path"]}),
                      **{key: interface[key] for key in ("qca_cells", "qca_inputs", "qca_outputs", "qca_sha256")})
        if reference:
            if reference.get("source_sha256") and reference["source_sha256"] != record["source_sha256"]:
                record["reference_status"] = "source_hash_mismatch"
            else:
                record.update(reference_status="hash_matched" if reference.get("source_sha256") else "case_matched_without_hash",
                              reference_area_tiles=reference["area_tiles"], reference_qca_cells=reference["qca_cells"],
                              area_delta_tiles=record["area_tiles"] - reference["area_tiles"],
                              qca_delta_cells=record["qca_cells"] - reference["qca_cells"])
    except Exception as error:
        record.update(status="audit_error", error=f"{type(error).__name__}: {error}")
    finally:
        record["seconds"] = round(time.monotonic() - started, 6)
        write_json(directory / "result.json", record)
        write_json(directory / "commands.json", record["commands"])
    return record


def prepare_snapshot(output, build_dir):
    snapshot = output / "snapshot"
    snapshot.mkdir()
    files, binaries = [], {}
    for name in BINARY_FILES:
        source = build_dir / name
        if not source.is_file() or not os.access(source, os.X_OK):
            raise ValueError(f"Missing executable: {source}")
        target = snapshot / name
        shutil.copy2(source, target)
        binaries[name] = target
        files.append({"path": str(target.relative_to(output)), "source": str(source), "sha256": sha256(target)})
    for name, source in SNAPSHOT_SOURCES.items():
        target = snapshot / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target)
        files.append({"path": str(target.relative_to(output)), "source": str(source), "sha256": sha256(target)})
    return binaries, files


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=ROOT / "build")
    parser.add_argument("--output-dir", type=Path, required=True, help="Fresh run directory; existing results are never overwritten")
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--budget", type=float, default=120,
                        help="Soft solver budget in seconds; each native process has a hard timeout 10 seconds later (default 120/130)")
    parser.add_argument("--attempts", type=int, default=320)
    parser.add_argument("--export-candidates", action="store_true",
                        help="Retain native candidate snapshots in each case's candidates directory for separate validation")
    parser.add_argument("--reference-csv", type=Path, help="Optional validated reference rows with case, area_tiles, qca_cells and eligible flag")
    args = parser.parse_args(argv)
    if args.jobs < 1 or args.attempts < 1 or not math.isfinite(args.budget) or args.budget <= 0:
        parser.error("jobs, attempts and finite budget must be positive")
    output, build = args.output_dir.resolve(), args.build_dir.resolve()
    if output.exists() and (not output.is_dir() or any(output.iterdir())):
        parser.error("output-dir must be empty; use a new directory to preserve prior evidence")
    output.mkdir(parents=True, exist_ok=True)
    try:
        items = inventory(ROOT / "tests/benchmarks_f")
        reference = references(args.reference_csv)
        binaries, files = prepare_snapshot(output, build)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    env = os.environ.copy()
    overrides = {name: "1" for name in ("OMP_NUM_THREADS", "OPENBLAS_NUM_THREADS", "MKL_NUM_THREADS", "NUMEXPR_NUM_THREADS")}
    env.update(overrides)
    manifest = {"schema": "ifcn.irregular_benchmark.v1", "created_at": datetime.now(timezone.utc).isoformat(),
                "case_count": len(items), "jobs": args.jobs, "solver_budget_seconds": args.budget,
                "process_timeout_seconds": args.budget + TIMEOUT_GRACE_SECONDS,
                "timeout_grace_seconds": TIMEOUT_GRACE_SECONDS,
                "attempts": args.attempts, "algorithm": "irregular", "environment_overrides": overrides,
                "export_candidates": args.export_candidates,
                "python": sys.version, "frozen_files": files, "scope": SCOPE}
    if args.reference_csv:
        shutil.copy2(args.reference_csv, output / "reference.csv")
        manifest["reference_sha256"] = sha256(output / "reference.csv")
    write_json(output / "manifest.json", manifest)
    write_json(output / "inventory.json", items)
    records = []
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = [pool.submit(run_case, item, output, binaries, args.budget, args.attempts, env,
                               reference.get(item["case"], reference.get(item["source"])),
                               export_candidates=args.export_candidates) for item in items]
        for future in as_completed(futures):
            record = future.result()
            records.append(record)
            write_json(output / "progress.json", records)
            print(json.dumps({key: record.get(key) for key in ("case", "status", "seconds", "area_tiles", "qca_cells")}), flush=True)
    order = {item["case"]: index for index, item in enumerate(items)}
    records.sort(key=lambda item: order[item["case"]])
    write_json(output / "summary.json", records)
    fields = ["case", "family", "source", "status", "eligible_for_area_comparison", "seconds",
              "solver_budget_seconds", "process_timeout_seconds",
              "diagnostic_width", "diagnostic_height", "diagnostic_area_tiles", "attempt_count",
              "width", "height", "area_tiles", "occupied_tiles", "qca_cells", "qca_inputs", "qca_outputs",
              "global_drc_passed", "source_dag_status", "vectors_checked", "physical_waveform_status",
              "reference_status", "reference_area_tiles", "reference_qca_cells", "area_delta_tiles", "qca_delta_cells",
              "source_sha256", "normalized_sha256", "candidate_sha256", "qca_sha256", "error"]
    with (output / "summary.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(records)
    changed = [item["path"] for item in files if sha256(output / item["path"]) != item["sha256"]]
    totals = {"case_count": len(records), "status_counts": dict(Counter(item["status"] for item in records)),
              "validated": sum(item["eligible_for_area_comparison"] for item in records),
              "frozen_files_unchanged": not changed, "changed_files": changed, "scope": SCOPE}
    write_json(output / "totals.json", totals)
    print(json.dumps(totals), flush=True)
    return 0 if not changed and all(item["eligible_for_area_comparison"] for item in records) else 1


if __name__ == "__main__":
    raise SystemExit(main())
