import argparse
from collections import Counter, defaultdict
import hashlib
import heapq
import json
import os
import pickle
import time
from statistics import mean
import numpy as np
import torch

from utils import add_project_root

add_project_root()

MPLCONFIG_DIR = os.path.abspath(
    os.path.join(
        os.path.dirname(__file__),
        "../../../results/.matplotlib",
    )
)
os.environ.setdefault("MPLCONFIGDIR", MPLCONFIG_DIR)

from lib import iFCN_Lab
from src.circuit_parse import CircuitParser
from src.gcn_model_less_node import normal_generate, visualize_layered_graph_sorted
from src.stochastic_clock import ClockField, install_phase_field
from src.toolkit import generate_gate_level_mapping_file


SEED = 7
X_SPACING = 4
Y_SPACING = 6
OUTPUT_DIR = os.path.abspath(
    os.path.join(
        os.path.dirname(__file__),
        "../../../results/random_phase_layout",
    )
)
LAYOUT_MEMORY_DIR = os.path.abspath(
    os.path.join(
        os.path.dirname(__file__),
        "../../../results/layout_memory",
    )
)
GCN_CACHE_DIR = os.path.join(LAYOUT_MEMORY_DIR, "gcn_cache")
GCN_CACHE_SCHEMA_VERSION = 3
ALGORITHM_DESCRIPTION = (
    "GCN layer ordering + orientation-aware adaptive compact layout search + "
    "GCN-guided local compaction + phase-aware monotone routing with edge-exposed ports"
)
DEFAULT_PHASE_CYCLE = 4
DEFAULT_PADDING = 2
DEFAULT_MAX_SAME_PHASE = 4
DEFAULT_LOCAL_LOOKAHEAD_DEPTH = 2
DEFAULT_LOCAL_BEAM_WIDTH = 12
DEFAULT_LOCAL_BRANCH_WIDTH = 10
TOP_DOWN = "top-down"
LEFT_RIGHT = "left-right"
ALL_DIRECTIONS = ((1, 0), (-1, 0), (0, 1), (0, -1))
DEFAULT_ROUTE_ORDER_VARIANTS = max(1, int(os.environ.get("IFCN_GCN_RL_ROUTE_ORDER_VARIANTS", "6")))
DEFAULT_GENERIC_ROUTER_COMBO_LIMIT = max(
    0,
    int(os.environ.get("IFCN_GCN_RL_GENERIC_ROUTER_COMBO_LIMIT", "0")),
)
DEFAULT_ALLOW_ESCAPE_ROUTING = os.environ.get("IFCN_GCN_RL_ALLOW_ESCAPE_ROUTING", "0") != "0"
DEFAULT_FLEXIBLE_ROUTER_EXPANSION_LIMIT = max(
    0,
    int(os.environ.get("IFCN_GCN_RL_FLEX_EXPANSION_LIMIT", "8000")),
)
DEFAULT_MONOTONE_ROUTER_EXPANSION_LIMIT = max(
    0,
    int(os.environ.get("IFCN_GCN_RL_MONOTONE_EXPANSION_LIMIT", "8000")),
)
DEFAULT_REPAIR_ORDER_ATTEMPTS = max(
    0,
    int(os.environ.get("IFCN_GCN_RL_REPAIR_ORDER_ATTEMPTS", "4")),
)
DEFAULT_ALTERNATE_PHASE_POLICY_ORDERS = max(
    0,
    int(os.environ.get("IFCN_GCN_RL_ALT_PHASE_POLICY_ORDERS", "3")),
)
DEFAULT_LOCAL_RIPUP_ATTEMPTS = max(
    0,
    int(os.environ.get("IFCN_GCN_RL_LOCAL_RIPUP_ATTEMPTS", "6")),
)


def default_benchmark_path():
    return ""


def parse_args():
    parser = argparse.ArgumentParser(
        description="Run placement and A* phase-aware routing on a benchmark circuit.",
    )
    parser.add_argument(
        "--benchmark",
        default=default_benchmark_path(),
        help="Path to the input Verilog benchmark.",
    )
    parser.add_argument(
        "--output-dir",
        default=OUTPUT_DIR,
        help="Directory for SVG/LaTeX/IFCN outputs.",
    )
    parser.add_argument("--seed", type=int, default=SEED)
    parser.add_argument("--x-spacing", type=int, default=X_SPACING)
    parser.add_argument("--y-spacing", type=int, default=Y_SPACING)
    parser.add_argument(
        "--layout-strategy",
        default="auto",
        choices=("auto", "fixed", "shifted", "adaptive", "gcn"),
        help="Layout candidate family to use. 'auto' evaluates all strategies.",
    )
    parser.add_argument(
        "--layout-orientation",
        default="auto",
        choices=("auto", LEFT_RIGHT, TOP_DOWN),
        help="Preferred layout orientation. 'auto' evaluates both orientations.",
    )
    parser.add_argument(
        "--parse-mode",
        default="auto",
        choices=("auto", "compact", "layered"),
        help=(
            "Circuit parsing strategy. compact minimizes node count; layered keeps layer "
            "redundancy to avoid cross-layer routes; auto uses layered for larger circuits."
        ),
    )
    parser.add_argument("--phase-cycle", type=int, default=DEFAULT_PHASE_CYCLE)
    parser.add_argument("--padding", type=int, default=DEFAULT_PADDING)
    parser.add_argument("--max-same-phase", type=int, default=DEFAULT_MAX_SAME_PHASE)
    parser.add_argument(
        "--board-margin",
        type=int,
        default=None,
        help="Blank chessboard margin before the first placed node. Defaults to padding + 1.",
    )
    parser.add_argument(
        "--local-refine-rounds",
        type=int,
        default=8,
        help="Greedy local compaction steps applied after the best base layout is selected. Set to 0 to disable.",
    )
    parser.add_argument(
        "--local-lookahead-depth",
        type=int,
        default=DEFAULT_LOCAL_LOOKAHEAD_DEPTH,
        help="Local compaction lookahead depth. Use 1 for pure greedy search, 2 to allow paired moves.",
    )
    parser.add_argument(
        "--local-beam-width",
        type=int,
        default=DEFAULT_LOCAL_BEAM_WIDTH,
        help="How many intermediate states to keep when the local compactor does lookahead.",
    )
    parser.add_argument(
        "--local-branch-width",
        type=int,
        default=DEFAULT_LOCAL_BRANCH_WIDTH,
        help="How many second-step actions to explore from each retained intermediate state.",
    )
    parser.add_argument(
        "--local-max-evaluations",
        type=int,
        default=0,
        help="Maximum local-compaction candidate evaluations. Use 0 to disable the budget.",
    )
    parser.add_argument(
        "--disable-gcn-cache",
        action="store_true",
        help="Disable on-disk GCN embedding/order cache and retrain every run.",
    )
    parser.add_argument(
        "--disable-layout-memory",
        action="store_true",
        help="Disable reading/writing the layout memory candidate for this benchmark.",
    )
    return parser.parse_args()


def set_global_seed(seed):
    np.random.seed(seed)
    torch.manual_seed(seed)
    if torch.cuda.is_available():
        torch.cuda.manual_seed_all(seed)


def build_benchmark_cache_key(benchmark_path, seed):
    benchmark_path = os.path.abspath(benchmark_path)
    stat = os.stat(benchmark_path)
    payload = json.dumps(
        {
            "cache_schema_version": GCN_CACHE_SCHEMA_VERSION,
            "benchmark_path": benchmark_path,
            "seed": int(seed),
            "size": int(stat.st_size),
            "mtime_ns": int(stat.st_mtime_ns),
        },
        sort_keys=True,
        ensure_ascii=True,
    )
    return hashlib.sha1(payload.encode("utf-8")).hexdigest()[:16]


def get_gcn_cache_path(benchmark_path, seed):
    stem = os.path.splitext(os.path.basename(benchmark_path))[0]
    cache_key = build_benchmark_cache_key(benchmark_path, seed)
    return os.path.join(GCN_CACHE_DIR, f"{stem}_{cache_key}.pkl")


def get_layout_memory_path(benchmark_path, seed):
    stem = os.path.splitext(os.path.basename(benchmark_path))[0]
    cache_key = build_benchmark_cache_key(benchmark_path, seed)
    return os.path.join(LAYOUT_MEMORY_DIR, f"{stem}_{cache_key}.json")


def flattened_layer_nodes(layer_container):
    return sorted(int(node_id) for layer in normalize_layers(layer_container) for node_id in layer)


def load_or_generate_gcn_layout(
    circuit,
    benchmark_path,
    seed,
    use_cache=True,
    device="auto",
):
    benchmark_path = os.path.abspath(benchmark_path)
    cache_path = get_gcn_cache_path(benchmark_path, seed)
    if use_cache and os.path.exists(cache_path):
        try:
            with open(cache_path, "rb") as cache_file:
                cached = pickle.load(cache_file)
            cached_layers = normalize_layers(cached["barycenter_opt_layers"])
            cached_embeddings = np.asarray(cached["embeddings"], dtype=float)
            expected_nodes = sorted(int(node_id) for node_id in circuit.effective_nodes)
            cached_nodes = flattened_layer_nodes(cached_layers)
            if cached_nodes != expected_nodes or cached_embeddings.shape[0] != len(expected_nodes):
                raise ValueError(
                    "cached graph does not match current parser output "
                    f"(cached_nodes={len(cached_nodes)}, expected_nodes={len(expected_nodes)})"
                )
            print(f"[GCN] Loaded cached embeddings/order from: {cache_path}")
            return (
                cached_embeddings,
                cached_layers,
                {int(layer): int(count) for layer, count in cached["crossings_per_layer"].items()},
                list(cached.get("edges", circuit.effective_edges)),
            )
        except (OSError, EOFError, pickle.PickleError, ValueError, KeyError, TypeError) as exc:
            print(f"[GCN] Ignoring broken cache {cache_path}: {exc}")

    if not circuit.effective_edges:
        ordered_layers = normalize_layers(circuit.layer_nodes)
        embeddings = np.zeros((len(circuit.effective_nodes), 2), dtype=float)
        for layer_idx, layer in enumerate(ordered_layers):
            for rank, node_id in enumerate(layer):
                node_index = circuit.node_to_index.get(int(node_id))
                if node_index is None:
                    continue
                embeddings[node_index, 0] = float(layer_idx)
                embeddings[node_index, 1] = float(rank)
        crossings_per_layer = {
            int(layer_idx): 0 for layer_idx in range(max(0, len(ordered_layers) - 1))
        }
        edges = []
        print("[GCN] No effective edges detected; using deterministic layer-order fallback.")
        if use_cache:
            os.makedirs(GCN_CACHE_DIR, exist_ok=True)
            with open(cache_path, "wb") as cache_file:
                pickle.dump(
                    {
                        "benchmark_path": benchmark_path,
                        "seed": int(seed),
                        "parse_mode_requested": getattr(circuit, "parse_mode_requested", "auto"),
                        "parse_mode_resolved": getattr(circuit, "parse_mode_resolved", "compact"),
                        "parse_cache_key": getattr(circuit, "parse_cache_key", ""),
                        "embeddings": embeddings,
                        "barycenter_opt_layers": ordered_layers,
                        "crossings_per_layer": crossings_per_layer,
                        "edges": edges,
                    },
                    cache_file,
                )
            print(f"[GCN] Cached zero-edge fallback written to: {cache_path}")
        return embeddings, ordered_layers, crossings_per_layer, edges

    data = circuit.build_pyg_data()
    embeddings, barycenter_opt_layers, crossings_per_layer, edges = normal_generate(
        data,
        circuit.layer_nodes,
        circuit.effective_edges,
        circuit.node_to_index,
        circuit.filePath,
        device=device,
    )

    if use_cache:
        os.makedirs(GCN_CACHE_DIR, exist_ok=True)
        with open(cache_path, "wb") as cache_file:
            pickle.dump(
                    {
                        "benchmark_path": benchmark_path,
                        "seed": int(seed),
                        "parse_mode_requested": getattr(circuit, "parse_mode_requested", "auto"),
                        "parse_mode_resolved": getattr(circuit, "parse_mode_resolved", "compact"),
                        "parse_cache_key": getattr(circuit, "parse_cache_key", ""),
                        "embeddings": np.asarray(embeddings, dtype=float),
                        "barycenter_opt_layers": normalize_layers(barycenter_opt_layers),
                        "crossings_per_layer": dict(crossings_per_layer),
                    "edges": list(edges),
                },
                cache_file,
            )
        print(f"[GCN] Cached embeddings/order written to: {cache_path}")

    return embeddings, normalize_layers(barycenter_opt_layers), crossings_per_layer, edges


def _layout_memory_candidate_from_payload(circuit, payload, memory_path):
    node_positions = {}
    for item in payload.get("node_positions", []):
        node_id = int(item["node_id"])
        node_positions[node_id] = (int(item["x"]), int(item["y"]))

    placed_nodes = set(node_positions.keys())
    required_nodes = {
        int(node_id)
        for layer in normalize_layers(circuit.layer_nodes)
        for node_id in layer
    }
    if not node_positions or placed_nodes != required_nodes:
        return None

    return {
        "strategy": f"memory:{payload.get('layout_strategy', 'unknown')}",
        "orientation": payload["layout_orientation"],
        "x_spacing": payload.get("x_spacing", "n/a"),
        "y_spacing": payload.get("y_spacing", "n/a"),
        "node_positions": node_positions,
        "routing_embedding_guidance": bool(payload.get("routing_embedding_guidance", False)),
        "memory_path": memory_path,
        "memory_area": float(payload.get("area", 0.0)),
        "memory_width": int(payload.get("width", 0)),
        "memory_height": int(payload.get("height", 0)),
        "memory_seed": int(payload.get("seed", -1)),
    }


def load_layout_memory_candidates(circuit, benchmark_path, seed):
    benchmark_path = os.path.abspath(benchmark_path)
    memory_path = get_layout_memory_path(benchmark_path, seed)
    candidates = []
    seen_paths = set()

    def add_candidate(candidate_path, prefer_seed=False):
        candidate_path = os.path.abspath(candidate_path)
        if candidate_path in seen_paths or not os.path.exists(candidate_path):
            return
        seen_paths.add(candidate_path)
        try:
            with open(candidate_path, "r", encoding="utf-8") as memory_file:
                payload = json.load(memory_file)
        except (OSError, json.JSONDecodeError):
            return
        if os.path.abspath(payload.get("benchmark_path", "")) != benchmark_path:
            return
        if int(payload.get("failed_edge_count", 0)) > 0:
            if prefer_seed:
                print(f"[Memory] Ignoring non-legal stored layout candidate: {candidate_path}")
            return
        candidate = _layout_memory_candidate_from_payload(circuit, payload, candidate_path)
        if candidate is None:
            return
        area = float(payload.get("area", candidate.get("memory_area", 0.0)))
        width = int(payload.get("width", candidate.get("memory_width", 0)))
        height = int(payload.get("height", candidate.get("memory_height", 0)))
        seed_value = int(payload.get("seed", 0))
        candidate["_memory_sort_key"] = (
            0 if prefer_seed else 1,
            area,
            max(width, height),
            width + height,
            seed_value,
        )
        candidates.append(candidate)

    add_candidate(memory_path, prefer_seed=True)
    stem = os.path.splitext(os.path.basename(benchmark_path))[0]
    if os.path.isdir(LAYOUT_MEMORY_DIR):
        for filename in sorted(os.listdir(LAYOUT_MEMORY_DIR)):
            if not filename.startswith(f"{stem}_") or not filename.endswith(".json"):
                continue
            add_candidate(os.path.join(LAYOUT_MEMORY_DIR, filename))

    candidates.sort(key=lambda item: item.get("_memory_sort_key", (9, 0, 0, 0, 0)))
    return candidates


def load_layout_memory_candidate(circuit, benchmark_path, seed):
    candidates = load_layout_memory_candidates(circuit, benchmark_path, seed)
    if candidates:
        candidate = candidates[0]
        message = "[Memory] Loaded stored layout candidate from: "
        if int(candidate.get("memory_seed", -1)) != int(seed):
            message = "[Memory] Loaded best compatible stored layout candidate from: "
        print(
            message
            + f"{candidate['memory_path']} "
            + f"area={candidate['memory_area']:.1f} "
            + f"seed={candidate['memory_seed']}"
        )
        return candidate
    return None


