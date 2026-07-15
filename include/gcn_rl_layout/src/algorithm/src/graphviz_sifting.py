"""Deterministic fixed-layer Graphviz ordering plus exact-gain sifting.

This module replaces the learned GCN warm ordering in the production layout
path.  Graphviz ``dot`` receives only adjacent-layer edges so its objective is
aligned with the crossing counter used by this project.  The returned order is
then refined by insertion moves.  Every evaluated move is scored by the exact
project counter and is accepted only on a strict crossing reduction.

Small layers evaluate every insertion position.  Large layers use a fixed,
deterministic candidate set and bounded node/evaluation/time budgets so the GUI
remains responsive on large EPFL circuits.
"""

from __future__ import annotations

import math
import os
import subprocess
import tempfile
import time
from collections import Counter, defaultdict
from pathlib import Path

import numpy as np


def _env_int(name, default, minimum=0):
    try:
        value = int(os.environ.get(name, default))
    except (TypeError, ValueError):
        value = int(default)
    return max(int(minimum), value)


def _env_float(name, default, minimum=0.0):
    try:
        value = float(os.environ.get(name, default))
    except (TypeError, ValueError):
        value = float(default)
    return max(float(minimum), value)


def normalize_layers(layer_nodes):
    if isinstance(layer_nodes, dict):
        return [
            [int(node) for node in layer_nodes[index]]
            for index in sorted(layer_nodes)
        ]
    return [[int(node) for node in layer] for layer in layer_nodes]


def adjacent_layer_edges(layers, edges):
    node_layer = {
        int(node): layer_index
        for layer_index, layer in enumerate(layers)
        for node in layer
    }
    return [
        (int(source), int(target))
        for source, target in edges
        if node_layer.get(int(target)) == node_layer.get(int(source), -2) + 1
    ]


def _graphviz_source(layers, edges):
    nodes = [int(node) for layer in layers for node in layer]
    names = {node: "n{}".format(index) for index, node in enumerate(nodes)}
    lines = [
        "digraph fixed_layers {",
        "  graph [rankdir=TB, mclimit=1.0, remincross=true, ranksep=0.6, nodesep=0.2];",
        '  node [shape=box, width=0.01, height=0.01, fixedsize=true, label=""];',
    ]
    for layer_index, layer in enumerate(layers):
        lines.append(
            "  subgraph rank_{} {{ rank=same; {} }}".format(
                layer_index,
                "; ".join(names[int(node)] for node in layer),
            )
        )
    for source, target in edges:
        if source in names and target in names and source != target:
            lines.append("  {} -> {};".format(names[source], names[target]))
    lines.append("}")
    return "\n".join(lines) + "\n", names


def graphviz_fixed_layer_order(layers, edges, timeout_seconds=None, dot_binary=None):
    """Return Graphviz dot/mincross order while preserving every fixed layer."""
    layers = normalize_layers(layers)
    timeout_seconds = (
        _env_float("IFCN_GRAPHVIZ_TIMEOUT", 60.0, minimum=0.1)
        if timeout_seconds is None
        else max(0.1, float(timeout_seconds))
    )
    dot_binary = dot_binary or os.environ.get("IFCN_GRAPHVIZ_DOT", "dot")
    source, names = _graphviz_source(layers, edges)
    reverse_names = {name: node for node, name in names.items()}
    with tempfile.TemporaryDirectory(prefix="ifcn-graphviz-") as directory:
        input_path = Path(directory) / "fixed_layers.dot"
        input_path.write_text(source, encoding="utf-8")
        started = time.perf_counter()
        process = subprocess.run(
            [str(dot_binary), "-Tplain", str(input_path)],
            capture_output=True,
            text=True,
            check=False,
            timeout=timeout_seconds,
        )
        elapsed = time.perf_counter() - started
    if process.returncode != 0:
        raise RuntimeError(
            "Graphviz dot failed with code {}: {}".format(
                process.returncode, process.stderr.strip()
            )
        )
    positions = {}
    for line in process.stdout.splitlines():
        fields = line.split()
        if len(fields) >= 4 and fields[0] == "node" and fields[1] in reverse_names:
            positions[reverse_names[fields[1]]] = float(fields[2])
    if set(positions) != set(names):
        missing = sorted(set(names) - set(positions))
        raise RuntimeError("Graphviz omitted {} nodes".format(len(missing)))
    ordered = []
    for layer in layers:
        original_rank = {node: index for index, node in enumerate(layer)}
        ordered.append(
            sorted(layer, key=lambda node: (positions[node], original_rank[node]))
        )
    _validate_order(layers, ordered)
    return ordered, elapsed


