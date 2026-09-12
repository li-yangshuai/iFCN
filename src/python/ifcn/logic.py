"""Scalar Boolean expression parsing and exhaustive Verilog truth tables.

Supports scalar continuous assignments with ~, &, ^, |, parentheses, and binary
constants. This deliberately small oracle is independent of circuit placement,
benchmark collections, and physical simulation. Callers must bound the number
of inputs before requesting an exhaustive truth table.
"""

from __future__ import annotations

import itertools
import re


def expression_tokens(expr: str) -> list[str]:
    tokens = re.findall(r"[A-Za-z_]\w*|[~&|^()]|1'b[01]|[01]", expr)
    if "".join(tokens) != re.sub(r"\s+", "", expr):
        raise ValueError(f"Unsupported expression: {expr!r}")
    return tokens


def expression_tree(expr: str):
    tokens = expression_tokens(expr)
    position = 0

    def parse_atom():
        nonlocal position
        if position >= len(tokens):
            raise ValueError(f"Incomplete expression: {expr!r}")
        token = tokens[position]
        position += 1
        if token == "~":
            return ("~", parse_atom())
        if token == "(":
            node = parse_level(0)
            if position >= len(tokens) or tokens[position] != ")":
                raise ValueError(f"Unbalanced expression: {expr!r}")
            position += 1
            return node
        if token in ("&", "|", "^", ")"):
            raise ValueError(f"Unexpected token: {token}")
        return ("value", token)

    def parse_level(level):
        nonlocal position
        operators = ("|", "^", "&")
        if level == len(operators):
            return parse_atom()
        node = parse_level(level + 1)
        while position < len(tokens) and tokens[position] == operators[level]:
            op = tokens[position]
            position += 1
            node = (op, node, parse_level(level + 1))
        return node

    result = parse_level(0)
    if position != len(tokens):
        raise ValueError(f"Unused tokens in {expr!r}")
    return result


def evaluate(tree, signals):
    op = tree[0]
    if op == "value":
        token = tree[1]
        if token in ("0", "1'b0"):
            return 0
        if token in ("1", "1'b1"):
            return 1
        return signals[token]
    if op == "~":
        return 1 ^ evaluate(tree[1], signals)
    left, right = evaluate(tree[1], signals), evaluate(tree[2], signals)
    return {"&": lambda: left & right, "|": lambda: left | right, "^": lambda: left ^ right}[op]()


def analyze(data: bytes) -> dict:
    text = data.decode("utf-8-sig")
    text = re.sub(r"/\*.*?\*/|//[^\n]*", "", text, flags=re.S)
    module = re.search(r"\bmodule\s+(\w+)\s*\(([^)]*)\)\s*;", text)
    if not module or len(re.findall(r"\bmodule\b", text)) != 1:
        raise ValueError("Exactly one old-style scalar module is required")

    def ports(kind):
        declarations = re.findall(rf"\b{kind}\s+([^;]+);", text)
        result = [part.strip() for declaration in declarations for part in declaration.split(",")]
        if not result or any(not re.fullmatch(r"[A-Za-z_]\w*", part) for part in result):
            raise ValueError(f"Unsupported {kind} declarations")
        if len(result) != len(set(result)):
            raise ValueError(f"Duplicate {kind} declaration")
        return result

    inputs, outputs = ports("input"), ports("output")
    assignments = re.findall(r"\bassign\s+(\w+)\s*=\s*([^;]+);", text)
    trees = [(name, expression_tree(expr)) for name, expr in assignments]
    if len(trees) != len(set(name for name, _ in trees)):
        raise ValueError("Multiple drivers are unsupported")
    rows = []
    for bits in itertools.product((0, 1), repeat=len(inputs)):
        signals = dict(zip(inputs, bits))
        pending = list(trees)
        while pending:
            remaining = []
            for name, tree in pending:
                try:
                    signals[name] = evaluate(tree, signals)
                except KeyError:
                    remaining.append((name, tree))
            if len(remaining) == len(pending):
                raise ValueError("Undefined input or cyclic assign dependency")
            pending = remaining
        rows.append([signals[name] for name in outputs])
    header = [part.strip() for part in module.group(2).split(",")]
    return {
        "module": module.group(1),
        "input_ports": inputs,
        "output_ports": outputs,
        "header_ports": header,
        "header_matches_declarations": set(header) == set(inputs + outputs),
        "assignments": len(assignments),
        "expression_operators": sum(len(re.findall(r"[~&|^]", expr)) for _, expr in assignments),
        "truth_table": rows,
    }
