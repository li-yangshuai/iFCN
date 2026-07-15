import os
import subprocess
from pathlib import Path


def _project_root():
    return Path(__file__).resolve().parents[5]


def _mapping_metrics_executable():
    root = _project_root()
    candidates = []
    env_path = os.environ.get("IFCN_MAPPING_METRICS_EXE")
    if env_path:
        candidates.append(Path(env_path))
    candidates.extend(
        [
            root / "build" / "ifcn_mapping_metrics",
            root / "build" / "src" / "ifcn_mapping_metrics",
        ]
    )
    for candidate in candidates:
        if candidate.is_file() and os.access(candidate, os.X_OK):
            return candidate
    return None


def _compute_mapping_metrics_from_file(filename):
    executable = _mapping_metrics_executable()
    if executable is None:
        return None

    try:
        result = subprocess.run(
            [str(executable), str(filename)],
            cwd=str(_project_root()),
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            timeout=120,
            check=False,
        )
    except Exception:
        return None

    if result.returncode != 0:
        return None
    parts = result.stdout.strip().split()
    if len(parts) < 2:
        return None
    try:
        return int(parts[0]), int(parts[1])
    except ValueError:
        return None


def _insert_or_update_mapping_metrics(filename, verbose=False):
    metrics = _compute_mapping_metrics_from_file(filename)
    if metrics is None:
        if verbose:
            print("[IFCN] Mapping metrics skipped: ifcn_mapping_metrics is not available.")
        return None

    path = Path(filename)
    try:
        lines = path.read_text(encoding="utf-8").splitlines()
    except OSError:
        return None

    filtered = [
        line
        for line in lines
        if not line.startswith("#cell count:")
        and not line.startswith("#cross count:")
        and not line.startswith("#phase cycle:")
    ]
    insert_at = None
    for index, line in enumerate(filtered):
        if line.startswith("#layout area:"):
            insert_at = index + 1
            break
    if insert_at is None:
        for index, line in enumerate(filtered):
            if line.startswith("#total layers:") or line.startswith("#edges number:"):
                insert_at = index + 1
    if insert_at is None:
        insert_at = 0

    cell_count, cross_count = metrics
    filtered[insert_at:insert_at] = [
        f"#cell count: {cell_count}",
        f"#cross count: {cross_count}",
    ]

    try:
        path.write_text("\n".join(filtered) + "\n", encoding="utf-8")
    except OSError:
        return None
    return metrics


def _phase_at(graphDraw, coord):
    phase_cycle = int(getattr(graphDraw, "phase_cycle", 4) or 4)
    if _is_2ddwave_scheme(getattr(graphDraw, "clock_scheme_name", "")):
        return (int(coord[0]) + int(coord[1])) % phase_cycle
    if not hasattr(graphDraw, "mapChessboard"):
        return -1
    try:
        return int(graphDraw.mapChessboard.getPhase((int(coord[0]), int(coord[1]))))
    except Exception:
        return -1


def _valid_route_phases(graphDraw, path):
    phases = []
    for coord in path or []:
        phase = _phase_at(graphDraw, coord)
        if phase >= 0:
            phases.append(phase)
    return phases


def _count_clock_cycles(phases, phase_cycle=4):
    if not phases:
        return 0.0

    phase_cycle = max(1, int(phase_cycle))
    start_phase = int(phases[0]) % phase_cycle
    relative_phases = [(int(phase) - start_phase) % phase_cycle for phase in phases]
    wraps = 0
    previous = relative_phases[0]
    for phase in relative_phases[1:]:
        if phase < previous:
            wraps += 1
        previous = phase

    final_phase = max(0, int(relative_phases[-1]))
    return wraps + (final_phase + 1) / phase_cycle


def _clock_cycles_for_sequence(graphDraw, sequence, phase_cycle):
    return _count_clock_cycles(_valid_route_phases(graphDraw, sequence), phase_cycle)


def _node_phase_sequence(graphDraw, node_id):
    try:
        return _valid_route_phases(graphDraw, [graphDraw.get_node_coord(int(node_id))])
    except Exception:
        return []


