#!/usr/bin/env python3
"""Validate provenance manifests for paper-derived sequential benchmarks.

The validator intentionally depends only on the Python standard library.  It
implements the JSON Schema keywords used by paper_benchmark.schema.json, then
adds repository-specific semantic and file-integrity checks that JSON Schema
cannot express conveniently.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from datetime import date
from pathlib import Path, PurePosixPath
from typing import Any, Mapping, Sequence
from urllib.parse import urlparse


SCHEMA_NAME = "ifcn.sequential.paper_benchmark.v1"
SCHEMA_ID = "https://ifcn.example/schema/sequential-paper-benchmark-v1.json"
REPOSITORY_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SCHEMA_PATH = (
    REPOSITORY_ROOT
    / "tests"
    / "benchmarks_f"
    / "SEQUENTIAL"
    / "schemas"
    / "paper_benchmark.schema.json"
)


def _json_type_matches(value: Any, expected: str) -> bool:
    if expected == "object":
        return isinstance(value, Mapping)
    if expected == "array":
        return isinstance(value, list)
    if expected == "string":
        return isinstance(value, str)
    if expected == "integer":
        return isinstance(value, int) and not isinstance(value, bool)
    if expected == "number":
        return isinstance(value, (int, float)) and not isinstance(value, bool)
    if expected == "boolean":
        return isinstance(value, bool)
    if expected == "null":
        return value is None
    return False


def _json_equal(left: Any, right: Any) -> bool:
    """Compare JSON values without treating booleans as the integers 0 and 1."""
    if isinstance(left, bool) or isinstance(right, bool):
        return isinstance(left, bool) and isinstance(right, bool) and left == right
    if isinstance(left, (int, float)) and isinstance(right, (int, float)):
        return left == right
    return type(left) is type(right) and left == right


def _resolve_local_ref(root_schema: Mapping[str, Any], reference: str) -> Any:
    if not reference.startswith("#/"):
        raise ValueError(f"unsupported non-local schema reference: {reference}")
    current: Any = root_schema
    for encoded_part in reference[2:].split("/"):
        part = encoded_part.replace("~1", "/").replace("~0", "~")
        if not isinstance(current, Mapping) or part not in current:
            raise ValueError(f"unresolvable schema reference: {reference}")
        current = current[part]
    return current


def _is_unique_json_sequence(values: Sequence[Any]) -> bool:
    encoded = [
        json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False)
        for value in values
    ]
    return len(encoded) == len(set(encoded))


def _validate_schema_instance(
    instance: Any,
    schema: Mapping[str, Any],
    root_schema: Mapping[str, Any],
    path: str = "$",
) -> list[str]:
    """Validate the JSON Schema subset used by this repository's manifest."""
    errors: list[str] = []

    if "$ref" in schema:
        try:
            target = _resolve_local_ref(root_schema, schema["$ref"])
        except (TypeError, ValueError) as exc:
            return [f"{path}: invalid schema: {exc}"]
        if not isinstance(target, Mapping):
            return [f"{path}: invalid schema: reference is not an object"]
        errors.extend(_validate_schema_instance(instance, target, root_schema, path))

    expected_types = schema.get("type")
    if expected_types is not None:
        if isinstance(expected_types, str):
            expected_types = [expected_types]
        if not isinstance(expected_types, list) or not all(
            isinstance(item, str) for item in expected_types
        ):
            return errors + [f"{path}: invalid schema type declaration"]
        if not any(_json_type_matches(instance, item) for item in expected_types):
            errors.append(
                f"{path}: expected type {' or '.join(expected_types)}, "
                f"got {type(instance).__name__}"
            )
            return errors

    if "const" in schema and not _json_equal(instance, schema["const"]):
        errors.append(f"{path}: expected constant {schema['const']!r}, got {instance!r}")

    enum = schema.get("enum")
    if enum is not None and not any(_json_equal(instance, item) for item in enum):
        errors.append(f"{path}: value {instance!r} is not one of {enum!r}")

    if isinstance(instance, Mapping):
        required = schema.get("required", [])
        for name in required:
            if name not in instance:
                errors.append(f"{path}.{name}: required property is missing")

        properties = schema.get("properties", {})
        if isinstance(properties, Mapping):
            for name, subschema in properties.items():
                if name in instance and isinstance(subschema, Mapping):
                    errors.extend(
                        _validate_schema_instance(
                            instance[name], subschema, root_schema, f"{path}.{name}"
                        )
                    )
            if schema.get("additionalProperties") is False:
                for name in instance:
                    if name not in properties:
                        errors.append(f"{path}.{name}: additional property is not allowed")

        minimum_properties = schema.get("minProperties")
        if isinstance(minimum_properties, int) and len(instance) < minimum_properties:
            errors.append(
                f"{path}: expected at least {minimum_properties} properties, "
                f"got {len(instance)}"
            )

    if isinstance(instance, list):
        minimum_items = schema.get("minItems")
        if isinstance(minimum_items, int) and len(instance) < minimum_items:
            errors.append(
                f"{path}: expected at least {minimum_items} items, got {len(instance)}"
            )
        if schema.get("uniqueItems") is True and not _is_unique_json_sequence(instance):
            errors.append(f"{path}: array items must be unique")
        item_schema = schema.get("items")
        if isinstance(item_schema, Mapping):
            for index, value in enumerate(instance):
                errors.extend(
                    _validate_schema_instance(
                        value, item_schema, root_schema, f"{path}[{index}]"
                    )
                )

    if isinstance(instance, str):
        minimum_length = schema.get("minLength")
        if isinstance(minimum_length, int) and len(instance) < minimum_length:
            errors.append(
                f"{path}: expected at least {minimum_length} characters, "
                f"got {len(instance)}"
            )
        pattern = schema.get("pattern")
        if isinstance(pattern, str):
            try:
                matches = re.search(pattern, instance) is not None
            except re.error as exc:
                errors.append(f"{path}: invalid schema regular expression: {exc}")
            else:
                if not matches:
                    errors.append(f"{path}: value does not match pattern {pattern!r}")
        if schema.get("format") == "date":
            try:
                parsed = date.fromisoformat(instance)
            except ValueError:
                errors.append(f"{path}: expected an ISO 8601 calendar date")
            else:
                if parsed.isoformat() != instance:
                    errors.append(f"{path}: expected canonical YYYY-MM-DD date")

    if isinstance(instance, (int, float)) and not isinstance(instance, bool):
        minimum = schema.get("minimum")
        maximum = schema.get("maximum")
        if isinstance(minimum, (int, float)) and instance < minimum:
            errors.append(f"{path}: value {instance!r} is below minimum {minimum!r}")
        if isinstance(maximum, (int, float)) and instance > maximum:
            errors.append(f"{path}: value {instance!r} exceeds maximum {maximum!r}")

    for subschema in schema.get("allOf", []):
        if isinstance(subschema, Mapping):
            errors.extend(_validate_schema_instance(instance, subschema, root_schema, path))

    any_of = schema.get("anyOf")
    if isinstance(any_of, list) and any_of:
        alternatives = [
            _validate_schema_instance(instance, candidate, root_schema, path)
            for candidate in any_of
            if isinstance(candidate, Mapping)
        ]
        if not alternatives or all(candidate_errors for candidate_errors in alternatives):
            errors.append(f"{path}: value does not satisfy any allowed alternative")

    negated = schema.get("not")
    if isinstance(negated, Mapping):
        if not _validate_schema_instance(instance, negated, root_schema, path):
            errors.append(f"{path}: value matches a prohibited schema")

    condition = schema.get("if")
    consequent = schema.get("then")
    if isinstance(condition, Mapping) and isinstance(consequent, Mapping):
        if not _validate_schema_instance(instance, condition, root_schema, path):
            errors.extend(
                _validate_schema_instance(instance, consequent, root_schema, path)
            )

    return errors


