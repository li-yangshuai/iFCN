import torch
import torch.nn.functional as F
from torch_geometric.nn import TransformerConv
from torch_geometric.utils import negative_sampling, to_undirected
from sklearn.decomposition import PCA
import numpy as np
import matplotlib.pyplot as plt
import networkx as nx
import copy
import os
import os.path as osp
from lib import iFCN_Lab
import matplotlib.patches as mpatches
from matplotlib.lines import Line2D
from collections import defaultdict
import matplotlib.cm as cm
import matplotlib.colors as mcolors
from src.graphviz_sifting import (
    deterministic_layout_embeddings,
    graphviz_sifting_order,
)

def safe_torch_device(preferred="auto"):
    if preferred == "cpu":
        return torch.device("cpu")
    if preferred in ("auto", "cuda") and torch.cuda.is_available():
        try:
            probe = torch.empty(1, device="cuda")
            probe = probe + 1
            torch.cuda.synchronize()
            return torch.device("cuda")
        except Exception as exc:
            print(f"[GCN] CUDA unavailable for this build/device, falling back to CPU: {exc}")
            return torch.device("cpu")
    if preferred == "cuda":
        print("[GCN] CUDA requested but torch.cuda.is_available() is false, falling back to CPU.")
    return torch.device("cpu")

def _ema_smooth(values, alpha=0.18):
    if not values:
        return []
    smoothed = [float(values[0])]
    for value in values[1:]:
        smoothed.append(alpha * float(value) + (1.0 - alpha) * smoothed[-1])
    return smoothed

def _resolve_training_curve_base(v_file_path):
    repo_root = osp.abspath(osp.join(osp.dirname(__file__), "../../.."))
    plot_root = osp.join(repo_root, "results", "gcn_loss_plots")

    if not v_file_path:
        return osp.join(plot_root, "unnamed", "gcn_training")

    normalized = osp.normpath(str(v_file_path))
    parts = normalized.split(os.sep)
    if "benchmarks" in parts:
        rel_path = osp.join(*parts[parts.index("benchmarks") + 1 :])
    else:
        rel_path = osp.basename(normalized)
    return osp.join(plot_root, rel_path)

def _save_training_curve(loss_list, v_file_path=None):
    if not loss_list:
        return

    curve_base = _resolve_training_curve_base(v_file_path)
    curve_dir = osp.dirname(curve_base)
    os.makedirs(curve_dir, exist_ok=True)

    epochs = np.arange(len(loss_list), dtype=np.int32)
    raw_loss = np.asarray(loss_list, dtype=np.float64)
    ema_loss = np.asarray(_ema_smooth(loss_list), dtype=np.float64)
    best_idx = int(np.argmin(raw_loss))
    best_loss = float(raw_loss[best_idx])
    circuit_name = osp.basename(str(v_file_path)) if v_file_path else "unknown"

    csv_path = f"{curve_base}.csv"
    with open(csv_path, "w", encoding="utf-8") as f:
        f.write("epoch,loss_raw,loss_ema\n")
        for epoch, raw, ema in zip(epochs, raw_loss, ema_loss):
            f.write(f"{int(epoch)},{raw:.8f},{ema:.8f}\n")

    fig, ax = plt.subplots(figsize=(7.2, 4.4), dpi=180)
    ax.plot(
        epochs,
        raw_loss,
        color="#9fb3c8",
        linewidth=1.2,
        alpha=0.9,
        label="Raw loss",
    )
    ax.plot(
        epochs,
        ema_loss,
        color="#1f4e79",
        linewidth=2.2,
        label="EMA loss",
    )
    ax.scatter(
        [best_idx],
        [best_loss],
        color="#c0392b",
        s=26,
        zorder=4,
        label=f"Best loss ({best_loss:.4f})",
    )
    ax.set_xlabel("Epoch")
    ax.set_ylabel("Self-supervised embedding loss")
    ax.set_title(f"GCN Training Curve - {circuit_name}")
    ax.grid(True, linestyle="--", linewidth=0.6, alpha=0.35)
    ax.legend(frameon=False)
    fig.tight_layout()

    svg_path = f"{curve_base}.svg"
    pdf_path = f"{curve_base}.pdf"
    fig.savefig(svg_path, bbox_inches="tight", pad_inches=0.05)
    fig.savefig(pdf_path, bbox_inches="tight", pad_inches=0.05)
    plt.close(fig)
    print(f"[GCN] Training curve saved: {svg_path}")
    print(f"[GCN] Training data saved: {csv_path}")

# ----------- Edge-aware directed graph encoder -----------
class GCN(torch.nn.Module):
    """Residual directed attention encoder kept under the legacy class name.

    ``CircuitParser`` already computes five edge features, but the old two-layer
    ``GCNConv`` discarded them.  TransformerConv keeps source->target direction,
    conditions messages on edge attributes, and remains compatible with graphs
    built by ``build_simple_pyg_data`` where edge attributes are absent.
    """

    supports_edge_attr = True

    def __init__(
        self,
        in_channels,
        hidden_channels=48,
        out_channels=24,
        edge_channels=0,
        dropout=0.10,
    ):
        super(GCN, self).__init__()
        edge_dim = int(edge_channels) if int(edge_channels) > 0 else None
        self.edge_channels = int(edge_channels)
        self.conv1 = TransformerConv(
            in_channels,
            hidden_channels,
            heads=2,
            concat=False,
            beta=True,
            dropout=float(dropout),
            edge_dim=edge_dim,
        )
        self.norm1 = torch.nn.LayerNorm(hidden_channels)
        self.conv2 = TransformerConv(
            hidden_channels,
            out_channels,
            heads=2,
            concat=False,
            beta=True,
            dropout=float(dropout),
            edge_dim=edge_dim,
        )
        self.norm2 = torch.nn.LayerNorm(out_channels)
        self.residual = torch.nn.Linear(in_channels, out_channels, bias=False)
        self.dropout = torch.nn.Dropout(float(dropout))

    def forward(self, x, edge_index, edge_attr=None):
        if self.edge_channels > 0:
            if edge_attr is None:
                raise ValueError("edge_attr is required by this edge-aware GCN")
            edge_attr = edge_attr.to(dtype=x.dtype)
        else:
            edge_attr = None
        hidden = self.conv1(x, edge_index, edge_attr)
        hidden = self.dropout(F.gelu(self.norm1(hidden)))
        output = self.conv2(hidden, edge_index, edge_attr)
        return self.norm2(output + self.residual(x))


def _data_edge_channels(data):
    edge_attr = getattr(data, "edge_attr", None)
    return int(edge_attr.shape[1]) if edge_attr is not None and edge_attr.ndim == 2 else 0


def _forward_graph_encoder(model, data):
    edge_attr = getattr(data, "edge_attr", None)
    if getattr(model, "supports_edge_attr", False):
        return model(data.x, data.edge_index, edge_attr)
    return model(data.x, data.edge_index)

# ----------- GCN 训练 -----------
import matplotlib.pyplot as plt