def _edge_route_phase_sequence(graphDraw, src, dst):
    routes = getattr(getattr(graphDraw, "mapChessboard", None), "nodePairRoutes", {})
    path = None
    if routes:
        key = (int(src), int(dst))
        if hasattr(routes, "get"):
            path = routes.get(key)
        if path is None:
            try:
                path = routes[key]
            except Exception:
                path = None
        if path is None and hasattr(routes, "items"):
            for (route_src, route_dst), route_path in routes.items():
                if int(route_src) == int(src) and int(route_dst) == int(dst):
                    path = route_path
                    break
    if path:
        return _valid_route_phases(graphDraw, path)

    sequence = []
    sequence.extend(_node_phase_sequence(graphDraw, src))
    sequence.extend(_node_phase_sequence(graphDraw, dst))
    return sequence


def _edge_route_path(graphDraw, src, dst):
    routes = getattr(getattr(graphDraw, "mapChessboard", None), "nodePairRoutes", {})
    if routes:
        key = (int(src), int(dst))
        path = routes.get(key) if hasattr(routes, "get") else None
        if path is None:
            try:
                path = routes[key]
            except Exception:
                path = None
        if path is None and hasattr(routes, "items"):
            for (route_src, route_dst), route_path in routes.items():
                if int(route_src) == int(src) and int(route_dst) == int(dst):
                    path = route_path
                    break
        if path:
            return [(int(x), int(y)) for x, y in path]

    try:
        src_coord = graphDraw.get_node_coord(int(src))
        dst_coord = graphDraw.get_node_coord(int(dst))
        return [(int(src_coord[0]), int(src_coord[1])), (int(dst_coord[0]), int(dst_coord[1]))]
    except Exception:
        return []


def _topological_nodes(graphDraw):
    parse = getattr(graphDraw, "parse", None)
    ordered = []
    seen = set()
    for layer in getattr(parse, "layer_nodes", []) or []:
        for node in layer:
            node = int(node)
            if node not in seen:
                ordered.append(node)
                seen.add(node)
    for node in getattr(parse, "effective_nodes", []) or []:
        node = int(node)
        if node not in seen:
            ordered.append(node)
            seen.add(node)
    return ordered


def _compute_critical_path_metrics(graphDraw, phase_cycle):
    parse = getattr(graphDraw, "parse", None)
    edges = [(int(u), int(v)) for u, v in (getattr(parse, "effective_edges", []) or [])]
    if not edges:
        routes = getattr(getattr(graphDraw, "mapChessboard", None), "nodePairRoutes", {})
        best_path = max(([(int(x), int(y)) for x, y in path] for path in routes.values()), key=len, default=[])
        return len(best_path), _clock_cycles_for_sequence(graphDraw, best_path, phase_cycle)

    adjacency = {}
    indegree = {}
    nodes = set()
    for src, dst in edges:
        adjacency.setdefault(src, []).append(dst)
        indegree.setdefault(src, 0)
        indegree[dst] = indegree.get(dst, 0) + 1
        nodes.add(src)
        nodes.add(dst)

    terminal_nodes = set()
    for node in getattr(parse, "getOutputNodesIndex", []) or []:
        terminal_nodes.add(int(node))
    if not terminal_nodes:
        terminal_nodes = {node for node in nodes if not adjacency.get(node)}

    best_path_by_node = {}
    for node in _topological_nodes(graphDraw):
        if node not in best_path_by_node and indegree.get(node, 0) == 0:
            try:
                x, y = graphDraw.get_node_coord(int(node))
                best_path_by_node[node] = [(int(x), int(y))]
            except Exception:
                best_path_by_node[node] = []

        prefix = best_path_by_node.get(node, [])
        for dst in adjacency.get(node, []):
            edge_path = _edge_route_path(graphDraw, node, dst)
            if not edge_path:
                continue
            candidate = list(prefix)
            if candidate and candidate[-1] == edge_path[0]:
                candidate.extend(edge_path[1:])
            else:
                candidate.extend(edge_path)
            if len(candidate) > len(best_path_by_node.get(dst, [])):
                best_path_by_node[dst] = candidate

    candidate_paths = [best_path_by_node[node] for node in terminal_nodes if node in best_path_by_node]
    if not candidate_paths:
        candidate_paths = list(best_path_by_node.values())
    best_path = max(
        candidate_paths,
        key=lambda path: (len(path), _clock_cycles_for_sequence(graphDraw, path, phase_cycle)),
        default=[],
    )
    return len(best_path), _clock_cycles_for_sequence(graphDraw, best_path, phase_cycle)


