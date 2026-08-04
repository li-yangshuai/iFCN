#!/usr/bin/env python3
import argparse
import ast
import json
import os
import random
import re
import time

from utils import add_project_root

add_project_root()

MPLCONFIG_DIR = os.path.abspath(
    os.path.join(
        os.path.dirname(__file__),
        "../../../results/.matplotlib",
    )
)
os.environ.setdefault("MPLCONFIGDIR", MPLCONFIG_DIR)
os.environ.setdefault("MPLBACKEND", "Agg")

from src.normalGraphDraw import NormalGraphDraw
from src.toolkit import generate_gate_level_mapping_file

import numpy as np
import torch


ALGO_DESC = (
    "normal graph draw algorithm with OGDF Sugiyama fixed-layer crossing ordering, "
    "right-down reachable initialization, exclusive fanin and shared-fanout "
    "endpoint-port constraints, "
    "local straight orthogonal crossover legality, "
    "adaptive negotiated L/Z Manhattan or direct/DP monotone routing, "
    "structural/congestion-guided node reposition, "
    "iterative routing repair, greedy post-route compaction, and post-routing "
    "2DDWave template consistency verification"
)


def report_progress(value, detail):
    """Emit a compact, line-buffered progress record for the desktop UI."""
    value = max(0, min(100, int(value)))
    print(f"[IFCN-PROGRESS] {value}|{detail}", flush=True)


def build_parser_safe_verilog(source_path, output_dir):
    with open(source_path, "r", encoding="utf-8") as source_file:
        lines = source_file.readlines()

    module_lines = []
    input_lines = []
    output_lines = []
    wire_lines = []
    assign_lines = []
    other_lines = []
    saw_endmodule = False

    for raw_line in lines:
        stripped = raw_line.strip()
        if not stripped:
            continue
        if stripped.startswith("module "):
            module_lines.append(stripped)
        elif stripped.startswith("input "):
            input_lines.append(stripped)
        elif stripped.startswith("output "):
            output_lines.append(stripped)
        elif stripped.startswith("wire "):
            wire_lines.append(stripped)
        elif stripped.startswith("assign "):
            assign_lines.append(stripped)
        elif stripped == "endmodule":
            saw_endmodule = True
        else:
            other_lines.append(stripped)

    if not module_lines or not input_lines or not output_lines or not assign_lines or other_lines:
        raise ValueError("Unsupported Verilog shape for parser-safe fallback.")

    temp_wires = []
    temp_counter = [0]

    def new_temp():
        temp_counter[0] += 1
        wire_name = f"ps{temp_counter[0]}"
        temp_wires.append(wire_name)
        return wire_name

    def add_assign(lhs, expr, statements):
        statements.append((lhs, expr))

    def lower_expr(node, statements):
        if isinstance(node, ast.Name):
            return node.id
        if isinstance(node, ast.Constant) and isinstance(node.value, (int, float)):
            return str(int(node.value))
        if isinstance(node, ast.UnaryOp) and isinstance(node.op, ast.Invert):
            operand_ref = lower_expr(node.operand, statements)
            if operand_ref.startswith("~"):
                temp_name = new_temp()
                add_assign(temp_name, operand_ref, statements)
                return temp_name
            return f"~{operand_ref}"
        if isinstance(node, ast.BinOp):
            left_ref = lower_expr(node.left, statements)
            right_ref = lower_expr(node.right, statements)
            if isinstance(node.op, ast.BitAnd):
                temp_name = new_temp()
                add_assign(temp_name, f"{left_ref} & {right_ref}", statements)
                return temp_name
            if isinstance(node.op, ast.BitOr):
                temp_name = new_temp()
                add_assign(temp_name, f"{left_ref} | {right_ref}", statements)
                return temp_name
            if isinstance(node.op, ast.BitXor):
                or_name = new_temp()
                and_name = new_temp()
                not_name = new_temp()
                xor_name = new_temp()
                add_assign(or_name, f"{left_ref} | {right_ref}", statements)
                add_assign(and_name, f"{left_ref} & {right_ref}", statements)
                add_assign(not_name, f"~{and_name}", statements)
                add_assign(xor_name, f"{or_name} & {not_name}", statements)
                return xor_name
        raise ValueError(f"Unsupported expression in parser-safe fallback: {ast.dump(node)}")

    def topo_sort_assigns(assign_pairs):
        assign_order = [lhs for lhs, _ in assign_pairs]
        assign_expr = {lhs: expr for lhs, expr in assign_pairs}
        defined_names = set(assign_expr)
        dependents = {lhs: set() for lhs in assign_order}
        remaining_deps = {}

        for lhs, expr in assign_pairs:
            refs = set(re.findall(r"[A-Za-z_][A-Za-z0-9_]*", expr))
            refs.discard(lhs)
            deps = {ref for ref in refs if ref in defined_names}
            remaining_deps[lhs] = len(deps)
            for dep in deps:
                dependents.setdefault(dep, set()).add(lhs)

        ready = [lhs for lhs in assign_order if remaining_deps[lhs] == 0]
        sorted_pairs = []

        while ready:
            lhs = ready.pop(0)
            sorted_pairs.append((lhs, assign_expr[lhs]))
            for dependent in sorted(dependents.get(lhs, ())):
                remaining_deps[dependent] -= 1
                if remaining_deps[dependent] == 0:
                    ready.append(dependent)

        if len(sorted_pairs) != len(assign_pairs):
            return assign_pairs
        return sorted_pairs

    lowered_assigns = []
    for assign_line in assign_lines:
        body = assign_line[len("assign "):].rstrip(";").strip()
        lhs, expr = body.split("=", 1)
        lhs = lhs.strip()
        expr = expr.strip()
        parsed_expr = ast.parse(expr, mode="eval")
        lowered_ref = lower_expr(parsed_expr.body, lowered_assigns)
        add_assign(lhs, lowered_ref, lowered_assigns)

    lowered_assigns = topo_sort_assigns(lowered_assigns)
    lowered_assign_lines = [f"assign {lhs} = {expr};" for lhs, expr in lowered_assigns]

    output_lines_clean = output_lines[:]
    if temp_wires:
        output_lines_clean = output_lines_clean + [f"wire {', '.join(temp_wires)};"]

    stem = os.path.splitext(os.path.basename(source_path))[0]
    parser_safe_path = os.path.join(output_dir, f"{stem}_parser_safe.v")
    rendered_lines = module_lines + input_lines + output_lines_clean + wire_lines + lowered_assign_lines
    if saw_endmodule:
        rendered_lines.append("endmodule")
    with open(parser_safe_path, "w", encoding="utf-8") as output_file:
        output_file.write("\n".join(rendered_lines) + "\n")
    return parser_safe_path