def train_gcn(
    model,
    data,
    v_file_path=None,
    epochs=200,
    lr=0.01,
    device='cpu',
    save_curve=True,
    return_losses=False,
    log_every=20,
):
    epochs = int(os.environ.get("IFCN_GCN_EPOCHS", epochs))
    optimizer = torch.optim.AdamW(model.parameters(), lr=lr, weight_decay=1e-4)
    model = model.to(device)
    data = data.to(device)
    edge_index = data.edge_index
    num_nodes = data.x.shape[0]

    if num_nodes < 2 or edge_index.numel() == 0:
        # There is no link-prediction signal in an edgeless/singleton graph.
        # Return a valid (untrained) encoder instead of propagating NaNs from an
        # empty mean into the layout order.
        loss_list = [0.0] * max(0, epochs)
        if save_curve:
            _save_training_curve(loss_list, v_file_path=v_file_path)
        if return_losses:
            return model, loss_list
        return model

    # The embedding objective is symmetric in node pairs, so both directions of
    # every logical edge must be excluded from negative sampling.  The previous
    # dense directed mask could label (v, u) as negative when (u, v) was a
    # positive edge, producing contradictory distance targets and O(N^2) memory.
    positive_edge_index = to_undirected(edge_index, num_nodes=num_nodes)

    model.train()
    loss_history = []
    last_loss = torch.tensor(0.0, dtype=torch.float32, device=device)
    for epoch in range(epochs):
        optimizer.zero_grad()
        embeddings = _forward_graph_encoder(model, data)
        source, target = edge_index
        pos_dist = torch.norm(embeddings[source] - embeddings[target], dim=1)

        negative_edge_index = negative_sampling(
            positive_edge_index,
            num_nodes=num_nodes,
            num_neg_samples=max(1, int(source.numel())),
            method="sparse",
        )
        neg_source, neg_target = negative_edge_index

        if neg_source.numel() == 0:
            loss_history.append(last_loss.detach())
            continue
        neg_dist = torch.norm(embeddings[neg_source] - embeddings[neg_target], dim=1)
        margin = 1.0
        pair_count = min(int(pos_dist.numel()), int(neg_dist.numel()))
        ranking_loss = F.relu(
            margin + pos_dist[:pair_count] - neg_dist[:pair_count]
        ).mean()
        loss = (
            ranking_loss
            + 0.05 * pos_dist.mean()
            + 1e-4 * embeddings.pow(2).mean()
        )
        loss.backward()
        torch.nn.utils.clip_grad_norm_(model.parameters(), max_norm=2.0)
        optimizer.step()
        last_loss = loss.detach()
        loss_history.append(last_loss)
        if log_every and epoch % log_every == 0:
            print(f"Epoch {epoch}, Loss: {float(last_loss.detach().cpu()):.4f}")

    if loss_history:
        loss_list = torch.stack(loss_history).detach().cpu().numpy().astype(float).tolist()
    else:
        loss_list = []
    if save_curve:
        _save_training_curve(loss_list, v_file_path=v_file_path)
    if return_losses:
        return model, loss_list
    return model


# ----------- 获取 embedding -----------
def get_embeddings(model, data, device='cpu'):
    model.eval()
    data = data.to(device)
    with torch.no_grad():
        embeddings = _forward_graph_encoder(model, data)
    return embeddings.cpu().numpy()

# ----------- PCA 排序 -----------
def sort_nodes_by_embedding(embeddings, node_indices, node_to_index, method='pca'):
    node_indices = [int(n) for n in node_indices]
    if len(node_indices) <= 1:
        return list(node_indices)

    present_nodes = [n for n in node_indices if n in node_to_index]
    missing_nodes = [n for n in node_indices if n not in node_to_index]
    if len(present_nodes) <= 1:
        return present_nodes + missing_nodes

    if method == 'pca':
        gcn_node_idx = np.asarray([int(node_to_index[n]) for n in present_nodes], dtype=int)
        layer_embeddings = np.asarray(embeddings[gcn_node_idx], dtype=float)

        if layer_embeddings.ndim != 2 or layer_embeddings.shape[0] <= 1:
            sorted_present = list(present_nodes)
        elif not np.isfinite(layer_embeddings).all():
            sorted_present = list(present_nodes)
        elif np.allclose(np.var(layer_embeddings, axis=0).sum(), 0.0):
            # 层内embedding完全一致时，保留原顺序，避免PCA产生NaN。
            sorted_present = list(present_nodes)
        else:
            try:
                pca = PCA(n_components=1)
                scores = pca.fit_transform(layer_embeddings).flatten()
                if not np.isfinite(scores).all():
                    raise ValueError("non-finite PCA scores")
                sorted_idx = np.argsort(scores, kind="stable")
                sorted_present = [present_nodes[i] for i in sorted_idx]
            except Exception:  # noqa: BLE001
                fallback_scores = layer_embeddings[:, 0]
                sorted_idx = np.argsort(fallback_scores, kind="stable")
                sorted_present = [present_nodes[i] for i in sorted_idx]
    else:
        gcn_node_idx = np.asarray([int(node_to_index[n]) for n in present_nodes], dtype=int)
        scores = np.asarray(embeddings[gcn_node_idx, 0], dtype=float)
        sorted_idx = np.argsort(scores, kind="stable")
        sorted_present = [present_nodes[i] for i in sorted_idx]

    return sorted_present + missing_nodes


# ----------- Barycenter排序与多轮优化 -----------
def barycenter_sort(prev_layer, curr_layer, edges):
    idx_prev = {n: i for i, n in enumerate(prev_layer)}
    curr_set = set(curr_layer)
    incoming_idx = defaultdict(list)
    for u, v in edges:
        if u in idx_prev and v in curr_set:
            incoming_idx[v].append(idx_prev[u])

    barycenters = []
    for v in curr_layer:
        neighbor_idx = incoming_idx.get(v)
        if neighbor_idx:
            center = np.mean(neighbor_idx)
        else:
            center = -1
        barycenters.append((center, v))
    barycenters.sort()
    sorted_nodes = [v for c, v in barycenters]
    return sorted_nodes

def barycenter_sort_down(next_layer, curr_layer, edges):
    # 用下游next_layer节点“牵引”当前层curr_layer节点的排序
    idx_next = {n: i for i, n in enumerate(next_layer)}
    curr_set = set(curr_layer)
    outgoing_idx = defaultdict(list)
    for u, v in edges:
        if u in curr_set and v in idx_next:
            outgoing_idx[u].append(idx_next[v])

    barycenters = []
    for u in curr_layer:
        neighbor_idx = outgoing_idx.get(u)
        if neighbor_idx:
            center = np.mean(neighbor_idx)
        else:
            center = -1
        barycenters.append((center, u))
    barycenters.sort()
    sorted_nodes = [u for c, u in barycenters]
    return sorted_nodes

def minimize_crossing_barycenter_fully(layer_nodes, edges, iters=5):
    sorted_layers = [list(nodes) for nodes in layer_nodes]
    for _ in range(iters):
        # 正向
        for i in range(1, len(sorted_layers)):
            sorted_layers[i] = barycenter_sort(sorted_layers[i-1], sorted_layers[i], edges)
        # 反向
        for i in range(len(sorted_layers)-2, -1, -1):   # 注意-1
            sorted_layers[i] = barycenter_sort_down(sorted_layers[i+1], sorted_layers[i], edges)
    return {i: sorted_layers[i] for i in range(len(sorted_layers))}


def _normalize_layer_list(layer_nodes):
    if isinstance(layer_nodes, dict):
        return [list(layer_nodes[i]) for i in sorted(layer_nodes.keys())]
    return [list(nodes) for nodes in layer_nodes]