def _load_schema(schema_path: Path) -> tuple[Mapping[str, Any] | None, list[str]]:
    try:
        with schema_path.open("r", encoding="utf-8") as handle:
            schema = json.load(handle)
    except (OSError, json.JSONDecodeError) as exc:
        return None, [f"schema: cannot load {schema_path}: {exc}"]
    if not isinstance(schema, Mapping):
        return None, [f"schema: {schema_path} must contain a JSON object"]

    errors: list[str] = []
    if schema.get("$schema") != "https://json-schema.org/draft/2020-12/schema":
        errors.append("schema.$schema: expected JSON Schema draft 2020-12")
    if schema.get("$id") != SCHEMA_ID:
        errors.append(f"schema.$id: expected {SCHEMA_ID!r}")
    declared_name = schema.get("properties", {}).get("schema", {}).get("const")
    if declared_name != SCHEMA_NAME:
        errors.append(f"schema.properties.schema.const: expected {SCHEMA_NAME!r}")
    return schema, errors


def _mapping_at(document: Mapping[str, Any], name: str) -> Mapping[str, Any]:
    value = document.get(name)
    return value if isinstance(value, Mapping) else {}


def _semantic_errors(document: Mapping[str, Any]) -> list[str]:
    errors: list[str] = []
    classification = _mapping_at(document, "classification")
    source = _mapping_at(document, "source")
    reconstruction = _mapping_at(document, "reconstruction")
    semantics = _mapping_at(document, "semantics")
    clock = _mapping_at(semantics, "clock")
    pipeline = _mapping_at(document, "expected_pipeline")
    validation = _mapping_at(document, "validation")

    benchmark_id = document.get("benchmark_id")
    citation = _mapping_at(source, "citation")
    if isinstance(benchmark_id, str) and "/" in benchmark_id:
        citekey = benchmark_id.split("/", 1)[0]
        if citation.get("key") != citekey:
            errors.append(
                "$.source.citation.key: must match the citekey prefix of $.benchmark_id"
            )

    state_element = classification.get("state_element")
    if state_element is not None and semantics.get("state_element") != state_element:
        errors.append(
            "$.semantics.state_element: must match $.classification.state_element"
        )

    trigger_model = classification.get("trigger_model")
    if trigger_model is not None and clock.get("kind") != trigger_model:
        errors.append(
            "$.semantics.clock.kind: must match $.classification.trigger_model"
        )

    if state_element in {"d_latch", "sr_latch"}:
        expected_outcomes = {
            "seqir": "reject",
            "legacy_cut": "not_run",
            "sequential_pnr": "not_run",
            "mapping": "not_run",
        }
        for stage, expected in expected_outcomes.items():
            stage_value = pipeline.get(stage)
            if isinstance(stage_value, Mapping) and stage_value.get("outcome") != expected:
                errors.append(
                    f"$.expected_pipeline.{stage}.outcome: current pipeline requires "
                    f"{expected!r} for a level-sensitive latch"
                )

    source_state_element = reconstruction.get("source_state_element")
    temporal_relation = reconstruction.get("temporal_relation")
    if temporal_relation == "same_model" and source_state_element != state_element:
        errors.append(
            "$.reconstruction.temporal_relation: same_model requires "
            "source_state_element to match $.classification.state_element"
        )
    elif temporal_relation == "sampled_state_only":
        sampled_adapters = {
            "d_latch": "dff",
            "sr_latch": "sr_flip_flop",
        }
        expected_target = sampled_adapters.get(source_state_element)
        if expected_target != state_element:
            errors.append(
                "$.reconstruction.source_state_element: sampled_state_only permits "
                "only d_latch -> dff or sr_latch -> sr_flip_flop"
            )
        if trigger_model != "edge" or clock.get("kind") != "edge":
            errors.append(
                "$.reconstruction.temporal_relation: sampled_state_only target must "
                "use an edge-triggered classification and clock"
            )
        faithfulness_scope = reconstruction.get("faithfulness_scope")
        if isinstance(faithfulness_scope, list) and "clock_semantics" in faithfulness_scope:
            errors.append(
                "$.reconstruction.faithfulness_scope: sampled_state_only cannot claim "
                "source clock_semantics"
            )
        not_claimed = reconstruction.get("not_claimed")
        if (
            not isinstance(not_claimed, list)
            or "transparent_window_timing_equivalence" not in not_claimed
        ):
            errors.append(
                "$.reconstruction.not_claimed: sampled_state_only must include "
                "'transparent_window_timing_equivalence'"
            )
        transformations = reconstruction.get("transformations")
        if not isinstance(transformations, list) or not transformations:
            errors.append(
                "$.reconstruction.transformations: sampled_state_only must document "
                "the adapter transformation"
            )

    if (
        semantics.get("physical_abstraction") == "abstract_state_boundary"
        and pipeline.get("physical_state_signoff") == "characterized"
    ):
        errors.append(
            "$.expected_pipeline.physical_state_signoff: an abstract state boundary "
            "cannot claim characterized physical state"
        )

    status = validation.get("status")
    checks = validation.get("checks")
    if status == "validated":
        if not isinstance(checks, list) or not checks:
            errors.append("$.validation.checks: validated status requires at least one check")
        elif any(
            not isinstance(check, Mapping) or check.get("status") != "pass"
            for check in checks
        ):
            errors.append("$.validation.checks: validated status requires every check to pass")
    elif status == "partially_validated" and isinstance(checks, list):
        if not any(
            isinstance(check, Mapping) and check.get("status") == "pass"
            for check in checks
        ):
            errors.append(
                "$.validation.checks: partially_validated status requires a passing check"
            )

    citation_url = citation.get("url")
    if isinstance(citation_url, str):
        parsed = urlparse(citation_url)
        if parsed.scheme not in {"http", "https"} or not parsed.netloc:
            errors.append("$.source.citation.url: expected an absolute HTTP(S) URL")
    artifact_url = source.get("artifact_url")
    if isinstance(artifact_url, str):
        parsed = urlparse(artifact_url)
        if parsed.scheme not in {"http", "https"} or not parsed.netloc:
            errors.append("$.source.artifact_url: expected an absolute HTTP(S) URL")

    files = document.get("files")
    if isinstance(files, list):
        roles = {
            item.get("role")
            for item in files
            if isinstance(item, Mapping) and isinstance(item.get("role"), str)
        }
        if "rtl" not in roles:
            errors.append("$.files: at least one file with role 'rtl' is required")

    return errors


