#!/usr/bin/env python3
"""Regenerate regular-clock TOY layouts with the production heuristic/fixed backends.

Build heuristic_layout_driver and ifcn_energy_analysis first. The fixed backend
also requires IFCN_LAYOUT_BINDINGS_DIR (or --bindings-dir) and its ordinary
Python dependencies. Outputs/logs stay under --output-dir; only accepted IFCN
files are copied to its validated/ subtree. This is structural/source validation,
not physical-waveform certification. GA randomness has no deterministic seed API.
"""
from __future__ import annotations

import argparse
from collections import Counter
from concurrent.futures import ThreadPoolExecutor, as_completed
import json
import os
from pathlib import Path
import shutil
import sys
import time
from types import SimpleNamespace

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'src/python'))
sys.path.insert(0, str(Path(__file__).resolve().parent))
from ifcn.frontend import normalize_verilog
from ifcn.layout_validation import dag_from_candidate, export_native_ifcn, validate_candidate
from ifcn.verify_logic import source_analysis, verify_bytes
from benchmark_irregular_layout import command_result, qca_interface, read_json, sha256, write_json

PHASES = {
    'USE': ((1, 2, 3, 4), (4, 3, 2, 1), (3, 4, 1, 2), (2, 1, 4, 3)),
    'RES': ((4, 1, 2, 3), (1, 2, 1, 4), (2, 3, 4, 1), (1, 4, 3, 2)),
    '2DDWave': ((1, 2, 3, 4), (2, 3, 4, 1), (3, 4, 1, 2), (4, 1, 2, 3)),
}
SCOPE = ('Original-source exhaustive truth (up to 11 inputs), routed geometry, global clock, '
         'unchanged fixed-phase template, native device mapping DRC, exact QCA terminal labels; '
         'physical waveform behavior is not certified.')


def complete_fixed_clock_template(candidate, scheme):
    """Preserve routed phases and fill only the unoccupied regular-clock tiles."""
    used = {(node['x'], node['y']) for node in candidate['nodes']}
    used.update(tuple(point) for route in candidate['routes'] for point in route['path'])
    if not used:
        raise ValueError('Cannot export an empty fixed-clock layout')
    phases = {}
    for cell in candidate['cells']:
        point = cell['x'], cell['y']
        expected = PHASES[scheme][point[1] % 4][point[0] % 4]
        if cell['phase'] != expected:
            raise ValueError(f'Stored phase at {point} disagrees with the {scheme} clock template')
        phases[point] = cell['phase']
    if not used <= phases.keys():
        raise ValueError('Fixed-clock candidate is missing an occupied tile phase')
    for y in range(min(y for x, y in used), max(y for x, y in used) + 1):
        for x in range(min(x for x, y in used), max(x for x, y in used) + 1):
            phases.setdefault((x, y), PHASES[scheme][y % 4][x % 4])
    return {**candidate, 'phase_map_scope': 'board',
            'cells': [{'x': x, 'y': y, 'phase': phase}
                      for (x, y), phase in sorted(phases.items())]}


def fixed_worker(args):
    # Import the classic Python algorithm only in its isolated subprocess.
    from ifcn.backend import normal_candidate
    from test_normal_graph_draw import run_layout_trial
    source, directory = map(Path, args.fixed_worker)
    options = SimpleNamespace(seed_retries=0, max_expansion_rounds=96,
                              route_expansion_timeout_sec=args.route_budget,
                              template_expansion_rounds=24, verbose=False, route_repair_iters=3,
                              phase_repair_iters=2, global_place_iters=5, compact_iters=64)
    trial = run_layout_trial(options, str(source), str(directory), '', args.seed, 0)
    draw = trial['draw']
    ports, violations, _ = draw.validate_gate_port_directions()
    candidate = normal_candidate(draw)
    candidate.update(schema='ifcn.regular_examples_candidate.v1', scheme='2DDWave',
                     algorithm='normal_2ddwave',
                     routed=trial['failed_count'] == 0 and bool(trial['template_ok']) and bool(ports),
                     port_validation=bool(ports), port_violations=[str(v) for v in violations],
                     failed_edge_count=trial['failed_count'], clock_template_ok=bool(trial['template_ok']),
                     run_time_s=trial['run_time'], parameters=vars(options) | {'seed': args.seed})
    write_json(directory / 'candidate.json', candidate)
    return 0 if candidate['routed'] else 1