def save_layout_memory(result, benchmark_path, seed, allow_failed=False):
    memory_path = get_layout_memory_path(benchmark_path, seed)
    os.makedirs(LAYOUT_MEMORY_DIR, exist_ok=True)
    if result is None:
        print(f"[Memory] Skip saving empty layout candidate: {memory_path}")
        return None
    failed_edge_count = int(len(result.get("failed_edges", [])))
    if failed_edge_count > 0 and not allow_failed:
        print(
            "[Memory] Skip saving non-legal layout candidate: "
            f"{memory_path} failed_edges={failed_edge_count}"
        )
        return None
    layout_strategy = str(result["layout_strategy"])
    while layout_strategy.startswith("memory:"):
        layout_strategy = layout_strategy[len("memory:") :]
    node_positions = [
        {
            "node_id": int(node_id),
            "x": int(coord[0]),
            "y": int(coord[1]),
        }
        for node_id, coord in sorted(result["node_positions"].items())
    ]
    payload = {
        "benchmark_path": os.path.abspath(benchmark_path),
        "seed": int(seed),
        "parse_mode_requested": getattr(result.get("circuit"), "parse_mode_requested", "auto"),
        "parse_mode_resolved": getattr(result.get("circuit"), "parse_mode_resolved", "compact"),
        "parse_cache_key": getattr(result.get("circuit"), "parse_cache_key", ""),
        "layout_strategy": layout_strategy,
        "layout_orientation": result["layout_orientation"],
        "x_spacing": result["x_spacing"],
        "y_spacing": result["y_spacing"],
        "width": int(result["width"]),
        "height": int(result["height"]),
        "area": float(result["area"]),
        "failed_edge_count": failed_edge_count,
        "direction_violation_count": int(result["direction_violation_count"]),
        "route_overhang_penalty": int(result["route_overhang_penalty"]),
        "io_exposure_penalty": int(result["io_exposure_penalty"]),
        "routing_embedding_guidance": bool(result.get("routing_embedding_guidance", False)),
        "node_positions": node_positions,
        "updated_at": int(time.time()),
    }
    with open(memory_path, "w", encoding="utf-8") as memory_file:
        json.dump(payload, memory_file, ensure_ascii=False, indent=2)
    print(f"[Memory] Saved layout memory to: {memory_path}")


def normalize_layers(layer_container):
    if isinstance(layer_container, dict):
        return [list(layer_container[layer]) for layer in sorted(layer_container.keys())]
    return [list(layer) for layer in layer_container]


def build_layer_dict(layer_container, sort_each_layer=False):
    layers = normalize_layers(layer_container)
    if sort_each_layer:
        layers = [sorted(int(node) for node in layer) for layer in layers]
    else:
        layers = [[int(node) for node in layer] for layer in layers]
    return {idx: nodes for idx, nodes in enumerate(layers)}


def place_node_coord(
    layer_idx,
    node_offset,
    x_spacing,
    y_spacing,
    origin_x,
    origin_y,
    orientation,
):
    if orientation == LEFT_RIGHT:
        return origin_x + layer_idx * x_spacing, origin_y + node_offset * y_spacing
    return origin_x + node_offset * x_spacing, origin_y + layer_idx * y_spacing


def build_node_positions_with_fixed_spacing(
    ordered_layers,
    x_spacing,
    y_spacing,
    origin_x,
    origin_y,
    orientation,
):
    node_positions = {}

    for layer_idx, nodes in enumerate(ordered_layers):
        for node_offset, node_id in enumerate(nodes):
            x, y = place_node_coord(
                layer_idx,
                node_offset,
                x_spacing,
                y_spacing,
                origin_x,
                origin_y,
                orientation,
            )
            node_id = int(node_id)
            node_positions[node_id] = (x, y)

    return node_positions


def first_layer_input_nodes(circuit, node_positions):
    input_node_source = getattr(circuit, "getInputNodesIndex", [])
    if callable(input_node_source):
        input_node_source = input_node_source()
    input_nodes = [
        int(node_id)
        for node_id in input_node_source
        if int(node_id) in node_positions
    ]
    if not input_nodes:
        return []

    node_layers = []
    for node_id in input_nodes:
        try:
            layer_idx = int(circuit.get_layer_of_node(node_id))
        except Exception:
            layer_idx = 0
        node_layers.append((layer_idx, node_id))

    first_layer = min(layer_idx for layer_idx, _node_id in node_layers)
    return [node_id for layer_idx, node_id in node_layers if layer_idx == first_layer]


def apply_common_first_input_phase(board, circuit, node_positions, common_phase=0):
    for node_id in first_layer_input_nodes(circuit, node_positions):
        coord = node_positions[int(node_id)]
        board.setPhase((int(coord[0]), int(coord[1])), int(common_phase))


def create_board_with_positions(
    circuit,
    node_positions,
    common_first_input_phase=None,
    clock_field=None,
):
    board = iFCN_Lab.MapChessboard()
    board.setPhaseEnabled(True)
    if clock_field is not None:
        install_phase_field(board, clock_field)
    for node_id, coord in node_positions.items():
        board.placeNode(node_id, coord, circuit.get_node_type(node_id))
    if common_first_input_phase is not None and clock_field is None:
        apply_common_first_input_phase(
            board,
            circuit,
            node_positions,
            common_phase=common_first_input_phase,
        )
    return board


class LayoutOnlyResult:
    def __init__(
        self,
        circuit,
        board,
        node_positions,
        width,
        height,
        routed_paths,
        failed_edges,
        selected_x_spacing,
        selected_y_spacing,
        selected_layout_strategy,
        run_time_sec,
    ):
        self.parse = circuit
        self.mapChessboard = board
        self.node_positions = node_positions
        self.width = width
        self.height = height
        self.routed_paths = routed_paths
        self.failed_edges = failed_edges
        self.selected_x_spacing = selected_x_spacing
        self.selected_y_spacing = selected_y_spacing
        self.selected_layout_strategy = selected_layout_strategy
        self.algorithm_description = ALGORITHM_DESCRIPTION
        self.run_time_sec = run_time_sec
        self.phase_assignment_ok = len(failed_edges) == 0
        self.phase_conflict_count = len(failed_edges)
        self.clock_template_ok = self.phase_assignment_ok
        self.clock_template_conflict_count = self.phase_conflict_count

    def get_node_coord(self, node_id):
        return self.node_positions[int(node_id)]


def build_route_order(circuit, node_positions=None, orientation=TOP_DOWN, embedding_scores=None):
    ordered_pairs = []
    seen = set()

    for layer in sorted(circuit.same_layer_route_pairs.keys()):
        for src, dst in circuit.same_layer_route_pairs[layer]:
            pair = (int(src), int(dst))
            if pair not in seen:
                ordered_pairs.append(pair)
                seen.add(pair)

    for src, dst in circuit.differ_layer_route_pairs:
        pair = (int(src), int(dst))
        if pair not in seen:
            ordered_pairs.append(pair)
            seen.add(pair)

    if node_positions is None:
        return ordered_pairs

    secondary_axis = 1 if orientation == LEFT_RIGHT else 0
    output_nodes = {int(node) for node in circuit.getOutputNodesIndex}
    fanout_count_cache = {}

    def get_fanout_count(node_id):
        node_id = int(node_id)
        if node_id not in fanout_count_cache:
            fanout_count_cache[node_id] = len(circuit.get_fanouts(node_id))
        return fanout_count_cache[node_id]

    def edge_priority(edge):
        src, dst = edge
        dst_is_sink = int(dst) in output_nodes or get_fanout_count(dst) == 0
        src_fanout_count = get_fanout_count(src)
        src_is_fanout_trunk = src_fanout_count > 1
        primary_axis = 0 if orientation == LEFT_RIGHT else 1
        primary_span = abs(
            node_positions[int(dst)][primary_axis] - node_positions[int(src)][primary_axis]
        )
        secondary_span = abs(
            node_positions[int(dst)][secondary_axis] - node_positions[int(src)][secondary_axis]
        )
        src_layer = circuit.get_layer_of_node(int(src))
        dst_layer = circuit.get_layer_of_node(int(dst))
        if embedding_scores is None:
            return (
                not dst_is_sink,
                -int(src_is_fanout_trunk),
                -primary_span if src_is_fanout_trunk else primary_span,
                -secondary_span if src_is_fanout_trunk else secondary_span,
                src_layer,
                -dst_layer,
            )

        embedding_span = abs(
            embedding_scores.get(int(dst), 0.5) - embedding_scores.get(int(src), 0.5)
        )
        layer_gap = dst_layer - src_layer
        fanout_pressure = src_fanout_count + len(circuit.get_fanins(int(dst)))
        return (
            not dst_is_sink,
            -int(src_is_fanout_trunk),
            -primary_span if src_is_fanout_trunk else primary_span,
            -secondary_span,
            -layer_gap,
            -embedding_span,
            -fanout_pressure,
            src_layer,
            -dst_layer,
        )

    ordered_pairs.sort(key=edge_priority)
    return ordered_pairs


def build_route_order_variants(
    circuit,
    node_positions=None,
    orientation=TOP_DOWN,
    embedding_scores=None,
    edge_priorities=None,
):
    base_order = build_route_order(
        circuit,
        node_positions=node_positions,
        orientation=orientation,
        embedding_scores=embedding_scores,
    )
    if not base_order or node_positions is None:
        return [base_order]

    primary_axis = 0 if orientation == LEFT_RIGHT else 1
    secondary_axis = 1 - primary_axis
    output_nodes = {int(node) for node in circuit.getOutputNodesIndex}

    def fanout_count(node_id):
        return len(circuit.get_fanouts(int(node_id)))

    def fanin_count(node_id):
        return len(circuit.get_fanins(int(node_id)))

    def edge_span(edge):
        src, dst = int(edge[0]), int(edge[1])
        start = node_positions[src]
        goal = node_positions[dst]
        primary_span = abs(int(goal[primary_axis]) - int(start[primary_axis]))
        secondary_span = abs(int(goal[secondary_axis]) - int(start[secondary_axis]))
        return primary_span, secondary_span, primary_span + secondary_span

    def edge_layer(edge):
        return circuit.get_layer_of_node(int(edge[0]))

    def source_secondary(edge):
        return int(node_positions[int(edge[0])][secondary_axis])

    learned_order = None
    if edge_priorities:
        learned_order = sorted(
            base_order,
            key=lambda edge: (
                -float(edge_priorities.get((int(edge[0]), int(edge[1])), 0.0)),
                int(edge[0]),
                int(edge[1]),
            ),
        )

    variants = [
        learned_order or base_order,
        base_order,
        sorted(
            base_order,
            key=lambda edge: (
                edge_layer(edge),
                -fanout_count(edge[0]),
                -edge_span(edge)[2],
                source_secondary(edge),
                int(edge[0]),
                int(edge[1]),
            ),
        ),
        sorted(
            base_order,
            key=lambda edge: (
                edge_layer(edge),
                -fanout_count(edge[0]),
                int(edge[0]),
                edge_span(edge)[2],
                int(edge[1]),
            ),
        ),
        sorted(
            base_order,
            key=lambda edge: (
                int(edge[1]) not in output_nodes,
                -fanin_count(edge[1]),
                -edge_span(edge)[1],
                edge_layer(edge),
                int(edge[0]),
                int(edge[1]),
            ),
        ),
        sorted(
            base_order,
            key=lambda edge: (
                source_secondary(edge),
                edge_layer(edge),
                -edge_span(edge)[2],
                int(edge[0]),
                int(edge[1]),
            ),
        ),
        list(reversed(base_order)),
    ]

    unique_variants = []
    seen = set()
    for variant in variants:
        signature = tuple((int(src), int(dst)) for src, dst in variant)
        if signature in seen:
            continue
        seen.add(signature)
        unique_variants.append(variant)
    return unique_variants


def add_direction(coord, direction):
    return coord[0] + direction[0], coord[1] + direction[1]


def get_outgoing_direction(path):
    if len(path) < 2:
        return None
    return path[1][0] - path[0][0], path[1][1] - path[0][1]


def get_incoming_direction(path):
    if len(path) < 2:
        return None
    return path[-2][0] - path[-1][0], path[-2][1] - path[-1][1]


def get_preferred_start_directions(orientation):
    if orientation == LEFT_RIGHT:
        return [(1, 0), (0, -1), (0, 1), (-1, 0)]
    return [(0, 1), (-1, 0), (1, 0), (0, -1)]


def get_preferred_end_directions(orientation):
    if orientation == LEFT_RIGHT:
        return [(-1, 0), (0, -1), (0, 1), (1, 0)]
    return [(0, -1), (-1, 0), (1, 0), (0, 1)]


def get_recorded_output_directions(outgoing_directions, node_id):
    recorded = outgoing_directions.get(int(node_id))
    if recorded is None:
        return set()
    if isinstance(recorded, set):
        return set(recorded)
    return {recorded}


def record_output_direction(outgoing_directions, node_id, direction, multi_output_nodes):
    node_id = int(node_id)
    if node_id in multi_output_nodes:
        recorded = get_recorded_output_directions(outgoing_directions, node_id)
        recorded.add(direction)
        outgoing_directions[node_id] = recorded
    else:
        outgoing_directions[node_id] = direction


def get_candidate_start_directions(node_id, orientation, incoming_directions, outgoing_directions):
    node_id = int(node_id)
    if node_id in outgoing_directions:
        recorded_directions = get_recorded_output_directions(
            outgoing_directions,
            node_id,
        )
        if len(recorded_directions) != 1:
            return []
        direction = next(iter(recorded_directions))
        if direction in incoming_directions[node_id]:
            return []
        return [direction]
    return [direction for direction in get_preferred_start_directions(orientation) if direction not in incoming_directions[node_id]]


def get_candidate_end_directions(node_id, orientation, incoming_directions, outgoing_directions):
    node_id = int(node_id)
    blocked = set(incoming_directions[node_id])
    blocked.update(get_recorded_output_directions(outgoing_directions, node_id))
    return [direction for direction in get_preferred_end_directions(orientation) if direction not in blocked]


def order_start_directions(start_coord, goal_coord, orientation, directions):
    dx = goal_coord[0] - start_coord[0]
    dy = goal_coord[1] - start_coord[1]
    fallback_rank = {direction: idx for idx, direction in enumerate(get_preferred_start_directions(orientation))}
    return sorted(
        directions,
        key=lambda direction: (
            -(direction[0] * dx + direction[1] * dy),
            fallback_rank.get(direction, len(ALL_DIRECTIONS)),
        ),
    )


def order_end_directions(start_coord, goal_coord, orientation, directions):
    dx = goal_coord[0] - start_coord[0]
    dy = goal_coord[1] - start_coord[1]
    fallback_rank = {direction: idx for idx, direction in enumerate(get_preferred_end_directions(orientation))}
    return sorted(
        directions,
        key=lambda direction: (
            direction[0] * dx + direction[1] * dy,
            fallback_rank.get(direction, len(ALL_DIRECTIONS)),
        ),
    )


def manhattan_distance(a, b):
    return abs(int(a[0]) - int(b[0])) + abs(int(a[1]) - int(b[1]))


def trailing_same_phase_run(board, path):
    if not path:
        return 1
    last_phase = board.getPhase(path[-1])
    if last_phase < 0:
        return 1
    run_len = 0
    for coord in reversed(path):
        if board.getPhase(coord) != last_phase:
            break
        run_len += 1
    return max(1, run_len)


def merge_prefix_and_branch_path(prefix_path, branch_path):
    if not prefix_path:
        return [(int(x), int(y)) for x, y in branch_path]
    if not branch_path:
        return [(int(x), int(y)) for x, y in prefix_path]
    merged = [(int(x), int(y)) for x, y in prefix_path]
    start_idx = 1 if merged[-1] == tuple(branch_path[0]) else 0
    merged.extend((int(x), int(y)) for x, y in branch_path[start_idx:])
    return merged


def phase_compatible(board, coord, expected_phase):
    phase = board.getPhase(coord)
    return phase < 0 or phase == expected_phase


def reconstruct_state_path(came_from, state):
    state_path = [state]
    while state in came_from:
        state = came_from[state]
        state_path.append(state)
    state_path.reverse()
    return state_path


def state_coord(state):
    return int(state[0]), int(state[1])


def state_phase(state):
    return int(state[2])


def state_run_len(state):
    return int(state[3])


def straight_coords_between(start, end):
    if start[0] == end[0]:
        step = 1 if end[1] > start[1] else -1
        return [(start[0], y) for y in range(start[1], end[1] + step, step)]
    if start[1] == end[1]:
        step = 1 if end[0] > start[0] else -1
        return [(x, start[1]) for x in range(start[0], end[0] + step, step)]
    return None


def build_straight_state_segment(board, start_state, end_state, phase_cycle, max_same_phase):
    coords = straight_coords_between(state_coord(start_state), state_coord(end_state))
    if coords is None or len(coords) < 3:
        return None

    target_phase = state_phase(end_state)
    target_run = state_run_len(end_state)
    states = {(state_phase(start_state), state_run_len(start_state)): [start_state]}
    for coord_idx, coord in enumerate(coords[1:], start=1):
        is_end = coord_idx == len(coords) - 1
        next_states = {}
        for (phase, run_len), segment in states.items():
            transitions = [((phase + 1) % phase_cycle, 1)]
            if max_same_phase <= 0 or run_len < max_same_phase:
                transitions.append((phase, run_len + 1))
            for next_phase, next_run in transitions:
                if is_end and (next_phase != target_phase or next_run != target_run):
                    continue
                if not is_end and not board.canPlaceWire(coord):
                    continue
                if not phase_compatible(board, coord, next_phase):
                    continue
                key = (next_phase, next_run)
                if key not in next_states:
                    next_states[key] = segment + [(coord[0], coord[1], next_phase, next_run)]
        states = next_states
        if not states:
            return None

    return states.get((target_phase, target_run))


def simplify_state_path(board, state_path, phase_cycle, max_same_phase):
    if len(state_path) < 4:
        return state_path

    simplified = [state_path[0]]
    idx = 0
    while idx < len(state_path) - 1:
        replacement = None
        replacement_end = None
        for end_idx in range(len(state_path) - 1, idx + 1, -1):
            if state_coord(state_path[idx]) == state_coord(state_path[end_idx]):
                continue
            segment = build_straight_state_segment(
                board,
                state_path[idx],
                state_path[end_idx],
                phase_cycle,
                max_same_phase,
            )
            if segment is not None and len(segment) < (end_idx - idx + 1):
                replacement = segment
                replacement_end = end_idx
                break
        if replacement is not None:
            simplified.extend(replacement[1:])
            idx = replacement_end
        else:
            simplified.append(state_path[idx + 1])
            idx += 1
    return simplified


