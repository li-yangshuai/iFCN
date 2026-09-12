#!/usr/bin/env python3
"""Build the occupied-layer merge ablation report from two layout cohorts."""

import argparse
import csv
import json
import statistics
from pathlib import Path


FIELDS = (
    "suite",
    "circuit",
    "benchmark",
    "nodes",
    "edges",
    "inputs",
    "outputs",
    "layers",
    "baseline_width",
    "baseline_height",
    "baseline_area",
    "merged_width",
    "merged_height",
    "merged_area",
    "area_reduction",
    "area_reduction_percent",
    "accepted_row_merges",
    "accepted_col_merges",
    "accepted_merge_count",
    "merge_evaluations",
    "failed_edge_count",
    "clock_template_ok",
    "clock_template_conflicts",
)


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline-results", required=True)
    parser.add_argument("--merged-results", required=True)
    parser.add_argument("--output-dir", required=True)
    return parser.parse_args()


def load_records(path):
    payload = json.loads(Path(path).read_text(encoding="utf-8"))
    return {record["benchmark"]: record for record in payload["records"]}


def load_history(record):
    summary_path = (
        Path(record["output_dir"])
        / (record["circuit"] + "_normal_graph_draw_summary.json")
    )
    return json.loads(summary_path.read_text(encoding="utf-8")).get(
        "contraction_history", []
    )


def make_rows(baseline, merged):
    if set(baseline) != set(merged):
        missing = sorted(set(baseline).symmetric_difference(merged))
        raise RuntimeError("cohort mismatch: {}".format(", ".join(missing)))
    rows = []
    for benchmark in sorted(baseline):
        before, after = baseline[benchmark], merged[benchmark]
        history = load_history(after)
        row_merges = sum(
            item.get("mode") == "adjacent_row_merge" for item in history
        )
        col_merges = sum(
            item.get("mode") == "adjacent_col_merge" for item in history
        )
        old_area, new_area = int(before["area"]), int(after["area"])
        reduction = old_area - new_area
        rows.append(
            {
                "suite": after["suite"],
                "circuit": after["circuit"],
                "benchmark": benchmark,
                "nodes": int(after["nodes"]),
                "edges": int(after["edges"]),
                "inputs": int(after["inputs"]),
                "outputs": int(after["outputs"]),
                "layers": int(after["layers"]),
                "baseline_width": int(before["width"]),
                "baseline_height": int(before["height"]),
                "baseline_area": old_area,
                "merged_width": int(after["width"]),
                "merged_height": int(after["height"]),
                "merged_area": new_area,
                "area_reduction": reduction,
                "area_reduction_percent": round(
                    100.0 * reduction / old_area if old_area else 0.0, 6
                ),
                "accepted_row_merges": int(row_merges),
                "accepted_col_merges": int(col_merges),
                "accepted_merge_count": int(row_merges + col_merges),
                "merge_evaluations": int(
                    after.get("contraction_layer_merge_evaluations", 0) or 0
                ),
                "failed_edge_count": int(after["failed_edge_count"]),
                "clock_template_ok": bool(after["clock_template_ok"]),
                "clock_template_conflicts": int(after["clock_template_conflicts"]),
            }
        )
    return rows


def deduplicate(rows):
    selected = {}
    for row in sorted(rows, key=lambda item: (item["suite"], item["circuit"])):
        selected.setdefault(row["circuit"].casefold(), row)
    return sorted(selected.values(), key=lambda item: (item["suite"], item["circuit"]))


def summarize(rows):
    old_total = sum(row["baseline_area"] for row in rows)
    new_total = sum(row["merged_area"] for row in rows)
    percents = [row["area_reduction_percent"] for row in rows]
    return {
        "circuit_count": len(rows),
        "zero_failure_count": sum(row["failed_edge_count"] == 0 for row in rows),
        "clock_legal_count": sum(
            row["clock_template_ok"] and row["clock_template_conflicts"] == 0
            for row in rows
        ),
        "improved_count": sum(row["area_reduction"] > 0 for row in rows),
        "equal_count": sum(row["area_reduction"] == 0 for row in rows),
        "worse_count": sum(row["area_reduction"] < 0 for row in rows),
        "accepted_merge_circuit_count": sum(
            row["accepted_merge_count"] > 0 for row in rows
        ),
        "accepted_merge_count": sum(row["accepted_merge_count"] for row in rows),
        "merge_evaluation_count": sum(row["merge_evaluations"] for row in rows),
        "baseline_total_area": old_total,
        "merged_total_area": new_total,
        "total_area_reduction": old_total - new_total,
        "weighted_area_reduction_percent": round(
            100.0 * (old_total - new_total) / old_total if old_total else 0.0, 6
        ),
        "mean_area_reduction_percent": round(statistics.mean(percents), 6),
        "median_area_reduction_percent": round(statistics.median(percents), 6),
    }


