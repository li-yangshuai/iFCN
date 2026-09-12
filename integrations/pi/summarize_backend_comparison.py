#!/usr/bin/env python3
"""Summarize complete backend matrices without dropping failures or changing cohorts."""

import argparse
from collections import Counter
import hashlib
import itertools
import json
import math
from pathlib import Path
import re
import statistics


ROOT = Path(__file__).resolve().parents[2]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def metric(row, name):
    if name == "occupied_bbox_area_tiles":
        geometry = row.get("summary_geometry", {})
        value = geometry.get("area_tiles") if geometry.get("verified") is True else None
    else:
        section, field = {"reported_area_tiles": ("pnr", "area_tiles"), "cell_count": ("mapping", "cell_count")}[name]
        value = row.get("metrics", {}).get(section, {}).get(field)
    return value if isinstance(value, (int, float)) and not isinstance(value, bool) and math.isfinite(value) and value > 0 else None


def occupied_geometry(candidate):
    points = {(node["x"], node["y"]) for node in candidate["nodes"]}
    points.update(tuple(point) for route in candidate["routes"] for point in route["path"])
    if not points or any(len(point) != 2 or any(not isinstance(value, int) or isinstance(value, bool) for value in point) for point in points):
        raise ValueError("Candidate must contain finite integer grid coordinates")
    x0, y0 = min(point[0] for point in points), min(point[1] for point in points)
    x1, y1 = max(point[0] for point in points), max(point[1] for point in points)
    return {"min_x": x0, "min_y": y0, "max_x": x1, "max_y": y1,
            "width": x1 - x0 + 1, "height": y1 - y0 + 1, "area_tiles": (x1 - x0 + 1) * (y1 - y0 + 1)}


def audit_geometry(document):
    result = {**document, "rows": []}
    diagnostics = []
    integrity_errors = []
    for original in document["rows"]:
        row = dict(original)
        bounds = row.get("occupied_bounds", {})
        geometry = {"verified": False, "reported_area_tiles": metric(row, "reported_area_tiles")}
        if not bounds.get("candidate_path") or not bounds.get("candidate_sha256"):
            geometry["error"] = "No candidate path/hash was recorded; occupied area is unavailable."
        else:
            geometry.update(candidate_path=bounds["candidate_path"], candidate_sha256=bounds["candidate_sha256"])
            try:
                data = Path(bounds["candidate_path"]).read_bytes()
                if hashlib.sha256(data).hexdigest() != bounds["candidate_sha256"]:
                    raise ValueError("Candidate hash does not match the recorded experiment")
                actual = occupied_geometry(json.loads(data))
                if any(actual[key] != bounds.get(key) for key in ("width", "height", "area_tiles")):
                    raise ValueError("Recomputed occupied geometry differs from the recorded experiment")
                geometry.update(actual, verified=True)
                geometry["reported_area_matches_occupied"] = geometry["reported_area_tiles"] == geometry["area_tiles"]
            except (ValueError, OSError, KeyError, TypeError) as error:
                geometry["error"] = str(error)
                integrity_errors.append({"case": row["case"], "algorithm": row["algorithm"], "error": str(error)})
        row["summary_geometry"] = geometry
        result["rows"].append(row)
        diagnostics.append({"case": row["case"], "algorithm": row["algorithm"], "run_id": row.get("run_id"), **geometry})
    return result, diagnostics, integrity_errors


