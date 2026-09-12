from src.circuit_parse import CircuitParser  # ✅ 来自 src
from src.gcn_model_less_node import (
    visualize_strict_right_down,
    TDDwave_generate,
    normal_graph_generate_2ddwave,
    visualize_layered_graph_sorted,
    strict_right_down_layout_max_fanin_right,
)
import numpy as np
from sklearn.cluster import KMeans
from collections import Counter, defaultdict
import matplotlib.cm as cm
import matplotlib.colors as mcolors
import matplotlib.pyplot as plt
from lib import iFCN_Lab
import torch
import math
import os
import time
import itertools
import heapq

class NormalGraphDraw:
    def __init__(self, v_file_path, save_training_curve=True):

        self.parse = CircuitParser(v_file_path)
        self.data = self.parse.build_pyg_data()

        (
        self.embeddings, self.barycenter_opt_layers, self.crossings_per_layer, self.edges
        ) = normal_graph_generate_2ddwave(
             self.data,
             self.parse.layer_nodes,
             self.parse.effective_edges,
             self.parse.node_to_index,
             self.parse.filePath,
             save_training_curve=save_training_curve,
        )

        # Normal flow under the 2DDWave template:
        # route first, then verify template consistency and materialize template phases.
        self.mapChessboard = iFCN_Lab.MapChessboard()
        self.phase_cycle = 4
        self.routing_padding = 2
        self.max_same_phase = 0
        self.clock_scheme_name = "2DDWave"
        self._uses_right_down_astar = hasattr(iFCN_Lab, "RightDownAStar")
        if getattr(self, "_uses_right_down_astar", False):
            self.astar = iFCN_Lab.RightDownAStar(self.mapChessboard)
        else:
            self.astar = iFCN_Lab.MapPhaseAStar(
                self.mapChessboard,
                self.phase_cycle,
                self.routing_padding,
                self.max_same_phase,
            )
        self.device = "cuda" if torch.cuda.is_available() else "cpu"
        self._node_id_to_idx = {}
        self._node_coord = {}
        self._coord_set = set()
        self._coord_cache_dirty = True
        self.ml_tuning = {
            "ml_similarity_weight": 2.0,
            "ml_extra_shift_scale": 2.0,
            "ml_base_score": 1.0,
            "ml_max_nodes": 8,
            "ml_max_shift": 8,
            "global_ml_max_nodes": 12,
            "global_ml_max_shift": 6,
            "route_insert_max_ops": 24,
            "phase_insert_max_ops": 12,
        }
        self.route_expansion_rounds = max(
            1, int(os.environ.get("IFCN_ROUTE_EXPANSION_ROUNDS", "96"))
        )
        self.route_expansion_timeout_sec = max(
            1.0, float(os.environ.get("IFCN_ROUTE_EXPANSION_TIMEOUT", "240"))
        )
        self.template_expansion_rounds = max(
            1, int(os.environ.get("IFCN_TEMPLATE_EXPANSION_ROUNDS", "24"))
        )
        self.route_expansion_history = []
        self.route_expansion_exhausted = False
        self.route_incompatibility_reason = ""
        self.right_down_port_capacity_nodes = self._right_down_port_capacity_nodes()
        self._last_route_priority = set()
        self._last_route_reverse_priority = False
        self._last_route_reverse_remaining = False
        self._last_route_explicit_priority = tuple()
        self.contraction_history = []
        self.contraction_evaluations = 0
        self.contraction_global_evaluations = 0
        self.contraction_recursive_evaluations = 0
        self.contraction_empty_line_evaluations = 0
        self.contraction_layer_merge_evaluations = 0
        self.contraction_exhausted = False
        self.contraction_runtime_sec = 0.0
        self.clock_template_ok = False
        self.clock_template_conflict_count = 0
        self.phase_assignment_ok = False
        self.phase_conflict_count = 0
        self.last_failed_pairs = {}
        self._stage_snapshot_counter = 0
        self.layers_per_row = max(
            1, int(os.environ.get("IFCN_LAYERS_PER_ROW", "1"))
        )
        self.fold_strategy = os.environ.get(
            "IFCN_FOLD_STRATEGY", "path"
        ).strip().lower()
        if self.fold_strategy not in {"path", "layer", "milp", "grid"}:
            raise ValueError(
                "IFCN_FOLD_STRATEGY must be 'path', 'layer', 'milp', or 'grid'"
            )
        self.transpose_initial_placement = os.environ.get(
            "IFCN_TRANSPOSE_INITIAL_PLACEMENT", "0"
        ).strip().lower() in {"1", "true", "yes", "on"}
        self.right_launch_fallback = os.environ.get(
            "IFCN_RIGHT_LAUNCH_FALLBACK", "1"
        ).strip().lower() not in {"0", "false", "no", "off"}
        self.skip_port_reservation = os.environ.get(
            "IFCN_SKIP_PORT_RESERVATION", "0"
        ).strip().lower() in {"1", "true", "yes", "on"}
        self.fanin_port_assignment = os.environ.get(
            "IFCN_FANIN_PORT_ASSIGNMENT", "exclusive"
        ).strip().lower()
        if self.fanin_port_assignment not in {"exclusive", "legacy"}:
            raise ValueError(
                "IFCN_FANIN_PORT_ASSIGNMENT must be 'exclusive' or 'legacy'"
            )

    def _sync_legacy_phase_status(self):
        # Backward compatibility for older scripts that still read phase_* fields.
        self.phase_assignment_ok = bool(self.clock_template_ok)
        self.phase_conflict_count = int(self.clock_template_conflict_count)

    def get_congestion_matrix(self):
        """
        返回当前布局区域的拥塞矩阵（0~1）与边界:
        congestion = (MAX_CAPACITY - current_capacity) / MAX_CAPACITY
        """
        min_x, min_y, max_x, max_y = self.mapChessboard.findLayoutBoard()
        if min_x == -1:
            used = self._collect_used_coords()
            if not used:
                return None, None
            xs = [p[0] for p in used]
            ys = [p[1] for p in used]
            min_x, max_x = min(xs), max(xs)
            min_y, max_y = min(ys), max(ys)

        width = max_x - min_x + 1
        height = max_y - min_y + 1
        if width <= 0 or height <= 0:
            return None, None

        max_cap = float(getattr(iFCN_Lab, "MAX_CELL_CAPACITY", 5))
        mat = np.zeros((height, width), dtype=np.float32)
        for y in range(height):
            for x in range(width):
                gx, gy = min_x + x, min_y + y
                cell = self.mapChessboard.gridMap.get((gx, gy))
                cap = max_cap if cell is None else float(cell.getCapacity())
                mat[y, x] = max(0.0, min(1.0, (max_cap - cap) / max_cap))
        return mat, (min_x, min_y, max_x, max_y)

    def save_congestion_heatmap(self, save_path, title=None):
        mat, _ = self.get_congestion_matrix()
        if mat is None:
            return False

        os.makedirs(os.path.dirname(save_path), exist_ok=True)
        fig, ax = plt.subplots(figsize=(7, 4.8))
        im = ax.imshow(mat, origin="lower", cmap="hot", vmin=0.0, vmax=1.0)
        cbar = plt.colorbar(im, ax=ax, fraction=0.046, pad=0.04)
        cbar.set_label("congestion ratio", fontsize=10)
        ax.set_xlabel("x")
        ax.set_ylabel("y")
        if title:
            ax.set_title(title)
        fig.tight_layout()
        fig.savefig(save_path, dpi=220, bbox_inches="tight")
        plt.close(fig)
        return True

    def _record_failed_pairs(self, failed_pairs):
        if not failed_pairs:
            self.last_failed_pairs = {}
            return
        self.last_failed_pairs = {
            (int(src), int(dst)): value
            for (src, dst), value in failed_pairs.items()
        }

    def _iter_failed_pairs(self, failed_pairs):
        pairs = []
        for src, dst in (failed_pairs or {}).keys():
            pairs.append((int(src), int(dst)))
        pairs.sort()
        return pairs

    def _highlight_failed_endpoints_in_tex(self, tex_path, failed_pairs):
        if not os.path.exists(tex_path):
            return

        with open(tex_path, "r", encoding="utf-8") as f:
            lines = f.readlines()

        begin_marker = "% failed endpoint highlights begin"
        end_marker = "% failed endpoint highlights end"

        cleaned = []
        skipping = False
        for line in lines:
            marker = line.strip()
            if marker == begin_marker:
                skipping = True
                continue
            if marker == end_marker:
                skipping = False
                continue
            if not skipping:
                cleaned.append(line)
        lines = cleaned

        pairs = self._iter_failed_pairs(failed_pairs)
        if not pairs:
            with open(tex_path, "w", encoding="utf-8") as f:
                f.writelines(lines)
            return

        palette = [
            "red!85!black",
            "blue!85!black",
            "green!70!black",
            "cyan!80!black",
            "magenta!80!black",
            "yellow!80!black",
            "red!50!blue",
            "red!50!cyan",
            "green!50!blue",
            "red!50!yellow",
            "blue!50!yellow",
            "green!50!magenta",
        ]

        node_rings = defaultdict(list)
        for idx, (src, dst) in enumerate(pairs):
            color = palette[idx % len(palette)]
            node_rings[src].append((idx, color))
            node_rings[dst].append((idx, color))

        overlay = [begin_marker]
        for idx, (src, dst) in enumerate(pairs):
            color = palette[idx % len(palette)]
            overlay.append(f"% pair#{idx}: {src}->{dst}, color={color}")

        for nid in sorted(node_rings.keys()):
            for ring_idx, (_, color) in enumerate(sorted(node_rings[nid], key=lambda x: x[0])):
                ring_size = 0.95 + 0.10 * ring_idx
                overlay.append(
                    f"\\node[circle, draw={color}, line width=1.1pt, minimum size={ring_size:.2f}cm, inner sep=0pt] at ({nid}) {{}};"
                )
        overlay.append(end_marker)

        insert_idx = None
        for i, line in enumerate(lines):
            if line.strip() == "\\end{tikzpicture}":
                insert_idx = i
                break
        if insert_idx is None:
            with open(tex_path, "w", encoding="utf-8") as f:
                f.writelines(lines)
            return

        block = [item + "\n" for item in overlay]
        lines = lines[:insert_idx] + block + lines[insert_idx:]
        with open(tex_path, "w", encoding="utf-8") as f:
            f.writelines(lines)

    def _snapshot_stage_tex(self, snapshot_dir, stage_name, failed_pairs=None):
        if not snapshot_dir:
            return
        os.makedirs(snapshot_dir, exist_ok=True)
        idx = int(self._stage_snapshot_counter)
        safe_stage = "".join(
            ch if ch.isalnum() or ch in {"_", "-", "."} else "_"
            for ch in str(stage_name)
        )
        circuit_stem = os.path.splitext(self.parse.fileName)[0]
        filename = f"{circuit_stem}_{idx:02d}_{safe_stage}"
        self._stage_snapshot_counter += 1
        iFCN_Lab.MapChessboard.outputTexFile(self.mapChessboard, filename, snapshot_dir)
        tex_path = os.path.join(snapshot_dir, f"{filename}.tex")
        self._highlight_failed_endpoints_in_tex(tex_path, failed_pairs)

    # Backward-compatible alias: stage snapshots now export tex layouts instead of heatmaps.
    def _snapshot_stage_heatmap(self, snapshot_dir, stage_name, failed_pairs=None):
        self._snapshot_stage_tex(snapshot_dir, stage_name, failed_pairs=failed_pairs)

    def _normalize_layers(self, layers):
        normalized = {}
        if isinstance(layers, dict):
            for layer_idx in sorted(layers.keys()):
                normalized[int(layer_idx)] = [int(node) for node in layers[layer_idx]]
            return normalized
        for layer_idx, nodes in enumerate(layers):
            normalized[int(layer_idx)] = [int(node) for node in nodes]
        return normalized

    def _build_layers_from_current_coords(self):
        self._ensure_coord_cache()
        layers = defaultdict(list)
        for node_id, (x, y) in self._node_coord.items():
            layers[int(y)].append((int(x), int(node_id)))
        if not layers:
            return {}

        ordered = {}
        for idx, y in enumerate(sorted(layers.keys())):
            ordered[idx] = [node_id for _, node_id in sorted(layers[y])]
        return ordered


    def show_circuit_figure(self, dir_path = "./results/normal/", include_raw_layout=True, include_physical_layout=True):
        if include_raw_layout:
            visualize_layered_graph_sorted(
                self.parse,
                self._normalize_layers(self.parse.layer_nodes),
                self.parse.effective_edges,
                self.parse.fileName,
                dir_path,
                file_suffix="_raw",
            )

        visualize_layered_graph_sorted(
            self.parse,
            self._normalize_layers(self.barycenter_opt_layers),
            self.edges,
            self.parse.fileName,
            dir_path,
        )
        if not include_physical_layout:
            return

        current_layers = self._build_layers_from_current_coords()
        if not current_layers:
            return

        visualize_layered_graph_sorted(
            self.parse,
            current_layers,
            self.edges,
            self.parse.fileName,
            dir_path,
            node_positions=self._node_coord,
            file_suffix="_physical",
        )

    def set_ml_tuning(self, **kwargs):
        for key, value in kwargs.items():
            if key in self.ml_tuning:
                self.ml_tuning[key] = value
        return dict(self.ml_tuning)

    def _refresh_coord_cache(self):
        coords_cpu = self.coords.detach().cpu().tolist()
        node_ids_cpu = self.node_ids.detach().cpu().tolist()
        self._node_coord = {}
        self._coord_set = set()
        for i, nid in enumerate(node_ids_cpu):
            x, y = coords_cpu[i]
            point = (int(x), int(y))
            nid_int = int(nid)
            self._node_coord[nid_int] = point
            self._coord_set.add(point)
        self._coord_cache_dirty = False

    def _ensure_coord_cache(self):
        if self._coord_cache_dirty:
            self._refresh_coord_cache()

    def caculate_rough_placement(self):
        """
        返回所有节点的粗略位置, 用GPU张量存储 (x, y)，整数坐标，不再放置到网格
        """
        coords = []   # 存 (x, y)
        node_ids = [] # 保留 node 编号，方便之后查找

        layer_gap = 1
        # 使用右下可达约束初排：优先保证 route 可达性，再局部优化交叉和面积。
        layer_nodes = [list(self.barycenter_opt_layers[i]) for i in range(len(self.barycenter_opt_layers))]
        sorted_layers, x_pos = strict_right_down_layout_max_fanin_right(
            layer_nodes,
            self.edges,
            embeddings=None,
        )
        self.route_sorted_layers = sorted_layers
        self.route_x_pos = x_pos

        layer_keys = sorted(sorted_layers.keys())
        if self.fold_strategy == "grid":
            # For small networks, solve both placement dimensions together
            # instead of deciding rows first.  The model is topology-only and
            # circuit-independent; the routed result is still accepted only
            # after the regular A* and 2DDWave legality checks.
            from scipy.optimize import Bounds, LinearConstraint, milp
            from scipy.sparse import lil_matrix

            target_width = max(
                1, int(os.environ.get("IFCN_GRID_TARGET_WIDTH", "0"))
            )
            target_height = max(
                1, int(os.environ.get("IFCN_GRID_TARGET_HEIGHT", "0"))
            )
            if target_width <= 1 or target_height <= 1:
                raise ValueError(
                    "grid placement requires IFCN_GRID_TARGET_WIDTH and "
                    "IFCN_GRID_TARGET_HEIGHT greater than one"
                )

            all_nodes = sorted(
                int(node)
                for layer_index in layer_keys
                for node in sorted_layers[layer_index]
            )
            node_index = {node: index for index, node in enumerate(all_nodes)}
            all_edges = [
                (int(src), int(dst))
                for src, dst in self.edges
                if int(src) in node_index and int(dst) in node_index
            ]
            node_count = len(all_nodes)
            x_offset = 0
            y_offset = node_count
            pair_offset = 2 * node_count
            node_pairs = list(itertools.combinations(all_nodes, 2))
            fanins = defaultdict(list)
            fanouts = defaultdict(list)
            for src, dst in all_edges:
                fanins[dst].append(src)
                fanouts[src].append(dst)
            two_fanin_nodes = sorted(
                node for node, inputs in fanins.items() if len(inputs) == 2
            )
            port_offset = pair_offset + 4 * len(node_pairs)
            high_fanout_inputs = sorted(
                node
                for node in all_nodes
                if self.parse.get_node_type(node) == iFCN_Lab.NodeType.Input
                and len(fanouts[node]) > 1
            )
            grid_prefer_bend_free_reconvergence = os.environ.get(
                "IFCN_GRID_PREFER_BEND_FREE_RECONVERGENCE", "0"
            ).strip().lower() in {"1", "true", "yes", "on"}
            grid_force_io_boundary = os.environ.get(
                "IFCN_GRID_FORCE_IO_BOUNDARY", "1"
            ).strip().lower() not in {"0", "false", "no", "off"}
            reserved_launch_pairs = [
                (source, node)
                for source in high_fanout_inputs
                for node in all_nodes
                if node != source
            ]
            reserved_launch_offset = port_offset + len(two_fanin_nodes)
            variable_count = reserved_launch_offset + 4 * len(reserved_launch_pairs)

            objective = np.zeros(variable_count, dtype=np.float64)
            for src, dst in all_edges:
                objective[x_offset + node_index[dst]] += 1.0
                objective[x_offset + node_index[src]] -= 1.0
                objective[y_offset + node_index[dst]] += 1.0
                objective[y_offset + node_index[src]] -= 1.0
            # Stable tie breakers favor the upper-left without changing the
            # primary total-Manhattan-length objective.
            for position, node in enumerate(all_nodes):
                objective[x_offset + node_index[node]] += 1.0e-4
                objective[y_offset + node_index[node]] += 1.0e-5

            lower_bounds = np.zeros(variable_count, dtype=np.float64)
            upper_bounds = np.ones(variable_count, dtype=np.float64)
            upper_bounds[x_offset:y_offset] = float(target_width - 1)
            upper_bounds[y_offset:pair_offset] = float(target_height - 1)
            integrality = np.ones(variable_count, dtype=np.int32)

            constraint_terms = []
            constraint_lowers = []
            constraint_uppers = []

            def add_constraint(terms, lower=-np.inf, upper=np.inf):
                constraint_terms.append(dict(terms))
                constraint_lowers.append(float(lower))
                constraint_uppers.append(float(upper))

            # Every logical edge must be monotonically right/down and span at
            # least one tile.  This is precisely the geometric precondition
            # used by the production router.
            for src, dst in all_edges:
                sx = x_offset + node_index[src]
                sy = y_offset + node_index[src]
                dx = x_offset + node_index[dst]
                dy = y_offset + node_index[dst]
                add_constraint({dx: 1.0, sx: -1.0}, lower=0.0)
                add_constraint({dy: 1.0, sy: -1.0}, lower=0.0)
                if (
                    len(fanouts[src]) > 1
                    and self.parse.get_node_type(src) == iFCN_Lab.NodeType.Input
                ):
                    # Boundary inputs are vertically ordered, so an adjacent
                    # input can occupy the down launch.  Start every branch at
                    # least one column to the right; the router may then share
                    # that prefix before turning down.
                    add_constraint({dx: 1.0, sx: -1.0}, lower=2.0)
                    inverter_children = sorted(
                        child
                        for child in fanouts[src]
                        if self.parse.get_node_type(child) == iFCN_Lab.NodeType.Not
                    )
                    if dst in inverter_children and (
                        len(inverter_children) == 1 or dst != inverter_children[0]
                    ):
                        # Distribute duplicated polarity branches over both
                        # launch axes.  A sole inverter takes the lower branch;
                        # with several copies the first may stay horizontal and
                        # the remaining copies move below the source.
                        add_constraint({dy: 1.0, sy: -1.0}, lower=1.0)
                add_constraint(
                    {dx: 1.0, sx: -1.0, dy: 1.0, sy: -1.0},
                    # A branching source needs at least one wire tile before
                    # any child gate so its routes can share a prefix and
                    # split without consuming a gate location.
                    lower=2.0 if len(fanouts[src]) > 1 else 1.0,
                )

            # Select one of four separating axes for every pair, which is a
            # compact all-different formulation for grid coordinates.
            x_big_m = float(target_width)
            y_big_m = float(target_height)
            for pair_position, (left_node, right_node) in enumerate(node_pairs):
                base = pair_offset + 4 * pair_position
                left_x = x_offset + node_index[left_node]
                left_y = y_offset + node_index[left_node]
                right_x = x_offset + node_index[right_node]
                right_y = y_offset + node_index[right_node]
                add_constraint(
                    {left_x: 1.0, right_x: -1.0, base: x_big_m},
                    upper=x_big_m - 1.0,
                )
                add_constraint(
                    {right_x: 1.0, left_x: -1.0, base + 1: x_big_m},
                    upper=x_big_m - 1.0,
                )
                add_constraint(
                    {left_y: 1.0, right_y: -1.0, base + 2: y_big_m},
                    upper=y_big_m - 1.0,
                )
                add_constraint(
                    {right_y: 1.0, left_y: -1.0, base + 3: y_big_m},
                    upper=y_big_m - 1.0,
                )
                add_constraint(
                    {base + direction: 1.0 for direction in range(4)},
                    lower=1.0,
                    upper=1.0,
                )

            # Two-input gates need one net that can reach the left port and
            # one that can reach the top port.  The binary variable chooses
            # which fanin serves which port; an edge may have extra clearance
            # in the other dimension as well.
            for port_position, node in enumerate(two_fanin_nodes):
                first, second = sorted(fanins[node])
                choice = port_offset + port_position
                dx = x_offset + node_index[node]
                dy = y_offset + node_index[node]
                first_x = x_offset + node_index[first]
                first_y = y_offset + node_index[first]
                second_x = x_offset + node_index[second]
                second_y = y_offset + node_index[second]
                add_constraint({dx: 1.0, first_x: -1.0, choice: -1.0}, lower=0.0)
                add_constraint({dy: 1.0, second_y: -1.0, choice: -1.0}, lower=0.0)
                add_constraint({dx: 1.0, second_x: -1.0, choice: 1.0}, lower=1.0)
                add_constraint({dy: 1.0, first_y: -1.0, choice: 1.0}, lower=1.0)
                if (
                    grid_prefer_bend_free_reconvergence
                    and
                    self.parse.get_node_type(first) != iFCN_Lab.NodeType.Input
                    and self.parse.get_node_type(second) != iFCN_Lab.NodeType.Input
                ):
                    # This optional objective-shaped restriction prefers an
                    # already-aligned reconvergence.  It is not a design rule:
                    # compact valid layouts may need a bend before a sink port,
                    # and the production router validates those candidates.
                    add_constraint(
                        {dy: 1.0, first_y: -1.0, choice: float(target_height)},
                        upper=float(target_height),
                    )
                    add_constraint(
                        {dx: 1.0, second_x: -1.0, choice: float(target_width)},
                        upper=float(target_width),
                    )
                    add_constraint(
                        {dx: 1.0, first_x: -1.0, choice: -float(target_width)},
                        upper=0.0,
                    )
                    add_constraint(
                        {dy: 1.0, second_y: -1.0, choice: -float(target_height)},
                        upper=0.0,
                    )

            for reservation_position, (source, node) in enumerate(
                reserved_launch_pairs
            ):
                base = reserved_launch_offset + 4 * reservation_position
                node_x = x_offset + node_index[node]
                node_y = y_offset + node_index[node]
                source_y = y_offset + node_index[source]
                # A primary input is fixed on x=0.  The four alternatives say
                # that every other node is on x=0, on x>=2, above the source,
                # or below the source.  Therefore (1, source_y), the first
                # right-launch wire tile, cannot contain a gate.
                add_constraint(
                    {node_x: 1.0, base: float(target_width)},
                    upper=float(target_width),
                )
                add_constraint(
                    {node_x: 1.0, base + 1: -float(target_width)},
                    lower=float(2 - target_width),
                )
                add_constraint(
                    {
                        node_y: 1.0,
                        source_y: -1.0,
                        base + 2: float(target_height),
                    },
                    upper=float(target_height - 1),
                )
                add_constraint(
                    {
                        source_y: 1.0,
                        node_y: -1.0,
                        base + 3: float(target_height),
                    },
                    upper=float(target_height - 1),
                )
                add_constraint(
                    {base + direction: 1.0 for direction in range(4)},
                    lower=1.0,
                    upper=1.0,
                )

            input_nodes = [
                int(node) for node in self.parse.getInputNodesIndex
                if int(node) in node_index
            ]
            output_nodes = [
                int(node) for node in self.parse.getOutputNodesIndex
                if int(node) in node_index
            ]
            if grid_force_io_boundary:
                for node in input_nodes:
                    add_constraint(
                        {x_offset + node_index[node]: 1.0}, lower=0.0, upper=0.0
                    )
                for node in output_nodes:
                    add_constraint(
                        {x_offset + node_index[node]: 1.0},
                        lower=float(target_width - 1),
                        upper=float(target_width - 1),
                    )

            # Use the output-cone DFS solely to define a stable, topology-based
            # boundary order for primary inputs.  It avoids needless crossings
            # while leaving their exact rows to the solver.
            input_set = set(input_nodes)
            input_order = []
            seen_inputs = set()
            visited = set()

            def visit_cone(node):
                node = int(node)
                if node in input_set:
                    if node not in seen_inputs:
                        seen_inputs.add(node)
                        input_order.append(node)
                    return
                if node in visited:
                    return
                visited.add(node)
                for fanin in fanins.get(node, ()):
                    visit_cone(fanin)

            for output_node in sorted(output_nodes):
                visit_cone(output_node)
            input_order.extend(
                node for node in sorted(input_set) if node not in seen_inputs
            )
            input_block_size = max(
                0, int(os.environ.get("IFCN_GRID_INPUT_BLOCK_SIZE", "0"))
            )
            input_block_gap = max(
                0, int(os.environ.get("IFCN_GRID_INPUT_BLOCK_GAP", "0"))
            )
            for input_position, (upper_input, lower_input) in enumerate(
                zip(input_order, input_order[1:]), start=1
            ):
                minimum_gap = 1
                if input_block_size > 0 and input_position % input_block_size == 0:
                    minimum_gap += input_block_gap
                add_constraint(
                    {
                        y_offset + node_index[lower_input]: 1.0,
                        y_offset + node_index[upper_input]: -1.0,
                    },
                    lower=float(minimum_gap),
                )

            constraint_matrix = lil_matrix(
                (len(constraint_terms), variable_count), dtype=np.float64
            )
            for row_index, terms in enumerate(constraint_terms):
                for variable, coefficient in terms.items():
                    constraint_matrix[row_index, variable] = coefficient
            result = milp(
                c=objective,
                integrality=integrality,
                bounds=Bounds(lower_bounds, upper_bounds),
                constraints=LinearConstraint(
                    constraint_matrix.tocsr(),
                    np.asarray(constraint_lowers, dtype=np.float64),
                    np.asarray(constraint_uppers, dtype=np.float64),
                ),
                options={
                    "time_limit": float(os.environ.get("IFCN_GRID_TIMEOUT", "60")),
                    "mip_rel_gap": 0.0,
                },
            )
            if not result.success or result.x is None:
                raise RuntimeError(
                    "grid placement failed for {}x{}: {}".format(
                        target_width, target_height, result.message
                    )
                )

            for node in all_nodes:
                coords.append(
                    [
                        int(round(result.x[x_offset + node_index[node]])),
                        int(round(result.x[y_offset + node_index[node]])),
                    ]
                )
                node_ids.append(node)
        elif self.layers_per_row <= 1:
            if self.transpose_initial_placement:
                node_layer = {
                    int(node): int(layer_index)
                    for layer_index in layer_keys
                    for node in sorted_layers[layer_index]
                }
                fanins = defaultdict(list)
                for src, dst in self.edges:
                    src, dst = int(src), int(dst)
                    if src in node_layer and dst in node_layer:
                        fanins[dst].append(src)

                # Order primary inputs by first encounter in a stable
                # depth-first traversal of the output cones.  This keeps the
                # leaves of each logic subtree contiguous in a vertical
                # orientation (mux/reduction trees in particular) without any
                # circuit-name knowledge.
                input_set = {
                    int(node) for node in self.parse.getInputNodesIndex
                    if int(node) in node_layer
                }
                input_order = []
                seen_inputs = set()
                visited = set()

                def visit_cone(node):
                    node = int(node)
                    if node in input_set:
                        if node not in seen_inputs:
                            seen_inputs.add(node)
                            input_order.append(node)
                        return
                    if node in visited:
                        return
                    visited.add(node)
                    for fanin in fanins.get(node, ()):
                        visit_cone(fanin)

                for output_node in sorted(self.parse.getOutputNodesIndex):
                    if int(output_node) in node_layer:
                        visit_cone(int(output_node))
                for input_node in sorted(input_set):
                    if input_node not in seen_inputs:
                        input_order.append(input_node)

                input_block_size = max(
                    0, int(os.environ.get("IFCN_TRANSPOSE_INPUT_BLOCK_SIZE", "0"))
                )
                input_block_gap = max(
                    0, int(os.environ.get("IFCN_TRANSPOSE_INPUT_BLOCK_GAP", "1"))
                )
                placed = {}
                for position, node in enumerate(input_order):
                    block_padding = (
                        (position // input_block_size) * input_block_gap
                        if input_block_size > 0
                        else 0
                    )
                    placed[node] = (0, int(position + block_padding))
                for layer_index in layer_keys:
                    layer = [
                        int(node) for node in sorted_layers[layer_index]
                        if int(node) not in input_set
                    ]
                    if not layer:
                        continue
                    layer.sort(
                        key=lambda node: (
                            max(
                                [placed[fanin][1] for fanin in fanins.get(node, ()) if fanin in placed]
                                or [0]
                            ),
                            sum(
                                placed[fanin][1]
                                for fanin in fanins.get(node, ())
                                if fanin in placed
                            ),
                            int(x_pos[node]),
                            node,
                        )
                    )
                    next_y = 0
                    for node in layer:
                        upstream_y = [
                            placed[fanin][1]
                            for fanin in fanins.get(node, ())
                            if fanin in placed
                        ]
                        y_value = max([next_y] + upstream_y)
                        if sum(1 for value in upstream_y if value == y_value) > 1:
                            y_value += 1
                        placed[node] = (int(layer_index), int(y_value))
                        next_y = int(y_value) + 1

                for layer_idx in layer_keys:
                    for node in sorted_layers[layer_idx]:
                        x, y_value = placed[int(node)]
                        coords.append([int(x), int(y_value)])
                        node_ids.append(node)
            else:
                for layer_idx in layer_keys:
                    y = layer_idx * layer_gap
                    for node in sorted_layers[layer_idx]:
                        x = int(x_pos[node])
                        coords.append([int(x), int(y)])
                        node_ids.append(node)
        elif self.fold_strategy == "layer":
            # Fast layer-packing candidate.  This orientation is useful for
            # broad reduction trees, where several consecutive logical layers
            # can share a physical row while each gate retains at most one
            # horizontal fanin.
            node_layer = {
                int(node): int(layer_index)
                for layer_index in layer_keys
                for node in sorted_layers[layer_index]
            }
            fanins = defaultdict(list)
            fanouts = defaultdict(list)
            for src, dst in self.edges:
                src, dst = int(src), int(dst)
                if src not in node_layer or dst not in node_layer:
                    continue
                fanins[dst].append(src)
                fanouts[src].append(dst)

            physical_row_by_layer = {}
            current_row = 0
            layers_in_current_row = 0
            for layer_index in layer_keys:
                can_share = (
                    layers_in_current_row > 0
                    and layers_in_current_row < self.layers_per_row
                )
                if can_share:
                    for node in sorted_layers[layer_index]:
                        same_row_fanins = sum(
                            1
                            for fanin in fanins.get(int(node), ())
                            if physical_row_by_layer.get(node_layer[fanin])
                            == current_row
                        )
                        if same_row_fanins > 1:
                            can_share = False
                            break
                if not can_share and layers_in_current_row > 0:
                    current_row += 1
                    layers_in_current_row = 0
                physical_row_by_layer[layer_index] = current_row
                layers_in_current_row += 1

            nodes_by_row = defaultdict(list)
            for layer_index in layer_keys:
                row = physical_row_by_layer[layer_index]
                nodes_by_row[row].extend(
                    int(node) for node in sorted_layers[layer_index]
                )

            placed = {}
            for physical_row in sorted(nodes_by_row):
                row_nodes = list(nodes_by_row[physical_row])
                row_set = set(row_nodes)
                same_row_indegree = {
                    node: sum(
                        1 for fanin in fanins.get(node, ()) if fanin in row_set
                    )
                    for node in row_nodes
                }
                ready = []
                for node in row_nodes:
                    if same_row_indegree[node] == 0:
                        heapq.heappush(
                            ready,
                            (int(x_pos[node]), node_layer[node], int(node)),
                        )
                scheduled = []
                while ready:
                    _, _, node = heapq.heappop(ready)
                    scheduled.append(node)
                    for fanout in fanouts.get(node, ()):
                        if fanout not in row_set:
                            continue
                        same_row_indegree[fanout] -= 1
                        if same_row_indegree[fanout] == 0:
                            heapq.heappush(
                                ready,
                                (
                                    int(x_pos[fanout]),
                                    node_layer[fanout],
                                    int(fanout),
                                ),
                            )
                if len(scheduled) != len(row_nodes):
                    raise RuntimeError("layer-folded 2DDWave row contains a dependency cycle")

                next_x = 0
                for node in scheduled:
                    upstream_x = [
                        placed[fanin][0]
                        for fanin in fanins.get(node, ())
                        if fanin in placed and placed[fanin][1] < physical_row
                    ]
                    x = max([next_x] + upstream_x)
                    placed[node] = (int(x), int(physical_row))
                    next_x = int(x) + 1

            for layer_idx in layer_keys:
                for node in sorted_layers[layer_idx]:
                    x, y = placed[int(node)]
                    if self.transpose_initial_placement:
                        x, y = y, x
                    coords.append([int(x), int(y)])
                    node_ids.append(node)
        elif self.fold_strategy == "milp":
            # Solve the row-folding problem exactly.  z_e is one precisely
            # when edge e is horizontal.  The row-difference constraints plus
            # one-horizontal-fanin/one-horizontal-fanout capacities produce
            # disjoint same-row paths while preventing cycles in the quotient
            # dependency graph.  Primary inputs remain on row zero.
            from scipy.optimize import Bounds, LinearConstraint, milp
            from scipy.sparse import lil_matrix

            all_nodes = sorted(
                int(node)
                for layer_index in layer_keys
                for node in sorted_layers[layer_index]
            )
            node_index = {node: index for index, node in enumerate(all_nodes)}
            all_edges = [
                (int(src), int(dst))
                for src, dst in self.edges
                if int(src) in node_index and int(dst) in node_index
            ]
            edge_index = {edge: index for index, edge in enumerate(all_edges)}
            node_count = len(all_nodes)
            edge_count = len(all_edges)
            target_rows = max(
                0, int(os.environ.get("IFCN_MILP_TARGET_ROWS", "0"))
            )
            unit_edge_steps = os.environ.get(
                "IFCN_MILP_UNIT_EDGE_STEPS", "0"
            ).strip().lower() in {"1", "true", "yes", "on"}
            maximum_row = (
                max(1, target_rows - 1)
                if target_rows > 0
                else max(1, node_count - 1)
            )
            row_offset = 0
            horizontal_offset = node_count
            maximum_row_index = node_count + edge_count
            variable_count = maximum_row_index + 1

            objective = np.zeros(variable_count, dtype=np.float64)
            height_weight = float(
                os.environ.get("IFCN_MILP_HEIGHT_WEIGHT", str(edge_count + 1))
            )
            objective[maximum_row_index] = max(0.01, height_weight)
            objective[horizontal_offset:maximum_row_index] = -1.0
            objective[:node_count] = 1.0e-4

            lower_bounds = np.zeros(variable_count, dtype=np.float64)
            upper_bounds = np.full(variable_count, maximum_row, dtype=np.float64)
            upper_bounds[horizontal_offset:maximum_row_index] = 1.0
            if target_rows > 0:
                lower_bounds[maximum_row_index] = float(maximum_row)
                upper_bounds[maximum_row_index] = float(maximum_row)
            integrality = np.ones(variable_count, dtype=np.int32)

            constraint_terms = []
            constraint_lowers = []
            constraint_uppers = []

            def add_constraint(terms, lower=-np.inf, upper=np.inf):
                constraint_terms.append(dict(terms))
                constraint_lowers.append(float(lower))
                constraint_uppers.append(float(upper))

            incoming_edges = defaultdict(list)
            outgoing_edges = defaultdict(list)
            for edge_position, (src, dst) in enumerate(all_edges):
                src_var = row_offset + node_index[src]
                dst_var = row_offset + node_index[dst]
                horizontal_var = horizontal_offset + edge_position
                # row(dst)-row(src) is zero iff z_e=1, otherwise >=1.
                add_constraint(
                    {dst_var: 1.0, src_var: -1.0, horizontal_var: 1.0},
                    lower=1.0,
                )
                add_constraint(
                    {
                        dst_var: 1.0,
                        src_var: -1.0,
                        horizontal_var: float(maximum_row),
                    },
                    upper=float(maximum_row),
                )
                if target_rows > 0 and unit_edge_steps:
                    add_constraint(
                        {dst_var: 1.0, src_var: -1.0},
                        upper=1.0,
                    )
                incoming_edges[dst].append(horizontal_var)
                outgoing_edges[src].append(horizontal_var)

            for horizontal_vars in incoming_edges.values():
                add_constraint(
                    {variable: 1.0 for variable in horizontal_vars},
                    upper=1.0,
                )
            for horizontal_vars in outgoing_edges.values():
                add_constraint(
                    {variable: 1.0 for variable in horizontal_vars},
                    upper=1.0,
                )
            for node in all_nodes:
                add_constraint(
                    {
                        row_offset + node_index[node]: 1.0,
                        maximum_row_index: -1.0,
                    },
                    upper=0.0,
                )
            for node in self.parse.getInputNodesIndex:
                node = int(node)
                if node in node_index:
                    add_constraint(
                        {row_offset + node_index[node]: 1.0},
                        lower=0.0,
                        upper=0.0,
                    )
            if target_rows > 0:
                for node in self.parse.getOutputNodesIndex:
                    node = int(node)
                    if node in node_index:
                        add_constraint(
                            {row_offset + node_index[node]: 1.0},
                            lower=float(maximum_row),
                            upper=float(maximum_row),
                        )

            constraint_matrix = lil_matrix(
                (len(constraint_terms), variable_count), dtype=np.float64
            )
            for row_index, terms in enumerate(constraint_terms):
                for variable, coefficient in terms.items():
                    constraint_matrix[row_index, variable] = coefficient
            result = milp(
                c=objective,
                integrality=integrality,
                bounds=Bounds(lower_bounds, upper_bounds),
                constraints=LinearConstraint(
                    constraint_matrix.tocsr(),
                    np.asarray(constraint_lowers, dtype=np.float64),
                    np.asarray(constraint_uppers, dtype=np.float64),
                ),
                options={
                    "time_limit": float(os.environ.get("IFCN_MILP_TIMEOUT", "60")),
                    "mip_rel_gap": 0.0,
                },
            )
            if not result.success or result.x is None:
                raise RuntimeError(
                    "MILP row folding failed: {}".format(result.message)
                )

            row_by_node = {
                node: int(round(result.x[row_offset + node_index[node]]))
                for node in all_nodes
            }
            selected_succ = {}
            selected_pred = {}
            for edge_position, (src, dst) in enumerate(all_edges):
                if result.x[horizontal_offset + edge_position] >= 0.5:
                    selected_succ[src] = dst
                    selected_pred[dst] = src

            components_by_row = defaultdict(list)
            for node in all_nodes:
                if node in selected_pred:
                    continue
                path = []
                cursor = node
                while True:
                    path.append(cursor)
                    if cursor not in selected_succ:
                        break
                    cursor = selected_succ[cursor]
                components_by_row[row_by_node[node]].append(path)

            fanins = defaultdict(list)
            fanouts = defaultdict(list)
            for src, dst in all_edges:
                fanins[dst].append(src)
                fanouts[src].append(dst)
            placed = {}
            occupied_node_by_coord = {}
            component_gap = max(
                0, int(os.environ.get("IFCN_MILP_COMPONENT_GAP", "1"))
            )

            def horizontal_offsets(path):
                offsets = [0]
                for source in path[:-1]:
                    # A multi-fanout source needs a wire tile before its
                    # horizontal successor so another branch can share that
                    # prefix and turn downward.  Placing the successor
                    # immediately adjacent would consume the only branch site.
                    step = 2 if len(fanouts.get(source, ())) > 1 else 1
                    offsets.append(offsets[-1] + step)
                return offsets

            for physical_row in sorted(components_by_row):
                paths = sorted(
                    components_by_row[physical_row],
                    key=lambda path: (
                        max(
                            [
                                int(placed[fanin][0]) - offset
                                for offset, node in zip(horizontal_offsets(path), path)
                                for fanin in fanins.get(node, ())
                                if fanin in placed
                            ]
                            or [0]
                        ),
                        min(path),
                    ),
                )
                row_occupied = set()
                row_blocked = set()
                for path in paths:
                    offsets = horizontal_offsets(path)
                    required_start = 0
                    for offset, node in zip(offsets, path):
                        for fanin in fanins.get(node, ()):
                            if fanin in placed:
                                required_start = max(
                                    required_start,
                                    int(placed[fanin][0]) - offset,
                                )
                    # Do not place an unrelated gate immediately below a gate
                    # from the previous row.  That position is its only down
                    # launch port and is also the current gate's top entry;
                    # keeping it free avoids the row insertions that otherwise
                    # destroy a compact MILP row assignment.
                    while True:
                        adjacent_conflict = False
                        for path_index, (offset, node) in enumerate(zip(offsets, path)):
                            x = int(required_start + offset)
                            if x in row_blocked:
                                adjacent_conflict = True
                                break
                            above = occupied_node_by_coord.get(
                                (x, int(physical_row) - 1)
                            )
                            if above is not None and node not in fanouts.get(above, ()):
                                same_row_predecessors = 1 if path_index > 0 else 0
                                needs_top_entry = (
                                    len(fanins.get(node, ()))
                                    > same_row_predecessors
                                )
                                above_fanouts = fanouts.get(above, ())
                                blocks_down_launch = len(above_fanouts) > 1
                                if len(above_fanouts) == 1:
                                    blocks_down_launch = (
                                        row_by_node[above_fanouts[0]]
                                        == int(physical_row)
                                    )
                                if needs_top_entry or blocks_down_launch:
                                    adjacent_conflict = True
                                    break
                        if not adjacent_conflict:
                            break
                        required_start += 1
                    for offset, node in zip(offsets, path):
                        placed[node] = (
                            int(required_start + offset), int(physical_row)
                        )
                        occupied_node_by_coord[placed[node]] = node
                        row_occupied.add(int(required_start + offset))
                    for x in range(
                        int(required_start - component_gap),
                        int(required_start + offsets[-1] + 1 + component_gap),
                    ):
                        if x >= 0:
                            row_blocked.add(x)

            for layer_idx in layer_keys:
                for node in sorted_layers[layer_idx]:
                    x, y = placed[int(node)]
                    if self.transpose_initial_placement:
                        x, y = y, x
                    coords.append([int(x), int(y)])
                    node_ids.append(node)
        else:
            # Build horizontal gate chains and place the quotient DAG by rows.
            # A selected edge consumes one right output and one left input, so
            # selected edges form vertex-disjoint paths.  Contracting a path
            # into one row captures the compact 2DDWave pattern without tying
            # folding to whole logical layers (which needlessly separates, for
            # example, an inverter from its only consumer).
            node_layer = {
                int(node): int(layer_index)
                for layer_index in layer_keys
                for node in sorted_layers[layer_index]
            }
            fanins = defaultdict(list)
            fanouts = defaultdict(list)
            for src, dst in self.edges:
                src, dst = int(src), int(dst)
                if src not in node_layer or dst not in node_layer:
                    continue
                fanins[dst].append(src)
                fanouts[src].append(dst)

            selected_succ = {}
            selected_pred = {}
            chain_size = {int(node): 1 for node in node_layer}

            # Prefer downstream continuations first.  They retain a gate next
            # to the logic it drives; shallow input stems are the first links
            # released if a contraction creates a quotient dependency cycle.
            candidate_edges = sorted(
                ((int(src), int(dst)) for src, dst in self.edges),
                key=lambda pair: (
                    -node_layer[pair[1]],
                    -node_layer[pair[0]],
                    abs(int(x_pos[pair[1]]) - int(x_pos[pair[0]])),
                    int(x_pos[pair[1]]),
                    pair[0],
                    pair[1],
                ),
            )

            def chain_head(node):
                while node in selected_pred:
                    node = selected_pred[node]
                return node

            def chain_tail(node):
                while node in selected_succ:
                    node = selected_succ[node]
                return node

            for src, dst in candidate_edges:
                if src in selected_succ or dst in selected_pred:
                    continue
                src_head = chain_head(src)
                dst_head = chain_head(dst)
                if src_head == dst_head:
                    continue
                combined_size = chain_size[src_head] + chain_size[dst_head]
                if combined_size > self.layers_per_row:
                    continue
                selected_succ[src] = dst
                selected_pred[dst] = src
                chain_size[src_head] = combined_size
                chain_size.pop(dst_head, None)

            def build_components():
                components = []
                component_of = {}
                for node in sorted(node_layer):
                    if node in selected_pred:
                        continue
                    path = []
                    cursor = node
                    while True:
                        component_of[cursor] = len(components)
                        path.append(cursor)
                        if cursor not in selected_succ:
                            break
                        cursor = selected_succ[cursor]
                    components.append(path)
                return components, component_of

            def quotient_graph(components, component_of):
                successors = {index: set() for index in range(len(components))}
                predecessors = {index: set() for index in range(len(components))}
                for src, dst in self.edges:
                    src_component = component_of[int(src)]
                    dst_component = component_of[int(dst)]
                    if src_component == dst_component:
                        continue
                    successors[src_component].add(dst_component)
                    predecessors[dst_component].add(src_component)
                return successors, predecessors

            # Keep every primary-input chain on the first physical row.  If a
            # later gate in such a chain also depends on another component,
            # split immediately before that gate; otherwise its external input
            # would incorrectly force the primary input itself to a later row
            # and consume an avoidable fanout-reservation row.
            input_nodes = {
                int(node) for node in self.parse.getInputNodesIndex
                if int(node) in node_layer
            }
            while True:
                components, component_of = build_components()
                split_edge = None
                for component_index, path in enumerate(components):
                    if not input_nodes.intersection(path):
                        continue
                    for node in path[1:]:
                        if any(
                            component_of[fanin] != component_index
                            for fanin in fanins.get(node, ())
                        ):
                            predecessor = selected_pred.get(node)
                            if predecessor is not None:
                                split_edge = (predecessor, node)
                            break
                    if split_edge is not None:
                        break
                if split_edge is None:
                    break
                split_src, split_dst = split_edge
                del selected_succ[split_src]
                del selected_pred[split_dst]

            # Horizontal path contraction can expose a cycle in the quotient
            # even though the Boolean graph itself is acyclic.  Split the
            # shallowest link participating in a cyclic component until the
            # row-dependency graph is a DAG.
            while True:
                components, component_of = build_components()
                successors, predecessors = quotient_graph(components, component_of)
                indegree = {index: len(preds) for index, preds in predecessors.items()}
                ready_components = [index for index, degree in indegree.items() if degree == 0]
                visited = 0
                while ready_components:
                    component = ready_components.pop()
                    visited += 1
                    for successor in successors[component]:
                        indegree[successor] -= 1
                        if indegree[successor] == 0:
                            ready_components.append(successor)
                if visited == len(components):
                    break
                cyclic_components = {index for index, degree in indegree.items() if degree > 0}
                removable = [
                    (src, dst)
                    for src, dst in selected_succ.items()
                    if component_of[src] in cyclic_components
                ]
                if not removable:
                    raise RuntimeError("unable to split cyclic horizontal path quotient")
                split_src, split_dst = min(
                    removable,
                    key=lambda pair: (
                        node_layer[pair[1]],
                        node_layer[pair[0]],
                        pair[0],
                        pair[1],
                    ),
                )
                del selected_succ[split_src]
                del selected_pred[split_dst]

            components, component_of = build_components()
            successors, predecessors = quotient_graph(components, component_of)
            indegree = {index: len(preds) for index, preds in predecessors.items()}
            component_row = {index: 0 for index in range(len(components))}
            ready_components = []
            for index, degree in indegree.items():
                if degree == 0:
                    heapq.heappush(
                        ready_components,
                        (min(int(x_pos[node]) for node in components[index]), index),
                    )
            topological_components = []
            while ready_components:
                _, component = heapq.heappop(ready_components)
                topological_components.append(component)
                for successor in successors[component]:
                    component_row[successor] = max(
                        component_row[successor], component_row[component] + 1
                    )
                    indegree[successor] -= 1
                    if indegree[successor] == 0:
                        heapq.heappush(
                            ready_components,
                            (
                                min(int(x_pos[node]) for node in components[successor]),
                                successor,
                            ),
                        )
            if len(topological_components) != len(components):
                raise RuntimeError("folded 2DDWave component graph contains a cycle")

            components_by_row = defaultdict(list)
            for component in topological_components:
                components_by_row[component_row[component]].append(component)

            placed = {}
            for physical_row in sorted(components_by_row):
                row_components = sorted(
                    components_by_row[physical_row],
                    key=lambda index: (
                        min(int(x_pos[node]) for node in components[index]),
                        min(components[index]),
                    ),
                )
                next_x = 0
                for component in row_components:
                    path = components[component]
                    required_start = next_x
                    for offset, node in enumerate(path):
                        for fanin in fanins.get(node, ()):
                            if fanin in placed:
                                required_start = max(
                                    required_start,
                                    int(placed[fanin][0]) - offset,
                                )
                    for offset, node in enumerate(path):
                        placed[node] = (
                            int(required_start + offset),
                            int(physical_row),
                        )
                    next_x = int(required_start + len(path))

            for layer_idx in layer_keys:
                for node in sorted_layers[layer_idx]:
                    x, y = placed[int(node)]
                    if self.transpose_initial_placement:
                        x, y = y, x
                    coords.append([int(x), int(y)])
                    node_ids.append(node)

        # 3. 转成 GPU 张量 [N, 2]
        self.node_ids = torch.tensor(node_ids, device=self.device, dtype=torch.int32)  # [N]
        self.coords   = torch.tensor(coords,   device=self.device, dtype=torch.int32)  # [N, 2]
        self.rough_coords = self.coords.clone()  # 保存初始粗略位置
        self._node_id_to_idx = {int(node_id): i for i, node_id in enumerate(node_ids)}
        self._coord_cache_dirty = True

        # 4. 基于粗略位置计算扇入方向
        self.fanin_directions = self.get_fanin_directions()


    # 对fanin数量大于2的node，获取fanin的坐标x大小，
    # 对于x小的fanin，他的扇入方向是（-1，0），对于x大的fanin扇入方向是（0，-1）
    # fanin数量为1 的node，固定扇入方向是（0，-1）
    # 返回一个字典，key是{fanin_node_id,node_id},value是扇入方向
    # 使用GPU快速计算
    def get_fanin_directions(self):
        fanin_directions = dict()
        self._ensure_coord_cache()

        for nid_t in self.node_ids.detach().cpu().tolist():
            nid = int(nid_t)
            node_x, _ = self.get_node_coord(nid)
            fanins = [
                int(fid) for fid in self.parse.get_fanins(nid)
                if int(fid) in self._node_id_to_idx
            ]
            if not fanins:
                continue

            fanins_sorted = sorted(
                fanins,
                key=lambda fid: (
                    self._node_coord[fid][0],
                    self._node_coord[fid][1],
                    fid,
                ),
            )
            if self.fanin_port_assignment == "legacy":
                left_port_fanin = None
                if len(fanins_sorted) >= 2:
                    left_side = [
                        fid for fid in fanins_sorted
                        if self._node_coord[fid][0] < node_x
                    ]
                    if left_side:
                        left_port_fanin = max(
                            left_side,
                            key=lambda fid: self._node_coord[fid][0],
                        )
                    else:
                        left_port_fanin = max(
                            fanins_sorted,
                            key=lambda fid: self._node_coord[fid][0],
                        )
                for fanin_id in fanins_sorted:
                    fanin_y = self._node_coord[fanin_id][1]
                    node_y = self._node_coord[nid][1]
                    if fanin_y == node_y:
                        fanin_directions[(fanin_id, nid)] = (-1, 0)
                    elif left_port_fanin is not None and fanin_id == left_port_fanin:
                        fanin_directions[(fanin_id, nid)] = (-1, 0)
                    else:
                        fanin_directions[(fanin_id, nid)] = (0, -1)
                continue

            left_port_fanin = None
            if len(fanins_sorted) >= 2:
                same_row = [
                    fid for fid in fanins_sorted
                    if self._node_coord[fid][1] == self._node_coord[nid][1]
                    and self._node_coord[fid][0] < node_x
                ]
                left_side = [
                    fid for fid in fanins_sorted
                    if self._node_coord[fid][0] < node_x
                ]
                if same_row:
                    left_port_fanin = max(
                        same_row,
                        key=lambda fid: (self._node_coord[fid][0], fid),
                    )
                elif left_side:
                    left_port_fanin = max(
                        left_side,
                        key=lambda fid: (self._node_coord[fid][0], fid),
                    )
                else:
                    left_port_fanin = max(
                        fanins_sorted,
                        key=lambda fid: (self._node_coord[fid][0], fid),
                    )

            for fanin_id in fanins_sorted:
                fanin_y = self._node_coord[fanin_id][1]
                node_y = self._node_coord[nid][1]
                if left_port_fanin is not None and fanin_id == left_port_fanin:
                    fanin_directions[(fanin_id, nid)] = (-1, 0)
                elif left_port_fanin is None and fanin_y == node_y:
                    fanin_directions[(fanin_id, nid)] = (-1, 0)
                else:
                    fanin_directions[(fanin_id, nid)] = (0, -1)

        return fanin_directions


    # 虚拟布局：从self.coord中查找某个坐标有没有node
    def has_node_at_coord(self, coord) -> bool:
        self._ensure_coord_cache()
        x, y = coord
        return (int(x), int(y)) in self._coord_set

    # 虚拟布局：移动整行（包含参数这一行） 从数字0开始
    def move_row_down(self, r: int, delta: int = 1):
        """
        向下移动整行 (r 以及以下所有行),y 坐标整体 +delta
        """
        mask = self.coords[:, 1] >= r
        self.coords[mask, 1] += delta
        self._coord_cache_dirty = True

    # 虚拟布局：移动整列(包含参数这一列) 都是从数字0开始
    def move_col_right(self, c: int, delta: int = 1):
        """
        向右移动整列 (c 以及右边所有列),x 坐标整体 +delta
        """
        mask = self.coords[:, 0] >= c
        self.coords[mask, 0] += delta
        self._coord_cache_dirty = True

    # 连续移动多行和多列
    def move_rows_and_cols(self, row_ops, col_ops):
        # 插入行（行号升序）
        row_shift = 0
        for r in row_ops:
            real_r = r + row_shift
            self.move_row_down(real_r, 1)
            row_shift += 1  # 每插入一行，后面行号整体下移一格

        # 插入列（列号升序）
        col_shift = 0
        for c in col_ops:
            real_c = c + col_shift
            self.move_col_right(real_c, 1)
            col_shift += 1  # 每插入一列，后面列号整体右移一格

    def _right_down_invariant_violations(self):
        """Return edges which are not monotonically right/down reachable.

        An edge may propagate horizontally when its sink is strictly to the
        right.  At most one same-row fanin may use a gate's left input port;
        remaining fanins must approach from an earlier row through the top
        port.
        """
        self._ensure_coord_cache()
        violations = []
        same_row_fanins = Counter()
        for src, dst in self.edges:
            src = int(src)
            dst = int(dst)
            if src not in self._node_coord or dst not in self._node_coord:
                continue
            src_x, src_y = self._node_coord[src]
            dst_x, dst_y = self._node_coord[dst]
            if (
                dst_x < src_x
                or dst_y < src_y
                or (dst_x == src_x and dst_y == src_y)
            ):
                violations.append((src, dst))
                continue
            if dst_y == src_y:
                same_row_fanins[dst] += 1
                if same_row_fanins[dst] > 1:
                    violations.append((src, dst))
        return violations

    def _right_down_port_capacity_nodes(self):
        """Nodes likely to create high sink-port pressure in right/down flow."""
        violations = []
        effective_nodes = {int(node_id) for node_id in self.parse.effective_nodes}
        for node_id in self.parse.effective_nodes:
            fanins = {
                int(src)
                for src in self.parse.get_fanins(int(node_id))
                if int(src) in effective_nodes
            }
            if len(fanins) > 2:
                violations.append(
                    {
                        "node": int(node_id),
                        "fanin_count": int(len(fanins)),
                        "node_type": str(self.parse.getNodeTypeString(int(node_id))),
                    }
                )
        return violations

    def move_rows_up_after(self, r: int, delta: int = 1):
        """
        将严格位于 r 下方的所有行整体上移 delta。
        """
        mask = self.coords[:, 1] > r
        self.coords[mask, 1] -= delta
        self._coord_cache_dirty = True

    def move_cols_left_after(self, c: int, delta: int = 1):
        """
        将严格位于 c 右侧的所有列整体左移 delta。
        """
        mask = self.coords[:, 0] > c
        self.coords[mask, 0] -= delta
        self._coord_cache_dirty = True

    def _refresh_fanin_directions_for_current_coords(self):
        self.fanin_directions = self.get_fanin_directions()

    def legalize_right_down_ports(self, max_passes=8):
        """Create the geometric margin required by every selected sink port.

        A left-entry sink needs ``dst.x >= src.x + 1``; a top-entry sink needs
        ``dst.x >= src.x``.  Global column insertion cannot separate nodes that
        initially share an x coordinate, so this deterministic pre-route pass
        shifts the affected target row segment while preserving its node order.
        """
        moves = 0
        for _ in range(max(1, int(max_passes))):
            self._refresh_fanin_directions_for_current_coords()
            changed = False
            self._ensure_coord_cache()
            node_order = sorted(
                self._node_coord,
                key=lambda node: (
                    self._node_coord[node][1],
                    self._node_coord[node][0],
                    int(node),
                ),
            )
            for dst in node_order:
                dst_x, dst_y = self.get_node_coord(dst)
                required_x = int(dst_x)
                for src in self.parse.get_fanins(int(dst)):
                    src = int(src)
                    if src not in self._node_id_to_idx:
                        continue
                    src_x, _ = self.get_node_coord(src)
                    direction = self.fanin_directions.get((src, int(dst)), (0, -1))
                    margin = 1 if tuple(direction) == (-1, 0) else 0
                    required_x = max(required_x, int(src_x) + margin)
                if required_x <= dst_x:
                    continue
                self._move_row_segment_right(dst_y, dst_x, required_x - dst_x)
                moves += 1
                changed = True
            if not changed:
                break
        self._refresh_fanin_directions_for_current_coords()
        return moves

    def _wire_owners_by_coord(self):
        """Return the routed edges occupying every non-endpoint wire cell."""
        owners = defaultdict(set)
        for node_pair, path in self.mapChessboard.nodePairRoutes.items():
            edge = (int(node_pair[0]), int(node_pair[1]))
            norm_path = [(int(x), int(y)) for x, y in path]
            for coord in norm_path[1:-1]:
                owners[coord].add(edge)
        return owners

    def _node_move_preserves_right_down(self, node_id: int, target) -> bool:
        """Check the geometric 2DDWave partial order before an expensive reroute."""
        node_id = int(node_id)
        target_x, target_y = int(target[0]), int(target[1])
        allow_same_row = bool(
            getattr(self, "_allow_same_row_phase_node_moves", False)
        )
        for fanin in self.parse.get_fanins(node_id):
            fanin = int(fanin)
            if fanin not in self._node_id_to_idx:
                continue
            fanin_x, fanin_y = self.get_node_coord(fanin)
            if (
                target_x < fanin_x
                or target_y < fanin_y
                or (
                    not allow_same_row
                    and target_y == fanin_y
                )
                or (target_x == fanin_x and target_y == fanin_y)
            ):
                return False
        for fanout in self.parse.get_fanouts(node_id):
            fanout = int(fanout)
            if fanout not in self._node_id_to_idx:
                continue
            fanout_x, fanout_y = self.get_node_coord(fanout)
            if (
                fanout_x < target_x
                or fanout_y < target_y
                or (
                    not allow_same_row
                    and fanout_y == target_y
                )
                or (fanout_x == target_x and fanout_y == target_y)
            ):
                return False

        if allow_same_row:
            impacted_sinks = {node_id}
            impacted_sinks.update(
                int(fanout)
                for fanout in self.parse.get_fanouts(node_id)
                if int(fanout) in self._node_id_to_idx
            )
            for sink in impacted_sinks:
                _, sink_y = (
                    (target_x, target_y)
                    if sink == node_id
                    else self.get_node_coord(sink)
                )
                same_row_count = 0
                for fanin in self.parse.get_fanins(sink):
                    fanin = int(fanin)
                    if fanin not in self._node_id_to_idx:
                        continue
                    _, fanin_y = (
                        (target_x, target_y)
                        if fanin == node_id
                        else self.get_node_coord(fanin)
                    )
                    if fanin_y == sink_y:
                        same_row_count += 1
                if same_row_count > 1:
                    return False
        return True

    def _phase_node_move_candidates(
        self,
        sweep: str,
        center_twice: int,
        y_bounds=None,
        x_bounds=None,
        direction_locks=None,
        recursive_depth=0,
    ):
        """Find nodes that can absorb one cell of their own single wire.

        ``top_down`` moves an upper-half node down into the first cell of one
        of its outgoing routes.  ``bottom_up`` moves a lower-half node up into
        the last vertical cell of one of its incoming routes.  ``left_right``
        and ``right_left`` are the exact horizontal counterparts.  The target
        must contain exactly one wire and no node, matching the physical
        contraction rule rather than deleting an otherwise empty global row
        or column.
        """
        sweep_specs = {
            "top_down": ("y", 1, True),
            "bottom_up": ("y", -1, False),
            "left_right": ("x", 1, True),
            "right_left": ("x", -1, False),
        }
        if sweep not in sweep_specs:
            raise ValueError(f"Unknown phase contraction sweep: {sweep}")
        axis, delta, outgoing_move = sweep_specs[sweep]
        if y_bounds is not None and x_bounds is not None:
            raise ValueError("phase contraction accepts only one axis window")
        bounds = x_bounds if axis == "x" else y_bounds

        self._ensure_coord_cache()
        wire_owners = self._wire_owners_by_coord()
        primary_axis = 0 if axis == "x" else 1
        secondary_axis = 1 - primary_axis
        node_order = sorted(
            self._node_coord,
            key=lambda node: (
                self._node_coord[node][primary_axis],
                self._node_coord[node][secondary_axis],
                int(node),
            ),
            reverse=delta < 0,
        )
        candidates = []
        for node_id in node_order:
            x, y = self._node_coord[int(node_id)]
            position = int(x) if axis == "x" else int(y)
            if bounds is not None:
                lower, upper = int(bounds[0]), int(bounds[1])
                if position < lower or position > upper:
                    continue
            lock_key = (int(node_id), axis)
            locked_sweep = None
            if direction_locks:
                locked_sweep = direction_locks.get(
                    lock_key,
                    direction_locks.get(int(node_id)),
                )
            if locked_sweep is not None and locked_sweep != sweep:
                continue
            target = (
                (int(x + delta), int(y))
                if axis == "x"
                else (int(x), int(y + delta))
            )
            target_position = target[primary_axis]
            if bounds is not None and not (lower <= target_position <= upper):
                continue
            old_distance = abs(2 * position - int(center_twice))
            new_distance = abs(2 * target_position - int(center_twice))
            if new_distance >= old_distance or target in self._coord_set:
                continue

            owners = wire_owners.get(target, set())
            if len(owners) != 1:
                continue
            route_edge = next(iter(owners))
            path = [
                (int(px), int(py))
                for px, py in self.mapChessboard.nodePairRoutes.get(route_edge, [])
            ]
            if outgoing_move:
                owns_target = (
                    route_edge[0] == int(node_id)
                    and len(path) >= 3
                    and path[1] == target
                )
            else:
                owns_target = (
                    route_edge[1] == int(node_id)
                    and len(path) >= 3
                    and path[-2] == target
                    and target[secondary_axis]
                    == (int(y) if secondary_axis == 1 else int(x))
                )
            if not owns_target:
                continue
            if not self._node_move_preserves_right_down(node_id, target):
                continue

            candidates.append(
                {
                    "sweep": str(sweep),
                    "node": int(node_id),
                    "from_coord": (int(x), int(y)),
                    "to_coord": target,
                    "route_edge": route_edge,
                    "phase_from": int(self.mapChessboard.getPhase((int(x), int(y)))),
                    "phase_target": int(self.mapChessboard.getPhase(target)),
                    "center_distance_before": int(old_distance),
                    "center_distance_after": int(new_distance),
                    "recursive_depth": int(recursive_depth),
                    "axis": axis,
                    "axis_window": (
                        [int(bounds[0]), int(bounds[1])]
                        if bounds is not None else []
                    ),
                    "y_window": (
                        [int(y_bounds[0]), int(y_bounds[1])]
                        if y_bounds is not None else []
                    ),
                }
            )
        return candidates

    def _recursive_phase_contraction_windows(self, minimum: int, maximum: int):
        """Return global-to-local windows for hierarchical axis contraction."""
        windows = []

        def visit(lower_y, upper_y, depth):
            lower_y, upper_y = int(lower_y), int(upper_y)
            if upper_y - lower_y < 2:
                return
            windows.append(
                {
                    "minimum": lower_y,
                    "maximum": upper_y,
                    # Compatibility aliases retained for callers that used
                    # the original vertical-only helper directly.
                    "min_y": lower_y,
                    "max_y": upper_y,
                    "center_twice": lower_y + upper_y,
                    "depth": int(depth),
                }
            )
            if upper_y - lower_y < 4:
                return
            middle = (lower_y + upper_y) // 2
            visit(lower_y, middle, depth + 1)
            visit(middle + 1, upper_y, depth + 1)

        visit(minimum, maximum, 0)
        # Breadth-first ordering gives both halves an opportunity before a
        # deep sub-window can consume the remaining reroute budget.
        windows.sort(key=lambda item: (item["depth"], item["minimum"]))
        return windows

    def _reroute_and_validate_current_coords(self, verbose=False):
        self._refresh_fanin_directions_for_current_coords()
        if self._last_route_explicit_priority:
            failed_pairs = self.reroute_with_priority_pairs(
                self._last_route_explicit_priority,
                verbose=verbose,
                explicit_priority_order=self._last_route_explicit_priority,
            )
        elif self._last_route_priority:
            failed_pairs = self.reroute_with_priority_pairs(
                self._last_route_priority,
                verbose=verbose,
                reverse_priority=self._last_route_reverse_priority,
                reverse_remaining=self._last_route_reverse_remaining,
            )
        elif self._last_route_reverse_remaining:
            failed_pairs = self.reroute_with_priority_pairs(
                (),
                verbose=verbose,
                reverse_remaining=True,
            )
        else:
            self.place_all_nodes_on_chessboard()
            failed_pairs = self.sequence_route_all_edges(verbose=verbose)
        if failed_pairs:
            self.clock_template_ok = False
            self.clock_template_conflict_count = 0
            self._sync_legacy_phase_status()
            return False

        template_ok, _ = self.verify_clock_template_consistency(verbose=verbose)
        return bool(template_ok)

    def _current_phase_contraction_metrics(self):
        width, height = self.mapChessboard.computeLayoutArea()
        width, height = max(0, int(width)), max(0, int(height))
        routed_wire_cells = sum(
            max(0, len(path) - 2)
            for path in self.mapChessboard.nodePairRoutes.values()
        )
        return {
            "width": width,
            "height": height,
            "area": width * height,
            "used_cell_count": len(self._collect_used_coords()),
            "routed_wire_cells": int(routed_wire_cells),
        }

    def _try_phase_node_move(self, candidate, verbose=False):
        """Move one node into its own wire cell and accept only a legal contraction."""
        prev_coords = self.coords.clone()
        prev_fanin_directions = dict(self.fanin_directions)
        prev_route_priority = set(self._last_route_priority)
        prev_reverse_priority = bool(self._last_route_reverse_priority)
        prev_reverse_remaining = bool(self._last_route_reverse_remaining)
        prev_explicit_priority = tuple(self._last_route_explicit_priority)
        prev_metrics = self._current_phase_contraction_metrics()

        node_id = int(candidate["node"])
        target = tuple(int(value) for value in candidate["to_coord"])
        node_index = self._node_id_to_idx[node_id]
        self.coords[node_index, 0] = target[0]
        self.coords[node_index, 1] = target[1]
        self._coord_cache_dirty = True

        geometry_ok = not self._right_down_invariant_violations()
        if geometry_ok and self._reroute_and_validate_current_coords(verbose=verbose):
            new_metrics = self._current_phase_contraction_metrics()
            area_reduced = new_metrics["area"] < prev_metrics["area"]
            neutral_inward_move = (
                new_metrics["area"] == prev_metrics["area"]
                and new_metrics["used_cell_count"] <= prev_metrics["used_cell_count"]
                and int(candidate["center_distance_after"])
                < int(candidate["center_distance_before"])
            )
            if area_reduced or neutral_inward_move:
                new_metrics["phase_after"] = int(
                    self.mapChessboard.getPhase(target)
                )
                return True, prev_metrics, new_metrics

        self.coords = prev_coords
        self._coord_cache_dirty = True
        self.fanin_directions = prev_fanin_directions
        self._last_route_priority = prev_route_priority
        self._last_route_reverse_priority = prev_reverse_priority
        self._last_route_reverse_remaining = prev_reverse_remaining
        self._last_route_explicit_priority = prev_explicit_priority
        # Candidate routing mutates the board.  Rebuild the preceding legal
        # node placement immediately so a rejection cannot poison the next move.
        restored = self._reroute_and_validate_current_coords(verbose=False)
        if not restored:
            raise RuntimeError(
                "failed to restore legal layout after rejected phase-node move"
            )
        return False, prev_metrics, None

    def _empty_line_candidates(self):
        """Return node-empty rows/columns inside the physical bounding box.

        Wires may cross the candidate line: they are discarded and rerouted
        after the coordinate collapse.  A line containing a node is never a
        blank-line candidate.
        """
        min_x, min_y, max_x, max_y = self.mapChessboard.findLayoutBoard()
        if max_x < min_x or max_y < min_y:
            return []
        self._ensure_coord_cache()
        node_x = {int(x) for x, _ in self._node_coord.values()}
        node_y = {int(y) for _, y in self._node_coord.values()}
        empty_rows = [
            int(y) for y in range(int(min_y) + 1, int(max_y)) if y not in node_y
        ]
        empty_cols = [
            int(x) for x in range(int(min_x) + 1, int(max_x)) if x not in node_x
        ]
        width = int(max_x) - int(min_x) + 1
        height = int(max_y) - int(min_y) + 1
        axis_order = ("y", "x") if width >= height else ("x", "y")
        by_axis = {"y": empty_rows, "x": empty_cols}
        return [
            (axis, line)
            for axis in axis_order
            for line in by_axis[axis]
        ]

    def _try_delete_empty_line(self, axis: str, line: int, verbose=False):
        """Delete one node-empty row/column and restore on any legal failure."""
        axis, line = str(axis), int(line)
        if axis not in {"x", "y"}:
            raise ValueError(f"Unknown empty-line axis: {axis}")
        self._ensure_coord_cache()
        if any(
            (int(x) == line if axis == "x" else int(y) == line)
            for x, y in self._node_coord.values()
        ):
            return False, None, None

        prev_coords = self.coords.clone()
        prev_fanin_directions = dict(self.fanin_directions)
        prev_route_priority = set(self._last_route_priority)
        prev_reverse_priority = bool(self._last_route_reverse_priority)
        prev_reverse_remaining = bool(self._last_route_reverse_remaining)
        prev_explicit_priority = tuple(self._last_route_explicit_priority)
        prev_metrics = self._current_phase_contraction_metrics()

        if axis == "x":
            self.move_cols_left_after(line, 1)
        else:
            self.move_rows_up_after(line, 1)

        geometry_ok = not self._right_down_invariant_violations()
        if geometry_ok and self._reroute_and_validate_current_coords(verbose=verbose):
            new_metrics = self._current_phase_contraction_metrics()
            if new_metrics["area"] < prev_metrics["area"]:
                return True, prev_metrics, new_metrics

        self.coords = prev_coords
        self._coord_cache_dirty = True
        self.fanin_directions = prev_fanin_directions
        self._last_route_priority = prev_route_priority
        self._last_route_reverse_priority = prev_reverse_priority
        self._last_route_reverse_remaining = prev_reverse_remaining
        self._last_route_explicit_priority = prev_explicit_priority
        restored = self._reroute_and_validate_current_coords(verbose=False)
        if not restored:
            raise RuntimeError(
                "failed to restore legal layout after rejected empty-line deletion"
            )
        return False, prev_metrics, None

    def _adjacent_layer_merge_candidates(self):
        """Return occupied adjacent rows/columns that can be projected together.

        Unlike blank-line deletion, both physical layers contain nodes.  A
        cheap projection test rejects a boundary when the two layers would put
        two nodes in the same cell.  Routing, phase, and right/down legality are
        checked later against the complete layout.
        """
        min_x, min_y, max_x, max_y = self.mapChessboard.findLayoutBoard()
        if max_x < min_x or max_y < min_y:
            return []

        self._ensure_coord_cache()
        x_projections = defaultdict(set)
        y_projections = defaultdict(set)
        x_counts = Counter()
        y_counts = Counter()
        for x, y in self._node_coord.values():
            x, y = int(x), int(y)
            x_projections[x].add(y)
            y_projections[y].add(x)
            x_counts[x] += 1
            y_counts[y] += 1

        width = int(max_x) - int(min_x) + 1
        height = int(max_y) - int(min_y) + 1
        candidates = []
        for axis, lower, upper, projections, counts, area_saving in (
            ("x", int(min_x), int(max_x), x_projections, x_counts, height),
            ("y", int(min_y), int(max_y), y_projections, y_counts, width),
        ):
            for line in range(lower, upper):
                first = projections.get(line, set())
                second = projections.get(line + 1, set())
                if not first or not second or first.intersection(second):
                    continue
                candidates.append(
                    {
                        "axis": axis,
                        "line": int(line),
                        "first_layer_node_count": int(counts[line]),
                        "second_layer_node_count": int(counts[line + 1]),
                        "moved_node_count": int(
                            sum(count for layer, count in counts.items() if layer > line)
                        ),
                        "area_saving_upper_bound": int(area_saving),
                    }
                )

        # Move the smallest suffix first.  This is the occupied-layer form of
        # outside-in contraction and tends to perturb fewer routed terminals.
        candidates.sort(
            key=lambda item: (
                int(item["moved_node_count"]),
                -int(item["area_saving_upper_bound"]),
                0 if item["axis"] == "x" else 1,
                -int(item["line"]),
            )
        )
        return candidates

    def _try_merge_adjacent_layers(self, candidate, verbose=False):
        """Project two occupied physical layers and accept only a legal reroute."""
        axis = str(candidate["axis"])
        line = int(candidate["line"])
        if axis not in {"x", "y"}:
            raise ValueError(f"Unknown adjacent-layer merge axis: {axis}")

        prev_coords = self.coords.clone()
        prev_fanin_directions = dict(self.fanin_directions)
        prev_route_priority = set(self._last_route_priority)
        prev_reverse_priority = bool(self._last_route_reverse_priority)
        prev_reverse_remaining = bool(self._last_route_reverse_remaining)
        prev_explicit_priority = tuple(self._last_route_explicit_priority)
        prev_metrics = self._current_phase_contraction_metrics()

        if axis == "x":
            self.move_cols_left_after(line, 1)
        else:
            self.move_rows_up_after(line, 1)

        self._ensure_coord_cache()
        unique_node_cells = len(self._coord_set) == len(self._node_coord)
        geometry_ok = unique_node_cells and not self._right_down_invariant_violations()
        if geometry_ok and self._reroute_and_validate_current_coords(verbose=verbose):
            new_metrics = self._current_phase_contraction_metrics()
            if new_metrics["area"] < prev_metrics["area"]:
                return True, prev_metrics, new_metrics

        self.coords = prev_coords
        self._coord_cache_dirty = True
        self.fanin_directions = prev_fanin_directions
        self._last_route_priority = prev_route_priority
        self._last_route_reverse_priority = prev_reverse_priority
        self._last_route_reverse_remaining = prev_reverse_remaining
        self._last_route_explicit_priority = prev_explicit_priority
        restored = self._reroute_and_validate_current_coords(verbose=False)
        if not restored:
            raise RuntimeError(
                "failed to restore legal layout after rejected adjacent-layer merge"
            )
        return False, prev_metrics, None

    def compact_layout(self, max_iters=64, verbose=False):
        """Select the best legal result from conservative and relaxed search.

        Both branches start from the same routed placement.  The conservative
        branch keeps phase-node moves strictly between rows; the relaxed branch
        also permits a single left-port same-row dependency.  Occupied-layer
        merging is legal in both branches.  Selecting by physical area, then
        used cells and routed cells, prevents an aggressive local move from
        making the final layout worse.
        """
        portfolio_enabled = os.environ.get(
            "IFCN_CONTRACTION_PORTFOLIO", "1"
        ).strip().lower() not in {"0", "false", "no", "off"}
        horizontal_enabled = os.environ.get(
            "IFCN_HORIZONTAL_CONTRACTION", "1"
        ).strip().lower() not in {"0", "false", "no", "off"}
        portfolio_ready = all(
            hasattr(self, field)
            for field in (
                "fanin_directions",
                "_last_route_priority",
                "_last_route_reverse_priority",
                "_last_route_reverse_remaining",
                "_last_route_explicit_priority",
            )
        )
        if not portfolio_enabled or not portfolio_ready:
            self._allow_same_row_phase_node_moves = False
            self._allow_horizontal_phase_node_moves = bool(horizontal_enabled)
            return self._compact_layout_fixed_point(
                max_iters=max_iters,
                verbose=verbose,
            )
        branch_modes = [(False, False), (True, False)]
        if horizontal_enabled:
            branch_modes.extend(((False, True), (True, True)))

        def capture_geometry_state():
            return {
                "coords": self.coords.clone(),
                "fanin_directions": dict(self.fanin_directions),
                "route_priority": set(self._last_route_priority),
                "reverse_priority": bool(self._last_route_reverse_priority),
                "reverse_remaining": bool(self._last_route_reverse_remaining),
                "explicit_priority": tuple(self._last_route_explicit_priority),
            }

        def restore_geometry_state(state):
            self.coords = state["coords"].clone()
            self._coord_cache_dirty = True
            self.fanin_directions = dict(state["fanin_directions"])
            self._last_route_priority = set(state["route_priority"])
            self._last_route_reverse_priority = bool(state["reverse_priority"])
            self._last_route_reverse_remaining = bool(state["reverse_remaining"])
            self._last_route_explicit_priority = tuple(state["explicit_priority"])
            if not self._reroute_and_validate_current_coords(verbose=False):
                raise RuntimeError("failed to restore legal contraction portfolio state")

        initial_state = capture_geometry_state()
        branch_results = []
        total_evaluations = {
            "contraction_evaluations": 0,
            "contraction_global_evaluations": 0,
            "contraction_recursive_evaluations": 0,
            "contraction_empty_line_evaluations": 0,
            "contraction_layer_merge_evaluations": 0,
        }
        any_exhausted = False

        for branch_index, (allow_same_row, allow_horizontal) in enumerate(branch_modes):
            if branch_index:
                restore_geometry_state(initial_state)
            self._allow_same_row_phase_node_moves = bool(allow_same_row)
            self._allow_horizontal_phase_node_moves = bool(allow_horizontal)
            reductions = self._compact_layout_fixed_point(
                max_iters=max_iters,
                verbose=verbose,
            )
            metrics = self._current_phase_contraction_metrics()
            result = {
                "allow_same_row": bool(allow_same_row),
                "allow_horizontal": bool(allow_horizontal),
                "reductions": int(reductions),
                "metrics": dict(metrics),
                "geometry": capture_geometry_state(),
                "history": list(self.contraction_history),
                "exhausted": bool(self.contraction_exhausted),
            }
            branch_results.append(result)
            any_exhausted = any_exhausted or result["exhausted"]
            for field in total_evaluations:
                total_evaluations[field] += int(getattr(self, field))
            if verbose:
                print(
                    "[verified contraction branch] same_row={} "
                    "horizontal={} area={} used={} routed={}".format(
                        allow_same_row,
                        allow_horizontal,
                        metrics["area"],
                        metrics["used_cell_count"],
                        metrics["routed_wire_cells"],
                    )
                )

        winner = min(
            branch_results,
            key=lambda result: (
                int(result["metrics"]["area"]),
                int(result["metrics"]["used_cell_count"]),
                int(result["metrics"]["routed_wire_cells"]),
            ),
        )
        if winner is not branch_results[-1]:
            restore_geometry_state(winner["geometry"])
        self._allow_same_row_phase_node_moves = bool(winner["allow_same_row"])
        self._allow_horizontal_phase_node_moves = bool(winner["allow_horizontal"])
        self.contraction_history = list(winner["history"])
        for field, value in total_evaluations.items():
            setattr(self, field, int(value))
        self.contraction_exhausted = bool(any_exhausted)
        return int(winner["reductions"])

    def _compact_layout_fixed_point(self, max_iters=64, verbose=False):
        """Run the complete contraction pipeline to a configurable fixed point.

        Empty-line deletion and occupied-layer merging can expose node moves
        which were impossible during the earlier phases of the same pass.
        Repeating the full legal contraction pipeline lets those opportunities
        feed back into the global/recursive sweeps.  Every accepted operation
        is still rerouted and phase-verified by ``_compact_layout_pass``.
        """
        max_passes = max(
            1, int(os.environ.get("IFCN_CONTRACTION_PASSES", "4"))
        )
        total_reductions = 0
        combined_history = []
        combined_evaluations = {
            "contraction_evaluations": 0,
            "contraction_global_evaluations": 0,
            "contraction_recursive_evaluations": 0,
            "contraction_empty_line_evaluations": 0,
            "contraction_layer_merge_evaluations": 0,
        }
        any_exhausted = False

        for pass_index in range(max_passes):
            remaining_reductions = max(0, int(max_iters) - total_reductions)
            if remaining_reductions <= 0:
                break
            reductions = self._compact_layout_pass(
                max_iters=remaining_reductions,
                verbose=verbose,
            )
            pass_history = list(self.contraction_history)
            for entry in pass_history:
                entry = dict(entry)
                entry["pass"] = int(pass_index + 1)
                entry["step"] = int(len(combined_history) + 1)
                combined_history.append(entry)
            for field in combined_evaluations:
                combined_evaluations[field] += int(getattr(self, field))
            any_exhausted = any_exhausted or bool(self.contraction_exhausted)
            total_reductions += int(reductions)

            if verbose:
                print(
                    "[verified contraction pass] pass={} reductions={}".format(
                        pass_index + 1,
                        reductions,
                    )
                )
            if reductions <= 0:
                break

        self.contraction_history = combined_history
        for field, value in combined_evaluations.items():
            setattr(self, field, int(value))
        self.contraction_exhausted = bool(any_exhausted)
        return total_reductions

    def _compact_layout_pass(self, max_iters=64, verbose=False):
        """Contract a top-down layout by moving nodes into their own wire cells.

        The full vertical interval is contracted first, followed recursively
        by its internal layer intervals.  Every interval has a local center;
        upper nodes move downward and lower nodes move upward.  Each accepted
        local move is rerouted and phase-verified.  Final fixed-point stages
        delete node-empty lines and then merge compatible occupied adjacent
        layers.  Wires are discarded and rerouted after every collapse.
        """
        max_iters = max(0, int(max_iters))
        if max_iters <= 0:
            return 0

        global_evaluation_budget = max(
            1, int(os.environ.get("IFCN_CONTRACTION_EVALUATIONS", "256"))
        )
        recursive_evaluation_budget = max(
            1,
            int(
                os.environ.get(
                    "IFCN_RECURSIVE_CONTRACTION_EVALUATIONS",
                    str(global_evaluation_budget),
                )
            ),
        )
        global_time_limit = max(
            1.0, float(os.environ.get("IFCN_CONTRACTION_TIMEOUT", "120"))
        )
        recursive_time_limit = max(
            1.0,
            float(
                os.environ.get(
                    "IFCN_RECURSIVE_CONTRACTION_TIMEOUT",
                    str(global_time_limit),
                )
            ),
        )
        empty_line_evaluation_budget = max(
            1,
            int(
                os.environ.get(
                    "IFCN_EMPTY_LINE_CONTRACTION_EVALUATIONS",
                    str(global_evaluation_budget),
                )
            ),
        )
        empty_line_time_limit = max(
            1.0,
            float(
                os.environ.get(
                    "IFCN_EMPTY_LINE_CONTRACTION_TIMEOUT",
                    str(global_time_limit),
                )
            ),
        )
        layer_merge_evaluation_budget = max(
            1,
            int(
                os.environ.get(
                    "IFCN_LAYER_MERGE_CONTRACTION_EVALUATIONS",
                    str(global_evaluation_budget),
                )
            ),
        )
        layer_merge_time_limit = max(
            1.0,
            float(
                os.environ.get(
                    "IFCN_LAYER_MERGE_CONTRACTION_TIMEOUT",
                    str(global_time_limit),
                )
            ),
        )
        window_evaluation_limit = max(
            1, int(os.environ.get("IFCN_CONTRACTION_WINDOW_EVALUATIONS", "4"))
        )
        reductions = 0
        last_legal_coords = self.coords.clone()
        self.contraction_history = []
        self.contraction_evaluations = 0
        self.contraction_global_evaluations = 0
        self.contraction_recursive_evaluations = 0
        self.contraction_empty_line_evaluations = 0
        self.contraction_layer_merge_evaluations = 0
        self.contraction_exhausted = False
        min_x, min_y, max_x, max_y = self.mapChessboard.findLayoutBoard()
        if max_y < min_y or max_x < min_x:
            return 0
        windows_by_axis = {
            "y": self._recursive_phase_contraction_windows(min_y, max_y),
            "x": self._recursive_phase_contraction_windows(min_x, max_x),
        }
        direction_locks = {}

        def record_accepted_move(candidate, sweep, old_metrics, new_metrics):
            nonlocal reductions, last_legal_coords
            reductions += 1
            candidate_axis = str(candidate.get("axis", "y"))
            direction_locks[(int(candidate["node"]), candidate_axis)] = sweep
            last_legal_coords = self.coords.clone()
            mode_prefix = (
                "global" if int(candidate["recursive_depth"]) == 0
                else "recursive"
            )
            self.contraction_history.append(
                {
                    "step": int(reductions),
                    "mode": "phase_node_{}_{}".format(mode_prefix, sweep),
                    "recursive_depth": int(candidate["recursive_depth"]),
                    "axis": candidate_axis,
                    "axis_window": list(
                        candidate.get("axis_window", candidate.get("y_window", []))
                    ),
                    "y_window": list(candidate["y_window"]),
                    "node": int(candidate["node"]),
                    "from_coord": list(candidate["from_coord"]),
                    "to_coord": list(candidate["to_coord"]),
                    "route_edge": list(candidate["route_edge"]),
                    "phase_from": int(candidate["phase_from"]),
                    "phase_target_before": int(candidate["phase_target"]),
                    "phase_after": int(new_metrics["phase_after"]),
                    "old_width": int(old_metrics["width"]),
                    "old_height": int(old_metrics["height"]),
                    "width": int(new_metrics["width"]),
                    "height": int(new_metrics["height"]),
                    "area": int(new_metrics["area"]),
                    "old_used_cell_count": int(old_metrics["used_cell_count"]),
                    "used_cell_count": int(new_metrics["used_cell_count"]),
                    "old_routed_wire_cells": int(old_metrics["routed_wire_cells"]),
                    "routed_wire_cells": int(new_metrics["routed_wire_cells"]),
                }
            )
            if verbose:
                print(
                    "[verified phase contract:{}:{}] node {}: {} -> {}, "
                    "phase {} -> {}, area={}x{}".format(
                        candidate["recursive_depth"],
                        sweep,
                        candidate["node"],
                        tuple(candidate["from_coord"]),
                        tuple(candidate["to_coord"]),
                        candidate["phase_from"],
                        new_metrics["phase_after"],
                        new_metrics["width"],
                        new_metrics["height"],
                    )
                )

        # Stage 1: complete whole-layout inward sweeps on both axes.  The
        # horizontal branch is symmetric to the established vertical branch;
        # every accepted move still survives a complete reroute and template
        # validation before it becomes the next checkpoint.
        global_started = time.perf_counter()
        global_reductions = 0
        global_exhausted = False
        contraction_axes = [("y", ("top_down", "bottom_up"))]
        if bool(getattr(self, "_allow_horizontal_phase_node_moves", False)):
            contraction_axes.append(("x", ("left_right", "right_left")))
        for axis, sweeps in contraction_axes:
            global_window = (
                windows_by_axis[axis][0] if windows_by_axis[axis] else None
            )
            while global_window is not None and global_reductions < max_iters:
                improved_this_round = False
                for sweep in sweeps:
                    bounds_keyword = (
                        {"x_bounds": (
                            global_window["minimum"], global_window["maximum"]
                        )}
                        if axis == "x" else
                        {"y_bounds": (
                            global_window["minimum"], global_window["maximum"]
                        )}
                    )
                    candidates = self._phase_node_move_candidates(
                        sweep,
                        global_window["center_twice"],
                        direction_locks=direction_locks,
                        recursive_depth=0,
                        **bounds_keyword,
                    )
                    for candidate in candidates:
                        if (
                            self.contraction_global_evaluations
                            >= global_evaluation_budget
                            or time.perf_counter() - global_started >= global_time_limit
                        ):
                            global_exhausted = True
                            break
                        self.contraction_global_evaluations += 1
                        self.contraction_evaluations += 1
                        success, old_metrics, new_metrics = self._try_phase_node_move(
                            candidate,
                            verbose=False,
                        )
                        if not success:
                            continue
                        global_reductions += 1
                        improved_this_round = True
                        record_accepted_move(candidate, sweep, old_metrics, new_metrics)
                        break
                    if global_exhausted or global_reductions >= max_iters:
                        break
                if global_exhausted or not improved_this_round:
                    break
            if global_exhausted or global_reductions >= max_iters:
                break

        # Stage 2: recursively contract internal physical-layer intervals from
        # the checkpoint produced above.  It has an independent budget so the
        # recursive search can never replace or truncate the global result.
        recursive_started = time.perf_counter()
        recursive_reductions = 0
        recursive_exhausted = False
        for axis, sweeps in contraction_axes:
            recursive_windows = [
                window for window in windows_by_axis[axis]
                if window["depth"] > 0
            ]
            while recursive_windows and recursive_reductions < max_iters:
                improved_this_round = False
                for window in recursive_windows:
                    for sweep in sweeps:
                        bounds_keyword = (
                            {"x_bounds": (window["minimum"], window["maximum"])}
                            if axis == "x" else
                            {"y_bounds": (window["minimum"], window["maximum"])}
                        )
                        candidates = self._phase_node_move_candidates(
                            sweep,
                            window["center_twice"],
                            direction_locks=direction_locks,
                            recursive_depth=window["depth"],
                            **bounds_keyword,
                        )
                        window_evaluations = 0
                        for candidate in candidates:
                            if window_evaluations >= window_evaluation_limit:
                                break
                            if (
                                self.contraction_recursive_evaluations
                                >= recursive_evaluation_budget
                                or time.perf_counter() - recursive_started
                                >= recursive_time_limit
                            ):
                                recursive_exhausted = True
                                break
                            window_evaluations += 1
                            self.contraction_recursive_evaluations += 1
                            self.contraction_evaluations += 1
                            success, old_metrics, new_metrics = self._try_phase_node_move(
                                candidate,
                                verbose=False,
                            )
                            if not success:
                                continue
                            recursive_reductions += 1
                            improved_this_round = True
                            record_accepted_move(candidate, sweep, old_metrics, new_metrics)
                            break
                        if recursive_exhausted or recursive_reductions >= max_iters:
                            break
                    if recursive_exhausted or recursive_reductions >= max_iters:
                        break
                if recursive_exhausted or not improved_this_round:
                    break
            if recursive_exhausted or recursive_reductions >= max_iters:
                break

        # Stage 3: delete node-empty rows/columns to a fixed point.  Existing
        # wires on a candidate line are discarded and rerouted; removing one
        # changes coordinates and phases, so every deletion is followed by a
        # complete legal reroute.
        empty_line_started = time.perf_counter()
        empty_line_reductions = 0
        empty_line_exhausted = False
        while empty_line_reductions < max_iters:
            empty_candidates = self._empty_line_candidates()
            if not empty_candidates:
                break
            improved_this_round = False
            for axis, line in empty_candidates:
                if (
                    self.contraction_empty_line_evaluations
                    >= empty_line_evaluation_budget
                    or time.perf_counter() - empty_line_started
                    >= empty_line_time_limit
                ):
                    empty_line_exhausted = True
                    break
                self.contraction_empty_line_evaluations += 1
                self.contraction_evaluations += 1
                success, old_metrics, new_metrics = self._try_delete_empty_line(
                    axis,
                    line,
                    verbose=False,
                )
                if not success:
                    continue

                reductions += 1
                empty_line_reductions += 1
                improved_this_round = True
                last_legal_coords = self.coords.clone()
                self.contraction_history.append(
                    {
                        "step": int(reductions),
                        "mode": (
                            "empty_row_delete" if axis == "y"
                            else "empty_col_delete"
                        ),
                        "axis": str(axis),
                        "line": int(line),
                        "old_width": int(old_metrics["width"]),
                        "old_height": int(old_metrics["height"]),
                        "width": int(new_metrics["width"]),
                        "height": int(new_metrics["height"]),
                        "area": int(new_metrics["area"]),
                        "old_used_cell_count": int(old_metrics["used_cell_count"]),
                        "used_cell_count": int(new_metrics["used_cell_count"]),
                        "old_routed_wire_cells": int(old_metrics["routed_wire_cells"]),
                        "routed_wire_cells": int(new_metrics["routed_wire_cells"]),
                    }
                )
                if verbose:
                    print(
                        "[verified phase contract:empty-{}] line {}, area={}x{}".format(
                            "row" if axis == "y" else "col",
                            line,
                            new_metrics["width"],
                            new_metrics["height"],
                        )
                    )
                break
            if empty_line_exhausted or not improved_this_round:
                break

        # Stage 4: merge adjacent occupied physical layers.  Nodes in the
        # second layer are projected onto the first and the downstream suffix
        # is shifted by one cell.  This is a real layer merge, not whitespace
        # removal; every proposal must survive a full reroute and phase check.
        layer_merge_started = time.perf_counter()
        layer_merge_reductions = 0
        layer_merge_exhausted = False
        while layer_merge_reductions < max_iters:
            merge_candidates = self._adjacent_layer_merge_candidates()
            if not merge_candidates:
                break
            improved_this_round = False
            for candidate in merge_candidates:
                if (
                    self.contraction_layer_merge_evaluations
                    >= layer_merge_evaluation_budget
                    or time.perf_counter() - layer_merge_started
                    >= layer_merge_time_limit
                ):
                    layer_merge_exhausted = True
                    break
                self.contraction_layer_merge_evaluations += 1
                self.contraction_evaluations += 1
                success, old_metrics, new_metrics = self._try_merge_adjacent_layers(
                    candidate,
                    verbose=False,
                )
                if not success:
                    continue

                reductions += 1
                layer_merge_reductions += 1
                improved_this_round = True
                last_legal_coords = self.coords.clone()
                axis = str(candidate["axis"])
                self.contraction_history.append(
                    {
                        "step": int(reductions),
                        "mode": (
                            "adjacent_row_merge" if axis == "y"
                            else "adjacent_col_merge"
                        ),
                        "axis": axis,
                        "line": int(candidate["line"]),
                        "merged_layers": [
                            int(candidate["line"]),
                            int(candidate["line"]) + 1,
                        ],
                        "first_layer_node_count": int(
                            candidate["first_layer_node_count"]
                        ),
                        "second_layer_node_count": int(
                            candidate["second_layer_node_count"]
                        ),
                        "moved_node_count": int(candidate["moved_node_count"]),
                        "old_width": int(old_metrics["width"]),
                        "old_height": int(old_metrics["height"]),
                        "width": int(new_metrics["width"]),
                        "height": int(new_metrics["height"]),
                        "area": int(new_metrics["area"]),
                        "old_used_cell_count": int(old_metrics["used_cell_count"]),
                        "used_cell_count": int(new_metrics["used_cell_count"]),
                        "old_routed_wire_cells": int(
                            old_metrics["routed_wire_cells"]
                        ),
                        "routed_wire_cells": int(new_metrics["routed_wire_cells"]),
                    }
                )
                if verbose:
                    print(
                        "[verified phase contract:merge-{}] layers {}+{}, "
                        "area={}x{}".format(
                            "row" if axis == "y" else "col",
                            candidate["line"],
                            int(candidate["line"]) + 1,
                            new_metrics["width"],
                            new_metrics["height"],
                        )
                    )
                break
            if layer_merge_exhausted or not improved_this_round:
                break

        self.contraction_exhausted = bool(
            global_exhausted
            or recursive_exhausted
            or empty_line_exhausted
            or layer_merge_exhausted
        )

        if not self._reroute_and_validate_current_coords(verbose=False):
            self.coords = last_legal_coords
            self._coord_cache_dirty = True
            if not self._reroute_and_validate_current_coords(verbose=False):
                raise RuntimeError("failed to restore last legal phase-contracted layout")
        return reductions

    # 获取虚拟节点坐标
    def get_node_coord(self, node_id: int):
        """
        返回指定 node_id 的 (x, y) 坐标
        """
        self._ensure_coord_cache()
        node_id = int(node_id)
        if node_id not in self._node_coord:
            raise ValueError(f"Node {node_id} 不存在")
        return self._node_coord[node_id]



    """
    ===============================================
    step2 根据step1，计算要预留的扇入和扇出方向
    ===============================================
    """
    def caculate_ports_reservation(self):
        self.reserve_fanin_ports()
        self.reserve_fanout_ports()


    def reserve_fanout_ports(self):
        # 遍历self.sorted_nodes_per_layer_new 首先预留扇出端口，对于每个node的扇出，如果扇出数量大于2，检查当前node的下方和右侧有没有node，
        # 如果有node，则需要在下方和右侧各插入一行/列
        for layer_idx in sorted(self.barycenter_opt_layers.keys()):
            nodes = self.barycenter_opt_layers[layer_idx]
            for nid_t in nodes:
                # 检查这个node的扇出数量
                fanouts = [
                    int(fout) for fout in self.parse.get_fanouts(int(nid_t))
                    if int(fout) in self._node_id_to_idx
                ]
                fanout_count = len(fanouts)
                if fanout_count == 0:
                    continue

                x, y = self.get_node_coord(int(nid_t))
                # 如果扇出数量是1，从self.coords中获取当前的坐标，然后检查下方有没有node，如果有插入1行
                if fanout_count == 1:
                    #说明只有1个扇出，如果扇出node的坐标不在当前node层级的正下方，就要插入一行
                    fanout_id = fanouts[0]
                    fanout_x, fanout_y = self.get_node_coord(int(fanout_id))
                    # 如果扇出node在下方但是不是正下方, 不管下方有没有node都要插入1行
                    if fanout_y == y + 1 and x != fanout_x and self.has_node_at_coord((x , y + 1)):
                        self.move_row_down(y + 1, 1)
                elif fanout_count > 1:
                    # 多扇出时至少预留一个向下分叉点，必要时再向右预留扩展空间。
                    self._ensure_coord_cache()
                    node_by_coord = {
                        coord: int(node)
                        for node, coord in self._node_coord.items()
                    }
                    down_occupant = node_by_coord.get((int(x), int(y + 1)))
                    right_occupant = node_by_coord.get((int(x + 1), int(y)))
                    if down_occupant is not None and down_occupant not in fanouts:
                        self.move_row_down(y+1 , 1)
                    if right_occupant is not None and right_occupant not in fanouts:
                        self.move_col_right(x+1 , 1)

    # 根据获取到的扇入方向，为布线node预留扇入口
    def reserve_fanin_ports(self):
        #遍历self.fanin_directions ，计算扇入的坐标位置有没有node，如果有node被占用，就要插入行和列
        for (fanin_id, node_id), direction in self.fanin_directions.items():
            fanin_id = int(fanin_id)
            node_id = int(node_id)
            x, y = self.get_node_coord(node_id)
            dx, dy = direction
            fanin_x = x + dx
            fanin_y = y + dy

            # 如果这里地方有node但是这个node是fanin_id的话，就不需要插入行和列get_node_coord可以获取扇入node的坐标
            fanin_coord = self.get_node_coord(fanin_id)
            if self.has_node_at_coord((fanin_x, fanin_y)) :
                if fanin_coord == (fanin_x, fanin_y):
                    continue
                elif dx == -1 and dy == 0:
                        # 左侧插入列
                    self.move_col_right(x , 1)
                elif dx == 0 and dy == -1:
                        # 上方插入行
                    self.move_row_down(y , 1)

    """
    ===============================================
    step2 不仅仅计算，而是真正的放置到棋盘格，进行数据的交互
    ===============================================
    """
    # 每次布局前，要清空棋盘格和之前所有的布线信息
    def clearMapChessboard(self):
        self.mapChessboard.reset()
        self.astar.reset()


    def place_all_nodes_on_chessboard(self):
        """
        遍历 GPU 张量里的坐标，把节点放置到 mapChessboard
        """
        self.clearMapChessboard()
        self._ensure_coord_cache()
        for node_id, (x, y) in self._node_coord.items():
            node_type = self.parse.get_node_type(node_id)
            self.mapChessboard.placeNode(node_id, (x, y), node_type)

    def _infer_route_direction(self, src, dst):
        key = (int(src), int(dst))
        if key in self.fanin_directions:
            return self.fanin_directions[key]
        src_x, _ = self.get_node_coord(src)
        dst_x, _ = self.get_node_coord(dst)
        if src_x < dst_x:
            return (-1, 0)
        return (0, -1)

    def _route_edge(self, src, dst, direction, fanout_direction=None):
        src = int(src)
        dst = int(dst)
        direction = (int(direction[0]), int(direction[1]))
        if getattr(self, "_uses_right_down_astar", False):
            _, src_y = self.get_node_coord(src)
            _, dst_y = self.get_node_coord(dst)
            if fanout_direction is not None:
                fanout_directions = (tuple(fanout_direction),)
            else:
                fanout_directions = (
                    ((1, 0),)
                    if dst_y == src_y
                    else (
                        ((0, 1), (1, 0))
                        if self.right_launch_fallback
                        else ((0, 1),)
                    )
                )
            for fanout_direction in fanout_directions:
                path = self.astar.route_with_dirs(
                    src,
                    dst,
                    fanout_direction,
                    direction,
                )
                if path:
                    return path
            return []
        path = self.astar.route_with_dirs(
            src,
            dst,
            0,
            1,
            direction[0],
            direction[1],
            True,
            True,
        )
        if path:
            return path
        return self.astar.route(src, dst)

    def _route_edge_with_direction_options(
        self,
        src,
        dst,
        preferred_direction,
        forbidden_directions=None,
    ):
        src = int(src)
        dst = int(dst)
        preferred_direction = (int(preferred_direction[0]), int(preferred_direction[1]))
        forbidden = {
            (int(direction[0]), int(direction[1]))
            for direction in (forbidden_directions or ())
        }
        # A two-input gate has two distinct physical sink ports (left/top).
        # Reserve ports by the paths already materialized on the board, not by
        # the initial preference table: the two fanins may legally swap their
        # preferred ports, but they may never occupy the same actual port.
        routes = getattr(
            getattr(self, "mapChessboard", None), "nodePairRoutes", {}
        )
        for (other_src, other_dst), raw_path in routes.items():
            if int(other_dst) != dst or int(other_src) == src:
                continue
            path = [(int(x), int(y)) for x, y in raw_path]
            if len(path) < 2:
                continue
            sink_x, sink_y = path[-1]
            port_x, port_y = path[-2]
            forbidden.add((port_x - sink_x, port_y - sink_y))

        directions = [preferred_direction]
        alternate = self._alternate_direction(preferred_direction)
        if alternate not in directions:
            directions.append(alternate)
        directions = [direction for direction in directions if direction not in forbidden]
        if not directions:
            return [], preferred_direction

        if getattr(self, "_uses_right_down_astar", False):
            _, src_y = self.get_node_coord(src)
            _, dst_y = self.get_node_coord(dst)
            fanout_directions = (
                ((1, 0),)
                if dst_y == src_y
                else (
                    ((0, 1), (1, 0))
                    if self.right_launch_fallback
                    else ((0, 1),)
                )
            )
            # Preserve the established sink-port portfolio for the preferred
            # down launch before trying the additional right-launch fallback.
            # This prevents a merely feasible right launch from replacing a
            # more compact alternate sink entry.
            for fanout_direction in fanout_directions:
                for direction in directions:
                    path = self._route_edge(
                        src,
                        dst,
                        direction,
                        fanout_direction=fanout_direction,
                    )
                    if path:
                        return path, direction
            return [], preferred_direction

        for direction in directions:
            path = self._route_edge(src, dst, direction)
            if path:
                return path, direction
        return [], preferred_direction

    def _alternate_direction(self, direction):
        d = (int(direction[0]), int(direction[1]))
        if d == (-1, 0):
            return (0, -1)
        return (-1, 0)

    def validate_gate_port_directions(self):
        """Verify that every routed fanin owns one distinct physical port."""
        violations = []
        bad_edges = set()
        sink_port_owners = defaultdict(list)
        routes = getattr(self.mapChessboard, "nodePairRoutes", {})

        for edge, raw_path in routes.items():
            src, dst = int(edge[0]), int(edge[1])
            path = [(int(x), int(y)) for x, y in raw_path]
            if len(path) < 2:
                bad_edges.add((src, dst))
                violations.append(((src, dst), "route has no physical sink port"))
                continue

            sink_x, sink_y = path[-1]
            port_x, port_y = path[-2]
            actual_direction = (port_x - sink_x, port_y - sink_y)
            sink_port_owners[(dst, (port_x, port_y))].append((src, dst))
            if actual_direction not in {(-1, 0), (0, -1)}:
                bad_edges.add((src, dst))
                violations.append(
                    (
                        (src, dst),
                        "fanin enters sink from illegal direction {}".format(
                            actual_direction
                        ),
                    )
                )

        for (dst, port), owners in sink_port_owners.items():
            if len(owners) <= 1:
                continue
            owners = sorted(set(owners))
            bad_edges.update(owners)
            message = "multiple fanins share input port {} of node {}".format(
                port, dst
            )
            violations.extend((edge, message) for edge in owners)

        self.last_port_direction_violations = list(violations)
        return not violations, violations, bad_edges

    def validate_route_topology(self):
        """Verify route endpoints, adjacency, and isolation from gate nodes."""
        self._ensure_coord_cache()
        violations = []
        bad_edges = set()
        node_owners = defaultdict(list)
        for node_id, coord in self._node_coord.items():
            node_owners[(int(coord[0]), int(coord[1]))].append(int(node_id))

        routes = getattr(self.mapChessboard, "nodePairRoutes", {})
        for edge, raw_path in routes.items():
            src, dst = int(edge[0]), int(edge[1])
            path = [(int(x), int(y)) for x, y in raw_path]
            edge_key = (src, dst)

            if len(path) < 2:
                bad_edges.add(edge_key)
                violations.append((edge_key, "route has fewer than two coordinates"))
                continue

            expected_start = self.get_node_coord(src)
            expected_end = self.get_node_coord(dst)
            if path[0] != expected_start or path[-1] != expected_end:
                bad_edges.add(edge_key)
                violations.append(
                    (
                        edge_key,
                        "route endpoints {} -> {} do not match nodes {} -> {}".format(
                            path[0], path[-1], expected_start, expected_end
                        ),
                    )
                )

            for previous, current in zip(path, path[1:]):
                if abs(current[0] - previous[0]) + abs(current[1] - previous[1]) != 1:
                    bad_edges.add(edge_key)
                    violations.append(
                        (
                            edge_key,
                            "route contains non-adjacent step {} -> {}".format(
                                previous, current
                            ),
                        )
                    )
                    break

            for coord in path[1:-1]:
                owners = node_owners.get(coord)
                if not owners:
                    continue
                bad_edges.add(edge_key)
                violations.append(
                    (
                        edge_key,
                        "route crosses logic node(s) {} at {}".format(
                            sorted(owners), coord
                        ),
                    )
                )
                break

        self.last_route_topology_violations = list(violations)
        return not violations, violations, bad_edges

    def _add_gate_port_failures(self, failed_pairs):
        failed_pairs = dict(failed_pairs or {})
        ports_ok, _, bad_edges = self.validate_gate_port_directions()
        topology_ok, _, topology_bad_edges = self.validate_route_topology()
        if ports_ok and topology_ok:
            return failed_pairs
        bad_edges.update(topology_bad_edges)
        for src, dst in bad_edges:
            failed_pairs.setdefault(
                (int(src), int(dst)),
                self._infer_route_direction(int(src), int(dst)),
            )
        return failed_pairs

    def _route_endpoint_coords(self, src, dst, direction):
        src_x, src_y = self.get_node_coord(src)
        dst_x, dst_y = self.get_node_coord(dst)
        dx, dy = int(direction[0]), int(direction[1])
        if dst_y == src_y:
            first_step = (src_x + 1, src_y)
        else:
            first_step = (src_x, src_y + 1)
        pre_goal = (dst_x + dx, dst_y + dy)
        return (src_x, src_y), (dst_x, dst_y), first_step, pre_goal

    def _is_launch_blocked(self, src, dst, direction):
        (_, _), (dst_x, dst_y), first_step, _ = self._route_endpoint_coords(src, dst, direction)
        return first_step != (dst_x, dst_y) and not self.mapChessboard.canPlaceWire(first_step)

    def _is_sink_blocked(self, src, dst, direction):
        (src_x, src_y), (_, _), _, pre_goal = self._route_endpoint_coords(src, dst, direction)
        return pre_goal != (src_x, src_y) and not self.mapChessboard.canPlaceWire(pre_goal)

    def _route_priority_key(self, pair):
        src, dst = int(pair[0]), int(pair[1])
        src_x, src_y = self.get_node_coord(src)
        dst_x, dst_y = self.get_node_coord(dst)
        fanouts = [
            int(fout) for fout in self.parse.get_fanouts(src)
            if int(fout) in self._node_id_to_idx
        ]
        fanout_count = len(fanouts)
        fanin_count = sum(
            int(fanin) in self._node_id_to_idx
            for fanin in self.parse.get_fanins(dst)
        )
        horizontal_span = abs(dst_x - src_x)
        vertical_span = max(0, dst_y - src_y)
        # Reserve scarce sink ports before long transit wires can occupy them.
        # The previous long-edge-first order was the dominant cause of final
        # source/sink port blockage in the full-circuit diagnostics.
        return (
            -fanin_count,
            horizontal_span + vertical_span,
            -fanout_count,
            horizontal_span,
            vertical_span,
            src,
            dst,
        )

    # 对每一层的布线对 (start, end) 按 |x_end - x_start| 从小到大排序。
    def sort_route_sequence(self):
        if self.parse.same_layer_route_pairs is None:
            self.parse.same_layer_route_pairs = {}
        if self.parse.differ_layer_route_pairs is None:
            self.parse.differ_layer_route_pairs = []

        for layer, pairs in self.parse.same_layer_route_pairs.items():
            sorted_pairs = sorted(
                pairs,
                key=self._route_priority_key,
            )
            self.parse.same_layer_route_pairs[layer] = sorted_pairs

        self.parse.differ_layer_route_pairs = sorted(
            self.parse.differ_layer_route_pairs,
            key=self._route_priority_key,
        )

    def sequence_route_all_edges(self, verbose=True):
        self.sort_route_sequence()
        failed_pairs = {}

        for layer in sorted(self.parse.same_layer_route_pairs.keys()):
            pairs = self.parse.same_layer_route_pairs[layer]
            for src, dst in pairs:
                direction = self._infer_route_direction(src, dst)
                path, direction = self._route_edge_with_direction_options(src, dst, direction)
                if not path:
                    if verbose:
                        print(
                            f"⚠️ 节点 {src}{self.get_node_coord(src)} 到 "
                            f"{dst}{self.get_node_coord(dst)} 的布线失败"
                        )
                    failed_pairs[(src, dst)] = direction
                    continue
                self.mapChessboard.savePath((src, dst), path)

        for src, dst in self.parse.differ_layer_route_pairs:
            direction = self._infer_route_direction(src, dst)
            path, direction = self._route_edge_with_direction_options(src, dst, direction)
            if not path:
                if verbose:
                    print(
                        f"⚠️ 节点 {src}{self.get_node_coord(src)} 到 "
                        f"{dst}{self.get_node_coord(dst)} 的布线失败"
                    )
                failed_pairs[(src, dst)] = direction
                continue
            self.mapChessboard.savePath((src, dst), path)

        failed_pairs = self._add_gate_port_failures(failed_pairs)
        if verbose:
            print(
                f"布线完成，合计布线数量: {self.parse.effective_edges_num}，"
                f"失败数量: {len(failed_pairs)}"
            )
        return failed_pairs

    def reroute_with_priority_pairs(
        self,
        priority_pairs,
        verbose=False,
        reverse_priority=False,
        reverse_remaining=False,
        explicit_priority_order=None,
    ):
        """
        清空并重布线：优先布线 priority_pairs，并且优先边尝试两种扇入方向。
        """
        all_pairs = self._materialize_priority_route_order(
            priority_pairs,
            reverse_priority=reverse_priority,
            reverse_remaining=reverse_remaining,
            explicit_priority_order=explicit_priority_order,
        )

        self.place_all_nodes_on_chessboard()
        failed_pairs = {}
        for src, dst in all_pairs:
            direction = self._infer_route_direction(src, dst)
            path, direction = self._route_edge_with_direction_options(src, dst, direction)

            if not path:
                if verbose:
                    print(
                        f"⚠️ 节点 {src}{self.get_node_coord(src)} 到 "
                        f"{dst}{self.get_node_coord(dst)} 的布线失败"
                    )
                failed_pairs[(src, dst)] = direction
                continue
            self.mapChessboard.savePath((src, dst), path)

        failed_pairs = self._add_gate_port_failures(failed_pairs)
        if verbose:
            print(
                f"布线完成，合计布线数量: {self.parse.effective_edges_num}，"
                f"失败数量: {len(failed_pairs)}"
            )
        return failed_pairs

    def _materialize_priority_route_order(
        self,
        priority_pairs,
        reverse_priority=False,
        reverse_remaining=False,
        explicit_priority_order=None,
    ):
        priority = {(int(s), int(d)) for s, d in priority_pairs}
        all_pairs = self._all_route_pairs()
        if explicit_priority_order:
            seen_priority = set()
            priority_pairs_ordered = []
            all_pair_set = set(all_pairs)
            for src, dst in explicit_priority_order:
                pair = (int(src), int(dst))
                if pair in all_pair_set and pair not in seen_priority:
                    priority_pairs_ordered.append(pair)
                    seen_priority.add(pair)
            priority_pairs_ordered.extend(
                pair
                for pair in all_pairs
                if pair in priority and pair not in seen_priority
            )
        else:
            priority_pairs_ordered = [pair for pair in all_pairs if pair in priority]
        remaining_pairs = [pair for pair in all_pairs if pair not in priority]
        if reverse_priority:
            priority_pairs_ordered.reverse()
        if reverse_remaining:
            remaining_pairs.reverse()
        return priority_pairs_ordered + remaining_pairs

    def _all_route_pairs(self):
        self.sort_route_sequence()
        all_pairs = []
        for layer in sorted(self.parse.same_layer_route_pairs.keys()):
            all_pairs.extend(
                (int(src), int(dst))
                for src, dst in self.parse.same_layer_route_pairs[layer]
            )
        all_pairs.extend(
            (int(src), int(dst))
            for src, dst in self.parse.differ_layer_route_pairs
        )
        return all_pairs

    def _net_grouped_route_order(self):
        return sorted(
            self._all_route_pairs(),
            key=lambda pair: (
                self.parse.get_layer_of_node(int(pair[0])),
                -len(self.parse.get_fanouts(int(pair[0]))),
                int(pair[0]),
                self._route_priority_key(pair),
            ),
        )

    def _sink_grouped_route_order(self):
        return sorted(
            self._all_route_pairs(),
            key=lambda pair: (
                -len(self.parse.get_fanins(int(pair[1]))),
                self.parse.get_layer_of_node(int(pair[1])),
                int(pair[1]),
                self._route_priority_key(pair),
            ),
        )

    def _blocking_route_pairs(self, failed_pairs):
        critical_coords = set()
        problem_nodes = set()
        for src, dst in failed_pairs:
            src = int(src)
            dst = int(dst)
            problem_nodes.update((src, dst))
            src_x, src_y = self.get_node_coord(src)
            dst_x, dst_y = self.get_node_coord(dst)
            critical_coords.add((int(src_x), int(src_y + 1)))
            critical_coords.add((int(dst_x - 1), int(dst_y)))
            critical_coords.add((int(dst_x), int(dst_y - 1)))

        blockers = set()
        for edge, path in self.mapChessboard.nodePairRoutes.items():
            normalized = (int(edge[0]), int(edge[1]))
            if normalized in failed_pairs:
                continue
            if any((int(x), int(y)) in critical_coords for x, y in path[1:-1]):
                blockers.add(normalized)
            elif normalized[0] in problem_nodes or normalized[1] in problem_nodes:
                blockers.add(normalized)
        return blockers

    def targeted_ripup_reroute(
        self,
        failed_pairs,
        max_attempts=16,
        verbose=False,
        baseline_order=None,
    ):
        """Search a small deterministic ordering of failed edges and blockers."""
        if not failed_pairs:
            return failed_pairs

        failed_list = sorted(
            ((int(src), int(dst)) for src, dst in failed_pairs),
            key=self._route_priority_key,
        )
        blockers = sorted(
            self._blocking_route_pairs(failed_pairs),
            key=self._route_priority_key,
        )
        candidates = []
        seen = set()

        def add_candidate(order):
            order = tuple(order)
            if order and order not in seen:
                seen.add(order)
                candidates.append(order)

        add_candidate(baseline_order or ())
        # Preserve the successful part of the checkpoint ordering and promote
        # a residual edge only as far as the wire that blocks its critical
        # port/corridor.  Moving every blocker to the front can solve the local
        # edge but needlessly destabilizes the already-routed global design.
        if baseline_order:
            stable_order = [
                (int(src), int(dst)) for src, dst in baseline_order
            ]
            stable_positions = {
                pair: index for index, pair in enumerate(stable_order)
            }
            stable_blockers = sorted(
                (pair for pair in blockers if pair in stable_positions),
                key=lambda pair: stable_positions[pair],
            )
            for failed_pair in failed_list:
                if failed_pair not in stable_positions:
                    continue
                for blocker_pair in stable_blockers:
                    promoted = [
                        pair for pair in stable_order if pair != failed_pair
                    ]
                    insert_at = promoted.index(blocker_pair)
                    promoted.insert(insert_at, failed_pair)
                    add_candidate(promoted)
        add_candidate(failed_list + blockers)
        add_candidate(list(reversed(failed_list)) + blockers)
        add_candidate(failed_list + list(reversed(blockers)))
        if len(failed_list) <= 6:
            for permutation in itertools.islice(
                itertools.permutations(failed_list),
                max(0, int(max_attempts)),
            ):
                add_candidate(list(permutation) + blockers)
        for offset in range(1, min(len(blockers), max(0, int(max_attempts))) + 1):
            add_candidate(
                failed_list + blockers[offset:] + blockers[:offset]
            )

        best_order = None
        best_failed = failed_pairs
        for order in candidates[:max(1, int(max_attempts))]:
            attempt_failed = self.reroute_with_priority_pairs(
                order,
                verbose=False,
                explicit_priority_order=order,
            )
            if verbose:
                print(
                    "[targeted rip-up attempt] priority_head={} failed={}".format(
                        list(order[:min(6, len(order))]),
                        sorted((int(src), int(dst)) for src, dst in attempt_failed),
                    )
                )
            if best_order is None or len(attempt_failed) < len(best_failed):
                best_failed = attempt_failed
                best_order = order
            if not attempt_failed:
                self._last_route_priority = set(order)
                self._last_route_reverse_priority = False
                self._last_route_reverse_remaining = False
                self._last_route_explicit_priority = tuple(order)
                if verbose:
                    print(
                        "[targeted rip-up] legal after {} explicit priority edges".format(
                            len(order)
                        )
                    )
                return attempt_failed

        if best_order is not None:
            best_failed = self.reroute_with_priority_pairs(
                best_order,
                verbose=False,
                explicit_priority_order=best_order,
            )
        return best_failed

    def targeted_corridor_expansion(
        self,
        failed_pairs,
        baseline_order,
        max_candidates=4,
        timeout_sec=5.0,
        verbose=False,
    ):
        """Open a small set of dedicated corridors around a residual failure.

        Global failure-driven expansion is effective while many nets are
        blocked, but it can oscillate once only one or two saturated monotone
        corridors remain.  Try several bounded endpoint/mid-span cuts from the
        same checkpoint, reroute the complete design, and retain only the best
        legal candidate.  This makes the operation transactional: an
        unsuccessful local repair cannot enlarge or degrade the saved layout.
        """
        if not failed_pairs or len(failed_pairs) > 4:
            return failed_pairs

        started = time.perf_counter()
        checkpoint_coords = self.coords.clone()
        checkpoint_order = tuple(baseline_order or self._all_route_pairs())
        best_coords = checkpoint_coords.clone()
        best_order = checkpoint_order
        best_failed = failed_pairs
        plans = []
        seen_plans = set()

        def add_plan(row_ops, col_ops, label):
            normalized = (
                tuple(sorted(set(int(value) for value in row_ops))),
                tuple(sorted(set(int(value) for value in col_ops))),
            )
            if (not normalized[0] and not normalized[1]) or normalized in seen_plans:
                return
            seen_plans.add(normalized)
            plans.append((normalized[0], normalized[1], label))

        launch_rows = []
        sink_rows = []
        sink_cols = []
        midpoint_rows = []
        midpoint_cols = []
        launch_cols = []
        for (src, dst), direction in failed_pairs.items():
            src_x, src_y = self.get_node_coord(int(src))
            dst_x, dst_y = self.get_node_coord(int(dst))
            launch_rows.append(int(src_y + 1))
            if dst_x > src_x:
                launch_cols.append(int(src_x + 1))
                midpoint_cols.append(int((src_x + dst_x + 1) // 2))
            if dst_y > src_y + 1:
                midpoint_rows.append(int((src_y + dst_y + 1) // 2))
            if tuple(direction) == (0, -1):
                sink_rows.append(int(dst_y))
            else:
                sink_cols.append(int(dst_x))

        # First reserve only endpoint capacity; then add one orthogonal bypass.
        # The last candidate combines both axes for a completely saturated
        # monotone rectangle.
        add_plan(launch_rows + sink_rows, sink_cols, "endpoint")
        add_plan(
            launch_rows + midpoint_rows + sink_rows,
            sink_cols,
            "horizontal-bypass",
        )
        add_plan(
            launch_rows + sink_rows,
            launch_cols + midpoint_cols + sink_cols,
            "vertical-bypass",
        )
        add_plan(
            launch_rows + midpoint_rows + sink_rows,
            launch_cols + midpoint_cols + sink_cols,
            "two-axis-bypass",
        )

        for row_ops, col_ops, label in plans[:max(1, int(max_candidates))]:
            if time.perf_counter() - started >= max(0.1, float(timeout_sec)):
                break
            self.coords = checkpoint_coords.clone()
            self._coord_cache_dirty = True
            self.move_rows_and_cols(row_ops, col_ops)
            if self._right_down_invariant_violations():
                continue
            self._refresh_fanin_directions_for_current_coords()
            if verbose:
                # Distinguish a geometric/port-capacity impossibility from an
                # ordering conflict.  The probe is discarded by the complete
                # reroute immediately below.
                self.place_all_nodes_on_chessboard()
                isolated = {}
                for src, dst in failed_pairs:
                    preferred = self._infer_route_direction(src, dst)
                    path, used_direction = self._route_edge_with_direction_options(
                        src, dst, preferred
                    )
                    isolated[(int(src), int(dst))] = {
                        "path_length": int(len(path)),
                        "direction": tuple(int(value) for value in used_direction),
                    }
                print("[targeted corridor probe] {} {}".format(label, isolated))
            attempt_failed = self.reroute_with_priority_pairs(
                checkpoint_order,
                verbose=False,
                explicit_priority_order=checkpoint_order,
            )
            attempt_failed = self.targeted_ripup_reroute(
                attempt_failed,
                max_attempts=12,
                verbose=False,
                baseline_order=checkpoint_order,
            )
            width, height = self.mapChessboard.computeLayoutArea()
            event = {
                "round": int(len(self.route_expansion_history) + 1),
                "previous_failed": int(len(failed_pairs)),
                "failed": int(len(attempt_failed)),
                "inserted_rows": [int(value) for value in row_ops],
                "inserted_cols": [int(value) for value in col_ops],
                "shifted_target_rows": 0,
                "propagated_right_down_moves": 0,
                "route_variant": "targeted-corridor-{}".format(label),
                "width": int(max(0, width)),
                "height": int(max(0, height)),
                "elapsed_sec": float(time.perf_counter() - started),
            }
            self.route_expansion_history.append(event)
            if verbose:
                print(
                    "[targeted corridor] {} failed {}->{} rows={} cols={} area={}x{} edges={}".format(
                        label,
                        len(failed_pairs),
                        len(attempt_failed),
                        list(row_ops),
                        list(col_ops),
                        event["width"],
                        event["height"],
                        sorted((int(src), int(dst)) for src, dst in attempt_failed),
                    )
                )
            if len(attempt_failed) < len(best_failed):
                best_failed = attempt_failed
                best_coords = self.coords.clone()
                best_order = tuple(
                    self._last_route_explicit_priority or checkpoint_order
                )
            if not attempt_failed:
                self._last_route_explicit_priority = tuple(
                    self._last_route_explicit_priority or checkpoint_order
                )
                return attempt_failed

        self.coords = best_coords
        self._coord_cache_dirty = True
        self._refresh_fanin_directions_for_current_coords()
        best_failed = self.reroute_with_priority_pairs(
            best_order,
            verbose=False,
            explicit_priority_order=best_order,
        )
        return best_failed

    def _collect_used_coords(self):
        self._ensure_coord_cache()
        used_coords = set()
        for _, coord in self._node_coord.items():
            used_coords.add((int(coord[0]), int(coord[1])))
        for _, path in self.mapChessboard.nodePairRoutes.items():
            for x, y in path:
                used_coords.add((int(x), int(y)))
        return used_coords

    def _build_clock_template_constraints(self):
        constraints = []
        for node_pair, path in self.mapChessboard.nodePairRoutes.items():
            if not path or len(path) < 2:
                continue
            norm_path = [(int(x), int(y)) for x, y in path]
            for i in range(len(norm_path) - 1):
                u = norm_path[i]
                v = norm_path[i + 1]
                constraints.append((u, v, 1, node_pair))  # phase(v)=phase(u)+1 mod 4
                constraints.append((v, u, 3, node_pair))  # reverse relation
        return constraints

    def solve_clock_template_consistency(self):
        used_coords = self._collect_used_coords()
        phase_graph = defaultdict(list)
        for u, v, delta, node_pair in self._build_clock_template_constraints():
            phase_graph[u].append((v, delta, node_pair))

        template_phase_map = {}
        conflicts = []
        seen_conflicts = set()

        for seed in used_coords:
            if seed in template_phase_map:
                continue
            # 2DDWave 下相位由坐标模板隐式决定，这里做的是一致性传播/校验。
            template_phase_map[seed] = self.mapChessboard.getTDDPhaseAtCoord(seed)
            stack = [seed]

            while stack:
                u = stack.pop()
                pu = template_phase_map[u]
                for v, delta, node_pair in phase_graph.get(u, []):
                    expected = (pu + delta) % 4
                    if v not in template_phase_map:
                        template_phase_map[v] = expected
                        stack.append(v)
                        continue
                    if template_phase_map[v] == expected:
                        continue

                    key = (
                        min(u, v),
                        max(u, v),
                        int(node_pair[0]),
                        int(node_pair[1]),
                    )
                    if key not in seen_conflicts:
                        seen_conflicts.add(key)
                        conflicts.append((u, v, node_pair, expected, template_phase_map[v]))

        # 路径未覆盖到的坐标（纯 node 孤点）按 2DDWave 模板补全
        for coord in used_coords:
            if coord not in template_phase_map:
                template_phase_map[coord] = self.mapChessboard.getTDDPhaseAtCoord(coord)

        return template_phase_map, conflicts

    def apply_clock_template_phases(self, template_phase_map):
        for coord, phase in template_phase_map.items():
            self.mapChessboard.setPhase(coord, int(phase))

    def materialize_template_phases_for_used_coords(self):
        for coord in self._collect_used_coords():
            phase = self.mapChessboard.getTDDPhaseAtCoord(coord)
            self.mapChessboard.trySetPhase(coord, phase)

    def verify_clock_template_consistency(self, verbose=True):
        template_phase_map, conflicts = self.solve_clock_template_consistency()
        if not conflicts:
            self.apply_clock_template_phases(template_phase_map)
            self.clock_template_ok = True
            self.clock_template_conflict_count = 0
            self._sync_legacy_phase_status()
            if verbose:
                print(f"2DDWave 模板一致性校验成功，已写入相位坐标数量: {len(template_phase_map)}")
            return True, []
        self.clock_template_ok = False
        self.clock_template_conflict_count = len(conflicts)
        self._sync_legacy_phase_status()
        if verbose:
            print(f"2DDWave 模板冲突数量: {len(conflicts)}")
        return False, conflicts

    # Backward-compatible aliases.
    def solve_phase_assignment(self):
        return self.solve_clock_template_consistency()

    def apply_phase_assignment(self, phase_assignment):
        return self.apply_clock_template_phases(phase_assignment)

    def assign_template_phases_for_used_coords(self):
        return self.materialize_template_phases_for_used_coords()

    def optimize_phase_assignment(self, verbose=True):
        return self.verify_clock_template_consistency(verbose=verbose)

    def failed_pairs_to_insert_ops(self, failed_pairs, max_ops_per_iter=4):
        row_pressure = Counter()
        col_pressure = Counter()
        for (src, dst), direction in failed_pairs.items():
            src_x, src_y = self.get_node_coord(src)
            dst_x, dst_y = self.get_node_coord(dst)
            launch_blocked = self._is_launch_blocked(src, dst, direction)
            sink_blocked = self._is_sink_blocked(src, dst, direction)

            # Every right/down route launches below its source.  Repeated
            # failures sharing a source row therefore vote strongly for one
            # dedicated horizontal corridor immediately below that row.
            row_pressure[int(src_y + 1)] += 8 if launch_blocked else 4

            if launch_blocked:
                if src_x < dst_x:
                    col_pressure[int(src_x + 1)] += 5

            if sink_blocked:
                if direction == (0, -1):
                    row_pressure[int(dst_y)] += 8
                elif direction == (-1, 0):
                    col_pressure[int(dst_x)] += 8

            if direction == (0, -1):
                row_pressure[int(dst_y)] += 5
            elif direction == (-1, 0):
                col_pressure[int(dst_x)] += 5
            else:
                row_pressure[int(dst_y)] += 3

            # Mid-span cuts create a bypass through the congested rectangle,
            # while endpoint cuts reserve legal launch/sink ports.
            if dst_x - src_x > 1:
                col_pressure[int((src_x + dst_x + 1) // 2)] += 3
                col_pressure[int(src_x + 1)] += 2
            if dst_y - src_y > 1:
                row_pressure[int((src_y + dst_y + 1) // 2)] += 3

        limit = max(1, int(max_ops_per_iter))
        row_ops = [
            cut for cut, _ in sorted(row_pressure.items(), key=lambda item: (-item[1], item[0]))[:limit]
        ]
        col_ops = [
            cut for cut, _ in sorted(col_pressure.items(), key=lambda item: (-item[1], item[0]))[:limit]
        ]
        row_ops.sort()
        col_ops.sort()
        return row_ops, col_ops

    def _move_row_segment_right(self, row_y, start_x, delta):
        if delta <= 0:
            return
        mask = (self.coords[:, 1] == int(row_y)) & (self.coords[:, 0] >= int(start_x))
        self.coords[mask, 0] += int(delta)
        self._coord_cache_dirty = True

    def _embedding_similarity(self, node_a, node_b):
        idx_a = self.parse.node_to_index.get(int(node_a))
        idx_b = self.parse.node_to_index.get(int(node_b))
        if idx_a is None or idx_b is None:
            return 0.0
        emb_a = self.embeddings[idx_a]
        emb_b = self.embeddings[idx_b]
        na = np.linalg.norm(emb_a)
        nb = np.linalg.norm(emb_b)
        if na == 0 or nb == 0:
            return 0.0
        sim = float(np.dot(emb_a, emb_b) / (na * nb))
        return max(-1.0, min(1.0, sim))

    def ml_guided_node_reposition(self, failed_pairs, max_nodes=None, max_shift=None):
        """
        使用确定性结构特征估计失败边“修复收益”，优先移动高收益目的节点。

        方法名为兼容既有调用保留；生产排序已不再训练 GCN。
        """
        if max_nodes is None:
            max_nodes = int(self.ml_tuning["ml_max_nodes"])
        if max_shift is None:
            max_shift = int(self.ml_tuning["ml_max_shift"])
        similarity_weight = float(self.ml_tuning["ml_similarity_weight"])
        extra_shift_scale = float(self.ml_tuning["ml_extra_shift_scale"])
        base_score = float(self.ml_tuning["ml_base_score"])

        pressure = defaultdict(float)
        target_x = {}
        moved = 0

        for (src, dst), direction in failed_pairs.items():
            src_x, _ = self.get_node_coord(src)
            dst_x, _ = self.get_node_coord(dst)
            sim = self._embedding_similarity(src, dst)
            # 相似度越低，说明结构相关性越弱，通常需要更大几何缓冲区
            ml_factor = 1.0 - (sim + 1.0) / 2.0

            margin = 1 if direction == (-1, 0) else 0
            required_x = src_x + margin
            violation = max(0, required_x - dst_x)
            score = base_score + violation + similarity_weight * ml_factor
            pressure[dst] += score

            ml_extra = int(round(extra_shift_scale * ml_factor))
            target = required_x + ml_extra
            target_x[dst] = max(target_x.get(dst, dst_x), target)

        ranked_nodes = sorted(pressure.items(), key=lambda kv: kv[1], reverse=True)[:max_nodes]
        for dst, _ in ranked_nodes:
            cur_x, cur_y = self.get_node_coord(dst)
            wanted_x = target_x.get(dst, cur_x)
            shift = min(max_shift, max(0, int(wanted_x - cur_x)))
            if shift <= 0:
                continue
            self._move_row_segment_right(cur_y, cur_x, shift)
            moved += 1

        return moved

    def separate_failed_vertical_channels(self, failed_pairs, max_nodes=8):
        """Open horizontal freedom for failed near-vertical connections.

        A purely global column insertion cannot separate a source and sink that
        share the same x coordinate.  Shift the sink row segment just enough to
        provide a left-entry port and a one-column detour; later outer-in
        contraction removes the margin when it is not required.
        """
        pressure = Counter()
        for src, dst in failed_pairs:
            src_x, _ = self.get_node_coord(src)
            dst_x, _ = self.get_node_coord(dst)
            if dst_x - src_x < 2:
                pressure[int(dst)] += 1

        moved = 0
        for dst, _ in sorted(pressure.items(), key=lambda item: (-item[1], item[0])):
            if moved >= max(1, int(max_nodes)):
                break
            dst_x, dst_y = self.get_node_coord(dst)
            required_x = dst_x
            for src in self.parse.get_fanins(int(dst)):
                src = int(src)
                if src not in self._node_id_to_idx:
                    continue
                src_x, _ = self.get_node_coord(src)
                required_x = max(required_x, int(src_x) + 2)
            shift = max(0, int(required_x - dst_x))
            if shift <= 0:
                continue
            self._move_row_segment_right(dst_y, dst_x, shift)
            moved += 1
        if moved:
            self._refresh_fanin_directions_for_current_coords()
        return moved

    def clock_template_conflicts_to_insert_ops(self, conflicts, max_ops_per_iter=4):
        row_ops = []
        col_ops = []
        for u, v, _, _, _ in conflicts:
            row_ops.extend([int(u[1]), int(v[1])])
            col_ops.extend([int(u[0]), int(v[0])])
        row_ops = sorted(set(row_ops))[:max_ops_per_iter]
        col_ops = sorted(set(col_ops))[:max_ops_per_iter]
        return row_ops, col_ops

    def phase_conflicts_to_insert_ops(self, conflicts, max_ops_per_iter=4):
        return self.clock_template_conflicts_to_insert_ops(
            conflicts,
            max_ops_per_iter=max_ops_per_iter,
        )

    def route_until_success(
        self,
        verbose=False,
        max_expansion_rounds=None,
        timeout_sec=None,
    ):
        """Route all edges, expanding failed regions until legal or exhausted.

        Row/column insertion preserves the right/down partial order.  Expansion
        is intentionally not rolled back merely because one round reaches a
        plateau: a new corridor can require several cuts before it connects
        both endpoint ports.
        """
        max_rounds = (
            self.route_expansion_rounds
            if max_expansion_rounds is None
            else max(0, int(max_expansion_rounds))
        )
        time_limit = (
            self.route_expansion_timeout_sec
            if timeout_sec is None
            else max(0.1, float(timeout_sec))
        )
        violations = self._right_down_invariant_violations()
        if violations:
            raise RuntimeError(
                "right/down placement invariant violated for {} edges".format(len(violations))
            )

        started = time.perf_counter()
        self.route_expansion_exhausted = False
        self.place_all_nodes_on_chessboard()
        failed_pairs = self.sequence_route_all_edges(verbose=verbose)
        if not failed_pairs:
            self._last_route_priority = set()
            self._last_route_reverse_priority = False
            self._last_route_reverse_remaining = False
            self._last_route_explicit_priority = tuple()

        # A compact placement can be routable but unlucky under the first
        # greedy net order.  Search a bounded, deterministic rip-up portfolio
        # on the unchanged geometry before opening any new rows or columns.
        # This is especially important for dense multi-fanout cones: expanding
        # first permanently inflates the board even when promoting the blocked
        # nets would have resolved the same physical corridors.  Every trial
        # still uses the regular port-aware router, and the accepted result is
        # checked again by the normal topology/template validation pipeline.
        if failed_pairs:
            failed_pairs = self.targeted_ripup_reroute(
                failed_pairs,
                max_attempts=max(
                    1,
                    int(os.environ.get("IFCN_PRE_EXPANSION_ROUTE_ATTEMPTS", "64")),
                ),
                verbose=verbose,
                baseline_order=tuple(self._all_route_pairs()),
            )

        best_failed_count = len(failed_pairs)
        rounds_without_improvement = 0
        failure_signature_hits = Counter()
        if failed_pairs:
            failure_signature_hits[
                tuple(sorted((int(src), int(dst)) for src, dst in failed_pairs))
            ] += 1
        best_failed_coords = self.coords.clone()
        best_failed_order = tuple(self._all_route_pairs())

        for round_index in range(max_rounds):
            if not failed_pairs:
                break
            elapsed = time.perf_counter() - started
            if elapsed >= time_limit:
                self.route_expansion_exhausted = True
                break

            per_axis_limit = min(
                int(self.ml_tuning["route_insert_max_ops"]),
                max(2, int(math.ceil(math.sqrt(len(failed_pairs))))),
            )
            shifted_targets = self.separate_failed_vertical_channels(
                failed_pairs,
                max_nodes=per_axis_limit,
            )
            # A local sink-row shift can also move unrelated gates on that
            # row.  Propagate every new x requirement through the downstream
            # DAG before inserting global corridors; otherwise those moved
            # gates may end up to the right of their fanouts.
            propagated_moves = 0
            if shifted_targets:
                propagated_moves = self.legalize_right_down_ports(
                    max_passes=max(8, len(self.parse.layer_nodes))
                )
            row_ops, col_ops = self.failed_pairs_to_insert_ops(
                failed_pairs,
                max_ops_per_iter=per_axis_limit,
            )
            if not row_ops and not col_ops:
                self.route_expansion_exhausted = True
                break

            previous_count = len(failed_pairs)
            self.move_rows_and_cols(row_ops, col_ops)
            violations = self._right_down_invariant_violations()
            if violations:
                raise RuntimeError(
                    "row/column expansion broke right/down invariant for {} edges".format(
                        len(violations)
                    )
                )
            self._refresh_fanin_directions_for_current_coords()

            # Alternate failed-first and global congestion order.  Failed-first
            # opens the new local corridor; the global order prevents that edge
            # from permanently starving unrelated high-fanout nets.
            route_variant = round_index % 6
            if route_variant in (0, 2):
                attempt_priority = set(failed_pairs.keys())
                current_route_order = tuple(
                    self._materialize_priority_route_order(
                        attempt_priority,
                        reverse_priority=(route_variant == 2),
                    )
                )
                failed_pairs = self.reroute_with_priority_pairs(
                    attempt_priority,
                    verbose=False,
                    reverse_priority=(route_variant == 2),
                )
                if not failed_pairs:
                    self._last_route_priority = attempt_priority
                    self._last_route_reverse_priority = route_variant == 2
                    self._last_route_reverse_remaining = False
                    self._last_route_explicit_priority = tuple()
            elif route_variant == 1:
                current_route_order = tuple(self._all_route_pairs())
                self.place_all_nodes_on_chessboard()
                failed_pairs = self.sequence_route_all_edges(verbose=False)
                if not failed_pairs:
                    self._last_route_priority = set()
                    self._last_route_reverse_priority = False
                    self._last_route_reverse_remaining = False
                    self._last_route_explicit_priority = tuple()
            elif route_variant == 3:
                current_route_order = tuple(
                    self._materialize_priority_route_order(
                        (),
                        reverse_remaining=True,
                    )
                )
                failed_pairs = self.reroute_with_priority_pairs(
                    (),
                    verbose=False,
                    reverse_remaining=True,
                )
                if not failed_pairs:
                    self._last_route_priority = set()
                    self._last_route_reverse_priority = False
                    self._last_route_reverse_remaining = True
                    self._last_route_explicit_priority = tuple()
            else:
                explicit_order = (
                    self._net_grouped_route_order()
                    if route_variant == 4 else
                    self._sink_grouped_route_order()
                )
                current_route_order = tuple(explicit_order)
                failed_pairs = self.reroute_with_priority_pairs(
                    explicit_order,
                    verbose=False,
                    explicit_priority_order=explicit_order,
                )
                if not failed_pairs:
                    self._last_route_priority = set(explicit_order)
                    self._last_route_reverse_priority = False
                    self._last_route_reverse_remaining = False
                    self._last_route_explicit_priority = tuple(explicit_order)

            current_failed_count = len(failed_pairs)
            current_signature = tuple(
                sorted((int(src), int(dst)) for src, dst in failed_pairs)
            )
            failure_signature_hits[current_signature] += 1
            if current_failed_count < best_failed_count:
                best_failed_count = current_failed_count
                rounds_without_improvement = 0
                best_failed_coords = self.coords.clone()
                best_failed_order = tuple(current_route_order)
            else:
                rounds_without_improvement += 1

            width, height = self.mapChessboard.computeLayoutArea()
            event = {
                "round": int(len(self.route_expansion_history) + 1),
                "previous_failed": int(previous_count),
                "failed": int(len(failed_pairs)),
                "inserted_rows": [int(value) for value in row_ops],
                "inserted_cols": [int(value) for value in col_ops],
                "shifted_target_rows": int(shifted_targets),
                "propagated_right_down_moves": int(propagated_moves),
                "route_variant": (
                    "failed-first"
                    if route_variant == 0 else
                    "global"
                    if route_variant == 1 else
                    "failed-first-reverse"
                    if route_variant == 2 else
                    "global-reverse"
                    if route_variant == 3 else
                    "net-grouped"
                    if route_variant == 4 else
                    "sink-grouped"
                ),
                "width": int(max(0, width)),
                "height": int(max(0, height)),
                "elapsed_sec": float(time.perf_counter() - started),
            }
            self.route_expansion_history.append(event)
            if verbose:
                print(
                    "[route expand] round={} failed {}->{} rows={} cols={} area={}x{}".format(
                        event["round"],
                        previous_count,
                        len(failed_pairs),
                        row_ops,
                        col_ops,
                        event["width"],
                        event["height"],
                    )
                )

            # Expansion is useful through short plateaus, but a repeated
            # failure set with no new best for many rounds is a routing-order
            # or port-capacity impasse.  Continuing only produces a huge board.
            if (
                failed_pairs
                and rounds_without_improvement >= 12
                and failure_signature_hits[current_signature] >= 3
            ):
                self.route_expansion_exhausted = True
                pressure_note = (
                    "; {} node(s) have more than two fanins"
                ).format(len(self.right_down_port_capacity_nodes)) if (
                    self.right_down_port_capacity_nodes
                ) else ""
                self.route_incompatibility_reason = (
                    "routing stagnated at best_failed={} for {} rounds{}"
                ).format(
                    best_failed_count,
                    rounds_without_improvement,
                    pressure_note,
                )
                if verbose:
                    print("[route expand stop] " + self.route_incompatibility_reason)
                break

        if failed_pairs and best_failed_order:
            # Restore the smallest observed failure core before targeted
            # rip-up.  Without this checkpoint a later route-order trial can
            # overwrite a one-edge near-solution with a much worse board.
            self.coords = best_failed_coords
            self._coord_cache_dirty = True
            self._refresh_fanin_directions_for_current_coords()
            failed_pairs = self.reroute_with_priority_pairs(
                best_failed_order,
                verbose=False,
                explicit_priority_order=best_failed_order,
            )
            failed_pairs = self.targeted_ripup_reroute(
                failed_pairs,
                max_attempts=16,
                verbose=verbose,
                baseline_order=best_failed_order,
            )
            if failed_pairs:
                failed_pairs = self.targeted_corridor_expansion(
                    failed_pairs,
                    baseline_order=best_failed_order,
                    max_candidates=4,
                    timeout_sec=min(5.0, max(1.0, time_limit * 0.2)),
                    verbose=verbose,
                )
            if not failed_pairs:
                self.route_expansion_exhausted = False
                self.route_incompatibility_reason = ""

        if failed_pairs and (
            len(self.route_expansion_history) >= max_rounds
            or time.perf_counter() - started >= time_limit
        ):
            self.route_expansion_exhausted = True
        return failed_pairs

    def _route_with_repair(self, verbose=False, repair_iters=2, use_ml_reposition=True):
        _ = use_ml_reposition  # compatibility with legacy callers
        return self.route_until_success(
            verbose=verbose,
            max_expansion_rounds=max(0, int(repair_iters)),
        )

    def print_latex(self, filePath=None, failed_pairs=None):
        filePath = filePath or './results/normal_Latex'
        os.makedirs(filePath, exist_ok=True)
        iFCN_Lab.MapChessboard.outputTexFile(self.mapChessboard, self.parse.fileName, filePath)
        save_path = os.path.join(filePath, f"{self.parse.fileName}.tex")
        highlight_pairs = self.last_failed_pairs if failed_pairs is None else failed_pairs
        self._highlight_failed_endpoints_in_tex(save_path, highlight_pairs)
        print(f"[✅] Latex file generated: {save_path}")

    def one_step_optimization(
        self,
        verbose=False,
        route_repair_iters=3,
        phase_repair_iters=2,
        global_place_iters=5,
        compact_iters=64,
        ml_tuning=None,
        snapshot_dir=None,
    ):
        self.clock_template_ok = False
        self.clock_template_conflict_count = 0
        self._sync_legacy_phase_status()
        self._stage_snapshot_counter = 0
        self.route_expansion_history = []
        self.route_expansion_exhausted = False
        self.route_incompatibility_reason = ""
        self._last_route_priority = set()
        self._last_route_reverse_priority = False
        self._last_route_reverse_remaining = False
        self._last_route_explicit_priority = tuple()
        self.contraction_history = []
        self.contraction_evaluations = 0
        self.contraction_global_evaluations = 0
        self.contraction_recursive_evaluations = 0
        self.contraction_empty_line_evaluations = 0
        self.contraction_layer_merge_evaluations = 0
        self.contraction_exhausted = False
        self.contraction_runtime_sec = 0.0
        if ml_tuning:
            self.set_ml_tuning(**ml_tuning)

        # 1) Graphviz+sifting order has already been computed.  Materialize a
        # compact placement whose every fanout remains strictly right/down.
        self.caculate_rough_placement()
        if not self.skip_port_reservation:
            self.caculate_ports_reservation()
        port_legalization_moves = self.legalize_right_down_ports()
        if verbose and port_legalization_moves:
            print("[port legalize] shifted row segments: {}".format(port_legalization_moves))
        violations = self._right_down_invariant_violations()
        if violations:
            raise RuntimeError(
                "initial 2DDWave placement is not right/down reachable for {} edges".format(
                    len(violations)
                )
            )

        # 2) Route, and on every failure insert rows/columns selected from the
        # failed endpoint pressure map.  Legacy finite repair/global-move knobs
        # are accepted for CLI compatibility but no longer terminate a plateau.
        _ = (route_repair_iters, global_place_iters)
        failed_pairs = self.route_until_success(verbose=verbose)
        self._snapshot_stage_heatmap(
            snapshot_dir,
            "stage2_route_{}".format("success" if not failed_pairs else "exhausted"),
            failed_pairs=failed_pairs,
        )
        if failed_pairs:
            if verbose:
                print(
                    "路由扩容预算耗尽，仍有失败边 {}，不导出为合法版图".format(
                        len(failed_pairs)
                    )
                )
            self._record_failed_pairs(failed_pairs)
            return failed_pairs

        # 3) Validate the 2DDWave phase template.  A phase conflict triggers
        # geometric expansion followed by the same all-edge routing closure.
        template_rounds = max(
            self.template_expansion_rounds,
            max(0, int(phase_repair_iters)) + 1,
        )
        conflicts = []
        for template_round in range(template_rounds):
            template_ok, conflicts = self.verify_clock_template_consistency(verbose=verbose)
            if template_ok:
                # 4) Only a completely legal result may be contracted.  Nodes
                # absorb adjacent cells of their own single wires in top-down
                # and bottom-up sweeps, recursively compact internal layers,
                # delete blank rows/columns, and merge compatible occupied
                # layers; every rejected action restores and reroutes the
                # preceding legal state.
                contraction_started = time.perf_counter()
                compact_reductions = self.compact_layout(
                    max_iters=compact_iters,
                    verbose=verbose,
                )
                self.contraction_runtime_sec += float(
                    time.perf_counter() - contraction_started
                )
                # ``compact_layout`` validates and reroutes every accepted cut;
                # rerouting once more with a different edge order can destroy
                # a legal dense solution and trigger needless re-expansion.
                template_ok, conflicts = self.verify_clock_template_consistency(verbose=False)
                if not template_ok:
                    # Compaction changed path parity; expand at the conflict in
                    # the next loop instead of returning a stale success flag.
                    if verbose:
                        print("紧凑化后模板需要重新扩容，冲突数: {}".format(len(conflicts)))
                else:
                    if compact_reductions > 0:
                        self._snapshot_stage_heatmap(
                            snapshot_dir,
                            f"stage4_compacted_{compact_reductions}",
                            failed_pairs=failed_pairs,
                        )
                    self._snapshot_stage_heatmap(
                        snapshot_dir,
                        "stage4_legal_cell_layout",
                        failed_pairs=failed_pairs,
                    )
                    self._record_failed_pairs(failed_pairs)
                    return failed_pairs

            row_ops, col_ops = self.clock_template_conflicts_to_insert_ops(
                conflicts,
                max_ops_per_iter=int(self.ml_tuning["phase_insert_max_ops"]),
            )
            if not row_ops and not col_ops:
                break
            if verbose:
                print(
                    "[template expand] round={} rows={} cols={} conflicts={}".format(
                        template_round + 1,
                        row_ops,
                        col_ops,
                        len(conflicts),
                    )
                )
            self.move_rows_and_cols(row_ops, col_ops)
            failed_pairs = self.route_until_success(verbose=False)
            self._snapshot_stage_heatmap(
                snapshot_dir,
                f"stage3_template_expand_{template_round + 1}_failed_{len(failed_pairs)}",
                failed_pairs=failed_pairs,
            )
            if failed_pairs:
                self._record_failed_pairs(failed_pairs)
                return failed_pairs

        self.clock_template_ok = False
        self.clock_template_conflict_count = len(conflicts)
        self._sync_legacy_phase_status()
        self._snapshot_stage_heatmap(
            snapshot_dir,
            f"stage3_template_expansion_exhausted_{len(conflicts)}",
            failed_pairs=failed_pairs,
        )
        self._record_failed_pairs(failed_pairs)
        return failed_pairs
#     def cluster_nodes(self, num_clusters=6):
#         """
#         使用 KMeans 对节点进行聚类。
#         返回: Dict[cluster_id -> List[orig_id]]
#         """
#         embeddings = self.embeddings
#         gnn2orig = {v: k for k, v in self.parse.node_to_index.items()}

#         kmeans = KMeans(n_clusters=num_clusters, random_state=42, n_init='auto')
#         labels = kmeans.fit_predict(embeddings)

#         clusters = defaultdict(list)
#         for gnn_idx, cluster_id in enumerate(labels):
#             orig_id = gnn2orig[gnn_idx]
#             clusters[cluster_id].append(orig_id)
#         return clusters

#     def evaluate_clustering(self, clusters: dict):
#         """
#         clusters: Dict[cluster_id -> List[node_id]]
#         return: dict of evaluation metrics
#         """
#         node2cluster = {}
#         for cid, nodes in clusters.items():
#             for nid in nodes:
#                 node2cluster[nid] = cid

#         # 1. Cut size
#         cut_edges = [(u,v) for u,v in self.edges if node2cluster.get(u) != node2cluster.get(v)]
#         cut_size = len(cut_edges)

#         # 2. Balance
#         sizes = [len(v) for v in clusters.values()]
#         size_max = max(sizes)
#         size_min = min(sizes)
#         balance_ratio = size_max / max(1, size_min)

#         # 3. 每簇层级跨度
#         # 原来（报错）
#         # 修复版本：适配 dict[int → List[int]]
#         layer_map = {nid: l for l, nodes in self.barycenter_opt_layers.items() for nid in nodes}
#         level_spans = []
#         for cid, nodes in clusters.items():
#             levels = [layer_map[n] for n in nodes if n in layer_map]
#             if levels:
#                 span = max(levels) - min(levels)
#                 level_spans.append(span)
#         avg_level_span = np.mean(level_spans)

#         return {
#             'cut_size': cut_size,
#             'balance_ratio': balance_ratio,
#             'avg_level_span': avg_level_span
#         }

#     def show_cluster_colored_graph(self, clusters, dir_path="results/clustered"):
#         node2cluster = {nid: cid for cid, nids in clusters.items() for nid in nids}
#         visualize_layered_graph_sorted(
#             self.parse,
#             self.barycenter_opt_layers,
#             self.edges,
#             self.parse.fileName,
#             dir_path,
#             node2cluster=node2cluster,
#             num_clusters=len(clusters)
#     )

#     def auto_cluster_and_evaluate(self, k_range=(2, 15)):
#         best_k = find_best_num_clusters(self.embeddings, k_range)
#         print(f"自动选择的最佳聚类数: {best_k}")
#         clusters = self.cluster_nodes(num_clusters=best_k)

#         metrics = self.evaluate_clustering(clusters)
#         print("聚类评估指标:")
#         for k, v in metrics.items():
#             print(f"{k}: {v}")

#         self.show_cluster_colored_graph(clusters, dir_path="results/clustered")
#         return clusters


# from sklearn.metrics import silhouette_score
# import matplotlib.pyplot as plt

# def find_best_num_clusters(embeddings, k_range=(2, 15)):
#     scores = []
#     K_range = list(range(k_range[0], k_range[1]+1))

#     for k in K_range:
#         kmeans = KMeans(n_clusters=k, random_state=42, n_init='auto')
#         labels = kmeans.fit_predict(embeddings)
#         score = silhouette_score(embeddings, labels)
#         scores.append(score)

#     # 取最大轮廓分数对应的 k
#     best_k = K_range[scores.index(max(scores))]

#     # 可视化轮廓分数
#     plt.figure(figsize=(6, 4))
#     plt.plot(K_range, scores, marker='o')
#     plt.xlabel('Number of Clusters (k)')
#     plt.ylabel('Silhouette Score')
#     plt.title('Best k by Silhouette Score')
#     plt.grid(True)
#     plt.tight_layout()
#     plt.savefig("results/cluster_silhouette_score.png")
#     print("轮廓图已保存: results/cluster_silhouette_score.png")

#     return best_k
