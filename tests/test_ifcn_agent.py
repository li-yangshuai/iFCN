import ast
import itertools
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
from ifcn_agent_frontend import normalize_verilog
from ifcn_agent import check_pnr, energy_summary, execute, qca_interface_report, run_pnr


def evaluate(node, values):
    if isinstance(node, ast.Name):
        return values[node.id]
    if isinstance(node, ast.UnaryOp):
        return 1 - evaluate(node.operand, values)
    a, b = evaluate(node.left, values), evaluate(node.right, values)
    return a & b if isinstance(node.op, ast.BitAnd) else a | b if isinstance(node.op, ast.BitOr) else a ^ b


class FrontendTests(unittest.TestCase):
    def test_lowering_preserves_truth_tables(self):
        expressions = ["a ^ b", "~(a | b) & c", "(a & b) | (a & c) | (b & c)",
                       "a | b & c", "~a ^ (b & ~c)", "a"]
        for expression in expressions:
            normalized, _ = normalize_verilog(f"module m(input a, b, c, output y); assign y={expression}; endmodule")
            for bits in itertools.product((0, 1), repeat=3):
                values = dict(zip(("a", "b", "c"), bits))
                expected = evaluate(ast.parse(expression, mode="eval").body, values)
                for name, expr in re.findall(r"assign (\w+) = (.*);", normalized):
                    values[name] = evaluate(ast.parse(expr, mode="eval").body, values)
                self.assertEqual(expected, values["y"], (expression, bits))

    def test_rejects_unsupported_or_invalid_rtl(self):
        for source in [
            "module m(input a, output y); always @* y=a; endmodule",
            "module m(input [3:0] a, output y); assign y=a; endmodule",
            "module m(input a, output y); assign y=missing; endmodule",
            "module m(input a, output y); assign y=1; endmodule",
            "module m(input a, output y); assign y=y & a; endmodule",
            "module m(input a, output y); assign y=a; assign y=~a; endmodule",
            "module m(input a, output y); wire w; assign y=w; endmodule",
            "module m(input a, output y); assign y=a && a; endmodule",
        ]:
            with self.subTest(source=source), self.assertRaises((ValueError, SyntaxError)):
                normalize_verilog(source)

    def test_topological_order_and_comments(self):
        normalized, metadata = normalize_verilog("""// example
module m(a,y); input a; output y; wire w;
assign y=~w; /* order is intentionally reversed */ assign w=~a;
endmodule""")
        self.assertLess(normalized.index("assign w"), normalized.index("assign y"))
        self.assertEqual(metadata["inputs"], ["a"])

    def test_partial_pnr_is_never_accepted(self):
        for record in [{}, {"layout_legal": True, "failed_edge_count": 1, "clock_template_ok": True},
                       {"layout_legal": True, "failed_edge_count": 0, "clock_template_ok": False},
                       {"layout_legal": True, "failed_edge_count": 0, "clock_legal": True,
                        "validation": {"passed": False}, "clock_template_ok": True}]:
            with self.assertRaises(ValueError):
                check_pnr(record)

    def test_irregular_clock_does_not_require_regular_template(self):
        check_pnr({"layout_legal": True, "failed_edge_count": 0, "clock_legal": True,
                   "clock_template_ok": None, "validation": {"passed": True}})

    def test_missing_device_output_cannot_pass(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            qca = root / "device.qca"
            qca.write_text("[TYPE:QCADCell]\ncell_function=QCAD_CELL_INPUT\n"
                           "[TYPE:QCADCell]\ncell_function=QCAD_CELL_OUTPUT\n")
            with self.assertRaisesRegex(ValueError, "expected 1 inputs/2 outputs, got 1 inputs/1 outputs"):
                qca_interface_report(qca, {"inputs": ["a"], "outputs": ["x", "y"]}, root / "interface.json")
            report = json.loads((root / "interface.json").read_text())
            self.assertFalse(report["passed"])

    def test_negative_energy_is_diagnostic_only(self):
        with tempfile.TemporaryDirectory() as tmp:
            report = Path(tmp) / "energy.txt"
            report.write_text("""[ENERGY_ANALYSIS]
available=TRUE
cycle_count=1
total_bath_eV=-1
average_bath_eV=-1
total_bath_clock_eV=-2
average_bath_clock_eV=-2
total_error_eV=3
[PER_CYCLE]
cycle,E_bath_eV
2,-1
[#PER_CYCLE]
[#ENERGY_ANALYSIS]
""")
            data = energy_summary(report, 1e-11)
            self.assertEqual(data["numerical_status"], "invalid_negative_bath_energy")
            self.assertIsNone(data["average_dissipated_power_W"])


class AlgorithmPolicyTests(unittest.TestCase):
    def test_exited_backend_cannot_leave_ignoring_grandchild(self):
        with tempfile.TemporaryDirectory(prefix="ifcn-descendant-test-") as tmp:
            root = Path(tmp)
            pidfile = root / "grandchild.pid"
            script = root / "parent.py"
            script.write_text("""import pathlib, subprocess, sys, time
p=pathlib.Path(sys.argv[1])
subprocess.Popen([sys.executable,'-c',
 'import os,pathlib,signal,sys,time; signal.signal(signal.SIGTERM,signal.SIG_IGN); pathlib.Path(sys.argv[1]).write_text(str(os.getpid())); time.sleep(30)',str(p)])
for i in range(100):
 if p.exists(): break
 time.sleep(.01)
""")
            manifest = {"configuration": {}, "parameters": {"timeout_s": 5},
                        "stages": {"pnr": {"log": "stage.log"}}}
            try:
                execute([sys.executable, script, pidfile], root, "pnr", manifest)
                pid = int(pidfile.read_text())
                for _ in range(30):
                    stat = Path(f"/proc/{pid}/stat")
                    if not stat.exists() or stat.read_text().split()[2] == "Z":
                        break
                    time.sleep(.01)
                else:
                    self.fail("Backend descendant survived process-group cleanup")
            finally:
                if pidfile.exists():
                    try:
                        os.kill(int(pidfile.read_text()), 9)
                    except ProcessLookupError:
                        pass

    def run_policy(self, algorithm, failure, expect_error=False):
        with tempfile.TemporaryDirectory(prefix="ifcn-policy-test-") as tmp:
            root = Path(tmp)
            out = root / "02_pnr_1"
            out.mkdir()
            source = "module m(input a, output y); assign y=~a; endmodule"
            (root / "normalized.v").write_text(source)
            (root / "source.v").write_text(source)
            manifest = {"configuration": {"python": sys.executable, "native_pnr_binary": sys.executable,
                                          "backend": "unused", "energy_binary": "fixture-energy"},
                        "parameters": {"algorithm": algorithm, "timeout_s": 30, "seed": 1},
                        "artifacts": {"normalized.v": {"path": "normalized.v"}, "source.v": {"path": "source.v"}},
                        "frontend": {"inputs": ["a"], "outputs": ["y"]}, "metrics": {}, "validation": {}}
            calls = []
            def backend(command, path, step, state, timeout_s):
                if command[0] == "fixture-energy":
                    Path(str(command[2]) + "_energy_input.qca").write_text(
                        "[TYPE:QCADCell]\ncell_function=QCAD_CELL_INPUT\n"
                        "[TYPE:QCADCell]\ncell_function=QCAD_CELL_OUTPUT\n")
                    return
                selected = command[command.index("--algorithm") + 1]
                calls.append(selected)
                if selected == "normal_2ddwave":
                    raise failure("first backend failed")
                target = Path(command[command.index("--output") + 1])
                (target / "layout.ifcn").write_text("fixture layout")
                (target / "routed_dag.json").write_text(json.dumps({"schema": "ifcn.dag.v1",
                    "nodes": [{"id": 0, "name": "a", "type": "input", "is_input": True, "is_output": False},
                              {"id": 1, "name": "y", "type": "not", "is_input": False, "is_output": True}],
                    "edges": [[0, 1]], "input_count": 1, "output_count": 1}))
                (target / "pnr.json").write_text(json.dumps({"algorithm": selected, "layout_legal": True,
                    "failed_edge_count": 0, "clock_legal": True, "clock_template_ok": None,
                    "validation": {"passed": True}, "ifcn": str(target / "layout.ifcn")}))
            with patch("ifcn_agent.execute", backend):
                if expect_error:
                    with self.assertRaises(failure):
                        run_pnr(out, root, manifest)
                else:
                    run_pnr(out, root, manifest)
            report = json.loads((out / "pnr_attempts.json").read_text())
            return calls, report, manifest

    def test_auto_preserves_failure_and_stops_at_first_valid_result(self):
        calls, report, manifest = self.run_policy("auto", RuntimeError)
        self.assertEqual(calls, ["normal_2ddwave", "compact"])
        self.assertEqual(report["selected_algorithm"], "compact")
        self.assertEqual([item["status"] for item in report["attempts"]], ["failed", "completed"])
        self.assertIn("first backend failed", report["attempts"][0]["error"])
        self.assertIn("layout.ifcn", manifest["artifacts"])

    def test_explicit_algorithm_failure_never_switches(self):
        calls, report, _ = self.run_policy("normal_2ddwave", RuntimeError, expect_error=True)
        self.assertEqual(calls, ["normal_2ddwave"])
        self.assertIsNone(report["selected_algorithm"])

    def test_cancel_never_falls_back(self):
        calls, report, _ = self.run_policy("auto", InterruptedError, expect_error=True)
        self.assertEqual(calls, ["normal_2ddwave"])
        self.assertEqual(report["attempts"][0]["status"], "cancelled")


class JobTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="ifcn-agent-test-")
        self.path = Path(self.temporary.name)
        self.runs = self.path / "runs"
        self.source = self.path / "with space.v"
        self.source.write_text("module m(input a, output y); assign y=~a; endmodule")
        fake = self.path / "fake-python"
        fake.write_text("""#!/usr/bin/python3
import json, pathlib, sys, time
p=pathlib.Path(__file__).parent
if (p/'pause').exists(): time.sleep(5)
out=pathlib.Path(sys.argv[sys.argv.index('--output')+1])
(out/'dag.json').write_text(json.dumps({'input_count':1,'output_count':1}))
(out/'dag.dot').write_text('digraph {}')
""")
        fake.chmod(0o700)
        self.env = dict(os.environ, IFCN_PYTHON=str(fake))
        self.run_id = None

    def command(self, *args, ok=True):
        run = subprocess.run([sys.executable, str(ROOT / "scripts/ifcn_agent.py"),
                              "--runs-dir", str(self.runs), *args], env=self.env,
                             capture_output=True, text=True, timeout=10)
        if ok:
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
        else:
            self.assertNotEqual(run.returncode, 0)
        return json.loads(run.stdout)

    def start(self, timeout=20):
        result = self.command("start", str(self.source), "--until", "parse", "--timeout", str(timeout))
        self.run_id = result["run_id"]
        return result

    def wait(self):
        for _ in range(100):
            result = self.command("status", self.run_id)
            if not result["active"]:
                return result
            time.sleep(0.1)
        self.fail("Worker did not settle.")

    def tearDown(self):
        if self.run_id:
            self.command("cancel", self.run_id)
            self.wait()
        self.temporary.cleanup()

    def test_snapshots_and_tamper_guard(self):
        self.start()
        self.source.write_text("changed after submission")
        result = self.wait()
        self.assertEqual(result["status"], "completed")
        artifact = self.command("read", self.run_id, "source.v")
        self.assertIn("assign y=~a", artifact["content"])
        self.command("resume", self.run_id, "--until", "parse")
        self.assertEqual(self.wait()["stages"]["parse"]["attempt"], 1)
        dag = self.runs / self.run_id / result["artifacts"]["dag.json"]["path"]
        dag.write_text("tampered")
        self.command("resume", self.run_id, "--until", "parse", ok=False)
        self.command("read", self.run_id, "../../etc/passwd", ok=False)

    def test_cancel_lock_and_resume(self):
        pause = self.path / "pause"
        pause.touch()
        self.start()
        self.command("resume", self.run_id, "--until", "parse", ok=False)
        time.sleep(0.3)
        self.command("cancel", self.run_id)
        self.assertEqual(self.wait()["status"], "cancelled")
        pause.unlink()
        self.command("resume", self.run_id, "--until", "parse")
        result = self.wait()
        self.assertEqual(result["status"], "completed")
        self.assertEqual(result["stages"]["parse"]["attempt"], 2)

    def test_timeout_and_resume(self):
        pause = self.path / "pause"
        pause.touch()
        self.start(timeout=1)
        self.assertEqual(self.wait()["status"], "failed")
        pause.unlink()
        self.command("resume", self.run_id, "--until", "parse")
        self.assertEqual(self.wait()["status"], "completed")

    def test_legacy_pnr_resume_cannot_skip_logic_guard(self):
        self.start()
        self.wait()
        path = self.runs / self.run_id / "manifest.json"
        manifest = json.loads(path.read_text())
        manifest["stages"]["pnr"] = {"status": "completed", "attempt": 1}
        # A valid hash alone is insufficient: the old graph lacks explicit
        # logic/port information and must not be approved by a PNR-only resume.
        manifest["artifacts"]["routed_dag.json"] = manifest["artifacts"]["dag.json"]
        path.write_text(json.dumps(manifest))
        self.command("resume", self.run_id, "--until", "pnr")
        state = self.wait()
        self.assertEqual(state["status"], "failed")
        self.assertIn("Routed logic validation unsupported", state["error"])


if __name__ == "__main__":
    unittest.main()