def environment(args):
    env = dict(os.environ, MPLBACKEND='Agg', PYTHONDONTWRITEBYTECODE='1',
               IFCN_LAYOUT_PARSE_MODE='compact', OMP_NUM_THREADS='1', OPENBLAS_NUM_THREADS='1',
               MKL_NUM_THREADS='1', NUMEXPR_NUM_THREADS='1', IFCN_GRAPHVIZ_TIMEOUT='30',
               IFCN_SIFT_TIMEOUT='10', IFCN_SIFT_EVALUATIONS='100000')
    if args.bindings_dir:
        env['IFCN_LAYOUT_BINDINGS_DIR'] = str(args.bindings_dir)
    for name in ('CONTRACTION', 'RECURSIVE_CONTRACTION', 'EMPTY_LINE_CONTRACTION', 'LAYER_MERGE_CONTRACTION'):
        env['IFCN_' + name + '_TIMEOUT'] = '15'
        env['IFCN_' + name + '_EVALUATIONS'] = '64'
    return env


def run_case(item, family, scheme, args, binaries, env):
    directory = args.output_dir / 'cases' / family / scheme / item['case']
    directory.mkdir(parents=True)
    row = {**item, 'family': family, 'scheme': scheme, 'status': 'running',
           'eligible_for_examples': False, 'physical_waveform_status': 'not_run',
           'validation_scope': SCOPE, 'commands': []}
    start = time.monotonic()
    try:
        raw = (ROOT / item['source']).read_bytes()
        (directory / 'source.v').write_bytes(raw)
        if sha256(directory / 'source.v') != item['source_sha256']:
            raise ValueError('Source changed after inventory creation')
        normalized, metadata = normalize_verilog(raw.decode('utf-8-sig'))
        (directory / 'normalized.v').write_text(normalized)
        row.update(normalized_sha256=sha256(directory / 'normalized.v'), frontend=metadata)
        if family == 'regular_heuristic':
            command = [binaries['heuristic_layout_driver'], scheme,
                       directory / 'normalized.v', directory / 'candidate.json',
                       '--generations', args.ga_generations, '--population', args.ga_population,
                       '--crossover', args.ga_crossover, '--mutation', args.ga_mutation]
            if args.ga_width:
                command += ['--grid-width', args.ga_width]
            if args.ga_height:
                command += ['--grid-height', args.ga_height]
            if args.ga_stop_after_first:
                command += ['--stop-after-first']
            timeout = args.ga_timeout
        else:
            command = [args.python, Path(__file__).resolve(), '--fixed-worker',
                       directory / 'normalized.v', directory, '--seed', args.seed,
                       '--route-budget', args.route_budget]
            timeout = args.fixed_timeout
        execution = command_result(command, directory, 'pnr', timeout, env)
        row['commands'].append(execution)
        if execution['timed_out'] or execution['exit_code'] != 0:
            row['status'] = ('timeout' if execution['timed_out'] else
                             'search_failed' if execution['exit_code'] == 1 else 'process_error')
            raise RuntimeError(f"P&R did not complete successfully: {execution['exit_code']}")
        candidate = read_json(directory / 'candidate.json')
        if candidate.get('routed') is not True:
            raise ValueError('Backend did not report a routed candidate')
        validation = validate_candidate(candidate, source_crossings=False)
        write_json(directory / 'drc.json', validation)
        if not validation['passed']:
            raise ValueError(f"Global validation failed: {validation['errors']}")
        if not all(cell['phase'] == PHASES[scheme][cell['y'] % 4][cell['x'] % 4]
                   for cell in candidate['cells']):
            raise ValueError('Candidate changed the selected fixed-clock template')
        dag = dag_from_candidate(candidate, validation)
        write_json(directory / 'routed_dag.json', dag)
        logic = verify_bytes(raw, json.dumps(dag).encode(), max_inputs=11)
        write_json(directory / 'logic.json', logic)
        if logic['status'] != 'equivalent':
            raise ValueError(f'Source-DAG truth mismatch: {logic}')
        export_candidate = complete_fixed_clock_template(candidate, scheme)
        width, height = export_native_ifcn(export_candidate, directory / 'layout.ifcn', family + '_' + scheme)
        used = {(node['x'], node['y']) for node in candidate['nodes']}
        used.update(tuple(point) for route in candidate['routes'] for point in route['path'])
        origin = min(x for x, y in used), min(y for x, y in used)
        path = directory / 'layout.ifcn'
        text = path.read_text().replace('#clock scheme: irregular', '#clock scheme: ' + scheme)
        text += ('#fixed clock template original origin: ' + str(origin) + '\n'
                 '#validation: source truth, global clock and native mapping DRC; physical waveform not characterized\n')
        path.write_text(text)
        execution = command_result([binaries['ifcn_energy_analysis'], path,
                                    directory / 'mapped', '--qca-only'],
                                   directory, 'mapping', args.mapping_timeout, env)
        row['commands'].append(execution)
        if execution['timed_out'] or execution['exit_code'] != 0:
            raise ValueError('Native physical mapper failed or timed out')
        qca = qca_interface(directory / 'mapped_energy_input.qca', logic)
        source = source_analysis(raw, max_inputs=11)
        if set(qca['terminals']['OUTPUT']) != set(source['output_ports']):
            raise ValueError('Native QCA changed a primary-output label')
        export = args.output_dir / 'validated' / family
        if family == 'regular_heuristic':
            export /= scheme
        export /= Path(item['source']).relative_to('tests/benchmarks_f').with_suffix('.ifcn')
        export.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, export)
        row.update(status='validated', eligible_for_examples=True, width=width, height=height,
                   area_tiles=width * height, **qca, source_vectors_checked=logic['vectors_checked'],
                   fixed_clock_template_unchanged=True, candidate_sha256=sha256(directory / 'candidate.json'),
                   ifcn_sha256=sha256(path), validated_ifcn=str(export),
                   search_parameters={key: candidate.get(key) for key in
                                      ('generations', 'population', 'parameters', 'grid_width', 'grid_height')})
    except Exception as error:
        if row['status'] == 'running':
            row['status'] = 'validation_failed'
        row['error'] = f'{type(error).__name__}: {error}'
    row['elapsed_s'] = time.monotonic() - start
    write_json(directory / 'result.json', row)
    print(json.dumps({key: row.get(key) for key in ('family', 'scheme', 'case', 'status', 'area_tiles', 'qca_cells')}), flush=True)
    return row


