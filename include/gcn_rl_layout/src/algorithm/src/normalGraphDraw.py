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
from collections import defaultdict
import matplotlib.cm as cm
import matplotlib.colors as mcolors
import matplotlib.pyplot as plt
from lib import iFCN_Lab
import torch
import os

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
        if self._uses_right_down_astar:
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
            "route_insert_max_ops": 6,
            "phase_insert_max_ops": 4,
        }
        self.clock_template_ok = False
        self.clock_template_conflict_count = 0
        self.phase_assignment_ok = False
        self.phase_conflict_count = 0
        self.last_failed_pairs = {}
        self._stage_snapshot_counter = 0

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

        for layer_idx in sorted(sorted_layers.keys()):
            y = layer_idx * layer_gap
            for node in sorted_layers[layer_idx]:
                x = int(x_pos[node])
                coords.append([x, y])
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
            left_port_fanin = None
            if len(fanins_sorted) >= 2:
                left_side = [fid for fid in fanins_sorted if self._node_coord[fid][0] < node_x]
                if left_side:
                    left_port_fanin = max(left_side, key=lambda fid: self._node_coord[fid][0])
                else:
                    left_port_fanin = max(fanins_sorted, key=lambda fid: self._node_coord[fid][0])

            for fanin_id in fanins_sorted:
                if left_port_fanin is not None and fanin_id == left_port_fanin:
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

    def _line_span(self, axis: str):
        self._ensure_coord_cache()
        if not self._node_coord:
            return None
        if axis == "x":
            values = [coord[0] for coord in self._node_coord.values()]
        elif axis == "y":
            values = [coord[1] for coord in self._node_coord.values()]
        else:
            raise ValueError(f"Unknown axis: {axis}")
        return int(min(values)), int(max(values))

    def _has_node_overlap_after_cut_compaction(self, axis: str, cut: int) -> bool:
        self._ensure_coord_cache()
        shifted = set()
        for x, y in self._node_coord.values():
            nx, ny = int(x), int(y)
            if axis == "x" and nx > cut:
                nx -= 1
            elif axis == "y" and ny > cut:
                ny -= 1
            coord = (nx, ny)
            if coord in shifted:
                return True
            shifted.add(coord)
        return False

    def _rank_cut_candidates(self, axis: str):
        span = self._line_span(axis)
        if span is None:
            return []
        lo, hi = span
        if lo >= hi:
            return []

        used_coords = self._collect_used_coords()
        candidates = []
        for cut in range(int(lo), int(hi)):
            if self._has_node_overlap_after_cut_compaction(axis, cut):
                continue

            if axis == "x":
                pressure = sum(1 for x, _ in used_coords if x == cut or x == cut + 1)
            else:
                pressure = sum(1 for _, y in used_coords if y == cut or y == cut + 1)
            candidates.append((pressure, abs(cut - (lo + hi) / 2.0), int(cut)))

        candidates.sort()
        return [cut for _, _, cut in candidates]

    def _reroute_and_validate_current_coords(self, verbose=False):
        self._refresh_fanin_directions_for_current_coords()
        self.place_all_nodes_on_chessboard()
        failed_pairs = self.sequence_route_all_edges(verbose=verbose)
        if failed_pairs:
            self.clock_template_ok = False
            self.clock_template_conflict_count = 0
            self._sync_legacy_phase_status()
            return False

        template_ok, _ = self.verify_clock_template_consistency(verbose=verbose)
        return bool(template_ok)

    def _try_compact_cut(self, axis: str, cut: int, verbose=False):
        if self._has_node_overlap_after_cut_compaction(axis, cut):
            return False, None

        prev_coords = self.coords.clone()
        prev_fanin_directions = dict(self.fanin_directions)

        if axis == "x":
            self.move_cols_left_after(int(cut), 1)
        elif axis == "y":
            self.move_rows_up_after(int(cut), 1)
        else:
            raise ValueError(f"Unknown axis: {axis}")

        if self._reroute_and_validate_current_coords(verbose=verbose):
            return True, self.mapChessboard.computeLayoutArea()

        self.coords = prev_coords
        self._coord_cache_dirty = True
        self.fanin_directions = prev_fanin_directions
        return False, None

    def compact_layout(self, max_iters=8, verbose=False):
        max_iters = max(0, int(max_iters))
        if max_iters <= 0:
            return 0

        reductions = 0
        for _ in range(max_iters):
            width, height = self.mapChessboard.computeLayoutArea()
            if width <= 0 or height <= 0:
                break

            # Shrinking height saves width area cells first; shrinking width saves height cells.
            axis_order = ["y", "x"] if width >= height else ["x", "y"]
            improved = False

            for axis in axis_order:
                for cut in self._rank_cut_candidates(axis):
                    success, new_area = self._try_compact_cut(axis, cut, verbose=False)
                    if not success:
                        continue
                    reductions += 1
                    improved = True
                    if verbose:
                        axis_name = "row" if axis == "y" else "col"
                        print(f"[layout compact] remove {axis_name} gap after {cut}, new area={new_area}")
                    break
                if improved:
                    break

            if not improved:
                break

        self._reroute_and_validate_current_coords(verbose=False)
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
                    if self.has_node_at_coord((x , y + 1)):
                        self.move_row_down(y+1 , 1)
                    if self.has_node_at_coord((x + 1, y)):
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

    def _route_edge(self, src, dst, direction):
        src = int(src)
        dst = int(dst)
        direction = (int(direction[0]), int(direction[1]))
        if self._uses_right_down_astar:
            return self.astar.route(src, dst, direction)
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

    def _route_edge_with_direction_options(self, src, dst, preferred_direction):
        preferred_direction = (int(preferred_direction[0]), int(preferred_direction[1]))
        directions = [preferred_direction]
        alternate = self._alternate_direction(preferred_direction)
        if alternate not in directions:
            directions.append(alternate)

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

    def _route_endpoint_coords(self, src, dst, direction):
        src_x, src_y = self.get_node_coord(src)
        dst_x, dst_y = self.get_node_coord(dst)
        dx, dy = int(direction[0]), int(direction[1])
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
        horizontal_span = abs(dst_x - src_x)
        vertical_span = max(0, dst_y - src_y)
        return (-fanout_count, -horizontal_span, -vertical_span, src, dst)

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

        if verbose:
            print(
                f"布线完成，合计布线数量: {self.parse.effective_edges_num}，"
                f"失败数量: {len(failed_pairs)}"
            )
        return failed_pairs

    def reroute_with_priority_pairs(self, priority_pairs, verbose=False):
        """
        清空并重布线：优先布线 priority_pairs，并且优先边尝试两种扇入方向。
        """
        self.sort_route_sequence()
        priority = {(int(s), int(d)) for s, d in priority_pairs}
        all_pairs = []

        for layer in sorted(self.parse.same_layer_route_pairs.keys()):
            all_pairs.extend((int(src), int(dst)) for src, dst in self.parse.same_layer_route_pairs[layer])
        all_pairs.extend((int(src), int(dst)) for src, dst in self.parse.differ_layer_route_pairs)

        all_pairs = sorted(
            all_pairs,
            key=lambda p: (0 if p in priority else 1),
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

        if verbose:
            print(
                f"布线完成，合计布线数量: {self.parse.effective_edges_num}，"
                f"失败数量: {len(failed_pairs)}"
            )
        return failed_pairs

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
        row_ops = []
        col_ops = []
        for (src, dst), direction in failed_pairs.items():
            src_x, src_y = self.get_node_coord(src)
            dst_x, dst_y = self.get_node_coord(dst)
            launch_blocked = self._is_launch_blocked(src, dst, direction)
            sink_blocked = self._is_sink_blocked(src, dst, direction)

            if launch_blocked:
                # Source-side congestion is usually more harmful than destination spacing:
                # shift the source column/right half to carve a dedicated launch corridor.
                col_ops.append(src_x)
                if src_y + 1 < dst_y:
                    row_ops.append(src_y + 1)

            if sink_blocked:
                if direction == (0, -1):
                    row_ops.append(dst_y)
                elif direction == (-1, 0):
                    col_ops.append(dst_x)

            if direction == (0, -1):
                row_ops.append(dst_y)
            elif direction == (-1, 0):
                col_ops.append(dst_x)
            else:
                row_ops.append(dst_y)
            if abs(dst_x - src_x) > 1:
                col_ops.append(min(src_x, dst_x) + 1)
            if dst_y - src_y > 1:
                row_ops.append(src_y + 1)
        row_ops = sorted(set(int(r) for r in row_ops))[:max_ops_per_iter]
        col_ops = sorted(set(int(c) for c in col_ops))[:max_ops_per_iter]
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
        使用 GCN embedding 估计失败边“修复收益”，优先移动高收益目的节点。
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

    def _route_with_repair(self, verbose=False, repair_iters=2, use_ml_reposition=True):
        failed_pairs = self.sequence_route_all_edges(verbose=verbose)
        for _ in range(max(0, int(repair_iters))):
            if not failed_pairs:
                break
            priority_retry = self.reroute_with_priority_pairs(set(failed_pairs.keys()), verbose=False)
            if len(priority_retry) < len(failed_pairs):
                failed_pairs = priority_retry
                if not failed_pairs:
                    break

            prev_coords = self.coords.clone()
            prev_failed_pairs = failed_pairs

            moved_nodes = 0
            if use_ml_reposition:
                moved_nodes = self.ml_guided_node_reposition(failed_pairs)

            row_ops = []
            col_ops = []
            if moved_nodes == 0:
                row_ops, col_ops = self.failed_pairs_to_insert_ops(failed_pairs)
                if not row_ops and not col_ops:
                    break
                self.move_rows_and_cols(row_ops, col_ops)

            self.place_all_nodes_on_chessboard()
            new_failed_pairs = self.sequence_route_all_edges(verbose=verbose)

            if len(new_failed_pairs) < len(prev_failed_pairs):
                failed_pairs = new_failed_pairs
                continue

            # 没有改善则回滚本轮操作
            self.coords = prev_coords
            self._coord_cache_dirty = True
            self.place_all_nodes_on_chessboard()
            failed_pairs = self.sequence_route_all_edges(verbose=False)
            break
        return failed_pairs

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
        compact_iters=8,
        ml_tuning=None,
        snapshot_dir=None,
    ):
        self.clock_template_ok = False
        self.clock_template_conflict_count = 0
        self._sync_legacy_phase_status()
        self._stage_snapshot_counter = 0
        if ml_tuning:
            self.set_ml_tuning(**ml_tuning)

        self.caculate_rough_placement()
        self.caculate_ports_reservation()
        failed_pairs = {}
        best_failed_pairs = None
        best_coords = self.coords.clone()

        # 阶段1：先把布线失败数降到最小（优先全布通）
        for _ in range(max(1, int(global_place_iters))):
            self.place_all_nodes_on_chessboard()
            failed_pairs = self._route_with_repair(
                verbose=verbose,
                repair_iters=route_repair_iters,
                use_ml_reposition=True,
            )
            self._snapshot_stage_heatmap(
                snapshot_dir,
                f"stage1_route_iter_failed_{len(failed_pairs)}",
                failed_pairs=failed_pairs,
            )

            if best_failed_pairs is None or len(failed_pairs) < len(best_failed_pairs):
                best_failed_pairs = dict(failed_pairs)
                best_coords = self.coords.clone()
                if verbose:
                    print(f"[布局搜索] 当前最优失败边数量: {len(best_failed_pairs)}")

            if not failed_pairs:
                break

            moved_nodes = self.ml_guided_node_reposition(
                failed_pairs,
                max_nodes=int(self.ml_tuning["global_ml_max_nodes"]),
                max_shift=int(self.ml_tuning["global_ml_max_shift"]),
            )
            if moved_nodes > 0:
                if verbose:
                    print(f"布线失败修复：ML移动节点数量 {moved_nodes}")
                continue

            row_ops, col_ops = self.failed_pairs_to_insert_ops(
                failed_pairs,
                max_ops_per_iter=int(self.ml_tuning["route_insert_max_ops"]),
            )
            if not row_ops and not col_ops:
                break
            if verbose:
                print(f"布线失败修复：插入行{row_ops}, 列{col_ops}")
            self.move_rows_and_cols(row_ops, col_ops)

        # 回滚到历史最佳布局并重新路由，防止坏动作累积
        if best_failed_pairs is not None:
            self.coords = best_coords.clone()
            self._coord_cache_dirty = True
            self.place_all_nodes_on_chessboard()
            failed_pairs = self._route_with_repair(
                verbose=False,
                repair_iters=max(1, int(route_repair_iters // 2)),
                use_ml_reposition=False,
            )
            self._snapshot_stage_heatmap(
                snapshot_dir,
                f"stage1_converged_failed_{len(failed_pairs)}",
                failed_pairs=failed_pairs,
            )
            if verbose:
                print(f"[布局收敛] 最终失败边数量: {len(failed_pairs)}")

        # 阶段2：未全布通则不允许做 2DDWave 模板一致性校验
        if failed_pairs:
            self._snapshot_stage_heatmap(
                snapshot_dir,
                "stage2_skip_template_check_due_to_route_fail",
                failed_pairs=failed_pairs,
            )
            if verbose:
                print(f"仍有未完成布线，跳过 2DDWave 模板一致性校验，失败边数量: {len(failed_pairs)}")
            self._record_failed_pairs(failed_pairs)
            return failed_pairs

        # 阶段3：在全布通前提下做 2DDWave 模板一致性校验/修复
        for _ in range(max(0, int(phase_repair_iters)) + 1):
            template_ok, conflicts = self.verify_clock_template_consistency(verbose=verbose)
            if template_ok:
                compact_reductions = self.compact_layout(
                    max_iters=compact_iters,
                    verbose=verbose,
                )
                if compact_reductions > 0:
                    self._snapshot_stage_heatmap(
                        snapshot_dir,
                        f"stage4_compacted_{compact_reductions}",
                        failed_pairs=failed_pairs,
                    )
                self._snapshot_stage_heatmap(
                    snapshot_dir,
                    "stage3_template_check_success",
                    failed_pairs=failed_pairs,
                )
                self._record_failed_pairs(failed_pairs)
                return failed_pairs

            conflict_pairs = {tuple(map(int, node_pair)) for _, _, node_pair, _, _ in conflicts}
            if conflict_pairs:
                new_failed = self.reroute_with_priority_pairs(conflict_pairs, verbose=False)
                if not new_failed:
                    template_ok, conflicts = self.verify_clock_template_consistency(verbose=verbose)
                    if template_ok:
                        compact_reductions = self.compact_layout(
                            max_iters=compact_iters,
                            verbose=verbose,
                        )
                        if compact_reductions > 0:
                            self._snapshot_stage_heatmap(
                                snapshot_dir,
                                f"stage4_compacted_{compact_reductions}",
                                failed_pairs=failed_pairs,
                            )
                        self._snapshot_stage_heatmap(
                            snapshot_dir,
                            "stage3_template_check_success_after_priority_reroute",
                            failed_pairs=failed_pairs,
                        )
                        self._record_failed_pairs(failed_pairs)
                        return failed_pairs
                else:
                    failed_pairs = new_failed
                    self._snapshot_stage_heatmap(
                        snapshot_dir,
                        f"stage3_route_degrade_failed_{len(failed_pairs)}",
                        failed_pairs=failed_pairs,
                    )
                    if verbose:
                        print(f"模板冲突边优先重布线后出现失败边: {len(failed_pairs)}")
                    self._record_failed_pairs(failed_pairs)
                    return failed_pairs

            row_ops, col_ops = self.clock_template_conflicts_to_insert_ops(
                conflicts,
                max_ops_per_iter=int(self.ml_tuning["phase_insert_max_ops"]),
            )
            if not row_ops and not col_ops:
                break
            if verbose:
                print(f"2DDWave 模板冲突修复：插入行{row_ops}, 列{col_ops}")
            self.move_rows_and_cols(row_ops, col_ops)
            self.place_all_nodes_on_chessboard()
            failed_pairs = self._route_with_repair(
                verbose=False,
                repair_iters=max(1, int(route_repair_iters // 2)),
                use_ml_reposition=False,
            )
            self._snapshot_stage_heatmap(
                snapshot_dir,
                f"stage3_template_repair_route_failed_{len(failed_pairs)}",
                failed_pairs=failed_pairs,
            )
            if failed_pairs:
                if verbose:
                    print(f"模板一致性修复触发布线退化，失败边数量: {len(failed_pairs)}")
                self._record_failed_pairs(failed_pairs)
                return failed_pairs

        self.clock_template_ok = False
        self.clock_template_conflict_count = (
            len(conflicts) if "conflicts" in locals() else self.clock_template_conflict_count
        )
        self._sync_legacy_phase_status()
        self._snapshot_stage_heatmap(
            snapshot_dir,
            f"stage3_template_check_failed_conflicts_{self.clock_template_conflict_count}",
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
