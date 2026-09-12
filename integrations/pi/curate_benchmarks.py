#!/usr/bin/env python3
"""Freeze scalar combinational benchmark sources without changing originals.

Use --check to verify the published snapshot, source hashes, complete truth
tables without writing files. This deliberately small
parser is a truth-table oracle for the supported scalar subset, not a complete
Verilog frontend or a device-level functional verifier.
"""

from __future__ import annotations

import argparse
import hashlib
import itertools
import json
from pathlib import Path
import re
import sys


ROOT = Path(__file__).resolve().parents[2]
VERSION = "v1"
SNAPSHOT = ROOT / "tests" / "benchmarks_pi" / VERSION
FOLDERS = ("TOY", "MAJ")
# Versioned membership must not change when other algorithms add new examples.
V1_SOURCES = {
    "TOY": ("1bitAdderAOIG", "1bitAdderMaj", "RCA2", "b1_r2", "clpl",
            "mux21", "mux41", "newtag", "par_check", "par_gen",
            "paper_2ddwave_carry_demo", "paper_2ddwave_crossing_demo",
            "paper_2ddwave_xor_demo", "t", "xnor2", "xor2", "xor5R"),
    "MAJ": ("1bitAdderAOIG", "1bitAdderMaj", "RCA2", "clpl", "mux41",
            "newtag", "par_check", "par_gen", "t", "xnor2", "xor2",
            "xor5R", "xor5_r1"),
}


def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def encoded(value: object) -> bytes:
    return (json.dumps(value, indent=2, sort_keys=True, ensure_ascii=False) + "\n").encode()


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


def truth_digest(analysis: dict) -> str:
    payload = {
        "input_count": len(analysis["input_ports"]),
        "output_count": len(analysis["output_ports"]),
        "rows": analysis["truth_table"],
    }
    return sha(encoded(payload))


def normalized(case_id: str, data: bytes) -> tuple[bytes, list[dict]]:
    if case_id == "TOY/xor2":
        old, new = b"module top(in0, in1, out);", b"module top(a, b, out);"
        if data.count(old) != 1:
            raise ValueError("TOY/xor2 source changed; review normalization")
        return data.replace(old, new), [{
            "kind": "interface_correction",
            "description": "Align module header with the existing scalar input declarations a,b; assign logic is unchanged.",
            "before": old.decode(), "after": new.decode(),
        }]
    if case_id == "TOY/RCA2":
        changes = []
        for old, new, description in (
            (b"assign w8 = a1 & b1;", b"assign w8 = a0 & b0;", "Use low-bit generate for the carry into bit 1."),
            (b"assign w19 = n2 & cin;", b"assign w19 = n2 & cint;", "Use the intermediate carry when computing final carry-out."),
        ):
            if data.count(old) != 1:
                raise ValueError("TOY/RCA2 source changed; review arithmetic correction")
            data = data.replace(old, new)
            changes.append({"kind": "arithmetic_correction", "description": description, "before": old.decode(), "after": new.decode()})
        return data, changes
    return data, []


def independent_reference(case_id: str, bits: tuple[int, ...]):
    """References express arithmetic/Boolean specifications, not source graphs."""
    name = case_id.split("/")[1]
    if case_id == "TOY/RCA2":
        a0, a1, b0, b1, cin = bits
        value = (a0 + 2 * a1) + (b0 + 2 * b1) + cin
        return [(value >> position) & 1 for position in range(3)]
    if name in ("xor2", "paper_2ddwave_xor_demo", "par_gen", "xor5R", "xor5_r1"):
        return [sum(bits) % 2]
    if name == "xnor2":
        return [1 ^ (sum(bits) % 2)]
    if name == "1bitAdderAOIG":
        return [sum(bits) % 2, int(sum(bits) >= 2)]
    if name == "1bitAdderMaj":
        a, b, cin = bits
        return [int(a + b + (1 - cin) >= 2)]
    if name in ("paper_2ddwave_carry_demo", "paper_2ddwave_crossing_demo"):
        return [int(sum(bits) >= 2)]
    if name == "mux21":
        return [bits[bits[2]]]
    if name == "mux41":
        return [bits[bits[4] + 2 * bits[5]]]
    return None


