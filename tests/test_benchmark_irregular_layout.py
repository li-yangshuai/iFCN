"""Benchmark boundaries must reject invalid/unfinished artifacts and references."""
from copy import deepcopy
from contextlib import redirect_stdout
import csv
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests/benchmarks"))
import benchmark_irregular_layout as benchmark


def qca_text(labels=("a", "b", "y")):
    return "".join("[TYPE:QCADCell]\nx=0\ny=0\ncell_options.clock=0\n"
                   f"cell_function=QCAD_CELL_{kind}\npsz={label}\n[#TYPE:QCADCell]\n"
                   for kind, label in zip(("INPUT", "INPUT", "OUTPUT"), labels))


class BenchmarkTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)
        self.source = self.directory / "xor2.v"
        self.source.write_text("module top(input a,input b,output y); assign y=a^b; endmodule\n")
        self.item = {"case": "TOY__xor2", "family": "TOY", "source": "tests/benchmarks_f/TOY/xor2.v",
                     "source_path": str(self.source), "source_sha256": benchmark.sha256(self.source)}
        self.candidate = json.loads((ROOT / "tests/fixtures/routing/xor2_fixed_clock.json").read_text())
        for node in self.candidate["nodes"]:
            if node["is_output"]:
                node["name"] = "y"
        self.candidate.update(routed=True, native_mapping_valid=True, width=1, height=1, area=1)

    def execute(self, candidate=None, device=None, timeout=False, export_candidates=False):
        candidate = self.candidate if candidate is None else candidate
        def fake_command(command, directory, name, budget, env):
            self.assertEqual(budget, 11.0)  # Soft budget=1, hard process bound=11.
            # Only subprocess artifacts are mocked: production validation,
            # source truth and IFCN serialization all execute unchanged.
            if name == "pnr":
                self.assertEqual(command[1], "irregular")
                self.assertEqual(command[4:8], ["--budget", "1.0", "--attempts", "320"])
                if export_candidates:
                    pool = directory / "candidates"
                    self.assertEqual(command[8:], ["--candidates-dir", pool])
                    pool.mkdir()
                    (pool / "candidate_000001.json").write_text(json.dumps(candidate))
                else:
                    self.assertEqual(len(command), 8)
                (directory / "candidate.json").write_text(json.dumps(candidate))
            else:
                self.assertEqual(command[-1], "--qca-only")
                (directory / "mapped_energy_input.qca").write_text(qca_text() if device is None else device)
            return {"command": list(map(str, command)), "exit_code": 0,
                    "timed_out": timeout and name == "pnr", "seconds": 0.01}
        with patch.object(benchmark, "command_result", side_effect=fake_command):
            return benchmark.run_case(self.item, self.directory / "run",
                                      {name: Path(name) for name in benchmark.BINARY_FILES},
                                      1.0, 320, os.environ.copy(), export_candidates=export_candidates)

    def test_accepts_only_computed_area_and_actual_exported_cell_count(self):
        result = self.execute()
        self.assertEqual(result["status"], "validated", result)
        self.assertTrue(result["eligible_for_area_comparison"])
        self.assertEqual(result["area_tiles"], 12)  # Ignores forged area=1 in native metadata.
        self.assertEqual(result["qca_cells"], 3)  # Counts the mocked mapper artifact, not candidate metadata.
        self.assertEqual(result["vectors_checked"], 4)
        self.assertEqual(result["physical_waveform_status"], "not_run")
        self.assertNotIn("candidate_pool_directory", result)
        self.assertFalse((self.directory / "run/cases/TOY__xor2/candidates").exists())

    def test_candidate_pool_is_retained_but_cannot_make_a_timeout_eligible(self):
        result = self.execute(timeout=True, export_candidates=True)
        self.assertEqual(result["status"], "timeout")
        self.assertFalse(result["eligible_for_area_comparison"])
        self.assertTrue(result["candidate_pool_requires_validation"])
        pool = Path(result["candidate_pool_directory"])
        self.assertEqual(pool, self.directory / "run/cases/TOY__xor2/candidates")
        self.assertEqual(benchmark.read_json(pool / "candidate_000001.json"), self.candidate)

    def test_timeout_cannot_accept_even_a_complete_valid_candidate_file(self):
        result = self.execute(timeout=True)
        self.assertEqual(result["status"], "timeout")
        self.assertFalse(result["eligible_for_area_comparison"])
        self.assertNotIn("area_tiles", result)
        self.assertEqual(len(result["commands"]), 1)

    def test_native_success_flags_cannot_hide_invalid_clock(self):
        candidate = deepcopy(self.candidate)
        for cell in candidate["cells"]:
            if (cell["x"], cell["y"]) == (1, 1):
                cell["phase"] = 1
        result = self.execute(candidate)
        self.assertEqual(result["status"], "drc_rejected")
        self.assertFalse(result["eligible_for_area_comparison"])
        self.assertNotIn("area_tiles", result)

    def test_routed_dag_is_compared_against_original_source_truth(self):
        self.source.write_text("module top(input a,input b,output y); assign y=~(a^b); endmodule\n")
        self.item["source_sha256"] = benchmark.sha256(self.source)
        result = self.execute()
        self.assertEqual(result["status"], "logic_rejected")
        self.assertEqual(result["source_dag_status"], "not_equivalent")
        self.assertFalse(result["eligible_for_area_comparison"])

    def test_wrong_or_duplicate_physical_terminal_labels_fail(self):
        result = self.execute(device=qca_text(("a", "b", "wrong_output")))
        self.assertEqual(result["status"], "device_rejected")
        self.assertFalse(result["eligible_for_area_comparison"])
        self.assertNotIn("qca_cells", result)
        logic = {"input_mapping": [{"source_port": "a"}, {"source_port": "b"}],
                 "output_mapping": [{"dag_node_name": "y"}]}
        device = self.directory / "duplicate.qca"
        device.write_text(qca_text(("a", "a", "y")))
        with self.assertRaisesRegex(ValueError, "Duplicate"):
            benchmark.qca_interface(device, logic)

    def test_nonfinite_json_and_physical_coordinates_are_rejected(self):
        path = self.directory / "bad.json"
        for value in ("NaN", "Infinity", "1e999"):
            path.write_text('{"value":' + value + '}')
            with self.assertRaises(ValueError):
                benchmark.read_json(path)
        result = self.execute(device=qca_text().replace("x=0", "x=nan", 1))
        self.assertEqual(result["status"], "device_rejected")

    def test_inventory_preserves_duplicate_paths(self):
        root = self.directory / "benchmarks"
        for family, name in (("TOY", "same.v"), ("MAJ", "same.v"), ("TOY", "nested/copy.v")):
            target = root / family / name
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(self.source.read_bytes())
        items = benchmark.inventory(root)
        self.assertEqual(len(items), 3)
        self.assertEqual(len({item["case"] for item in items}), 3)
        self.assertTrue(all(len(item["same_content_cases"]) == 3 for item in items))

    def test_failed_and_physically_failed_references_cannot_win(self):
        path = self.directory / "reference.csv"
        fields = ["case", "status", "eligible_for_area_comparison", "area_tiles", "qca_cells", "physical_waveform_status"]
        with path.open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=fields)
            writer.writeheader()
            for status, eligible, area, physical in (("timeout", True, 1, "not_run"),
                                                     ("validated", True, 2, "failed"),
                                                     ("validated", False, 3, "passed"),
                                                     ("validated", True, 12, "not_run")):
                writer.writerow(dict(zip(fields, ("TOY__xor2", status, eligible, area, 79, physical))))
        self.assertEqual(benchmark.references(path)["TOY__xor2"]["area_tiles"], 12)

    def test_real_subprocess_timeout_kills_its_group(self):
        result = benchmark.command_result([sys.executable, "-c", "import time; time.sleep(5)"],
                                           self.directory, "timeout", 0.05, os.environ.copy())
        self.assertTrue(result["timed_out"])
        self.assertNotEqual(result["exit_code"], 0)
        self.assertLess(result["seconds"], 2)

    def test_corrected_mapping_reference_overrides_the_original_interface_failure(self):
        path = self.directory / "corrected.csv"
        path.write_text("case,status,eligible,area_tiles,qca_cells,corrected_mapping_status,corrected_eligible,corrected_area_tiles,corrected_qca_cells\n"
                        "TOY__clpl,rejected,False,588,1088,passed,True,588,1088\n"
                        "TOY__bad,passed,True,1,1,rejected,False,1,1\n")
        result = benchmark.references(path)
        self.assertEqual(set(result), {"TOY__clpl"})
        self.assertEqual(result["TOY__clpl"]["qca_cells"], 1088)

    def test_complete_cli_preserves_each_path_and_snapshots_its_tools(self):
        root = self.directory / "repository"
        for family in ("TOY", "MAJ"):
            fixture = root / "tests/benchmarks_f" / family / "xor.v"
            fixture.parent.mkdir(parents=True)
            fixture.write_bytes(self.source.read_bytes())
        build = self.directory / "build"
        build.mkdir()
        pnr = build / "ifcn_combinational_pnr"
        mapper = build / "ifcn_energy_analysis"
        pnr.write_text("#!" + sys.executable + "\nimport pathlib,sys\n"
                       "assert sys.argv[1]=='irregular'\n"
                       "if '--candidates-dir' in sys.argv:\n"
                       "    pool=pathlib.Path(sys.argv[sys.argv.index('--candidates-dir')+1])\n"
                       "    pool.mkdir()\n"
                       "    (pool/'candidate_000001.json').write_text(" + repr(json.dumps(self.candidate)) + ")\n"
                       "pathlib.Path(sys.argv[3]).write_text(" + repr(json.dumps(self.candidate)) + ")\n")
        mapper.write_text("#!" + sys.executable + "\nimport pathlib,sys\n"
                          "assert sys.argv[3]=='--qca-only'\n"
                          "pathlib.Path(sys.argv[2]+'_energy_input.qca').write_text(" + repr(qca_text()) + ")\n")
        pnr.chmod(0o755)
        mapper.chmod(0o755)
        output = self.directory / "complete-run"
        with patch.object(benchmark, "ROOT", root), redirect_stdout(io.StringIO()):
            code = benchmark.main(["--build-dir", str(build), "--output-dir", str(output),
                                   "--jobs", "2", "--budget", "2", "--attempts", "320"])
        self.assertEqual(code, 0)
        records = benchmark.read_json(output / "summary.json")
        self.assertEqual({row["case"] for row in records}, {"TOY__xor", "MAJ__xor"})
        self.assertTrue(all(row["eligible_for_area_comparison"] for row in records))
        totals = benchmark.read_json(output / "totals.json")
        self.assertTrue(totals["frozen_files_unchanged"])
        self.assertEqual(totals["validated"], 2)
        self.assertFalse(benchmark.read_json(output / "manifest.json")["export_candidates"])
        self.assertTrue(all("--candidates-dir" not in row["commands"][0]["command"] for row in records))

        exported = self.directory / "exported-run"
        with patch.object(benchmark, "ROOT", root), redirect_stdout(io.StringIO()):
            code = benchmark.main(["--build-dir", str(build), "--output-dir", str(exported),
                                   "--jobs", "2", "--budget", "2", "--export-candidates"])
        self.assertEqual(code, 0)
        self.assertTrue(benchmark.read_json(exported / "manifest.json")["export_candidates"])
        for row in benchmark.read_json(exported / "summary.json"):
            pool = exported / "cases" / row["case"] / "candidates"
            self.assertEqual(row["commands"][0]["command"][-2:], ["--candidates-dir", str(pool)])
            self.assertTrue(row["eligible_for_area_comparison"])
            self.assertTrue(row["candidate_pool_requires_validation"])
            self.assertEqual(benchmark.read_json(pool / "candidate_000001.json"), self.candidate)


if __name__ == "__main__":
    unittest.main()
