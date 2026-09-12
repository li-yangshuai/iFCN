"""Regression cases for rejecting incomplete/shorted routing artifacts."""
from copy import deepcopy
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'src/python'))
from ifcn.layout_validation import validate_candidate, validate_clock_epochs, export_native_ifcn

class LayoutValidationTests(unittest.TestCase):
    def setUp(self):
        self.candidate=json.loads((ROOT/'tests/fixtures/routing/xor2_fixed_clock.json').read_text())

    def test_real_xor_allows_same_source_fanout_and_orthogonal_crossing(self):
        # At (1,1), source 1 travels vertically toward OR node 3 while source 0
        # travels horizontally toward AND node 4. They have distinct sinks;
        # the native device mapper checks the realized crossover downstream.
        result=validate_candidate(self.candidate,source_crossings=True)
        self.assertTrue(result['passed'],result['errors'])

    def test_distinct_sources_cannot_share_one_fanin_port(self):
        # Both sources now enter the SAME AND node 4 through neighbor (1,1).
        # This would collapse its two physical fanin ports during mapping.
        for route in self.candidate['routes']:
            if (route['source'],route['target'])==(1,4):
                route['path']=[[1,0],[1,1],[2,1]]
        result=validate_candidate(self.candidate)
        self.assertFalse(result['passed'])
        self.assertTrue(any('Multiple fanins share one physical input port at sink 4' in e for e in result['errors']))

    def test_completed_flag_cannot_hide_missing_edge(self):
        self.candidate['routed']=True
        self.candidate['routes'].pop()
        result=validate_candidate(self.candidate)
        self.assertFalse(result['passed'])
        self.assertIn('Routes do not exactly cover the effective DAG edges',result['errors'])

    def test_phase_metadata_cannot_hide_backward_step(self):
        for cell in self.candidate['cells']:
            if (cell['x'],cell['y'])==(1,1):
                cell['phase']=1
        result=validate_candidate(self.candidate)
        self.assertFalse(result['passed'])
        self.assertTrue(any('backward clock step' in e for e in result['errors']))

    def test_bend_is_not_accepted_as_orthogonal_port_crossing(self):
        # Rerouting source 0 to approach (1,1) from above creates a bend at
        # the shared point, so the distinct-source sharing is illegal.
        for route in self.candidate['routes']:
            if (route['source'],route['target'])==(0,4):
                route['path']=[[0,0],[1,0],[1,1],[2,1]]
        result=validate_candidate(self.candidate)
        self.assertFalse(result['passed'])
        self.assertTrue(any('without an H/V crossing' in e for e in result['errors']))

    def test_native_export_translates_coordinates_and_phase_together(self):
        shifted=deepcopy(self.candidate)
        for node in shifted['nodes']:
            node['x']+=5;node['y']+=7
        for cell in shifted['cells']:
            cell['x']+=5;cell['y']+=7
        for route in shifted['routes']:
            route['path']=[[x+5,y+7] for x,y in route['path']]
        with tempfile.TemporaryDirectory() as temp:
            output=Path(temp)/'layout.ifcn'
            self.assertEqual(export_native_ifcn(shifted,output,'compact'),(3,4))
            exported=output.read_text()
            self.assertIn('0, a, input, (0,0);',exported)
            self.assertIn('(0,0):0;',exported)
            self.assertIn('(1,1):2;',exported)
            self.assertIn('#mapping mode: combinational',exported)

    def test_real_compact_xor_rejects_a_whole_cycle_fanin_mismatch(self):
        # Captured Compact XOR: a reaches OR directly in phase 2, but its
        # other fanout traverses a full cycle before AND. Both b paths take
        # three advances. Local phase order cannot detect the contradiction.
        nodes = {
            0: {'x': 1, 'y': 1, 'is_input': True},
            1: {'x': 3, 'y': 1, 'is_input': True},
            3: {'x': 1, 'y': 2, 'is_input': False},
            4: {'x': 3, 'y': 2, 'is_input': False},
        }
        routes = {
            (0, 3): [(1, 1), (1, 2)],
            (0, 4): [(1, 1), (1, 0), (2, 0), (3, 0), (4, 0), (4, 1), (4, 2), (3, 2)],
            (1, 3): [(3, 1), (2, 1), (2, 2), (1, 2)],
            (1, 4): [(3, 1), (2, 1), (2, 2), (3, 2)],
        }
        phases = {(1, 1): 3, (1, 2): 3, (1, 0): 4, (2, 0): 4,
                  (3, 0): 1, (4, 0): 2, (4, 1): 3, (4, 2): 3,
                  (3, 2): 3, (3, 1): 4, (2, 1): 1, (2, 2): 2}
        self.assertTrue(all((phases[b] - phases[a]) % 4 in (0, 1)
                            for path in routes.values() for a, b in zip(path, path[1:])))
        result = validate_clock_epochs(nodes, routes, phases)
        self.assertFalse(result['passed'])
        self.assertTrue(any('epoch conflict' in e for e in result['errors']))

    def test_globally_consistent_fixed_clock_reconvergence(self):
        result = validate_candidate(self.candidate, source_crossings=True)
        self.assertTrue(result['clock_timing']['passed'], result['errors'])
        self.assertEqual(result['clock_timing']['node_epochs'],
                         {n['id']: n['x'] + n['y'] for n in self.candidate['nodes']})

    def test_individual_gates_cannot_share_one_clock_epoch(self):
        nodes = {0: {'x': 0, 'y': 0, 'is_input': True},
                 1: {'x': 1, 'y': 0, 'is_input': False}}
        result = validate_clock_epochs(nodes, {(0, 1): [(0, 0), (1, 0)]},
                                       {(0, 0): 1, (1, 0): 1})
        self.assertFalse(result['passed'])
        self.assertTrue(any('no clock advance' in e for e in result['errors']))

    def test_gate_cannot_switch_together_with_its_last_fanin_wire(self):
        nodes = {0: {'x': 0, 'y': 0, 'is_input': True},
                 1: {'x': 2, 'y': 0, 'is_input': False}}
        result = validate_clock_epochs(nodes, {(0, 1): [(0, 0), (1, 0), (2, 0)]},
                                       {(0, 0): 1, (1, 0): 2, (2, 0): 2})
        self.assertFalse(result['passed'])
        self.assertTrue(any('fanin wire must hold' in e for e in result['errors']))

    def test_locally_forward_input_cannot_be_shifted_a_cycle(self):
        nodes = {0: {'x': 0, 'y': 0, 'is_input': True},
                 1: {'x': 6, 'y': 0, 'is_input': True},
                 2: {'x': 5, 'y': 0, 'is_input': False}}
        routes = {(0, 2): [(x, 0) for x in range(6)], (1, 2): [(6, 0), (5, 0)]}
        phases = {(x, 0): x % 4 + 1 for x in range(6)} | {(6, 0): 1}
        result = validate_clock_epochs(nodes, routes, phases)
        self.assertFalse(result['passed'])
        self.assertTrue(any('launch cycle' in e for e in result['errors']))

    def test_gate_output_cannot_drive_a_long_wire_in_its_own_phase(self):
        # A same-phase load after a NOT passed local/epoch checks but inverted
        # incorrectly in physical June MUX simulations. Isolate its output.
        nodes = {0: {'x': 0, 'y': 0, 'is_input': True},
                 1: {'x': 1, 'y': 0, 'is_input': False},
                 2: {'x': 4, 'y': 0, 'is_input': False}}
        routes = {(0, 1): [(0, 0), (1, 0)],
                  (1, 2): [(1, 0), (2, 0), (3, 0), (4, 0)]}
        phases = {(0, 0): 1, (1, 0): 2, (2, 0): 2, (3, 0): 2, (4, 0): 3}
        result = validate_clock_epochs(nodes, routes, phases)
        self.assertFalse(result['passed'])
        self.assertTrue(any('output wire switches' in e for e in result['errors']))
        phases.update({(2, 0): 3, (3, 0): 3, (4, 0): 4})
        result = validate_clock_epochs(nodes, routes, phases)
        self.assertTrue(result['passed'], result['errors'])

if __name__=='__main__':
    unittest.main()
