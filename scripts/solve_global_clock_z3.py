#!/usr/bin/env python3
"""Solve an exported iFCN global phase/epoch problem with Z3.

This is an optional scalable backend for fixed routed geometry.  The returned
TSV is deliberately consumed by the C++ flow and then checked again with
``GlobalPhaseSolver::validateSolution``; a Z3 model is never trusted as the
final sign-off by itself.
"""

from __future__ import annotations

import argparse
import builtins
import json
import os
import platform
import sys
import tempfile
import time
from pathlib import Path
from typing import Any, Mapping, Sequence


ROOT = Path(__file__).resolve().parents[1]
DEFAULT_Z3_ROOT = ROOT / "build" / "tools" / "z3-local"


class SolveError(RuntimeError):
    pass


SIGNED_EPOCH_BITS = 32


def _can_use_signed_epoch_bitvectors(
    phase_count: int,
    candidates: Sequence[int],
    routes: Sequence[Mapping[str, Any]],
    timing_arcs: Sequence[Mapping[str, Any]],
    anchors: Sequence[Mapping[str, Any]],
) -> bool:
    """Return whether exact epoch arithmetic fits a signed 32-bit BV model.

    For a power-of-two phase count, modulo-phase equality is exactly equality
    of the epoch's low bits.  Bit-vectors let Z3 bit-blast the remaining 0/1
    route decisions instead of mixing them with unbounded integer quotient
    variables.  The conservative envelope below covers every simple path from
    an anchor through all timing arcs, route iteration offsets, and route
    edges.  Staying below the signed limit therefore also rules out a model
    that satisfies an integer equality only by wrapping around the bit-vector.
    """
    if phase_count < 2 or phase_count & (phase_count - 1):
        return False
    maximum_ii = max((abs(int(value)) for value in candidates), default=0)
    anchor_magnitude = max(
        (abs(int(anchor["epoch"])) for anchor in anchors), default=0
    )
    timing_span = sum(
        abs(int(arc["latency_epochs"]))
        + abs(int(arc["iteration_distance"])) * maximum_ii
        for arc in timing_arcs
    )
    route_span = sum(
        abs(int(route["iteration_distance"])) * maximum_ii
        + max(0, len(route["occurrences"]) - 1)
        for route in routes
    )
    return (
        anchor_magnitude + timing_span + route_span
        < (1 << (SIGNED_EPOCH_BITS - 1))
    )


def _signed_model_value(model: Any, variable: Any, bitvector: bool) -> int:
    value = model.eval(variable, model_completion=True).as_long()
    if bitvector and value >= (1 << (SIGNED_EPOCH_BITS - 1)):
        value -= 1 << SIGNED_EPOCH_BITS
    return value


def load_z3(root: Path):
    python_path = root / "usr" / "lib" / "python3" / "dist-packages"
    library_path = root / "usr" / "lib" / "x86_64-linux-gnu"
    if python_path.is_dir():
        sys.path.insert(0, str(python_path))
        builtins.Z3_LIB_DIRS = [str(library_path)]
    try:
        import z3  # type: ignore
    except ImportError as exc:
        raise SolveError(
            "z3 Python bindings are unavailable; pass --z3-root or install z3-solver"
        ) from exc
    return z3


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


def require_list(problem: Mapping[str, Any], key: str) -> list[Any]:
    value = problem.get(key)
    if not isinstance(value, list):
        raise SolveError(f"{key} must be a list")
    return value


