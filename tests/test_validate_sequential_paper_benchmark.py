#!/usr/bin/env python3
"""Tests for the standard-library sequential paper manifest validator."""

from __future__ import annotations

import json
import subprocess
import sys
import unittest
from pathlib import Path


REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
SCRIPTS = REPOSITORY_ROOT / "scripts"
if str(SCRIPTS) not in sys.path:
    sys.path.insert(0, str(SCRIPTS))

from validate_sequential_paper_benchmark import (  # noqa: E402
    SCHEMA_ID,
    SCHEMA_NAME,
    validate_manifest,
    validate_manifest_file,
)


SCHEMA = (
    REPOSITORY_ROOT
    / "tests"
    / "benchmarks_f"
    / "SEQUENTIAL"
    / "schemas"
    / "paper_benchmark.schema.json"
)
FIXTURES = REPOSITORY_ROOT / "tests" / "fixtures" / "sequential"
VALID = FIXTURES / "paper_benchmark.valid.json"
INVALID = FIXTURES / "paper_benchmark.invalid.json"
VALIDATOR = SCRIPTS / "validate_sequential_paper_benchmark.py"


class PaperBenchmarkValidatorTest(unittest.TestCase):
    maxDiff = None

    def test_schema_identity_and_draft(self) -> None:
        schema = json.loads(SCHEMA.read_text(encoding="utf-8"))
        self.assertEqual(
            schema["$schema"], "https://json-schema.org/draft/2020-12/schema"
        )
        self.assertEqual(schema["$id"], SCHEMA_ID)
        self.assertEqual(schema["properties"]["schema"]["const"], SCHEMA_NAME)

    def test_valid_fixture_and_referenced_rtl_pass(self) -> None:
        self.assertEqual(validate_manifest_file(VALID), [])

    def test_d_latch_is_level_sensitive_and_stops_after_seqir_rejection(self) -> None:
        document = json.loads(VALID.read_text(encoding="utf-8"))
        document["benchmark_id"] = "demo2025/d_latch_active_high_1b"
        document["classification"].update(
            state_element="d_latch", trigger_model="level"
        )
        document["semantics"].update(
            state_element="d_latch",
            clock={
                "kind": "level",
                "clock_domains": 1,
                "level": "active_high",
                "logical_clock_abstraction": "q follows d while enable is high",
            },
            next_state_equation="q_next = enable ? d : q",
        )
        document["expected_pipeline"]["seqir"]["outcome"] = "reject"
        for stage in ("legacy_cut", "sequential_pnr", "mapping"):
            document["expected_pipeline"][stage]["outcome"] = "not_run"

        self.assertEqual(validate_manifest(document, check_files=False), [])

        document["classification"]["trigger_model"] = "edge"
        errors = "\n".join(validate_manifest(document, check_files=False))
        self.assertIn("$.classification.trigger_model", errors)
        self.assertIn("$.semantics.clock.kind", errors)

    def test_sampled_state_adapters_preserve_only_boundary_state(self) -> None:
        for source_element, target_element in (
            ("d_latch", "dff"),
            ("sr_latch", "sr_flip_flop"),
        ):
            with self.subTest(source=source_element, target=target_element):
                document = json.loads(VALID.read_text(encoding="utf-8"))
                document["benchmark_id"] = f"demo2025/{target_element}_sampled_1b"
                document["classification"]["state_element"] = target_element
                document["semantics"]["state_element"] = target_element
                if target_element == "sr_flip_flop":
                    document["semantics"]["simultaneous_set_reset"] = "forbidden"
                    document["semantics"]["next_state_equation"] = (
                        "q_next = set ? 1 : (reset ? 0 : q)"
                    )
                reconstruction = document["reconstruction"]
                reconstruction["source_state_element"] = source_element
                reconstruction["temporal_relation"] = "sampled_state_only"
                reconstruction["faithfulness_scope"].remove("clock_semantics")
                reconstruction["not_claimed"].append(
                    "transparent_window_timing_equivalence"
                )
                reconstruction["transformations"].append(
                    "Sample source latch state at an explicit transaction boundary"
                )

                self.assertEqual(validate_manifest(document, check_files=False), [])

    def test_sr_latch_hold_semantics_and_sampled_adapter_are_valid(self) -> None:
        document = json.loads(VALID.read_text(encoding="utf-8"))
        document["benchmark_id"] = "demo2025/sr_latch_deng_fig9_1b"
        document["classification"].update(
            state_element="sr_latch", trigger_model="level"
        )
        document["semantics"].update(
            state_element="sr_latch",
            clock={
                "kind": "level",
                "clock_domains": 1,
                "level": "active_high",
                "logical_clock_abstraction": (
                    "The source majority feedback latch has no logical clock pin; "
                    "level denotes its continuously enabled feedback relation"
                ),
            },
            next_state_equation="q_next = M(s, not_r, q); s = r = 1 holds q",
            simultaneous_set_reset="hold",
            legal_input_contract="s and r may take all four binary combinations",
        )
        document["expected_pipeline"]["seqir"]["outcome"] = "reject"
        for stage in ("legacy_cut", "sequential_pnr", "mapping"):
            document["expected_pipeline"][stage]["outcome"] = "not_run"

        self.assertEqual(validate_manifest(document, check_files=False), [])

        document["benchmark_id"] = "demo2025/sr_flip_flop_deng_fig9_sampled_1b"
        document["classification"].update(
            state_element="sr_flip_flop", trigger_model="edge"
        )
        document["semantics"].update(
            state_element="sr_flip_flop",
            clock={
                "kind": "edge",
                "clock_domains": 1,
                "edge": "posedge",
                "logical_clock_abstraction": (
                    "Adapter samples the clockless source latch once per transaction"
                ),
            },
        )
        reconstruction = document["reconstruction"]
        reconstruction["source_state_element"] = "sr_latch"
        reconstruction["temporal_relation"] = "sampled_state_only"
        reconstruction["faithfulness_scope"].remove("clock_semantics")
        reconstruction["not_claimed"].append(
            "transparent_window_timing_equivalence"
        )
        reconstruction["transformations"].append(
            "Sample the source feedback state at a transaction boundary"
        )
        for stage in ("seqir", "legacy_cut"):
            document["expected_pipeline"][stage]["outcome"] = "pass"
        for stage in ("sequential_pnr", "mapping"):
            document["expected_pipeline"][stage]["outcome"] = "prototype_expected"

        self.assertEqual(validate_manifest(document, check_files=False), [])

    def test_sampled_state_adapter_rejects_an_equivalence_overclaim(self) -> None:
        document = json.loads(VALID.read_text(encoding="utf-8"))
        reconstruction = document["reconstruction"]
        reconstruction["source_state_element"] = "d_latch"
        reconstruction["temporal_relation"] = "sampled_state_only"

        errors = "\n".join(validate_manifest(document, check_files=False))
        self.assertIn("$.reconstruction.faithfulness_scope", errors)
        self.assertIn("$.reconstruction.not_claimed", errors)

    def test_invalid_fixture_reports_schema_semantics_and_integrity(self) -> None:
        errors = validate_manifest_file(INVALID)
        joined = "\n".join(errors)
        self.assertIn("$.classification.trigger_model", joined)
        self.assertIn("$.source.locator", joined)
        self.assertIn("$.source.citation.key", joined)
        self.assertIn("$.semantics.state_element", joined)
        self.assertIn("$.expected_pipeline.seqir.outcome", joined)
        self.assertIn("$.expected_pipeline.physical_state_signoff", joined)
        self.assertIn("$.validation.checks", joined)
        self.assertIn("$.files[0].sha256", joined)

    def test_cli_exit_codes_and_diagnostics(self) -> None:
        passed = subprocess.run(
            [sys.executable, str(VALIDATOR), str(VALID)],
            cwd=REPOSITORY_ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )
        self.assertEqual(passed.returncode, 0, passed.stdout)
        self.assertIn(f"PASS {VALID}", passed.stdout)

        failed = subprocess.run(
            [sys.executable, str(VALIDATOR), str(INVALID)],
            cwd=REPOSITORY_ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            check=False,
        )
        self.assertEqual(failed.returncode, 2, failed.stdout)
        self.assertIn(f"FAIL {INVALID}", failed.stdout)
        self.assertIn("$.files[0].sha256", failed.stdout)


if __name__ == "__main__":
    unittest.main()
