#!/usr/bin/env python3
"""Run the native sequential RTL suite through the iFCN experiment flows.

The script deliberately keeps three results separate:

* ``cut`` is the legacy register-cut DAG flow and is not a physical
  sequential-layout success;
* ``cyclic_ii4`` restores routed iteration-distance-one feedback and fixes the
  initiation interval to four epochs;
* ``cyclic_adaptive`` uses the same cyclic geometry flow and enumerates the
  caller-provided II candidates.

Each synthesized SeqIR is also exhaustively checked against an independent
one-step reference function before any layout result is accepted.  The suite
uses only Python's standard library and records every command and raw log.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import itertools
import json
import math
import os
import platform
import re
import shutil
import statistics
import subprocess
import sys
import tempfile
import time
from datetime import datetime, timezone
from pathlib import Path
from typing import Any, Callable, Mapping, Sequence


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_RTL_ROOT = ROOT / "tests" / "benchmarks_f" / "SEQUENTIAL" / "rtl_v"
DEFAULT_OUTPUT = ROOT / "build" / "artifacts" / "sequential_rtl_experiments_v1"
DEFAULT_YOSYS = ROOT / "build" / "tools" / "yosys-local" / "usr" / "bin" / "yosys"
DEFAULT_CONVERTER = ROOT / "scripts" / "yosys_json_to_seqir.py"
DEFAULT_CUT_PNR = ROOT / "build-release" / "ifcn_sequential_pnr"
DEFAULT_CYCLIC_PNR = ROOT / "build-release" / "ifcn_paper_cyclic_pnr"
DEFAULT_Z3_SOLVER = ROOT / "scripts" / "solve_global_clock_z3.py"
DEFAULT_Z3_ROOT = ROOT / "build" / "tools" / "z3-local"


class ExperimentError(RuntimeError):
    pass


def clear_stage_artifacts(paths: Sequence[Path]) -> None:
    """Remove only the explicitly named outputs before reusing a stage dir."""
    for path in paths:
        try:
            path.unlink()
        except FileNotFoundError:
            pass


def parse_geometry_candidate_count(output: str) -> int:
    match = re.search(r"\bgeometry_candidates=(\d+)\b", output)
    if match is None:
        raise ExperimentError(
            "successful cyclic geometry stage omitted geometry_candidates"
        )
    count = int(match.group(1))
    if count <= 0:
        raise ExperimentError(
            f"cyclic geometry stage reported invalid geometry_candidates={count}"
        )
    return count


def reduce_geometry_fallback_status(statuses: Sequence[str]) -> str:
    """Summarize ranked solves without converting an unresolved case to UNSAT."""
    if "success" in statuses:
        return "success"
    if statuses and all(status == "unsat" for status in statuses):
        return "unsat"
    if "unknown" in statuses:
        return "unknown"
    if "limit" in statuses:
        return "limit"
    return statuses[-1] if statuses else "failed"


def artifact_reference(
    path: Path, stage_status: str, root: Path = ROOT
) -> str | None:
    """Expose a stage artifact only after that stage completed successfully."""
    if stage_status != "success" or not path.is_file():
        return None
    return path.relative_to(root).as_posix()


def load_json_artifact(path: Path, stage_status: str) -> Any | None:
    """Read structured output only when its producing stage succeeded."""
    if stage_status != "success" or not path.is_file():
        return None
    return json.loads(path.read_text(encoding="utf-8"))


def utc_now() -> str:
    return datetime.now(timezone.utc).isoformat(timespec="milliseconds")


def atomic_text(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(
        prefix=f".{path.name}.", suffix=".tmp", dir=path.parent, text=True
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


def write_json(path: Path, value: Any) -> None:
    atomic_text(path, json.dumps(value, indent=2, sort_keys=True) + "\n")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def local_yosys_environment(executable: Path) -> dict[str, str]:
    env = dict(os.environ)
    if executable.parent.name != "bin":
        return env
    prefix = executable.parent.parent
    path_entries = [str(prefix / "bin")]
    if env.get("PATH"):
        path_entries.append(env["PATH"])
    env["PATH"] = os.pathsep.join(path_entries)
    libraries = [prefix / "lib", prefix / "lib64"]
    libraries.extend(sorted((prefix / "lib").glob("*-linux-gnu")))
    ld_entries = [str(path) for path in libraries if path.is_dir()]
    if env.get("LD_LIBRARY_PATH"):
        ld_entries.append(env["LD_LIBRARY_PATH"])
    env["LD_LIBRARY_PATH"] = os.pathsep.join(ld_entries)
    env["YOSYS_DATDIR"] = str(prefix / "share" / "yosys")
    tcl = sorted(prefix.glob("share/tcltk/tcl*/init.tcl"), reverse=True)
    if tcl:
        env["TCL_LIBRARY"] = str(tcl[0].parent)
    return env


def executable_prefix(path: Path) -> list[str]:
    if path.suffix == ".py" or not os.access(path, os.X_OK):
        return [sys.executable, str(path)]
    return [str(path)]


def run_stage(
    name: str,
    command: Sequence[str],
    directory: Path,
    env: Mapping[str, str],
    timeout: float,
) -> dict[str, Any]:
    stdout_path = directory / f"{name}.stdout.log"
    stderr_path = directory / f"{name}.stderr.log"
    started = time.perf_counter()
    stdout = ""
    stderr = ""
    exit_code: int | None = None
    timed_out = False
    launch_error: str | None = None
    try:
        completed = subprocess.run(
            list(command), cwd=ROOT, env=dict(env), text=True,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            timeout=timeout, check=False,
        )
        stdout, stderr, exit_code = completed.stdout, completed.stderr, completed.returncode
    except subprocess.TimeoutExpired as exc:
        timed_out = True
        stdout = (exc.stdout or b"")
        stderr = (exc.stderr or b"")
        if isinstance(stdout, bytes):
            stdout = stdout.decode(errors="replace")
        if isinstance(stderr, bytes):
            stderr = stderr.decode(errors="replace")
    except OSError as exc:
        launch_error = str(exc)
        stderr = launch_error + "\n"
    atomic_text(stdout_path, stdout)
    atomic_text(stderr_path, stderr)
    combined = stdout + "\n" + stderr
    status = "success" if exit_code == 0 else "failed"
    if timed_out:
        status = "timeout"
    # Status words can legitimately occur in a successful output path (for
    # example an artifact directory named ``limit``).  Only reinterpret a
    # failed process, and match the tools' explicit status records.
    elif exit_code != 0 and re.search(
        r"(?:global(?: cyclic)? phase/epoch solve|global_clock_z3=)\s*LIMIT\b",
        combined, re.IGNORECASE,
    ):
        status = "limit"
    elif exit_code != 0 and "placement/routing failed" in combined:
        status = "routing_failed"
    elif exit_code != 0 and "Q pseudo input has no fanout to replace" in combined:
        status = "unsupported_observation_only_state"
    elif exit_code != 0 and "INVALID_INPUT" in combined:
        status = "invalid_input"
    elif exit_code != 0 and re.search(
        r"phase/epoch solve UNSAT", combined, re.IGNORECASE
    ):
        status = "unsat"
    elif exit_code != 0 and re.search(r"global_clock_z3=UNSAT", combined):
        status = "unsat"
    elif exit_code != 0 and re.search(r"global_clock_z3=UNKNOWN", combined):
        status = "unknown"
    return {
        "name": name,
        "command": list(command),
        "status": status,
        "exit_code": exit_code,
        "timed_out": timed_out,
        "launch_error": launch_error,
        "duration_seconds": round(time.perf_counter() - started, 6),
        "stdout": stdout_path.relative_to(ROOT).as_posix(),
        "stderr": stderr_path.relative_to(ROOT).as_posix(),
    }


def module_name(path: Path) -> str:
    match = re.search(r"\bmodule\s+([A-Za-z_][A-Za-z0-9_$]*)", path.read_text())
    if not match:
        raise ExperimentError(f"cannot find module name in {path}")
    return match.group(1)


def yosys_script(rtl: Path, module: str, output: Path) -> str:
    quote = lambda path: '"' + str(path).replace("\\", "\\\\").replace('"', '\\"') + '"'
    return ";\n".join([
        f"read_verilog -sv {quote(rtl)}",
        f"hierarchy -check -top {module}",
        "proc", "opt", "dffunmap", "techmap", "abc -g AND,OR", "clean",
        f"write_json {quote(output)}",
    ]) + ";\n"


def state_arguments(path: Path) -> list[str]:
    value = json.loads(path.read_text())
    return list(value["ifcn_sequential_pnr"]["state_arguments"])


def signal_value(values: Mapping[str, int], signal: str) -> int:
    if signal == "const.0":
        return 0
    if signal == "const.1":
        return 1
    return int(values[signal])


def evaluate_seqir(seqir: Mapping[str, Any], inputs: Mapping[str, int]) -> dict[str, int]:
    values = dict(inputs)
    pending = list(seqir["combinational_nodes"])
    while pending:
        progress = False
        rest = []
        for node in pending:
            operands = node["inputs"]
            if any(signal not in values and not signal.startswith("const.") for signal in operands.values()):
                rest.append(node)
                continue
            op = node["op"]
            resolved = {name: signal_value(values, signal) for name, signal in operands.items()}
            if op == "not":
                result = 1 - resolved["A"]
            elif op == "buf":
                result = resolved["A"]
            elif op == "and":
                result = resolved["A"] & resolved["B"]
            elif op == "or":
                result = resolved["A"] | resolved["B"]
            elif op == "xor":
                result = resolved["A"] ^ resolved["B"]
            elif op == "mux":
                result = resolved["B"] if resolved["S"] else resolved["A"]
            else:
                raise ExperimentError(f"unsupported SeqIR op in checker: {op}")
            values[node["output"]] = result
            progress = True
        if not progress:
            missing = sorted({s for n in rest for s in n["inputs"].values() if s not in values})
            raise ExperimentError(f"SeqIR evaluator stalled; missing {missing}")
        pending = rest
    return {register["q"]: signal_value(values, register["d"]) for register in seqir["registers"]}


Reference = Callable[[int, Mapping[str, int]], int]


def reference_functions() -> dict[str, tuple[int, tuple[str, ...], Reference]]:
    return {
        "dff_sync": (1, ("rst", "d"), lambda q, i: 0 if i["rst"] else i["d"]),
        "tff_sampled": (
            1,
            ("logical_clk", "t"),
            lambda q, i: q ^ (i["logical_clk"] & i["t"]),
        ),
        "dff_reset_n_sampled": (
            1,
            ("logical_clk", "reset_n", "d"),
            lambda q, i: (
                0
                if not i["reset_n"]
                else (i["d"] if i["logical_clk"] else q)
            ),
        ),
        "toggle_ff": (1, ("rst",), lambda q, i: 0 if i["rst"] else q ^ 1),
        "enable_hold_ff": (1, ("rst", "en", "d"), lambda q, i: 0 if i["rst"] else (i["d"] if i["en"] else q)),
        "reconvergent_feedback_ff": (1, ("rst", "a", "b"), lambda q, i: 0 if i["rst"] else ((q ^ i["a"]) ^ (q & i["b"]))),
        "counter2_sync": (2, ("rst", "en"), lambda q, i: 0 if i["rst"] else ((q + 1) & 3 if i["en"] else q)),
        "johnson2_sync": (2, ("rst",), lambda q, i: 0 if i["rst"] else (((q & 1) << 1) | (1 - ((q >> 1) & 1)))),
        "shift_register3_sampled": (
            3,
            ("logical_clk", "serial_in"),
            lambda q, i: (
                ((q << 1) & 7) | i["serial_in"]
                if i["logical_clk"]
                else q
            ),
        ),
        "shift_register4": (4, ("rst", "en", "serial_in"), lambda q, i: 0 if i["rst"] else ((((q << 1) & 15) | i["serial_in"]) if i["en"] else q)),
        "lfsr4": (4, ("rst", "en"), lambda q, i: 1 if i["rst"] else ((((q << 1) & 15) | (((q >> 3) ^ (q >> 2)) & 1)) if i["en"] else q)),
        "johnson4_free_running": (4, (), lambda q, i: ((q << 1) & 15) | (1 - ((q >> 3) & 1))),
    }


def indexed_signal(name: str, bit: int, width: int) -> str:
    return name if width == 1 else f"{name}[{bit}]"


def verify_transition_relation(module: str, seqir: Mapping[str, Any]) -> dict[str, Any]:
    specification = reference_functions().get(module)
    if specification is None:
        return {"status": "not_available", "cases": 0, "mismatches": 0}
    width, input_names, reference = specification
    mismatches = []
    mismatch_count = 0
    cases = 0
    for state in range(1 << width):
        for vector in itertools.product((0, 1), repeat=len(input_names)):
            primary = dict(zip(input_names, vector))
            values = dict(primary)
            for bit in range(width):
                values[indexed_signal("q", bit, width)] = (state >> bit) & 1
            observed = evaluate_seqir(seqir, values)
            observed_state = sum(
                int(observed[indexed_signal("q", bit, width)]) << bit
                for bit in range(width)
            )
            expected = reference(state, primary)
            cases += 1
            if observed_state != expected:
                mismatch_count += 1
                if len(mismatches) < 8:
                    mismatches.append({
                        "state": state, "inputs": primary,
                        "expected": expected, "observed": observed_state,
                    })
    return {
        "status": "pass" if mismatch_count == 0 else "fail",
        "cases": cases,
        "mismatches": mismatch_count,
        "examples": mismatches,
    }


def layout_metrics(path: Path) -> dict[str, Any]:
    if not path.is_file():
        return {}
    text = path.read_text(errors="replace")
    coordinate_pattern = re.compile(r"\(\s*(-?\d+)\s*,\s*(-?\d+)\s*\)")
    node_pattern = re.compile(
        r"^\s*-?\d+\s*,.*?,.*?,\s*"
        r"\(\s*(-?\d+)\s*,\s*(-?\d+)\s*\)\s*;?\s*$"
    )
    path_pattern = re.compile(
        r"^\s*\(\s*-?\d+\s*,\s*-?\d+\s*\)\s*:\s*(.*)$"
    )
    coordinates: list[tuple[int, int]] = []
    routed_steps = 0
    route_sites: set[tuple[int, int]] = set()
    for line in text.splitlines():
        node_match = node_pattern.match(line)
        if node_match is not None:
            coordinates.append(
                (int(node_match.group(1)), int(node_match.group(2)))
            )
            continue

        path_match = path_pattern.match(line)
        if path_match is None:
            continue
        # The tuple before ":" contains endpoint IDs, not layout coordinates.
        cells = [
            (int(x), int(y))
            for x, y in coordinate_pattern.findall(path_match.group(1))
        ]
        routed_steps += max(0, len(cells) - 1)
        route_sites.update(cells)
        coordinates.extend(cells)
    if not coordinates:
        return {"routed_steps": routed_steps, "unique_route_sites": len(route_sites)}
    xs, ys = [c[0] for c in coordinates], [c[1] for c in coordinates]
    width, height = max(xs) - min(xs) + 1, max(ys) - min(ys) + 1
    return {
        "width": width, "height": height, "bbox_area": width * height,
        "routed_steps": routed_steps, "unique_route_sites": len(route_sites),
    }


def load_report(path: Path) -> dict[str, Any]:
    if not path.is_file():
        return {}
    return json.loads(path.read_text())


def run_design(rtl: Path, output: Path, args: argparse.Namespace, env: Mapping[str, str]) -> dict[str, Any]:
    module = module_name(rtl)
    output.mkdir(parents=True, exist_ok=True)
    result: dict[str, Any] = {
        "design": module, "rtl": rtl.relative_to(ROOT).as_posix(),
        "rtl_sha256": sha256(rtl), "stages": [], "variants": [],
    }
    yosys_json = output / "yosys.json"
    script = output / "synthesis.ys"
    atomic_text(script, yosys_script(rtl.resolve(), module, yosys_json.resolve()))
    yosys_stage = run_stage(
        "yosys", [str(args.yosys), "-l", str(output / "yosys.log"), "-s", str(script)],
        output, env, args.timeout_seconds,
    )
    result["stages"].append(yosys_stage)
    if yosys_stage["status"] != "success":
        result["status"] = yosys_stage["status"]
        return result

    seqir_path, cut_path, state_path = output / "seqir.json", output / "cut.v", output / "state.json"
    converter = executable_prefix(args.converter) + [
        str(yosys_json), "-o", str(seqir_path), "--top", module,
        "--legacy-cut-verilog", str(cut_path), "--state-manifest", str(state_path),
        "--compact-legacy-names",
    ]
    converter_stage = run_stage("converter", converter, output, env, args.timeout_seconds)
    result["stages"].append(converter_stage)
    if converter_stage["status"] != "success":
        result["status"] = converter_stage["status"]
        return result
    seqir = json.loads(seqir_path.read_text())
    transition = verify_transition_relation(module, seqir)
    result["transition_check"] = transition
    result["state_bits"] = len(seqir["registers"])
    result["comb_nodes"] = len(seqir["combinational_nodes"])
    if transition["status"] != "pass":
        result["status"] = "semantic_mismatch"
        return result
    states = state_arguments(state_path)

    variants = [
        ("cut", args.cut_pnr, args.ii),
        ("cyclic_ii4", args.cyclic_pnr, "4"),
        ("cyclic_adaptive", args.cyclic_pnr, args.ii),
    ]
    for name, executable, ii in variants:
        variant_dir = output / name
        variant_dir.mkdir(parents=True, exist_ok=True)
        layout, tex = variant_dir / "layout.ifcn", variant_dir / "layout.tex"
        layout_report = Path(str(layout) + ".json")
        clear_stage_artifacts((layout, layout_report, tex))
        command = executable_prefix(executable) + [str(cut_path), str(layout)]
        for state in states:
            command.extend(["--state", state])
        command.extend([
            "--ii", ii, "--max-same-phase", str(args.max_same_phase),
            "--max-dfs-nodes", str(args.max_dfs_nodes),
            "--spacing", str(args.spacing), "--tex", str(tex),
        ])
        if name.startswith("cyclic"):
            command.extend([
                "--route-search-cost", str(args.route_search_cost),
                "--compaction-max-states", str(args.compaction_max_states),
                "--compaction-seeds", str(args.compaction_seeds),
            ])
        stage = run_stage(name, command, variant_dir, env, args.timeout_seconds)
        metrics: dict[str, Any] = {}
        if stage["status"] == "success":
            metrics = {**layout_metrics(layout), **load_report(layout_report)}
        variant = {
            "name": name, "status": stage["status"],
            "duration_seconds": stage["duration_seconds"],
            # Prefer measurements emitted by the C++ flow; the parser is a
            # compatibility fallback for older report schemas.
            "stage": stage, "metrics": metrics,
            "layout": artifact_reference(layout, stage["status"]),
            "tex": artifact_reference(tex, stage["status"]),
        }
        result["variants"].append(variant)

    z3_dir = output / "cyclic_z3_adaptive"
    z3_dir.mkdir(parents=True, exist_ok=True)
    z3_layout, z3_tex = z3_dir / "layout.ifcn", z3_dir / "layout.tex"
    problem_path = z3_dir / "clock_problem.json"
    solution_path = z3_dir / "clock_solution.tsv"
    z3_summary = z3_dir / "z3_summary.json"
    z3_layout_report = Path(str(z3_layout) + ".json")
    clear_stage_artifacts((
        z3_layout, z3_layout_report, z3_tex, problem_path,
        solution_path, z3_summary,
    ))
    geometry_command = executable_prefix(args.cyclic_pnr) + [str(cut_path), str(z3_layout)]
    for state in states:
        geometry_command.extend(["--state", state])
    geometry_command.extend([
        "--ii", args.ii, "--max-same-phase", str(args.max_same_phase),
        "--max-dfs-nodes", str(args.max_dfs_nodes), "--spacing", str(args.spacing),
        "--route-search-cost", str(args.route_search_cost),
        "--compaction-max-states", str(args.compaction_max_states),
        "--compaction-seeds", str(args.compaction_seeds),
        "--clock-problem-out", str(problem_path), "--defer-phase",
    ])
    geometry_stage = run_stage(
        "cyclic_z3_geometry", geometry_command, z3_dir, env, args.timeout_seconds
    )
    z3_stages = [geometry_stage]
    z3_status = geometry_stage["status"]
    z3_metrics: dict[str, Any] = {}
    if geometry_stage["status"] == "success":
        geometry_stdout = ROOT / str(geometry_stage["stdout"])
        geometry_count = parse_geometry_candidate_count(
            geometry_stdout.read_text(encoding="utf-8", errors="replace")
        )
        attempted_ranks: list[dict[str, Any]] = []
        solve_statuses: list[str] = []
        selected_rank: int | None = None
        fallback_abort_status: str | None = None
        rank_limit = min(geometry_count, args.max_geometry_ranks)
        ladder_started = time.monotonic()
        ladder_budget_exhausted = False
        for geometry_rank in range(rank_limit):
            if (
                geometry_rank > 0
                and time.monotonic() - ladder_started
                >= args.geometry_ladder_seconds
            ):
                fallback_abort_status = "limit"
                ladder_budget_exhausted = True
                break
            if geometry_rank > 0:
                clear_stage_artifacts((problem_path, solution_path, z3_summary))
                ranked_geometry_command = [
                    *geometry_command, "--geometry-rank", str(geometry_rank)
                ]
                ranked_geometry_stage = run_stage(
                    f"cyclic_z3_geometry_rank_{geometry_rank:03d}",
                    ranked_geometry_command, z3_dir, env, args.timeout_seconds,
                )
                z3_stages.append(ranked_geometry_stage)
                if ranked_geometry_stage["status"] != "success":
                    fallback_abort_status = ranked_geometry_stage["status"]
                    break

            clear_stage_artifacts((solution_path, z3_summary))
            solve_command = executable_prefix(args.z3_solver) + [
                str(problem_path), str(solution_path), "--summary", str(z3_summary),
                "--z3-root", str(args.z3_root),
                "--timeout-ms", str(args.z3_timeout_ms),
            ]
            solve_stage = run_stage(
                f"cyclic_z3_solve_rank_{geometry_rank:03d}", solve_command,
                z3_dir, env,
                max(args.timeout_seconds, args.z3_timeout_ms / 1000.0 + 5.0),
            )
            z3_stages.append(solve_stage)
            solve_statuses.append(str(solve_stage["status"]))
            attempt = {"rank": geometry_rank, "status": solve_stage["status"]}
            solve_summary = load_json_artifact(z3_summary, solve_stage["status"])
            if solve_summary is not None:
                attempt["z3"] = solve_summary
            attempted_ranks.append(attempt)
            if solve_stage["status"] == "success":
                selected_rank = geometry_rank
                break
            if solve_stage["status"] not in {"unsat", "unknown", "limit"}:
                fallback_abort_status = str(solve_stage["status"])
                break

        rank_limit_reached = (
            selected_rank is None
            and len(attempted_ranks) >= rank_limit
            and rank_limit < geometry_count
        )
        if rank_limit_reached:
            fallback_abort_status = "limit"

        if selected_rank is not None:
            z3_status = "success"
        elif fallback_abort_status is not None:
            z3_status = fallback_abort_status
        else:
            z3_status = reduce_geometry_fallback_status(solve_statuses)

        z3_metrics["geometry_fallback"] = {
            "candidate_count": geometry_count,
            "rank_limit": rank_limit,
            "rank_limit_reached": rank_limit_reached,
            "ladder_budget_seconds": args.geometry_ladder_seconds,
            "ladder_budget_exhausted": ladder_budget_exhausted,
            "attempts": attempted_ranks,
            "selected_rank": selected_rank,
        }
        if selected_rank is not None:
            clear_stage_artifacts((z3_layout, z3_layout_report, z3_tex))
            finalize_command = executable_prefix(args.cyclic_pnr) + [
                str(cut_path), str(z3_layout)
            ]
            for state in states:
                finalize_command.extend(["--state", state])
            finalize_command.extend([
                "--ii", args.ii, "--max-same-phase", str(args.max_same_phase),
                "--max-dfs-nodes", str(args.max_dfs_nodes),
                "--spacing", str(args.spacing),
                "--route-search-cost", str(args.route_search_cost),
                "--compaction-max-states", str(args.compaction_max_states),
                "--compaction-seeds", str(args.compaction_seeds),
                "--geometry-rank", str(selected_rank),
                "--phase-solution", str(solution_path), "--tex", str(z3_tex),
            ])
            finalize_stage = run_stage(
                "cyclic_z3_finalize", finalize_command, z3_dir, env,
                args.timeout_seconds,
            )
            z3_stages.append(finalize_stage)
            z3_status = finalize_stage["status"]
            if finalize_stage["status"] == "success":
                z3_metrics = {
                    **layout_metrics(z3_layout),
                    **load_report(z3_layout_report),
                    **z3_metrics,
                }
    result["variants"].append({
        "name": "cyclic_z3_adaptive", "status": z3_status,
        "duration_seconds": round(sum(float(stage["duration_seconds"]) for stage in z3_stages), 6),
        "stages": z3_stages, "metrics": z3_metrics,
        "layout": artifact_reference(z3_layout, z3_status),
        "tex": artifact_reference(z3_tex, z3_status),
    })
    result["status"] = "complete"
    return result


def aggregate(records: Sequence[Mapping[str, Any]]) -> list[dict[str, Any]]:
    groups: dict[str, list[Mapping[str, Any]]] = {}
    for record in records:
        for variant in record.get("variants", []):
            groups.setdefault(str(variant["name"]), []).append(variant)
    output = []
    for name, variants in sorted(groups.items()):
        successes = [v for v in variants if v["status"] == "success"]
        times = [float(v["duration_seconds"]) for v in variants]
        output.append({
            "variant": name, "cases": len(variants), "successes": len(successes),
            "success_rate": len(successes) / len(variants) if variants else 0.0,
            "status_counts": {status: sum(v["status"] == status for v in variants) for status in sorted({str(v["status"]) for v in variants})},
            "runtime_seconds_median": statistics.median(times) if times else None,
            "runtime_seconds_p95": sorted(times)[max(0, math.ceil(0.95 * len(times)) - 1)] if times else None,
        })
    return output


def write_csv(path: Path, records: Sequence[Mapping[str, Any]]) -> None:
    fields = [
        "design", "state_bits", "comb_nodes", "transition_cases", "transition_status",
        "variant", "status", "duration_seconds", "ii", "nodes", "routes",
        "feedback_routes", "directed_cycle_present", "mapped_qca_cells",
        "width", "height", "bbox_area", "routed_steps", "dfs_nodes", "decisions",
        "forced_edges", "conflicts", "ii_candidates_tried", "geometry_seconds",
        "phase_seconds", "mapping_seconds", "physical_state_signoff",
        "layout", "tex",
    ]
    rows = []
    for record in records:
        for variant in record.get("variants", []):
            metrics = variant.get("metrics", {})
            solver = metrics.get("phase_solver", {})
            runtimes = metrics.get("runtime_seconds", {})
            rows.append({
                "design": record["design"], "state_bits": record.get("state_bits", ""),
                "comb_nodes": record.get("comb_nodes", ""),
                "transition_cases": record.get("transition_check", {}).get("cases", ""),
                "transition_status": record.get("transition_check", {}).get("status", ""),
                "variant": variant["name"], "status": variant["status"],
                "duration_seconds": variant["duration_seconds"],
                "ii": metrics.get("initiation_interval", ""), "nodes": metrics.get("nodes", ""),
                "routes": metrics.get("routes", ""), "feedback_routes": metrics.get("feedback_routes", ""),
                "directed_cycle_present": metrics.get("directed_cycle_present", ""),
                "mapped_qca_cells": metrics.get("mapped_qca_cells", ""),
                "width": metrics.get("bbox_width", metrics.get("width", "")),
                "height": metrics.get("bbox_height", metrics.get("height", "")),
                "bbox_area": metrics.get("bbox_area", ""),
                "routed_steps": metrics.get("route_steps", metrics.get("routed_steps", "")),
                "dfs_nodes": solver.get("dfs_nodes", ""), "decisions": solver.get("decisions", ""),
                "forced_edges": solver.get("forced_edges", ""), "conflicts": solver.get("conflicts", ""),
                "ii_candidates_tried": solver.get("ii_candidates_tried", ""),
                "geometry_seconds": runtimes.get("geometry", ""), "phase_seconds": runtimes.get("phase", ""),
                "mapping_seconds": runtimes.get("mapping", ""),
                "physical_state_signoff": metrics.get("physical_state_signoff", ""),
                "layout": variant.get("layout") or "", "tex": variant.get("tex") or "",
            })
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def parse_args(argv: Sequence[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--rtl-root", type=Path, default=DEFAULT_RTL_ROOT)
    parser.add_argument("--output-dir", type=Path, default=DEFAULT_OUTPUT)
    parser.add_argument("--yosys", type=Path, default=DEFAULT_YOSYS)
    parser.add_argument("--converter", type=Path, default=DEFAULT_CONVERTER)
    parser.add_argument("--cut-pnr", type=Path, default=DEFAULT_CUT_PNR)
    parser.add_argument("--cyclic-pnr", type=Path, default=DEFAULT_CYCLIC_PNR)
    parser.add_argument("--z3-solver", type=Path, default=DEFAULT_Z3_SOLVER)
    parser.add_argument("--z3-root", type=Path, default=DEFAULT_Z3_ROOT)
    parser.add_argument("--z3-timeout-ms", type=int, default=60_000)
    parser.add_argument("--ii", default="4,8,12,16,20,24")
    parser.add_argument("--max-same-phase", type=int, default=4)
    parser.add_argument("--max-dfs-nodes", type=int, default=5_000_000)
    parser.add_argument("--spacing", type=int, default=2)
    parser.add_argument("--route-search-cost", type=float, default=80.0)
    parser.add_argument("--compaction-max-states", type=int, default=256)
    parser.add_argument("--compaction-seeds", type=int, default=16)
    parser.add_argument("--timeout-seconds", type=float, default=120.0)
    parser.add_argument("--max-geometry-ranks", type=int, default=64)
    parser.add_argument("--geometry-ladder-seconds", type=float, default=120.0)
    parser.add_argument("--design", action="append", default=[])
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = parse_args(argv)
    if not 1 <= args.max_same_phase <= 4:
        raise SystemExit("--max-same-phase must be between 1 and 4 tiles")
    if args.max_geometry_ranks <= 0:
        raise SystemExit("--max-geometry-ranks must be positive")
    if args.geometry_ladder_seconds <= 0.0:
        raise SystemExit("--geometry-ladder-seconds must be positive")
    args.rtl_root = args.rtl_root.resolve()
    args.output_dir = args.output_dir.resolve()
    for key in ("yosys", "converter", "cut_pnr", "cyclic_pnr", "z3_solver", "z3_root"):
        setattr(args, key, getattr(args, key).resolve())
    args.output_dir.mkdir(parents=True, exist_ok=True)
    candidates = sorted(
        path for path in args.rtl_root.glob("*.v")
        if not path.stem.endswith("_cut") and path.stem in reference_functions()
    )
    if args.design:
        selected = set(args.design)
        candidates = [path for path in candidates if path.stem in selected]
    env = local_yosys_environment(args.yosys)
    probe = subprocess.run([str(args.yosys), "-V"], env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
    records = []
    for rtl in candidates:
        print(f"[sequential-rtl] {rtl.stem}", flush=True)
        try:
            record = run_design(rtl, args.output_dir / rtl.stem, args, env)
        except Exception as exc:  # preserve the rest of a publication batch
            record = {"design": rtl.stem, "rtl": rtl.relative_to(ROOT).as_posix(), "status": "runner_error", "error": str(exc), "variants": []}
        records.append(record)
        write_json(args.output_dir / rtl.stem / "result.json", record)
    summary = {
        "schema": "ifcn.sequential-rtl-experiments.v1",
        "created_at": utc_now(),
        "scope": "RTL semantics plus abstract cut and uncharacterized-state cyclic gate-level P&R; not cell-level storage sign-off",
        "configuration": {
            "rtl_root": args.rtl_root.relative_to(ROOT).as_posix(), "ii": args.ii,
            "max_same_phase": args.max_same_phase, "max_dfs_nodes": args.max_dfs_nodes,
            "spacing": args.spacing, "route_search_cost": args.route_search_cost,
            "compaction_max_states": args.compaction_max_states,
            "compaction_seeds": args.compaction_seeds,
            "timeout_seconds": args.timeout_seconds,
            "max_geometry_ranks": args.max_geometry_ranks,
            "geometry_ladder_seconds": args.geometry_ladder_seconds,
        },
        "environment": {
            "platform": platform.platform(), "python": platform.python_version(),
            "yosys": probe.stdout.strip(), "yosys_sha256": sha256(args.yosys),
            "cut_pnr_sha256": sha256(args.cut_pnr), "cyclic_pnr_sha256": sha256(args.cyclic_pnr),
            "z3_solver_sha256": sha256(args.z3_solver),
            "z3_root": str(args.z3_root),
        },
        "records": records,
        "aggregate": aggregate(records),
    }
    write_json(args.output_dir / "summary.json", summary)
    write_csv(args.output_dir / "summary.csv", records)
    failed_semantics = [r["design"] for r in records if r.get("transition_check", {}).get("status") != "pass"]
    print(f"summary={args.output_dir / 'summary.json'} semantic_failures={len(failed_semantics)}")
    return 1 if failed_semantics else 0


if __name__ == "__main__":
    raise SystemExit(main())