def commit_state_path(board, src, dst, state_path, phase_cycle, max_same_phase, save_path=True):
    state_path = simplify_state_path(board, state_path, phase_cycle, max_same_phase)
    seen_coords = set()
    path = []
    for x, y, phase, _run_len in state_path:
        coord = (x, y)
        if coord in seen_coords:
            return []
        seen_coords.add(coord)
        if board.getPhase(coord) < 0:
            board.setPhase(coord, phase)
        path.append(coord)

    for idx in range(1, len(path) - 1):
        board.placeWire(path[idx])
    if save_path:
        board.savePath((src, dst), path)
    return path


def route_monotone_with_phase(
    board,
    src,
    dst,
    phase_cycle,
    padding,
    max_same_phase,
    orientation,
    start_dir=None,
    end_dir=None,
    start_dirs=None,
    end_dirs=None,
    start_coord=None,
    goal_coord=None,
    start_run_len=1,
    save_path=True,
    force_start_advance=False,
    expansion_limit=DEFAULT_MONOTONE_ROUTER_EXPANSION_LIMIT,
    advance_cost=1,
    hold_cost=2,
):
    start = tuple(start_coord) if start_coord is not None else board.getPlacedNodeCoord(src)
    goal = tuple(goal_coord) if goal_coord is not None else board.getPlacedNodeCoord(dst)
    if start == goal:
        return [start]

    if orientation == LEFT_RIGHT:
        if goal[0] < start[0]:
            return []
        min_x = start[0]
        max_x = goal[0]
        min_y = min(start[1], goal[1]) - padding
        max_y = max(start[1], goal[1]) + padding
        directions = [(1, 0), (0, -1), (0, 1)] if goal[0] > start[0] else [(0, -1), (0, 1)]
    else:
        if goal[1] < start[1]:
            return []
        min_x = min(start[0], goal[0]) - padding
        max_x = max(start[0], goal[0]) + padding
        min_y = start[1]
        max_y = goal[1]
        directions = [(-1, 0), (1, 0), (0, 1)] if goal[1] > start[1] else [(-1, 0), (1, 0)]

    start_phase = board.getPhase(start)
    start_phases = [start_phase] if start_phase >= 0 else list(range(phase_cycle))
    queue = []
    came_from = {}
    g_score = {}

    for phase in start_phases:
        state = (start[0], start[1], phase, max(1, int(start_run_len)))
        g_score[state] = 0
        heuristic = abs(start[0] - goal[0]) + abs(start[1] - goal[1])
        heapq.heappush(queue, (heuristic, 0, state))

    visited = set()
    expansions = 0

    while queue and (expansion_limit <= 0 or expansions < expansion_limit):
        _priority, current_cost, current = heapq.heappop(queue)
        if current in visited:
            continue
        visited.add(current)
        expansions += 1

        x, y, phase, run_len = current
        current_coord = (x, y)
        if not phase_compatible(board, current_coord, phase):
            continue

        if current_coord == goal:
            state_path = reconstruct_state_path(came_from, current)
            return commit_state_path(
                board,
                src,
                dst,
                state_path,
                phase_cycle,
                max_same_phase,
                save_path=save_path,
            )

        prev_coord = None
        if current in came_from:
            prev_state = came_from[current]
            prev_coord = (prev_state[0], prev_state[1])

        for dx, dy in directions:
            nx = x + dx
            ny = y + dy
            if nx < min_x or nx > max_x or ny < min_y or ny > max_y:
                continue
            next_coord = (nx, ny)
            if prev_coord is not None and next_coord == prev_coord:
                continue
            if prev_coord is None:
                if start_dirs is not None and (dx, dy) not in start_dirs:
                    continue
                if start_dir is not None and (dx, dy) != start_dir:
                    continue
            if next_coord == goal:
                incoming_dir = (x - goal[0], y - goal[1])
                if end_dirs is not None and incoming_dir not in end_dirs:
                    continue
                if end_dir is not None and incoming_dir != end_dir:
                    continue
            if next_coord != goal and not board.canPlaceWire(next_coord):
                continue

            next_phase = (phase + 1) % phase_cycle
            can_hold = (max_same_phase <= 0) or (run_len < max_same_phase)

            candidate_states = []
            if phase_compatible(board, next_coord, next_phase):
                candidate_states.append(
                    ((nx, ny, next_phase, 1), max(1, int(advance_cost)))
                )
            allow_hold = not (force_start_advance and prev_coord is None)
            if allow_hold and can_hold and phase_compatible(board, next_coord, phase):
                candidate_states.append(
                    ((nx, ny, phase, run_len + 1), max(1, int(hold_cost)))
                )

            for next_state, transition_cost in candidate_states:
                turn_cost = 0
                if prev_coord is not None and (dx, dy) != (x - prev_coord[0], y - prev_coord[1]):
                    turn_cost = 3
                tentative_cost = current_cost + transition_cost + turn_cost
                if next_state in g_score and tentative_cost >= g_score[next_state]:
                    continue
                g_score[next_state] = tentative_cost
                came_from[next_state] = current
                heuristic = abs(nx - goal[0]) + abs(ny - goal[1])
                heapq.heappush(queue, (tentative_cost + heuristic, tentative_cost, next_state))

    return []


def route_flexible_with_phase(
    board,
    src,
    dst,
    phase_cycle,
    padding,
    max_same_phase,
    orientation,
    start_dir=None,
    end_dir=None,
    start_dirs=None,
    end_dirs=None,
    start_coord=None,
    goal_coord=None,
    start_run_len=1,
    save_path=True,
    expansion_limit=DEFAULT_FLEXIBLE_ROUTER_EXPANSION_LIMIT,
    force_start_advance=False,
    advance_cost=1,
    hold_cost=2,
):
    if expansion_limit <= 0:
        return []

    start = tuple(start_coord) if start_coord is not None else board.getPlacedNodeCoord(src)
    goal = tuple(goal_coord) if goal_coord is not None else board.getPlacedNodeCoord(dst)
    if start == goal:
        return [start]

    local_padding = max(1, int(padding))
    min_x = min(start[0], goal[0]) - local_padding
    max_x = max(start[0], goal[0]) + local_padding
    min_y = min(start[1], goal[1]) - local_padding
    max_y = max(start[1], goal[1]) + local_padding

    primary_axis = 0 if orientation == LEFT_RIGHT else 1
    preferred_dirs = (
        [(1, 0), (0, -1), (0, 1), (-1, 0)]
        if orientation == LEFT_RIGHT else
        [(0, 1), (-1, 0), (1, 0), (0, -1)]
    )
    directions = [direction for direction in preferred_dirs if direction in ALL_DIRECTIONS]

    start_phase = board.getPhase(start)
    start_phases = [start_phase] if start_phase >= 0 else list(range(phase_cycle))
    queue = []
    came_from = {}
    g_score = {}

    for phase in start_phases:
        state = (start[0], start[1], phase, max(1, int(start_run_len)), 0, 0)
        g_score[state] = 0
        heapq.heappush(queue, (manhattan_distance(start, goal), 0, state))

    expansions = 0
    while queue and expansions < expansion_limit:
        _priority, current_cost, current = heapq.heappop(queue)
        if current_cost != g_score.get(current):
            continue
        expansions += 1

        x, y, phase, run_len, prev_dx, prev_dy = current
        current_coord = (x, y)
        if not phase_compatible(board, current_coord, phase):
            continue

        if current_coord == goal:
            state_path = [
                (state[0], state[1], state[2], state[3])
                for state in reconstruct_state_path(came_from, current)
            ]
            return commit_state_path(
                board,
                src,
                dst,
                state_path,
                phase_cycle,
                max_same_phase,
                save_path=save_path,
            )

        prev_coord = None
        if current in came_from:
            prev_state = came_from[current]
            prev_coord = (prev_state[0], prev_state[1])

        for dx, dy in directions:
            nx = x + dx
            ny = y + dy
            if nx < min_x or nx > max_x or ny < min_y or ny > max_y:
                continue
            next_coord = (nx, ny)
            if prev_coord is not None and next_coord == prev_coord:
                continue
            if prev_coord is None:
                if start_dirs is not None and (dx, dy) not in start_dirs:
                    continue
                if start_dir is not None and (dx, dy) != start_dir:
                    continue
            if next_coord == goal:
                incoming_dir = (x - goal[0], y - goal[1])
                if end_dirs is not None and incoming_dir not in end_dirs:
                    continue
                if end_dir is not None and incoming_dir != end_dir:
                    continue
            if next_coord != goal and not board.canPlaceWire(next_coord):
                continue

            next_phase = (phase + 1) % phase_cycle
            can_hold = (max_same_phase <= 0) or (run_len < max_same_phase)
            candidate_states = []
            if phase_compatible(board, next_coord, next_phase):
                candidate_states.append(
                    (next_phase, 1, max(1, int(advance_cost)))
                )
            allow_hold = not (force_start_advance and prev_coord is None)
            if allow_hold and can_hold and phase_compatible(board, next_coord, phase):
                candidate_states.append(
                    (phase, run_len + 1, max(1, int(hold_cost)))
                )

            for candidate_phase, candidate_run, transition_cost in candidate_states:
                turn_cost = 0
                if (prev_dx != 0 or prev_dy != 0) and (dx, dy) != (prev_dx, prev_dy):
                    turn_cost = 2
                backward_cost = 0
                if primary_axis == 0:
                    backward_cost = 4 if (goal[0] - start[0]) * dx < 0 else 0
                else:
                    backward_cost = 4 if (goal[1] - start[1]) * dy < 0 else 0
                next_state = (nx, ny, candidate_phase, candidate_run, dx, dy)
                tentative_cost = current_cost + transition_cost + turn_cost + backward_cost
                if tentative_cost >= g_score.get(next_state, float("inf")):
                    continue
                came_from[next_state] = current
                g_score[next_state] = tentative_cost
                heuristic = manhattan_distance(next_coord, goal)
                heapq.heappush(queue, (tentative_cost + heuristic, tentative_cost, next_state))

    return []


def build_shifted_node_positions(
    circuit,
    ordered_layers,
    x_spacing,
    y_spacing,
    origin_x,
    origin_y,
    orientation,
):
    node_positions = {}

    for layer_idx, nodes in enumerate(ordered_layers):
        if orientation == LEFT_RIGHT:
            base_positions = [origin_y + idx * y_spacing for idx, _node in enumerate(nodes)]
            axis_index = 1
            axis_origin = origin_y
        else:
            base_positions = [origin_x + idx * x_spacing for idx, _node in enumerate(nodes)]
            axis_index = 0
            axis_origin = origin_x
        desired_offsets = []

        for idx, node_id in enumerate(nodes):
            node_id = int(node_id)
            fanins = [int(fanin) for fanin in circuit.get_fanins(node_id) if int(fanin) in node_positions]
            if fanins:
                desired_offsets.append(
                    mean(node_positions[fanin][axis_index] for fanin in fanins) - base_positions[idx]
                )

        offset = int(round(mean(desired_offsets))) if desired_offsets else 0
        shifted_positions = [value + offset for value in base_positions]
        min_position = min(shifted_positions) if shifted_positions else axis_origin
        if min_position < axis_origin:
            shifted_positions = [value + (axis_origin - min_position) for value in shifted_positions]

        for node_id, secondary_axis in zip(nodes, shifted_positions):
            node_id = int(node_id)
            if orientation == LEFT_RIGHT:
                x = origin_x + layer_idx * x_spacing
                node_positions[node_id] = (x, secondary_axis)
            else:
                y = origin_y + layer_idx * y_spacing
                node_positions[node_id] = (secondary_axis, y)

    return node_positions


def build_node_layer_map(ordered_layers):
    node_to_layer = {}
    for layer_idx, nodes in enumerate(ordered_layers):
        for node_id in nodes:
            node_to_layer[int(node_id)] = layer_idx
    return node_to_layer


def estimate_boundary_route_loads(circuit, ordered_layers):
    if len(ordered_layers) < 2:
        return []

    node_to_layer = build_node_layer_map(ordered_layers)
    boundary_loads = [0] * (len(ordered_layers) - 1)
    boundary_branching = [0] * (len(ordered_layers) - 1)

    for src, dst in circuit.effective_edges:
        src = int(src)
        dst = int(dst)
        src_layer = node_to_layer.get(src)
        dst_layer = node_to_layer.get(dst)
        if src_layer is None or dst_layer is None or dst_layer <= src_layer:
            continue
        for boundary in range(src_layer, dst_layer):
            boundary_loads[boundary] += 1

    for boundary, (left_layer, right_layer) in enumerate(zip(ordered_layers, ordered_layers[1:])):
        branching_left = sum(
            1 for node_id in left_layer if len(circuit.get_fanouts(int(node_id))) > 1
        )
        branching_right = sum(
            1 for node_id in right_layer if len(circuit.get_fanins(int(node_id))) > 1
        )
        boundary_branching[boundary] = branching_left + branching_right

    return list(zip(boundary_loads, boundary_branching))


def build_adaptive_primary_positions(circuit, ordered_layers, base_primary_spacing, axis_origin):
    if not ordered_layers:
        return []

    positions = [axis_origin]
    base_gap = max(1, base_primary_spacing - 1)

    for boundary_idx, (route_load, branching_load) in enumerate(
        estimate_boundary_route_loads(circuit, ordered_layers)
    ):
        left_width = len(ordered_layers[boundary_idx])
        right_width = len(ordered_layers[boundary_idx + 1])
        width_scale = max(1, min(left_width, right_width))
        gap = base_gap

        if route_load > width_scale:
            gap += 1
        if route_load > width_scale * 2:
            gap += 1
        if branching_load > max(left_width, right_width):
            gap += 1
        if max(left_width, right_width) >= 6 and route_load > 0:
            gap += 1

        positions.append(positions[-1] + min(base_primary_spacing + 3, gap))

    return positions


def compute_adaptive_secondary_gap(circuit, prev_node_id, next_node_id, base_secondary_spacing):
    prev_node_id = int(prev_node_id)
    next_node_id = int(next_node_id)
    prev_fanin = len(circuit.get_fanins(prev_node_id))
    prev_fanout = len(circuit.get_fanouts(prev_node_id))
    next_fanin = len(circuit.get_fanins(next_node_id))
    next_fanout = len(circuit.get_fanouts(next_node_id))

    gap = max(1, min(3, base_secondary_spacing - 2))
    if max(prev_fanin, prev_fanout, next_fanin, next_fanout) >= 2:
        gap += 1
    if prev_fanin + prev_fanout + next_fanin + next_fanout >= 6:
        gap += 1

    return min(base_secondary_spacing + 1, gap)


def get_neighbor_axis_targets(
    circuit,
    node_id,
    node_positions,
    axis_index,
    include_fanins=True,
    include_fanouts=False,
):
    targets = []

    if include_fanins:
        for fanin in circuit.get_fanins(node_id):
            fanin = int(fanin)
            if fanin in node_positions:
                targets.append(node_positions[fanin][axis_index])

    if include_fanouts:
        for fanout in circuit.get_fanouts(node_id):
            fanout = int(fanout)
            if fanout in node_positions:
                targets.append(node_positions[fanout][axis_index])

    return targets


def solve_ordered_targets_with_gaps(targets, min_gaps, axis_origin):
    if not targets:
        return []

    adjusted_targets = []
    cumulative_gap = 0
    for idx, target in enumerate(targets):
        adjusted_targets.append(float(target) - cumulative_gap)
        if idx < len(min_gaps):
            cumulative_gap += min_gaps[idx]

    fitted = [max(float(axis_origin), adjusted_targets[0])]
    for target in adjusted_targets[1:]:
        fitted.append(max(fitted[-1], target))

    positions = []
    cumulative_gap = 0
    for idx, fitted_value in enumerate(fitted):
        positions.append(int(round(fitted_value + cumulative_gap)))
        if idx < len(min_gaps):
            cumulative_gap += min_gaps[idx]

    shift_candidates = [targets[idx] - positions[idx] for idx in range(len(targets))]
    shift = int(round(mean(shift_candidates))) if shift_candidates else 0
    positions = [position + shift for position in positions]
    min_position = min(positions)
    if min_position < axis_origin:
        positions = [position + (axis_origin - min_position) for position in positions]

    return positions


def solve_adaptive_secondary_positions(
    circuit,
    nodes,
    node_positions,
    base_secondary_spacing,
    axis_origin,
    axis_index,
    include_fanins=True,
    include_fanouts=False,
):
    if not nodes:
        return []

    fallback_targets = [axis_origin]
    for idx in range(1, len(nodes)):
        fallback_targets.append(
            fallback_targets[-1] + compute_adaptive_secondary_gap(
                circuit,
                nodes[idx - 1],
                nodes[idx],
                base_secondary_spacing,
            )
        )

    targets = []
    for idx, node_id in enumerate(nodes):
        neighbor_targets = get_neighbor_axis_targets(
            circuit,
            int(node_id),
            node_positions,
            axis_index,
            include_fanins=include_fanins,
            include_fanouts=include_fanouts,
        )
        if neighbor_targets:
            targets.append(int(round(mean(neighbor_targets))))
        else:
            targets.append(fallback_targets[idx])

    min_gaps = [
        compute_adaptive_secondary_gap(circuit, prev_node, next_node, base_secondary_spacing)
        for prev_node, next_node in zip(nodes, nodes[1:])
    ]
    return solve_ordered_targets_with_gaps(targets, min_gaps, axis_origin)