def _file_errors(
    document: Mapping[str, Any], manifest_path: Path | None, check_files: bool
) -> list[str]:
    errors: list[str] = []
    files = document.get("files")
    if not isinstance(files, list):
        return errors

    seen_paths: set[str] = set()
    base = manifest_path.resolve().parent if manifest_path is not None else None
    if check_files and base is None:
        errors.append("$.files: manifest_path is required when file checking is enabled")

    for index, entry in enumerate(files):
        if not isinstance(entry, Mapping):
            continue
        raw_path = entry.get("path")
        if not isinstance(raw_path, str):
            continue
        field = f"$.files[{index}].path"
        if raw_path in seen_paths:
            errors.append(f"{field}: duplicate referenced path {raw_path!r}")
        seen_paths.add(raw_path)

        posix_path = PurePosixPath(raw_path)
        if (
            not raw_path
            or "\\" in raw_path
            or posix_path.is_absolute()
            or ".." in posix_path.parts
            or posix_path.as_posix() != raw_path
        ):
            errors.append(f"{field}: must be a normalized relative POSIX path")
            continue
        if not check_files or base is None:
            continue

        target = (base / Path(*posix_path.parts)).resolve()
        try:
            target.relative_to(base)
        except ValueError:
            errors.append(f"{field}: resolves outside the manifest directory")
            continue
        if not target.is_file():
            errors.append(f"{field}: referenced file does not exist: {raw_path}")
            continue

        declared_hash = entry.get("sha256")
        if isinstance(declared_hash, str) and re.fullmatch(r"[0-9a-f]{64}", declared_hash):
            try:
                digest = hashlib.sha256(target.read_bytes()).hexdigest()
            except OSError as exc:
                errors.append(f"{field}: cannot read referenced file: {exc}")
                continue
            if digest != declared_hash:
                errors.append(
                    f"$.files[{index}].sha256: expected {declared_hash}, got {digest}"
                )

    return errors