def verify_archived_implementation(directory, protocol):
    snapshot = directory / "implementation-snapshot"
    index_path = snapshot / "snapshot-index.json"
    if not index_path.is_file():
        return {"available": False, "note": "No archived implementation snapshot was supplied."}
    index = json.loads(index_path.read_text())
    expected_source = {**protocol["code_sha256"], **protocol["normal_implementation_sha256"]}
    if index["source"] != expected_source or index["runtime"] != protocol["runtime_sha256"]:
        raise ValueError("Archived implementation index differs from the completed experiment protocol")
    for name, digest in index["source"].items():
        if sha(snapshot / "source" / name) != digest:
            raise ValueError(f"Archived source hash mismatch: {name}")
    runtime_count = 0
    for name, item in index["runtime"].items():
        if item["sha256"] is not None:
            if sha(snapshot / "runtime" / name) != item["sha256"]:
                raise ValueError(f"Archived runtime hash mismatch: {name}")
            runtime_count += 1
    return {"available": True, "verified": True, "path": str(snapshot), "index_sha256": sha(index_path),
            "source_files_verified": len(index["source"]), "runtime_files_verified": runtime_count,
            "note": "Verified the archived matrix implementation against its protocol. Later changes to live production source are not retroactively classified as drift during this completed experiment."}


def completed(row):
    return (row.get("task_completed") is True
            and row.get("status") in {"completed", "completed_with_warnings"}
            and not row.get("artifact_mismatches")
            and not row.get("summary_validation_errors"))


def describe_scope(name, case_ids, algorithms, rows):
    by_algorithm = {}
    failures = []
    for algorithm in algorithms:
        cohort = [rows[(case_id, algorithm)] for case_id in case_ids]
        count = sum(completed(row) for row in cohort)
        by_algorithm[algorithm] = {
            "planned": len(cohort), "recorded": sum(row["status"] != "not_reported" for row in cohort),
            "completed": count,
            "completed_with_warnings": sum(completed(row) and row["status"] == "completed_with_warnings" for row in cohort),
            "not_completed": len(cohort) - count,
            "completion_fraction": count / len(cohort) if cohort else None,
            "status_counts": dict(sorted(Counter(row["status"] for row in cohort).items())),
        }
        failures.extend({"case": row["case"], "algorithm": algorithm, "status": row["status"],
                         "stage": row.get("stage"), "error": row.get("error"),
                         "validation_errors": row.get("summary_validation_errors", []),
                         "run_id": row.get("run_id")} for row in cohort if not completed(row))
    comparisons = []
    for left, right in itertools.combinations(algorithms, 2):
        measures = {}
        for field in ("occupied_bbox_area_tiles", "cell_count"):
            pairs, exclusions = [], []
            for case_id in case_ids:
                a, b = rows[(case_id, left)], rows[(case_id, right)]
                av, bv = metric(a, field), metric(b, field)
                reasons = []
                for algorithm, row, value in ((left, a, av), (right, b, bv)):
                    if not completed(row):
                        reasons.append(f"{algorithm}:task_not_completed:{row['status']}")
                    elif value is None:
                        reasons.append(f"{algorithm}:missing_or_invalid_{field}")
                if reasons:
                    exclusions.append({"case": case_id, "reasons": reasons})
                else:
                    pairs.append({"case": case_id, "left": av, "right": bv,
                                  "left_over_right": av / bv,
                                  "left_reported_area_tiles": metric(a, "reported_area_tiles"),
                                  "right_reported_area_tiles": metric(b, "reported_area_tiles"),
                                  "left_run_id": a.get("run_id"), "right_run_id": b.get("run_id")})
            measures[field] = {
                "unit": "occupied_bbox_tiles" if field == "occupied_bbox_area_tiles" else "QCA_cells",
                "paired_case_count": len(pairs), "pairs": pairs, "excluded": exclusions,
                "left_mean_on_paired_cases": statistics.mean(item["left"] for item in pairs) if pairs else None,
                "right_mean_on_paired_cases": statistics.mean(item["right"] for item in pairs) if pairs else None,
                "ratio_of_paired_sums_left_over_right": (sum(item["left"] for item in pairs) / sum(item["right"] for item in pairs)) if pairs else None,
                "median_paired_ratio_left_over_right": statistics.median(item["left_over_right"] for item in pairs) if pairs else None,
            }
        comparisons.append({"left_algorithm": left, "right_algorithm": right, "metrics": measures})
    return {"name": name, "case_ids": case_ids, "by_algorithm": by_algorithm,
            "not_completed_cases": failures, "paired_comparisons": comparisons}