def _estimate_critical_path_metrics(graphDraw, phase_cycle):
    """Cache the route-derived metric shared by raw and encoded IFCN output."""
    cache = getattr(graphDraw, "_ifcn_critical_path_metrics_cache", None)
    cache_key = int(phase_cycle)
    if isinstance(cache, dict) and cache_key in cache:
        return cache[cache_key]
    value = _compute_critical_path_metrics(graphDraw, phase_cycle)
    if not isinstance(cache, dict):
        cache = {}
        setattr(graphDraw, "_ifcn_critical_path_metrics_cache", cache)
    cache[cache_key] = value
    return value


def _estimate_layout_clocks(graphDraw, phase_cycle):
    parse = getattr(graphDraw, "parse", None)
    edges = [(int(u), int(v)) for u, v in (getattr(parse, "effective_edges", []) or [])]
    if not edges:
        routes = getattr(getattr(graphDraw, "mapChessboard", None), "nodePairRoutes", {})
        return max((_clock_cycles_for_sequence(graphDraw, path, phase_cycle) for path in routes.values()), default=0.0)

    adjacency = {}
    indegree = {}
    for src, dst in edges:
        adjacency.setdefault(src, []).append(dst)
        indegree.setdefault(src, 0)
        indegree[dst] = indegree.get(dst, 0) + 1

    # Keep the best phase sequence per ending phase, because a lower immediate
    # clock count can still become the best path after the next wrap.
    best_by_node = {}
    for node in _topological_nodes(graphDraw):
        states = best_by_node.setdefault(node, {})
        if not states and indegree.get(node, 0) == 0:
            initial = _node_phase_sequence(graphDraw, node)
            if initial:
                states[initial[-1]] = initial

        for dst in adjacency.get(node, []):
            edge_phases = _edge_route_phase_sequence(graphDraw, node, dst)
            if not edge_phases:
                continue
            source_states = states or {edge_phases[0]: edge_phases[:1]}
            dst_states = best_by_node.setdefault(dst, {})
            for prefix in source_states.values():
                candidate = list(prefix)
                tail = edge_phases[1:] if candidate and candidate[-1] == edge_phases[0] else edge_phases
                candidate.extend(tail)
                if not candidate:
                    continue
                last_phase = candidate[-1]
                current = dst_states.get(last_phase)
                candidate_score = (_count_clock_cycles(candidate, phase_cycle), len(candidate))
                current_score = (_count_clock_cycles(current, phase_cycle), len(current)) if current else (-1, -1)
                if candidate_score > current_score:
                    dst_states[last_phase] = candidate

    sequences = [sequence for states in best_by_node.values() for sequence in states.values()]
    if sequences:
        return max(_count_clock_cycles(sequence, phase_cycle) for sequence in sequences)

    routes = getattr(getattr(graphDraw, "mapChessboard", None), "nodePairRoutes", {})
    return max((_clock_cycles_for_sequence(graphDraw, path, phase_cycle) for path in routes.values()), default=0.0)


def _pack_phase_block(block):
    packed_rows = []
    for row in block:
        row_byte = 0
        for column_index, phase in enumerate(row):
            row_byte |= (int(phase) & 0x3) << (2 * column_index)
        packed_rows.append(f"{row_byte:02x}")
    return "".join(packed_rows)


def _is_2ddwave_scheme(clock_scheme_name):
    scheme = str(clock_scheme_name).strip().lower().replace(" ", "")
    return scheme in {"2ddwave", "tddwave"}


def _template_phase_for_coord(graphDraw, coord, phase_cycle):
    scheme = getattr(graphDraw, "clock_scheme_name", "")
    if _is_2ddwave_scheme(scheme):
        return (int(coord[0]) + int(coord[1])) % int(phase_cycle)
    return 0


