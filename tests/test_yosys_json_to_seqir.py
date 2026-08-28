#!/usr/bin/env python3
"""Offline contract tests for scripts/yosys_json_to_seqir.py."""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from typing import Any


REPOSITORY = Path(__file__).resolve().parents[1]
SCRIPT = REPOSITORY / "scripts" / "yosys_json_to_seqir.py"
FIXTURE = REPOSITORY / "tests" / "fixtures" / "sequential" / "toggle_ff.semantic.yosys.json"
CANONICAL_FIXTURE = (
    REPOSITORY / "tests" / "fixtures" / "sequential" / "toggle_ff.canonical.yosys.json"
)

sys.path.insert(0, str(SCRIPT.parent))
import yosys_json_to_seqir as importer  # noqa: E402


def _load_fixture(path: Path = FIXTURE) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def _reverse_object_order(value: Any) -> Any:
    if isinstance(value, dict):
        return {
            key: _reverse_object_order(child)
            for key, child in reversed(list(value.items()))
        }
    if isinstance(value, list):
        return [_reverse_object_order(child) for child in value]
    return value


def _module(document: dict[str, Any]) -> dict[str, Any]:
    return document["modules"]["toggle_ff"]


def _register_cell(document: dict[str, Any]) -> dict[str, Any]:
    cells = _module(document)["cells"]
    return next(cell for cell in cells.values() if cell["type"].startswith("$s"))


def _add_enable(document: dict[str, Any], cell_type: str) -> None:
    module = _module(document)
    module["ports"]["en"] = {"direction": "input", "bits": [6]}
    module["netnames"]["en"] = {
        "hide_name": 0,
        "bits": [6],
        "attributes": {},
    }
    register = _register_cell(document)
    register["type"] = cell_type
    register["parameters"]["EN_POLARITY"] = "1"
    register["connections"]["EN"] = [6]
    register["port_directions"]["EN"] = "input"


def _simple_gate_document() -> dict[str, Any]:
    """A lowered primitive netlist exercising every supported gate operation."""
    cells: dict[str, Any] = {
        "not_gate": {
            "type": "$_NOT_",
            "parameters": {},
            "connections": {"A": [3], "Y": [7]},
        },
        "and_gate": {
            "type": "$_AND_",
            "parameters": {},
            "connections": {"A": [7], "B": [4], "Y": [8]},
        },
        "or_gate": {
            "type": "$_OR_",
            "parameters": {},
            "connections": {"A": [8], "B": [6], "Y": [9]},
        },
        "xor_gate": {
            "type": "$_XOR_",
            "parameters": {},
            "connections": {"A": [9], "B": [4], "Y": [10]},
        },
        "mux_gate": {
            "type": "$_MUX_",
            "parameters": {},
            "connections": {"A": [10], "B": [3], "S": [5], "Y": [11]},
        },
        "state": {
            "type": "$_DFF_P_",
            "parameters": {},
            "connections": {"C": [2], "D": [11], "Q": [6]},
        },
    }
    return {
        "creator": "offline lowered primitive fixture",
        "modules": {
            "gate_chain": {
                "attributes": {"top": "1"},
                "ports": {
                    "clk": {"direction": "input", "bits": [2]},
                    "a": {"direction": "input", "bits": [3]},
                    "b": {"direction": "input", "bits": [4]},
                    "sel": {"direction": "input", "bits": [5]},
                    "q": {"direction": "output", "bits": [6]},
                },
                "cells": cells,
                "netnames": {
                    "clk": {"hide_name": 0, "bits": [2]},
                    "a": {"hide_name": 0, "bits": [3]},
                    "b": {"hide_name": 0, "bits": [4]},
                    "sel": {"hide_name": 0, "bits": [5]},
                    "q": {"hide_name": 0, "bits": [6]},
                },
            }
        },
    }