def join_logical_and_interface_audit(document, manifest, audit, comparison_hash, diagnostic=None):
    """Derive qualification without altering the original pipeline success flags."""
    if audit.get("comparison_snapshot_sha256") != comparison_hash or audit.get("comparison_is_partial") is not False:
        raise ValueError("Logic audit is not bound to this complete results snapshot")
    case_lookup = {case["id"]: case for case in manifest["cases"]}
    audits = {}
    for item in audit["rows"]:
        key = (item["case"], item["algorithm"])
        if key in audits:
            raise ValueError(f"Duplicate logic-audit row: {key}")
        audits[key] = item
    qualification_rows, strict_rows, evidence_errors = [], [], []
    observed_output_mismatches = set()
    for original in document["rows"]:
        row = dict(original)
        key = (row["case"], row["algorithm"])
        raw_complete = completed(row)
        info = {"case": key[0], "algorithm": key[1], "run_id": row.get("run_id"),
                "raw_status": row["status"], "raw_execution_completed": raw_complete,
                "logic_status": "not_audited_pipeline_not_completed", "qca_interface_counts_status": "not_checked",
                "source_dag_and_qca_counts_qualified": False}
        if raw_complete:
            logical = audits.get(key)
            info["logic_status"] = logical.get("status", "not_audited") if logical else "not_audited"
            info["logic_reason"] = logical.get("reason") if logical else "Missing independent logic audit"
            errors = []
            try:
                case = case_lookup[key[0]]
                run_directory = Path(row["run_directory"])
                run = json.loads((run_directory / "manifest.json").read_text())
                if run["run_id"] != row["run_id"]:
                    raise ValueError("Run identity mismatch")
                if logical:
                    if logical["run_id"] != row["run_id"] or logical["source_sha256"] != case["frozen_sha256"]:
                        raise ValueError("Logic audit does not match run/source identity")
                    dag_item = run["artifacts"]["routed_dag.json"]
                    dag_path = run_directory / dag_item["path"]
                    if sha(dag_path) != dag_item["sha256"] or dag_item["sha256"] != logical["routed_dag_sha256"]:
                        raise ValueError("Routed DAG hash differs from run or logic audit")
                qca_item = run["artifacts"]["device.qca"]
                qca_path = run_directory / qca_item["path"]
                qca_bytes = qca_path.read_bytes()
                if hashlib.sha256(qca_bytes).hexdigest() != qca_item["sha256"]:
                    raise ValueError("QCA hash differs from the recorded run")
                qca = qca_bytes.decode()
                input_count = len(re.findall(r"(?m)^cell_function=QCAD_CELL_INPUT\s*$", qca))
                output_count = len(re.findall(r"(?m)^cell_function=QCAD_CELL_OUTPUT\s*$", qca))
                expected_inputs, expected_outputs = len(case["input_ports"]), len(case["output_ports"])
                counts_match = input_count == expected_inputs and output_count == expected_outputs
                info.update(expected_inputs=expected_inputs, expected_outputs=expected_outputs,
                            actual_qca_inputs=input_count, actual_qca_outputs=output_count,
                            qca_interface_counts_status="counts_match" if counts_match else "count_mismatch",
                            qca_path=str(qca_path), qca_sha256=qca_item["sha256"])
                if output_count != expected_outputs:
                    observed_output_mismatches.add(key)
                info["source_dag_and_qca_counts_qualified"] = bool(logical and logical.get("status") == "equivalent" and logical.get("equivalent") is True and counts_match)
            except (ValueError, OSError, KeyError, TypeError) as error:
                errors.append(str(error))
                evidence_errors.append({"case": key[0], "algorithm": key[1], "error": str(error)})
                info["qca_interface_counts_status"] = "verification_error"
            info["evidence_errors"] = errors
        qualification_rows.append(info)
        row["raw_pipeline_status"] = original["status"]
        row["task_completed"] = info["source_dag_and_qca_counts_qualified"]
        if raw_complete and not row["task_completed"]:
            row["status"] = "logical_or_interface_gate_not_passed"
            row["error"] = f"logic={info['logic_status']}; QCA interface={info['qca_interface_counts_status']}; {info.get('logic_reason') or ''}"
        strict_rows.append(row)
    raw_completed_keys = {(row["case"], row["algorithm"]) for row in document["rows"] if completed(row)}
    if set(audits) != raw_completed_keys:
        evidence_errors.append({"error": "Independent audit row set does not exactly cover raw completed cases"})
    if diagnostic is not None:
        diagnostic_keys = {(item["case"], item["algorithm"]) for item in diagnostic["output_count_mismatches"]}
        if diagnostic_keys != observed_output_mismatches:
            evidence_errors.append({"error": "Independent QCA recount differs from the diagnostic mismatch set"})
    by_algorithm = {}
    for algorithm in document["protocol"]["algorithms"]:
        cohort = [item for item in qualification_rows if item["algorithm"] == algorithm]
        by_algorithm[algorithm] = {
            "planned": len(cohort),
            "raw_execution_completed": sum(item["raw_execution_completed"] for item in cohort),
            "logic_status_counts": dict(Counter(item["logic_status"] for item in cohort)),
            "qca_output_count_mismatches": sum(item.get("actual_qca_outputs") != item.get("expected_outputs") for item in cohort if "actual_qca_outputs" in item),
            "source_dag_and_qca_counts_qualified": sum(item["source_dag_and_qca_counts_qualified"] for item in cohort),
        }
    return {**document, "rows": strict_rows}, {
        "definition": "Original pipeline completion plus independently audited source-to-routed-DAG equivalence and matching QCA input/output cell counts. This is not device functional/timing or power signoff.",
        "matrix_implementation": "The archived implementation used by the completed matrix, before adding production logic/interface guards. No claim that the upgraded production version was rerun on all 90 attempts.",
        "raw_execution_completed": sum(item["raw_execution_completed"] for item in qualification_rows),
        "source_dag_and_qca_counts_qualified": sum(item["source_dag_and_qca_counts_qualified"] for item in qualification_rows),
        "by_algorithm": by_algorithm, "rows": qualification_rows, "evidence_errors": evidence_errors,
    }