def solve(problem: Mapping[str, Any], z3: Any, timeout_ms: int) -> dict[str, Any]:
    if problem.get("schema") != "ifcn.global-clock-problem.v1":
        raise SolveError("unsupported clock-problem schema")
    occurrence_granularity = problem.get("occurrence_granularity")
    if occurrence_granularity not in (None, "tile"):
        raise SolveError(
            "unsupported clock occurrence granularity; sequential P&R uses tile"
        )
    phase_count = int(problem["phase_count"])
    if "max_consecutive_same_phase_occurrences" in problem:
        max_run = int(problem["max_consecutive_same_phase_occurrences"])
    else:
        # Legacy generic clock-problem fixtures used the misleading suffix
        # "cells" even though the solver has always constrained occurrences.
        max_run = int(problem["max_consecutive_same_phase_cells"])
    candidates = [int(value) for value in require_list(problem, "ii_candidates")]
    event_ids = [str(value) for value in require_list(problem, "events")]
    resources = require_list(problem, "clock_resources")
    occurrences = require_list(problem, "occurrences")
    routes = require_list(problem, "routes")
    timing_arcs = require_list(problem, "timing_arcs")
    anchors = require_list(problem, "anchors")

    resource_by_id = {str(item["id"]): item for item in resources}
    if len(resource_by_id) != len(resources):
        raise SolveError("duplicate clock-resource identifier")
    for identifier, resource in resource_by_id.items():
        if not identifier:
            raise SolveError("clock-resource identifier must not be empty")
        if resource.get("sharing") not in {
            "exclusive_or_aliased", "phase_shared_independent_epochs"
        }:
            raise SolveError(f"clock resource {identifier} has invalid sharing")

    occurrence_by_id = {str(item["id"]): item for item in occurrences}
    if len(occurrence_by_id) != len(occurrences):
        raise SolveError("duplicate occurrence identifier")
    epoch_ids = sorted({str(item["epoch_variable"]) for item in occurrences})
    resource_users: dict[str, list[str]] = {identifier: [] for identifier in resource_by_id}
    for occurrence in occurrences:
        resource = str(occurrence["clock_resource"])
        if resource not in resource_users:
            raise SolveError(
                f"occurrence {occurrence['id']} refers to unknown clock resource {resource}"
            )
        resource_users[resource].append(str(occurrence["epoch_variable"]))
    for identifier, users in resource_users.items():
        if (
            resource_by_id[identifier]["sharing"] == "exclusive_or_aliased"
            and len(set(users)) > 1
        ):
            raise SolveError(
                f"exclusive clock resource {identifier} uses independent epochs"
            )
        # Repeated fanout occurrences may intentionally name the same absolute
        # epoch variable.  They add no phase constraint and should not create
        # redundant congruence/quotient variables in the backend.
        resource_users[identifier] = list(dict.fromkeys(users))

    use_epoch_bitvectors = _can_use_signed_epoch_bitvectors(
        phase_count, candidates, routes, timing_arcs, anchors
    )
    phase_bits = phase_count.bit_length() - 1 if use_epoch_bitvectors else 0

    attempted = []
    unknown_attempts = []
    started_all = time.perf_counter()
    for ii in candidates:
        solver = z3.Solver()
        if timeout_ms > 0:
            solver.set(timeout=timeout_ms)
        if use_epoch_bitvectors:
            event = {
                identifier: z3.BitVec(f"event_{index}", SIGNED_EPOCH_BITS)
                for index, identifier in enumerate(event_ids)
            }
            epoch = {
                identifier: z3.BitVec(f"epoch_{index}", SIGNED_EPOCH_BITS)
                for index, identifier in enumerate(epoch_ids)
            }

            def epoch_constant(value: int) -> Any:
                return z3.BitVecVal(value, SIGNED_EPOCH_BITS)
        else:
            event = {
                identifier: z3.Int(f"event_{index}")
                for index, identifier in enumerate(event_ids)
            }
            epoch = {
                identifier: z3.Int(f"epoch_{index}")
                for index, identifier in enumerate(epoch_ids)
            }

            def epoch_constant(value: int) -> int:
                return value
        assertions = 0

        def add(expression: Any) -> None:
            nonlocal assertions
            solver.add(expression)
            assertions += 1

        for anchor in anchors:
            add(
                event[str(anchor["event"])]
                == epoch_constant(int(anchor["epoch"]))
            )
        for arc in timing_arcs:
            source = event[str(arc["source_event"])]
            sink = event[str(arc["sink_event"])]
            distance = int(arc["iteration_distance"])
            latency = int(arc["latency_epochs"])
            add(
                sink + epoch_constant(distance * ii) - source
                == epoch_constant(latency)
            )
        for route in routes:
            occurrence_ids = [str(value) for value in route["occurrences"]]
            if not occurrence_ids:
                raise SolveError(f"route {route['id']} has no occurrences")
            variables = [epoch[str(occurrence_by_id[item]["epoch_variable"])] for item in occurrence_ids]
            source_event = event[str(route["source_event"])]
            sink_event = event[str(route["sink_event"])]
            iteration_offset = int(route["iteration_distance"]) * ii
            add(variables[0] == event[str(route["source_event"])])
            add(
                variables[-1]
                == sink_event + epoch_constant(iteration_offset)
            )
            # A route containing E hold/+1 edges has an endpoint advance in
            # [0,E].  With a maximum run R it needs at least floor(E/R)
            # advancing edges.  These redundant event-level bounds are a
            # sound projection of the detailed constraints and make cyclic
            # low-II contradictions visible before branching on every cell.
            route_edges = len(variables) - 1
            endpoint_advance = (
                sink_event + epoch_constant(iteration_offset) - source_event
            )
            minimum_advance = route_edges // max_run if max_run > 0 else 0
            add(endpoint_advance >= epoch_constant(minimum_advance))
            add(endpoint_advance <= epoch_constant(route_edges))
            advances = []
            for edge_index, (previous, current) in enumerate(zip(variables, variables[1:])):
                advance = z3.Bool(f"route_{len(attempted)}_{route['id']}_{edge_index}")
                add(
                    current
                    == previous
                    + z3.If(
                        advance, epoch_constant(1), epoch_constant(0)
                    )
                )
                advances.append(advance)
            if max_run > 0:
                for begin in range(0, len(advances) - max_run + 1):
                    add(z3.Or(*advances[begin:begin + max_run]))
        congruence_index = 0
        for users in resource_users.values():
            if len(users) < 2:
                continue
            representative = epoch[users[0]]
            for identifier in users[1:]:
                if use_epoch_bitvectors:
                    # For phase counts 2/4/8, congruence is exactly equality
                    # of the low log2(phase_count) epoch bits.  This pure-BV
                    # encoding avoids the mixed integer/Boolean search blowup
                    # seen on crossover-heavy physical QCA layouts.
                    add(
                        z3.Extract(phase_bits - 1, 0, epoch[identifier])
                        == z3.Extract(phase_bits - 1, 0, representative)
                    )
                else:
                    quotient = z3.Int(
                        f"resource_period_{congruence_index}"
                    )
                    congruence_index += 1
                    # Linearize congruence instead of using integer mod; this
                    # remains the exact fallback for non-power-of-two phases
                    # or a problem outside the signed-BV safety envelope.
                    add(
                        epoch[identifier] - representative
                        == phase_count * quotient
                    )

        started = time.perf_counter()
        status = solver.check()
        elapsed = time.perf_counter() - started
        attempted.append({
            "ii": ii, "status": str(status), "seconds": elapsed,
            "assertions": assertions,
        })
        if status == z3.unknown:
            reason = solver.reason_unknown()
            attempted[-1]["reason"] = reason
            unknown_attempts.append({"ii": ii, "reason": reason})
            continue
        if status != z3.sat:
            continue
        model = solver.model()
        value = lambda variable: _signed_model_value(
            model, variable, use_epoch_bitvectors
        )
        event_epoch = {identifier: value(variable) for identifier, variable in event.items()}
        epoch_value = {identifier: value(variable) for identifier, variable in epoch.items()}
        occurrence_epoch = {
            str(item["id"]): epoch_value[str(item["epoch_variable"])]
            for item in occurrences
        }
        resource_phase = {}
        for resource in resources:
            identifier = str(resource["id"])
            users = resource_users[identifier]
            if not users:
                # Unused declarations are legal in the C++ problem contract
                # and have no phase to materialize in a solution.
                continue
            resource_phase[identifier] = epoch_value[users[0]] % phase_count
        return {
            "status": "SAT", "phase_count": phase_count, "ii": ii,
            "event_epoch": event_epoch, "occurrence_epoch": occurrence_epoch,
            "clock_resource_phase": resource_phase, "attempted": attempted,
            "seconds": time.perf_counter() - started_all,
        }
    if unknown_attempts:
        return {
            "status": "UNKNOWN",
            "reason": "; ".join(
                f"ii={attempt['ii']}: {attempt['reason']}"
                for attempt in unknown_attempts
            ),
            "attempted": attempted,
            "seconds": time.perf_counter() - started_all,
        }
    return {
        "status": "UNSAT", "attempted": attempted,
        "seconds": time.perf_counter() - started_all,
    }


