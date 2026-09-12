"""Regression cases for rejecting incomplete/shorted Pi routing artifacts."""
from copy import deepcopy
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'scripts'))
from ifcn_agent_layout_validation import validate_candidate, export_native_ifcn

class LayoutValidationTests(unittest.TestCase):
    def setUp(self):
        self.candidate=json.loads((ROOT/'tests/fixtures/pi_layout/xor2_fixed_clock.json').read_text())

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

if __name__=='__main__':
    unittest.main()