def summarize(document, manifest, final_checks, integrity_checks=None):
    protocol = document["protocol"]
    algorithm_ids, selected = protocol["algorithms"], protocol["cases"]
    if len(set(algorithm_ids)) != len(algorithm_ids) or len(set(selected)) != len(selected):
        raise ValueError("Duplicate algorithm or selected case in protocol")
    case_lookup = {case["id"]: case for case in manifest["cases"]}
    if set(selected) - case_lookup.keys():
        raise ValueError("Protocol contains cases absent from the frozen manifest")
    indexed = {}
    row_errors = []
    for original in document["rows"]:
        row = dict(original)
        key = (row["case"], row["algorithm"])
        if key in indexed:
            raise ValueError(f"Repeated result for {key}; repetitions require a separate declared protocol")
        if key[0] not in selected or key[1] not in algorithm_ids:
            raise ValueError(f"Result not planned by protocol: {key}")
        case = case_lookup[row["case"]]
        errors = []
        if row.get("input_sha256") != case["frozen_sha256"]:
            errors.append("input_hash_does_not_match_frozen_case")
        if row.get("function_group") != case["function_group"]:
            errors.append("function_group_does_not_match_manifest")
        if row.get("task_completed") is True and (row.get("status") not in {"completed", "completed_with_warnings"} or row.get("artifact_mismatches")):
            errors.append("task_completed_inconsistent_with_status_or_artifacts")
        row["summary_validation_errors"] = errors
        if errors:
            row_errors.append({"case": key[0], "algorithm": key[1], "errors": errors})
        indexed[key] = row
    missing = []
    for case_id, algorithm in itertools.product(selected, algorithm_ids):
        key = (case_id, algorithm)
        if key not in indexed:
            missing.append({"case": case_id, "algorithm": algorithm})
            indexed[key] = {"case": case_id, "algorithm": algorithm, "status": "not_reported", "task_completed": False,
                            "error": "The declared matrix contains no result row for this attempt."}
    representatives = [group["representative_case_id"] for group in manifest["function_groups"]]
    if any(case_id not in case_lookup for case_id in representatives):
        raise ValueError("Manifest representative is absent from cases")
    for group in manifest["function_groups"]:
        if case_lookup[group["representative_case_id"]]["function_group"] != group["id"]:
            raise ValueError("Manifest representative has a mismatched function_group")
    selected_representatives = [case_id for case_id in representatives if case_id in selected]
    unique_bytes = []
    seen = set()
    for case in manifest["cases"]:
        if case["id"] in selected and case["frozen_sha256"] not in seen:
            unique_bytes.append(case["id"])
            seen.add(case["frozen_sha256"])
    checks = dict(integrity_checks or {})
    checks.update({
        "benchmark_final_checks_passed": final_checks.get("eligible_for_comparison") is True and all(value is True for value in final_checks.values()),
        "matrix_complete": not missing,
        "reported_counts_match_rows": document.get("finished") == len(document["rows"]) and document.get("total") == len(selected) * len(algorithm_ids),
        "row_identity_checks_passed": not row_errors,
    })
    scopes = {
        "all_representations": describe_scope("All planned source representations, including explicitly marked duplicate cases", selected, algorithm_ids, indexed),
        "unique_byte_representations": describe_scope("One deterministic manifest-order case per frozen byte hash among the selected inputs", unique_bytes, algorithm_ids, indexed),
        "preselected_function_representatives": describe_scope("Only manifest-preselected function representatives; no replacement based on observed success", selected_representatives, algorithm_ids, indexed),
    }
    return {
        "schema": "ifcn.backend.comparison.summary.v1", "benchmark_version": manifest["benchmark_version"],
        "eligible_for_comparison": all(checks.values()), "checks": checks,
        "protocol": protocol,
        "policy": {
            "raw_completion_meaning": "Layout/mapping execution completed under the original matrix criterion; it is not a logical or physical correctness claim.",
            "completion_denominator": "All planned cases in each scope; failed, cancelled, harness-error and missing outcomes remain in the denominator.",
            "function_representatives": "Use representative_case_id from the frozen manifest, even when another representation succeeds and the representative fails.",
            "paired_metrics": "Both pipelines must complete the same frozen input and provide positive finite values. Each metric reports its exact paired cohort and all exclusions. No unpaired mean comparison is made.",
            "area_definition": "The bounding rectangle of all actual node and routed-grid coordinates, independently recomputed from hash-verified layout_candidate.json. Reported backend width*height is preserved separately and is never used for paired area statistics.",
            "interpretation": "Descriptive end-to-end pipeline comparison only. Equivalent Boolean rewriting may change DAGs; no pure-P&R superiority, significance, power accuracy, device correctness or LLM-effect claim is inferred.",
            "runtime": "Wall time is retained in the input evidence but is not compared here because this protocol permits shared-host concurrency.",
        },
        "matrix": {"planned": len(selected) * len(algorithm_ids), "recorded": len(document["rows"]), "missing": missing, "row_validation_errors": row_errors},
        "function_coverage": {"declared_function_groups": len(representatives), "selected_predeclared_representatives": len(selected_representatives),
                              "unselected_representatives": [case_id for case_id in representatives if case_id not in selected]},
        "scopes": scopes,
    }


