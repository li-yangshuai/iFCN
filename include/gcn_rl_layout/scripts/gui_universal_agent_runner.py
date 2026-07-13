#!/usr/bin/env python3
"""Run the trained universal memory agent for one circuit and export GUI artifacts.

Unlike :mod:`evaluate_universal_graph_ppo`, this entry point retains the full
exact-routing result for the winning stochastic-clock scenario.  The exported
filenames and the small set of top-level summary metrics intentionally match
the legacy GUI runner so the C++ side can load the result without knowing how
the placement was produced.
"""

from __future__ import annotations

import argparse
from dataclasses import asdict
import glob
import json
import math
import os
import sys
import time
from types import SimpleNamespace

import numpy as np
import torch


SCRIPT_DIR = os.path.abspath(os.path.dirname(__file__))
LAYOUT_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, ".."))
PROJECT_ROOT = os.path.abspath(os.path.join(LAYOUT_ROOT, "..", ".."))
ALGORITHM_ROOT = os.path.join(LAYOUT_ROOT, "src", "algorithm")
MAIN_ROOT = os.path.join(ALGORITHM_ROOT, "main")
RESULTS_ROOT = os.path.join(LAYOUT_ROOT, "results")
for import_root in (ALGORITHM_ROOT, MAIN_ROOT):
    if import_root not in sys.path:
        sys.path.insert(0, import_root)

from evaluate_universal_graph_ppo import (  # noqa: E402
    RUNTIME_DEFAULTS,
    _run_policy_trial,
    load_checkpoint,
)
from src.layout_retrieval_memory import LayoutRetrievalMemory  # noqa: E402
from src.memory_policy_bridge import retrieve_policy_memory  # noqa: E402
from src.stochastic_clock import CAUSAL_CLOCK_MODES, sample_clock_field  # noqa: E402
from train_layout_ppo import (  # noqa: E402
    clone_positions,
    evaluate_layout_candidate_with_timeout,
    export_layout_artifacts,
    is_legal_layout_result,
    resolve_device,
    scalarize_layout_result,
    set_global_seed,
)
from train_universal_graph_ppo import (  # noqa: E402
    clock_aligned_start_positions,
    count_clock_violations,
    field_bounds_for_positions,
    prepare_context,
    sample_field,
)


def parse_args(argv=None):
    parser = argparse.ArgumentParser(
        description=(
            "Run one trained recurrent universal GCN+RL agent under sampled "
            "causal clock fields and export an IFCN layout for the desktop GUI."
        )
    )
    parser.add_argument("--benchmark", required=True, help="Input Verilog circuit.")
    parser.add_argument("--output-dir", required=True, help="Artifact output directory.")
    parser.add_argument(
        "--checkpoint",
        default="auto",
        help=(
            "Universal graph PPO checkpoint, or 'auto' to use the newest "
            "universal_graph_ppo_best_exact.pt below the results directory."
        ),
    )
    parser.add_argument("--device", choices=("auto", "cpu", "cuda"), default="auto")
    parser.add_argument("--seed", type=int, default=20260711)
    parser.add_argument("--clock-field-samples", type=int, default=8)
    parser.add_argument("--policy-trials", type=int, default=1)
    parser.add_argument("--steps-per-episode", type=int, default=None)
    parser.add_argument("--exact-eval-timeout-sec", type=int, default=None)
    parser.add_argument("--parse-mode", choices=("auto", "compact", "layered"), default="auto")
    parser.add_argument("--phase-count", type=int, choices=(3, 4), default=None)
    parser.add_argument("--padding", type=int, default=None)
    parser.add_argument("--max-same-phase", type=int, default=None)
    parser.add_argument("--clock-mode", choices=CAUSAL_CLOCK_MODES, default=None)
    parser.add_argument("--retrieval-top-k", type=int, default=4)
    parser.add_argument(
        "--clock-aligned-start",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="Align the initial placement with sampled clock causality (default).",
    )
    parser.add_argument(
        "--allow-exact-memory-retrieval",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="Allow same-topology IFCN exemplars during practical GUI inference (default).",
    )
    parser.add_argument(
        "--retrieval-memory",
        default=None,
        help="Optional memory JSON override; normally read from the checkpoint.",
    )
    parser.add_argument(
        "--deterministic",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="Use greedy policy actions (default); --no-deterministic samples actions.",
    )
    parser.add_argument(
        "--require-legal",
        action=argparse.BooleanOptionalAction,
        default=True,
        help="Fail without exporting when neither policy nor fallback routes legally (default).",
    )
    return parser.parse_args(argv)


