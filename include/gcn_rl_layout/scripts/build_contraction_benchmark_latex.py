#!/usr/bin/env python3
"""Build the grouped successful-circuit P&R/contraction benchmark table."""

import argparse
import csv
import json
import shutil
from collections import OrderedDict
from pathlib import Path


CSV_FIELDS = (
    "dataset",
    "circuit",
    "nodes",
    "edges",
    "inputs",
    "outputs",
    "layers",
    "layout_area",
    "layout_routing_runtime_sec",
    "compacted_area",
    "contraction_runtime_sec",
    "area_reduction_percent",
)

DATASET_ORDER = (
    "ISCAS85",
    "IWLS93",
    "MAJ",
    "TOY",
    "fontes18",
    "trindade16",
)


def parse_args():
    parser = argparse.ArgumentParser(
        description="Create a dataset-grouped LaTeX table for legal layouts only."
    )
    parser.add_argument("--results", required=True, help="layout_results.json")
    parser.add_argument("--output-dir", required=True)
    parser.add_argument(
        "--rows-per-page",
        type=int,
        default=35,
        help="Target body-row count before a forced dataset-boundary page break.",
    )
    parser.add_argument(
        "--collect-layouts",
        action="store_true",
        help="Copy each selected unique circuit output into one publication folder.",
    )
    return parser.parse_args()


def read_json(path):
    with Path(path).open("r", encoding="utf-8") as handle:
        return json.load(handle)


def latex_escape(value):
    text = str(value)
    for source, replacement in (
        ("\\", r"\textbackslash{}"),
        ("&", r"\&"),
        ("%", r"\%"),
        ("$", r"\$"),
        ("#", r"\#"),
        ("_", r"\_"),
        ("{", r"\{"),
        ("}", r"\}"),
    ):
        text = text.replace(source, replacement)
    return text


def require_number(record, field, integer=False):
    value = record.get(field, "")
    if value in (None, ""):
        raise RuntimeError(
            "record {} is missing {}; rerun the timed benchmark cohort".format(
                record.get("benchmark", "<unknown>"), field
            )
        )
    return int(value) if integer else float(value)


def selected_records(data):
    """Keep only zero-failure, template-legal, fully exported layouts."""
    selected = []
    for record in data.get("records", []):
        if record.get("status") != "pass":
            continue
        if int(record.get("failed_edge_count", -1)) != 0:
            continue
        if not bool(record.get("clock_template_ok", False)):
            continue
        if not bool(record.get("ifcn_exists", False)):
            continue
        selected.append(record)
    return selected


def deduplicate_records(records):
    """Keep one deterministic record for each case-insensitive circuit name."""
    priorities = {name: index for index, name in enumerate(DATASET_ORDER)}
    ordered = sorted(
        records,
        key=lambda record: (
            priorities.get(str(record.get("suite", "")), len(priorities)),
            str(record.get("benchmark", "")).casefold(),
        ),
    )
    unique = []
    seen = set()
    for record in ordered:
        name_key = str(record.get("circuit", "")).casefold()
        if name_key in seen:
            continue
        seen.add(name_key)
        unique.append(record)
    return unique


def make_rows(records):
    rows = []
    for record in records:
        before = require_number(record, "pre_contraction_area", integer=True)
        after = require_number(record, "area", integer=True)
        rows.append(
            {
                "dataset": str(record["suite"]),
                "circuit": str(record["circuit"]),
                "nodes": require_number(record, "nodes", integer=True),
                "edges": require_number(record, "edges", integer=True),
                "inputs": require_number(record, "inputs", integer=True),
                "outputs": require_number(record, "outputs", integer=True),
                "layers": require_number(record, "layers", integer=True),
                "layout_area": before,
                "layout_routing_runtime_sec": require_number(
                    record, "layout_routing_runtime_sec"
                ),
                "compacted_area": after,
                "contraction_runtime_sec": require_number(
                    record, "contraction_runtime_sec"
                ),
                "area_reduction_percent": (
                    100.0 * (before - after) / before if before > 0 else 0.0
                ),
            }
        )
    return rows


def grouped_rows(rows):
    priorities = {name: index for index, name in enumerate(DATASET_ORDER)}
    groups = OrderedDict()
    rows = sorted(
        rows,
        key=lambda row: (
            priorities.get(row["dataset"], len(priorities)),
            row["circuit"].casefold(),
        ),
    )
    for row in rows:
        groups.setdefault(row["dataset"], []).append(row)
    for dataset in groups:
        groups[dataset].sort(key=lambda row: row["circuit"].casefold())
    return groups