def build_adaptive_node_positions(
    circuit,
    ordered_layers,
    x_spacing,
    y_spacing,
    origin_x,
    origin_y,
    orientation,
):
    node_positions = {}

    if orientation == LEFT_RIGHT:
        primary_positions = build_adaptive_primary_positions(
            circuit,
            ordered_layers,
            x_spacing,
            origin_x,
        )
        axis_index = 1
        axis_origin = origin_y
        base_secondary_spacing = y_spacing
    else:
        primary_positions = build_adaptive_primary_positions(
            circuit,
            ordered_layers,
            y_spacing,
            origin_y,
        )
        axis_index = 0
        axis_origin = origin_x
        base_secondary_spacing = x_spacing

    for layer_idx, nodes in enumerate(ordered_layers):
        secondary_positions = solve_adaptive_secondary_positions(
            circuit,
            nodes,
            node_positions,
            base_secondary_spacing,
            axis_origin,
            axis_index,
            include_fanins=True,
            include_fanouts=False,
        )
        primary_coord = primary_positions[layer_idx]
        for node_id, secondary_coord in zip(nodes, secondary_positions):
            node_id = int(node_id)
            if orientation == LEFT_RIGHT:
                node_positions[node_id] = (primary_coord, secondary_coord)
            else:
                node_positions[node_id] = (secondary_coord, primary_coord)

    for layer_idx in reversed(range(len(ordered_layers))):
        secondary_positions = solve_adaptive_secondary_positions(
            circuit,
            ordered_layers[layer_idx],
            node_positions,
            base_secondary_spacing,
            axis_origin,
            axis_index,
            include_fanins=True,
            include_fanouts=True,
        )
        primary_coord = primary_positions[layer_idx]
        for node_id, secondary_coord in zip(ordered_layers[layer_idx], secondary_positions):
            node_id = int(node_id)
            if orientation == LEFT_RIGHT:
                node_positions[node_id] = (primary_coord, secondary_coord)
            else:
                node_positions[node_id] = (secondary_coord, primary_coord)

    return node_positions


def build_embedding_score_map(circuit, ordered_layers, embeddings):
    if embeddings is None:
        return {}

    node_ids = []
    rows = []
    embedding_count = len(embeddings)

    for layer in ordered_layers:
        for node_id in layer:
            node_id = int(node_id)
            node_index = circuit.node_to_index.get(node_id)
            if node_index is None or node_index >= embedding_count:
                continue
            row = np.asarray(embeddings[node_index], dtype=float)
            if row.ndim != 1 or not np.isfinite(row).all():
                continue
            node_ids.append(node_id)
            rows.append(row)

    if not rows:
        return {}

    matrix = np.vstack(rows)
    if matrix.shape[0] <= 1 or np.allclose(np.var(matrix, axis=0).sum(), 0.0):
        scores = np.arange(matrix.shape[0], dtype=float)
    else:
        centered = matrix - matrix.mean(axis=0, keepdims=True)
        try:
            _u, _s, vh = np.linalg.svd(centered, full_matrices=False)
            scores = centered @ vh[0]
        except np.linalg.LinAlgError:
            scores = centered[:, 0]

    finite_scores = np.asarray(scores, dtype=float)
    finite_scores = finite_scores[np.isfinite(finite_scores)]
    if finite_scores.size == 0:
        return {}

    min_score = float(np.min(finite_scores))
    max_score = float(np.max(finite_scores))
    if max_score - min_score < 1e-9:
        return {node_id: 0.5 for node_id in node_ids}

    return {
        node_id: float((score - min_score) / (max_score - min_score))
        for node_id, score in zip(node_ids, scores)
        if np.isfinite(score)
    }


def compute_embedding_secondary_gap(
    circuit,
    prev_node_id,
    next_node_id,
    embedding_scores,
    base_secondary_spacing,
):
    prev_node_id = int(prev_node_id)
    next_node_id = int(next_node_id)
    prev_score = embedding_scores.get(prev_node_id)
    next_score = embedding_scores.get(next_node_id)

    if prev_score is None or next_score is None:
        score_gap = 0.18
    else:
        score_gap = abs(float(next_score) - float(prev_score))

    max_degree = max(
        len(circuit.get_fanins(prev_node_id)),
        len(circuit.get_fanouts(prev_node_id)),
        len(circuit.get_fanins(next_node_id)),
        len(circuit.get_fanouts(next_node_id)),
    )

    gap = 1.5
    if score_gap > 0.08:
        gap += 0.5
    if score_gap > 0.18:
        gap += 0.5
    if score_gap > 0.32:
        gap += 0.5
    if max_degree >= 2:
        gap += 0.5
    if max_degree >= 3:
        gap += 0.5

    return min(float(max(1, base_secondary_spacing)), gap)


def build_embedding_secondary_targets(
    circuit,
    nodes,
    embedding_scores,
    base_secondary_spacing,
    axis_origin,
):
    if not nodes:
        return []

    if len(nodes) == 1:
        return [float(axis_origin)]

    fallback_scores = np.linspace(0.0, 1.0, len(nodes))
    raw_scores = np.asarray(
        [
            embedding_scores.get(int(node_id), float(fallback_scores[idx]))
            for idx, node_id in enumerate(nodes)
        ],
        dtype=float,
    )

    if not np.isfinite(raw_scores).all() or np.max(raw_scores) - np.min(raw_scores) < 1e-9:
        normalized_scores = fallback_scores
    else:
        normalized_scores = (raw_scores - np.min(raw_scores)) / (
            np.max(raw_scores) - np.min(raw_scores)
        )

    targets = [float(axis_origin)]
    score_diffs = np.diff(normalized_scores)
    positive_diffs = np.abs(score_diffs[np.abs(score_diffs) > 1e-9])
    reference_gap = float(np.median(positive_diffs)) if positive_diffs.size else 0.15

    for idx in range(1, len(nodes)):
        score_gap = abs(float(normalized_scores[idx] - normalized_scores[idx - 1]))
        embedding_gap = compute_embedding_secondary_gap(
            circuit,
            nodes[idx - 1],
            nodes[idx],
            embedding_scores,
            base_secondary_spacing,
        )
        relative_gap = 1.0 + min(1.5, score_gap / max(reference_gap, 1e-6))
        targets.append(targets[-1] + min(float(base_secondary_spacing), 0.6 * embedding_gap + 0.4 * relative_gap))

    return targets


def compute_layer_embedding_stats(ordered_layers, embedding_scores):
    centroids = []
    spreads = []
    for layer in ordered_layers:
        layer_scores = [embedding_scores[int(node_id)] for node_id in layer if int(node_id) in embedding_scores]
        if not layer_scores:
            centroids.append(0.5)
            spreads.append(0.0)
            continue
        centroids.append(float(mean(layer_scores)))
        spreads.append(float(max(layer_scores) - min(layer_scores)))
    return centroids, spreads


def build_gcn_guided_primary_positions(
    circuit,
    ordered_layers,
    embedding_scores,
    base_primary_spacing,
    axis_origin,
):
    if not ordered_layers:
        return []

    positions = [axis_origin]
    boundary_loads = estimate_boundary_route_loads(circuit, ordered_layers)
    layer_centroids, layer_spreads = compute_layer_embedding_stats(ordered_layers, embedding_scores)
    base_gap = max(1, base_primary_spacing - 2)

    for boundary_idx, (route_load, branching_load) in enumerate(boundary_loads):
        left_width = len(ordered_layers[boundary_idx])
        right_width = len(ordered_layers[boundary_idx + 1])
        width_scale = max(1, min(left_width, right_width))
        centroid_gap = abs(layer_centroids[boundary_idx + 1] - layer_centroids[boundary_idx])
        spread_pressure = layer_spreads[boundary_idx] + layer_spreads[boundary_idx + 1]

        gap = float(base_gap)
        if route_load > width_scale:
            gap += 1.0
        if route_load > width_scale * 2:
            gap += 1.0
        if branching_load > max(left_width, right_width):
            gap += 0.5
        if centroid_gap > 0.22:
            gap += 0.5
        if spread_pressure > 0.70:
            gap += 0.5

        positions.append(positions[-1] + min(float(base_primary_spacing + 2), gap))

    return [int(round(position)) for position in positions]


def build_primary_gap_constraints(circuit, ordered_layers, base_primary_spacing):
    gaps = []
    base_gap = max(1.0, float(base_primary_spacing - 2))

    for boundary_idx, (route_load, branching_load) in enumerate(
        estimate_boundary_route_loads(circuit, ordered_layers)
    ):
        left_width = len(ordered_layers[boundary_idx])
        right_width = len(ordered_layers[boundary_idx + 1])
        width_scale = max(1, min(left_width, right_width))

        gap = base_gap
        if route_load > width_scale:
            gap += 0.5
        if route_load > width_scale * 2:
            gap += 0.5
        if branching_load > max(left_width, right_width):
            gap += 0.5

        gaps.append(min(float(base_primary_spacing + 1), gap))

    return gaps


def blend_primary_positions(
    circuit,
    ordered_layers,
    compact_positions,
    baseline_positions,
    base_primary_spacing,
    axis_origin,
):
    targets = [
        0.65 * compact_position + 0.35 * baseline_position
        for compact_position, baseline_position in zip(compact_positions, baseline_positions)
    ]
    min_gaps = build_primary_gap_constraints(circuit, ordered_layers, base_primary_spacing)
    return solve_ordered_targets_with_gaps(targets, min_gaps, axis_origin)


def solve_gcn_guided_secondary_positions(
    circuit,
    nodes,
    node_positions,
    embedding_scores,
    baseline_axis_positions,
    base_secondary_spacing,
    axis_origin,
    axis_index,
    include_fanins=True,
    include_fanouts=False,
):
    if not nodes:
        return []

    embedding_targets = build_embedding_secondary_targets(
        circuit,
        nodes,
        embedding_scores,
        base_secondary_spacing,
        axis_origin,
    )

    blended_targets = []
    for idx, node_id in enumerate(nodes):
        neighbor_targets = get_neighbor_axis_targets(
            circuit,
            int(node_id),
            node_positions,
            axis_index,
            include_fanins=include_fanins,
            include_fanouts=include_fanouts,
        )
        baseline_target = baseline_axis_positions.get(int(node_id))
        if neighbor_targets:
            if baseline_target is None:
                blended_targets.append(0.6 * embedding_targets[idx] + 0.4 * mean(neighbor_targets))
            else:
                blended_targets.append(
                    0.5 * embedding_targets[idx] +
                    0.3 * mean(neighbor_targets) +
                    0.2 * baseline_target
                )
        elif baseline_target is not None:
            blended_targets.append(0.7 * embedding_targets[idx] + 0.3 * baseline_target)
        else:
            blended_targets.append(embedding_targets[idx])

    min_gaps = []
    for prev_node, next_node in zip(nodes, nodes[1:]):
        adaptive_gap = compute_adaptive_secondary_gap(
            circuit,
            prev_node,
            next_node,
            base_secondary_spacing,
        )
        embedding_gap = compute_embedding_secondary_gap(
            circuit,
            prev_node,
            next_node,
            embedding_scores,
            base_secondary_spacing,
        )
        layer_pressure = 0.5 if len(nodes) >= 4 else 0.0
        min_gaps.append(max(1.0, 0.65 * adaptive_gap + 0.35 * embedding_gap + layer_pressure))

    return solve_ordered_targets_with_gaps(blended_targets, min_gaps, axis_origin)


def build_gcn_guided_node_positions(
    circuit,
    ordered_layers,
    embeddings,
    x_spacing,
    y_spacing,
    origin_x,
    origin_y,
    orientation,
):
    embedding_scores = build_embedding_score_map(circuit, ordered_layers, embeddings)
    if not embedding_scores:
        return build_adaptive_node_positions(
            circuit,
            ordered_layers,
            x_spacing,
            y_spacing,
            origin_x,
            origin_y,
            orientation,
        )

    baseline_positions = build_adaptive_node_positions(
        circuit,
        ordered_layers,
        x_spacing,
        y_spacing,
        origin_x,
        origin_y,
        orientation,
    )
    node_positions = {}

    if orientation == LEFT_RIGHT:
        baseline_primary_positions = [
            baseline_positions[int(layer[0])][0] if layer else origin_x
            for layer in ordered_layers
        ]
        compact_primary_positions = build_gcn_guided_primary_positions(
            circuit,
            ordered_layers,
            embedding_scores,
            x_spacing,
            origin_x,
        )
        primary_positions = blend_primary_positions(
            circuit,
            ordered_layers,
            compact_primary_positions,
            baseline_primary_positions,
            x_spacing,
            origin_x,
        )
        axis_index = 1
        axis_origin = origin_y
        base_secondary_spacing = y_spacing
        baseline_axis_positions = {
            int(node_id): coord[1] for node_id, coord in baseline_positions.items()
        }
    else:
        baseline_primary_positions = [
            baseline_positions[int(layer[0])][1] if layer else origin_y
            for layer in ordered_layers
        ]
        compact_primary_positions = build_gcn_guided_primary_positions(
            circuit,
            ordered_layers,
            embedding_scores,
            y_spacing,
            origin_y,
        )
        primary_positions = blend_primary_positions(
            circuit,
            ordered_layers,
            compact_primary_positions,
            baseline_primary_positions,
            y_spacing,
            origin_y,
        )
        axis_index = 0
        axis_origin = origin_x
        base_secondary_spacing = x_spacing
        baseline_axis_positions = {
            int(node_id): coord[0] for node_id, coord in baseline_positions.items()
        }

    for layer_idx, nodes in enumerate(ordered_layers):
        secondary_positions = solve_gcn_guided_secondary_positions(
            circuit,
            nodes,
            node_positions,
            embedding_scores,
            baseline_axis_positions,
            base_secondary_spacing,
            axis_origin,
            axis_index,
            include_fanins=True,
            include_fanouts=False,
        )
        primary_coord = primary_positions[layer_idx]
        for node_id, secondary_coord in zip(nodes, secondary_positions):
            node_id = int(node_id)
            if orientation == LEFT_RIGHT:
                node_positions[node_id] = (primary_coord, secondary_coord)
            else:
                node_positions[node_id] = (secondary_coord, primary_coord)

    for layer_idx in reversed(range(len(ordered_layers))):
        secondary_positions = solve_gcn_guided_secondary_positions(
            circuit,
            ordered_layers[layer_idx],
            node_positions,
            embedding_scores,
            baseline_axis_positions,
            base_secondary_spacing,
            axis_origin,
            axis_index,
            include_fanins=True,
            include_fanouts=True,
        )
        primary_coord = primary_positions[layer_idx]
        for node_id, secondary_coord in zip(ordered_layers[layer_idx], secondary_positions):
            node_id = int(node_id)
            if orientation == LEFT_RIGHT:
                node_positions[node_id] = (primary_coord, secondary_coord)
            else:
                node_positions[node_id] = (secondary_coord, primary_coord)

    return node_positions


def summarize_axis_spacing(values):
    unique_values = sorted({int(value) for value in values})
    if len(unique_values) < 2:
        return "n/a"

    diffs = [right - left for left, right in zip(unique_values, unique_values[1:]) if right > left]
    if not diffs:
        return "n/a"
    if min(diffs) == max(diffs):
        return str(diffs[0])
    return f"{min(diffs)}..{max(diffs)}"


def summarize_node_position_spacing(node_positions):
    if not node_positions:
        return "n/a", "n/a"

    x_spacing = summarize_axis_spacing(coord[0] for coord in node_positions.values())
    y_spacing = summarize_axis_spacing(coord[1] for coord in node_positions.values())
    return x_spacing, y_spacing


def clone_positions(node_positions):
    return {
        int(node_id): (int(coord[0]), int(coord[1]))
        for node_id, coord in node_positions.items()
    }


def layout_score_key(result):
    return (
        len(result["failed_edges"]),
        result["direction_violation_count"],
        result["area"],
        max(result["width"], result["height"]),
        result["width"] + result["height"],
        result["route_overhang_penalty"],
        result["io_exposure_penalty"],
    )


def get_primary_axis(coord, orientation):
    return coord[0] if orientation == LEFT_RIGHT else coord[1]


def get_secondary_axis(coord, orientation):
    return coord[1] if orientation == LEFT_RIGHT else coord[0]


def make_coord(primary_axis, secondary_axis, orientation):
    primary_axis = int(primary_axis)
    secondary_axis = int(secondary_axis)
    if orientation == LEFT_RIGHT:
        return primary_axis, secondary_axis
    return secondary_axis, primary_axis


def get_layer_primary_positions(ordered_layers, node_positions, orientation):
    primary_positions = []
    for layer_nodes in ordered_layers:
        if not layer_nodes:
            primary_positions.append(0)
            continue
        primary_positions.append(
            get_primary_axis(node_positions[int(layer_nodes[0])], orientation)
        )
    return primary_positions


def compute_layer_route_pressure(boundary_loads, layer_idx):
    pressure = 0.0
    if layer_idx > 0:
        pressure += sum(boundary_loads[layer_idx - 1])
    if layer_idx < len(boundary_loads):
        pressure += sum(boundary_loads[layer_idx])
    return pressure


