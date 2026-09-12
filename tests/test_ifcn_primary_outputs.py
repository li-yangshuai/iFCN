#!/usr/bin/env python3
"""Keep a primary output observable when it also drives a downstream gate."""
from __future__ import annotations
import argparse
import itertools
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import xml.etree.ElementTree as ET

SOURCE = b'''module observe(a, b, x, y);
input a, b;
output x, y;
wire n;
assign x = a | b;
assign n = ~x;
assign y = n;
endmodule
'''
LAYOUT = '''#circuit name: observe
#mapping mode: combinational
#phase count: 4
{header}
#nodes info
0, a, input, (0,0);
1, b, input, (2,0);
2, x, or, (1,1);
3, n, not, (1,3);
4, y, output, (1,5);
#nodes info
#paths info
(0,2): (0,0),(0,1),(1,1);
(1,2): (2,0),(2,1),(1,1);
(2,3): (1,1),(1,2),(1,3);
(3,4): (1,3),(1,4),(1,5);
#paths info
#phase map
(0,0): 0;
(2,0): 0;
(0,1): 0;
(2,1): 0;
(1,1): 1;
(1,2): 2;
(1,3): 3;
(1,4): 0;
(1,5): 1;
#phase map
'''

TAP_SOURCE = b"""module observe_tap(a,b,tap,y);
input a,b;
output tap,y;
assign tap=a;
assign y=tap&b;
endmodule
"""
TAP_LAYOUT = """#circuit name: observe_tap
#mapping mode: combinational
#primary output nodes: 2,3
#nodes info
0, a, input, (0,0);
1, b, input, (2,0);
2, tap, output, (0,2);
3, y, and, (2,3);
#nodes info
#paths info
(0,2): (0,0),(0,1),(0,2);
(2,3): (0,2),(0,3),(1,3),(2,3);
(1,3): (2,0),(2,1),(2,2),(2,3);
#paths info
#phase map
(0,0): 0;
(0,1): 0;
(0,2): 1;
(0,3): 2;
(1,3): 2;
(2,3): 3;
(2,2): 2;
(2,1): 1;
(2,0): 0;
#phase map
"""

ROOT = Path(__file__).resolve().parents[1]
MAJORITY_CASES = (
    ('majority_leaf', ROOT/'tests/benchmarks_f/TOY/paper_2ddwave_crossing_demo.v', {'carry'}, 28),
    ('majority_parity', ROOT/'tests/benchmarks_f/TOY/1bitAdderMaj.v', {'M3'}, 227),
)


def run(command, directory, name, *, env=None):
    result = subprocess.run(list(map(str, command)), cwd=directory, env=env,
                            capture_output=True, text=True, timeout=30)
    (directory / f'{name}.log').write_text(result.stdout + result.stderr)
    return result


def cells(path):
    return [block.split('[#TYPE:QCADCell]', 1)[0]
            for block in path.read_text().split('[TYPE:QCADCell]')[1:]]


def outputs(blocks):
    return {re.search(r'^psz=(.*)$', block, re.M)[1]
            for block in blocks if 'cell_function=QCAD_CELL_OUTPUT\n' in block}


