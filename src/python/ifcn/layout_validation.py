"""Independent artifact validation shared by the circuit layout adapters.

These checks establish routed geometry and globally consistent clock epochs.
Physical gate behavior still needs an independent waveform check.
"""
from collections import defaultdict
import graphlib


def validate_clock_epochs(nodes, routes, phases, *, max_same_phase=4):
    """Lift modulo-four phases to absolute, synchronous tile epochs.

    Every route contributes exact 0/1 epoch differences. A shared tile has one
    epoch, and primary inputs launch within the first clock cycle. This catches
    reconvergent paths that differ by a whole cycle despite legal local phases.
    """
    adjacency = defaultdict(list)
    errors = []
    missing = [(n['x'], n['y']) for n in nodes.values()
               if phases.get((n['x'], n['y'])) not in (1, 2, 3, 4)]
    if missing:
        return {'passed': False, 'errors': [f'Circuit nodes have missing/invalid clock phases: {missing}'],
                'node_epochs': {}, 'max_same_phase_tiles': max_same_phase}
    for edge, path in routes.items():
        if not path:
            continue
        advance = 0
        run = 1
        for left, right in zip(path, path[1:]):
            step = (phases[right] - phases[left]) % 4
            advance += step
            run = run + 1 if step == 0 else 1
            if run > max_same_phase:
                errors.append(f'Route {edge} exceeds {max_same_phase} consecutive same-phase tiles')
                break
            adjacency[left].append((right, step))
            adjacency[right].append((left, -step))
        if advance < 1:
            errors.append(f'Route {edge} has no clock advance between distinct circuit nodes')
        if len(path) >= 2 and (phases[path[-1]] - phases[path[-2]]) % 4 != 1:
            errors.append(f'Route {edge} enters its gate without a clock advance; the fanin wire must hold while the gate switches')
        if len(path) >= 2 and not nodes[edge[0]].get('is_input') and (phases[path[1]] - phases[path[0]]) % 4 != 1:
            errors.append(f'Route {edge} leaves its gate without a clock advance; the gate must hold while its output wire switches')
    epochs = {}
    inputs = [(n['x'], n['y']) for n in nodes.values() if n.get('is_input')]
    starts = inputs + sorted(adjacency)
    for start in starts:
        if start in epochs:
            continue
        epochs[start] = phases[start] - 1
        queue = [start]
        for left in queue:
            for right, step in adjacency[left]:
                expected = epochs[left] + step
                if right not in epochs:
                    epochs[right] = expected
                    queue.append(right)
                elif epochs[right] != expected:
                    errors.append(f'Clock epoch conflict at {right}: {epochs[right]} versus {expected}; reconvergent signals are not synchronized')
    for point in inputs:
        if epochs[point] != phases[point] - 1:
            errors.append(f'Primary input at {point} is shifted into a different launch cycle')
    return {'passed': not errors, 'errors': list(dict.fromkeys(errors)),
            'node_epochs': {node: epochs.get((n['x'], n['y'])) for node, n in nodes.items()},
            'max_same_phase_tiles': max_same_phase}