def _embedding_scores_by_layer(embeddings, layer_nodes, node_to_index, sign=1.0):
    """Return a normalized one-dimensional GCN score for nodes in each layer."""
    scores_by_layer = {}
    scores_by_node = {}
    if embeddings is None or node_to_index is None:
        return scores_by_layer, scores_by_node

    for layer_idx, nodes in enumerate(_normalize_layer_list(layer_nodes)):
        nodes = [int(node) for node in nodes]
        present = [node for node in nodes if node in node_to_index]
        if len(present) <= 1:
            layer_scores = {node: 0.0 for node in nodes}
        else:
            emb_idx = np.asarray([int(node_to_index[node]) for node in present], dtype=int)
            layer_embeddings = np.asarray(embeddings[emb_idx], dtype=float)
            try:
                if (
                    layer_embeddings.ndim != 2
                    or not np.isfinite(layer_embeddings).all()
                    or np.allclose(np.var(layer_embeddings, axis=0).sum(), 0.0)
                ):
                    raise ValueError("degenerate embeddings")
                raw_scores = PCA(n_components=1).fit_transform(layer_embeddings).flatten()
            except Exception:  # noqa: BLE001
                raw_scores = layer_embeddings[:, 0] if layer_embeddings.ndim == 2 else np.zeros(len(present))

            raw_scores = np.asarray(raw_scores, dtype=float) * float(sign)
            if not np.isfinite(raw_scores).all():
                raw_scores = np.zeros(len(present), dtype=float)
            spread = float(np.max(raw_scores) - np.min(raw_scores))
            if spread > 1e-12:
                raw_scores = (raw_scores - float(np.min(raw_scores))) / spread
            else:
                raw_scores = np.zeros(len(present), dtype=float)
            layer_scores = {node: float(score) for node, score in zip(present, raw_scores)}
            for node in nodes:
                layer_scores.setdefault(node, 0.5)

        scores_by_layer[layer_idx] = layer_scores
        scores_by_node.update(layer_scores)
    return scores_by_layer, scores_by_node


def _barycenter_sort_with_embedding(anchor_layer, curr_layer, edges, embedding_scores, reverse=False, bias=0.0):
    idx_anchor = {int(n): i for i, n in enumerate(anchor_layer)}
    curr_nodes = [int(n) for n in curr_layer]
    curr_set = set(curr_nodes)
    neighbor_idx = defaultdict(list)

    for u, v in edges:
        u = int(u)
        v = int(v)
        if not reverse:
            if u in idx_anchor and v in curr_set:
                neighbor_idx[v].append(idx_anchor[u])
        else:
            if u in curr_set and v in idx_anchor:
                neighbor_idx[u].append(idx_anchor[v])

    previous_rank = {node: rank for rank, node in enumerate(curr_nodes)}
    decorated = []
    for node in curr_nodes:
        positions = neighbor_idx.get(node)
        if positions:
            center = float(np.mean(positions))
        else:
            center = float(previous_rank[node])
        emb_score = float(embedding_scores.get(node, 0.5))
        decorated.append((center + float(bias) * emb_score, emb_score, previous_rank[node], node))

    decorated.sort()
    return [node for _, _, _, node in decorated]


def _build_adjacent_boundary_edges(layer_nodes, edges):
    layers = _normalize_layer_list(layer_nodes)
    node_layer = {
        int(node): int(layer_idx)
        for layer_idx, nodes in enumerate(layers)
        for node in nodes
    }
    boundary_edges = defaultdict(list)
    for u, v in edges:
        u = int(u)
        v = int(v)
        lu = node_layer.get(u)
        lv = node_layer.get(v)
        if lu is None or lv is None:
            continue
        if lv == lu + 1:
            boundary_edges[lu].append((u, v))
    return boundary_edges


def _layer_boundary_crossings(layers, boundary_edges, layer_idx):
    total = 0
    if layer_idx > 0:
        total += count_crossings_fast(
            layers[layer_idx - 1],
            layers[layer_idx],
            boundary_edges.get(layer_idx - 1, ()),
        )
    if layer_idx < len(layers) - 1:
        total += count_crossings_fast(
            layers[layer_idx],
            layers[layer_idx + 1],
            boundary_edges.get(layer_idx, ()),
        )
    return int(total)


def total_adjacent_crossings(layer_nodes, edges):
    layers = _normalize_layer_list(layer_nodes)
    boundary_edges = _build_adjacent_boundary_edges(layers, edges)
    total = 0
    for layer_idx in range(len(layers) - 1):
        total += count_crossings_fast(
            layers[layer_idx],
            layers[layer_idx + 1],
            boundary_edges.get(layer_idx, ()),
        )
    return int(total)


def _gcn_guided_local_sifting(
    layer_nodes,
    edges,
    embedding_scores,
    max_passes=1,
    max_nodes_per_layer=64,
    candidate_radius=8,
):
    """
    Improve layer orders by moving nodes within their own layer.

    GCN scores control the node visitation order and tie-breaks. A move is accepted
    only when it reduces the exact adjacent-layer crossing count, so this stage is
    monotone with respect to the crossing objective.
    """
    layers = _normalize_layer_list(layer_nodes)
    if len(layers) <= 1:
        return {i: layers[i] for i in range(len(layers))}

    boundary_edges = _build_adjacent_boundary_edges(layers, edges)
    degree = defaultdict(int)
    for edges_on_boundary in boundary_edges.values():
        for u, v in edges_on_boundary:
            degree[int(u)] += 1
            degree[int(v)] += 1

    for _ in range(max_passes):
        any_improved = False
        layer_priorities = []
        for layer_idx, nodes in enumerate(layers):
            if len(nodes) <= 1:
                continue
            layer_priorities.append(
                (_layer_boundary_crossings(layers, boundary_edges, layer_idx), layer_idx)
            )
        layer_priorities.sort(reverse=True)

        for _, layer_idx in layer_priorities:
            layer = list(layers[layer_idx])
            if len(layer) <= 1:
                continue

            node_sequence = sorted(
                layer,
                key=lambda n: (-degree[int(n)], embedding_scores.get(int(n), 0.5), int(n)),
            )[:max_nodes_per_layer]

            for node in node_sequence:
                layer = list(layers[layer_idx])
                if node not in layer:
                    continue
                old_pos = layer.index(node)
                base_cost = _layer_boundary_crossings(layers, boundary_edges, layer_idx)
                best_pos = old_pos
                best_cost = base_cost
                best_tie = abs(
                    old_pos / max(1, len(layer) - 1) - embedding_scores.get(int(node), 0.5)
                )

                target_pos = int(round(embedding_scores.get(int(node), 0.5) * max(1, len(layer) - 1)))
                candidate_positions = {
                    0,
                    len(layer) - 1,
                    old_pos,
                    target_pos,
                }
                for center in (old_pos, target_pos):
                    for delta in range(-candidate_radius, candidate_radius + 1):
                        pos = center + delta
                        if 0 <= pos < len(layer):
                            candidate_positions.add(pos)
                if len(layer) > 12:
                    for frac in (0.25, 0.5, 0.75):
                        candidate_positions.add(int(round(frac * (len(layer) - 1))))

                for new_pos in sorted(candidate_positions):
                    if new_pos == old_pos:
                        continue
                    candidate = layer[:old_pos] + layer[old_pos + 1 :]
                    candidate.insert(new_pos, node)
                    layers[layer_idx] = candidate
                    cost = _layer_boundary_crossings(layers, boundary_edges, layer_idx)
                    tie = abs(
                        new_pos / max(1, len(candidate) - 1)
                        - embedding_scores.get(int(node), 0.5)
                    )
                    if cost < best_cost or (cost == best_cost and tie < best_tie):
                        best_pos = new_pos
                        best_cost = cost
                        best_tie = tie

                if best_cost < base_cost:
                    candidate = layer[:old_pos] + layer[old_pos + 1 :]
                    candidate.insert(best_pos, node)
                    layers[layer_idx] = candidate
                    any_improved = True
                else:
                    layers[layer_idx] = layer

        if not any_improved:
            break

    return {i: layers[i] for i in range(len(layers))}