def markdown(report):
    lines = ["# 冻结基准后端结果统计", "", f"实验记录完整性：{'通过' if report['eligible_for_comparison'] else '未通过；本报告仅用于诊断'}。", "",
             "按同一冻结输入比较端到端流程。失败保留；未推断算法优势、统计显著性、物理正确性或 LLM 效果。", ""]
    names = {"all_representations": "全部表示案例", "unique_byte_representations": "去除字节重复的表示案例", "preselected_function_representatives": "预先指定的函数代表"}
    for key, scope in report["scopes"].items():
        lines += [f"## {names[key]}", "", "| 流程 | 布局/映射执行完成（原判据） / 计划 | 其中带警告完成 | 原判据未完成 |", "|---|---:|---:|---:|"]
        for name, stats in scope["by_algorithm"].items():
            lines.append(f"| {name} | {stats['completed']} / {stats['planned']} | {stats['completed_with_warnings']} | {stats['not_completed']} |")
        lines += ["", "原判据集合的描述性配对面积，可能包含后续逻辑/接口审查未通过者，不作效益依据。面积统一为实际节点和路径的最小包围矩形：", "", "| 左流程 | 右流程 | 配对案例数 | 左平均包围矩形面积 / tiles | 右平均包围矩形面积 / tiles |", "|---|---|---:|---:|---:|"]
        for pair in scope["paired_comparisons"]:
            area = pair["metrics"]["occupied_bbox_area_tiles"]
            left, right = area["left_mean_on_paired_cases"], area["right_mean_on_paired_cases"]
            lines.append(f"| {pair['left_algorithm']} | {pair['right_algorithm']} | {area['paired_case_count']} | {left if left is not None else 'NA'} | {right if right is not None else 'NA'} |")
        lines.append("")
    lines += ["每个配对的具体案例、元胞统计、不配对原因和全部失败记录见同目录 JSON。不同流程对的共同完成集合可能不同，不作跨集合优劣排序。", ""]
    if "joined_qualification" in report:
        joined = report["joined_qualification"]
        lines += ["## 独立逻辑与 QCA 端口数量资格", "",
                  "下表追加检查来源与布线后 DAG 的布尔等价性，以及 QCA 输入/输出元胞数量。unsupported 不算通过；这些检查仍不验证器件波形、时钟或功耗。", "",
                  "| 流程 | 原判据执行完成 | DAG equivalent | DAG unsupported | DAG not_equivalent | QCA 输出数量不符 | DAG 等价且 QCA 端口数量符合 |",
                  "|---|---:|---:|---:|---:|---:|---:|"]
        for algorithm, data in joined["by_algorithm"].items():
            counts = data["logic_status_counts"]
            lines.append(f"| {algorithm} | {data['raw_execution_completed']} | {counts.get('equivalent',0)} | {counts.get('unsupported',0)} | {counts.get('not_equivalent',0)} | {data['qca_output_count_mismatches']} | {data['source_dag_and_qca_counts_qualified']} / {data['planned']} |")
        lines += ["", "### 追加资格后的预设函数代表", "", "| 流程 | DAG 等价且 QCA 端口数量符合 / 预设代表总数 |", "|---|---:|"]
        strict_scope = report["qualified_scopes"]["preselected_function_representatives"]
        for algorithm, stats in strict_scope["by_algorithm"].items():
            lines.append(f"| {algorithm} | {stats['completed']} / {stats['planned']} |")
        lines += ["", "追加资格后的共同案例配对面积：", "", "| 左流程 | 右流程 | 配对代表数 | 左平均包围矩形面积 / tiles | 右平均包围矩形面积 / tiles |", "|---|---|---:|---:|---:|"]
        for pair in strict_scope["paired_comparisons"]:
            area = pair["metrics"]["occupied_bbox_area_tiles"]
            lines.append(f"| {pair['left_algorithm']} | {pair['right_algorithm']} | {area['paired_case_count']} | {area['left_mean_on_paired_cases']} | {area['right_mean_on_paired_cases']} |")
        lines += ["", "这些结果属于完成 90 项矩阵时归档的实现。之后生产版本新增逻辑与接口门槛，不代表新版已完成 90 项重跑。原始结果未改；所有追加判定逐行保存在 JSON。", ""]
    return "\n".join(lines)