def validate_candidate(candidate, *, source_crossings=False):
    errors = []
    node_list, route_list = candidate.get('nodes', []), candidate.get('routes', [])
    nodes = {int(n['id']): n for n in node_list}
    positions = {node: (int(n['x']), int(n['y'])) for node, n in nodes.items()}
    edges_list = [tuple(map(int, edge)) for edge in candidate.get('edges', [])]
    edges = set(edges_list)
    routes = {(int(r['source']), int(r['target'])): [tuple(map(int, p)) for p in r['path']] for r in route_list}
    cells = {(int(c['x']), int(c['y'])): int(c['phase']) for c in candidate.get('cells', [])}
    if not nodes or not edges:
        errors.append('Empty circuit or routed topology')
    if len(nodes) != len(node_list) or len(edges) != len(edges_list) or len(routes) != len(route_list):
        errors.append('Duplicate node, edge, or route identity')
    if set(routes) != edges:
        errors.append('Routes do not exactly cover the effective DAG edges')
    drawn = {int(n) for layer in candidate.get('layers', []) for n in layer}
    if drawn != set(nodes):
        errors.append('Placement does not cover exactly the drawable DAG nodes')
    if len(set(positions.values())) != len(nodes):
        errors.append('Multiple nodes occupy the same coordinate')
    if any(min(p) < 0 for p in positions.values()):
        errors.append('Negative node coordinate')
    if not any(n.get('is_input') for n in nodes.values()) or not any(n.get('is_output') for n in nodes.values()):
        errors.append('Missing primary inputs or outputs')
    dependencies = {node: set() for node in nodes}
    for src, dst in edges:
        if src not in nodes or dst not in nodes:
            errors.append('An effective edge references a missing node')
        else:
            dependencies[dst].add(src)
    try:
        topological_order = list(graphlib.TopologicalSorter(dependencies).static_order())
    except graphlib.CycleError:
        errors.append('The combinational topology contains a directed cycle')
        topological_order = []
    sink_ports, sink_owners = defaultdict(set), defaultdict(list)
    coordinate_nodes = {p: node for node, p in positions.items()}
    crossing_uses = defaultdict(lambda: defaultdict(set))
    max_phase_run = 0
    for edge, path in routes.items():
        if len(path) < 2:
            errors.append(f'Route {edge} has fewer than two points')
            continue
        if path[0] != positions.get(edge[0]) or path[-1] != positions.get(edge[1]):
            errors.append(f'Route {edge} endpoints disagree with node positions')
        if len(set(path)) != len(path):
            errors.append(f'Route {edge} repeats a coordinate')
        if path[-2] in sink_ports[edge[1]]:
            errors.append(f'Multiple fanins share one physical input port at sink {edge[1]}')
        sink_ports[edge[1]].add(path[-2])
        sink_owners[path[-2]].append(edge)
        previous_phase, phase_run = None, 0
        for index, point in enumerate(path):
            phase = cells.get(point)
            if phase not in (1, 2, 3, 4):
                errors.append(f'Route {edge} has an absent or invalid clock phase at {point}')
            phase_run = phase_run + 1 if phase == previous_phase else 1
            max_phase_run = max(max_phase_run, phase_run)
            previous_phase = phase
            if 0 < index < len(path) - 1 and point in coordinate_nodes:
                errors.append(f'Route {edge} passes through another gate at {point}')
            if index:
                prev = path[index - 1]
                if abs(point[0] - prev[0]) + abs(point[1] - prev[1]) != 1:
                    errors.append(f'Route {edge} is not four-connected')
                if phase in (1,2,3,4) and cells.get(prev) in (1,2,3,4) and (phase-cells[prev])%4 not in (0,1):
                    errors.append(f'Route {edge} contains a backward clock step')
            if 0 < index < len(path)-1:
                before, after = path[index-1], path[index+1]
                orientation = 'H' if before[1] == point[1] == after[1] else ('V' if before[0] == point[0] == after[0] else 'B')
                crossing_uses[point][edge[0]].add(orientation)
    for edge, path in routes.items():
        if len(path) <= 2:
            continue
        # Same-source fanout trunks are electrically one net. Distinct nets
        # may cross at a shared coarse port neighbor only as straight H/V
        # segments; the existing device mapper then validates realization.
        for owner in sink_owners[path[1]]:
            if owner == edge or owner[0] == edge[0]:
                continue
            uses = crossing_uses[path[1]]
            first, second = uses.get(edge[0], set()), uses.get(owner[0], set())
            if {frozenset(first), frozenset(second)} != {frozenset('H'), frozenset('V')}:
                errors.append(f'Route {edge} overlaps another source at a gate port without an H/V crossing')
    if source_crossings:
        crossed_pairs = set()
        for point, sources in crossing_uses.items():
            if len(sources) <= 1:
                continue
            values = list(sources.values())
            if len(sources) != 2 or {frozenset(v) for v in values} != {frozenset('H'), frozenset('V')}:
                errors.append(f'Different source trees overlap without a straight H/V crossing at {point}')
            pair = tuple(sorted(sources))
            if pair in crossed_pairs:
                errors.append(f'The same source-tree pair crosses more than once: {pair}')
            crossed_pairs.add(pair)
    check_names = ['dag_acyclic', 'exact_edge_coverage', 'exact_node_coverage', 'unique_node_positions',
                   'route_endpoints', 'simple_four_connected_paths', 'no_route_through_gate',
                   'distinct_fanin_ports', 'port_sharing_geometry', 'clock_range_and_forward_steps']
    if source_crossings:
        check_names.append('source_aware_crossings')
    timing = {'passed': False, 'errors': ['Geometry must pass before clock-epoch validation']}
    if not errors:
        timing = validate_clock_epochs(nodes, routes, cells)
        errors.extend(timing['errors'])
    check_names += ['global_clock_epoch_consistency', 'primary_input_launch_cycle',
                    'gate_to_gate_clock_advance', 'gate_fanin_clock_boundary',
                    'gate_fanout_clock_boundary',
                    'bounded_same_phase_runs']
    return {'passed': not errors, 'checks': check_names, 'errors': errors,
            'max_same_phase_run': max_phase_run, 'topological_order': topological_order,
            'clock_timing': timing,
            'scope': 'Geometric routes, physical gate ports, and globally synchronized clock epochs; physical device behavior requires waveform validation.'}


