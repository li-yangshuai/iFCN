"""Checks for the independent scalar expression and truth-table oracle."""

import itertools
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "src/python"))
from ifcn.logic import analyze, evaluate, expression_tree


class ScalarLogicTests(unittest.TestCase):
    def test_scalar_precedence_and_parentheses(self):
        for a, b, c in itertools.product((0, 1), repeat=3):
            signals = dict(a=a, b=b, c=c)
            self.assertEqual(evaluate(expression_tree("a | b & ~c"), signals), a | (b & (1 - c)))
            self.assertEqual(evaluate(expression_tree("(a | b) & ~c"), signals), (a | b) & (1 - c))
            self.assertEqual(evaluate(expression_tree("a ^ b & c"), signals), a ^ (b & c))

    def test_assignment_order_does_not_change_semantics(self):
        data = b"module top(a,b,y); input a,b; output y; wire n; assign y=~n; assign n=a & b; endmodule"
        self.assertEqual(analyze(data)["truth_table"], [[1], [1], [1], [0]])

    def test_unsupported_or_unresolved_logic_fails(self):
        with self.assertRaises(ValueError):
            expression_tree("a ? b : c")
        for source in (
            b"module top(a,y); input a; output y; assign y=missing & a; endmodule",
            b"module top(a,y); input a; output y; wire n; assign y=n; assign n=y; endmodule",
            b"module top(a,y); input a; output y; assign y=a; assign y=~a; endmodule",
        ):
            with self.subTest(source=source), self.assertRaises(ValueError):
                analyze(source)

    def test_binary_constants_and_comment_stripping(self):
        data = b"""// Constant forms in a scalar expression.
module top(a,y); input a; output y;
assign y=(a & 1'b1) ^ (0 | 1); /* invert input */
endmodule
"""
        self.assertEqual(analyze(data)["truth_table"], [[1], [0]])


if __name__ == "__main__":
    unittest.main()
