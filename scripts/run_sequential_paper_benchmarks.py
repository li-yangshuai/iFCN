#!/usr/bin/env python3
"""Run paper-derived sequential RTL through the reproducible iFCN pipeline.

The runner deliberately uses only the Python standard library.  It validates
each provenance manifest before reading it, synthesizes the declared RTL with
Yosys, converts the result to SeqIR and a legacy cut DAG, expands every state
boundary from ``state.json``, and finally invokes ``ifcn_sequential_pnr``.

Level-sensitive latch manifests are successful only when the SeqIR importer
rejects an unsupported latch explicitly.  Edge-triggered and sampled-state
manifests are successful only when every expected runnable stage completes.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Mapping, Sequence


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_BENCHMARK_ROOT = (
    REPOSITORY_ROOT / "tests" / "benchmarks_f" / "SEQUENTIAL" / "papers"
)
DEFAULT_OUTPUT_ROOT = (
    REPOSITORY_ROOT / "build" / "artifacts" / "sequential_paper_benchmarks"
)
DEFAULT_VALIDATOR = REPOSITORY_ROOT / "scripts" / "validate_sequential_paper_benchmark.py"
DEFAULT_CONVERTER = REPOSITORY_ROOT / "scripts" / "yosys_json_to_seqir.py"
DEFAULT_YOSYS = REPOSITORY_ROOT / "build" / "tools" / "yosys-local" / "usr" / "bin" / "yosys"
DEFAULT_PNR = REPOSITORY_ROOT / "build" / "ifcn_sequential_pnr"
DEFAULT_CYCLIC_PNR = REPOSITORY_ROOT / "build" / "ifcn_paper_cyclic_pnr"
SUMMARY_SCHEMA = "ifcn.sequential-paper-benchmark-run.v0"
CASE_SCHEMA = "ifcn.sequential-paper-benchmark-case.v0"


class RunnerError(RuntimeError):
    """A deterministic benchmark or runner configuration error."""


def _utc_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds")


def _atomic_write_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(
        prefix=f".{path.name}.", suffix=".tmp", dir=str(path.parent), text=True
    )
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="\n") as handle:
            handle.write(text)
        os.replace(temporary, path)
    except BaseException:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass
        raise


def _write_json(path: Path, payload: Any) -> None:
    _atomic_write_text(
        path,
        json.dumps(payload, indent=2, sort_keys=True, ensure_ascii=False) + "\n",
    )


def _sha256(path: Path) -> str | None:
    try:
        digest = hashlib.sha256()
        with path.open("rb") as handle:
            for chunk in iter(lambda: handle.read(1024 * 1024), b""):
                digest.update(chunk)
        return digest.hexdigest()
    except OSError:
        return None


def _repo_path(raw: str | Path) -> Path:
    path = Path(raw).expanduser()
    return path.resolve() if path.is_absolute() else (REPOSITORY_ROOT / path).resolve()


def _display_path(path: Path, base: Path = REPOSITORY_ROOT) -> str:
    try:
        return path.resolve().relative_to(base.resolve()).as_posix()
    except ValueError:
        return str(path.resolve())


def _executable_path(raw: str | Path, env: Mapping[str, str]) -> str:
    text = str(raw)
    path = Path(text).expanduser()
    if path.is_absolute() or len(path.parts) > 1:
        return str(_repo_path(path))
    located = shutil.which(text, path=env.get("PATH"))
    return located if located is not None else text


def _command_prefix(raw: str | Path, env: Mapping[str, str]) -> list[str]:
    executable = _executable_path(raw, env)
    path = Path(executable)
    if path.suffix.lower() == ".py" or (path.is_file() and not os.access(path, os.X_OK)):
        return [sys.executable, executable]
    return [executable]


def _prepend_environment(env: dict[str, str], name: str, paths: Sequence[Path]) -> None:
    values = [str(path) for path in paths if path.is_dir()]
    existing = env.get(name)
    if existing:
        values.append(existing)
    if values:
        env[name] = os.pathsep.join(values)


def _local_yosys_environment(raw_yosys: str | Path) -> tuple[dict[str, str], dict[str, str]]:
    """Return an environment rooted at the selected ``.../usr/bin/yosys``."""
    env = dict(os.environ)
    yosys = _repo_path(raw_yosys) if len(Path(raw_yosys).parts) > 1 else Path(raw_yosys)
    if yosys.parent.name == "bin":
        prefix = yosys.parent.parent
        _prepend_environment(env, "PATH", [prefix / "bin"])
        library_directories = [prefix / "lib", prefix / "lib64"]
        library_directories.extend(sorted((prefix / "lib").glob("*-linux-gnu")))
        _prepend_environment(env, "LD_LIBRARY_PATH", library_directories)
        env["YOSYS_DATDIR"] = str(prefix / "share" / "yosys")

        tcl_candidates = []
        for pattern in ("share/tcltk/tcl*", "lib/tcl*", "lib/*/tcl*"):
            tcl_candidates.extend(prefix.glob(pattern))
        tcl_roots = sorted(
            {candidate.resolve() for candidate in tcl_candidates if (candidate / "init.tcl").is_file()},
            reverse=True,
        )
        env["TCL_LIBRARY"] = str(
            tcl_roots[0] if tcl_roots else prefix / "share" / "tcltk" / "tcl8.6"
        )

    recorded = {
        name: env.get(name, "")
        for name in ("PATH", "LD_LIBRARY_PATH", "YOSYS_DATDIR", "TCL_LIBRARY")
    }
    return env, recorded


def _tool_identity(
    name: str,
    raw: str | Path,
    env: Mapping[str, str],
    *,
    yosys: bool = False,
) -> dict[str, Any]:
    prefix = _command_prefix(raw, env)
    tool_path = Path(prefix[-1])
    digest = _sha256(tool_path) if tool_path.is_file() else None
    version = None
    probe: dict[str, Any] | None = None
    if yosys:
        command = prefix + ["-V"]
        started = time.perf_counter()
        try:
            completed = subprocess.run(
                command,
                cwd=REPOSITORY_ROOT,
                env=dict(env),
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                timeout=15.0,
                check=False,
            )
            output = completed.stdout.strip()
            version = next((line.strip() for line in output.splitlines() if line.strip()), None)
            probe = {
                "command": command,
                "exit_code": completed.returncode,
                "duration_seconds": round(time.perf_counter() - started, 6),
                "output": output,
            }
        except (OSError, subprocess.TimeoutExpired) as exc:
            probe = {
                "command": command,
                "exit_code": None,
                "duration_seconds": round(time.perf_counter() - started, 6),
                "output": str(exc),
            }
    if version is None:
        if digest:
            version = f"sha256:{digest}"
            if prefix[0] == sys.executable:
                version += f"; Python {sys.version.split()[0]}"
        else:
            version = "unavailable"
    return {
        "name": name,
        "path": str(tool_path),
        "command_prefix": prefix,
        "version": version,
        "sha256": digest,
        "version_probe": probe,
    }


def _write_process_logs(stdout_path: Path, stderr_path: Path, stdout: str, stderr: str) -> None:
    _atomic_write_text(stdout_path, stdout)
    _atomic_write_text(stderr_path, stderr)


def _run_process(
    name: str,
    command: Sequence[str],
    *,
    cwd: Path,
    env: Mapping[str, str],
    timeout_seconds: float,
    stdout_path: Path,
    stderr_path: Path,
    version: str,
) -> dict[str, Any]:
    started_at = _utc_now()
    started = time.perf_counter()
    exit_code: int | None = None
    timed_out = False
    launch_error: str | None = None
    stdout = ""
    stderr = ""
    try:
        completed = subprocess.run(
            list(command),
            cwd=cwd,
            env=dict(env),
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=timeout_seconds,
            check=False,
        )
        exit_code = completed.returncode
        stdout = completed.stdout
        stderr = completed.stderr
    except subprocess.TimeoutExpired as exc:
        timed_out = True
        stdout = exc.stdout or ""
        stderr = exc.stderr or ""
        if isinstance(stdout, bytes):
            stdout = stdout.decode(errors="replace")
        if isinstance(stderr, bytes):
            stderr = stderr.decode(errors="replace")
        stderr += f"\nrunner: timeout after {timeout_seconds:g} seconds\n"
    except OSError as exc:
        launch_error = str(exc)
        stderr = f"runner: cannot launch command: {exc}\n"
    duration = round(time.perf_counter() - started, 6)
    _write_process_logs(stdout_path, stderr_path, stdout, stderr)
    if timed_out:
        status = "timeout"
    elif launch_error is not None:
        status = "failed"
    elif exit_code == 0:
        status = "success"
    else:
        status = "failed"
    return {
        "name": name,
        "command": list(command),
        "command_display": shlex.join(command),
        "version": version,
        "started_at": started_at,
        "duration_seconds": duration,
        "exit_code": exit_code,
        "status": status,
        "timed_out": timed_out,
        "launch_error": launch_error,
        "stdout": stdout_path.name,
        "stderr": stderr_path.name,
    }


def _skipped_stage(name: str, expected: Any, reason: str, version: str = "") -> dict[str, Any]:
    return {
        "name": name,
        "command": [],
        "command_display": "",
        "version": version,
        "started_at": None,
        "duration_seconds": 0.0,
        "exit_code": None,
        "status": "not_run",
        "timed_out": False,
        "launch_error": None,
        "stdout": None,
        "stderr": None,
        "expected": expected,
        "reason": reason,
    }


def _load_json_object(path: Path, description: str) -> Mapping[str, Any]:
    try:
        payload = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise RunnerError(f"cannot read {description} {path}: {exc}") from exc
    if not isinstance(payload, Mapping):
        raise RunnerError(f"{description} {path} must contain a JSON object")
    return payload


def _stage_outcome(pipeline: Mapping[str, Any], stage: str) -> str:
    value = pipeline.get(stage)
    if not isinstance(value, Mapping) or not isinstance(value.get("outcome"), str):
        raise RunnerError(f"expected_pipeline.{stage}.outcome is missing")
    return str(value["outcome"])


def _yosys_quote(value: str | Path) -> str:
    text = str(value).replace("\\", "\\\\").replace('"', '\\"')
    return f'"{text}"'


def _yosys_script(rtl_files: Sequence[Path], module: str, output: Path) -> str:
    if not rtl_files:
        raise RunnerError("manifest declares no RTL files")
    lines = [
        "read_verilog -sv " + " ".join(_yosys_quote(path) for path in rtl_files),
        # The manifest schema restricts this to a Yosys-safe RTL identifier.
        # Quoting it would make the quote characters part of the identifier in
        # Yosys command scripts (unlike filenames, which do need quotes).
        f"hierarchy -check -top {module}",
        "proc",
        "opt",
        "dffunmap",
        "techmap",
        "abc -g AND,OR",
        "clean",
        f"write_json {_yosys_quote(output)}",
    ]
    return ";\n".join(lines) + ";\n"


def _state_arguments(path: Path) -> list[str]:
    payload = _load_json_object(path, "state manifest")
    if payload.get("schema") != "ifcn.state_manifest.v0":
        raise RunnerError(
            f"state manifest has schema {payload.get('schema')!r}, expected "
            "'ifcn.state_manifest.v0'"
        )
    pnr = payload.get("ifcn_sequential_pnr")
    if not isinstance(pnr, Mapping):
        raise RunnerError("state manifest lacks ifcn_sequential_pnr")
    raw_arguments = pnr.get("state_arguments")
    if not isinstance(raw_arguments, list) or not raw_arguments:
        raise RunnerError("state manifest has no state_arguments")
    arguments: list[str] = []
    for index, raw in enumerate(raw_arguments):
        if not isinstance(raw, str) or raw.count(":") != 1:
            raise RunnerError(f"state_arguments[{index}] is not a D-event:Q-event pair")
        data_event, q_event = raw.split(":")
        if not data_event or not q_event:
            raise RunnerError(f"state_arguments[{index}] has an empty event")
        arguments.append(raw)
    if len(arguments) != len(set(arguments)):
        raise RunnerError("state manifest contains duplicate state_arguments")

    boundaries = payload.get("state_boundaries")
    if not isinstance(boundaries, list) or len(boundaries) != len(arguments):
        raise RunnerError("state_boundaries and state_arguments have different sizes")
    boundary_arguments = []
    for index, boundary in enumerate(boundaries):
        if not isinstance(boundary, Mapping):
            raise RunnerError(f"state_boundaries[{index}] is not an object")
        argument = boundary.get("state_argument")
        if not isinstance(argument, str):
            data_event = boundary.get("data_event")
            q_event = boundary.get("q_event")
            if not isinstance(data_event, str) or not isinstance(q_event, str):
                raise RunnerError(f"state_boundaries[{index}] lacks events")
            argument = f"{data_event}:{q_event}"
        boundary_arguments.append(argument)
    if boundary_arguments != arguments:
        raise RunnerError("state_boundaries do not match state_arguments in order")
    return arguments


def _is_latch_rejection(output: str) -> bool:
    lowered = output.lower()
    return "latch" in lowered and (
        "unsupported" in lowered
        or "outside ifcn.seqir.v0" in lowered
        or "reject" in lowered
    )


def _is_limit(output: str) -> bool:
    return re.search(r"\bLIMIT\b", output, flags=re.IGNORECASE) is not None


def _artifact_ok(stage: dict[str, Any], paths: Sequence[Path]) -> bool:
    missing = [str(path) for path in paths if not path.is_file() or path.stat().st_size == 0]
    if not missing:
        return True
    stage["status"] = "failed"
    stage["artifact_error"] = "missing or empty output: " + ", ".join(missing)
    return False


def _case_artifacts(case_dir: Path, output_root: Path) -> list[str]:
    return [
        path.relative_to(output_root).as_posix()
        for path in sorted(case_dir.iterdir())
        if path.is_file()
    ]


def _finish_case(case: dict[str, Any], case_dir: Path, output_root: Path) -> dict[str, Any]:
    case["duration_seconds"] = round(
        sum(float(stage.get("duration_seconds", 0.0)) for stage in case["stages"]),
        6,
    )
    result_path = case_dir / "result.json"
    case["artifacts"] = _case_artifacts(case_dir, output_root) + [
        result_path.relative_to(output_root).as_posix()
    ]
    _write_json(result_path, case)
    return case


def _run_case(
    manifest_path: Path,
    benchmark_root: Path,
    output_root: Path,
    args: argparse.Namespace,
    env: Mapping[str, str],
    tools: Mapping[str, Mapping[str, Any]],
) -> dict[str, Any]:
    relative_parent = manifest_path.parent.relative_to(benchmark_root)
    case_dir = output_root / relative_parent
    case_dir.mkdir(parents=True, exist_ok=True)
    case: dict[str, Any] = {
        "schema": CASE_SCHEMA,
        "benchmark_id": relative_parent.as_posix(),
        "manifest": _display_path(manifest_path),
        "artifact_dir": relative_parent.as_posix(),
        "started_at": _utc_now(),
        "status": "fail",
        "expectation_met": False,
        "failed_stage": None,
        "state_arguments": [],
        "metrics": {},
        "stages": [],
    }

    validator_command = list(tools["validator"]["command_prefix"]) + [str(manifest_path)]
    validator_stage = _run_process(
        "provenance",
        validator_command,
        cwd=REPOSITORY_ROOT,
        env=env,
        timeout_seconds=args.timeout_seconds,
        stdout_path=case_dir / "provenance.stdout.log",
        stderr_path=case_dir / "provenance.stderr.log",
        version=str(tools["validator"]["version"]),
    )
    validator_stage["expected"] = "pass"
    case["stages"].append(validator_stage)
    if validator_stage["status"] != "success":
        case["status"] = "timeout" if validator_stage["status"] == "timeout" else "fail"
        case["failed_stage"] = "provenance"
        return _finish_case(case, case_dir, output_root)

    try:
        manifest = _load_json_object(manifest_path, "benchmark manifest")
        benchmark_id = manifest.get("benchmark_id")
        if not isinstance(benchmark_id, str):
            raise RunnerError("manifest benchmark_id is missing")
        case["benchmark_id"] = benchmark_id
        module = manifest.get("module")
        if not isinstance(module, str) or not module:
            raise RunnerError("manifest module is missing")
        classification = manifest.get("classification")
        reconstruction = manifest.get("reconstruction")
        pipeline = manifest.get("expected_pipeline")
        files = manifest.get("files")
        if not isinstance(classification, Mapping) or not isinstance(pipeline, Mapping):
            raise RunnerError("manifest classification or expected_pipeline is missing")
        if not isinstance(reconstruction, Mapping) or not isinstance(files, list):
            raise RunnerError("manifest reconstruction or files is missing")
        expected = {
            name: _stage_outcome(pipeline, name)
            for name in ("yosys", "seqir", "legacy_cut", "sequential_pnr", "mapping")
        }
        case["classification"] = dict(classification)
        case["temporal_relation"] = reconstruction.get("temporal_relation", "same_model")
        case["expected_pipeline"] = expected
        rtl_files = [
            (manifest_path.parent / str(entry["path"])).resolve()
            for entry in files
            if isinstance(entry, Mapping) and entry.get("role") == "rtl"
        ]
        if not rtl_files:
            raise RunnerError("manifest declares no file with role 'rtl'")
    except RunnerError as exc:
        case["failed_stage"] = "manifest_load"
        case["error"] = str(exc)
        return _finish_case(case, case_dir, output_root)

    yosys_json = case_dir / "yosys.json"
    yosys_log = case_dir / "yosys.log"
    synthesis_script = case_dir / "synthesis.ys"
    _atomic_write_text(synthesis_script, _yosys_script(rtl_files, module, yosys_json))
    yosys_expected = expected["yosys"]
    if yosys_expected == "not_run":
        stage = _skipped_stage("yosys", yosys_expected, "expected_pipeline requests not_run")
        case["stages"].append(stage)
        case["status"] = "pass"
        case["expectation_met"] = True
        return _finish_case(case, case_dir, output_root)

    yosys_command = list(tools["yosys"]["command_prefix"]) + [
        "-l",
        str(yosys_log),
        "-s",
        str(synthesis_script),
    ]
    yosys_stage = _run_process(
        "yosys",
        yosys_command,
        cwd=REPOSITORY_ROOT,
        env=env,
        timeout_seconds=args.timeout_seconds,
        stdout_path=case_dir / "yosys.stdout.log",
        stderr_path=case_dir / "yosys.stderr.log",
        version=str(tools["yosys"]["version"]),
    )
    yosys_stage["expected"] = yosys_expected
    case["stages"].append(yosys_stage)
    if yosys_stage["status"] == "timeout":
        case["status"] = "timeout"
        case["failed_stage"] = "yosys"
        return _finish_case(case, case_dir, output_root)
    if yosys_stage["status"] != "success":
        case["status"] = "reject" if yosys_expected == "reject" else "fail"
        case["expectation_met"] = yosys_expected == "reject"
        case["failed_stage"] = None if case["expectation_met"] else "yosys"
        return _finish_case(case, case_dir, output_root)
    if not _artifact_ok(yosys_stage, [yosys_json]):
        case["failed_stage"] = "yosys"
        return _finish_case(case, case_dir, output_root)
    if yosys_expected == "reject":
        yosys_stage["expectation_error"] = "stage passed but reject was expected"
        case["failed_stage"] = "yosys"
        return _finish_case(case, case_dir, output_root)

    converter_expectations = {"seqir": expected["seqir"], "legacy_cut": expected["legacy_cut"]}
    if all(outcome == "not_run" for outcome in converter_expectations.values()):
        case["stages"].append(
            _skipped_stage(
                "converter", converter_expectations, "expected_pipeline requests not_run",
                str(tools["converter"]["version"]),
            )
        )
        case["status"] = "pass"
        case["expectation_met"] = True
        return _finish_case(case, case_dir, output_root)

    seqir = case_dir / "seqir.json"
    cut_verilog = case_dir / "cut.v"
    state_json = case_dir / "state.json"
    converter_command = list(tools["converter"]["command_prefix"]) + [
        str(yosys_json),
        "-o",
        str(seqir),
        "--top",
        module,
        "--legacy-cut-verilog",
        str(cut_verilog),
        "--state-manifest",
        str(state_json),
        "--compact-legacy-names",
    ]
    converter_stage = _run_process(
        "converter",
        converter_command,
        cwd=REPOSITORY_ROOT,
        env=env,
        timeout_seconds=args.timeout_seconds,
        stdout_path=case_dir / "converter.stdout.log",
        stderr_path=case_dir / "converter.stderr.log",
        version=str(tools["converter"]["version"]),
    )
    converter_stage["expected"] = converter_expectations
    combined_converter_log = (
        (case_dir / "converter.stdout.log").read_text(encoding="utf-8", errors="replace")
        + "\n"
        + (case_dir / "converter.stderr.log").read_text(encoding="utf-8", errors="replace")
    )
    expected_reject = "reject" in converter_expectations.values()
    latch_reject = _is_latch_rejection(combined_converter_log)
    if converter_stage["status"] == "timeout":
        case["status"] = "timeout"
        case["failed_stage"] = "converter"
        case["stages"].append(converter_stage)
        return _finish_case(case, case_dir, output_root)
    if converter_stage["status"] != "success":
        converter_stage["status"] = "reject" if latch_reject else "failed"
        converter_stage["latch_rejection"] = latch_reject
        case["stages"].append(converter_stage)
        case["status"] = "reject" if latch_reject else "fail"
        case["expectation_met"] = bool(
            expected_reject
            and latch_reject
            and classification.get("state_element") in {"d_latch", "sr_latch"}
        )
        case["failed_stage"] = None if case["expectation_met"] else "converter"
        return _finish_case(case, case_dir, output_root)
    case["stages"].append(converter_stage)
    if expected_reject:
        converter_stage["expectation_error"] = "converter passed but reject was expected"
        case["failed_stage"] = "converter"
        return _finish_case(case, case_dir, output_root)
    if not _artifact_ok(converter_stage, [seqir, cut_verilog, state_json]):
        case["failed_stage"] = "converter"
        return _finish_case(case, case_dir, output_root)

    try:
        state_arguments = _state_arguments(state_json)
    except RunnerError as exc:
        converter_stage["status"] = "failed"
        converter_stage["artifact_error"] = str(exc)
        case["failed_stage"] = "converter"
        return _finish_case(case, case_dir, output_root)
    case["state_arguments"] = state_arguments

    pnr_expectations = {
        "sequential_pnr": expected["sequential_pnr"],
        "mapping": expected["mapping"],
    }
    if all(outcome == "not_run" for outcome in pnr_expectations.values()):
        case["stages"].append(
            _skipped_stage(
                "pnr", pnr_expectations, "expected_pipeline requests not_run",
                str(tools["pnr"]["version"]),
            )
        )
        case["status"] = "pass"
        case["expectation_met"] = True
        return _finish_case(case, case_dir, output_root)

    layout = case_dir / "layout.ifcn"
    latex_layout = case_dir / "layout.tex"
    pnr_command = list(tools["pnr"]["command_prefix"]) + [str(cut_verilog), str(layout)]
    for argument in state_arguments:
        pnr_command.extend(["--state", argument])
    pnr_command.extend(
        [
            "--ii",
            args.ii,
            "--max-same-phase",
            str(args.max_same_phase),
            "--max-dfs-nodes",
            str(args.max_dfs_nodes),
            "--spacing",
            str(args.spacing),
            "--tex",
            str(latex_layout),
        ]
    )
    pnr_stage = _run_process(
        "pnr",
        pnr_command,
        cwd=REPOSITORY_ROOT,
        env=env,
        timeout_seconds=args.timeout_seconds,
        stdout_path=case_dir / "pnr.stdout.log",
        stderr_path=case_dir / "pnr.stderr.log",
        version=str(tools["pnr"]["version"]),
    )
    pnr_stage["expected"] = pnr_expectations
    combined_pnr_log = (
        (case_dir / "pnr.stdout.log").read_text(encoding="utf-8", errors="replace")
        + "\n"
        + (case_dir / "pnr.stderr.log").read_text(encoding="utf-8", errors="replace")
    )
    if pnr_stage["status"] == "timeout":
        case["status"] = "timeout"
        case["failed_stage"] = "pnr"
    elif pnr_stage["status"] != "success" and _is_limit(combined_pnr_log):
        pnr_stage["status"] = "limit"
        case["status"] = "limit"
        case["failed_stage"] = "pnr"
    elif pnr_stage["status"] != "success":
        case["status"] = "reject" if "reject" in pnr_expectations.values() else "fail"
        case["expectation_met"] = "reject" in pnr_expectations.values()
        case["failed_stage"] = None if case["expectation_met"] else "pnr"
    else:
        report_path = Path(str(layout) + ".json")
        if _artifact_ok(pnr_stage, [layout, report_path, latex_layout]):
            try:
                report = _load_json_object(report_path, "P&R report")
                case["metrics"] = {
                    name: report[name]
                    for name in (
                        "initiation_interval",
                        "nodes",
                        "routes",
                        "mapped_qca_cells",
                        "state_boundaries",
                        "feedback_routes",
                        "q_pseudo_nodes_removed",
                        "directed_cycle_present",
                        "physical_state_signoff",
                    )
                    if name in report
                }
            except RunnerError as exc:
                pnr_stage["status"] = "failed"
                pnr_stage["artifact_error"] = str(exc)
                case["failed_stage"] = "pnr"
            else:
                if "reject" in pnr_expectations.values():
                    pnr_stage["expectation_error"] = "P&R passed but reject was expected"
                    case["failed_stage"] = "pnr"
                else:
                    case["status"] = "pass"
                    case["expectation_met"] = True
        else:
            case["failed_stage"] = "pnr"
    case["stages"].append(pnr_stage)
    return _finish_case(case, case_dir, output_root)


def _stage_by_name(case: Mapping[str, Any], name: str) -> Mapping[str, Any]:
    stages = case.get("stages", [])
    if isinstance(stages, list):
        for stage in stages:
            if isinstance(stage, Mapping) and stage.get("name") == name:
                return stage
    return {}


def _write_csv(path: Path, cases: Sequence[Mapping[str, Any]]) -> None:
    base_fields = [
        "benchmark_id",
        "manifest",
        "state_element",
        "temporal_relation",
        "status",
        "expectation_met",
        "failed_stage",
        "state_boundaries",
        "initiation_interval",
        "nodes",
        "routes",
        "feedback_routes",
        "directed_cycle_present",
        "physical_state_signoff",
        "mapped_qca_cells",
        "duration_seconds",
        "artifact_dir",
        "latex",
    ]
    stage_fields = []
    for stage in ("provenance", "yosys", "converter", "pnr"):
        stage_fields.extend(
            f"{stage}_{field}"
            for field in ("status", "exit_code", "duration_seconds", "version", "command")
        )
    fields = base_fields + stage_fields
    rows = []
    for case in cases:
        classification = case.get("classification", {})
        metrics = case.get("metrics", {})
        row: dict[str, Any] = {
            "benchmark_id": case.get("benchmark_id", ""),
            "manifest": case.get("manifest", ""),
            "state_element": classification.get("state_element", "") if isinstance(classification, Mapping) else "",
            "temporal_relation": case.get("temporal_relation", ""),
            "status": case.get("status", ""),
            "expectation_met": case.get("expectation_met", False),
            "failed_stage": case.get("failed_stage") or "",
            "state_boundaries": len(case.get("state_arguments", [])),
            "duration_seconds": case.get("duration_seconds", ""),
            "artifact_dir": case.get("artifact_dir", ""),
            "latex": (
                f"{case.get('artifact_dir', '')}/layout.tex"
                if case.get("status") == "pass"
                else ""
            ),
        }
        for metric in (
            "initiation_interval",
            "nodes",
            "routes",
            "feedback_routes",
            "directed_cycle_present",
            "physical_state_signoff",
            "mapped_qca_cells",
        ):
            row[metric] = metrics.get(metric, "") if isinstance(metrics, Mapping) else ""
        for stage_name in ("provenance", "yosys", "converter", "pnr"):
            stage = _stage_by_name(case, stage_name)
            for field in ("status", "exit_code", "duration_seconds", "version"):
                value = stage.get(field, "")
                row[f"{stage_name}_{field}"] = "" if value is None else value
            row[f"{stage_name}_command"] = stage.get("command_display", "")
        rows.append(row)

    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(
        prefix=f".{path.name}.", suffix=".tmp", dir=str(path.parent), text=True
    )
    try:
        with os.fdopen(descriptor, "w", encoding="utf-8", newline="") as handle:
            writer = csv.DictWriter(handle, fieldnames=fields)
            writer.writeheader()
            writer.writerows(rows)
        os.replace(temporary, path)
    except BaseException:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass
        raise


def _parse_ii(value: str) -> str:
    try:
        values = [int(token) for token in value.split(",")]
    except ValueError as exc:
        raise argparse.ArgumentTypeError("II must be a comma-separated integer list") from exc
    if not values or any(item <= 0 for item in values):
        raise argparse.ArgumentTypeError("II candidates must be positive")
    if values != sorted(set(values)):
        raise argparse.ArgumentTypeError("II candidates must be unique and increasing")
    return ",".join(str(item) for item in values)


def _positive_int(value: str) -> int:
    parsed = int(value)
    if parsed <= 0:
        raise argparse.ArgumentTypeError("value must be positive")
    return parsed


def _arguments(argv: Sequence[str] | None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run validated sequential paper benchmarks through Yosys, SeqIR, and iFCN P&R."
    )
    parser.add_argument("--benchmarks-root", default=str(DEFAULT_BENCHMARK_ROOT))
    parser.add_argument("--output-dir", default=str(DEFAULT_OUTPUT_ROOT))
    parser.add_argument("--validator", default=str(DEFAULT_VALIDATOR))
    parser.add_argument("--yosys", default=str(DEFAULT_YOSYS))
    parser.add_argument("--converter", default=str(DEFAULT_CONVERTER))
    parser.add_argument("--pnr", default=str(DEFAULT_PNR))
    parser.add_argument("--cyclic-pnr", default=str(DEFAULT_CYCLIC_PNR))
    parser.add_argument(
        "--physical-feedback",
        action="store_true",
        help="replace register-cut Q fanout by routed iteration-distance feedback",
    )
    parser.add_argument("--ii", type=_parse_ii, default="4,8,12,16,20,24")
    parser.add_argument("--max-dfs-nodes", type=_positive_int, default=5_000_000)
    parser.add_argument("--max-same-phase", type=int, default=4)
    parser.add_argument("--spacing", type=int, default=2)
    parser.add_argument("--timeout-seconds", type=float, default=600.0)
    args = parser.parse_args(argv)
    if not 1 <= args.max_same_phase <= 4:
        parser.error("--max-same-phase must be between 1 and 4 tiles")
    if args.spacing < 2:
        parser.error("--spacing must be at least 2")
    if args.timeout_seconds <= 0:
        parser.error("--timeout-seconds must be positive")
    return args


def main(argv: Sequence[str] | None = None) -> int:
    args = _arguments(argv)
    benchmark_root = _repo_path(args.benchmarks_root)
    output_root = _repo_path(args.output_dir)
    output_root.mkdir(parents=True, exist_ok=True)
    manifests = sorted(benchmark_root.glob("**/benchmark.json")) if benchmark_root.is_dir() else []

    env, recorded_environment = _local_yosys_environment(args.yosys)
    selected_pnr = args.cyclic_pnr if args.physical_feedback else args.pnr
    tools = {
        "validator": _tool_identity("validator", args.validator, env),
        "yosys": _tool_identity("yosys", args.yosys, env, yosys=True),
        "converter": _tool_identity("converter", args.converter, env),
        "pnr": _tool_identity("pnr", selected_pnr, env),
    }
    started_at = _utc_now()
    started = time.perf_counter()
    cases = [
        _run_case(manifest, benchmark_root, output_root, args, env, tools)
        for manifest in manifests
    ]
    counts: dict[str, int] = {}
    for case in cases:
        status = str(case["status"])
        counts[status] = counts.get(status, 0) + 1
    counts["total"] = len(cases)
    counts["expectations_met"] = sum(bool(case["expectation_met"]) for case in cases)
    counts["expectations_failed"] = len(cases) - counts["expectations_met"]
    summary = {
        "schema": SUMMARY_SCHEMA,
        "started_at": started_at,
        "finished_at": _utc_now(),
        "duration_seconds": round(time.perf_counter() - started, 6),
        "repository_root": str(REPOSITORY_ROOT),
        "benchmark_root": str(benchmark_root),
        "output_root": str(output_root),
        "configuration": {
            "ii": [int(value) for value in args.ii.split(",")],
            "max_dfs_nodes": args.max_dfs_nodes,
            "max_same_phase": args.max_same_phase,
            "spacing": args.spacing,
            "timeout_seconds": args.timeout_seconds,
            "physical_feedback": args.physical_feedback,
        },
        "yosys_environment": recorded_environment,
        "tools": tools,
        "manifests": [_display_path(path) for path in manifests],
        "counts": counts,
        "cases": cases,
    }
    _write_json(output_root / "summary.json", summary)
    _write_csv(output_root / "summary.csv", cases)

    if not manifests:
        print(f"no benchmark.json files found below {benchmark_root}", file=sys.stderr)
        print(f"summary={output_root / 'summary.json'}")
        return 2
    print(
        f"sequential_paper_benchmarks total={len(cases)} "
        f"met={counts['expectations_met']} failed={counts['expectations_failed']} "
        f"summary={output_root / 'summary.json'}"
    )
    return 0 if counts["expectations_failed"] == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