def latex_escape(value):
    text = str(value)
    for old, new in (
        ("\\", r"\textbackslash{}"),
        ("_", r"\_"),
        ("%", r"\%"),
        ("&", r"\&"),
        ("#", r"\#"),
    ):
        text = text.replace(old, new)
    return text


def write_latex(path, rows, summary):
    lines = [
        r"\documentclass[10pt]{article}",
        r"\usepackage[a4paper,margin=1.2cm,landscape]{geometry}",
        r"\usepackage{booktabs,longtable}",
        r"\begin{document}",
        r"\small",
        r"\begin{longtable}{llrrrrrrrrr}",
        r"\caption{Ablation of occupied adjacent-layer merging. Only layouts with zero failed routes and a legal 2DDWave clock template are reported.}\label{tab:layer_merge_ablation}\\",
        r"\toprule",
        r"Dataset & Circuit & Nodes & Edges & I/O & Layers & Baseline & Merged & $\Delta A$ & $\Delta A$ (\%) & Merges \\",
        r"\midrule",
        r"\endfirsthead",
        r"\toprule",
        r"Dataset & Circuit & Nodes & Edges & I/O & Layers & Baseline & Merged & $\Delta A$ & $\Delta A$ (\%) & Merges \\",
        r"\midrule",
        r"\endhead",
    ]
    previous_suite = None
    for row in rows:
        suite = row["suite"] if row["suite"] != previous_suite else ""
        previous_suite = row["suite"]
        io_count = row["inputs"] + row["outputs"]
        lines.append(
            "{} & {} & {} & {} & {} & {} & {} & {} & {} & {:.2f} & {} \\\\".format(
                latex_escape(suite),
                latex_escape(row["circuit"]),
                row["nodes"],
                row["edges"],
                io_count,
                row["layers"],
                row["baseline_area"],
                row["merged_area"],
                row["area_reduction"],
                row["area_reduction_percent"],
                row["accepted_merge_count"],
            )
        )
    lines.extend(
        [
            r"\bottomrule",
            r"\end{longtable}",
            "Matched circuits: {}; improved/equal/worse: {}/{}/{}; "
            "accepted merges: {}; total area: {} $\\rightarrow$ {} "
            "($-{:.2f}\\%$).".format(
                summary["circuit_count"],
                summary["improved_count"],
                summary["equal_count"],
                summary["worse_count"],
                summary["accepted_merge_count"],
                summary["baseline_total_area"],
                summary["merged_total_area"],
                summary["weighted_area_reduction_percent"],
            ),
            r"\end{document}",
            "",
        ]
    )
    path.write_text("\n".join(lines), encoding="utf-8")


def write_csv(path, rows):
    with path.open("w", newline="", encoding="utf-8") as handle:
        writer = csv.DictWriter(handle, fieldnames=FIELDS)
        writer.writeheader()
        writer.writerows(rows)


def main():
    args = parse_args()
    output_dir = Path(args.output_dir).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    rows = make_rows(
        load_records(args.baseline_results), load_records(args.merged_results)
    )
    unique_rows = deduplicate(rows)
    all_summary = summarize(rows)
    unique_summary = summarize(unique_rows)
    write_csv(output_dir / "layer_merge_ablation_all_89.csv", rows)
    write_csv(output_dir / "layer_merge_ablation_unique_61.csv", unique_rows)
    (output_dir / "layer_merge_ablation.json").write_text(
        json.dumps(
            {
                "baseline_results": str(Path(args.baseline_results).resolve()),
                "merged_results": str(Path(args.merged_results).resolve()),
                "all_circuits_summary": all_summary,
                "unique_circuits_summary": unique_summary,
                "all_circuits": rows,
                "unique_circuits": unique_rows,
            },
            indent=2,
            ensure_ascii=False,
        )
        + "\n",
        encoding="utf-8",
    )
    write_latex(
        output_dir / "layer_merge_ablation_unique_61.tex",
        unique_rows,
        unique_summary,
    )
    print(json.dumps(all_summary, indent=2))


if __name__ == "__main__":
    main()