def semantic_label(case_id: str) -> tuple[str, str]:
    name = case_id.split("/")[1]
    if case_id == "TOY/RCA2":
        return "unsigned_two_bit_adder", "A=a0+2*a1; B=b0+2*b1; {cout,sum1,sum0}=A+B+cin."
    if case_id == "MAJ/RCA2":
        return "source_defined_multi_output_logic", "Preserved source logic; x/y port numbering does not establish the intended arithmetic specification. Do not pair with the corrected TOY/RCA2 as equivalent designs."
    if name == "par_check":
        return "source_defined_combinational_logic", "Preserved source function (a XOR b) XOR ((c AND NOT p) OR (NOT a AND p)), with MAJ inputs mapped by declaration order. This is not labeled conventional four-input parity checking."
    if name == "1bitAdderMaj":
        return "majority_with_inverted_third_input", "majority(input0,input1,NOT input2), one output; the historical filename does not make this a complete full adder."
    labels = {
        "1bitAdderAOIG": ("one_bit_full_adder", "sum=XOR(input0,input1,input2); carry=majority(input0,input1,input2)."),
        "xor2": ("xor2", "XOR of two inputs."),
        "paper_2ddwave_xor_demo": ("xor2", "XOR of two inputs; alternate representation."),
        "xnor2": ("xnor2", "Complement of XOR of two inputs."),
        "par_gen": ("xor3", "XOR of three inputs."),
        "xor5R": ("xor5", "XOR of five inputs."),
        "xor5_r1": ("xor5", "XOR of five inputs; exact duplicate of MAJ/xor5R."),
        "mux21": ("mux2", "Input 2 selects data input 0 or 1."),
        "mux41": ("mux4", "Inputs 4 and 5 form little-endian selector of data inputs 0..3."),
        "paper_2ddwave_carry_demo": ("majority3", "At least two of three inputs are high."),
        "paper_2ddwave_crossing_demo": ("majority3", "At least two of three inputs are high; alternate representation."),
        "clpl": ("multi_output_and_or_chain", "Source-defined 11-input, 5-output cascaded AND/OR network."),
        "newtag": ("source_defined_combinational_logic", "Source-defined 8-input, 1-output Boolean network."),
        "t": ("source_defined_multi_output_logic", "Source-defined 5-input, 2-output Boolean network."),
        "b1_r2": ("source_defined_multi_output_logic", "Source-defined 3-input, 4-output Boolean network."),
    }
    return labels[name]