def load_draw_with_fallback(
    benchmark_path,
    output_dir,
    save_training_curve=True,
    crossing_orderer="ogdf",
):
    try:
        return NormalGraphDraw(
            benchmark_path,
            save_training_curve=save_training_curve,
            crossing_orderer=crossing_orderer,
        ), benchmark_path, False
    except RuntimeError as exc:
        if "Invalid vertex name provided for edge creation." not in str(exc):
            raise
        parser_safe_path = build_parser_safe_verilog(benchmark_path, output_dir)
        print(
            "[Parser] Rewrote unsupported benchmark to parser-safe AOIG form: "
            f"{parser_safe_path}"
        )
        draw = NormalGraphDraw(
            parser_safe_path,
            save_training_curve=save_training_curve,
            crossing_orderer=crossing_orderer,
        )
        draw.parse.fileName = os.path.basename(benchmark_path)
        draw.parse.filePath = benchmark_path
        return draw, parser_safe_path, True


def set_random_seed(seed):
    seed = int(seed)
    random.seed(seed)
    np.random.seed(seed)
    torch.manual_seed(seed)
    if torch.cuda.is_available():
        torch.cuda.manual_seed_all(seed)
    if hasattr(torch.backends, "cudnn"):
        torch.backends.cudnn.benchmark = False
        torch.backends.cudnn.deterministic = True


