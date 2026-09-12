"""Deterministic Graphviz/sifting order, structural features, and layout drawings."""

from collections import defaultdict
import os

import matplotlib.cm as cm
import matplotlib.colors as mcolors
import matplotlib.patches as mpatches
import matplotlib.pyplot as plt
from matplotlib.lines import Line2D
import networkx as nx
import numpy as np

from lib import iFCN_Lab
from src.graphviz_sifting import deterministic_layout_embeddings, graphviz_sifting_order


def strict_right_down_layout_max_fanin_right(layer_nodes, edges, embeddings=None):
    """
    改进版布局规则：
    - 所有扇出都以其父节点中 x 最大的那个为 base
    - 若父节点只有一个扇出：
        · 若正下方至扇出层之间没有节点阻挡 -> 正下方 (x=base_x)
        · 若有阻挡 -> 向右偏移一格 (x=base_x+1)
    - 若父节点有多个扇出：第1个在 base_x，后续右移
    - 若有多个父节点：同样以 x 最大的父节点为 base
    """
    x_pos = {}                    # node -> x 坐标
    node_layer = {}               # node -> 层号
    occupied = defaultdict(set)   # layer -> 已占用x坐标
    fanins = defaultdict(list)
    fanouts = defaultdict(list)

    # 构建扇入、扇出映射
    for u, v in edges:
        fanins[v].append(u)
        fanouts[u].append(v)

    # 建立 node -> layer 索引
    for layer_idx, nodes in enumerate(layer_nodes):
        for node in nodes:
            node_layer[node] = layer_idx

    # 每个父节点扇出计数
    fanout_offset = defaultdict(int)

    # 辅助函数：判断正下方路径是否有节点阻挡
    def has_blocker(base_x, parent_layer, child_layer):
        for ly in range(parent_layer + 1, child_layer):
            if base_x in occupied[ly]:
                return True
        return False

    for layer_idx, nodes in enumerate(layer_nodes):
        for node in nodes:
            fanin_nodes = fanins[node]
            fanin_xs = [x_pos[u] for u in fanin_nodes if u in x_pos]

            if fanin_xs:
                # 以所有父节点中 x 最大的为 base
                base_parent = fanin_nodes[fanin_xs.index(max(fanin_xs))]
                base_x = max(fanin_xs)

                num_fanouts = len(fanouts[base_parent])
                offset = fanout_offset[base_parent]

                if num_fanouts == 1:
                    # 单扇出情况：检查正下方是否被阻挡
                    parent_layer = node_layer[base_parent]
                    child_layer = node_layer[node]

                    if has_blocker(base_x, parent_layer, child_layer):
                        x_try = base_x + 1  # 被阻挡 → 右移
                    else:
                        x_try = base_x      # 无阻挡 → 正下方
                else:
                    # 多扇出情况：第1个在 base_x，后续右移
                    x_try = base_x + offset
                    fanout_offset[base_parent] += 1

            else:
                # 无扇入（输入层）
                x_try = 0

            # 避免层内重叠
            while x_try in occupied[layer_idx]:
                x_try += 1

            x_pos[node] = x_try
            occupied[layer_idx].add(x_try)

    # 层内排序
    sorted_per_layer = {
        i: sorted(layer_nodes[i], key=lambda n: x_pos[n])
        for i in range(len(layer_nodes))
    }

    return sorted_per_layer, x_pos


def count_crossings_fast(layer1_order, layer2_order, edges):
    idx1 = {n: i for i, n in enumerate(layer1_order)}
    idx2 = {n: i for i, n in enumerate(layer2_order)}
    edge_pos = [(idx1[u], idx2[v]) for (u, v) in edges if u in idx1 and v in idx2]
    edge_pos.sort()
    targets = [t for s, t in edge_pos]
    def merge_count(arr):
        if len(arr) <= 1: return arr, 0
        mid = len(arr) // 2
        left, inv_l = merge_count(arr[:mid])
        right, inv_r = merge_count(arr[mid:])
        merged, inv = [], inv_l + inv_r
        i = j = 0
        while i < len(left) and j < len(right):
            if left[i] <= right[j]:
                merged.append(left[i]); i += 1
            else:
                merged.append(right[j]); j += 1
                inv += len(left) - i
        merged += left[i:]
        merged += right[j:]
        return merged, inv
    _, total = merge_count(targets)
    return total