def build_manifest() -> tuple[dict, dict[str, bytes], dict[str, bytes]]:
    cases, files, originals = [], {}, {}
    for folder in FOLDERS:
        for name in sorted(V1_SOURCES[folder]):
            source = ROOT / "tests" / "benchmarks_f" / folder / f"{name}.v"
            case_id = f"{folder}/{source.stem}"
            source_relpath = source.relative_to(ROOT).as_posix()
            original = source.read_bytes()
            originals[source_relpath] = original
            frozen, changes = normalized(case_id, original)
            before, after = analyze(original), analyze(frozen)
            if not after["header_matches_declarations"]:
                raise ValueError(f"Invalid frozen module header: {case_id}")
            equivalent = before["truth_table"] == after["truth_table"]
            expected = [independent_reference(case_id, bits) for bits in itertools.product((0, 1), repeat=len(after["input_ports"]))]
            has_reference = expected[0] is not None
            if has_reference and after["truth_table"] != expected:
                raise ValueError(f"Independent specification failed: {case_id}")
            if case_id != "TOY/RCA2" and not equivalent:
                raise ValueError(f"Unexpected logic change: {case_id}")
            if case_id == "TOY/RCA2" and equivalent:
                raise ValueError("RCA2 correction unexpectedly did not change truth table")
            path = f"tests/benchmarks_pi/{VERSION}/{folder}/{source.name}"
            files[path] = frozen
            category, specification = semantic_label(case_id)
            digest = truth_digest(after)
            differences = sum(a != b for a, b in zip(before["truth_table"], after["truth_table"]))
            cases.append({
                "id": case_id,
                "source_relpath": source_relpath,
                "source_sha256": sha(original),
                "frozen_relpath": path,
                "frozen_sha256": sha(frozen),
                "input_ports": after["input_ports"],
                "output_ports": after["output_ports"],
                "module": after["module"],
                "source_header_matches_declarations": before["header_matches_declarations"],
                "frozen_header_matches_declarations": after["header_matches_declarations"],
                "assignments": after["assignments"],
                "expression_operators": after["expression_operators"],
                "semantic_category": category,
                "specification": specification,
                "function_group": "tt_" + digest[:16],
                "truth_table_sha256": digest,
                "original_truth_table_sha256": truth_digest(before),
                "change_notes": changes,
                "validation": {
                    "vectors": len(after["truth_table"]),
                    "source_to_frozen": "arithmetic_correction_against_independent_reference" if case_id == "TOY/RCA2" else "exhaustive_equivalence",
                    "source_to_frozen_equivalent": equivalent,
                    "changed_output_vectors": differences,
                    "independent_specification_checked": has_reference,
                    "independent_specification_passed": True if has_reference else None,
                    "physical_verification": "not_established_by_this_manifest",
                },
            })
    groups = []
    for group_id in sorted({case["function_group"] for case in cases}):
        members = [case for case in cases if case["function_group"] == group_id]
        groups.append({
            "id": group_id,
            "representative_case_id": members[0]["id"],
            "case_ids": [case["id"] for case in members],
            "semantic_category": members[0]["semantic_category"],
            "input_count": len(members[0]["input_ports"]),
            "output_count": len(members[0]["output_ports"]),
            "truth_table_sha256": members[0]["truth_table_sha256"],
            "interpretation": "Identical complete truth tables with inputs and outputs matched by declaration order; different members are representation cases, not independent Boolean functions.",
        })
    hashes = {}
    for case in cases:
        previous = hashes.setdefault(case["frozen_sha256"], case["id"])
        case["exact_duplicate_of"] = None if previous == case["id"] else previous
    manifest = {
        "schema_version": 1,
        "benchmark_version": VERSION,
        "name": "iFCN Pi scalar combinational benchmark snapshot",
        "generator_relpath": "integrations/pi/curate_benchmarks.py",
        "scope": "The 30 top-level TOY and MAJ Verilog sources present when v1 was frozen. Generated subdirectories are excluded. No original source is changed.",
        "port_order": "input/output declaration order; input combinations enumerate 0 before 1 lexicographically",
        "classification_limit": "Function groups compare fixed declared port order. This is not NPN equivalence, and no claim of an absolute independent circuit count is made.",
        "evaluation_policy": {
            "primary_function_cases": "Use one declared representative per function_group for function-level aggregate statistics.",
            "representation_cases": "Report all source representations separately when studying representation robustness; identify function_group and exact_duplicate_of.",
            "same_function_pnr_comparisons": "Compare algorithm results for the same frozen_sha256. A cross-representation comparison also requires matching function_group.",
            "claims": "Source logical checks do not establish routed/device-level correctness, measured power, LLM benefit, or publication priority.",
        },
        "counts": {
            "files": len(cases), "toy": sum(case["id"].startswith("TOY/") for case in cases),
            "maj": sum(case["id"].startswith("MAJ/") for case in cases),
            "source_byte_unique": len({case["source_sha256"] for case in cases}),
            "frozen_byte_unique": len(hashes), "fixed_port_order_truth_table_classes": len(groups),
            "independent_specification_cases": sum(case["validation"]["independent_specification_checked"] for case in cases),
        },
        "cases": cases,
        "function_groups": groups,
    }
    return manifest, files, originals


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="Read-only verification of existing frozen files")
    args = parser.parse_args()
    manifest, files, originals = build_manifest()
    payloads = dict(files)
    payloads[f"tests/benchmarks_pi/{VERSION}/manifest.json"] = encoded(manifest)
    existing_manifest = SNAPSHOT / "manifest.json"
    # v1 is immutable after creation: source drift must not silently rewrite it.
    if existing_manifest.exists() and existing_manifest.read_bytes() != payloads[f"tests/benchmarks_pi/{VERSION}/manifest.json"]:
        raise ValueError("v1 source or generator drift detected; review and create a new benchmark version instead of overwriting v1")
    for relpath, data in payloads.items():
        target = ROOT / relpath
        if args.check:
            if not target.is_file() or target.read_bytes() != data:
                raise ValueError(f"Missing or modified frozen artifact: {relpath}")
        else:
            if target.exists() and target.read_bytes() != data:
                raise ValueError(f"Refusing to overwrite existing artifact: {relpath}")
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
    for relpath, original in originals.items():
        if (ROOT / relpath).read_bytes() != original:
            raise ValueError(f"Original source changed during curation: {relpath}")
    print(json.dumps({"status": "verified" if args.check else "frozen", "manifest": str(existing_manifest), "counts": manifest["counts"], "original_sources_unchanged": True}, indent=2))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ValueError, OSError) as error:
        print(f"Curation failed: {error}", file=sys.stderr)
        raise SystemExit(1)