def summary(args, rows, planned):
    write_json(args.output_dir / 'summary.json', sorted(rows, key=lambda r: (r['family'], r['scheme'], r['case'])))
    groups = {}
    for row in rows:
        key = row['family'] + '/' + row['scheme']
        groups.setdefault(key, Counter())[row['status']] += 1
    write_json(args.output_dir / 'totals.json', {'completed': len(rows), 'planned': planned,
               'groups': {key: dict(counts) for key, counts in groups.items()}})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build-release')
    parser.add_argument('--output-dir', type=Path)
    parser.add_argument('--python', default=sys.executable, help='Python interpreter with classic layout dependencies')
    parser.add_argument('--bindings-dir', type=Path)
    parser.add_argument('--family', choices=('all', 'regular_heuristic', 'regular_2ddwave'), default='all')
    parser.add_argument('--scheme', choices=tuple(PHASES), help='Limit heuristic schemes; fixed always uses 2DDWave')
    parser.add_argument('--case', action='append', help='Limit to a case ID, e.g. TOY__xor2_demo; repeatable')
    parser.add_argument('--jobs', type=int, default=4)
    parser.add_argument('--ga-timeout', type=float, default=60)
    parser.add_argument('--ga-generations', type=int, default=32)
    parser.add_argument('--ga-population', type=int, default=64)
    parser.add_argument('--ga-width', type=int, help='Override heuristic board width')
    parser.add_argument('--ga-height', type=int, help='Override heuristic board height')
    parser.add_argument('--ga-crossover', type=float, default=.9)
    parser.add_argument('--ga-mutation', type=float, default=.5)
    parser.add_argument('--ga-stop-after-first', action='store_true', help='Stop after the first production-GA legal incumbent')
    parser.add_argument('--fixed-timeout', type=float, default=180)
    parser.add_argument('--route-budget', type=float, default=60)
    parser.add_argument('--mapping-timeout', type=float, default=30)
    parser.add_argument('--seed', type=int, default=1, help='Fixed backend seed; production GA has no seed API')
    parser.add_argument('--fixed-worker', nargs=2, metavar=('SOURCE', 'DIRECTORY'), help=argparse.SUPPRESS)
    args = parser.parse_args()
    if args.fixed_worker:
        return fixed_worker(args)
    if args.output_dir is None:
        parser.error('--output-dir is required')
    if args.jobs < 1 or min(args.ga_timeout, args.fixed_timeout, args.route_budget, args.mapping_timeout) <= 0:
        parser.error('Job count and time budgets must be positive')
    if args.ga_generations < 1 or args.ga_population < 2 or any(
            value is not None and value < 1 for value in (args.ga_width, args.ga_height)):
        parser.error('GA generations/grid sizes must be positive, and population must be at least two')
    if not (0 <= args.ga_crossover <= 1 and 0 <= args.ga_mutation <= 1):
        parser.error('GA crossover and mutation rates must be in [0,1]')
    if args.fixed_timeout <= args.route_budget:
        parser.error('--fixed-timeout must allow overhead beyond --route-budget')
    args.output_dir = args.output_dir.resolve()
    args.build_dir = args.build_dir.resolve()
    if args.bindings_dir:
        args.bindings_dir = args.bindings_dir.resolve()
    if args.output_dir.exists() and any(args.output_dir.iterdir()):
        parser.error('Output directory must be empty; previous evidence is never overwritten')
    args.output_dir.mkdir(parents=True, exist_ok=True)
    binaries = {}
    names = ['ifcn_energy_analysis']
    if args.family != 'regular_2ddwave':
        names += ['heuristic_layout_driver']
    for name in names:
        choices = [args.build_dir / name, args.build_dir / 'tests' / name]
        binary = next((p for p in choices if p.is_file() and os.access(p, os.X_OK)), None)
        if binary is None:
            parser.error(f'Missing executable target {name} in {args.build_dir}')
        # Freeze native binaries so an unrelated rebuild cannot change a run.
        destination = args.output_dir / 'snapshot' / name
        destination.parent.mkdir(exist_ok=True)
        shutil.copy2(binary, destination)
        binaries[name] = destination
    items = []
    for path in sorted((ROOT / 'tests/benchmarks_f/TOY').rglob('*.v')):
        case = 'TOY__' + '__'.join(path.relative_to(ROOT / 'tests/benchmarks_f/TOY').with_suffix('').parts)
        if args.case and case not in args.case:
            continue
        items.append({'case': case, 'source': str(path.relative_to(ROOT)), 'source_sha256': sha256(path)})
    if not items or (args.case and set(args.case) != {item['case'] for item in items}):
        parser.error('No input cases or an unknown --case ID')
    env = environment(args)
    tasks = []
    for item in items:
        if args.family != 'regular_2ddwave':
            tasks.extend((item, 'regular_heuristic', scheme) for scheme in ([args.scheme] if args.scheme else PHASES))
        if args.family != 'regular_heuristic':
            tasks.append((item, 'regular_2ddwave', '2DDWave'))
    files = [Path(__file__).resolve(), ROOT / 'tests/HeuristicLayoutDriver.cpp']
    files += sorted((ROOT / 'src/python/ifcn').glob('*.py'))
    if args.family != 'regular_heuristic':
        files += sorted((ROOT / 'include/layout_backend/src/algorithm').rglob('*.py'))
        binding_root = args.bindings_dir or Path(env.get('IFCN_LAYOUT_BINDINGS_DIR', args.build_dir / 'python/lib'))
        files += sorted(binding_root.glob('*.so'))
    before = {str(path): sha256(path) for path in files}
    manifest = {'schema': 'ifcn.regular_benchmark.v1', 'jobs': args.jobs, 'runs': len(tasks),
                'settings': {key: str(value) if isinstance(value, Path) else value for key, value in vars(args).items()},
                'native_binary_sha256': {name: sha256(path) for name, path in binaries.items()},
                'source_tool_sha256_before': before, 'validation_scope': SCOPE,
                'ga_randomness': 'Production random_device; no deterministic seed API',
                'environment': {key: value for key, value in env.items() if key.startswith(('IFCN_', 'OMP_', 'OPENBLAS_', 'MKL_', 'NUMEXPR_'))}}
    write_json(args.output_dir / 'inventory.json', items)
    write_json(args.output_dir / 'manifest.json', manifest)
    rows = []
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for future in as_completed([pool.submit(run_case, *task, args, binaries, env) for task in tasks]):
            rows.append(future.result())
            summary(args, rows, len(tasks))
    after = {str(path): sha256(path) for path in files}
    manifest['source_tool_sha256_after'] = after
    manifest['source_tools_unchanged'] = before == after
    write_json(args.output_dir / 'manifest.json', manifest)
    if before != after:
        raise RuntimeError('Source tools changed during the benchmark; discard this run')
    return 0 if all(row['eligible_for_examples'] for row in rows) else 1


if __name__ == '__main__':
    raise SystemExit(main())
