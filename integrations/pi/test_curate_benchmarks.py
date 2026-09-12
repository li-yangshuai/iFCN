"""Regression checks for benchmark preservation and the intentional RCA correction."""

import ast
import importlib.util
import itertools
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest
from unittest.mock import patch


HERE = Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location("curation", HERE / "curate_benchmarks.py")
C = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(C)


class CurationTests(unittest.TestCase):
    def test_scalar_precedence_and_parentheses(self):
        # Parentheses, unary inversion, and Verilog bitwise precedence differ.
        for a, b, c in itertools.product((0, 1), repeat=3):
            signals = dict(a=a, b=b, c=c)
            self.assertEqual(C.evaluate(C.expression_tree("a | b & ~c"), signals), a | (b & (1 - c)))
            self.assertEqual(C.evaluate(C.expression_tree("(a | b) & ~c"), signals), (a | b) & (1 - c))
            self.assertEqual(C.evaluate(C.expression_tree("a ^ b & c"), signals), a ^ (b & c))

    def test_assignment_order_does_not_change_semantics(self):
        data = b"module top(a,b,y); input a,b; output y; wire n; assign y=~n; assign n=a & b; endmodule"
        self.assertEqual(C.analyze(data)["truth_table"], [[1], [1], [1], [0]])

    def test_unsupported_or_unresolved_logic_fails(self):
        with self.assertRaises(ValueError):
            C.expression_tree("a ? b : c")
        with self.assertRaises(ValueError):
            C.analyze(b"module top(a,y); input a; output y; assign y=missing & a; endmodule")
        with self.assertRaises(ValueError):
            C.analyze(b"module top(a,y); input a; output y; wire n; assign y=n; assign n=y; endmodule")

    def test_rca_arithmetic_and_preserved_maj(self):
        manifest, files, _ = C.build_manifest()
        cases = {case["id"]: case for case in manifest["cases"]}
        toy, maj = cases["TOY/RCA2"], cases["MAJ/RCA2"]
        self.assertTrue(toy["validation"]["independent_specification_passed"])
        self.assertEqual(toy["validation"]["changed_output_vectors"], 12)
        self.assertFalse(toy["validation"]["source_to_frozen_equivalent"])
        self.assertEqual(maj["source_sha256"], maj["frozen_sha256"])
        self.assertNotEqual(toy["function_group"], maj["function_group"])
        analysis = C.analyze(files[toy["frozen_relpath"]])
        # A=0, B=2, cin=1 gives sum=3 and no overflow.
        index = list(itertools.product((0, 1), repeat=5)).index((0, 0, 0, 1, 1))
        self.assertEqual(analysis["truth_table"][index], [1, 1, 0])

    def test_frozen_manifest_and_original_hashes(self):
        manifest, files, originals = C.build_manifest()
        self.assertEqual(C.encoded(manifest), (C.SNAPSHOT / "manifest.json").read_bytes())
        self.assertEqual(manifest["counts"]["files"], 30)
        for relpath, data in files.items():
            self.assertEqual(data, (C.ROOT / relpath).read_bytes())
        for relpath, data in originals.items():
            self.assertEqual(data, (C.ROOT / relpath).read_bytes())
        for case in manifest["cases"]:
            if case["id"] != "TOY/RCA2":
                self.assertTrue(case["validation"]["source_to_frozen_equivalent"])

    def test_duplicate_and_function_level_accounting(self):
        manifest = json.loads((C.SNAPSHOT / "manifest.json").read_text())
        cases = {case["id"]: case for case in manifest["cases"]}
        self.assertEqual(cases["MAJ/xor5_r1"]["exact_duplicate_of"], "MAJ/xor5R")
        self.assertEqual(cases["TOY/xor2"]["function_group"], cases["MAJ/xor2"]["function_group"])
        self.assertEqual(len(manifest["function_groups"]), manifest["counts"]["fixed_port_order_truth_table_classes"])

    def test_pi_normalization_and_existing_parity_rewrite(self):
        frontend_path = C.ROOT / "scripts/ifcn_agent_frontend.py"
        frontend_spec = importlib.util.spec_from_file_location("frontend_under_test", frontend_path)
        frontend = importlib.util.module_from_spec(frontend_spec)
        frontend_spec.loader.exec_module(frontend)
        rewrite_path = C.ROOT / "include/gcn_rl_layout/src/algorithm/main/test_normal_graph_draw.py"
        tree = ast.parse(rewrite_path.read_text())
        wanted = {"build_parser_safe_verilog", "build_verified_parity_canonical_verilog"}
        definitions = [node for node in tree.body if isinstance(node, ast.FunctionDef) and node.name in wanted]
        self.assertEqual(len(definitions), 2)
        # Exercise the exact existing rewrite functions in isolation, avoiding
        # imports that would load Torch, GUI state or routing/training backends.
        namespace = {"ast": ast, "os": os, "re": re}
        exec(compile(ast.Module(body=definitions, type_ignores=[]), str(rewrite_path), "exec"), namespace)
        manifest = json.loads((C.SNAPSHOT / "manifest.json").read_text())
        rewritten = 0
        with tempfile.TemporaryDirectory(prefix="ifcn-benchmark-rewrite-") as tmp, patch.dict(os.environ, {"IFCN_PARITY_CANONICALIZATION": "auto"}):
            for case in manifest["cases"]:
                source = (C.ROOT / case["frozen_relpath"]).read_bytes()
                original = C.analyze(source)
                normalized, metadata = frontend.normalize_verilog(source.decode())
                normalized_analysis = C.analyze(normalized.encode())
                self.assertEqual(metadata["inputs"], case["input_ports"])
                self.assertEqual(metadata["outputs"], case["output_ports"])
                self.assertEqual(normalized_analysis["truth_table"], original["truth_table"], case["id"])
                normalized_path = Path(tmp) / "normalized.v"
                normalized_path.write_text(normalized)
                rewritten_path = namespace["build_verified_parity_canonical_verilog"](str(normalized_path), tmp)
                if rewritten_path is not None:
                    rewritten += 1
                    rewritten_analysis = C.analyze(Path(rewritten_path).read_bytes())
                    self.assertEqual(rewritten_analysis["input_ports"], case["input_ports"])
                    self.assertEqual(rewritten_analysis["output_ports"], case["output_ports"])
                    self.assertEqual(rewritten_analysis["truth_table"], original["truth_table"], case["id"])
        self.assertEqual(rewritten, 10)

    def test_yosys_syntax_and_structural_checks(self):
        yosys = C.ROOT / "build/tools/yosys-local/usr/bin/yosys"
        if not yosys.is_file():
            self.skipTest("Optional existing local Yosys runtime is not present")
        prefix = yosys.parent.parent
        env = dict(os.environ)
        libraries = [prefix / "lib", prefix / "lib64", *sorted((prefix / "lib").glob("*-linux-gnu"))]
        env["LD_LIBRARY_PATH"] = os.pathsep.join([str(path) for path in libraries if path.is_dir()] + ([env["LD_LIBRARY_PATH"]] if env.get("LD_LIBRARY_PATH") else []))
        env["YOSYS_DATDIR"] = str(prefix / "share/yosys")
        tcl = sorted(prefix.glob("share/tcltk/tcl*/init.tcl"), reverse=True)
        if tcl:
            env["TCL_LIBRARY"] = str(tcl[0].parent)
        manifest = json.loads((C.SNAPSHOT / "manifest.json").read_text())
        for case in manifest["cases"]:
            path = C.ROOT / case["frozen_relpath"]
            command = [str(yosys), "-Q", "-T", "-p", f'read_verilog "{path}"; hierarchy -check -top top; proc; check -assert']
            result = subprocess.run(command, capture_output=True, text=True, timeout=20, env=env)
            self.assertEqual(result.returncode, 0, f"{case['id']}: {result.stdout}\n{result.stderr}")


if __name__ == "__main__":
    unittest.main(verbosity=2)