def minimize_crossing_gcn_guided(
    layer_nodes,
    edges,
    embeddings,
    node_to_index,
    iters=10,
    refine_passes=1,
):
    """
    GCN-assisted crossing minimization.

    The previous flow used GCN only to create the PCA initialization, which was
    often washed out by barycenter sweeps. This routine keeps barycenter as the
    exact graph-drawing workhorse, but also uses the trained embedding as a
    layer-wise ordering signal for biased barycenter candidates and local sifting.
    The final layout is selected by exact crossing count.
    """
    base_layers = _normalize_layer_list(layer_nodes)
    if len(base_layers) <= 1:
        return {i: base_layers[i] for i in range(len(base_layers))}

    candidates = []
    plain = minimize_crossing_barycenter_fully(base_layers, edges, iters=iters)
    _, default_scores = _embedding_scores_by_layer(
        embeddings,
        base_layers,
        node_to_index,
        sign=1.0,
    )
    candidates.append((plain, default_scores))

    for sign in (1.0, -1.0):
        scores_by_layer, scores_by_node = _embedding_scores_by_layer(
            embeddings,
            base_layers,
            node_to_index,
            sign=sign,
        )
        for bias in (0.02, 0.05, 0.10, 0.20):
            layers = [list(nodes) for nodes in base_layers]
            for _ in range(iters):
                for layer_idx in range(1, len(layers)):
                    layers[layer_idx] = _barycenter_sort_with_embedding(
                        layers[layer_idx - 1],
                        layers[layer_idx],
                        edges,
                        scores_by_layer.get(layer_idx, {}),
                        reverse=False,
                        bias=bias,
                    )
                for layer_idx in range(len(layers) - 2, -1, -1):
                    layers[layer_idx] = _barycenter_sort_with_embedding(
                        layers[layer_idx + 1],
                        layers[layer_idx],
                        edges,
                        scores_by_layer.get(layer_idx, {}),
                        reverse=True,
                        bias=bias,
                    )

            candidates.append(({i: list(layers[i]) for i in range(len(layers))}, scores_by_node))

    best_layers = None
    best_scores = default_scores
    best_crossings = None
    for candidate, candidate_scores in candidates:
        crossing_count = total_adjacent_crossings(candidate, edges)
        if best_crossings is None or crossing_count < best_crossings:
            best_crossings = crossing_count
            best_layers = candidate
            best_scores = candidate_scores

    refined_layers = _gcn_guided_local_sifting(
        best_layers,
        edges,
        best_scores,
        max_passes=refine_passes,
    )
    refined_crossings = total_adjacent_crossings(refined_layers, edges)
    if refined_crossings < best_crossings:
        best_layers = refined_layers

    return {i: list(best_layers[i]) for i in range(len(best_layers))}


def minimize_crossing_local_sifting(
    layer_nodes,
    edges,
    iters=10,
    refine_passes=1,
):
    """Barycenter plus the same local sifting search, but without GCN scores."""
    base_layers = _normalize_layer_list(layer_nodes)
    plain = minimize_crossing_barycenter_fully(base_layers, edges, iters=iters)
    neutral_scores = {
        int(node): 0.5
        for nodes in _normalize_layer_list(plain)
        for node in nodes
    }
    refined = _gcn_guided_local_sifting(
        plain,
        edges,
        neutral_scores,
        max_passes=refine_passes,
    )
    if total_adjacent_crossings(refined, edges) < total_adjacent_crossings(plain, edges):
        return refined
    return plain


def minimize_crossing_sugiyama_fixed_layer(
    layer_nodes,
    edges,
    iters=10,
    refine_passes=1,
):
    """Classical Sugiyama-style fixed-layer crossing minimization baseline.

    The rebuttal comparison keeps the same parser-provided logic layers for both
    algorithms, so this baseline represents the complete non-learning Sugiyama
    ordering step under the same crossing metric as the GCN-guided method.
    """
    return minimize_crossing_local_sifting(
        layer_nodes,
        edges,
        iters=iters,
        refine_passes=refine_passes,
    )



def get_initial_xs(layer0, embeddings=None):
    layer0 = list(layer0)
    if embeddings is not None:
        if len(layer0) == 1:
            return {layer0[0]: 0}
        from sklearn.decomposition import PCA
        emb = np.array([embeddings[n] for n in layer0])
        pca = PCA(n_components=1)
        xs = pca.fit_transform(emb).flatten()
        idx = np.argsort(xs)
        sorted_nodes = [layer0[i] for i in idx]
        return {n: i for i, n in enumerate(sorted_nodes)}
    else:
        return {n: i for i, n in enumerate(layer0)}


def strict_right_down_layout(layer_nodes, edges, embeddings=None):

    # 1. 首层节点先横向均匀排开（或按特征排序）
    x_pos = {n: i for i, n in enumerate(layer_nodes[0])}
    if embeddings is not None:
        # 用embedding/PCA排序
        x_pos = get_initial_xs(layer_nodes[0], embeddings)

    # 2. 其余层严格右下方约束
    for i in range(1, len(layer_nodes)):
        prev_layer = layer_nodes[i - 1]
        curr_layer = layer_nodes[i]
        idx_prev = {n: x_pos[n] for n in prev_layer}
        for v in curr_layer:
            fanins = [u for u, w in edges if w == v and u in idx_prev]
            if fanins:
                min_x = max(idx_prev[u] for u in fanins)
                x_pos[v] = max(x_pos.get(v, 0), min_x)
        # 本层内再排序（防止重叠，可加入embedding排序作为tie-breaker）
        curr_sorted = sorted(curr_layer, key=lambda n: (x_pos[n], n))
        for offset, n in enumerate(curr_sorted):
            x_pos[n] += offset

    result = {}
    for i, layer in enumerate(layer_nodes):
        result[i] = sorted(layer, key=lambda n: x_pos[n])
    return result, x_pos




# from collections import defaultdict

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







def strict_left_down_layout(layer_nodes, edges, embeddings=None, start_layer_idx=0):
    # start_layer_idx允许直接对第N层做左下排序
    all_nodes = [n for layer in layer_nodes for n in layer]
    node2layer = {n: i for i, layer in enumerate(layer_nodes) for n in layer}

    # 首层（第start_layer_idx层）排序
    first_layer = layer_nodes[0]
    x_pos = {n: len(first_layer)-1-i for i, n in enumerate(first_layer)}  # 反向编号
    if embeddings is not None:
        x_pos = get_initial_xs(layer_nodes[0], embeddings)
        # 然后反向编号
        tmp = sorted(layer_nodes[0], key=lambda n: x_pos[n], reverse=True)
        x_pos = {n: i for i, n in enumerate(tmp)}

    for i in range(1, len(layer_nodes)):
        prev_layer = layer_nodes[i - 1]
        curr_layer = layer_nodes[i]
        idx_prev = {n: x_pos[n] for n in prev_layer}
        for v in curr_layer:
            fanins = [u for u, w in edges if w == v and u in idx_prev]
            if fanins:
                max_x = min(idx_prev[u] for u in fanins)
                x_pos[v] = min(x_pos.get(v, float('inf')), max_x)
        curr_sorted = sorted(curr_layer, key=lambda n: (x_pos[n], n), reverse=True)
        for offset, n in enumerate(curr_sorted):
            x_pos[n] -= offset  # x依次更小，防止重叠
    result = {}
    for i, layer in enumerate(layer_nodes):
        result[i] = sorted(layer, key=lambda n: x_pos[n], reverse=True)
    return result, x_pos