def resolve_checkpoint(checkpoint: str, results_root: str = RESULTS_ROOT) -> str:
    """Resolve an explicit checkpoint or the newest trained exact checkpoint."""

    if checkpoint and str(checkpoint).strip().lower() == "auto":
        environment_checkpoint = os.environ.get("IFCN_UNIVERSAL_AGENT_CHECKPOINT")
        if environment_checkpoint:
            checkpoint = environment_checkpoint
    if checkpoint and str(checkpoint).strip().lower() != "auto":
        resolved = os.path.abspath(os.path.expanduser(str(checkpoint)))
        if not os.path.isfile(resolved):
            raise FileNotFoundError(f"universal agent checkpoint not found: {resolved}")
        return resolved

    pattern = os.path.join(os.path.abspath(results_root), "**", "universal_graph_ppo_best_exact.pt")
    candidates = [path for path in glob.glob(pattern, recursive=True) if os.path.isfile(path)]
    if not candidates:
        raise FileNotFoundError(
            "no universal_graph_ppo_best_exact.pt was found below "
            f"{os.path.abspath(results_root)}; pass --checkpoint explicitly"
        )
    candidates.sort(key=lambda path: (os.path.getmtime(path), path), reverse=True)
    return os.path.abspath(candidates[0])


def _possible_memory_paths(raw_path: str | None, checkpoint_path: str):
    if not raw_path:
        return
    expanded = os.path.expanduser(str(raw_path))
    if os.path.isabs(expanded):
        yield os.path.abspath(expanded)
        return
    yield os.path.abspath(expanded)
    yield os.path.abspath(os.path.join(PROJECT_ROOT, expanded))
    yield os.path.abspath(os.path.join(os.path.dirname(checkpoint_path), expanded))


def resolve_retrieval_memory(args, checkpoint_path: str, checkpoint_payload):
    """Load the checkpoint's persistent memory, with relocation-safe fallbacks."""

    requested = getattr(args, "retrieval_memory", None)
    candidates = []
    for raw_path in (
        requested,
        checkpoint_payload.get("retrieval_memory"),
        checkpoint_payload.get("config", {}).get("retrieval_memory"),
    ):
        candidates.extend(_possible_memory_paths(raw_path, checkpoint_path) or ())
    candidates.extend(
        os.path.join(os.path.dirname(checkpoint_path), filename)
        for filename in (
            "layout_retrieval_memory_online.json",
            "layout_retrieval_memory.json",
        )
    )

    visited = set()
    for candidate in candidates:
        candidate = os.path.abspath(candidate)
        if candidate in visited:
            continue
        visited.add(candidate)
        if os.path.isfile(candidate):
            return LayoutRetrievalMemory.load(candidate), candidate

    if requested:
        raise FileNotFoundError(f"retrieval memory not found: {os.path.abspath(requested)}")
    return None, None


def build_runtime_args(cli_args, checkpoint_payload) -> SimpleNamespace:
    settings = dict(RUNTIME_DEFAULTS)
    settings.update(checkpoint_payload.get("config", {}))
    settings["device"] = cli_args.device
    # Keep preprocessing tied to the trained checkpoint so changing the clock
    # seed does not retrain the small GCN or fragment its on-disk cache.
    settings["seed"] = int(settings.get("seed", RUNTIME_DEFAULTS["seed"]))
    settings["parse_mode"] = str(cli_args.parse_mode)
    settings["disable_gcn_cache"] = False
    settings["clock_aligned_start"] = bool(cli_args.clock_aligned_start)
    for name in (
        "steps_per_episode",
        "exact_eval_timeout_sec",
        "phase_count",
        "padding",
        "max_same_phase",
        "clock_mode",
    ):
        value = getattr(cli_args, name)
        if value is not None:
            settings[name] = value
    return SimpleNamespace(**settings)


def _finite_metric(value, fallback=1.0e12) -> float:
    value = float(value)
    return value if math.isfinite(value) else float(fallback)


def candidate_selection_key(candidate) -> tuple[float, ...]:
    """Rank legality first and routed area second, as promised by the GUI."""

    return (
        float(not bool(candidate["legal"])),
        _finite_metric(candidate["area"]),
        float(candidate["failed_edges"] + candidate["direction_violations"] + candidate["clock_violations"]),
        float(max(int(candidate["width"]), int(candidate["height"]))),
        _finite_metric(candidate["cost"]),
        float(candidate["field_index"]),
        float(candidate["trial_index"]),
    )