def visualize_layered_graph_sorted(
    parse,
    sorted_nodes_per_layer,
    edges,
    circuitName,
    savedPath,
    node2cluster=None,       # ✅ 可选：聚类信息 {node_id: cluster_id}
    num_clusters=None,       # ✅ 可选：聚类总数（用于 colormap 映射）
    node_positions=None,     # ✅ 可选：显式节点坐标 {node_id: (x, y)}
    file_suffix="",          # ✅ 可选：导出文件后缀
    title=None,              # ✅ 可选：图标题
    verbose=True,            # ✅ 可选：是否打印保存路径
):
    """
    可视化分层有向图：小图(≤300节点)画有向图；大图(>300节点)用散点提速。
    节点按类型上色；若传入 node2cluster，则边缘颜色表示聚类。
    """
    type2color = {
        iFCN_Lab.NodeType.Input:      '#6ecff6',
        iFCN_Lab.NodeType.Output:     '#f6bc6e',
        iFCN_Lab.NodeType.Maj:        '#f1c40f',
        iFCN_Lab.NodeType.And:        '#f66e6e',
        iFCN_Lab.NodeType.Or:         '#7d6ef6',
        iFCN_Lab.NodeType.Not:        '#b9f66e',
        iFCN_Lab.NodeType.Redundancy: '#34495e',
        iFCN_Lab.NodeType.Fanout:     '#2ecc71',
    }
    type2label = {
        iFCN_Lab.NodeType.Input:      'INPUT',
        iFCN_Lab.NodeType.Output:     'OUTPUT',
        iFCN_Lab.NodeType.Maj:        'MAJORITY',
        iFCN_Lab.NodeType.And:        'AND (&)',
        iFCN_Lab.NodeType.Or:         'OR (|)',
        iFCN_Lab.NodeType.Not:        'NOT (¬)',
        iFCN_Lab.NodeType.Redundancy: 'WIRE',
        iFCN_Lab.NodeType.Fanout:     'FANOUT',
    }

    total_nodes = sum(len(nodes) for nodes in sorted_nodes_per_layer.values())
    layer_count = max(1, len(sorted_nodes_per_layer))
    max_layer_nodes = max((len(nodes) for nodes in sorted_nodes_per_layer.values()), default=1)
    y_gap = 0.95
    x_gap = 0.88

    os.makedirs(savedPath, exist_ok=True)
    base_name = os.path.splitext(os.path.basename(circuitName))[0]
    save_path = os.path.join(savedPath, base_name + str(file_suffix) + ".svg")

    used_types = set()
    handles = None  # legend 占位
    compact_pos = None
    compact_cols = None
    compact_rows = None

    if node_positions:
        xs = sorted({int(coord[0]) for coord in node_positions.values()})
        ys = sorted({int(coord[1]) for coord in node_positions.values()})
        x_rank = {x: idx for idx, x in enumerate(xs)}
        y_rank = {y: idx for idx, y in enumerate(ys)}
        compact_pos = {
            int(node): (0.86 * x_rank[int(coord[0])], -0.92 * y_rank[int(coord[1])])
            for node, coord in node_positions.items()
        }
        compact_cols = max(1, len(xs))
        compact_rows = max(1, len(ys))

    node_to_layer = {
        int(node): int(layer)
        for layer, nodes_sorted in sorted_nodes_per_layer.items()
        for node in nodes_sorted
    }

    if total_nodes <= 300:
        G = nx.DiGraph()
        pos = {}
        labels = {}
        nodelist = []
        node_colors = []
        node_edgecolors = []

        if node2cluster and num_clusters:
            cmap = cm.get_cmap("tab20", num_clusters)
            norm = mcolors.Normalize(vmin=0, vmax=max(1, num_clusters - 1))
        else:
            cmap = None

        for layer, nodes_sorted in sorted_nodes_per_layer.items():
            n = len(nodes_sorted)
            start_x = - (n - 1) * x_gap / 2
            for i, node in enumerate(nodes_sorted):
                if compact_pos is not None and int(node) in compact_pos:
                    x, y = compact_pos[int(node)]
                else:
                    x = start_x + i * x_gap
                    y = -layer * y_gap
                node_type = parse.getNodeTypeEnum(node)

                G.add_node(node)
                pos[node] = (x, y)
                nodelist.append(node)
                node_colors.append(type2color.get(node_type, '#d3d3d3'))
                labels[node] = type2label.get(node_type, 'N')
                used_types.add(node_type)

                # 边缘颜色来自聚类信息
                if node2cluster and node in node2cluster:
                    cluster_id = node2cluster[node]
                    edge_color = cmap(norm(cluster_id))
                else:
                    edge_color = '#000000'  # 默认为黑边
                node_edgecolors.append(edge_color)

        adjacent_edges = []
        long_span_edges = []
        for u, v in edges:
            u = int(u)
            v = int(v)
            if u not in pos or v not in pos:
                continue
            G.add_edge(u, v)
            layer_span = abs(node_to_layer.get(v, 0) - node_to_layer.get(u, 0))
            if layer_span > 1:
                long_span_edges.append((u, v))
            else:
                adjacent_edges.append((u, v))

        if compact_pos is not None:
            fig_w = min(10, max(3.6, 0.95 * compact_cols + 1.8))
            fig_h = min(10, max(2.8, 0.95 * compact_rows + 1.2))
            node_size = 980 if total_nodes <= 20 else 620
            font_size = 12 if total_nodes <= 20 else 8
        else:
            fig_w = min(11, max(3.6, 0.9 * max_layer_nodes + 1.8))
            fig_h = min(10, max(2.8, 0.9 * layer_count + 1.0))
            node_size = 980 if total_nodes <= 20 else 620
            font_size = 12 if total_nodes <= 20 else 8
        fig, ax = plt.subplots(figsize=(fig_w, fig_h))

        for layer, nodes_sorted in sorted_nodes_per_layer.items():
            layer_nodes = [int(node) for node in nodes_sorted if int(node) in pos]
            if not layer_nodes:
                continue
            y = float(np.mean([pos[node][1] for node in layer_nodes]))
            ax.axhline(y, color="#e7ecf2", linewidth=0.8, zorder=0)

        if adjacent_edges:
            nx.draw_networkx_edges(
                G,
                pos,
                ax=ax,
                edgelist=adjacent_edges,
                arrows=True,
                arrowstyle="-|>",
                arrowsize=10,
                width=0.8,
                edge_color="#8996a8",
                alpha=0.68,
                connectionstyle="arc3,rad=0.0",
            )
        if long_span_edges:
            nx.draw_networkx_edges(
                G,
                pos,
                ax=ax,
                edgelist=long_span_edges,
                arrows=True,
                arrowstyle="-|>",
                arrowsize=10,
                width=0.85,
                edge_color="#b6bfcc",
                style="dashed",
                alpha=0.55,
                connectionstyle="arc3,rad=0.0",
            )
        nx.draw_networkx_nodes(
            G, pos,
            ax=ax,
            nodelist=nodelist,
            node_color=node_colors,
            edgecolors=node_edgecolors,  # ✅ 新增边缘染色
            linewidths=1.0,
            node_size=node_size,
        )
        nx.draw_networkx_labels(
            G,
            pos,
            ax=ax,
            labels={node: str(node) for node in nodelist},
            font_size=font_size,
            font_weight="bold",
        )

        handles = [
            mpatches.Patch(color=type2color[t], label=type2label[t])
            for t in used_types
        ]
        if adjacent_edges:
            handles.append(
                Line2D([0], [0], color="#8996a8", linewidth=1.0, linestyle="-", label="Adj. edge")
            )
        if long_span_edges:
            handles.append(
                Line2D([0], [0], color="#b6bfcc", linewidth=1.0, linestyle="--", label="Long edge")
            )

    else:
        xs = []
        ys = []
        node_colors = []
        node_edgecolors = []

        if node2cluster and num_clusters:
            cmap = cm.get_cmap("tab20", num_clusters)
            norm = mcolors.Normalize(vmin=0, vmax=max(1, num_clusters - 1))
        else:
            cmap = None

        for layer, nodes_sorted in sorted_nodes_per_layer.items():
            n = len(nodes_sorted)
            start_x = - (n - 1) * x_gap / 2
            for i, node in enumerate(nodes_sorted):
                if compact_pos is not None and int(node) in compact_pos:
                    x, y = compact_pos[int(node)]
                else:
                    x = start_x + i * x_gap
                    y = -layer * y_gap
                node_type = parse.getNodeTypeEnum(node)

                xs.append(x)
                ys.append(y)
                node_colors.append(type2color.get(node_type, '#d3d3d3'))
                used_types.add(node_type)

                # 聚类边缘颜色
                if node2cluster and node in node2cluster:
                    cluster_id = node2cluster[node]
                    edge_color = cmap(norm(cluster_id))
                else:
                    edge_color = '#000000'
                node_edgecolors.append(edge_color)

        if compact_pos is not None:
            fig_w = min(12, max(4.0, 0.85 * compact_cols + 1.6))
            fig_h = min(10, max(3.0, 0.85 * compact_rows + 1.2))
        else:
            fig_w = min(12, max(4.0, 0.22 * max_layer_nodes + 2.4))
            fig_h = min(10, max(3.0, 0.75 * layer_count + 1.0))
        plt.figure(figsize=(fig_w, fig_h))

        plt.scatter(
            xs, ys,
            c=node_colors,
            edgecolors=node_edgecolors,
            s=50, alpha=0.9, linewidths=1.5
        )

        handles = [
            mpatches.Patch(color=type2color[t], label=type2label[t])
            for t in used_types
        ]


    if handles:
        plt.legend(handles=handles, loc='lower left', markerscale=10, fontsize=10)
    plt.axis('off')
    if title:
        plt.gcf().suptitle(title, y=0.995, fontsize=11)
    top_margin = 0.94 if title else 0.98
    plt.subplots_adjust(left=0.02, right=0.98, top=top_margin, bottom=0.02)
    plt.savefig(save_path, bbox_inches='tight', pad_inches=0.02)
    plt.close()
    if verbose:
        print(f"图已保存: {save_path}")


def generate_layer_order(layer_nodes, edges, node_to_index):
    print("-------------------Graphviz + exact-gain sifting-------------------")
    ordered_layers, diagnostics = graphviz_sifting_order(
        layer_nodes,
        edges,
        count_crossings_fast,
    )
    ordered_list = [ordered_layers[index] for index in range(len(ordered_layers))]
    embeddings = deterministic_layout_embeddings(ordered_list, edges, node_to_index)
    crossings_per_layer = {}
    total_crossings = 0
    for layer_index in range(len(ordered_list) - 1):
        value = count_crossings_fast(
            ordered_list[layer_index],
            ordered_list[layer_index + 1],
            edges,
        )
        crossings_per_layer[layer_index] = value
        total_crossings += value
    print(
        "[Graphviz+sifting] crossings={} (removed {} after dot), "
        "dot={:.3f}s, sift={:.3f}s, evaluations={}, fallback={}".format(
            total_crossings,
            diagnostics["crossings_removed"],
            diagnostics["graphviz_seconds"],
            diagnostics["seconds"],
            diagnostics["evaluations"],
            diagnostics["graphviz_fallback_to_raw"],
        )
    )
    return embeddings, ordered_layers, crossings_per_layer, edges