def topology(blocks):
    # Output annotations are passive. No physical cell, dot, polarization,
    # layer or clock field may change when a signal becomes observable.
    return [re.sub(r'^clr\.(?:red|green|blue)=.*\n', '',
                   re.sub(r'^cell_function=QCAD_CELL_(NORMAL|OUTPUT)$',
                          'cell_function=PASSIVE',
                          re.sub(r'\[TYPE:QCADLabel\].*?\[#TYPE:QCADLabel\]\n?', '', block, flags=re.S),
                          flags=re.M), flags=re.M) for block in blocks]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--energy-binary', type=Path, required=True)
    parser.add_argument('--gui-binary', type=Path)
    parser.add_argument('--simulation-binary', type=Path)
    parser.add_argument('--output-dir', type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='ifcn-primary-outputs-') as temporary:
        directory = args.output_dir.resolve() if args.output_dir else Path(temporary)
        directory.mkdir(parents=True, exist_ok=True)
        mapped = {}
        for name, header, expected in (
            ('legacy', '', {'y'}),
            ('both', '#primary output nodes: 2,4', {'x', 'y'}),
            ('internal_only', '#primary output nodes: 2', {'x'}),
        ):
            layout = directory / f'{name}.ifcn'
            layout.write_text(LAYOUT.format(header=header))
            result = run([args.energy_binary, layout, directory/name, '--qca-only'], directory, name)
            assert result.returncode == 0, (name, result.stderr)
            mapped[name] = cells(directory/f'{name}_energy_input.qca')
            assert outputs(mapped[name]) == expected, (name, outputs(mapped[name]))
        assert topology(mapped['legacy']) == topology(mapped['both']) == topology(mapped['internal_only']), 'Output metadata changed physical geometry or electrical parameters'
        invalid = {
            'missing_node': '#primary output nodes: 2,99',
            'duplicate_id': '#primary output nodes: 2,2',
            'empty': '#primary output nodes:',
            'duplicate_header': '#primary output nodes: 2\n#primary output nodes: 4',
            'input_observation': '#primary output nodes: 0,4',
        }
        for name, header in invalid.items():
            layout = directory/f'{name}.ifcn'; layout.write_text(LAYOUT.format(header=header))
            result = run([args.energy_binary, layout, directory/name, '--qca-only'], directory, name)
            assert result.returncode != 0, (name, 'Invalid output metadata was accepted')
            assert not (directory/f'{name}_energy_input.qca').exists()
        # A pure alias stays an observable output while forwarding its
        # value through a different physical port into an AND gate.
        (directory/'tap.ifcn').write_text(TAP_LAYOUT)
        tap_result = run([args.energy_binary, directory/'tap.ifcn', directory/'tap', '--qca-only'],
                         directory, 'tap')
        assert tap_result.returncode == 0, tap_result.stderr
        tap_cells = cells(directory/'tap_energy_input.qca')
        assert outputs(tap_cells) == {'tap', 'y'}
        tap_sites = {(round((float(re.search(r'^x=(.*)$', block, re.M)[1])-200)/20),
                      round((float(re.search(r'^y=(.*)$', block, re.M)[1])-200)/20))
                     for block in tap_cells}
        # The old output template left a missing cell between the observed
        # center and its outgoing boundary. Long-range simulator coupling can
        # conceal that gap in a small circuit, so also enforce actual wire
        # continuity independently of truth-table agreement.
        assert {(2, 12), (2, 13), (2, 14)} <= tap_sites, 'Primary output alias has a disconnected outgoing half-wire'
        for name, source_path, expected, expected_cells in MAJORITY_CASES:
            layout = directory/f'{name}.ifcn'
            layout.write_bytes((ROOT/'tests/fixtures'/f'{name}.ifcn').read_bytes())
            result = run([args.energy_binary, layout, directory/name, '--qca-only'], directory, name)
            assert result.returncode == 0, (name, result.stderr)
            device = cells(directory/f'{name}_energy_input.qca')
            assert outputs(device) == expected, (name, outputs(device))
            assert len(device) == expected_cells, (name, 'Output annotation changed mapped geometry')
        if args.gui_binary:
            for name, expected in (('both', {'a', 'b', 'x', 'y'}),
                                   ('internal_only', {'a', 'b', 'x'}),
                                   ('tap', {'a', 'b', 'tap', 'y'}),
                                   ('majority_leaf', {'a', 'b', 'c', 'carry'}),
                                   ('majority_parity', {'A', 'B', 'Cin', 'M3'})):
                svg = directory/f'{name}.svg'
                env = dict(os.environ, QT_QPA_PLATFORM='offscreen',
                           IFCN_AUTO_MAP_FILE=str(directory/f'{name}.ifcn'),
                           IFCN_AUTO_EXPORT_CELL_LAYOUT=str(svg))
                result = run([args.gui_binary], directory, 'gui_'+name, env=env)
                assert result.returncode == 0, result.stderr
                labels = {''.join(element.itertext()) for element in ET.parse(svg).iter()
                          if element.tag.split('}')[-1] == 'text'}
                assert expected <= labels, (name, expected, labels)
                if name == 'internal_only': assert 'y' not in labels
        if args.simulation_binary:
            from test_combinational_physical_waveform import (
                source_analysis, device_terminals, check_waveform,
                HOLD_CYCLES, SAMPLES_PER_CYCLE)
            cases = [('both', SOURCE), ('tap', TAP_SOURCE)]
            cases.extend((name, source.read_bytes()) for name, source, _, _ in MAJORITY_CASES)
            for name, source_bytes in cases:
                source = source_analysis(source_bytes, max_inputs=11)
                qca = directory/f'{name}_energy_input.qca'
                terminals, labels, _ = device_terminals(qca, source)
                vectors = list(itertools.product((0, 1), repeat=len(source['input_ports']))) * 2
                vector_path = directory/f'{name}_inputs.vt'
                vector_path.write_text(','.join(source['input_ports'])+'\n'+''.join(
                    (','.join(map(str, vector))+'\n')*HOLD_CYCLES for vector in vectors))
                samples = len(vectors)*HOLD_CYCLES*SAMPLES_PER_CYCLE
                prefix = directory/f'simulation_{name}'
                result = run([args.simulation_binary, qca, '--model', 'bistable', '--vectors', vector_path,
                              '--samples', samples, '--max-iterations', 1000, '--repetitions', 1,
                              '--warmup', 0, '--seed', 1, '--require-equivalent', '--equivalence-tolerance', 0,
                              '--output-prefix', prefix, '--json', directory/f'simulation_{name}.json'],
                             directory, f'simulation_{name}')
                assert result.returncode == 0, result.stderr
                report = check_waveform(Path(str(prefix)+'_bistable_baseline.rst'), source, vectors,
                                        terminals, labels, samples)
                report_file = 'physical_truth.json' if name == 'both' else f'{name}_physical_truth.json'
                (directory/report_file).write_text(json.dumps(report, indent=2)+'\n')
                assert report['passed'], report['counterexamples']
        print(json.dumps({'passed': True, 'cells': len(mapped['both']),
                          'outputs': sorted(outputs(mapped['both'])), 'invalid_cases_rejected': len(invalid)}))


if __name__ == '__main__':
    main()
