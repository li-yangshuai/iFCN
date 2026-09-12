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
    "normal graph draw algorithm based on Graphviz dot/mincross + exact-gain sifting, "
    "right-down reachable initialization, structural-feature-guided node reposition, "
    "failure-directed row/column expansion, legality-verified recursive "
    "top-down/bottom-up phase-aware node-to-wire contraction, fixed-point "
    "blank-row/column deletion, and "
    "post-routing "
    "2DDWave template consistency verification"
)


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
    declared_outputs = {
        name
        for output_line in output_lines
        for name in re.findall(r"[A-Za-z_][A-Za-z0-9_]*", output_line)
        if name != "output"
    }

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
        # A terminal expression must remain the physical output gate.  Keeping
        # an ``output = temporary`` alias makes the parser drop the output
        # tile during alias fusion, leaving a visually complete but electrically
        # unterminated cell layout.  Rename the just-created temporary instead.
        if lhs in declared_outputs and lowered_ref in temp_wires:
            temp_wires.remove(lowered_ref)
            name_pattern = re.compile(rf"\b{re.escape(lowered_ref)}\b")
            lowered_assigns[:] = [
                (name_pattern.sub(lhs, assign_lhs),
                 name_pattern.sub(lhs, assign_expr))
                for assign_lhs, assign_expr in lowered_assigns
            ]
        else:
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


def build_verified_parity_canonical_verilog(source_path, output_dir):
    """Return a parser-safe parity implementation when it is *proven* equal.

    Some supplied benchmarks spell out an XOR cone in an AND/OR/NOT form.
    Routing such a cone directly needlessly exposes every intermediate
    complement and fanout to placement.  For small, single-output,
    combinational sources we can safely recognise this one useful Boolean
    identity: exhaustively evaluate the source truth table, and only when the
    function is parity (or inverted parity) of all primary inputs, rebuild it
    as a balanced XOR expression before lowering it back to the same AOIG
    primitives understood by the mapper.  No rewrite is emitted unless every
    input assignment agrees, so this is a correctness-preserving optimisation,
    not a circuit-specific shortcut.
    """
    mode = os.environ.get("IFCN_PARITY_CANONICALIZATION", "auto").strip().lower()
    if mode not in {"auto", "off"}:
        raise ValueError(
            "IFCN_PARITY_CANONICALIZATION must be 'auto' or 'off'"
        )
    if mode == "off":
        return None

    with open(source_path, "r", encoding="utf-8") as source_file:
        lines = source_file.readlines()

    module_lines = []
    input_lines = []
    output_lines = []
    assignments = {}
    saw_endmodule = False
    for raw_line in lines:
        stripped = raw_line.strip()
        if not stripped or stripped.startswith("//"):
            continue
        if stripped.startswith("module "):
            module_lines.append(stripped)
        elif stripped.startswith("input "):
            input_lines.append(stripped)
        elif stripped.startswith("output "):
            output_lines.append(stripped)
        elif stripped.startswith("wire "):
            continue
        elif stripped.startswith("assign "):
            body = stripped[len("assign "):].rstrip(";").strip()
            if "=" not in body:
                return None
            lhs, expr = (part.strip() for part in body.split("=", 1))
            if not re.fullmatch(r"[A-Za-z_][A-Za-z0-9_]*", lhs):
                return None
            try:
                assignments[lhs] = ast.parse(expr, mode="eval").body
            except SyntaxError:
                return None
        elif stripped == "endmodule":
            saw_endmodule = True
        else:
            return None

    declaration_names = lambda declarations: [
        name
        for declaration in declarations
        for name in re.findall(r"[A-Za-z_][A-Za-z0-9_]*", declaration)
        if name not in {"input", "output", "wire", "reg"}
    ]
    input_names = declaration_names(input_lines)
    output_names = declaration_names(output_lines)
    if (
        not module_lines
        or not saw_endmodule
        or len(input_names) < 2
        or len(input_names) > 8
        or len(output_names) != 1
        or output_names[0] not in assignments
    ):
        return None

    output_name = output_names[0]

    def evaluate(node, values, visiting):
        if isinstance(node, ast.Name):
            if node.id in values:
                return values[node.id]
            if node.id not in assignments or node.id in visiting:
                raise ValueError("unsupported or cyclic combinational reference")
            visiting.add(node.id)
            value = evaluate(assignments[node.id], values, visiting)
            visiting.remove(node.id)
            values[node.id] = value
            return value
        if isinstance(node, ast.Constant) and isinstance(node.value, (int, float)):
            integer = int(node.value)
            if integer in {0, 1}:
                return integer
        if isinstance(node, ast.UnaryOp) and isinstance(node.op, ast.Invert):
            return 1 - evaluate(node.operand, values, visiting)
        if isinstance(node, ast.BinOp):
            lhs = evaluate(node.left, values, visiting)
            rhs = evaluate(node.right, values, visiting)
            if isinstance(node.op, ast.BitAnd):
                return lhs & rhs
            if isinstance(node.op, ast.BitOr):
                return lhs | rhs
            if isinstance(node.op, ast.BitXor):
                return lhs ^ rhs
        raise ValueError("unsupported Boolean expression")

    inverted_parity = None
    try:
        for assignment_mask in range(1 << len(input_names)):
            values = {
                name: (assignment_mask >> index) & 1
                for index, name in enumerate(input_names)
            }
            output_value = evaluate(assignments[output_name], values, set())
            parity = assignment_mask.bit_count() & 1
            is_inverted = output_value == (1 - parity)
            if inverted_parity is None:
                inverted_parity = is_inverted
            elif inverted_parity != is_inverted:
                return None
    except ValueError:
        return None

    def balanced_xor_expression(names):
        level = list(names)
        while len(level) > 1:
            next_level = []
            for index in range(0, len(level) - 1, 2):
                next_level.append(f"({level[index]} ^ {level[index + 1]})")
            if len(level) & 1:
                next_level.append(level[-1])
            level = next_level
        return level[0]

    parity_expr = balanced_xor_expression(input_names)
    if inverted_parity:
        parity_expr = f"~({parity_expr})"
    stem = os.path.splitext(os.path.basename(source_path))[0]
    canonical_path = os.path.join(output_dir, f"{stem}_parity_canonical.v")
    rendered_lines = (
        module_lines
        + input_lines
        + output_lines
        + [f"assign {output_name} = {parity_expr};", "endmodule"]
    )
    with open(canonical_path, "w", encoding="utf-8") as output_file:
        output_file.write("\n".join(rendered_lines) + "\n")
    return build_parser_safe_verilog(canonical_path, output_dir)