def run_layout_trial(
    args,
    benchmark_path,
    output_dir,
    stage_tex_dir,
    seed,
    attempt_index,
    crossing_orderer=None,
    progress_points=(8, 32, 75),
):
    set_random_seed(seed)
    crossing_orderer = crossing_orderer or args.crossing_orderer
    trial_stage_dir = os.path.join(stage_tex_dir, f"seed_{seed}") if args.seed_retries > 0 else stage_tex_dir
    start_time = time.perf_counter()
    orderer_label = "OGDF Sugiyama" if crossing_orderer == "ogdf" else "GCN-guided"
    report_progress(
        progress_points[0],
        f"Parsing circuit and running {orderer_label} layer ordering (seed {seed})",
    )
    draw, resolved_benchmark_path, parser_safe_generated = load_draw_with_fallback(
        benchmark_path,
        output_dir,
        save_training_curve=not args.skip_training_curve,
        crossing_orderer=crossing_orderer,
    )
    draw.set_router_mode(args.router)
    report_progress(progress_points[1], "Crossing-aware ordering completed; placing and routing")
    failed_pairs = draw.one_step_optimization(
        verbose=args.verbose,
        route_repair_iters=args.route_repair_iters,
        phase_repair_iters=args.phase_repair_iters,
        global_place_iters=args.global_place_iters,
        compact_iters=args.compact_iters,
        snapshot_dir=trial_stage_dir,
    )
    report_progress(progress_points[2], "Placement, routing, compaction, and legality checks completed")
    run_time = time.perf_counter() - start_time

    width, height = draw.mapChessboard.computeLayoutArea()
    if width < 0 or height < 0:
        width, height = 0, 0
    draw.width = width
    draw.height = height
    draw.run_time_sec = run_time
    draw.algorithm_description = ALGO_DESC

    failed_count = len(failed_pairs or {})
    template_conflicts = int(getattr(draw, "clock_template_conflict_count", 0))
    template_ok = bool(getattr(draw, "clock_template_ok", False))
    port_violations = list(getattr(draw, "last_port_direction_violations", []))
    port_directions_ok = not port_violations
    score = (
        failed_count,
        0 if port_directions_ok else 1,
        len(port_violations),
        0 if template_ok else 1,
        template_conflicts,
        int(width) * int(height),
    )
    return {
        "draw": draw,
        "failed_pairs": failed_pairs,
        "resolved_benchmark_path": resolved_benchmark_path,
        "parser_safe_generated": parser_safe_generated,
        "stage_tex_dir": trial_stage_dir,
        "seed": int(seed),
        "attempt_index": int(attempt_index),
        "run_time": float(run_time),
        "failed_count": failed_count,
        "template_ok": template_ok,
        "template_conflicts": template_conflicts,
        "port_directions_ok": port_directions_ok,
        "port_direction_violations": len(port_violations),
        "score": score,
        "crossing_orderer": draw.crossing_orderer,
        "crossing_order_metrics": dict(draw.crossing_order_metrics),
        "router_mode": str(draw.router_mode),
        "router_mode_requested": str(draw.router_mode_requested),
        "routing_metrics": dict(getattr(draw, "last_negotiated_metrics", {})),
    }


def default_benchmark_path():
    candidate = os.path.abspath(
        os.path.join(
            os.path.dirname(__file__),
            "../../../benchmarks/TOY/mux21.v",
        )
    )
    return candidate if os.path.exists(candidate) else ""


def parse_args():
    parser = argparse.ArgumentParser(
        description="Run the non-neural Normal Graph placement and routing flow.",
    )
    parser.add_argument(
        "--benchmark",
        default=default_benchmark_path(),
        help="Path to the input Verilog benchmark.",
    )
    parser.add_argument(
        "--output-dir",
        default="",
        help="Directory for SVG/LaTeX/IFCN outputs.",
    )
    parser.add_argument(
        "--stage-tex-dir",
        default="",
        help="Directory for intermediate stage TeX snapshots. Defaults to <output-dir>/stage_tex.",
    )
    parser.add_argument("--route-repair-iters", type=int, default=3)
    parser.add_argument("--phase-repair-iters", type=int, default=2)
    parser.add_argument("--global-place-iters", type=int, default=5)
    parser.add_argument("--compact-iters", type=int, default=8)
    parser.add_argument(
        "--crossing-orderer",
        choices=("ogdf", "gcn"),
        default=os.environ.get("IFCN_NORMAL_CROSSING_ORDERER", "ogdf").strip().lower(),
        help="Within-layer crossing minimizer. The desktop Normal Graph flow uses OGDF.",
    )
    parser.add_argument(
        "--router",
        choices=("auto", "legacy", "negotiated"),
        default=os.environ.get("IFCN_NORMAL_ROUTER", "auto").strip().lower(),
        help=(
            "Routing backend. Auto uses negotiated Manhattan routing through 128 edges "
            "and the regression-proven direct/DP backend above that threshold."
        ),
    )
    parser.add_argument(
        "--seed",
        type=int,
        default=int(os.environ.get("IFCN_NORMAL_GRAPH_SEED", "1")),
        help="Random seed for deterministic placement and repair tie-breaking.",
    )
    parser.add_argument(
        "--seed-retries",
        type=int,
        default=int(os.environ.get("IFCN_NORMAL_GRAPH_SEED_RETRIES", "4")),
        help="Extra sequential seeds to try if a legal 2DDWave layout is not found.",
    )
    parser.add_argument("--skip-figures", action="store_true")
    parser.add_argument("--skip-latex", action="store_true")
    parser.add_argument("--skip-stage-snapshots", action="store_true")
    parser.add_argument("--skip-training-curve", action="store_true")
    parser.add_argument(
        "--order-only",
        action="store_true",
        help=(
            "Run only parsing and fixed-layer crossing optimization, then emit "
            "the native raw/ordered SVG pair without requiring a legal 2DDWave route."
        ),
    )
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()
    if not args.benchmark:
        parser.error("--benchmark is required")
    return args