def render_latex(groups, rows_per_page):
    latex = [
        r"\documentclass{article}",
        r"\usepackage[margin=1.0cm,landscape]{geometry}",
        r"\usepackage{booktabs,longtable,multirow}",
        r"\usepackage[T1]{fontenc}",
        r"\setlength{\tabcolsep}{3.4pt}",
        r"\renewcommand{\arraystretch}{0.82}",
        r"\begin{document}",
        r"\scriptsize",
        r"\begin{longtable}{llrrrrrrrr}",
        r"\caption{Circuit characteristics, layout-and-routing results, and phase-aware contraction runtime for successful circuits only.}\label{tab:phase_contraction_all_success}\\",
        r"\toprule",
        r"Dataset & Circuit & Nodes & Edges & I/O & Levels & Area$_{\mathrm{P\&R}}$ & $t_{\mathrm{P\&R}}$ (s) & Area$_{\mathrm{C}}$ & $t_{\mathrm{C}}$ (s) \\",
        r"\midrule",
        r"\endfirsthead",
        r"\multicolumn{10}{c}{\tablename\ \thetable{} -- continued}\\",
        r"\toprule",
        r"Dataset & Circuit & Nodes & Edges & I/O & Levels & Area$_{\mathrm{P\&R}}$ & $t_{\mathrm{P\&R}}$ (s) & Area$_{\mathrm{C}}$ & $t_{\mathrm{C}}$ (s) \\",
        r"\midrule",
        r"\endhead",
        r"\midrule",
        r"\multicolumn{10}{r}{Continued on next page}\\",
        r"\endfoot",
        r"\bottomrule",
        r"\endlastfoot",
    ]
    page_rows = 0
    for group_index, (dataset, rows) in enumerate(groups.items()):
        if (
            group_index > 0
            and page_rows > 0
            and page_rows + len(rows) > max(1, int(rows_per_page))
        ):
            latex.append(r"\pagebreak[4]")
            page_rows = 0
        elif group_index > 0:
            latex.append(r"\addlinespace[1.5pt]")

        for row_index, row in enumerate(rows):
            if row_index != 0:
                dataset_cell = ""
            elif len(rows) == 1:
                dataset_cell = latex_escape(dataset)
            else:
                dataset_cell = r"\multirow{{{}}}{{*}}{{{}}}".format(
                    len(rows), latex_escape(dataset)
                )
            latex.append(
                "{} & {} & {} & {} & {}/{} & {} & {} & {:.4f} & {} & {:.4f} \\\\".format(
                    dataset_cell,
                    latex_escape(row["circuit"]),
                    row["nodes"],
                    row["edges"],
                    row["inputs"],
                    row["outputs"],
                    row["layers"],
                    row["layout_area"],
                    row["layout_routing_runtime_sec"],
                    row["compacted_area"],
                    row["contraction_runtime_sec"],
                )
            )
        page_rows += len(rows)

    latex.extend(
        [
            r"\end{longtable}",
            r"\noindent\footnotesize Notes: I/O denotes primary inputs/primary outputs. "
            r"Area$_{\mathrm{P\&R}}$ is the routed bounding-box area before contraction; "
            r"Area$_{\mathrm{C}}$ is the area after global node contraction, recursive "
            r"internal-layer contraction, and node-empty row/column deletion. "
            r"Only layouts with zero failed edges and a legal 2DDWave clock template are included. "
            r"Duplicate circuit names across datasets are matched case-insensitively and reported once.",
            r"\end{document}",
            "",
        ]
    )
    return "\n".join(latex)


def safe_component(value):
    text = str(value).strip()
    return "".join(ch if ch.isalnum() or ch in "-_." else "_" for ch in text)