def load_draw_with_fallback(benchmark_path, output_dir, save_training_curve=True):
    parity_canonical_path = build_verified_parity_canonical_verilog(
        benchmark_path, output_dir
    )
    if parity_canonical_path is not None:
        print(
            "[Boolean] Exhaustively verified parity canonicalization: "
            f"{parity_canonical_path}"
        )
        draw = NormalGraphDraw(
            parity_canonical_path,
            save_training_curve=save_training_curve,
        )
        draw.parse.fileName = os.path.basename(benchmark_path)
        draw.parse.filePath = benchmark_path
        return draw, parity_canonical_path, True
    try:
        return NormalGraphDraw(
            benchmark_path,
            save_training_curve=save_training_curve,
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


def run_layout_trial(args, benchmark_path, output_dir, stage_tex_dir, seed, attempt_index):
    set_random_seed(seed)
    trial_stage_dir = os.path.join(stage_tex_dir, f"seed_{seed}") if args.seed_retries > 0 else stage_tex_dir
    start_time = time.perf_counter()
    draw, resolved_benchmark_path, parser_safe_generated = load_draw_with_fallback(
        benchmark_path,
        output_dir,
        save_training_curve=not args.skip_training_curve,
    )
    draw.route_expansion_rounds = max(1, int(args.max_expansion_rounds))
    draw.route_expansion_timeout_sec = max(1.0, float(args.route_expansion_timeout_sec))
    draw.template_expansion_rounds = max(1, int(args.template_expansion_rounds))
    failed_pairs = draw.one_step_optimization(
        verbose=args.verbose,
        route_repair_iters=args.route_repair_iters,
        phase_repair_iters=args.phase_repair_iters,
        global_place_iters=args.global_place_iters,
        compact_iters=args.compact_iters,
        snapshot_dir=trial_stage_dir,
    )
    run_time = time.perf_counter() - start_time
    contraction_runtime = max(
        0.0, float(getattr(draw, "contraction_runtime_sec", 0.0) or 0.0)
    )
    layout_routing_runtime = max(0.0, float(run_time) - contraction_runtime)

    width, height = draw.mapChessboard.computeLayoutArea()
    if width < 0 or height < 0:
        width, height = 0, 0
    draw.width = width
    draw.height = height
    draw.run_time_sec = run_time
    draw.layout_routing_runtime_sec = layout_routing_runtime
    draw.contraction_runtime_sec = contraction_runtime
    draw.algorithm_description = ALGO_DESC

    failed_count = len(failed_pairs or {})
    template_conflicts = int(getattr(draw, "clock_template_conflict_count", 0))
    template_ok = bool(getattr(draw, "clock_template_ok", False))
    score = (
        failed_count,
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
        "score": score,
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
        description="Run the Graphviz+sifting graph-draw placement and routing flow.",
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
    parser.add_argument("--compact-iters", type=int, default=64)
    parser.add_argument(
        "--max-expansion-rounds",
        type=int,
        default=int(os.environ.get("IFCN_ROUTE_EXPANSION_ROUNDS", "96")),
        help="Maximum failure-directed row/column expansion rounds per routing closure.",
    )
    parser.add_argument(
        "--route-expansion-timeout-sec",
        type=float,
        default=float(os.environ.get("IFCN_ROUTE_EXPANSION_TIMEOUT", "240")),
        help="Time budget for each route-until-success closure.",
    )
    parser.add_argument(
        "--template-expansion-rounds",
        type=int,
        default=int(os.environ.get("IFCN_TEMPLATE_EXPANSION_ROUNDS", "24")),
        help="Maximum 2DDWave template-conflict expansion rounds.",
    )
    parser.add_argument(
        "--seed",
        type=int,
        default=int(os.environ.get("IFCN_NORMAL_GRAPH_SEED", "1")),
        help="Random seed for downstream placement/routing retries.",
    )
    parser.add_argument(
        "--seed-retries",
        type=int,
        default=int(os.environ.get("IFCN_NORMAL_GRAPH_SEED_RETRIES", "0")),
        help="Extra sequential seeds (normally unnecessary for deterministic Graphviz+sifting).",
    )
    parser.add_argument("--skip-figures", action="store_true")
    parser.add_argument("--skip-latex", action="store_true")
    parser.add_argument("--skip-stage-snapshots", action="store_true")
    parser.add_argument("--skip-training-curve", action="store_true")
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

    stage_tex_dir = ""
    if not args.skip_stage_snapshots:
        stage_tex_dir = args.stage_tex_dir or os.path.join(output_dir, "stage_tex")
        stage_tex_dir = os.path.abspath(stage_tex_dir)

    trials = []
    max_attempts = 1 + max(0, int(args.seed_retries))
    for attempt_index in range(max_attempts):
        seed = int(args.seed) + attempt_index
        trial = run_layout_trial(
            args,
            benchmark_path,
            output_dir,
            stage_tex_dir,
            seed,
            attempt_index,
        )
        trials.append(trial)
        print(
            "[NORMAL-GRAPH] seed "
            f"{seed}: failed_edges={trial['failed_count']}, "
            f"template_ok={trial['template_ok']}, "
            f"area={int(trial['draw'].width) * int(trial['draw'].height)}"
        )
        if trial["failed_count"] == 0 and trial["template_ok"]:
            break

    best_trial = min(trials, key=lambda item: item["score"])
    draw = best_trial["draw"]
    failed_pairs = best_trial["failed_pairs"]
    resolved_benchmark_path = best_trial["resolved_benchmark_path"]
    parser_safe_generated = best_trial["parser_safe_generated"]
    run_time = best_trial["run_time"]
    width = draw.width
    height = draw.height

    if not args.skip_figures:
        draw.show_circuit_figure(dir_path=output_dir)
    if not args.skip_latex:
        draw.print_latex(output_dir)

    layout_legal = bool(
        not failed_pairs and getattr(draw, "clock_template_ok", False)
    )
    if layout_legal:
        ifcn_path = generate_gate_level_mapping_file(
            draw,
            output_dir=output_dir,
            filename_stem=f"{stem}_normal_graph_draw",
            verbose=True,
        )
    else:
        # A partially routed board is not a publishable cell-level layout.
        # Preserve the exact failure core in JSON/logs and avoid spending most
        # of a large-circuit timeout serializing misleading phase artifacts.
        ifcn_path = ""
        print(
            "[IFCN] Illegal layout artifacts skipped; failure core retained in summary."
        )
    encoded_ifcn_path = os.path.join(output_dir, f"{stem}_normal_graph_draw_encoded.ifcn")
    contraction_history = list(getattr(draw, "contraction_history", []))
    pre_contraction_width = (
        int(contraction_history[0]["old_width"])
        if contraction_history else int(width)
    )
    pre_contraction_height = (
        int(contraction_history[0]["old_height"])
        if contraction_history else int(height)
    )
    pre_contraction_area = pre_contraction_width * pre_contraction_height
    final_area = int(width) * int(height)
    final_usage_metrics = draw._current_phase_contraction_metrics()
    pre_contraction_used_cell_count = (
        int(contraction_history[0]["old_used_cell_count"])
        if contraction_history else int(final_usage_metrics["used_cell_count"])
    )
    pre_contraction_routed_wire_cells = (
        int(contraction_history[0]["old_routed_wire_cells"])
        if contraction_history else int(final_usage_metrics["routed_wire_cells"])
    )
    used_cell_count = int(final_usage_metrics["used_cell_count"])
    routed_wire_cells = int(final_usage_metrics["routed_wire_cells"])
    summary = {
        "benchmark": benchmark_path,
        "resolved_benchmark": resolved_benchmark_path,
        "parser_safe_generated": bool(parser_safe_generated),
        "node_count": int(draw.parse.effective_nodes_num),
        "edge_count": int(draw.parse.effective_edges_num),
        "input_count": int(draw.parse.InputNodesNum),
        "output_count": int(draw.parse.OutputNodesNum),
        "io_count": int(draw.parse.InputNodesNum + draw.parse.OutputNodesNum),
        "layer_count": int(draw.parse.total_layers),
        "seed": int(best_trial["seed"]),
        "seed_attempts": [
            {
                "seed": int(trial["seed"]),
                "failed_edge_count": int(trial["failed_count"]),
                "clock_template_ok": bool(trial["template_ok"]),
                "clock_template_conflicts": int(trial["template_conflicts"]),
                "area": int(trial["draw"].width) * int(trial["draw"].height),
                "route_expansion_rounds": len(
                    getattr(trial["draw"], "route_expansion_history", [])
                ),
                "route_expansion_exhausted": bool(
                    getattr(trial["draw"], "route_expansion_exhausted", False)
                ),
            }
            for trial in trials
        ],
        "output_dir": output_dir,
        "ifcn": os.path.abspath(ifcn_path) if ifcn_path else "",
        "encoded_ifcn": (
            os.path.abspath(encoded_ifcn_path)
            if layout_legal and os.path.isfile(encoded_ifcn_path)
            else ""
        ),
        "raw_svg": os.path.join(output_dir, f"{stem}_raw.svg"),
        "ordered_svg": os.path.join(output_dir, f"{stem}.svg"),
        "physical_svg": os.path.join(output_dir, f"{stem}_physical.svg"),
        "stage_tex_dir": best_trial["stage_tex_dir"],
        "failed_edge_count": len(failed_pairs or {}),
        "failed_edges": [
            {
                "src": int(src),
                "dst": int(dst),
                "direction": [int(direction[0]), int(direction[1])],
            }
            for (src, dst), direction in sorted((failed_pairs or {}).items())
        ],
        "layout_legal": layout_legal,
        "route_expansion_round_count": len(
            getattr(draw, "route_expansion_history", [])
        ),
        "route_expansion_exhausted": bool(
            getattr(draw, "route_expansion_exhausted", False)
        ),
        "route_incompatibility_reason": str(
            getattr(draw, "route_incompatibility_reason", "")
        ),
        "right_down_port_capacity_nodes": list(
            getattr(draw, "right_down_port_capacity_nodes", [])
        ),
        "route_expansion_history": list(
            getattr(draw, "route_expansion_history", [])
        ),
        "contraction_step_count": len(
            getattr(draw, "contraction_history", [])
        ),
        "contraction_evaluations": int(
            getattr(draw, "contraction_evaluations", 0)
        ),
        "contraction_global_evaluations": int(
            getattr(draw, "contraction_global_evaluations", 0)
        ),
        "contraction_recursive_evaluations": int(
            getattr(draw, "contraction_recursive_evaluations", 0)
        ),
        "contraction_empty_line_evaluations": int(
            getattr(draw, "contraction_empty_line_evaluations", 0)
        ),
        "contraction_layer_merge_evaluations": int(
            getattr(draw, "contraction_layer_merge_evaluations", 0)
        ),
        "contraction_exhausted": bool(
            getattr(draw, "contraction_exhausted", False)
        ),
        "contraction_history": contraction_history,
        "pre_contraction_width": pre_contraction_width,
        "pre_contraction_height": pre_contraction_height,
        "pre_contraction_area": pre_contraction_area,
        "contraction_area_reduction": pre_contraction_area - final_area,
        "contraction_area_reduction_percent": (
            100.0 * (pre_contraction_area - final_area) / pre_contraction_area
            if pre_contraction_area > 0 else 0.0
        ),
        "pre_contraction_used_cell_count": pre_contraction_used_cell_count,
        "used_cell_count": used_cell_count,
        "contraction_used_cell_reduction": (
            pre_contraction_used_cell_count - used_cell_count
        ),
        "contraction_used_cell_reduction_percent": (
            100.0
            * (pre_contraction_used_cell_count - used_cell_count)
            / pre_contraction_used_cell_count
            if pre_contraction_used_cell_count > 0 else 0.0
        ),
        "pre_contraction_routed_wire_cells": pre_contraction_routed_wire_cells,
        "routed_wire_cells": routed_wire_cells,
        "contraction_routed_wire_cell_reduction": (
            pre_contraction_routed_wire_cells - routed_wire_cells
        ),
        "width": int(width),
        "height": int(height),
        "area": final_area,
        "layout_routing_runtime_sec": float(
            getattr(draw, "layout_routing_runtime_sec", run_time)
        ),
        "contraction_runtime_sec": float(
            getattr(draw, "contraction_runtime_sec", 0.0)
        ),
        "run_time_sec": float(run_time),
        "algorithm_description": ALGO_DESC,
    }
    summary_path = os.path.join(output_dir, f"{stem}_normal_graph_draw_summary.json")
    with open(summary_path, "w", encoding="utf-8") as handle:
        json.dump(summary, handle, ensure_ascii=False, indent=2)

    print(
        "[NORMAL-GRAPH] IFCN: {}".format(
            os.path.abspath(ifcn_path) if ifcn_path else "not generated (illegal layout)"
        )
    )
    print(f"[NORMAL-GRAPH] Summary: {summary_path}")
    if stage_tex_dir:
        print(f"[NORMAL-GRAPH] Stage tex layouts saved in: {stage_tex_dir}")
    else:
        print("[NORMAL-GRAPH] Stage tex layouts skipped")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
