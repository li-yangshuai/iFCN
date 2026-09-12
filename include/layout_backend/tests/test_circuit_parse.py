import contextlib
import io
import itertools
from pathlib import Path
import sys
import tempfile
import unittest

LAYOUT_ROOT = Path(__file__).resolve().parents[1]
ALGORITHM_ROOT = LAYOUT_ROOT / "src" / "algorithm"
sys.path.insert(0, str(ALGORITHM_ROOT))

from lib import iFCN_Lab
from src.circuit_parse import CircuitParser


def evaluate(parsed, inputs):
    values = {}
    pending = set(parsed.effective_nodes)
    while pending:
        progressed = False
        for node in sorted(pending):
            kind = parsed.getNodeTypeString(node).lower()
            fanins = parsed.get_fanins(node)
            if node in parsed.getInputNodesIndex:
                value = inputs[parsed.getNodeName(node)]
            elif not all(source in values for source in fanins):
                continue
            elif kind == "and":
                assert len(fanins) == 2
                value = all(values[source] for source in fanins)
            elif kind == "or":
                assert len(fanins) == 2
                value = any(values[source] for source in fanins)
            elif kind == "not":
                assert len(fanins) == 1
                value = not values[fanins[0]]
            else:
                assert kind in {"output", "wire", "fanout", "redundancy"}, kind
                assert len(fanins) == 1
                value = values[fanins[0]]
            values[node] = int(value)
            pending.remove(node)
            progressed = True
        if not progressed:
            raise AssertionError("Effective graph contains an undriven node or cycle")
    names = {int(node): parsed.getNodeName(node) for node in parsed.getOutputNodesIndex}
    for alias, driver in parsed.fused_output_aliases.items():
        names[int(driver)] = parsed.parser.getNodeName(alias)
    return {names[int(node)]: values[int(node)] for node in parsed.getOutputNodesIndex}


class FixedClockParserTest(unittest.TestCase):
    def parse_text(self, source):
        with tempfile.TemporaryDirectory(prefix="ifcn-fixed-parser-") as directory:
            path = Path(directory) / "source.v"
            path.write_text(source)
            with contextlib.redirect_stdout(io.StringIO()):
                return CircuitParser(str(path))

    def test_majority_uses_two_input_basis_with_exact_truth(self):
        parsed = self.parse_text("""
module top(input a,b,c, output out);
assign out=(a&b)|(a&c)|(b&c);
endmodule
""")
        self.assertTrue(all(len(parsed.get_fanins(node)) <= 2 for node in parsed.effective_nodes))
        self.assertNotIn(iFCN_Lab.NodeType.Maj,
                         [parsed.getNodeTypeEnum(node) for node in parsed.effective_nodes])
        for bits in itertools.product((0, 1), repeat=3):
            self.assertEqual(evaluate(parsed, dict(zip(("a", "b", "c"), bits))),
                             {"out": int(sum(bits) >= 2)})

    def test_observed_tap_and_two_equal_outputs_retain_identity(self):
        parsed = self.parse_text("""
module top(input a,b,c, output first,second,y,z);
wire w;
assign w=a&b;
assign first=w;
assign second=w;
assign y=w|c;
assign z=w&c;
endmodule
""")
        self.assertEqual(parsed.OutputNodesNum, 4)
        for a, b, c in itertools.product((0, 1), repeat=3):
            self.assertEqual(evaluate(parsed, {"a": a, "b": b, "c": c}),
                             {"first": a & b, "second": a & b,
                              "y": (a & b) | c, "z": a & b & c})

    def test_terminal_alias_keeps_its_source_port_name(self):
        parsed = self.parse_text("""
module top(input a,b, output out);
wire driver;
assign driver=a&b;
assign out=driver;
endmodule
""")
        self.assertEqual({parsed.parser.getNodeName(node)
                          for node in parsed.getOutputNodesIndex}, {"out"})
        for a, b in itertools.product((0, 1), repeat=2):
            self.assertEqual(evaluate(parsed, {"a": a, "b": b}), {"out": a & b})

    def test_b1_observable_direct_input_is_not_removed_with_fanout(self):
        fixture = LAYOUT_ROOT.parents[1] / "tests/benchmarks_f/TOY/b1_r2.v"
        with contextlib.redirect_stdout(io.StringIO()):
            parsed = CircuitParser(str(fixture))
        self.assertEqual(parsed.OutputNodesNum, 4)
        for a, b, c in itertools.product((0, 1), repeat=3):
            self.assertEqual(evaluate(parsed, {"pi0": a, "pi1": b, "pi2": c}),
                             {"po0": c, "po1": a ^ b,
                              "po2": ((1-c) & a & b) | (c & (1-a) & (1-b)), "po3": 1-c})


if __name__ == "__main__":
    unittest.main()