def collect_layouts(records, output_dir):
    collection_root = output_dir / "successful_layouts_zero_failure_unique"
    if collection_root.exists() and any(collection_root.iterdir()):
        raise RuntimeError(
            "layout collection already exists and is non-empty: {}".format(
                collection_root
            )
        )
    collection_root.mkdir(parents=True, exist_ok=True)
    manifest_rows = []
    for record in records:
        source = Path(record["output_dir"])
        if not source.is_dir():
            raise RuntimeError("missing circuit output directory: {}".format(source))
        dataset = safe_component(record["suite"])
        circuit = safe_component(record["circuit"])
        destination = collection_root / dataset / circuit
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copytree(str(source), str(destination))
        manifest_rows.append(
            {
                "dataset": str(record["suite"]),
                "circuit": str(record["circuit"]),
                "benchmark": str(record["benchmark"]),
                "failed_edge_count": int(record["failed_edge_count"]),
                "clock_template_ok": bool(record["clock_template_ok"]),
                "relative_output_dir": str(destination.relative_to(collection_root)),
            }
        )

    with (collection_root / "layout_manifest.csv").open(
        "w", encoding="utf-8", newline=""
    ) as handle:
        writer = csv.DictWriter(handle, fieldnames=tuple(manifest_rows[0]))
        writer.writeheader()
        writer.writerows(manifest_rows)
    readme = [
        "# 零失败唯一电路版图集合",
        "",
        "- 电路数量：{}；".format(len(manifest_rows)),
        "- 筛选条件：失败边为 0、2DDWave 时钟模板合法、IFCN 已成功导出；",
        "- 重名规则：电路名不区分大小写，每个名字只保留一个 dataset 记录；",
        "- 目录结构：`<dataset>/<circuit>/`；",
        "- 每个电路目录保留 IFCN、编码 IFCN、summary JSON、运行日志和门级版图 TeX；",
        "- `gate_level_latex_manifest.csv` 给出61个门级 TeX 的相对路径。",
        "",
        "详细对应关系见 `layout_manifest.csv`。",
        "",
    ]
    (collection_root / "README.md").write_text(
        "\n".join(readme), encoding="utf-8"
    )
    return collection_root


def main():
    args = parse_args()
    data = read_json(args.results)
    selected = selected_records(data)
    records = deduplicate_records(selected)
    rows = make_rows(records)
    groups = grouped_rows(rows)
    output_dir = Path(args.output_dir).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    csv_path = output_dir / "successful_contraction_benchmarks.csv"
    with csv_path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=CSV_FIELDS)
        writer.writeheader()
        writer.writerows(rows)

    summary = {
        "source_results": str(Path(args.results).resolve()),
        "selection": "status=pass, failed_edge_count=0, clock_template_ok=true, ifcn_exists=true",
        "deduplication": "case-insensitive circuit name; dataset priority then benchmark path",
        "successful_record_count_before_deduplication": len(selected),
        "duplicate_record_count_removed": len(selected) - len(records),
        "circuit_count": len(rows),
        "dataset_counts": {dataset: len(group) for dataset, group in groups.items()},
        "records": rows,
    }
    (output_dir / "successful_contraction_benchmarks.json").write_text(
        json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8"
    )
    (output_dir / "successful_contraction_benchmarks.tex").write_text(
        render_latex(groups, args.rows_per_page), encoding="utf-8"
    )
    readme = [
        "# 零失败电路布局布线与收缩汇总",
        "",
        "- 原始零失败成功记录：{}；".format(len(selected)),
        "- 去除重名记录：{}；".format(len(selected) - len(records)),
        "- 最终唯一电路：{}；".format(len(records)),
        "- Dataset：{}。".format(
            "、".join("{}({})".format(name, len(group)) for name, group in groups.items())
        ),
        "",
        "## 文件",
        "",
        "- `successful_contraction_benchmarks.tex/pdf`：可独立编译的两页 LaTeX 长表；",
        "- `successful_contraction_benchmarks.csv/json`：表格源数据；",
        "- `successful_layouts_zero_failure_unique/`：61 个唯一成功电路的集中版图目录；",
        "- `successful_layouts_zero_failure_unique/gate_level_latex_manifest.csv`：门级版图 TeX 索引。",
        "",
        "计时采用单任务顺序运行。布局布线时间不含三阶段收缩；收缩时间包含每个候选",
        "触发的全网重布线和 2DDWave 合法性校验。失败边不为 0 的电路未进入本目录。",
        "",
    ]
    (output_dir / "README.md").write_text("\n".join(readme), encoding="utf-8")
    collection_root = (
        collect_layouts(records, output_dir) if args.collect_layouts else None
    )
    print("TABLE_DIR {}".format(output_dir))
    print("CIRCUITS {}".format(len(rows)))
    print("DATASETS {}".format(json.dumps(summary["dataset_counts"], ensure_ascii=False)))
    if collection_root is not None:
        print("LAYOUT_COLLECTION {}".format(collection_root))


if __name__ == "__main__":
    main()