def minimize_crossings_strict_right_down_first_layer(layer_nodes, edges, x_pos):
    """
    layer_nodes: List[List[node]]
    edges: List[(u, v)]
    x_pos: dict[node, x]
    return: new_sorted_layers, new_x_pos
    """
    import copy
    x_pos = copy.deepcopy(x_pos)
    curr_layer = sorted(layer_nodes[0], key=lambda n: x_pos[n])
    next_layer = layer_nodes[1] if len(layer_nodes) > 1 else []
    idx_next = {n: x_pos[n] for n in next_layer}
    improved = True
    while improved:
        improved = False
        for i in range(len(curr_layer) - 1):
            n1, n2 = curr_layer[i], curr_layer[i+1]
            # n2提前，n1后移，需保证所有fanout依赖右下（即下层fanout不能反超）
            fanouts1 = [w for u, w in edges if u == n1 and w in idx_next]
            if fanouts1 and min(idx_next[w] for w in fanouts1) < i+1:
                continue
            fanouts2 = [w for u, w in edges if u == n2 and w in idx_next]
            if fanouts2 and min(idx_next[w] for w in fanouts2) < i:
                continue
            # 若合法，评估交换是否减少交叉数
            tmp_layer = curr_layer[:i] + [n2, n1] + curr_layer[i+2:]
            cross0 = count_crossings_fast(curr_layer, next_layer, edges)
            cross1 = count_crossings_fast(tmp_layer, next_layer, edges)
            if cross1 < cross0:
                curr_layer = tmp_layer
                improved = True
                # 更新x_pos
                for j, n in enumerate(curr_layer):
                    x_pos[n] = j
                idx_next = {n: x_pos[n] for n in next_layer}
                break
    # 最终x_pos按新顺序同步
    for j, n in enumerate(curr_layer):
        x_pos[n] = j
    # 构造排序输出
    result = {0: list(curr_layer)}
    # 其他层顺序不变
    for i in range(1, len(layer_nodes)):
        result[i] = sorted(layer_nodes[i], key=lambda n: x_pos[n])
    return result, x_pos



# ----------- 交叉数统计 ----------- #
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