def self_test():
    cases = [dict(id=name, function_group=group, frozen_sha256=digest) for name, group, digest in
             [("A", "g1", "a"), ("A_duplicate", "g1", "a"), ("A_variant", "g1", "v"), ("B", "g2", "b")]]
    manifest = {"benchmark_version": "test", "cases": cases, "function_groups": [dict(id="g1", representative_case_id="A"), dict(id="g2", representative_case_id="B")]}
    protocol = {"cases": [case["id"] for case in cases], "algorithms": ["left", "right"]}
    rows = []
    for case, algorithm in itertools.product(cases, protocol["algorithms"]):
        fail = (case["id"], algorithm) in {("B", "left"), ("A_variant", "right")}
        value = 1 if case["id"] == "A_variant" else 10 if algorithm == "left" else 20
        rows.append({"case": case["id"], "algorithm": algorithm, "function_group": case["function_group"], "input_sha256": case["frozen_sha256"],
                     "status": "failed" if fail else "completed", "task_completed": not fail, "metrics": {"pnr": {"area_tiles": value + 5}, "mapping": {"cell_count": value * 2}},
                     "summary_geometry": {"verified": True, "area_tiles": value}})
    document = {"protocol": protocol, "rows": rows, "finished": 8, "total": 8}
    summary = summarize(document, manifest, {"eligible_for_comparison": True})
    assert summary["eligible_for_comparison"]
    functions = summary["scopes"]["preselected_function_representatives"]
    assert functions["by_algorithm"]["left"]["planned"] == 2
    assert functions["by_algorithm"]["left"]["completed"] == 1
    area = functions["paired_comparisons"][0]["metrics"]["occupied_bbox_area_tiles"]
    assert [pair["case"] for pair in area["pairs"]] == ["A"]
    assert area["left_mean_on_paired_cases"] == 10 and area["right_mean_on_paired_cases"] == 20
    assert area["pairs"][0]["left_reported_area_tiles"] == 15
    assert summary["scopes"]["all_representations"]["paired_comparisons"][0]["metrics"]["occupied_bbox_area_tiles"]["paired_case_count"] == 2
    assert len(summary["scopes"]["unique_byte_representations"]["case_ids"]) == 3
    incomplete = summarize({**document, "rows": rows[:-1], "finished": 7}, manifest, {"eligible_for_comparison": True})
    assert not incomplete["eligible_for_comparison"] and incomplete["matrix"]["missing"]
    assert incomplete["scopes"]["all_representations"]["by_algorithm"]["right"]["planned"] == 4
    for invalid in ([*rows, rows[0]], [{**rows[0], "case": "unplanned"}, *rows[1:]]):
        try:
            summarize({**document, "rows": invalid}, manifest, {"eligible_for_comparison": True})
        except ValueError:
            pass
        else:
            raise AssertionError("Duplicate or unplanned row was accepted")
    bounds = occupied_geometry({"nodes": [{"x": -2, "y": 1}, {"x": 0, "y": 1}], "routes": [{"path": [[-2, 1], [-2, 3], [0, 3], [0, 1]]}]})
    assert bounds["width"] == 3 and bounds["height"] == 3 and bounds["area_tiles"] == 9
    print("Self-test passed: fixed representatives, paired occupied-bbox cohorts, separately retained reported area, byte deduplication, retained failures, missing and invalid rows.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", nargs="?", type=Path, help="Completed evaluate_backends output directory")
    parser.add_argument("--output", type=Path, help="New summary directory; default DIRECTORY/summary")
    parser.add_argument("--logic-audit", type=Path, help="Independent complete routed-logic batch audit bound to results.json")
    parser.add_argument("--qca-diagnostic", type=Path, help="Independent output-count mismatch list for cross-checking")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    if not args.directory:
        parser.error("directory is required unless --self-test is used")
    directory = args.directory.resolve()
    document = json.loads((directory / "results.json").read_text())
    protocol = json.loads((directory / "protocol.json").read_text())
    checks = json.loads((directory / "final-checks.json").read_text())
    manifest_path = Path(protocol["manifest"])
    manifest = json.loads(manifest_path.read_text())
    selected = set(protocol["cases"])
    integrity = {"protocol_copies_match": document["protocol"] == protocol,
                 "manifest_hash_matches_protocol": sha(manifest_path) == protocol["manifest_sha256"],
                 "selected_frozen_inputs_still_match": all((ROOT / case["frozen_relpath"]).is_file() and sha(ROOT / case["frozen_relpath"]) == case["frozen_sha256"] for case in manifest["cases"] if case["id"] in selected)}
    document, geometry_records, geometry_errors = audit_geometry(document)
    integrity["recorded_candidates_hash_and_geometry_match"] = not geometry_errors
    report = summarize(document, manifest, checks, integrity)
    report["geometry_audit"] = {"definition": "Actual nodes and routed points only; unused margins excluded", "records": geometry_records, "integrity_errors": geometry_errors}
    if args.logic_audit:
        audit = json.loads(args.logic_audit.read_text())
        diagnostic = json.loads(args.qca_diagnostic.read_text()) if args.qca_diagnostic else None
        strict_document, qualification = join_logical_and_interface_audit(document, manifest, audit, sha(directory / "results.json"), diagnostic)
        report["joined_qualification"] = qualification
        strict_integrity = {**integrity, "independent_audit_bindings_passed": not qualification["evidence_errors"]}
        strict_report = summarize(strict_document, manifest, checks, strict_integrity)
        report["qualified_scopes"] = strict_report["scopes"]
        report["qualified_comparison_eligible"] = strict_report["eligible_for_comparison"]
        report["independent_audit_evidence"] = {"logic": {"path": str(args.logic_audit.resolve()), "sha256": sha(args.logic_audit)}}
        if args.qca_diagnostic:
            report["independent_audit_evidence"]["qca_diagnostic"] = {"path": str(args.qca_diagnostic.resolve()), "sha256": sha(args.qca_diagnostic)}
    report["evidence"] = {name: {"path": str(directory / name), "sha256": sha(directory / name)} for name in ("results.json", "protocol.json", "final-checks.json")}
    report["summarizer_sha256"] = sha(Path(__file__))
    report["implementation_snapshot"] = verify_archived_implementation(directory, protocol)
    output = (args.output or directory / "summary").resolve()
    output.mkdir(parents=True, exist_ok=True)
    (output / "summary.json").write_text(json.dumps(report, indent=2, ensure_ascii=False, allow_nan=False) + "\n")
    (output / "summary.md").write_text(markdown(report))
    print(json.dumps({"summary": str(output / "summary.json"), "eligible_for_comparison": report["eligible_for_comparison"],
                      "qualified_comparison_eligible": report.get("qualified_comparison_eligible"),
                      "raw_execution_completed": report.get("joined_qualification", {}).get("raw_execution_completed"),
                      "source_dag_and_qca_counts_qualified": report.get("joined_qualification", {}).get("source_dag_and_qca_counts_qualified"),
                      "matrix": report["matrix"], "function_coverage": report["function_coverage"]}, indent=2))
    return 0 if report["eligible_for_comparison"] and report.get("qualified_comparison_eligible", True) else 1


if __name__ == "__main__":
    raise SystemExit(main())