def main():
    args = parse_args()
    benchmark_path = os.path.abspath(args.benchmark)
    if not os.path.isfile(benchmark_path):
        raise FileNotFoundError(f"Benchmark not found: {benchmark_path}")

    stem = os.path.splitext(os.path.basename(benchmark_path))[0]
    output_dir = args.output_dir or os.path.join(
        os.path.dirname(benchmark_path),
        f"{stem}_normal_graph_draw",
    )
    output_dir = os.path.abspath(output_dir)
    os.makedirs(output_dir, exist_ok=True)
    report_progress(3, "Preparing fixed-clock 2DDWave run")

    stage_tex_dir = ""
    if not args.skip_stage_snapshots:
        stage_tex_dir = args.stage_tex_dir or os.path.join(output_dir, "stage_tex")
        stage_tex_dir = os.path.abspath(stage_tex_dir)

    if args.order_only:
        set_random_seed(args.seed)
        draw, resolved_benchmark_path, parser_safe_generated = load_draw_with_fallback(
            benchmark_path,
            output_dir,
            save_training_curve=False,
            crossing_orderer=args.crossing_orderer,
        )
        report_progress(65, "Crossing optimization complete; writing native raw/ordered SVG")
        draw.show_circuit_figure(
            dir_path=output_dir,
            include_raw_layout=True,
            include_physical_layout=False,
        )
        summary = {
            "benchmark": benchmark_path,
            "resolved_benchmark": resolved_benchmark_path,
            "parser_safe_generated": bool(parser_safe_generated),
            "mode": "order-only",
            "crossing_orderer_requested": args.crossing_orderer,
            "crossing_orderer": draw.crossing_orderer,
            "crossing_order_metrics": dict(draw.crossing_order_metrics),
            "raw_svg": os.path.join(output_dir, f"{stem}_raw.svg"),
            "ordered_svg": os.path.join(output_dir, f"{stem}.svg"),
        }
        summary_path = os.path.join(output_dir, f"{stem}_order_only_summary.json")
        with open(summary_path, "w", encoding="utf-8") as summary_file:
            json.dump(summary, summary_file, indent=2)
        report_progress(100, "Native raw/ordered SVG export complete")
        print(json.dumps(summary, indent=2))
        return 0

    trials = []
    max_attempts = 1 + max(0, int(args.seed_retries))
    for attempt_index in range(max_attempts):
        seed = int(args.seed) + attempt_index
        if attempt_index == 0:
            progress_points = (8, 32, 75)
        else:
            retry_start = min(87, 75 + 3 * attempt_index)
            progress_points = (
                retry_start,
                min(89, retry_start + 1),
                min(90, retry_start + 2),
            )
        candidate_orderers = (args.crossing_orderer,)
        seed_trials = []
        for candidate_orderer in candidate_orderers:
            trial = run_layout_trial(
                args,
                benchmark_path,
                output_dir,
                stage_tex_dir,
                seed,
                attempt_index,
                crossing_orderer=candidate_orderer,
                progress_points=progress_points,
            )
            trials.append(trial)
            seed_trials.append(trial)
            print(
                "[NORMAL-GRAPH] seed "
                f"{seed}, orderer={candidate_orderer}: "
                f"failed_edges={trial['failed_count']}, "
                f"ports_ok={trial['port_directions_ok']}, "
                f"template_ok={trial['template_ok']}, "
                f"area={int(trial['draw'].width) * int(trial['draw'].height)}"
            )
        if any(
            trial["failed_count"] == 0
            and trial["port_directions_ok"]
            and trial["template_ok"]
            for trial in seed_trials
        ):
            break

    best_trial = min(trials, key=lambda item: item["score"])
    draw = best_trial["draw"]
    failed_pairs = best_trial["failed_pairs"]
    resolved_benchmark_path = best_trial["resolved_benchmark_path"]
    parser_safe_generated = best_trial["parser_safe_generated"]
    run_time = best_trial["run_time"]
    width = draw.width
    height = draw.height
    report_progress(91, "Selecting the best strictly legal layout")

    # Export is a hard legality boundary.  A best-effort trial is useful for
    # diagnostics, but it must never become a device-level layout artifact.
    final_ports_ok, final_port_violations, _ = draw.validate_gate_port_directions()
    final_overlaps_ok, final_overlap_conflicts = draw.validate_route_overlap_legality()
    final_template_ok, _ = draw.verify_clock_template_consistency(verbose=False)
    if (
        failed_pairs
        or not final_ports_ok
        or not final_overlaps_ok
        or not final_template_ok
    ):
        raise RuntimeError(
            "No strictly legal 2DDWave layout was found after "
            f"{len(trials)} seed attempt(s): failed_edges={len(failed_pairs or {})}, "
            f"port_violations={len(final_port_violations)}, "
            f"overlap_conflicts={len(final_overlap_conflicts)}, "
            f"template_ok={bool(final_template_ok)}. Refusing to export invalid IFCN/LaTeX artifacts."
        )

    if not args.skip_figures:
        report_progress(93, "Rendering layout figures")
        draw.show_circuit_figure(dir_path=output_dir)
    if not args.skip_latex:
        report_progress(95, "Writing full-layout phase LaTeX")
        draw.print_latex(output_dir)

    report_progress(97, "Writing gate-level IFCN artifacts")
    ifcn_path = generate_gate_level_mapping_file(
        draw,
        output_dir=output_dir,
        filename_stem=f"{stem}_normal_graph_draw",
        verbose=True,
    )
    encoded_ifcn_path = os.path.join(output_dir, f"{stem}_normal_graph_draw_encoded.ifcn")
    summary = {
        "benchmark": benchmark_path,
        "resolved_benchmark": resolved_benchmark_path,
        "parser_safe_generated": bool(parser_safe_generated),
        "seed": int(best_trial["seed"]),
        "crossing_orderer_requested": args.crossing_orderer,
        "crossing_orderer": best_trial["crossing_orderer"],
        "crossing_order_metrics": best_trial["crossing_order_metrics"],
        "router_mode": best_trial["router_mode"],
        "router_mode_requested": best_trial["router_mode_requested"],
        "routing_metrics": best_trial["routing_metrics"],
        "seed_attempts": [
            {
                "seed": int(trial["seed"]),
                "failed_edge_count": int(trial["failed_count"]),
                "clock_template_ok": bool(trial["template_ok"]),
                "clock_template_conflicts": int(trial["template_conflicts"]),
                "port_directions_ok": bool(trial["port_directions_ok"]),
                "port_direction_violations": int(trial["port_direction_violations"]),
                "crossing_orderer": trial["crossing_orderer"],
                "crossing_order_metrics": trial["crossing_order_metrics"],
                "router_mode": trial["router_mode"],
                "router_mode_requested": trial["router_mode_requested"],
                "routing_metrics": trial["routing_metrics"],
                "area": int(trial["draw"].width) * int(trial["draw"].height),
            }
            for trial in trials
        ],
        "output_dir": output_dir,
        "ifcn": os.path.abspath(ifcn_path),
        "encoded_ifcn": os.path.abspath(encoded_ifcn_path) if os.path.isfile(encoded_ifcn_path) else "",
        "raw_svg": os.path.join(output_dir, f"{stem}_raw.svg"),
        "ordered_svg": os.path.join(output_dir, f"{stem}.svg"),
        "physical_svg": os.path.join(output_dir, f"{stem}_physical.svg"),
        "stage_tex_dir": best_trial["stage_tex_dir"],
        "failed_edge_count": len(failed_pairs or {}),
        "port_directions_ok": bool(best_trial["port_directions_ok"]),
        "port_direction_violations": int(best_trial["port_direction_violations"]),
        "width": int(width),
        "height": int(height),
        "area": int(width) * int(height),
        "run_time_sec": float(run_time),
        "algorithm_description": ALGO_DESC,
    }
    summary_path = os.path.join(output_dir, f"{stem}_normal_graph_draw_summary.json")
    with open(summary_path, "w", encoding="utf-8") as handle:
        json.dump(summary, handle, ensure_ascii=False, indent=2)

    print(f"[NORMAL-GRAPH] IFCN: {os.path.abspath(ifcn_path)}")
    print(f"[NORMAL-GRAPH] Summary: {summary_path}")
    if stage_tex_dir:
        print(f"[NORMAL-GRAPH] Stage tex layouts saved in: {stage_tex_dir}")
    else:
        print("[NORMAL-GRAPH] Stage tex layouts skipped")
    report_progress(100, "Fixed-clock 2DDWave layout is ready")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