def route_policy_candidate(context, field, trial, runtime_args, field_index, trial_index):
    candidate = {
        "strategy": "universal-memory-agent",
        "orientation": str(context.env.orientation),
        "x_spacing": "n/a",
        "y_spacing": "n/a",
        "node_positions": clone_positions(trial["positions"]),
        "routing_embedding_guidance": False,
        "routing_edge_priorities": trial["route_edge_priorities"],
        "clock_field": field,
    }
    exact_started = time.perf_counter()
    result = evaluate_layout_candidate_with_timeout(
        candidate,
        context.circuit,
        runtime_args.phase_count,
        runtime_args.padding,
        runtime_args.max_same_phase,
        embedding_scores=None,
        timeout_sec=runtime_args.exact_eval_timeout_sec,
    )
    exact_runtime_sec = time.perf_counter() - exact_started
    clock_violations = int(count_clock_violations(result, field))
    failed_edges = int(len(result.get("failed_edges", ())))
    direction_violations = int(result.get("direction_violation_count", 0))
    legal = bool(is_legal_layout_result(result) and clock_violations == 0)
    cost = scalarize_layout_result(
        result,
        aspect_ratio_limit=runtime_args.aspect_ratio_limit,
        aspect_ratio_weight=runtime_args.aspect_ratio_weight,
        max_span_weight=runtime_args.max_span_weight,
        area_reference=float(context.warm_start["area"]),
        area_regression_weight=runtime_args.area_regression_weight,
    )
    return {
        "candidate_source": "policy",
        "legal": legal,
        "area": _finite_metric(result.get("area")),
        "width": int(result.get("width", 0)),
        "height": int(result.get("height", 0)),
        "cost": _finite_metric(cost),
        "failed_edges": failed_edges,
        "direction_violations": direction_violations,
        "clock_violations": clock_violations,
        "exact_runtime_sec": float(exact_runtime_sec),
        "exact_timed_out": bool(result.get("exact_evaluation_timeout", False)),
        "exact_error": result.get("exact_evaluation_error"),
        "field_index": int(field_index),
        "trial_index": int(trial_index),
        "field_hash": str(field.field_hash),
        "clock_spec": asdict(field.spec),
        "proxy_area": float(trial["proxy_area"]),
        "reward_total": float(trial["reward_total"]),
        "memory_entry_ids": list(trial["memory_entry_ids"]),
        "memory_exact_count": int(trial["memory_exact_count"]),
        "memory_best_similarity": float(trial["memory_best_similarity"]),
        "positions": {
            str(node_id): [int(coord[0]), int(coord[1])]
            for node_id, coord in sorted(trial["positions"].items())
        },
        "actions": trial["actions"],
        "_result": result,
    }


def _serializable_candidate(candidate):
    return {key: value for key, value in candidate.items() if not key.startswith("_")}


def write_json(path: str, payload):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    temporary = f"{path}.tmp.{os.getpid()}"
    with open(temporary, "w", encoding="utf-8") as output:
        json.dump(payload, output, ensure_ascii=False, indent=2, allow_nan=False)
    os.replace(temporary, path)


def emit_progress(stage: str, progress: float, message: str, **details):
    """Emit one machine-readable line while remaining useful in plain logs."""

    payload = {
        "stage": str(stage),
        "progress": float(max(0.0, min(1.0, progress))),
        "message": str(message),
        **details,
    }
    print(f"IFCN_PROGRESS {json.dumps(payload, ensure_ascii=False, separators=(',', ':'))}")


def validate_args(args):
    if not os.path.isfile(args.benchmark):
        raise FileNotFoundError(f"benchmark not found: {os.path.abspath(args.benchmark)}")
    for name in ("clock_field_samples", "policy_trials", "retrieval_top_k"):
        if int(getattr(args, name)) <= 0:
            raise ValueError(f"--{name.replace('_', '-')} must be positive")
    if args.steps_per_episode is not None and int(args.steps_per_episode) <= 0:
        raise ValueError("--steps-per-episode must be positive")
    if args.exact_eval_timeout_sec is not None and int(args.exact_eval_timeout_sec) <= 0:
        raise ValueError("--exact-eval-timeout-sec must be positive")