# ----------- 层级可视化 -----------
def visualize_layered_graph(layer_nodes, edges, circuitName, savedPath):
    G = nx.DiGraph()
    G.add_edges_from(edges)
    pos = {}
    y_gap = 2.0
    x_gap = 1.2
    for layer, nodes_sorted in enumerate(layer_nodes):   # 用enumerate！
        n = len(nodes_sorted)
        start_x = - (n - 1) * x_gap / 2
        for i, node in enumerate(nodes_sorted):
            x = start_x + i * x_gap
            y = -layer * y_gap
            pos[node] = (x, y)
    fig_w = min(16, max(8, len(pos) // 10))
    fig_h = min(10, max(3, 0.8 * len(layer_nodes)))
    plt.figure(figsize=(fig_w, fig_h))
    nx.draw(
        G, pos,
        with_labels=True, node_size=120, node_color='skyblue',
        font_size=6, arrows=True, arrowstyle='-|>', arrowsize=10,
        edge_color='gray'
    )
    plt.title('Hierarchical Layout (Sorted Each Layer)', fontsize=10)
    plt.xlabel('X')
    plt.ylabel('Layer')
    plt.tight_layout()
    

    os.makedirs(savedPath, exist_ok=True)  # 自动创建文件夹（如果不存在）
    # 假设 circuitName = 'mux41.v'
    base_name = os.path.splitext(os.path.basename(circuitName))[0]
    save_path = os.path.join(savedPath, base_name + ".png")
    plt.savefig(save_path, dpi=300)
    

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


### version 0 
def visualize_strict_right_down(parse,
                                sorted_nodes_per_layer,
                                x_pos,
                                edges,
                                crossings_per_layer,
                                savedPath):


    # 颜色与标签映射，与 visualize_layered_graph_sorted 保持一致
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

    # 统计节点总数
    total_nodes = parse.effective_nodes_num

    # 坐标缩放与布局参数（与另一函数保持一致）
    y_gap = 1.0
    x_gap = 1.0

    os.makedirs(savedPath, exist_ok=True)
    base_name = osp.splitext(osp.basename(parse.fileName))[0]
    save_path = osp.join(savedPath, base_name + ".svg")

    # 图例句柄（两种模式都要用到）
    legend_handles = [
        mpatches.Patch(color=type2color[typ], label=type2label[typ])
        for typ in type2color
    ]

    if total_nodes <= 5000:
        # networkx 正常绘制模式
        G = nx.DiGraph()
        
        pos = {}
        node_colors = []
        labels = {}

        for layer, nodes_sorted in sorted_nodes_per_layer.items():
            for i, node_int64 in enumerate(nodes_sorted):
                node = int(node_int64)
                x = float(x_pos[node]) * x_gap
                y = -layer * y_gap
                # if node == 3:
                #     x -= 0.5
                pos[node] = (x, y)
                G.add_node(node)
                node_type = parse.getNodeTypeEnum(node)
                label = type2label.get(node_type, 'N')
                color = type2color.get(node_type, '#d3d3d3')
                labels[node] = label
                node_colors.append(color)
        
        G.add_edges_from((int(u), int(v)) for u, v in edges)

        # 画布尺寸策略与另一函数一致/相近
        fig_w = min(16, max(8, len(pos) // 10))
        fig_h = min(16, max(3, 0.8 * len(sorted_nodes_per_layer)))
        plt.figure(figsize=(fig_w, fig_h))

        nx.draw(
            G, pos,
            with_labels=True,
            node_color=node_colors,
            node_size=1000,
            arrows=True,
            font_size=12,
        )

    else:
        # 大图：像素散点模式（更快更省显存）
        from collections import defaultdict
        xs_by_type, ys_by_type = defaultdict(list), defaultdict(list)

        for layer, nodes_sorted in sorted_nodes_per_layer.items():
            for node in nodes_sorted:
                node = int(node)
                x = float(x_pos[node]) * x_gap
                y = -layer * y_gap
                node_type = parse.getNodeTypeEnum(node)
                xs_by_type[node_type].append(x)
                ys_by_type[node_type].append(y)

        fig_w = min(18, max(8, total_nodes // 150))
        fig_h = min(10, max(3, 0.8 * len(sorted_nodes_per_layer)))
        plt.figure(figsize=(fig_w, fig_h))

        for typ, xs in xs_by_type.items():
            ys = ys_by_type[typ]
            plt.scatter(
                xs, ys,
                c=type2color.get(typ, '#d3d3d3'),
                s=1,               # 任意占位，像素标记主要由 marker 决定
                marker=',',        # 单像素
                linewidths=0,      # 取消描边，避免放大
                alpha=1.0,         # 半透明在单像素上不明显，且会触发混合
                label=type2label.get(typ, str(typ)),
                rasterized=False,   # 大量点更省内存/更快渲染（尤其导出PDF）
                antialiased=True  # 关抗锯齿，像素更利落
            )

        

    # 统一的装饰与保存
    plt.legend(handles=legend_handles, loc='lower left', markerscale=18, fontsize=14)
    plt.axis('off')
    # plt.tight_layout()
    plt.savefig(save_path, bbox_inches='tight', pad_inches=0.02)
    plt.close()
    print(f"[✅] Circuit diagram: {save_path}")


#### version1
# def visualize_strict_right_down(parse,
#                                 sorted_nodes_per_layer,
#                                 x_pos,
#                                 edges,
#                                 savedPath):


#     # 颜色与标签映射，与 visualize_layered_graph_sorted 保持一致
#     type2color = {
#         iFCN_Lab.NodeType.Input:      '#6ecff6',
#         iFCN_Lab.NodeType.Output:     '#f6bc6e',
#         iFCN_Lab.NodeType.Maj:        '#f1c40f',
#         iFCN_Lab.NodeType.And:        '#f66e6e',
#         iFCN_Lab.NodeType.Or:         '#7d6ef6',
#         iFCN_Lab.NodeType.Not:        '#b9f66e',
#         iFCN_Lab.NodeType.Redundancy: '#34495e', 
#         iFCN_Lab.NodeType.Fanout:     '#2ecc71',
#     }
#     type2label = {
#         iFCN_Lab.NodeType.Input:      'INPUT',
#         iFCN_Lab.NodeType.Output:     'OUTPUT',
#         iFCN_Lab.NodeType.Maj:        'MAJORITY',
#         iFCN_Lab.NodeType.And:        'AND (&)',
#         iFCN_Lab.NodeType.Or:         'OR (|)',
#         iFCN_Lab.NodeType.Not:        'NOT (¬)',
#         iFCN_Lab.NodeType.Redundancy: 'WIRE',
#         iFCN_Lab.NodeType.Fanout:     'FANOUT',
#     }

#     # 统计节点总数
#     total_nodes = parse.effective_nodes_num

#     # 坐标缩放与布局参数（与另一函数保持一致）
#     y_gap = 2.0
#     x_gap = 1.2

#     os.makedirs(savedPath, exist_ok=True)
#     base_name = osp.splitext(osp.basename(parse.fileName))[0]
#     save_path = osp.join(savedPath, base_name + ".svg")

#     # 图例句柄（两种模式都要用到）
#     legend_handles = [
#         mpatches.Patch(color=type2color[typ], label=type2label[typ])
#         for typ in type2color
#     ]

#     from collections import defaultdict
#     xs_by_type, ys_by_type = defaultdict(list), defaultdict(list)

#     for layer, nodes_sorted in sorted_nodes_per_layer.items():
#         for node in nodes_sorted:
#             node = int(node)
#             x = float(x_pos[node]) * x_gap
#             y = -layer * y_gap
#             node_type = parse.getNodeTypeEnum(node)
#             xs_by_type[node_type].append(x)
#             ys_by_type[node_type].append(y)

#     fig_w = min(18, max(8, total_nodes // 150))
#     fig_h = min(10, max(3, 0.8 * len(sorted_nodes_per_layer)))
#     plt.figure(figsize=(fig_w, fig_h))

#     for typ, xs in xs_by_type.items():
#         ys = ys_by_type[typ]
#         plt.scatter(
#             xs, ys,
#             c=type2color.get(typ, '#d3d3d3'),
#             s=20, alpha=0.8, marker='.',
#             label=type2label.get(typ, str(typ))
#         )

#     # ------------------ 替代 draw edges 为像素线 ------------------
#     for u, v in edges:
#         layer_u = parse.get_layer_of_node(u)
#         layer_v = parse.get_layer_of_node(v)
#         x1 = float(x_pos[u]) * x_gap
#         y1 = -layer_u * y_gap
#         x2 = float(x_pos[v]) * x_gap
#         y2 = -layer_v * y_gap

#         dx = 0.1
#         dy = 0.1

#         # 路径点
#         route_x = []
#         route_y = []

#         # 垂直走 y1 -> y2
#         steps_y = int(abs(y2 - y1) / dy)
#         for i in range(steps_y + 1):
#             route_x.append(x1)
#             route_y.append(y1 + np.sign(y2 - y1) * i * dy)

#         # 水平走 x1 -> x2
#         steps_x = int(abs(x2 - x1) / dx)
#         for i in range(1, steps_x + 1):
#             route_x.append(x1 + np.sign(x2 - x1) * i * dx)
#             route_y.append(y2)

#         # 绘制像素连线
#         plt.scatter(route_x, route_y, c='gray', s=0.4, alpha=0.6, marker='.')

#     # ------------------ 保存 ------------------
#     plt.axis('off')
#     plt.tight_layout()
#     plt.savefig(save_path, bbox_inches='tight', pad_inches=0.02)
#     plt.close()
#     print(f"图已保存: {save_path}")


# version 2
# def visualize_strict_right_down(parse,
#                                 sorted_nodes_per_layer,
#                                 x_pos,
#                                 edges,
#                                 crossings_per_layer,
#                                 savedPath):

#     # 颜色与标签映射，与 visualize_layered_graph_sorted 保持一致
#     type2color = {
#         iFCN_Lab.NodeType.Input:      '#6ecff6',
#         iFCN_Lab.NodeType.Output:     '#f6bc6e',
#         iFCN_Lab.NodeType.Maj:        '#f1c40f',
#         iFCN_Lab.NodeType.And:        '#f66e6e',
#         iFCN_Lab.NodeType.Or:         '#7d6ef6',
#         iFCN_Lab.NodeType.Not:        '#b9f66e',
#         iFCN_Lab.NodeType.Redundancy: '#34495e', 
#         iFCN_Lab.NodeType.Fanout:     '#2ecc71',
#     }
#     type2label = {
#         iFCN_Lab.NodeType.Input:      'INPUT',
#         iFCN_Lab.NodeType.Output:     'OUTPUT',
#         iFCN_Lab.NodeType.Maj:        'MAJORITY',
#         iFCN_Lab.NodeType.And:        'AND (&)',
#         iFCN_Lab.NodeType.Or:         'OR (|)',
#         iFCN_Lab.NodeType.Not:        'NOT (¬)',
#         iFCN_Lab.NodeType.Redundancy: 'WIRE',
#         iFCN_Lab.NodeType.Fanout:     'FANOUT',
#     }

#     total_nodes = parse.effective_nodes_num
#     y_gap = 0.2
#     x_gap = 0.2

#     os.makedirs(savedPath, exist_ok=True)
#     base_name = osp.splitext(osp.basename(parse.fileName))[0]
#     save_path = osp.join(savedPath, base_name + ".svg")

#     legend_handles = [
#         mpatches.Patch(color=type2color[typ], label=type2label[typ])
#         for typ in type2color
#     ]

#     if total_nodes <= 5000:
#         G = nx.DiGraph()
#         pos = {}
#         node_colors = []
#         labels = {}

#         # 构建累计交叉线数量 dy_per_layer
#         dy_per_layer = {0: 0}
#         for layer in range(1, len(sorted_nodes_per_layer)):
#             prev = dy_per_layer[layer - 1]
#             crossings = crossings_per_layer.get(layer - 1, 0)
#             dy_per_layer[layer] = prev + crossings + 1

#         for layer, nodes_sorted in sorted_nodes_per_layer.items():
#             for i, node_int64 in enumerate(nodes_sorted):
#                 node = int(node_int64)
#                 x = float(x_pos[node]) * x_gap
#                 y = -dy_per_layer[layer] * y_gap
#                 pos[node] = (x, y)
#                 G.add_node(node)
#                 node_type = parse.getNodeTypeEnum(node)
#                 label = type2label.get(node_type, 'N')
#                 color = type2color.get(node_type, '#d3d3d3')
#                 labels[node] = label
#                 node_colors.append(color)

#         G.add_edges_from((int(u), int(v)) for u, v in edges)

#         fig_w = min(16, max(8, len(pos) // 10))
#         fig_h = min(16, max(3, 0.8 * len(sorted_nodes_per_layer)))
#         plt.figure(figsize=(fig_w, fig_h))

#         nx.draw(
#             G, pos,
#             with_labels=True,
#             node_color=node_colors,
#             node_size=100,
#             arrows=True,
#             font_size=6,
#         )

#         plt.axis('off')
#         plt.tight_layout()
#         plt.savefig(save_path, bbox_inches='tight', pad_inches=0.02)
#         plt.close()
#         print(f"图已保存: {save_path}")





# ----------- Scatter像素可视化 -----------
def visualize_scatter_layout(layer_nodes, sorted_nodes_per_layer):
    xs, ys, cs = [], [], []
    y_gap = 2.0
    x_gap = 1.2
    for layer, nodes_sorted in sorted_nodes_per_layer.items():
        n = len(nodes_sorted)
        start_x = - (n - 1) * x_gap / 2
        for i, _ in enumerate(nodes_sorted):
            x = start_x + i * x_gap
            y = -layer * y_gap
            xs.append(x)
            ys.append(y)
            cs.append(layer)
    plt.figure(figsize=(12, 0.5 * len(layer_nodes)))
    plt.scatter(xs, ys, c=cs, cmap='rainbow', s=4, alpha=0.7, marker='.')
    plt.title('Hierarchical Node Distribution (Pixel Scatter)')
    plt.xlabel('X')
    plt.ylabel('Layer')
    plt.grid(True, linestyle=':')
    plt.show()


# ================= Graphviz + exact-gain sifting production flow ===================
def _graphviz_sifting_generate(layer_nodes, edges, node_to_index):
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


# ================= 2DDWave normal graph flow ===================
def normal_graph_generate_2ddwave(
    data,
    layer_nodes,
    edges,
    node_to_index,
    v_file_path,
    save_training_curve=True,
):
    # ``data``, ``v_file_path`` and ``save_training_curve`` remain in the
    # signature so GUI/RL callers do not need a migration.  No neural training
    # is performed in the production ordering path anymore.
    _ = (data, v_file_path, save_training_curve)
    return _graphviz_sifting_generate(layer_nodes, edges, node_to_index)


# ================= Backward-compatible RL warm-order entry ===================
def normal_generate(data, layer_nodes, edges, node_to_index, v_file_path, device="auto"):
    _ = (data, v_file_path, device)
    return _graphviz_sifting_generate(layer_nodes, edges, node_to_index)


def normal_generate_optimize(data, layer_nodes,node_to_index, edges):
    device = safe_torch_device()
    print("Using device:", device)
    # 1. 训练GCN
    model = GCN(
        in_channels=data.num_features,
        edge_channels=_data_edge_channels(data),
    )
    trained_model = train_gcn(model, data, device=device)
    embeddings = get_embeddings(trained_model, data, device=device)

    # 2. 每层初排（可选：按GCN+PCA排序作为初始）
    sorted_nodes_per_layer = {}
    for layer, nodes in enumerate(layer_nodes):
        sorted_nodes = sort_nodes_by_embedding(embeddings, nodes, node_to_index)
        sorted_nodes_per_layer[layer] = sorted_nodes

    # 3. 多轮Barycenter全局优化
    # 直接用原layer_nodes结构全局多轮barycenter优化
    barycenter_opt_layers = minimize_crossing_gcn_guided(
        [sorted_nodes_per_layer[i] for i in range(len(layer_nodes))],
        edges,
        embeddings,
        node_to_index,
        iters=10,
        refine_passes=1,
    )

    # 3. 优化：遍历删除
    layer_nodes_new = [list(barycenter_opt_layers[i]) for i in range(len(barycenter_opt_layers))]
    edges_new = edges.copy()
    removed_pairs = {}    # {被删层idx: [ (fanin, node, fanout), ... ]}
    hidden_nodes = set()  # 所有被删/隐藏掉的节点

    i = 0
    while i < len(layer_nodes_new) - 2:
        layer1 = layer_nodes_new[i]
        layer2 = layer_nodes_new[i+1]
        layer3 = layer_nodes_new[i+2]
        cross = count_crossings_fast(layer1, layer2, edges_new)
        print(f"Check Layer {i+1}: crossings={cross}, sizes: {len(layer1)}->{len(layer2)}->{len(layer3)}")
        if cross == 0 and len(layer2) == len(layer1):
            # 记录每个node的 (fanin, node, fanout)
            fanins = {n: [u for u, v in edges_new if v == n and u in layer1] for n in layer2}
            fanouts = {n: [v for u, v in edges_new if u == n and v in layer3] for n in layer2}
            pairs = []
            for n in layer2:
                us = fanins[n]
                vs = fanouts[n]
                for u in us:
                    for v in vs:
                        pairs.append((u, n, v))
                        if (u, n) in edges_new: edges_new.remove((u, n))
                        if (n, v) in edges_new: edges_new.remove((n, v))
                        edges_new.append((u, v))
                hidden_nodes.add(n)
            removed_pairs[i+1] = pairs  # i+1是被删的层
            layer_nodes_new.pop(i+1)
            continue  # 不加i，连续检测当前位置
        i += 1

    print("All hidden (deleted) nodes:", sorted(hidden_nodes))
    print("Removed layers (layer idx -> [ (fanin, node, fanout), ... ]):", removed_pairs)
    # 统计优化了多少层
    num_layers_optimized = len(removed_pairs)
    # 统计隐藏了多少个node
    num_hidden_nodes = len(hidden_nodes)
    print(f"\n优化/删除的层数: {num_layers_optimized}")
    print(f"被隐藏/合并的节点数: {num_hidden_nodes}\n")

    # 4. 可视化——直接用删层后的顺序，不重新排序
        # === 统计每层交叉线 ===
    sorted_nodes_per_layer_new = {i: layer for i, layer in enumerate(layer_nodes_new)}
    crossings_per_layer = {}
    total_crossings = 0
    for i in range(len(layer_nodes_new) - 1):
        layer1 = sorted_nodes_per_layer_new[i]
        layer2 = sorted_nodes_per_layer_new[i+1]
        cross = count_crossings_fast(layer1, layer2, edges_new)
        crossings_per_layer[i] = cross
        total_crossings += cross
        print(f"Layer {i}-{i+1} crossings: {cross} , Layer {i} nodes: {len(layer1)}, Layer {i+1} nodes: {len(layer2)}")
    print(f"Total crossings (all adjacent layers): {total_crossings}")
    return sorted_nodes_per_layer_new, edges_new, crossings_per_layer
  

##### 2DDwave #####
def TDDwave_generate(data, layer_nodes, edges, v_file_path):
    device = safe_torch_device()
    print("Using device:", device)
    # 1. 训练GCN
    # model = GCN(in_channels=data.num_features)
    # trained_model = train_gcn(model, data, v_file_path, device=device)
    # embeddings = get_embeddings(trained_model, data, device=device)
    embeddings = None

    # 2. Right-Down顺序排序
    sorted_nodes_per_layer, x_pos = strict_right_down_layout_max_fanin_right(layer_nodes, edges, embeddings=None)
    crossings_per_layer = {}
    # 3. 统计交叉数（可选）
    # total_crossings = 0
    # for i in range(len(layer_nodes) - 1):
    #     layer1 = sorted_nodes_per_layer[i]
    #     layer2 = sorted_nodes_per_layer[i+1]
    #     cross = count_crossings_fast(layer1, layer2, edges)
    #     crossings_per_layer[i] = cross
        # print(f"Layer {i}-{i+1} crossings: {cross} , Layer {i} nodes: {len(layer1)}, Layer {i+1} nodes: {len(layer2)}")
    #     total_crossings += cross
    # print(f"Total crossings (all adjacent layers): {total_crossings}")

    return sorted_nodes_per_layer, x_pos, edges, crossings_per_layer, embeddings

def TDDwave_generate_optimize(data, layer_nodes, edges):
    device = safe_torch_device()
    # device = torch.device("cpu")
    print("Using device:", device)
    # 1. 训练GCN
    model = GCN(
        in_channels=data.num_features,
        edge_channels=_data_edge_channels(data),
    )
    trained_model = train_gcn(model, data, device=device)
    embeddings = get_embeddings(trained_model, data, device=device)

    # 2. GCN排序+右下优化
    sorted_nodes_per_layer, x_pos = strict_right_down_layout(layer_nodes, edges, embeddings=embeddings)

    # 3. 优化：遍历删除
    layer_nodes_new = [list(sorted_nodes_per_layer[i]) for i in range(len(sorted_nodes_per_layer))]
    edges_new = copy.deepcopy(edges)
    removed_pairs = {}    # {被删层idx: [ (fanin, node, fanout), ... ]}
    hidden_nodes = set()  # 所有被删/隐藏掉的节点

    i = 0
    while i < len(layer_nodes_new) - 2:
        layer1 = layer_nodes_new[i]
        layer2 = layer_nodes_new[i+1]
        layer3 = layer_nodes_new[i+2]
        cross = count_crossings_fast(layer1, layer2, edges_new)
        if cross == 0 and len(layer2) == len(layer1):
            fanins = {n: [u for u, v in edges_new if v == n and u in layer1] for n in layer2}
            fanouts = {n: [v for u, v in edges_new if u == n and v in layer3] for n in layer2}
            pairs = []
            for n in layer2:
                us = fanins[n]
                vs = fanouts[n]
                for u in us:
                    for v in vs:
                        pairs.append((u, n, v))
                        if (u, n) in edges_new: edges_new.remove((u, n))
                        if (n, v) in edges_new: edges_new.remove((n, v))
                        edges_new.append((u, v))
                hidden_nodes.add(n)
            removed_pairs[i+1] = pairs  # i+1是被删的层
            layer_nodes_new.pop(i+1)
            continue  # 不加i，连续检测当前位置
        i += 1

    print("All hidden (deleted) nodes:", sorted(hidden_nodes))
    print("Removed layers (layer idx -> [ (fanin, node, fanout), ... ]):", removed_pairs)
    # 统计优化了多少层
    num_layers_optimized = len(removed_pairs)
    # 统计隐藏了多少个node
    num_hidden_nodes = len(hidden_nodes)
    print(f"\n优化/删除的层数: {num_layers_optimized}")
    print(f"被隐藏/合并的节点数: {num_hidden_nodes}\n")
    
    # === 统计每层交叉线 ===
    sorted_nodes_per_layer_new = {i: layer for i, layer in enumerate(layer_nodes_new)}
    crossings_per_layer = {}
    total_crossings = 0
    for i in range(len(layer_nodes_new) - 1):
        layer1 = sorted_nodes_per_layer_new[i]
        layer2 = sorted_nodes_per_layer_new[i+1]
        cross = count_crossings_fast(layer1, layer2, edges_new)
        crossings_per_layer[i] = cross
        total_crossings += cross
        print(f"Layer {i}-{i+1} crossings: {cross} , Layer {i} nodes: {len(layer1)}, Layer {i+1} nodes: {len(layer2)}")
    print(f"Total crossings (all adjacent layers): {total_crossings}")

    # print("test0:", layer_nodes)
    # print("test1:", layer_nodes_new)
    # print("test2:", edges)
    # print("test3:", edges_new)
    return layer_nodes_new, sorted_nodes_per_layer_new, x_pos, edges_new, crossings_per_layer, embeddings




##### 2DDwave转折 #####
def twoPart_TDDwave_generate(data, layer_nodes, edges):

    device = safe_torch_device()
    print("Using device:", device)
    # 1. 训练GCN
    model = GCN(
        in_channels=data.num_features,
        edge_channels=_data_edge_channels(data),
    )
    trained_model = train_gcn(model, data, device=device)
    embeddings = get_embeddings(trained_model, data, device=device)

    # 2. 两段分开排序方向
    num_layers = len(layer_nodes)
    mid = num_layers // 2

    right_layers = layer_nodes[:mid]
    left_layers = layer_nodes[mid:]

    # 前半部分右下布局
    right_sorted, right_x = strict_right_down_layout(right_layers, edges, embeddings=embeddings)
    # 后半部分左下布局
    left_sorted, left_x = strict_left_down_layout(left_layers, edges, embeddings=embeddings)

    # 衔接x坐标（整体平移后半部分，防止重叠）
    offset = max(right_x.values()) + 1 if right_x else 0
    left_x = {k: v + offset for k, v in left_x.items()}

    # 合并排序和坐标
    sorted_nodes_per_layer = {}
    x_pos = {}
    for i in range(mid):
        sorted_nodes_per_layer[i] = right_sorted[i]
        x_pos.update(right_x)
    for i in range(mid, num_layers):
        sorted_nodes_per_layer[i] = left_sorted[i - mid]
        x_pos.update(left_x)

    # === 合并后统一消层 ===
    layer_nodes_new = [sorted_nodes_per_layer[i] for i in range(num_layers)]
    edges_new = copy.deepcopy(edges)
    removed_pairs = {}    # {被删层idx: [ (fanin, node, fanout), ... ]}
    hidden_nodes = set()  # 所有被删/隐藏掉的节点

    i = 0
    while i < len(layer_nodes_new) - 2:
        layer1 = layer_nodes_new[i]
        layer2 = layer_nodes_new[i+1]
        layer3 = layer_nodes_new[i+2]
        cross = count_crossings_fast(layer1, layer2, edges_new)
        if cross == 0 and len(layer2) == len(layer1):
            fanins = {n: [u for u, v in edges_new if v == n and u in layer1] for n in layer2}
            fanouts = {n: [v for u, v in edges_new if u == n and v in layer3] for n in layer2}
            pairs = []
            for n in layer2:
                us = fanins[n]
                vs = fanouts[n]
                for u in us:
                    for v in vs:
                        pairs.append((u, n, v))
                        if (u, n) in edges_new: edges_new.remove((u, n))
                        if (n, v) in edges_new: edges_new.remove((n, v))
                        edges_new.append((u, v))
                hidden_nodes.add(n)
            removed_pairs[i+1] = pairs  # i+1是被删的层
            layer_nodes_new.pop(i+1)
            continue  # 不加i，连续检测当前位置
        i += 1

    # 统计优化效果
    print("\nAll hidden (deleted) nodes:", sorted(hidden_nodes))
    print("Removed layers (layer idx -> [ (fanin, node, fanout), ... ]):", removed_pairs)
    print(f"\n优化/删除的层数: {len(removed_pairs)}")
    print(f"被隐藏/合并的节点数: {len(hidden_nodes)}")
    for idx, nodes in removed_pairs.items():
        print(f"  删除第{idx}层，隐藏节点数: {len(nodes)}")

    # === 统计每层交叉线 ===
    sorted_nodes_per_layer_new = {i: layer for i, layer in enumerate(layer_nodes_new)}
    crossings_per_layer = {}
    total_crossings = 0
    for i in range(len(layer_nodes_new) - 1):
        layer1 = sorted_nodes_per_layer_new[i]
        layer2 = sorted_nodes_per_layer_new[i+1]
        cross = count_crossings_fast(layer1, layer2, edges_new)
        crossings_per_layer[i] = cross
        total_crossings += cross
        print(f"Layer {i}-{i+1} crossings: {cross} , Layer {i} nodes: {len(layer1)}, Layer {i+1} nodes: {len(layer2)}")
    print(f"Total crossings (all adjacent layers): {total_crossings}")


    return layer_nodes_new, sorted_nodes_per_layer_new, x_pos, edges_new, crossings_per_layer