def _validate_order(expected, actual):
    if len(expected) != len(actual):
        raise RuntimeError("ordering changed the number of fixed layers")
    for layer_index, (expected_layer, actual_layer) in enumerate(zip(expected, actual)):
        if Counter(expected_layer) != Counter(actual_layer):
            raise RuntimeError(
                "ordering changed the node set of layer {}".format(layer_index)
            )


def _boundary_edges(layers, edges):
    node_layer = {
        int(node): layer_index
        for layer_index, layer in enumerate(layers)
        for node in layer
    }
    grouped = defaultdict(list)
    degree = defaultdict(int)
    for source, target in edges:
        source = int(source)
        target = int(target)
        boundary = node_layer.get(source)
        if boundary is not None and node_layer.get(target) == boundary + 1:
            grouped[boundary].append((source, target))
            degree[source] += 1
            degree[target] += 1
    return grouped, degree


def _local_crossings(layers, grouped_edges, layer_index, crossing_counter):
    value = 0
    if layer_index > 0:
        value += crossing_counter(
            layers[layer_index - 1],
            layers[layer_index],
            grouped_edges.get(layer_index - 1, ()),
        )
    if layer_index < len(layers) - 1:
        value += crossing_counter(
            layers[layer_index],
            layers[layer_index + 1],
            grouped_edges.get(layer_index, ()),
        )
    return int(value)


def _total_crossings(layers, grouped_edges, crossing_counter):
    return int(
        sum(
            crossing_counter(
                layers[index],
                layers[index + 1],
                grouped_edges.get(index, ()),
            )
            for index in range(len(layers) - 1)
        )
    )


def _candidate_positions(size, old_position, full_limit, radius, sample_count):
    if size <= full_limit:
        return list(range(size))
    positions = {0, size - 1, old_position}
    for delta in range(-radius, radius + 1):
        candidate = old_position + delta
        if 0 <= candidate < size:
            positions.add(candidate)
    for sample in range(max(2, sample_count)):
        positions.add(
            int(round(sample * (size - 1) / max(1, sample_count - 1)))
        )
    return sorted(positions)


def exact_gain_sifting(initial_layers, edges, crossing_counter, **overrides):
    """Monotonically refine an order and return the order plus diagnostics."""
    layers = normalize_layers(initial_layers)
    grouped_edges, degree = _boundary_edges(layers, edges)
    initial_crossings = _total_crossings(layers, grouped_edges, crossing_counter)
    max_passes = int(overrides.get("max_passes", _env_int("IFCN_SIFT_PASSES", 5, 1)))
    max_nodes = int(
        overrides.get(
            "max_nodes_per_layer",
            _env_int("IFCN_SIFT_MAX_NODES_PER_LAYER", 96, 1),
        )
    )
    full_limit = int(
        overrides.get(
            "full_position_limit",
            _env_int("IFCN_SIFT_FULL_POSITION_LIMIT", 128, 2),
        )
    )
    radius = int(
        overrides.get("candidate_radius", _env_int("IFCN_SIFT_CANDIDATE_RADIUS", 12, 0))
    )
    sample_count = int(
        overrides.get("sampled_positions", _env_int("IFCN_SIFT_SAMPLED_POSITIONS", 33, 2))
    )
    evaluation_budget = int(
        overrides.get("evaluation_budget", _env_int("IFCN_SIFT_EVALUATIONS", 200000, 1))
    )
    time_limit = float(
        overrides.get("time_limit", _env_float("IFCN_SIFT_TIMEOUT", 20.0, 0.0))
    )

    started = time.perf_counter()
    evaluations = 0
    exhausted = False
    for _pass_index in range(max_passes):
        improved = False
        priorities = [
            (_local_crossings(layers, grouped_edges, index, crossing_counter), index)
            for index, layer in enumerate(layers)
            if len(layer) > 1
        ]
        priorities.sort(key=lambda item: (-item[0], item[1]))
        for _crossings, layer_index in priorities:
            rank = {node: index for index, node in enumerate(layers[layer_index])}
            nodes = sorted(
                layers[layer_index],
                key=lambda node: (-degree[int(node)], rank[node], int(node)),
            )[:max_nodes]
            for node in nodes:
                if evaluations >= evaluation_budget or time.perf_counter() - started >= time_limit:
                    exhausted = True
                    break
                old_layer = list(layers[layer_index])
                old_position = old_layer.index(node)
                base_cost = _local_crossings(
                    layers, grouped_edges, layer_index, crossing_counter
                )
                best_cost = base_cost
                best_layer = old_layer
                without = old_layer[:old_position] + old_layer[old_position + 1 :]
                for new_position in _candidate_positions(
                    len(old_layer), old_position, full_limit, radius, sample_count
                ):
                    if new_position == old_position:
                        continue
                    if evaluations >= evaluation_budget or time.perf_counter() - started >= time_limit:
                        exhausted = True
                        break
                    candidate = list(without)
                    candidate.insert(new_position, node)
                    layers[layer_index] = candidate
                    candidate_cost = _local_crossings(
                        layers, grouped_edges, layer_index, crossing_counter
                    )
                    evaluations += 1
                    if candidate_cost < best_cost:
                        best_cost = candidate_cost
                        best_layer = candidate
                layers[layer_index] = list(best_layer)
                improved = improved or best_cost < base_cost
                if exhausted:
                    break
            if exhausted:
                break
        if exhausted or not improved:
            break

    final_crossings = _total_crossings(layers, grouped_edges, crossing_counter)
    if final_crossings > initial_crossings:
        raise RuntimeError("strict sifting increased the exact crossing count")
    _validate_order(initial_layers, layers)
    return layers, {
        "initial_crossings": initial_crossings,
        "final_crossings": final_crossings,
        "crossings_removed": initial_crossings - final_crossings,
        "evaluations": evaluations,
        "seconds": time.perf_counter() - started,
        "budget_exhausted": exhausted,
    }