def main(argv=None):
    args = parse_args(argv)
    validate_args(args)
    started = time.perf_counter()
    benchmark = os.path.abspath(args.benchmark)
    output_dir = os.path.abspath(args.output_dir)
    os.makedirs(output_dir, exist_ok=True)
    checkpoint_path = resolve_checkpoint(args.checkpoint)
    emit_progress("loading", 0.02, "Loading universal agent checkpoint")

    set_global_seed(args.seed)
    rng = np.random.default_rng(args.seed)
    device = resolve_device(args.device)
    checkpoint_path, checkpoint, model = load_checkpoint(checkpoint_path, device)
    retrieval_memory, retrieval_memory_path = resolve_retrieval_memory(
        args,
        checkpoint_path,
        checkpoint,
    )
    runtime_args = build_runtime_args(args, checkpoint)
    context = prepare_context(benchmark, runtime_args)
    emit_progress("prepared", 0.15, "Circuit graph and retrieval memory are ready")

    effective_policy_trials = int(args.policy_trials)
    if bool(args.deterministic) and effective_policy_trials > 1:
        effective_policy_trials = 1
        print(
            "[Universal-GUI] deterministic actions make repeated policy trials "
            "identical; running one effective trial per field"
        )
    print(
        "[Universal-GUI] "
        f"benchmark={os.path.basename(benchmark)} device={device} "
        f"fields={args.clock_field_samples} trials={effective_policy_trials} "
        f"checkpoint={checkpoint_path}"
    )
    if retrieval_memory_path:
        print(
            "[Universal-GUI] retrieval memory "
            f"entries={len(retrieval_memory)} path={retrieval_memory_path}"
        )
    else:
        print("[Universal-GUI] retrieval memory unavailable; using recurrent policy only")

    candidates = []
    failures = []
    field_contexts = []
    completed_trials = 0
    total_policy_trials = int(args.clock_field_samples) * effective_policy_trials
    for field_index in range(int(args.clock_field_samples)):
        context.env.reset()
        field = sample_field(
            context.env.current_positions,
            context.env.orientation,
            runtime_args,
            rng,
        )
        if bool(args.clock_aligned_start):
            start_positions = clock_aligned_start_positions(context.env, field, runtime_args)
            field = sample_clock_field(
                field_bounds_for_positions(start_positions, runtime_args),
                field.spec,
            )
        else:
            start_positions = clone_positions(context.warm_start["node_positions"])
        frozen_memory = retrieve_policy_memory(
            retrieval_memory,
            context.env,
            field,
            top_k=int(args.retrieval_top_k),
            exclude_exact_topology=not bool(args.allow_exact_memory_retrieval),
        )
        field_contexts.append((field, clone_positions(start_positions), frozen_memory))

        for trial_index in range(effective_policy_trials):
            try:
                trial = _run_policy_trial(
                    context,
                    field,
                    model,
                    device,
                    runtime_args,
                    deterministic=bool(args.deterministic),
                    frozen_memory=frozen_memory,
                    start_positions=start_positions,
                )
                routed = route_policy_candidate(
                    context,
                    field,
                    trial,
                    runtime_args,
                    field_index,
                    trial_index,
                )
                candidates.append(routed)
                print(
                    "[Universal-GUI] candidate "
                    f"field={field_index + 1}/{args.clock_field_samples} "
                    f"trial={trial_index + 1}/{effective_policy_trials} "
                    f"legal={routed['legal']} failed={routed['failed_edges']} "
                    f"clock_violations={routed['clock_violations']} "
                    f"area={routed['area']:.0f} exact={routed['exact_runtime_sec']:.2f}s"
                )
            except Exception as exc:  # Keep other sampled fields usable in the GUI.
                failure = {
                    "field_index": int(field_index),
                    "trial_index": int(trial_index),
                    "error_type": type(exc).__name__,
                    "error": str(exc),
                }
                failures.append(failure)
                print(
                    "[Universal-GUI] candidate failed "
                    f"field={field_index + 1} trial={trial_index + 1}: "
                    f"{type(exc).__name__}: {exc}",
                    file=sys.stderr,
                )
            finally:
                completed_trials += 1
                emit_progress(
                    "routing",
                    0.15 + 0.65 * completed_trials / max(1, total_policy_trials),
                    "Evaluating stochastic-clock candidates",
                    completed=completed_trials,
                    total=total_policy_trials,
                )

    if not candidates:
        raise RuntimeError(f"all universal-agent candidates failed: {failures}")

    if not any(candidate["legal"] for candidate in candidates):
        print(
            "[Universal-GUI] no policy candidate routed legally; trying the "
            "initial placement as an exact fallback"
        )
        for field_index, (field, start_positions, frozen_memory) in enumerate(field_contexts):
            fallback_trial = {
                "positions": clone_positions(start_positions),
                "route_edge_priorities": None,
                "proxy_area": float(context.warm_start["area"]),
                "reward_total": 0.0,
                "memory_entry_ids": list(frozen_memory.entry_ids),
                "memory_exact_count": int(frozen_memory.exact_count),
                "memory_best_similarity": float(frozen_memory.best_similarity),
                "actions": [],
            }
            try:
                fallback = route_policy_candidate(
                    context,
                    field,
                    fallback_trial,
                    runtime_args,
                    field_index,
                    -1,
                )
                fallback["candidate_source"] = (
                    "clock-aligned-start-fallback"
                    if bool(args.clock_aligned_start)
                    else "warm-start-fallback"
                )
                candidates.append(fallback)
                if fallback["legal"]:
                    break
            except Exception as exc:
                failures.append(
                    {
                        "field_index": int(field_index),
                        "trial_index": -1,
                        "candidate_source": (
                            "clock-aligned-start-fallback"
                            if bool(args.clock_aligned_start)
                            else "warm-start-fallback"
                        ),
                        "error_type": type(exc).__name__,
                        "error": str(exc),
                    }
                )

    candidates.sort(key=candidate_selection_key)
    best = candidates[0]
    best_result = best["_result"]
    elapsed_sec = time.perf_counter() - started
    stem = os.path.splitext(os.path.basename(benchmark))[0]
    artifact_paths = {
        "ifcn": os.path.join(output_dir, f"{stem}_rl_layout.ifcn"),
        "svg": os.path.join(output_dir, f"{stem}_rl_layout.svg"),
        "tex": os.path.join(output_dir, f"{stem}_rl_layout.tex"),
    }
    if best["legal"] or not bool(args.require_legal):
        emit_progress("exporting", 0.92, "Exporting IFCN, SVG and TeX artifacts")
        export_layout_artifacts(
            context.circuit,
            context.ordered_layers,
            best_result,
            clone_positions(best_result["node_positions"]),
            output_dir,
            elapsed_sec,
            artifact_stem=stem,
            benchmark_label=os.path.basename(benchmark),
            phase_cycle=int(runtime_args.phase_count),
            layout_strategy="universal-memory-agent",
            algorithm_description=(
                "universal memory GCN+RL agent with strict stochastic-clock exact routing"
            ),
        )
    else:
        artifact_paths = {}
    summary = {
        "schema_version": 1,
        "status": (
            "ok"
            if best["legal"]
            else ("failed" if bool(args.require_legal) else "degraded")
        ),
        "engine": "universal-memory-agent",
        "benchmark": benchmark,
        "output_dir": output_dir,
        "checkpoint": checkpoint_path,
        "checkpoint_episode": int(checkpoint.get("episode", checkpoint.get("epoch", 0))),
        "retrieval_memory": retrieval_memory_path,
        "retrieval_memory_entries": int(len(retrieval_memory)) if retrieval_memory else 0,
        "seed": int(args.seed),
        "preprocess_seed": int(runtime_args.seed),
        "device": str(device),
        "deterministic_actions": bool(args.deterministic),
        "clock_mode": str(runtime_args.clock_mode),
        "phase_count": int(runtime_args.phase_count),
        "clock_aligned_start": bool(args.clock_aligned_start),
        "exact_memory_retrieval_allowed": bool(args.allow_exact_memory_retrieval),
        "clock_field_samples": int(args.clock_field_samples),
        "policy_trials_requested": int(args.policy_trials),
        "policy_trials_effective": int(effective_policy_trials),
        "candidate_count": int(len(candidates)),
        "candidate_failure_count": int(len(failures)),
        "candidate_failures": failures,
        "require_legal": bool(args.require_legal),
        "strict_success": bool(best["legal"]),
        "best_failed_edges": int(best["failed_edges"]),
        "best_direction_violation_count": int(best["direction_violations"]),
        "best_clock_violations": int(best["clock_violations"]),
        "best_area": float(best["area"]),
        "best_width": int(best["width"]),
        "best_height": int(best["height"]),
        "best_cost": float(best["cost"]),
        "best_field_index": int(best["field_index"]),
        "best_trial_index": int(best["trial_index"]),
        "best_field_hash": str(best["field_hash"]),
        "elapsed_sec": float(elapsed_sec),
        "best_candidate": _serializable_candidate(best),
        "candidates": [_serializable_candidate(candidate) for candidate in candidates],
        "artifacts": artifact_paths,
    }
    summary_path = os.path.join(output_dir, f"{stem}_rl_summary.json")
    write_json(summary_path, summary)
    print(
        "[Universal-GUI] selected "
        f"field={best['field_index'] + 1} trial={best['trial_index'] + 1} "
        f"legal={best['legal']} area={best['area']:.0f} cost={best['cost']:.1f}"
    )
    print(f"[Universal-GUI] summary={summary_path}")
    if bool(args.require_legal) and not best["legal"]:
        emit_progress("failed", 1.0, "No legal fully routed layout was found")
        raise SystemExit(2)
    emit_progress("completed", 1.0, "Universal layout is ready", summary=summary_path)
    return summary


if __name__ == "__main__":
    main()