def infer_layer_secondary_spacing(layer_nodes, node_positions, orientation):
    secondary_positions = sorted(
        get_secondary_axis(node_positions[int(node_id)], orientation)
        for node_id in layer_nodes
    )
    diffs = [
        right - left
        for left, right in zip(secondary_positions, secondary_positions[1:])
        if right > left
    ]
    if not diffs:
        return 1
    return max(1, int(round(mean(diffs))))


def node_position_signature(node_positions):
    return tuple(
        (int(node_id), int(coord[0]), int(coord[1]))
        for node_id, coord in sorted(node_positions.items())
    )


def predict_secondary_span_after_shift(
    current_secondary,
    delta,
    global_min_secondary,
    global_max_secondary,
):
    next_secondary = current_secondary + delta
    next_min_secondary = min(global_min_secondary, next_secondary)
    next_max_secondary = max(global_max_secondary, next_secondary)

    if current_secondary == global_min_secondary and delta > 0:
        next_min_secondary = min(global_max_secondary, next_secondary)
    if current_secondary == global_max_secondary and delta < 0:
        next_max_secondary = max(global_min_secondary, next_secondary)

    return next_max_secondary - next_min_secondary


def compute_projected_secondary_span(
    node_positions,
    shifted_node_ids,
    delta,
    orientation,
):
    shifted_node_ids = {int(node_id) for node_id in shifted_node_ids}
    projected_secondaries = []
    for node_id, coord in node_positions.items():
        secondary_axis = get_secondary_axis(coord, orientation)
        if int(node_id) in shifted_node_ids:
            secondary_axis += delta
        projected_secondaries.append(secondary_axis)

    if not projected_secondaries:
        return 0
    return max(projected_secondaries) - min(projected_secondaries)


def build_desired_secondary_map(
    circuit,
    ordered_layers,
    node_positions,
    orientation,
    embedding_scores=None,
):
    desired_secondary_map = {}
    axis_index = 1 if orientation == LEFT_RIGHT else 0

    for layer_nodes in ordered_layers:
        base_secondary_spacing = infer_layer_secondary_spacing(
            layer_nodes,
            node_positions,
            orientation,
        )
        axis_origin = min(
            (
                get_secondary_axis(node_positions[int(node_id)], orientation)
                for node_id in layer_nodes
            ),
            default=1,
        )
        if embedding_scores:
            embedding_targets = build_embedding_secondary_targets(
                circuit,
                layer_nodes,
                embedding_scores,
                base_secondary_spacing,
                axis_origin,
            )
            embedding_target_map = {
                int(node_id): float(target)
                for node_id, target in zip(layer_nodes, embedding_targets)
            }
        else:
            embedding_target_map = {}

        for node_id in layer_nodes:
            node_id = int(node_id)
            current_secondary = get_secondary_axis(node_positions[node_id], orientation)
            neighbor_targets = get_neighbor_axis_targets(
                circuit,
                node_id,
                node_positions,
                axis_index,
                include_fanins=True,
                include_fanouts=True,
            )
            neighbor_target = (
                float(mean(neighbor_targets))
                if neighbor_targets else float(current_secondary)
            )
            embedding_target = embedding_target_map.get(node_id, float(current_secondary))
            desired_secondary_map[node_id] = 0.65 * neighbor_target + 0.35 * embedding_target

    return desired_secondary_map


def build_local_compaction_actions(
    circuit,
    ordered_layers,
    node_positions,
    orientation,
    embedding_scores=None,
):
    boundary_loads = estimate_boundary_route_loads(circuit, ordered_layers)
    global_secondary_values = [
        get_secondary_axis(coord, orientation)
        for coord in node_positions.values()
    ]
    global_min_secondary = min(global_secondary_values, default=1)
    global_max_secondary = max(global_secondary_values, default=1)
    global_secondary_mid = 0.5 * (global_min_secondary + global_max_secondary)
    desired_secondary_map = build_desired_secondary_map(
        circuit,
        ordered_layers,
        node_positions,
        orientation,
        embedding_scores,
    )

    if embedding_scores:
        layer_centroids, layer_spreads = compute_layer_embedding_stats(
            ordered_layers,
            embedding_scores,
        )
    else:
        layer_centroids = [0.5] * len(ordered_layers)
        layer_spreads = [0.0] * len(ordered_layers)

    layer_primary_positions = get_layer_primary_positions(
        ordered_layers,
        node_positions,
        orientation,
    )
    compression_deltas = (-4, -2, -1)
    secondary_deltas = (-2, -1, 1, 2)

    gap_actions = []
    for boundary_idx, (route_load, branching_load) in enumerate(boundary_loads):
        centroid_gap = abs(layer_centroids[boundary_idx + 1] - layer_centroids[boundary_idx])
        spread_pressure = layer_spreads[boundary_idx] + layer_spreads[boundary_idx + 1]
        current_gap = (
            layer_primary_positions[boundary_idx + 1] - layer_primary_positions[boundary_idx]
        )
        pressure = (
            route_load * 3.0 +
            branching_load * 2.0 +
            centroid_gap * 4.0 +
            spread_pressure * 2.0 -
            current_gap * 0.25
        )
        for delta in compression_deltas:
            if current_gap + delta <= 0:
                continue
            gap_actions.append(((pressure, abs(delta), boundary_idx), ("gap", boundary_idx, delta)))

    layer_actions = []
    for layer_idx, layer_nodes in enumerate(ordered_layers):
        secondary_values = [
            get_secondary_axis(node_positions[int(node_id)], orientation)
            for node_id in layer_nodes
        ]
        layer_scores = [
            embedding_scores.get(int(node_id), 0.5)
            for node_id in layer_nodes
        ] if embedding_scores else []
        embedding_spread = (
            max(layer_scores) - min(layer_scores)
            if len(layer_scores) > 1 else 0.0
        )
        current_layer_center = float(mean(secondary_values)) if secondary_values else global_secondary_mid
        if layer_scores:
            normalized_score_center = float(mean(layer_scores))
            desired_layer_center = (
                0.7 * current_layer_center +
                0.3 * (
                    global_min_secondary +
                    normalized_score_center * max(1.0, global_max_secondary - global_min_secondary)
                )
            )
        else:
            desired_layer_center = 0.65 * current_layer_center + 0.35 * global_secondary_mid

        route_pressure = compute_layer_route_pressure(boundary_loads, layer_idx)
        layer_min_secondary = min(secondary_values, default=global_min_secondary)
        layer_max_secondary = max(secondary_values, default=global_max_secondary)
        for delta in secondary_deltas:
            next_layer_center = current_layer_center + delta
            next_span = (
                max(global_max_secondary, layer_max_secondary + delta) -
                min(global_min_secondary, layer_min_secondary + delta)
            )
            if layer_min_secondary == global_min_secondary and delta > 0:
                next_span = min(next_span, global_max_secondary - min(global_max_secondary, layer_min_secondary + delta))
            if layer_max_secondary == global_max_secondary and delta < 0:
                next_span = min(next_span, max(global_min_secondary, layer_max_secondary + delta) - global_min_secondary)

            priority = (
                next_span,
                abs(next_layer_center - desired_layer_center),
                -route_pressure,
                -embedding_spread,
                layer_idx,
                delta,
            )
            layer_actions.append((priority, ("layer_shift", layer_idx, delta)))

    block_actions = []
    current_secondary_span = global_max_secondary - global_min_secondary
    for block_size in (2, 3):
        if len(ordered_layers) < block_size:
            continue
        for start_idx in range(0, len(ordered_layers) - block_size + 1):
            block_layers = tuple(range(start_idx, start_idx + block_size))
            block_nodes = tuple(
                int(node_id)
                for layer_idx in block_layers
                for node_id in ordered_layers[layer_idx]
            )
            route_pressure = sum(
                compute_layer_route_pressure(boundary_loads, layer_idx)
                for layer_idx in block_layers
            )
            for delta in secondary_deltas:
                projected_span = compute_projected_secondary_span(
                    node_positions,
                    block_nodes,
                    delta,
                    orientation,
                )
                if projected_span > current_secondary_span:
                    continue
                priority = (
                    projected_span,
                    -route_pressure,
                    start_idx,
                    block_size,
                    delta,
                )
                block_actions.append((priority, ("layer_block_shift", block_layers, delta)))

    segment_actions = []
    seen_segment_actions = set()
    for layer_idx, layer_nodes in enumerate(ordered_layers):
        if len(layer_nodes) < 3:
            continue

        sorted_nodes = [
            int(node_id)
            for node_id in sorted(
                layer_nodes,
                key=lambda node_id: (
                    get_secondary_axis(node_positions[int(node_id)], orientation),
                    int(node_id),
                ),
            )
        ]
        sorted_secondaries = [
            get_secondary_axis(node_positions[node_id], orientation)
            for node_id in sorted_nodes
        ]
        for cut_idx in range(1, len(sorted_nodes)):
            cut_gap = sorted_secondaries[cut_idx] - sorted_secondaries[cut_idx - 1]
            candidate_segments = []
            if cut_idx >= 2:
                candidate_segments.append(tuple(sorted_nodes[:cut_idx]))
            if (len(sorted_nodes) - cut_idx) >= 2:
                candidate_segments.append(tuple(sorted_nodes[cut_idx:]))

            for segment_nodes in candidate_segments:
                segment_center = mean(
                    get_secondary_axis(node_positions[node_id], orientation)
                    for node_id in segment_nodes
                )
                desired_center = mean(
                    desired_secondary_map.get(
                        int(node_id),
                        get_secondary_axis(node_positions[int(node_id)], orientation),
                    )
                    for node_id in segment_nodes
                )
                for delta in secondary_deltas:
                    action = ("segment_shift", segment_nodes, delta)
                    action_key = (segment_nodes, delta)
                    if action_key in seen_segment_actions:
                        continue
                    seen_segment_actions.add(action_key)
                    predicted_span = compute_projected_secondary_span(
                        node_positions,
                        segment_nodes,
                        delta,
                        orientation,
                    )
                    priority = (
                        predicted_span,
                        -cut_gap,
                        abs((segment_center + delta) - desired_center),
                        abs((segment_center + delta) - global_secondary_mid),
                        layer_idx,
                        delta,
                    )
                    segment_actions.append((priority, action))

    node_actions = []
    for layer_nodes in ordered_layers:
        for node_id in layer_nodes:
            node_id = int(node_id)
            current_secondary = get_secondary_axis(node_positions[node_id], orientation)
            desired_secondary = desired_secondary_map.get(node_id, float(current_secondary))
            node_degree = len(circuit.get_fanins(node_id)) + len(circuit.get_fanouts(node_id))
            for delta in secondary_deltas:
                next_secondary = current_secondary + delta
                predicted_span = predict_secondary_span_after_shift(
                    current_secondary,
                    delta,
                    global_min_secondary,
                    global_max_secondary,
                )
                priority = (
                    predicted_span,
                    abs(next_secondary - desired_secondary),
                    abs(next_secondary - global_secondary_mid),
                    -node_degree,
                    node_id,
                    delta,
                )
                node_actions.append((priority, ("node_shift", node_id, delta)))

    ordered_actions = []
    ordered_actions.extend(action for _priority, action in sorted(gap_actions))
    ordered_actions.extend(action for _priority, action in sorted(block_actions))
    ordered_actions.extend(action for _priority, action in sorted(segment_actions))
    ordered_actions.extend(action for _priority, action in sorted(layer_actions))
    ordered_actions.extend(action for _priority, action in sorted(node_actions))
    return ordered_actions


def apply_local_compaction_action(ordered_layers, node_positions, orientation, action):
    action_type, target, delta = action

    if action_type == "gap":
        primary_positions = get_layer_primary_positions(
            ordered_layers,
            node_positions,
            orientation,
        )
        if (primary_positions[target + 1] - primary_positions[target] + int(delta)) <= 0:
            return False
        for layer_idx in range(target + 1, len(ordered_layers)):
            for node_id in ordered_layers[layer_idx]:
                node_id = int(node_id)
                coord = node_positions[node_id]
                primary_axis = get_primary_axis(coord, orientation) + delta
                secondary_axis = get_secondary_axis(coord, orientation)
                if primary_axis < 1:
                    return False
                node_positions[node_id] = make_coord(
                    primary_axis,
                    secondary_axis,
                    orientation,
                )
        return True

    if action_type == "segment_shift":
        shifted_nodes = {int(node_id) for node_id in target}
        target_coords = {}
        for node_id in shifted_nodes:
            coord = node_positions[node_id]
            primary_axis = get_primary_axis(coord, orientation)
            secondary_axis = get_secondary_axis(coord, orientation) + delta
            if secondary_axis < 1:
                return False
            target_coords[node_id] = make_coord(
                primary_axis,
                secondary_axis,
                orientation,
            )

        occupied_targets = {tuple(coord) for coord in target_coords.values()}
        for other_node_id, other_coord in node_positions.items():
            if int(other_node_id) in shifted_nodes:
                continue
            if tuple(other_coord) in occupied_targets:
                return False

        for node_id, coord in target_coords.items():
            node_positions[int(node_id)] = coord
        return True

    if action_type == "layer_block_shift":
        shifted_nodes = {
            int(node_id)
            for layer_idx in target
            for node_id in ordered_layers[int(layer_idx)]
        }
        target_coords = {}
        occupied_targets = set()
        for node_id in shifted_nodes:
            coord = node_positions[node_id]
            primary_axis = get_primary_axis(coord, orientation)
            secondary_axis = get_secondary_axis(coord, orientation) + delta
            if secondary_axis < 1:
                return False
            target_coord = make_coord(primary_axis, secondary_axis, orientation)
            if target_coord in occupied_targets:
                return False
            occupied_targets.add(target_coord)
            target_coords[node_id] = target_coord

        for other_node_id, other_coord in node_positions.items():
            if int(other_node_id) in shifted_nodes:
                continue
            if tuple(other_coord) in occupied_targets:
                return False

        for node_id, coord in target_coords.items():
            node_positions[int(node_id)] = coord
        return True

    if action_type == "layer_shift":
        for node_id in ordered_layers[target]:
            node_id = int(node_id)
            coord = node_positions[node_id]
            primary_axis = get_primary_axis(coord, orientation)
            secondary_axis = get_secondary_axis(coord, orientation) + delta
            if secondary_axis < 1:
                return False
            node_positions[node_id] = make_coord(
                primary_axis,
                secondary_axis,
                orientation,
            )
        return True

    if action_type == "node_shift":
        node_id = int(target)
        coord = node_positions[node_id]
        primary_axis = get_primary_axis(coord, orientation)
        secondary_axis = get_secondary_axis(coord, orientation) + delta
        if secondary_axis < 1:
            return False
        target_coord = make_coord(primary_axis, secondary_axis, orientation)
        for other_node_id, other_coord in node_positions.items():
            if int(other_node_id) == node_id:
                continue
            if tuple(other_coord) == target_coord:
                return False
        node_positions[node_id] = target_coord
        return True

    return False


def local_beam_search_key(result):
    return (
        len(result["failed_edges"]),
        int(result["direction_violation_count"]),
        float(result["area"]),
        max(int(result["width"]), int(result["height"])),
        int(result["width"]) + int(result["height"]),
        int(result["route_overhang_penalty"]),
        int(result["io_exposure_penalty"]),
    )


def evaluate_local_compaction_positions(
    candidate_positions,
    circuit,
    orientation,
    local_strategy,
    phase_cycle,
    padding,
    max_same_phase,
    evaluation_embedding_scores,
    eval_cache,
):
    signature = node_position_signature(candidate_positions)
    candidate_result = eval_cache.get(signature)
    if candidate_result is None:
        candidate_x_spacing, candidate_y_spacing = summarize_node_position_spacing(
            candidate_positions
        )
        candidate_result = evaluate_layout_candidate(
            {
                "strategy": local_strategy,
                "orientation": orientation,
                "x_spacing": candidate_x_spacing,
                "y_spacing": candidate_y_spacing,
                "node_positions": candidate_positions,
                "routing_embedding_guidance": bool(evaluation_embedding_scores),
            },
            circuit,
            phase_cycle,
            padding,
            max_same_phase,
            embedding_scores=evaluation_embedding_scores,
        )
        eval_cache[signature] = candidate_result
    return candidate_result, layout_score_key(candidate_result)