class YosysJsonToSeqirTest(unittest.TestCase):
    def test_toggle_ff_cuts_state_and_lowers_synchronous_reset(self) -> None:
        result = importer.import_yosys_json(_load_fixture())

        self.assertEqual(result["schema"], "ifcn.seqir.v0")
        self.assertEqual(result["module"], "toggle_ff")
        self.assertEqual(
            result["clock_domains"],
            [{"id": "clock.0000", "signal": "clk", "edge": "posedge"}],
        )
        roles = {port["id"]: port["roles"] for port in result["ports"]}
        self.assertEqual(roles["clk"], ["logical_clock"])
        self.assertEqual(roles["rst"], ["synchronous_reset"])
        self.assertEqual(roles["q"], [])

        self.assertEqual(len(result["registers"]), 1)
        register = result["registers"][0]
        self.assertEqual(register["q"], "q")
        self.assertEqual(register["d"], "internal.reg.0000.d_reset")
        self.assertEqual(register["source"]["type"], "$sdff")
        self.assertTrue(register["controls_lowered_into_d"])
        self.assertEqual(register["control_priority"], ["reset", "data"])
        self.assertEqual(
            register["control_provenance"]["reset"],
            {
                "signal": "rst",
                "kind": "synchronous",
                "polarity": "active_high",
                "value": 0,
            },
        )

        nodes = result["combinational_nodes"]
        self.assertEqual([node["op"] for node in nodes], ["not", "mux"])
        self.assertEqual(nodes[0]["inputs"], {"A": "q"})
        self.assertEqual(nodes[0]["output"], "bit.5")
        self.assertEqual(nodes[1]["source"]["lowering"], "synchronous_reset")
        self.assertEqual(
            nodes[1]["inputs"],
            {"A": "bit.5", "B": "const.0", "S": "rst"},
        )
        self.assertEqual(
            result["state_edges"],
            [{
                "id": "state.0000",
                "data": "internal.reg.0000.d_reset",
                "to": "q",
                "register": "reg.0000",
                "logical_latency_ticks": 1,
            }],
        )
        self.assertTrue(result["comb_regions"][0]["must_be_dag"])
        self.assertEqual(result["diagnostics"]["control_muxes_inserted"], 1)

    def test_semantic_json_key_order_does_not_change_output(self) -> None:
        document = _load_fixture()
        expected = importer._render_json(importer.import_yosys_json(document))
        actual = importer._render_json(
            importer.import_yosys_json(_reverse_object_order(document))
        )
        self.assertEqual(actual, expected)

    def test_sdffe_lowers_enable_inside_reset_for_reset_priority(self) -> None:
        document = _load_fixture()
        _add_enable(document, "$sdffe")
        result = importer.import_yosys_json(document)

        register = result["registers"][0]
        self.assertEqual(
            register["control_priority"], ["reset", "enable", "data_or_hold"]
        )
        by_lowering = {
            node["source"].get("lowering"): node
            for node in result["combinational_nodes"]
            if "lowering" in node["source"]
        }
        enable = by_lowering["enable"]
        reset = by_lowering["synchronous_reset"]
        self.assertEqual(enable["inputs"], {"A": "q", "B": "bit.5", "S": "en"})
        self.assertEqual(reset["inputs"]["A"], enable["output"])
        self.assertEqual(reset["inputs"]["B"], "const.0")
        self.assertEqual(register["d"], reset["output"])
        self.assertEqual(result["diagnostics"]["control_muxes_inserted"], 2)

    def test_sdffce_lowers_reset_inside_enable_for_enable_priority(self) -> None:
        document = _load_fixture()
        _add_enable(document, "$sdffce")
        result = importer.import_yosys_json(document)

        register = result["registers"][0]
        self.assertEqual(
            register["control_priority"], ["enable", "reset", "data_or_hold"]
        )
        by_lowering = {
            node["source"].get("lowering"): node
            for node in result["combinational_nodes"]
            if "lowering" in node["source"]
        }
        reset = by_lowering["synchronous_reset"]
        enable = by_lowering["enable"]
        self.assertEqual(reset["inputs"], {"A": "bit.5", "B": "const.0", "S": "rst"})
        self.assertEqual(enable["inputs"]["A"], "q")
        self.assertEqual(enable["inputs"]["B"], reset["output"])
        self.assertEqual(register["d"], enable["output"])

    def test_active_low_dffe_swaps_mux_arms_without_adding_an_inverter(self) -> None:
        document = _load_fixture()
        _add_enable(document, "$dffe")
        register_cell = next(
            cell for cell in _module(document)["cells"].values() if cell["type"] == "$dffe"
        )
        register_cell["parameters"]["EN_POLARITY"] = "0"
        result = importer.import_yosys_json(document)

        register = result["registers"][0]
        enable = next(
            node
            for node in result["combinational_nodes"]
            if node["source"].get("lowering") == "enable"
        )
        self.assertEqual(enable["inputs"], {"A": "bit.5", "B": "q", "S": "en"})
        self.assertEqual(register["d"], enable["output"])
        self.assertEqual(register["control_priority"], ["enable", "data_or_hold"])
        self.assertEqual(
            register["control_provenance"]["enable"]["polarity"], "active_low"
        )
        self.assertNotIn("synchronous_reset", {
            node["source"].get("lowering") for node in result["combinational_nodes"]
        })

    def test_lowered_simple_cells_cover_supported_gate_set(self) -> None:
        result = importer.import_yosys_json(_simple_gate_document())

        self.assertEqual(result["module"], "gate_chain")
        self.assertEqual(
            [node["op"] for node in result["combinational_nodes"]],
            ["not", "and", "or", "xor", "mux"],
        )
        self.assertEqual(result["registers"][0]["source"]["type"], "$_DFF_P_")
        self.assertFalse(result["registers"][0]["controls_lowered_into_d"])
        self.assertEqual(result["diagnostics"]["control_muxes_inserted"], 0)

    def test_real_yosys_toggle_emits_strict_legacy_cut_and_state_manifest(self) -> None:
        seqir = importer.import_yosys_json(_load_fixture(CANONICAL_FIXTURE))
        legacy = importer.build_legacy_cut_artifacts(seqir)

        self.assertEqual(
            legacy.verilog,
            "\n".join([
                "module ifcn_cut_toggle_ff(ifcn_q_0000,ifcn_pi_rst_0000,ifcn_d_0000);",
                "input ifcn_q_0000,ifcn_pi_rst_0000;",
                "output ifcn_d_0000;",
                "wire ifcn_d_0000,ifcn_n_0000;",
                "assign ifcn_n_0000=ifcn_pi_rst_0000|ifcn_q_0000;",
                "assign ifcn_d_0000=~ifcn_n_0000;",
                "endmodule",
                "",
            ]),
        )
        self.assertNotIn("clk", legacy.verilog)
        manifest = legacy.state_manifest
        self.assertEqual(manifest["schema"], "ifcn.state_manifest.v0")
        self.assertEqual(manifest["cut_dag"]["clock_signals_excluded"], ["clk"])
        self.assertEqual(
            manifest["state_boundaries"][0]["data_event"], "ifcn_d_0000"
        )
        self.assertEqual(
            manifest["state_boundaries"][0]["q_event"], "ifcn_q_0000"
        )
        self.assertEqual(
            manifest["ifcn_sequential_pnr"]["argv"],
            ["--state", "ifcn_d_0000:ifcn_q_0000"],
        )

    def test_xor_and_mux_are_lowered_to_single_operation_aoi_assignments(self) -> None:
        seqir = importer.import_yosys_json(_simple_gate_document())
        legacy = importer.build_legacy_cut_artifacts(seqir)

        self.assertNotIn("^", legacy.verilog)
        self.assertNotIn("?", legacy.verilog)
        assignments = [
            line for line in legacy.verilog.splitlines() if line.startswith("assign ")
        ]
        self.assertEqual(len(assignments), 12)
        for assignment in assignments:
            expression = assignment.split("=", 1)[1].removesuffix(";")
            binary_operators = expression.count("&") + expression.count("|")
            self.assertLessEqual(binary_operators, 1, assignment)
            if binary_operators:
                self.assertNotIn("~", expression, assignment)
        self.assertEqual(
            legacy.state_manifest["ifcn_sequential_pnr"]["state_arguments"],
            ["ifcn_d_0000:ifcn_q_0000"],
        )

    def test_compact_legacy_names_keep_tex_nodes_small(self) -> None:
        seqir = importer.import_yosys_json(_load_fixture(CANONICAL_FIXTURE))
        legacy = importer.build_legacy_cut_artifacts(seqir, compact_names=True)

        self.assertIn(
            "module ifcn_cut_toggle_ff(q0,i0,d0);\ninput q0,i0;\noutput d0;",
            legacy.verilog,
        )
        self.assertIn("assign n0=i0|q0;", legacy.verilog)
        self.assertIn("assign d0=~n0;", legacy.verilog)
        self.assertEqual(
            legacy.state_manifest["ifcn_sequential_pnr"]["state_arguments"],
            ["d0:q0"],
        )

    def test_legacy_cut_fails_instead_of_modeling_constant_as_primary_input(self) -> None:
        semantic_seqir = importer.import_yosys_json(_load_fixture())
        with self.assertRaisesRegex(
            importer.SeqirImportError,
            r"legacy Parse cannot represent constant const\.0",
        ):
            importer.build_legacy_cut_artifacts(semantic_seqir)

    def test_unknown_cell_fails_closed_with_name_and_type(self) -> None:
        document = _load_fixture()
        cell = next(
            cell for cell in _module(document)["cells"].values() if cell["type"] == "$not"
        )
        cell["type"] = "$add"

        with self.assertRaisesRegex(
            importer.SeqirImportError,
            r"unsupported Yosys cell .* of type '\$add'",
        ):
            importer.import_yosys_json(document)

    def test_falling_edge_register_fails_explicitly(self) -> None:
        document = _load_fixture()
        register = _register_cell(document)
        register["type"] = "$dff"
        register["parameters"]["CLK_POLARITY"] = "0"

        with self.assertRaisesRegex(
            importer.SeqirImportError, "not positive-edge triggered"
        ):
            importer.import_yosys_json(document)

    def test_cli_writes_identical_stable_json_and_does_not_write_on_error(self) -> None:
        expected = importer._render_json(importer.import_yosys_json(_load_fixture()))
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            output = directory / "toggle_ff.seqir.json"
            completed = subprocess.run(
                [sys.executable, str(SCRIPT), str(FIXTURE), "--top", "toggle_ff", "-o", str(output)],
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(completed.returncode, 0, completed.stderr)
            self.assertEqual(output.read_text(encoding="utf-8"), expected)

            bad_document = _load_fixture()
            next(iter(_module(bad_document)["cells"].values()))["type"] = "$add"
            bad_input = directory / "bad.yosys.json"
            bad_input.write_text(json.dumps(bad_document), encoding="utf-8")
            bad_output = directory / "must_not_exist.json"
            failed = subprocess.run(
                [sys.executable, str(SCRIPT), str(bad_input), "-o", str(bad_output)],
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(failed.returncode, 2)
            self.assertIn("unsupported Yosys cell", failed.stderr)
            self.assertFalse(bad_output.exists())

    def test_cli_can_emit_seqir_cut_and_manifest_together(self) -> None:
        document = _load_fixture(CANONICAL_FIXTURE)
        seqir = importer.import_yosys_json(document)
        legacy = importer.build_legacy_cut_artifacts(seqir)
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            seqir_path = directory / "toggle.seqir.json"
            cut_path = directory / "toggle.cut.v"
            manifest_path = directory / "toggle.state.json"
            completed = subprocess.run(
                [
                    sys.executable,
                    str(SCRIPT),
                    str(CANONICAL_FIXTURE),
                    "-o",
                    str(seqir_path),
                    "--legacy-cut-verilog",
                    str(cut_path),
                    "--state-manifest",
                    str(manifest_path),
                ],
                check=False,
                capture_output=True,
                text=True,
            )
            self.assertEqual(completed.returncode, 0, completed.stderr)
            self.assertEqual(
                seqir_path.read_text(encoding="utf-8"), importer._render_json(seqir)
            )
            self.assertEqual(cut_path.read_text(encoding="utf-8"), legacy.verilog)
            self.assertEqual(
                manifest_path.read_text(encoding="utf-8"),
                importer._render_json(legacy.state_manifest),
            )


if __name__ == "__main__":
    unittest.main()
