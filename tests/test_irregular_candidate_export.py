#!/usr/bin/env python3
"""Observe legal alternatives from the one irregular search without overwriting files."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'src/python'))
from ifcn.layout_validation import dag_from_candidate, validate_candidate
from ifcn.verify_logic import verify_bytes


SOURCE = b'module example(a,b,y); input a,b; output y; assign y=a&b; endmodule\n'


def topology(candidate):
    return {
        'layers': candidate['layers'],
        'nodes': sorted(candidate['nodes'], key=lambda node: node['id']),
        'edges': sorted(candidate['edges']),
        'routes': sorted(candidate['routes'], key=lambda route: (route['source'], route['target'])),
        'cells': sorted(candidate['cells'], key=lambda cell: (cell['x'], cell['y'])),
        'metrics': candidate['metrics'],
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--binary', type=Path, required=True)
    args = parser.parse_args()
    binary = args.binary.resolve()
    with tempfile.TemporaryDirectory(prefix='ifcn-candidate-export-') as temporary:
        directory = Path(temporary)
        source = directory / 'and.v'
        source.write_bytes(SOURCE)
        final = directory / 'winner.json'
        alternatives = directory / 'alternatives'

        def run(output, *extra):
            return subprocess.run([str(binary), 'irregular', str(source), str(output),
                                   '--budget', '2', '--attempts', '1', *map(str, extra)],
                                  cwd=directory, capture_output=True, text=True, timeout=20)

        result = run(final, '--candidates-dir', alternatives)
        assert result.returncode == 0, result.stderr
        winner = json.loads(final.read_text())
        files = sorted(alternatives.iterdir())
        assert files, 'No legal candidates were exported'
        assert [path.name for path in files] == [f'candidate_{i:06d}.json' for i in range(1, len(files) + 1)]
        winner_seen = False
        for path in files:
            candidate = json.loads(path.read_text())
            assert candidate['schema'] == 'ifcn.native_candidate.v1'
            assert candidate['algorithm'] == 'irregular'
            assert candidate['routed'] and candidate['native_mapping_valid']
            assert candidate['native_validation_error'] == '' and candidate['selected_seed']
            assert candidate['metrics']['area_tiles'] > 0 and candidate['metrics']['physical_cells'] > 0
            assert {node['name'] for node in candidate['nodes'] if node['is_output']} == {'y'}
            assert {node['name'] for node in candidate['nodes'] if node['is_input']} == {'a', 'b'}
            validation = validate_candidate(candidate, source_crossings=False)
            assert validation['passed'], validation['errors']
            logic = verify_bytes(SOURCE, json.dumps(dag_from_candidate(candidate, validation)).encode(), max_inputs=2)
            assert logic['status'] == 'equivalent', logic
            winner_seen |= topology(candidate) == topology(winner)
        assert winner_seen, 'No exported candidate has the final winner topology, clocks and metrics'

        # A second call must leave both earlier candidates and its final file intact.
        saved = {path.name: path.read_bytes() for path in files}
        original_final = final.read_bytes()
        duplicate = run(final, '--candidates-dir', alternatives)
        assert duplicate.returncode != 0 and 'refusing to overwrite' in duplicate.stderr
        assert final.read_bytes() == original_final
        assert {path.name: path.read_bytes() for path in alternatives.iterdir()} == saved

        occupied = directory / 'occupied'
        occupied.mkdir(); (occupied / 'unrelated.txt').write_text('keep this file')
        rejected = directory / 'rejected.json'
        result = run(rejected, '--candidates-dir', occupied)
        assert result.returncode != 0 and not rejected.exists()
        assert (occupied / 'unrelated.txt').read_text() == 'keep this file'

        empty = directory / 'existing-empty'
        empty.mkdir()
        result = run(directory / 'empty-winner.json', '--candidates-dir', empty)
        assert result.returncode == 0 and any(empty.glob('candidate_*.json')), result.stderr

        reserved = directory / 'reserved'
        result = run(reserved / 'candidate_000001.json', '--candidates-dir', reserved)
        assert result.returncode != 0 and not any(reserved.iterdir())

        # Failed search options emit the existing failure diagnostic only.
        invalid = directory / 'invalid'
        result = run(directory / 'invalid.json', '--candidates-dir', invalid, '--budget', '0')
        assert result.returncode != 0 and not any(invalid.iterdir())
        assert not json.loads((directory / 'invalid.json').read_text())['routed']

        plain = directory / 'plain'
        plain.mkdir()
        result = run(plain / 'winner.json')
        assert result.returncode == 0, result.stderr
        assert sorted(path.name for path in plain.iterdir()) == ['winner.json']
        print(json.dumps({'passed': True, 'legal_candidates_checked': len(files),
                          'winner_observed': winner_seen, 'overwrite_protection': True}))


if __name__ == '__main__':
    main()