def refine_layout_with_local_compaction(
    result,
    circuit,
    ordered_layers,
    phase_cycle,
    padding,
    max_same_phase,
    guidance_embedding_scores=None,
    evaluation_embedding_scores=None,
    max_rounds=0,
    lookahead_depth=DEFAULT_LOCAL_LOOKAHEAD_DEPTH,
    beam_width=DEFAULT_LOCAL_BEAM_WIDTH,
    branch_width=DEFAULT_LOCAL_BRANCH_WIDTH,
    max_evaluations=0,
):
    if max_rounds <= 0 or result is None:
        return result

    orientation = result["layout_orientation"]
    best_result = result
    best_score = layout_score_key(result)
    current_positions = clone_positions(result["node_positions"])
    local_strategy = f"{result['layout_strategy']}+local"
    eval_cache = {}
    evaluation_count = 0
    evaluation_budget = int(max_evaluations) if max_evaluations is not None else 0

    for _round in range(max_rounds):
        if evaluation_budget > 0 and evaluation_count >= evaluation_budget:
            break
        round_best_result = None
        round_best_score = best_score
        round_best_positions = None
        first_step_states = []
        for action in build_local_compaction_actions(
            circuit,
            ordered_layers,
            current_positions,
            orientation,
            guidance_embedding_scores,
        ):
            candidate_positions = clone_positions(current_positions)
            if not apply_local_compaction_action(
                ordered_layers,
                candidate_positions,
                orientation,
                action,
            ):
                continue
            if evaluation_budget > 0 and evaluation_count >= evaluation_budget:
                break

            candidate_result, candidate_score = evaluate_local_compaction_positions(
                candidate_positions,
                circuit,
                orientation,
                local_strategy,
                phase_cycle,
                padding,
                max_same_phase,
                evaluation_embedding_scores,
                eval_cache,
            )
            evaluation_count += 1
            first_step_states.append(
                (
                    local_beam_search_key(candidate_result),
                    candidate_score,
                    clone_positions(candidate_positions),
                    candidate_result,
                )
            )
            if candidate_score < round_best_score:
                round_best_result = candidate_result
                round_best_score = candidate_score
                round_best_positions = clone_positions(candidate_positions)

        if lookahead_depth >= 2 and first_step_states:
            beam_states = sorted(first_step_states, key=lambda item: item[0])[:beam_width]
            for _beam_key, _score, first_positions, _first_result in beam_states:
                if evaluation_budget > 0 and evaluation_count >= evaluation_budget:
                    break
                second_actions = build_local_compaction_actions(
                    circuit,
                    ordered_layers,
                    first_positions,
                    orientation,
                    guidance_embedding_scores,
                )[:branch_width]
                for action in second_actions:
                    second_positions = clone_positions(first_positions)
                    if not apply_local_compaction_action(
                        ordered_layers,
                        second_positions,
                        orientation,
                        action,
                    ):
                        continue
                    if evaluation_budget > 0 and evaluation_count >= evaluation_budget:
                        break
                    candidate_result, candidate_score = evaluate_local_compaction_positions(
                        second_positions,
                        circuit,
                        orientation,
                        local_strategy,
                        phase_cycle,
                        padding,
                        max_same_phase,
                        evaluation_embedding_scores,
                        eval_cache,
                    )
                    evaluation_count += 1
                    if candidate_score < round_best_score:
                        round_best_result = candidate_result
                        round_best_score = candidate_score
                        round_best_positions = clone_positions(second_positions)

        if round_best_result is None:
            break

        best_result = round_best_result
        best_score = round_best_score
        current_positions = round_best_positions

    best_result["score"] = best_score
    return best_result


def build_layout_candidates(
    circuit,
    ordered_layers,
    origin_x,
    origin_y,
    base_x_spacing,
    base_y_spacing,
    embeddings=None,
    allowed_strategies=None,
    allowed_orientations=None,
):
    max_layer_width = max((len(layer) for layer in ordered_layers), default=0)
    total_layers = len(ordered_layers)

    candidates = []
    strategy_filter = set(
        ("fixed", "shifted", "adaptive", "gcn")
        if allowed_strategies is None else allowed_strategies
    )
    orientation_filter = set(
        (LEFT_RIGHT, TOP_DOWN)
        if allowed_orientations is None else allowed_orientations
    )

    def compact_spacing_candidates(base_spacing, upper_extra=1):
        base_spacing = max(1, int(base_spacing))
        upper = max(base_spacing + int(upper_extra), 2)
        return list(range(1, upper + 1))

    for orientation in (LEFT_RIGHT, TOP_DOWN):
        if orientation not in orientation_filter:
            continue
        if max_layer_width <= 2 and total_layers <= 8:
            x_candidates = compact_spacing_candidates(base_x_spacing, upper_extra=0)
            y_candidates = compact_spacing_candidates(min(base_y_spacing, 4), upper_extra=0)
        elif max_layer_width <= 3 and total_layers <= 10:
            x_candidates = compact_spacing_candidates(base_x_spacing, upper_extra=1)
            y_candidates = compact_spacing_candidates(min(base_y_spacing, 4), upper_extra=1)
        elif orientation == LEFT_RIGHT and total_layers <= 10:
            x_candidates = sorted({
                1,
                2,
                max(1, base_x_spacing - 1),
                base_x_spacing,
                base_x_spacing + 1,
            })
            y_candidates = sorted({
                1,
                2,
                max(1, base_y_spacing - 3),
                max(1, base_y_spacing - 2),
                max(1, base_y_spacing - 1),
                base_y_spacing,
            })
        else:
            x_candidates = sorted({
                1,
                2,
                max(1, base_x_spacing - 1),
                base_x_spacing,
                base_x_spacing + 1,
            })
            y_candidates = sorted({
                1,
                2,
                max(1, base_y_spacing - 2),
                max(1, base_y_spacing - 1),
                base_y_spacing,
            })

        for x_spacing in x_candidates:
            for y_spacing in y_candidates:
                if "fixed" in strategy_filter:
                    candidates.append(
                        {
                            "strategy": "fixed",
                            "orientation": orientation,
                            "x_spacing": x_spacing,
                            "y_spacing": y_spacing,
                            "node_positions": build_node_positions_with_fixed_spacing(
                                ordered_layers,
                                x_spacing,
                                y_spacing,
                                origin_x,
                                origin_y,
                                orientation,
                            ),
                        }
                    )
                if "shifted" in strategy_filter:
                    candidates.append(
                        {
                            "strategy": "shifted",
                            "orientation": orientation,
                            "x_spacing": x_spacing,
                            "y_spacing": y_spacing,
                            "node_positions": build_shifted_node_positions(
                                circuit,
                                ordered_layers,
                                x_spacing,
                                y_spacing,
                                origin_x,
                                origin_y,
                                orientation,
                            ),
                        }
                    )
                if "adaptive" in strategy_filter:
                    adaptive_positions = build_adaptive_node_positions(
                        circuit,
                        ordered_layers,
                        x_spacing,
                        y_spacing,
                        origin_x,
                        origin_y,
                        orientation,
                    )
                    adaptive_x_spacing, adaptive_y_spacing = summarize_node_position_spacing(
                        adaptive_positions
                    )
                    candidates.append(
                        {
                            "strategy": "adaptive",
                            "orientation": orientation,
                            "x_spacing": adaptive_x_spacing,
                            "y_spacing": adaptive_y_spacing,
                            "node_positions": adaptive_positions,
                        }
                    )
                if "gcn" in strategy_filter and embeddings is not None:
                    gcn_positions = build_gcn_guided_node_positions(
                        circuit,
                        ordered_layers,
                        embeddings,
                        x_spacing,
                        y_spacing,
                        origin_x,
                        origin_y,
                        orientation,
                    )
                    gcn_x_spacing, gcn_y_spacing = summarize_node_position_spacing(
                        gcn_positions
                    )
                    candidates.append(
                        {
                            "strategy": "gcn",
                            "orientation": orientation,
                            "x_spacing": gcn_x_spacing,
                            "y_spacing": gcn_y_spacing,
                            "node_positions": gcn_positions,
                        }
                    )

    return candidates


def route_single_edge_with_direction_constraints(
    board,
    generic_router,
    src,
    dst,
    phase_cycle,
    padding,
    max_same_phase,
    orientation,
    incoming_directions,
    outgoing_directions,
    routed_paths=None,
    first_layer_inputs=None,
    combined_port_search=True,
    advance_cost=1,
    hold_cost=2,
    multi_output_nodes=None,
    allow_relaxed_ports=True,
    allow_escape_routing=DEFAULT_ALLOW_ESCAPE_ROUTING,
    monotone_expansion_limit=None,
    flexible_expansion_limit=None,
):
    monotone_expansion_limit = (
        DEFAULT_MONOTONE_ROUTER_EXPANSION_LIMIT
        if monotone_expansion_limit is None else
        max(0, int(monotone_expansion_limit))
    )
    flexible_expansion_limit = (
        DEFAULT_FLEXIBLE_ROUTER_EXPANSION_LIMIT
        if flexible_expansion_limit is None else
        max(0, int(flexible_expansion_limit))
    )
    start_coord = board.getPlacedNodeCoord(src)
    goal_coord = board.getPlacedNodeCoord(dst)
    force_start_advance = int(src) in (first_layer_inputs or set())
    if int(src) in {int(node_id) for node_id in (multi_output_nodes or ())}:
        start_directions = [
            direction
            for direction in get_preferred_start_directions(orientation)
            if direction not in incoming_directions[int(src)]
        ]
    else:
        start_directions = get_candidate_start_directions(
            src,
            orientation,
            incoming_directions,
            outgoing_directions,
        )
    end_directions = get_candidate_end_directions(
        dst,
        orientation,
        incoming_directions,
        outgoing_directions,
    )
    start_directions = order_start_directions(start_coord, goal_coord, orientation, start_directions)
    end_directions = order_end_directions(start_coord, goal_coord, orientation, end_directions)

    if not start_directions or not end_directions:
        return []

    branch_path = route_edge_from_existing_source_trunk(
        board,
        int(src),
        int(dst),
        routed_paths or {},
        phase_cycle,
        padding,
        max_same_phase,
        orientation,
        incoming_directions,
        outgoing_directions,
        combined_port_search=combined_port_search,
        advance_cost=advance_cost,
        hold_cost=hold_cost,
        monotone_expansion_limit=monotone_expansion_limit,
    )
    if branch_path:
        return branch_path

    start_directions = [
        direction
        for direction in start_directions
        if add_direction(start_coord, direction) == goal_coord
        or board.canPlaceWire(add_direction(start_coord, direction))
    ]
    if not start_directions:
        return []
    direction_pairs = [
        (start_dir, end_dir)
        for start_dir in start_directions
        for end_dir in end_directions
    ]

    if combined_port_search:
        path = route_monotone_with_phase(
            board,
            src,
            dst,
            phase_cycle,
            padding,
            max_same_phase,
            orientation,
            start_dirs=set(start_directions),
            end_dirs=set(end_directions),
            force_start_advance=force_start_advance,
            expansion_limit=monotone_expansion_limit,
            advance_cost=advance_cost,
            hold_cost=hold_cost,
        )
        if path:
            return path
    else:
        for start_dir, end_dir in direction_pairs:
            path = route_monotone_with_phase(
                board,
                src,
                dst,
                phase_cycle,
                padding,
                max_same_phase,
                orientation,
                start_dir=start_dir,
                end_dir=end_dir,
                force_start_advance=force_start_advance,
                expansion_limit=monotone_expansion_limit,
                advance_cost=advance_cost,
                hold_cost=hold_cost,
            )
            if path:
                return path

    # Monotone routing can fail when an already-routed net forces a short
    # detour. Keep the selected node ports fixed while allowing A* to move
    # backwards around the obstruction before considering relaxed ports.
    if combined_port_search:
        path = route_flexible_with_phase(
            board,
            src,
            dst,
            phase_cycle,
            padding,
            max_same_phase,
            orientation,
            start_dirs=set(start_directions),
            end_dirs=set(end_directions),
            force_start_advance=force_start_advance,
            expansion_limit=flexible_expansion_limit,
            advance_cost=advance_cost,
            hold_cost=hold_cost,
        )
        if path:
            return path
    if allow_relaxed_ports:
        relaxed_monotone_path = route_monotone_with_phase(
            board,
            src,
            dst,
            phase_cycle,
            padding,
            max_same_phase,
            orientation,
            force_start_advance=force_start_advance,
            expansion_limit=monotone_expansion_limit,
            advance_cost=advance_cost,
            hold_cost=hold_cost,
        )
        if relaxed_monotone_path:
            return relaxed_monotone_path

        flexible_path = route_flexible_with_phase(
            board,
            src,
            dst,
            phase_cycle,
            padding,
            max_same_phase,
            orientation,
            force_start_advance=force_start_advance,
            expansion_limit=flexible_expansion_limit,
            advance_cost=advance_cost,
            hold_cost=hold_cost,
        )
        if flexible_path:
            return flexible_path

    if DEFAULT_GENERIC_ROUTER_COMBO_LIMIT > 0:
        for start_dir, end_dir in direction_pairs[:DEFAULT_GENERIC_ROUTER_COMBO_LIMIT]:
            start_neighbor = add_direction(start_coord, start_dir)
            if start_neighbor != goal_coord and not board.canPlaceWire(start_neighbor):
                continue
            path = generic_router.route_with_dirs(
                src,
                dst,
                start_dir[0],
                start_dir[1],
                end_dir[0],
                end_dir[1],
                True,
                True,
            )
            if path:
                return path

    if allow_escape_routing:
        # Last-resort exact route without fixed port directions. This keeps a
        # difficult benchmark from being discarded solely because all legal
        # port choices were locally blocked; direction violations are still
        # counted and heavily penalized by the scorer.
        path = generic_router.route(src, dst)
        if path:
            return path

    return []


def build_source_trunk_branch_candidates(src, dst, routed_paths, board, orientation, limit=32):
    if not routed_paths:
        return []

    goal_coord = tuple(board.getPlacedNodeCoord(int(dst)))
    best_by_coord = {}
    for (route_src, _route_dst), route_path in routed_paths.items():
        if int(route_src) != int(src) or not route_path:
            continue
        path = [(int(x), int(y)) for x, y in route_path]
        if len(path) < 3:
            continue
        for idx in range(1, len(path) - 1):
            branch_coord = path[idx]
            if orientation == LEFT_RIGHT and branch_coord[0] > goal_coord[0]:
                continue
            if orientation != LEFT_RIGHT and branch_coord[1] > goal_coord[1]:
                continue
            prefix = path[: idx + 1]
            aligned = int(branch_coord[0] != goal_coord[0] and branch_coord[1] != goal_coord[1])
            score = (
                manhattan_distance(branch_coord, goal_coord),
                aligned,
                len(prefix),
                idx,
            )
            current = best_by_coord.get(branch_coord)
            if current is None or score < current[0]:
                best_by_coord[branch_coord] = (score, prefix)

    candidates = [
        (score, coord, prefix)
        for coord, (score, prefix) in best_by_coord.items()
    ]
    candidates.sort(key=lambda item: item[0])
    return candidates[:limit]


def route_edge_from_existing_source_trunk(
    board,
    src,
    dst,
    routed_paths,
    phase_cycle,
    padding,
    max_same_phase,
    orientation,
    incoming_directions,
    outgoing_directions,
    combined_port_search=True,
    advance_cost=1,
    hold_cost=2,
    monotone_expansion_limit=DEFAULT_MONOTONE_ROUTER_EXPANSION_LIMIT,
):
    goal_coord = board.getPlacedNodeCoord(int(dst))
    end_directions = get_candidate_end_directions(
        dst,
        orientation,
        incoming_directions,
        outgoing_directions,
    )
    if not end_directions:
        return []

    for _score, branch_coord, prefix_path in build_source_trunk_branch_candidates(
        src,
        dst,
        routed_paths,
        board,
        orientation,
    ):
        start_directions = order_start_directions(
            branch_coord,
            goal_coord,
            orientation,
            get_preferred_start_directions(orientation),
        )
        ordered_end_directions = order_end_directions(
            branch_coord,
            goal_coord,
            orientation,
            end_directions,
        )
        start_run_len = trailing_same_phase_run(board, prefix_path)

        start_directions = [
            direction
            for direction in start_directions
            if add_direction(branch_coord, direction) == goal_coord
            or board.canPlaceWire(add_direction(branch_coord, direction))
        ]
        if not start_directions:
            continue
        if combined_port_search:
            branch_segment = route_monotone_with_phase(
                board,
                src,
                dst,
                phase_cycle,
                padding,
                max_same_phase,
                orientation,
                start_dirs=set(start_directions),
                end_dirs=set(ordered_end_directions),
                start_coord=branch_coord,
                goal_coord=goal_coord,
                start_run_len=start_run_len,
                save_path=False,
                expansion_limit=monotone_expansion_limit,
                advance_cost=advance_cost,
                hold_cost=hold_cost,
            )
            if branch_segment:
                return merge_prefix_and_branch_path(prefix_path, branch_segment)
        else:
            for start_dir in start_directions:
                for end_dir in ordered_end_directions:
                    branch_segment = route_monotone_with_phase(
                        board,
                        src,
                        dst,
                        phase_cycle,
                        padding,
                        max_same_phase,
                        orientation,
                        start_dir=start_dir,
                        end_dir=end_dir,
                        start_coord=branch_coord,
                        goal_coord=goal_coord,
                        start_run_len=start_run_len,
                        save_path=False,
                        expansion_limit=monotone_expansion_limit,
                        advance_cost=advance_cost,
                        hold_cost=hold_cost,
                    )
                    if branch_segment:
                        return merge_prefix_and_branch_path(
                            prefix_path,
                            branch_segment,
                        )

    return []


def compute_direction_violation_count(routed_paths, multi_output_nodes=None):
    multi_output_nodes = {
        int(node_id) for node_id in (multi_output_nodes or ())
    }
    incoming_directions = defaultdict(list)
    outgoing_directions = defaultdict(set)

    for (src, dst), path in routed_paths.items():
        out_dir = get_outgoing_direction(path)
        in_dir = get_incoming_direction(path)
        if out_dir is not None:
            outgoing_directions[int(src)].add(out_dir)
        if in_dir is not None:
            incoming_directions[int(dst)].append(in_dir)

    violation_count = 0
    all_nodes = set(incoming_directions.keys()) | set(outgoing_directions.keys())
    for node_id in all_nodes:
        incoming_dir_set = set(incoming_directions[node_id])
        if len(incoming_dir_set) < len(incoming_directions[node_id]):
            violation_count += len(incoming_directions[node_id]) - len(incoming_dir_set)
        if (
            node_id not in multi_output_nodes
            and len(outgoing_directions[node_id]) > 1
        ):
            violation_count += len(outgoing_directions[node_id]) - 1
        overlap = incoming_dir_set & outgoing_directions[node_id]
        violation_count += len(overlap)

    return violation_count