def validate_manifest(
    document: Any,
    manifest_path: Path | str | None = None,
    *,
    check_files: bool = True,
    schema_path: Path | str = DEFAULT_SCHEMA_PATH,
) -> list[str]:
    """Return deterministic diagnostics; an empty list means valid."""
    schema, errors = _load_schema(Path(schema_path))
    if schema is None:
        return errors

    errors.extend(_validate_schema_instance(document, schema, schema))
    if isinstance(document, Mapping):
        errors.extend(_semantic_errors(document))
        normalized_manifest_path = Path(manifest_path) if manifest_path is not None else None
        errors.extend(_file_errors(document, normalized_manifest_path, check_files))
    return errors


def validate_manifest_file(
    manifest_path: Path | str,
    *,
    check_files: bool = True,
    schema_path: Path | str = DEFAULT_SCHEMA_PATH,
) -> list[str]:
    path = Path(manifest_path)
    try:
        with path.open("r", encoding="utf-8") as handle:
            document = json.load(handle)
    except (OSError, json.JSONDecodeError) as exc:
        return [f"$: cannot load {path}: {exc}"]
    return validate_manifest(
        document,
        path,
        check_files=check_files,
        schema_path=schema_path,
    )


def _parse_args(argv: Sequence[str] | None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Validate iFCN sequential paper benchmark provenance manifests."
    )
    parser.add_argument("manifest", nargs="+", type=Path, help="manifest JSON file")
    parser.add_argument(
        "--schema",
        type=Path,
        default=DEFAULT_SCHEMA_PATH,
        help=f"schema path (default: {DEFAULT_SCHEMA_PATH})",
    )
    parser.add_argument(
        "--no-check-files",
        action="store_true",
        help="validate declarations without opening referenced files",
    )
    return parser.parse_args(argv)


def main(argv: Sequence[str] | None = None) -> int:
    args = _parse_args(argv)
    failed = False
    for manifest in args.manifest:
        errors = validate_manifest_file(
            manifest,
            check_files=not args.no_check_files,
            schema_path=args.schema,
        )
        if errors:
            failed = True
            print(f"FAIL {manifest}")
            for error in errors:
                print(f"  - {error}")
        else:
            print(f"PASS {manifest}")
    return 2 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