def render_solution(result: Mapping[str, Any]) -> str:
    lines = ["ifcn.global-clock-solution.v1", f"status\t{result['status']}"]
    if result["status"] == "SAT":
        lines.extend([
            f"phase_count\t{result['phase_count']}",
            f"ii\t{result['ii']}",
        ])
        lines.extend(f"event\t{key}\t{value}" for key, value in sorted(result["event_epoch"].items()))
        lines.extend(f"occurrence\t{key}\t{value}" for key, value in sorted(result["occurrence_epoch"].items()))
        lines.extend(f"resource\t{key}\t{value}" for key, value in sorted(result["clock_resource_phase"].items()))
    return "\n".join(lines) + "\n"


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("problem", type=Path)
    parser.add_argument("solution", type=Path)
    parser.add_argument("--summary", type=Path)
    parser.add_argument("--z3-root", type=Path, default=DEFAULT_Z3_ROOT)
    parser.add_argument("--timeout-ms", type=int, default=60_000)
    args = parser.parse_args(argv)
    z3 = load_z3(args.z3_root.resolve())
    problem = json.loads(args.problem.read_text())
    result = solve(problem, z3, args.timeout_ms)
    atomic_text(args.solution, render_solution(result))
    summary = {
        "schema": "ifcn.global-clock-z3-run.v1",
        "problem": str(args.problem), "solution": str(args.solution),
        "backend": "z3", "z3_version": z3.get_version_string(),
        "python": platform.python_version(), **result,
    }
    if args.summary:
        atomic_text(args.summary, json.dumps(summary, indent=2, sort_keys=True) + "\n")
    print(
        f"global_clock_z3={result['status']} z3={z3.get_version_string()} "
        f"seconds={result['seconds']:.6f} solution={args.solution}"
    )
    return {"SAT": 0, "UNSAT": 2, "UNKNOWN": 3}.get(str(result["status"]), 1)


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError, SolveError) as error:
        print(f"solve_global_clock_z3 failed: {error}", file=sys.stderr)
        raise SystemExit(1)