def find_direction_violation_edges(routed_paths, multi_output_nodes=None):
    multi_output_nodes = {
        int(node_id) for node_id in (multi_output_nodes or ())
    }
    incoming = defaultdict(list)
    outgoing = defaultdict(list)
    for edge, path in routed_paths.items():
        normalized_edge = (int(edge[0]), int(edge[1]))
        out_dir = get_outgoing_direction(path)
        in_dir = get_incoming_direction(path)
        if out_dir is not None:
            outgoing[normalized_edge[0]].append((normalized_edge, out_dir))
        if in_dir is not None:
            incoming[normalized_edge[1]].append((normalized_edge, in_dir))

    conflict_edges = set()
    for node_id in set(incoming) | set(outgoing):
        incoming_by_direction = defaultdict(list)
        outgoing_by_direction = defaultdict(list)
        for edge, direction in incoming[node_id]:
            incoming_by_direction[direction].append(edge)
        for edge, direction in outgoing[node_id]:
            outgoing_by_direction[direction].append(edge)

        for edges in incoming_by_direction.values():
            if len(edges) > 1:
                conflict_edges.update(edges)
        if node_id not in multi_output_nodes and len(outgoing_by_direction) > 1:
            for edges in outgoing_by_direction.values():
                conflict_edges.update(edges)
        for direction in set(incoming_by_direction) & set(outgoing_by_direction):
            conflict_edges.update(incoming_by_direction[direction])
            conflict_edges.update(outgoing_by_direction[direction])
    return conflict_edges


def route_edges_with_phase(
    board,
    circuit,
    phase_cycle,
    padding,
    max_same_phase,
    orientation,
    embedding_scores=None,
    clock_field=None,
    edge_priorities=None,
):
    node_positions = {
        int(node_id): (int(coord[0]), int(coord[1]))
        for node_id, coord in board.nodeIndexToCoordMap.items()
    }
    first_input_nodes = set(first_layer_input_nodes(circuit, node_positions))
    multi_output_nodes = {
        int(node_id)
        for node_id in circuit.effective_nodes
        if str(circuit.getNodeTypeString(int(node_id))).lower()
        in {"input", "fanout"}
    }
    edge_count = len(circuit.effective_edges)
    large_circuit = edge_count >= 75 or (
        edge_count >= 60 and int(phase_cycle) <= 2
    )
    initial_monotone_budget = 1000 if edge_count >= 75 else 2000
    initial_flexible_budget = 1500 if edge_count >= 75 else 2500
    initial_monotone_limit = (
        initial_monotone_budget
        if large_circuit and DEFAULT_MONOTONE_ROUTER_EXPANSION_LIMIT <= 0
        else min(
            DEFAULT_MONOTONE_ROUTER_EXPANSION_LIMIT,
            initial_monotone_budget,
        )
        if large_circuit
        else DEFAULT_MONOTONE_ROUTER_EXPANSION_LIMIT
    )
    initial_flexible_limit = (
        min(DEFAULT_FLEXIBLE_ROUTER_EXPANSION_LIMIT, initial_flexible_budget)
        if large_circuit
        else DEFAULT_FLEXIBLE_ROUTER_EXPANSION_LIMIT
    )

    def route_order_once(
        ordered_pairs,
        combined_port_search=True,
        advance_cost=1,
        hold_cost=2,
    ):
        attempt_board = create_board_with_positions(
            circuit,
            node_positions,
            common_first_input_phase=0,
            clock_field=clock_field,
        )
        routed_paths = {}
        failed_edges = []
        generic_router = iFCN_Lab.MapPhaseAStar(
            attempt_board,
            phase_cycle,
            padding,
            max_same_phase,
        )
        incoming_directions = defaultdict(set)
        outgoing_directions = {}

        for src, dst in ordered_pairs:
            path = route_single_edge_with_direction_constraints(
                attempt_board,
                generic_router,
                int(src),
                int(dst),
                phase_cycle,
                padding,
                max_same_phase,
                orientation,
                incoming_directions,
                outgoing_directions,
                routed_paths=routed_paths,
                first_layer_inputs=first_input_nodes,
                combined_port_search=combined_port_search,
                advance_cost=advance_cost,
                hold_cost=hold_cost,
                multi_output_nodes=multi_output_nodes,
                monotone_expansion_limit=initial_monotone_limit,
                flexible_expansion_limit=initial_flexible_limit,
            )
            if not path:
                failed_edges.append((int(src), int(dst)))
                continue

            attempt_board.savePath((int(src), int(dst)), path)
            out_dir = get_outgoing_direction(path)
            in_dir = get_incoming_direction(path)
            if out_dir is not None:
                record_output_direction(
                    outgoing_directions,
                    int(src),
                    out_dir,
                    multi_output_nodes,
                )
            if in_dir is not None:
                incoming_directions[int(dst)].add(in_dir)
            routed_paths[(int(src), int(dst))] = path

        return attempt_board, routed_paths, failed_edges

    best_payload = None
    best_key = None
    route_order_variants = build_route_order_variants(
        circuit,
        node_positions=node_positions,
        orientation=orientation,
        embedding_scores=embedding_scores,
        edge_priorities=edge_priorities,
    )
    route_order_limit = (
        min(DEFAULT_ROUTE_ORDER_VARIANTS, 3)
        if large_circuit
        else DEFAULT_ROUTE_ORDER_VARIANTS
    )
    route_order_variants = route_order_variants[:route_order_limit]

    for ordered_pairs in route_order_variants:
        attempt_board, routed_paths, failed_edges = route_order_once(ordered_pairs)
        width, height = attempt_board.computeLayoutArea()
        area = width * height if width > 0 and height > 0 else float("inf")
        direction_violations = compute_direction_violation_count(
            routed_paths,
            multi_output_nodes,
        )
        key = (
            len(failed_edges),
            direction_violations,
            area,
            max(width, height),
            width + height,
            sum(len(path) for path in routed_paths.values()),
        )
        if best_key is None or key < best_key:
            best_key = key
            best_payload = (attempt_board, routed_paths, failed_edges)
        if not failed_edges and direction_violations == 0:
            break

    if best_payload is not None and (
        best_payload[2]
        or compute_direction_violation_count(
            best_payload[1],
            multi_output_nodes,
        ) > 0
    ):
        alternate_orders = route_order_variants[
            :(
                min(DEFAULT_ALTERNATE_PHASE_POLICY_ORDERS, 1)
                if large_circuit
                else DEFAULT_ALTERNATE_PHASE_POLICY_ORDERS
            )
        ]
        for advance_cost, hold_cost in ((1, 1), (2, 1)):
            for ordered_pairs in alternate_orders:
                attempt_board, routed_paths, failed_edges = route_order_once(
                    ordered_pairs,
                    advance_cost=advance_cost,
                    hold_cost=hold_cost,
                )
                width, height = attempt_board.computeLayoutArea()
                area = width * height if width > 0 and height > 0 else float("inf")
                direction_violations = compute_direction_violation_count(
                    routed_paths,
                    multi_output_nodes,
                )
                key = (
                    len(failed_edges),
                    direction_violations,
                    area,
                    max(width, height),
                    width + height,
                    sum(len(path) for path in routed_paths.values()),
                )
                if best_key is None or key < best_key:
                    best_key = key
                    best_payload = (attempt_board, routed_paths, failed_edges)
                if not failed_edges and direction_violations == 0:
                    break
            if best_payload is not None and (
                not best_payload[2]
                and compute_direction_violation_count(
                    best_payload[1],
                    multi_output_nodes,
                ) == 0
            ):
                break

    if best_payload is not None and (
        best_payload[2]
        or compute_direction_violation_count(
            best_payload[1],
            multi_output_nodes,
        ) > 0
    ) and route_order_variants:
        for ordered_pairs in route_order_variants:
            attempt_board, routed_paths, failed_edges = route_order_once(
                ordered_pairs,
                combined_port_search=False,
            )
            width, height = attempt_board.computeLayoutArea()
            area = width * height if width > 0 and height > 0 else float("inf")
            direction_violations = compute_direction_violation_count(
                routed_paths,
                multi_output_nodes,
            )
            key = (
                len(failed_edges),
                direction_violations,
                area,
                max(width, height),
                width + height,
                sum(len(path) for path in routed_paths.values()),
            )
            if best_key is None or key < best_key:
                best_key = key
                best_payload = (attempt_board, routed_paths, failed_edges)
            if not failed_edges and direction_violations == 0:
                break

    def problem_edges(payload):
        edges = {
            (int(src), int(dst))
            for src, dst in payload[2]
        }
        edges.update(
            find_direction_violation_edges(
                payload[1],
                multi_output_nodes,
            )
        )
        return edges

    if (
        best_payload is not None
        and route_order_variants
        and DEFAULT_REPAIR_ORDER_ATTEMPTS > 0
    ):
        base_order = route_order_variants[0]
        repair_queue = []
        queued_orders = set()

        def queue_problem_orders(payload):
            problems = problem_edges(payload)
            if not 0 < len(problems) <= 24:
                return
            problem_order = [
                edge
                for edge in base_order
                if (int(edge[0]), int(edge[1])) in problems
            ]
            clean_order = [
                edge
                for edge in base_order
                if (int(edge[0]), int(edge[1])) not in problems
            ]
            candidates = (
                problem_order + clean_order,
                list(reversed(problem_order)) + clean_order,
                clean_order + problem_order,
                clean_order + list(reversed(problem_order)),
            )
            for repair_order in candidates:
                signature = tuple(
                    (int(src), int(dst)) for src, dst in repair_order
                )
                if signature in queued_orders:
                    continue
                queued_orders.add(signature)
                repair_queue.append(repair_order)

        queue_problem_orders(best_payload)
        repair_attempts = 0
        while (
            repair_queue
            and repair_attempts < (
                min(DEFAULT_REPAIR_ORDER_ATTEMPTS, 2)
                if large_circuit
                else DEFAULT_REPAIR_ORDER_ATTEMPTS
            )
        ):
            repair_order = repair_queue.pop(0)
            repair_attempts += 1
            attempt_board, routed_paths, failed_edges = route_order_once(repair_order)
            width, height = attempt_board.computeLayoutArea()
            area = width * height if width > 0 and height > 0 else float("inf")
            direction_violations = compute_direction_violation_count(
                routed_paths,
                multi_output_nodes,
            )
            key = (
                len(failed_edges),
                direction_violations,
                area,
                max(width, height),
                width + height,
                sum(len(path) for path in routed_paths.values()),
            )
            if best_key is None or key < best_key:
                best_key = key
                best_payload = (attempt_board, routed_paths, failed_edges)
                queue_problem_orders(best_payload)
            if not failed_edges and direction_violations == 0:
                break

    def reroute_problem_subset(source_payload, reroute_order, advance_cost, hold_cost):
        source_board, source_paths, _source_failed = source_payload
        reroute_set = {
            (int(src), int(dst)) for src, dst in reroute_order
        }
        attempt_board = create_board_with_positions(
            circuit,
            node_positions,
            common_first_input_phase=0,
            clock_field=clock_field,
        )
        routed_paths = {}
        incoming_directions = defaultdict(set)
        outgoing_directions = {}
        placed_by_source = set()

        for edge, path in source_paths.items():
            normalized_edge = (int(edge[0]), int(edge[1]))
            if normalized_edge in reroute_set:
                continue
            normalized_path = [
                (int(coord[0]), int(coord[1])) for coord in path
            ]
            for coord in normalized_path:
                phase = int(source_board.getPhase(coord))
                if phase >= 0 and attempt_board.getPhase(coord) < 0:
                    attempt_board.setPhase(coord, phase)
            for coord in normalized_path[1:-1]:
                occupancy_key = (normalized_edge[0], coord)
                if occupancy_key in placed_by_source:
                    continue
                attempt_board.placeWire(coord)
                placed_by_source.add(occupancy_key)
            attempt_board.savePath(normalized_edge, normalized_path)
            routed_paths[normalized_edge] = normalized_path
            out_dir = get_outgoing_direction(normalized_path)
            in_dir = get_incoming_direction(normalized_path)
            if out_dir is not None:
                record_output_direction(
                    outgoing_directions,
                    normalized_edge[0],
                    out_dir,
                    multi_output_nodes,
                )
            if in_dir is not None:
                incoming_directions[normalized_edge[1]].add(in_dir)

        failed_edges = []
        generic_router = iFCN_Lab.MapPhaseAStar(
            attempt_board,
            phase_cycle,
            padding,
            max_same_phase,
        )
        for src, dst in reroute_order:
            edge = (int(src), int(dst))
            path = route_single_edge_with_direction_constraints(
                attempt_board,
                generic_router,
                edge[0],
                edge[1],
                phase_cycle,
                padding,
                max_same_phase,
                orientation,
                incoming_directions,
                outgoing_directions,
                routed_paths=routed_paths,
                first_layer_inputs=first_input_nodes,
                combined_port_search=True,
                advance_cost=advance_cost,
                hold_cost=hold_cost,
                multi_output_nodes=multi_output_nodes,
                allow_relaxed_ports=False,
                allow_escape_routing=False,
                monotone_expansion_limit=(
                    min(DEFAULT_MONOTONE_ROUTER_EXPANSION_LIMIT, 2000)
                    if large_circuit and DEFAULT_MONOTONE_ROUTER_EXPANSION_LIMIT > 0
                    else 2000
                    if large_circuit
                    else DEFAULT_MONOTONE_ROUTER_EXPANSION_LIMIT
                ),
                flexible_expansion_limit=(
                    min(DEFAULT_FLEXIBLE_ROUTER_EXPANSION_LIMIT, 2500)
                    if large_circuit
                    else DEFAULT_FLEXIBLE_ROUTER_EXPANSION_LIMIT
                ),
            )
            if not path:
                failed_edges.append(edge)
                continue
            attempt_board.savePath(edge, path)
            routed_paths[edge] = path
            out_dir = get_outgoing_direction(path)
            in_dir = get_incoming_direction(path)
            if out_dir is not None:
                record_output_direction(
                    outgoing_directions,
                    edge[0],
                    out_dir,
                    multi_output_nodes,
                )
            if in_dir is not None:
                incoming_directions[edge[1]].add(in_dir)

        return attempt_board, routed_paths, failed_edges

    if (
        best_payload is not None
        and route_order_variants
        and DEFAULT_LOCAL_RIPUP_ATTEMPTS > 0
    ):
        core_problems = problem_edges(best_payload)
        problems = set(core_problems)
        if core_problems:
            problem_nodes = {
                int(node_id)
                for edge in core_problems
                for node_id in edge
            }
            blocker_priority = Counter()
            for src, dst in circuit.effective_edges:
                edge = (int(src), int(dst))
                if edge in core_problems:
                    continue
                blocker_priority[edge] += (
                    int(edge[0] in problem_nodes) +
                    int(edge[1] in problem_nodes)
                ) * 4

            endpoint_neighbors = set()
            for node_id in problem_nodes:
                coord = node_positions.get(int(node_id))
                if coord is None:
                    continue
                endpoint_neighbors.add((int(coord[0]), int(coord[1])))
                for direction in ALL_DIRECTIONS:
                    endpoint_neighbors.add(add_direction(coord, direction))
            for edge, path in best_payload[1].items():
                normalized_edge = (int(edge[0]), int(edge[1]))
                if normalized_edge in core_problems:
                    continue
                endpoint_overlap = sum(
                    (int(coord[0]), int(coord[1])) in endpoint_neighbors
                    for coord in path[1:-1]
                )
                blocker_priority[normalized_edge] += endpoint_overlap * 3

                for failed_src, failed_dst in best_payload[2]:
                    start = node_positions[int(failed_src)]
                    goal = node_positions[int(failed_dst)]
                    min_x = min(int(start[0]), int(goal[0])) - 1
                    max_x = max(int(start[0]), int(goal[0])) + 1
                    min_y = min(int(start[1]), int(goal[1])) - 1
                    max_y = max(int(start[1]), int(goal[1])) + 1
                    corridor_overlap = sum(
                        min_x <= int(coord[0]) <= max_x and
                        min_y <= int(coord[1]) <= max_y
                        for coord in path[1:-1]
                    )
                    blocker_priority[normalized_edge] += corridor_overlap

            if len(problems) <= 24:
                for edge, priority in sorted(
                    blocker_priority.items(),
                    key=lambda item: (-int(item[1]), int(item[0][0]), int(item[0][1])),
                ):
                    if priority <= 0 or len(problems) >= 24:
                        break
                    problems.add(edge)
        if 0 < len(problems) <= 24:
            base_order = route_order_variants[0]
            problem_order = [
                (int(src), int(dst))
                for src, dst in base_order
                if (int(src), int(dst)) in problems
            ]
            local_attempts = [
                (problem_order, 1, 2),
                (list(reversed(problem_order)), 1, 2),
                (problem_order, 1, 1),
                (list(reversed(problem_order)), 1, 1),
                (problem_order, 2, 1),
                (list(reversed(problem_order)), 2, 1),
            ][:(
                min(DEFAULT_LOCAL_RIPUP_ATTEMPTS, 3)
                if large_circuit
                else DEFAULT_LOCAL_RIPUP_ATTEMPTS
            )]
            source_payload = best_payload
            for reroute_order, advance_cost, hold_cost in local_attempts:
                attempt_board, routed_paths, failed_edges = reroute_problem_subset(
                    source_payload,
                    reroute_order,
                    advance_cost,
                    hold_cost,
                )
                width, height = attempt_board.computeLayoutArea()
                area = width * height if width > 0 and height > 0 else float("inf")
                direction_violations = compute_direction_violation_count(
                    routed_paths,
                    multi_output_nodes,
                )
                key = (
                    len(failed_edges),
                    direction_violations,
                    area,
                    max(width, height),
                    width + height,
                    sum(len(path) for path in routed_paths.values()),
                )
                if best_key is None or key < best_key:
                    best_key = key
                    best_payload = (attempt_board, routed_paths, failed_edges)
                if not failed_edges and direction_violations == 0:
                    break

    if best_payload is None:
        return board, {}, list(circuit.effective_edges)
    return best_payload