def deterministic_layout_embeddings(layers, edges, node_to_index):
    """Provide stable structural features to legacy downstream embedding APIs.

    The production ordering no longer trains a GCN, but routing and placement
    code still consumes an embedding matrix for similarity and spacing hints.
    These six deterministic features preserve that interface without pretending
    they are learned representations.
    """
    layers = normalize_layers(layers)
    count = len(node_to_index)
    result = np.zeros((count, 6), dtype=np.float32)
    indegree = defaultdict(int)
    outdegree = defaultdict(int)
    for source, target in edges:
        outdegree[int(source)] += 1
        indegree[int(target)] += 1
    max_in = max(indegree.values(), default=1)
    max_out = max(outdegree.values(), default=1)
    layer_denominator = max(1, len(layers) - 1)
    for layer_index, layer in enumerate(layers):
        rank_denominator = max(1, len(layer) - 1)
        for rank, node in enumerate(layer):
            index = node_to_index.get(int(node))
            if index is None:
                continue
            normalized_rank = rank / rank_denominator
            result[int(index)] = np.asarray(
                [
                    4.0 * normalized_rank,
                    math.sin(math.pi * normalized_rank),
                    math.cos(math.pi * normalized_rank),
                    layer_index / layer_denominator,
                    math.log1p(indegree[int(node)]) / math.log1p(max_in),
                    math.log1p(outdegree[int(node)]) / math.log1p(max_out),
                ],
                dtype=np.float32,
            )
    return result


def graphviz_sifting_order(layer_nodes, edges, crossing_counter):
    """Run the complete production ordering and return diagnostics."""
    raw_layers = normalize_layers(layer_nodes)
    scored_edges = adjacent_layer_edges(raw_layers, edges)
    graphviz_started = time.perf_counter()
    fallback = False
    try:
        graphviz_layers, graphviz_seconds = graphviz_fixed_layer_order(
            raw_layers, scored_edges
        )
    except (FileNotFoundError, RuntimeError, subprocess.TimeoutExpired) as exc:
        fallback = True
        graphviz_layers = raw_layers
        graphviz_seconds = time.perf_counter() - graphviz_started
        print(
            "[Graphviz+sifting] Graphviz unavailable; refining the raw fixed-layer order: {}".format(
                exc
            )
        )
    refined_layers, sifting = exact_gain_sifting(
        graphviz_layers, scored_edges, crossing_counter
    )
    diagnostics = {
        "graphviz_seconds": graphviz_seconds,
        "graphviz_fallback_to_raw": fallback,
        "scored_adjacent_edges": len(scored_edges),
        **sifting,
    }
    return {index: layer for index, layer in enumerate(refined_layers)}, diagnostics
