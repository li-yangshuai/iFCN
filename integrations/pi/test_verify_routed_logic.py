"""Tests of the independent source-to-routed-DAG Boolean audit."""
import copy
import json
from pathlib import Path
import sys
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parent))
from verify_routed_logic import verify_bytes

SOURCE=b'module top(a,b,z); input a,b; output z; wire n; assign n=a & b; assign z=n; endmodule\n'
DAG={'schema':'ifcn.dag.v1','input_count':2,'output_count':1,'nodes':[
    {'id':0,'name':'a','type':'input','is_input':True,'is_output':False},
    {'id':1,'name':'b','type':'input','is_input':True,'is_output':False},
    {'id':2,'name':'n','type':'and','is_input':False,'is_output':True},
], 'edges':[[0,2],[1,2]]}

class RoutedLogicAuditTests(unittest.TestCase):
    def audit(self,dag=None,source=SOURCE):
        return verify_bytes(source,json.dumps(dag or DAG).encode())

    def test_explicit_source_alias_and_full_truth_table(self):
        result=self.audit()
        self.assertEqual(result['status'],'equivalent')
        self.assertEqual(result['vectors_checked'],4)
        self.assertEqual(result['output_mapping'][0]['alias_chain'],['z','n'])
        self.assertEqual(result['source_truth_sha256'],result['dag_truth_sha256'])

    def test_real_gate_change_gives_named_counterexamples(self):
        dag=copy.deepcopy(DAG);dag['nodes'][2]['type']='or'
        result=self.audit(dag)
        self.assertEqual(result['status'],'not_equivalent')
        self.assertFalse(result['equivalent'])
        self.assertEqual(result['mismatch_count'],2)
        self.assertEqual(result['counterexamples'][0],{'inputs':{'a':0,'b':1},'source_outputs':{'z':0},'dag_outputs':{'z':1}})

    def test_missing_output_name_does_not_trigger_function_matching(self):
        dag=copy.deepcopy(DAG);dag['nodes'][2]['name']='mystery'
        result=self.audit(dag)
        self.assertEqual(result['status'],'unsupported')
        self.assertIsNone(result['equivalent'])
        self.assertEqual(result['vectors_checked'],0)

    def test_scalar_ansi_has_the_same_named_interface(self):
        source=b'module top(input wire a, b, output z);wire n;assign n=a & b;assign z=n;endmodule\n'
        result=self.audit(source=source)
        self.assertEqual(result['status'],'equivalent')
        self.assertEqual(result['input_ports'],['a','b'])
        self.assertEqual(result['output_ports'],['z'])

    def test_input_order_cannot_substitute_for_input_names(self):
        dag=copy.deepcopy(DAG);dag['nodes'][0]['name']='renamed'
        self.assertEqual(self.audit(dag)['status'],'unsupported')

    def test_unknown_constant_type_is_not_inferred_from_name(self):
        dag=copy.deepcopy(DAG);dag['nodes'][2].update(name='n',type='const1')
        self.assertEqual(self.audit(dag)['status'],'unsupported')

    def test_fanin_loss_is_not_evaluated_as_unary_and(self):
        dag=copy.deepcopy(DAG);dag['edges'].pop()
        result=self.audit(dag)
        self.assertEqual(result['status'],'unsupported')
        self.assertIn('supported arity is 2',result['reason'])

    def test_inverted_source_output_is_not_guessed_as_alias(self):
        source=SOURCE.replace(b'assign z=n;',b'assign z=~n;')
        self.assertEqual(self.audit(source=source)['status'],'unsupported')

    def test_bounded_audit_rejects_excessive_input_count(self):
        result=verify_bytes(SOURCE,json.dumps(DAG).encode(),max_inputs=1)
        self.assertEqual(result['status'],'unsupported')
        self.assertIn('outside exhaustive limit',result['reason'])

    def test_exact_names_preserve_output_order_without_permutation(self):
        source=b'module t(a,b,s,c);input a,b;output s,c;wire n;assign s=a | b;assign c=a & b;endmodule\n'
        dag=copy.deepcopy(DAG);dag['output_count']=2
        dag['nodes'][2].update(name='s',type='and')
        dag['nodes'].append({'id':3,'name':'c','type':'or','is_input':False,'is_output':True})
        dag['edges'] += [[0,3],[1,3]]
        result=self.audit(dag,source)
        self.assertEqual(result['status'],'not_equivalent')
        self.assertEqual(result['mismatch_count'],2)
        self.assertEqual([m['dag_node_id'] for m in result['output_mapping']],[2,3])

if __name__=='__main__':
    unittest.main()