def _write_encoded_phase_mapping_file(
    graphDraw,
    output_dir,
    filename_stem,
    phase_cycle,
    verbose=True,
):
    phase_cycle = int(phase_cycle)
    if phase_cycle not in (3, 4):
        raise ValueError("phase_cycle must be 3 or 4 for encoded IFCN output.")
    block_size = 3 if phase_cycle == 3 else 4

    encoded_filename = os.path.join(output_dir, f"{filename_stem}_encoded.ifcn")
    critical_path, critical_phase_cycle = _estimate_critical_path_metrics(graphDraw, phase_cycle)
    phase_enabled = True
    if hasattr(graphDraw, "mapChessboard") and hasattr(graphDraw.mapChessboard, "isPhaseEnabled"):
        phase_enabled = bool(graphDraw.mapChessboard.isPhaseEnabled())
    clock_scheme_name = str(getattr(graphDraw, "clock_scheme_name", "random phase"))
    raw_unassigned_phase = "none" if phase_enabled and _is_2ddwave_scheme(clock_scheme_name) else "-1"

    with open(encoded_filename, "w") as f:
        f.write(f"#circuit name: {graphDraw.parse.fileName}\n\n")
        algorithm_desc = getattr(
            graphDraw,
            "algorithm_description",
            "graph draw algorithm",
        )
        run_time = getattr(graphDraw, "run_time_sec", None)
        f.write(f"#designed by {algorithm_desc}.\n\n")
        f.write("#gate level placement and routing infomation\n")
        f.write(f"#gates number: {graphDraw.parse.effective_nodes_num}\n")
        f.write(f"#input/output: {graphDraw.parse.InputNodesNum} / {graphDraw.parse.OutputNodesNum}\n")
        f.write(f"#edges number: {graphDraw.parse.effective_edges_num}\n")
        f.write(f"#total layers: {graphDraw.parse.total_layers}\n")
        f.write(f"#layout area: width: {graphDraw.width}, height: {graphDraw.height}, area: {graphDraw.width * graphDraw.height}\n")
        f.write(f"#critical path: {critical_path}\n")
        f.write(f"#clocks: {critical_phase_cycle}\n")
        if run_time is not None:
            f.write(f"#run time: {float(run_time):.6f} s\n")
        f.write("#phase encoding: enabled\n")
        f.write(f"#phase count: {phase_cycle}\n")
        f.write(f"#clock scheme: {clock_scheme_name if phase_enabled else 'disabled'}\n")
        f.write(f"#block size: {block_size}x{block_size}\n")
        f.write("#coordinate origin: normalized top-left (0,0)\n")
        f.write(f"#unassigned phase in raw map: {raw_unassigned_phase}\n")
        f.write("#encoded padding/unassigned compatibility fill: 0\n")
        f.write("#encoding: row-major blocks; each block stores one byte per row; each cell uses 2 bits; column 0 is LSB\n\n")

        f.write("#nodes info \n")
        f.write("### nodeIndex, nodeName, nodeType, nodePosition ###\n")
        for node_id in graphDraw.parse.effective_nodes:
            node_name = graphDraw.parse.getNodeName(node_id)
            node_type = graphDraw.parse.getNodeTypeString(node_id)
            x, y = graphDraw.get_node_coord(node_id)
            f.write(f"{node_id}, {node_name}, {node_type}, ({x},{y});\n")
        f.write("#nodes info \n\n")

        if hasattr(graphDraw, "mapChessboard") and hasattr(graphDraw.mapChessboard, "nodePairRoutes"):
            f.write("#paths info\n")
            f.write("### {node1, node2} : path ###\n")
            for (u, v), path in graphDraw.mapChessboard.nodePairRoutes.items():
                path_str = ",".join([f"({px},{py})" for px, py in path])
                f.write(f"({u},{v}): {path_str};\n")
            f.write("#paths info\n")
        else:
            f.write("#paths info\n### empty ###\n#paths info\n")
        f.write("\n")

        if not phase_enabled:
            f.write("#encoded phase map\n### disabled ###\n#encoded phase map\n")
        else:
            min_x, min_y, max_x, max_y = graphDraw.mapChessboard.findLayoutBoard()
            if max_x < min_x or max_y < min_y:
                f.write("#encoded phase map\n### empty layout ###\n#encoded phase map\n")
            else:
                raw_width = int(max_x - min_x + 1)
                raw_height = int(max_y - min_y + 1)
                padded_width = ((raw_width + block_size - 1) // block_size) * block_size
                padded_height = ((raw_height + block_size - 1) // block_size) * block_size
                blocks_x = padded_width // block_size
                blocks_y = padded_height // block_size

                f.write(f"#absolute bbox: ({min_x},{min_y}) -> ({max_x},{max_y})\n")
                f.write(f"#raw size: width: {raw_width}, height: {raw_height}\n")
                f.write(f"#padded size: width: {padded_width}, height: {padded_height}\n")
                f.write(f"#blocks: columns: {blocks_x}, rows: {blocks_y}\n\n")
                f.write("#encoded phase map\n")
                f.write("### (block_x,block_y): hex ###\n")

                for block_row in range(blocks_y):
                    line_items = []
                    for block_col in range(blocks_x):
                        block = []
                        for local_y in range(block_size):
                            row = []
                            for local_x in range(block_size):
                                norm_x = block_col * block_size + local_x
                                norm_y = block_row * block_size + local_y
                                if norm_x >= raw_width or norm_y >= raw_height:
                                    phase_val = 0
                                else:
                                    abs_x = min_x + norm_x
                                    abs_y = min_y + norm_y
                                    if _is_2ddwave_scheme(clock_scheme_name):
                                        phase_val = _template_phase_for_coord(
                                            graphDraw,
                                            (abs_x, abs_y),
                                            phase_cycle,
                                        )
                                    else:
                                        phase_val = int(
                                            graphDraw.mapChessboard.getPhase((abs_x, abs_y))
                                        )
                                        if phase_val < 0:
                                            phase_val = _template_phase_for_coord(
                                                graphDraw,
                                                (abs_x, abs_y),
                                                phase_cycle,
                                            )
                                if phase_val < 0 or phase_val >= phase_cycle:
                                    raise ValueError(
                                        "phase value out of range for encoded IFCN: "
                                        f"phase={phase_val}, phase_cycle={phase_cycle}, "
                                        f"normalized=({norm_x},{norm_y})"
                                    )
                                row.append(phase_val)
                            block.append(row)

                        encoded = _pack_phase_block(block)
                        line_items.append(f"({block_col},{block_row}):0x{encoded};")
                    f.write(" ".join(line_items) + "\n")
                f.write("#encoded phase map\n")

    _insert_or_update_mapping_metrics(encoded_filename, verbose=verbose)
    if verbose:
        print(f"[IFCN] Encoded mapping file generated -> {encoded_filename}")
    return encoded_filename


def generate_gate_level_mapping_file(
    graphDraw,
    output_dir="./results/GoodiFCN/",
    filename_stem=None,
    phase_cycle=4,
    write_encoded=True,
    verbose=True,
):
    """
    ==========================================================
    生成 <circuit_name>_gate_level_pr.ifcn 文件
    内容包括：
        1️⃣ 电路名（来自 graphDraw.parse.fileName）
        2️⃣ 节点信息（index, name, type, position）
        3️⃣ 路径信息（来自 graphDraw.mapChessboard.nodePairRoutes）
    ==========================================================
    """
    import os

    # ----------------------------------------
    # 1️⃣ 构造输出文件名
    # ----------------------------------------
    circuit_name = os.path.splitext(graphDraw.parse.fileName)[0]
    output_dir = output_dir or "./results/GoodiFCN/"
    os.makedirs(output_dir, exist_ok=True)
    stem = filename_stem if filename_stem is not None else f"{circuit_name}_gate_level_pr"
    filename = os.path.join(output_dir, f"{stem}.ifcn")
    phase_cycle = int(getattr(graphDraw, "phase_cycle", phase_cycle))
    critical_path, critical_phase_cycle = _estimate_critical_path_metrics(graphDraw, phase_cycle)

    # ----------------------------------------
    # 2️⃣ 打开文件写入
    # ----------------------------------------
    with open(filename, 'w') as f:
        # ----------------------------------------
        # 写电路名
        # ----------------------------------------
        f.write(f"#circuit name: {graphDraw.parse.fileName}\n\n")

        algorithm_desc = getattr(
            graphDraw,
            "algorithm_description",
            "graph draw algorithm",
        )
        run_time = getattr(graphDraw, "run_time_sec", None)
        f.write(f"#designed by {algorithm_desc}.\n\n")
        f.write(f"#gate level placement and routing infomation\n")
        f.write(f"#gates number: {graphDraw.parse.effective_nodes_num}\n")
        f.write(f"#input/output: {graphDraw.parse.InputNodesNum} / {graphDraw.parse.OutputNodesNum}\n")
        f.write(f"#edges number: {graphDraw.parse.effective_edges_num}\n")
        f.write(f"#total layers: {graphDraw.parse.total_layers}\n")
        f.write(f"#layout area: width: {graphDraw.width}, height: {graphDraw.height}, area: {graphDraw.width * graphDraw.height}\n")
        f.write(f"#critical path: {critical_path}\n")
        f.write(f"#clocks: {critical_phase_cycle}\n")
        if run_time is not None:
            f.write(f"#run time: {float(run_time):.6f} s\n")
        f.write(f"#phase count: {phase_cycle}\n")
        phase_enabled = True
        if hasattr(graphDraw, "mapChessboard") and hasattr(graphDraw.mapChessboard, "isPhaseEnabled"):
            phase_enabled = bool(graphDraw.mapChessboard.isPhaseEnabled())

        template_ok = bool(
            getattr(
                graphDraw,
                "clock_template_ok",
                getattr(graphDraw, "phase_assignment_ok", False),
            )
        )
        template_conflicts = int(
            getattr(
                graphDraw,
                "clock_template_conflict_count",
                getattr(graphDraw, "phase_conflict_count", 0),
            )
        )
        clock_scheme_name = str(getattr(graphDraw, "clock_scheme_name", "random phase"))
        if phase_enabled:
            f.write(f"#clock scheme: {clock_scheme_name}\n")
            f.write(f"#clock scheme consistency: {'success' if template_ok else 'failed'}\n")
            f.write(f"#clock scheme conflicts: {template_conflicts}\n")
            f.write(f"#random phase scheme consistency: {'success' if template_ok else 'failed'}\n")
            f.write(f"#random phase scheme conflicts: {template_conflicts}\n")
        else:
            f.write("#clock scheme: disabled\n")
            f.write("#clock scheme consistency: disabled\n")
            f.write("#clock scheme conflicts: 0\n")
            f.write("#random phase scheme consistency: disabled\n")
            f.write("#random phase scheme conflicts: 0\n")
        f.write("\n")
        # ----------------------------------------
        # 节点信息
        # ----------------------------------------
        f.write("#nodes info \n")
        f.write("### nodeIndex, nodeName, nodeType, nodePosition ###\n")

        for node_id in graphDraw.parse.effective_nodes:
            node_name = graphDraw.parse.getNodeName(node_id)
            node_type = graphDraw.parse.getNodeTypeString(node_id)
            # node_type = node_type_enum.split('.')[-1] if '.' in node_type_enum else node_type_enum

            x, y = graphDraw.get_node_coord(node_id)

            f.write(f"{node_id}, {node_name}, {node_type}, ({x},{y});\n")

        f.write("#nodes info \n\n")

        # ----------------------------------------
        # 路径信息（来自 self.mapChessboard.nodePairRoutes）
        # ----------------------------------------
        if hasattr(graphDraw, "mapChessboard") and hasattr(graphDraw.mapChessboard, "nodePairRoutes"):
            f.write("#paths info\n")
            f.write("### {node1, node2} : path ###\n")

            for (u, v), path in graphDraw.mapChessboard.nodePairRoutes.items():
                # path 应该是 [(x1,y1), (x2,y2), ...]
                path_str = ",".join([f"({px},{py})" for px, py in path])
                f.write(f"({u},{v}): {path_str};\n")

            f.write("#paths info\n")
        else:
            f.write("#paths info\n### empty ###\n#paths info\n")

                # ----------------------------------------
        # 输出已实际铺设区域的相位；未使用格保持 -1。
        # ----------------------------------------
        f.write("#phase map\n")
        f.write("### (x,y) : phase ###\n")

        if not phase_enabled:
            f.write("### disabled ###\n")
            f.write("#phase map\n")
        else:
            # 获取实际布线边界
            minX, minY, maxX, maxY = graphDraw.mapChessboard.findLayoutBoard()
            if maxX < minX or maxY < minY:
                f.write("### empty layout ###\n#phase map\n")
            else:
                for y in range(minY, maxY + 1):
                    line_items = []
                    for x in range(minX, maxX + 1):
                        if _is_2ddwave_scheme(clock_scheme_name):
                            phase_val = _template_phase_for_coord(
                                graphDraw,
                                (x, y),
                                phase_cycle,
                            )
                        else:
                            phase_val = graphDraw.mapChessboard.getPhase((x, y))
                            if phase_val < 0:
                                phase_val = _template_phase_for_coord(
                                    graphDraw,
                                    (x, y),
                                    phase_cycle,
                                )
                        line_items.append(f"({x},{y}):{phase_val};")
                    f.write(" ".join(line_items) + "\n")
                f.write("#phase map\n")

    _insert_or_update_mapping_metrics(filename, verbose=verbose)
    if verbose:
        print(f"[IFCN] Gate-level mapping file generated -> {filename}")
    if write_encoded:
        encoded_phase_cycle = int(getattr(graphDraw, "phase_cycle", phase_cycle))
        if encoded_phase_cycle in (3, 4):
            _write_encoded_phase_mapping_file(
                graphDraw,
                output_dir,
                stem,
                encoded_phase_cycle,
                verbose=verbose,
            )
        else:
            if verbose:
                print(
                    "[IFCN] Encoded mapping skipped: "
                    f"phase_cycle={encoded_phase_cycle} is not supported."
                )
    return filename