def dag_from_candidate(candidate, validation):
    nodes = [{k: n[k] for k in ('id','name','type','is_input','is_output')} for n in candidate['nodes']]
    return {'schema':'ifcn.dag.v1', 'nodes':nodes, 'edges':candidate['edges'],
            'layers':candidate['layers'], 'topological_order':validation['topological_order'],
            'input_count':sum(bool(n['is_input']) for n in nodes),
            'output_count':sum(bool(n['is_output']) for n in nodes)}


def export_native_ifcn(candidate, output, algorithm):
    """Serialize the actual C++ routing using the existing combinational IFCN format.

    Native phases are 1..4; the shared IFCN/QCA mapping reader expects 0..3.
    Coordinates and phases are translated together without rotating or rerouting.
    """
    used = {tuple(p) for route in candidate['routes'] for p in route['path']}
    used.update((node['x'],node['y']) for node in candidate['nodes'])
    min_x, min_y = min(p[0] for p in used), min(p[1] for p in used)
    max_x, max_y = max(p[0] for p in used), max(p[1] for p in used)
    width, height = max_x-min_x+1, max_y-min_y+1
    def coord(p):
        return f'({p[0]-min_x},{p[1]-min_y})'
    lines = [f'#circuit name: {output.stem}', f'#algorithm: {algorithm}', '#mapping mode: combinational',
             '#primary output nodes: ' + ','.join(str(n['id']) for n in candidate['nodes'] if n.get('is_output')),
             '#phase count: 4', '#clock scheme: irregular', '#clock scheme consistency: success',
             f'#layout area: width: {width}, height: {height}, area: {width*height}',
             '#nodes info', '### nodeIndex, nodeName, nodeType, nodePosition ###']
    for node in candidate['nodes']:
        lines.append(f"{node['id']}, {node['name']}, {node['type']}, {coord((node['x'],node['y']))};")
    lines += ['#nodes info', '#paths info', '### {node1, node2} : path ###']
    for route in candidate['routes']:
        lines.append(f"({route['source']},{route['target']}): "+','.join(coord(p) for p in route['path'])+';')
    lines += ['#paths info', '#phase map', '### (x,y) : phase ###']
    phases = {(c['x'],c['y']):c['phase'] for c in candidate['cells']}
    for point in sorted(used, key=lambda p:(p[1],p[0])):
        lines.append(f'{coord(point)}:{phases[point]-1};')
    lines += ['#phase map', '']
    output.write_text('\n'.join(lines))
    return width,height