def compute_io_exposure_penalty(circuit, node_positions, board, orientation):
    return compute_io_edge_penalty(circuit, node_positions, board, orientation)


def compute_io_edge_penalty(circuit, node_positions, board, orientation):
    if not node_positions:
        return 10**6

    xs = [coord[0] for coord in node_positions.values()]
    ys = [coord[1] for coord in node_positions.values()]
    min_node_x = min(xs)
    max_node_x = max(xs)
    min_node_y = min(ys)
    max_node_y = max(ys)

    input_nodes = list(circuit.getInputNodesIndex)
    output_nodes = list(circuit.getOutputNodesIndex)
    if not input_nodes or not output_nodes:
        return 0

    if orientation == LEFT_RIGHT:
        input_penalty = 0
        for node in input_nodes:
            node_id = int(node)
            if node_id not in node_positions:
                continue
            input_penalty += max(0, node_positions[node_id][0] - min_node_x)

        output_penalty = 0
        for node in output_nodes:
            node_id = int(node)
            if node_id not in node_positions:
                continue
            output_penalty += max(0, max_node_x - node_positions[node_id][0])
        return input_penalty + output_penalty

    top_penalty = 0
    for node in input_nodes:
        node_id = int(node)
        if node_id not in node_positions:
            continue
        top_penalty += max(0, node_positions[node_id][1] - min_node_y)

    bottom_penalty = 0
    for node in output_nodes:
        node_id = int(node)
        if node_id not in node_positions:
            continue
        bottom_penalty += max(0, max_node_y - node_positions[node_id][1])

    return top_penalty + bottom_penalty


def compute_route_overhang_penalty(node_positions, board):
    if not node_positions:
        return 10**6

    min_x, min_y, max_x, max_y = board.findLayoutBoard()
    if max_x < min_x or max_y < min_y:
        return 10**6

    xs = [coord[0] for coord in node_positions.values()]
    ys = [coord[1] for coord in node_positions.values()]
    min_node_x = min(xs)
    max_node_x = max(xs)
    min_node_y = min(ys)
    max_node_y = max(ys)

    return (
        max(0, min_node_x - min_x) +
        max(0, max_x - max_node_x) +
        max(0, min_node_y - min_y) +
        max(0, max_y - max_node_y)
    )


def evaluate_layout_candidate(
    candidate,
    circuit,
    phase_cycle,
    padding,
    max_same_phase,
    embedding_scores=None,
):
    clock_field = candidate.get("clock_field")
    if clock_field is not None:
        if not isinstance(clock_field, ClockField):
            raise TypeError("candidate['clock_field'] must be a ClockField")
        if int(clock_field.spec.phase_count) != int(phase_cycle):
            raise ValueError(
                "clock field phase_count does not match routing phase_cycle: "
                f"{clock_field.spec.phase_count} != {phase_cycle}"
            )
        xs = [int(coord[0]) for coord in candidate["node_positions"].values()]
        ys = [int(coord[1]) for coord in candidate["node_positions"].values()]
        required_margin = max(0, int(padding))
        required_bounds = (
            min(xs) - required_margin,
            min(ys) - required_margin,
            max(xs) + required_margin,
            max(ys) + required_margin,
        )
        field_min_x, field_min_y, field_max_x, field_max_y = clock_field.bounds
        req_min_x, req_min_y, req_max_x, req_max_y = required_bounds
        if not (
            field_min_x <= req_min_x
            and field_min_y <= req_min_y
            and field_max_x >= req_max_x
            and field_max_y >= req_max_y
        ):
            raise ValueError(
                "clock field does not cover the placement plus routing padding: "
                f"field={clock_field.bounds}, required={required_bounds}"
            )

    board = create_board_with_positions(
        circuit,
        candidate["node_positions"],
        clock_field=clock_field,
    )
    board, routed_paths, failed_edges = route_edges_with_phase(
        board,
        circuit,
        phase_cycle,
        padding,
        max_same_phase,
        candidate["orientation"],
        embedding_scores=embedding_scores,
        clock_field=clock_field,
        edge_priorities=candidate.get("routing_edge_priorities"),
    )
    width, height = board.computeLayoutArea()
    area = width * height if width > 0 and height > 0 else float("inf")
    io_exposure_penalty = compute_io_edge_penalty(
        circuit,
        candidate["node_positions"],
        board,
        candidate["orientation"],
    )
    route_overhang_penalty = compute_route_overhang_penalty(candidate["node_positions"], board)
    multi_output_nodes = {
        int(node_id)
        for node_id in circuit.effective_nodes
        if str(circuit.getNodeTypeString(int(node_id))).lower()
        in {"input", "fanout"}
    }
    direction_violation_count = compute_direction_violation_count(
        routed_paths,
        multi_output_nodes,
    )
    return {
        "board": board,
        "node_positions": candidate["node_positions"],
        "routed_paths": routed_paths,
        "failed_edges": failed_edges,
        "x_spacing": candidate["x_spacing"],
        "y_spacing": candidate["y_spacing"],
        "layout_strategy": candidate["strategy"],
        "layout_orientation": candidate["orientation"],
        "routing_embedding_guidance": bool(
            candidate.get(
                "routing_embedding_guidance",
                candidate["strategy"] == "gcn",
            )
        ),
        "routing_edge_priority_guidance": bool(candidate.get("routing_edge_priorities")),
        "width": width,
        "height": height,
        "area": area,
        "io_exposure_penalty": io_exposure_penalty,
        "route_overhang_penalty": route_overhang_penalty,
        "direction_violation_count": direction_violation_count,
        "clock_field": clock_field,
        "clock_field_hash": clock_field.field_hash if clock_field is not None else "",
        "clock_scheme": "causal-random-field" if clock_field is not None else "dynamic-legacy",
    }


def select_best_layout(
    circuit,
    ordered_layers,
    board_margin,
    phase_cycle,
    padding,
    max_same_phase,
    base_x_spacing,
    base_y_spacing,
    embeddings=None,
    embedding_scores=None,
    allowed_strategies=None,
    allowed_orientations=None,
    local_refine_rounds=0,
    local_lookahead_depth=DEFAULT_LOCAL_LOOKAHEAD_DEPTH,
    local_beam_width=DEFAULT_LOCAL_BEAM_WIDTH,
    local_branch_width=DEFAULT_LOCAL_BRANCH_WIDTH,
    local_max_evaluations=0,
    extra_candidates=None,
    evaluation_fn=None,
    evaluation_stats=None,
):
    best_result = None
    if evaluation_fn is None:
        evaluation_fn = evaluate_layout_candidate

    candidate_pool = list(
        build_layout_candidates(
            circuit,
            ordered_layers,
            board_margin,
            board_margin,
            base_x_spacing,
            base_y_spacing,
            embeddings=embeddings,
            allowed_strategies=allowed_strategies,
            allowed_orientations=allowed_orientations,
        )
    )
    if extra_candidates:
        candidate_pool.extend(extra_candidates)
    if evaluation_stats is not None:
        evaluation_stats.clear()
        evaluation_stats.update(
            {
                "candidate_count": 0,
                "legal_count": 0,
                "timeout_count": 0,
                "failed_edge_count_sum": 0,
                "total_eval_sec": 0.0,
                "max_eval_sec": 0.0,
            }
        )

    for candidate in candidate_pool:
        eval_start = time.perf_counter()
        result = evaluation_fn(
            candidate,
            circuit,
            phase_cycle,
            padding,
            max_same_phase,
            embedding_scores=(
                embedding_scores
                if candidate.get(
                    "routing_embedding_guidance",
                    candidate["strategy"] == "gcn",
                )
                else None
            ),
        )
        eval_sec = float(time.perf_counter() - eval_start)
        result["candidate_eval_sec"] = eval_sec
        if evaluation_stats is not None:
            evaluation_stats["candidate_count"] += 1
            failed_edges = int(len(result.get("failed_edges", [])))
            evaluation_stats["failed_edge_count_sum"] += failed_edges
            if failed_edges == 0:
                evaluation_stats["legal_count"] += 1
            if bool(result.get("exact_evaluation_timeout", False)):
                evaluation_stats["timeout_count"] += 1
            evaluation_stats["total_eval_sec"] += eval_sec
            evaluation_stats["max_eval_sec"] = max(evaluation_stats["max_eval_sec"], eval_sec)
        score = layout_score_key(result)
        result["score"] = score
        if best_result is None or score < best_result["score"]:
            best_result = result

    if best_result is None:
        return None

    refined_result = refine_layout_with_local_compaction(
        best_result,
        circuit,
        ordered_layers,
        phase_cycle,
        padding,
        max_same_phase,
        guidance_embedding_scores=embedding_scores,
        evaluation_embedding_scores=(
            embedding_scores if best_result.get("routing_embedding_guidance", False) else None
        ),
        max_rounds=local_refine_rounds,
        lookahead_depth=local_lookahead_depth,
        beam_width=local_beam_width,
        branch_width=local_branch_width,
        max_evaluations=local_max_evaluations,
    )
    if refined_result["score"] < best_result["score"]:
        best_result = refined_result

    return best_result


def main():
    args = parse_args()
    benchmark_path = os.path.abspath(args.benchmark)
    output_dir = os.path.abspath(args.output_dir)

    if not os.path.exists(benchmark_path):
        raise FileNotFoundError(f"Benchmark not found: {benchmark_path}")

    set_global_seed(args.seed)
    os.makedirs(output_dir, exist_ok=True)

    circuit = CircuitParser(benchmark_path, parse_mode=args.parse_mode)
    embeddings, barycenter_opt_layers, crossings_per_layer, _ = load_or_generate_gcn_layout(
        circuit,
        benchmark_path,
        args.seed,
        use_cache=not args.disable_gcn_cache,
    )

    ordered_layers = normalize_layers(barycenter_opt_layers)
    embedding_scores = build_embedding_score_map(circuit, ordered_layers, embeddings)
    original_layer_map = build_layer_dict(circuit.layer_nodes, sort_each_layer=True)
    reordered_layer_map = build_layer_dict(ordered_layers)
    board_margin = args.board_margin if args.board_margin is not None else args.padding + 1
    memory_candidate = None
    if not args.disable_layout_memory:
        memory_candidate = load_layout_memory_candidate(circuit, benchmark_path, args.seed)
    memory_only = (
        memory_candidate is not None and
        args.layout_strategy == "auto" and
        args.layout_orientation == "auto"
    )
    effective_local_refine_rounds = 0 if memory_only else args.local_refine_rounds
    if memory_only:
        print("[Memory] Reusing stored layout as the only generated-candidate warm start.")

    route_start = time.perf_counter()
    layout_result_data = select_best_layout(
        circuit,
        ordered_layers,
        board_margin,
        args.phase_cycle,
        args.padding,
        args.max_same_phase,
        args.x_spacing,
        args.y_spacing,
        embeddings=embeddings,
        embedding_scores=embedding_scores,
        allowed_strategies=(
            ()
            if memory_only else
            (None if args.layout_strategy == "auto" else (args.layout_strategy,))
        ),
        allowed_orientations=(
            ()
            if memory_only else
            (None if args.layout_orientation == "auto" else (args.layout_orientation,))
        ),
        local_refine_rounds=effective_local_refine_rounds,
        local_lookahead_depth=args.local_lookahead_depth,
        local_beam_width=args.local_beam_width,
        local_branch_width=args.local_branch_width,
        local_max_evaluations=args.local_max_evaluations,
        extra_candidates=([memory_candidate] if memory_candidate is not None else None),
    )
    run_time_sec = time.perf_counter() - route_start
    if not args.disable_layout_memory:
        save_layout_memory(layout_result_data, benchmark_path, args.seed)
    board = layout_result_data["board"]
    node_positions = layout_result_data["node_positions"]
    routed_paths = layout_result_data["routed_paths"]
    failed_edges = layout_result_data["failed_edges"]
    selected_x_spacing = layout_result_data["x_spacing"]
    selected_y_spacing = layout_result_data["y_spacing"]
    selected_layout_strategy = layout_result_data["layout_strategy"]
    selected_layout_orientation = layout_result_data["layout_orientation"]
    direction_violation_count = layout_result_data["direction_violation_count"]

    file_stem = f"{os.path.splitext(circuit.fileName)[0]}_phase_layout"
    iFCN_Lab.MapChessboard.outputTexFile(board, file_stem, output_dir)
    visualize_layered_graph_sorted(
        circuit,
        original_layer_map,
        circuit.effective_edges,
        circuit.fileName,
        output_dir,
        file_suffix="_original_layers",
        title="Original Layering",
        verbose=False,
    )
    visualize_layered_graph_sorted(
        circuit,
        reordered_layer_map,
        circuit.effective_edges,
        circuit.fileName,
        output_dir,
        file_suffix="_reordered_layers",
        title="GCN + Barycenter Reordered Layering",
        verbose=False,
    )
    visualize_layered_graph_sorted(
        circuit,
        reordered_layer_map,
        circuit.effective_edges,
        circuit.fileName,
        output_dir,
        node_positions=node_positions,
        file_suffix="_phase_layout",
        verbose=False,
    )

    width, height = board.computeLayoutArea()
    min_x, min_y, max_x, max_y = board.findLayoutBoard()
    tex_path = os.path.join(output_dir, f"{file_stem}.tex")
    ifcn_path = os.path.join(output_dir, f"{file_stem}.ifcn")
    svg_path = os.path.join(output_dir, f"{file_stem}.svg")
    original_layers_svg_path = os.path.join(
        output_dir,
        f"{os.path.splitext(circuit.fileName)[0]}_original_layers.svg",
    )
    reordered_layers_svg_path = os.path.join(
        output_dir,
        f"{os.path.splitext(circuit.fileName)[0]}_reordered_layers.svg",
    )

    layout_result = LayoutOnlyResult(
        circuit,
        board,
        node_positions,
        width,
        height,
        routed_paths,
        failed_edges,
        selected_x_spacing,
        selected_y_spacing,
        selected_layout_strategy,
        run_time_sec,
    )
    generate_gate_level_mapping_file(
        layout_result,
        output_dir=output_dir,
        filename_stem=file_stem,
        phase_cycle=args.phase_cycle,
        verbose=False,
    )

    print("[Layout] Phase-aware routing completed.")
    print(f"[Layout] Circuit: {benchmark_path}")
    print(f"[Layout] Nodes placed: {len(node_positions)}")
    print(f"[Layout] Layers: {len(ordered_layers)}")
    print(f"[Layout] Crossings after GCN+barycenter ordering: {sum(crossings_per_layer.values())}")
    print(f"[Layout] Routed edges: {len(routed_paths)} / {len(routed_paths) + len(failed_edges)}")
    print(f"[Layout] Failed edges: {len(failed_edges)}")
    print(f"[Layout] Board size: {width} x {height}")
    print(f"[Layout] Board bbox: ({min_x}, {min_y}) -> ({max_x}, {max_y})")
    print(f"[Layout] Base spacing: horizontal={args.x_spacing}, vertical={args.y_spacing}")
    print(
        f"[Layout] Layout search request: strategy={args.layout_strategy}, "
        f"orientation={args.layout_orientation}"
    )
    print(f"[Layout] Board margin: {board_margin}")
    print(
        "[Layout] Router params: "
        f"phase_cycle={args.phase_cycle}, padding={args.padding}, "
        f"max_same_phase={args.max_same_phase}"
    )
    print(
        "[Layout] Selected layout: "
        f"strategy={selected_layout_strategy}, "
        f"orientation={selected_layout_orientation}, "
        f"x_spacing={selected_x_spacing}, y_spacing={selected_y_spacing}"
    )
    print(f"[Layout] Direction violations: {direction_violation_count}")
    print(f"[Layout] Routing runtime: {run_time_sec:.4f} s")
    print(f"[Layout] Original layering SVG written to: {original_layers_svg_path}")
    print(f"[Layout] Reordered layering SVG written to: {reordered_layers_svg_path}")
    print(f"[Layout] SVG written to: {svg_path}")
    print(f"[Layout] LaTeX written to: {tex_path}")
    print(f"[Layout] IFCN written to: {ifcn_path}")
    print(f"[Layout] Encoded IFCN written to: {os.path.join(output_dir, f'{file_stem}_encoded.ifcn')}")
    if failed_edges:
        print(f"[Layout] Failed edge list: {failed_edges}")


if __name__ == "__main__":
    main()
