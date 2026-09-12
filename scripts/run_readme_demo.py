#!/usr/bin/env python3
"""Run the README XOR example and render figures from its actual artifacts.

Requires the C++ command-line tools, the configured Python layout backend,
Graphviz, and Matplotlib. All generated circuits and reports stay in output/.
"""

import argparse
import csv
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time

from ifcn_agent import configuration, energy_summary

ROOT = Path(__file__).resolve().parents[1]
ALIASES = {
    "source.v": "xor2.v",
    "normalized.v": "xor2_normalized.v",
    "dag.json": "xor2_dag.json",
    "dag.dot": "xor2_dag.dot",
    "layout.ifcn": "xor2_layout.ifcn",
    "device.qca": "xor2_device.qca",
    "pnr.json": "xor2_pnr.json",
    "logic.json": "xor2_logic.json",
}


def read_json(path):
    return json.loads(path.read_text())


def write_json(path, value):
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False, allow_nan=False) + "\n")


def run(command, *, output, env, timeout=600):
    command = [str(item) for item in command]
    print("Running:", " ".join(command), flush=True)
    result = subprocess.run(command, cwd=ROOT, env=env, capture_output=True,
                            text=True, timeout=timeout)
    output.write_text(result.stdout + result.stderr)
    if result.returncode:
        raise RuntimeError(f"Command failed ({result.returncode}); see {output}")
    return result.stdout


def compute(output, env):
    cfg = configuration()
    adapter = [sys.executable, ROOT / "scripts/ifcn_agent.py", "--runs-dir", output / "runs"]
    launched = json.loads(run(
        [*adapter, "start", ROOT / "integrations/pi/examples/xor2.v", "--algorithm", "compact",
         "--until", "map", "--timeout", "180", "--seed", "1"],
        output=output / "pipeline.log", env=env))
    directory = Path(launched["run_directory"])
    deadline = time.monotonic() + 600
    while True:
        manifest = read_json(directory / "manifest.json")
        if manifest["status"] in {"completed", "failed", "cancelled", "interrupted"}:
            break
        if time.monotonic() > deadline:
            subprocess.run([str(x) for x in [*adapter, "cancel", launched["run_id"]]],
                           cwd=ROOT, env=env, capture_output=True, timeout=10)
            raise TimeoutError(f"Pipeline timed out; inspect {directory}")
        time.sleep(0.5)
    if manifest["status"] != "completed":
        raise RuntimeError(f"Pipeline {manifest['status']}: {manifest.get('error')}; see {directory}")
    for source, alias in ALIASES.items():
        shutil.copyfile(directory / manifest["artifacts"][source]["path"], output / alias)

    vectors = output / "xor2_inputs.vt"
    vectors.write_text("a,b\n0,0\n0,1\n1,0\n1,1\n0,0\n0,1\n1,0\n1,1\n")
    shared = [cfg["simulation_binary"], output / "xor2_device.qca",
              "--vectors", vectors, "--repetitions", "1", "--warmup", "0",
              "--require-equivalent", "--equivalence-tolerance", "0", "--output-prefix", output / "xor2"]
    commands = {
        "bistable": [*shared, "--model", "bistable", "--samples", "2048",
                     "--max-iterations", "1000", "--seed", "1", "--json", output / "bistable.json"],
        "coherence": [*shared, "--model", "coherence", "--numeric-method", "rk4",
                      "--time-step", "1e-16", "--duration", "8e-11", "--json", output / "coherence.json"],
        "energy": [cfg["energy_binary"], output / "xor2_device.qca", output / "xor2",
                   "--waveform", "--time-step", "1e-16", "--duration", "8e-11",
                   "--clock-period", "1e-11", "--input-period", "1e-11", "--clock-slope", "1e-12",
                   "--vectors", vectors],
    }
    write_json(output / "commands.json", {key: [str(x) for x in value] for key, value in commands.items()})
    for name, command in commands.items():
        run(command, output=output / f"{name}.log", env=env)


def summarize(output):
    pnr = read_json(output / "xor2_pnr.json")
    logic = read_json(output / "xor2_logic.json")
    if not pnr["layout_legal"] or not pnr["clock_legal"] or logic["status"] != "equivalent":
        raise RuntimeError("Layout legality, clock legality, or source-to-DAG equivalence failed")
    comparisons = {}
    for model in ("bistable", "coherence"):
        pair = read_json(output / f"{model}.json")["comparisons"][0]
        accuracy = pair["accuracy"]
        if not accuracy["comparable"] or accuracy["output_samples"] <= 0 or accuracy["max_absolute_error"] != 0:
            raise RuntimeError(f"{model}: incomplete or unequal baseline/accelerated results")
        comparisons[model] = {
            "output_samples": accuracy["output_samples"],
            "max_absolute_error": accuracy["max_absolute_error"],
            "integration_samples": pair["work"]["samples"],
            "max_iteration_samples": pair["work"].get("max_iteration_samples"),
            "scope": "baseline versus accelerated engine; not device versus RTL",
        }
    energy = energy_summary(output / "xor2_energy.txt", 1e-11)
    summary = {
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "source": "integrations/pi/examples/xor2.v",
        "expression": "y = a ^ b",
        "algorithm": pnr["algorithm"],
        "layout": {key: pnr[key] for key in ("width", "height", "area_tiles", "phase_count", "layout_legal", "clock_legal")},
        "logic": {key: logic[key] for key in ("status", "vectors_checked", "expected_vector_count")},
        "device_cells": (output / "xor2_device.qca").read_text().count("[TYPE:QCADCell]"),
        "mapping_metric_cells_before_export_normalization": pnr["mapping_validation"]["cell_count"],
        "simulation": comparisons,
        "energy": energy,
        "limitations": [
            "Source-to-DAG Boolean equivalence does not prove device-level timing or functionality.",
            "Energy is a model diagnostic; the initial counted cycle has a large energy-balance residual.",
            "Time-step convergence has not been tested; these values are not a converged power estimate.",
        ],
        "sha256": {name: hashlib.sha256((output / name).read_bytes()).hexdigest()
                   for name in [*ALIASES.values(), "xor2_inputs.vt", "bistable.json", "coherence.json", "xor2_energy.txt"]},
    }
    write_json(output / "summary.json", summary)
    return summary


def render_dag(output):
    dag = read_json(output / "xor2_dag.json")
    lines = ["digraph xor2 {", 'graph [rankdir=LR, bgcolor="white", pad="0.45", nodesep="0.7", ranksep="0.85", dpi=180];',
             'node [shape=box, style="rounded,filled", fontname="DejaVu Sans", fontsize=16, margin="0.20,0.14", penwidth=1.5, color="#64748b", fillcolor="#f1f5f9"];',
             'edge [color="#64748b", penwidth=1.8, arrowsize=0.8];']
    for node in dag["nodes"]:
        label = node["name"] + "\n" + node["type"].upper()
        fill = "#dbeafe" if node["is_input"] else "#ccfbf1" if node["is_output"] else "#f1f5f9"
        lines.append(f'n{node["id"]} [label={json.dumps(label)}, fillcolor="{fill}"];')
    for left, right in dag["edges"]:
        lines.append(f"n{left} -> n{right};")
    lines.append("}")
    dotfile = output / "xor2_dag_styled.dot"
    dotfile.write_text("\n".join(lines) + "\n")
    for extension in ("png", "svg"):
        subprocess.run(["dot", f"-T{extension}", str(dotfile), "-o", str(output / f"xor2_dag.{extension}")],
                       check=True, timeout=30)


def csv_section(report, name):
    return list(csv.DictReader(report.split(f"[{name}]", 1)[1].split(f"[#{name}]", 1)[0].strip().splitlines()))


def render_energy(output, summary):
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    from matplotlib.colors import Normalize, TwoSlopeNorm

    text = (output / "xor2_energy.txt").read_text()
    cycles = csv_section(text, "PER_CYCLE")
    cells = csv_section(text, "PER_CELL")
    plt.rcParams.update({"font.family": "DejaVu Sans", "font.size": 11, "axes.titleweight": "bold",
                         "axes.edgecolor": "#cbd5e1", "axes.labelcolor": "#334155", "text.color": "#0f172a"})
    fig, axes = plt.subplots(1, 2, figsize=(13.2, 5.8), gridspec_kw={"width_ratios": [1.05, 1]})
    fig.subplots_adjust(top=0.76, bottom=0.20, left=0.065, right=0.955, wspace=0.32)
    fig.text(0.065, 0.94, "XOR2 · energy model diagnostic", fontsize=21, weight="bold")
    fig.text(0.065, 0.875, f"{summary['device_cells']} QCA cells   |   80 ps duration   |   0.1 fs time step   |   10 ps clock / input period", color="#475569", fontsize=12)
    clock = [int(row["cycle"]) for row in cycles]
    axes[0].bar([x - 0.19 for x in clock], [float(row["E_bath_eV"]) * 1000 for row in cycles],
                width=0.38, color="#0f766e", label="Bath energy")
    axes[0].bar([x + 0.19 for x in clock], [float(row["E_error_eV"]) * 1000 for row in cycles],
                width=0.38, color="#fb923c", label="Energy-balance residual")
    axes[0].set(title="Per-cycle energy", xlabel="Counted clock cycle", ylabel="Energy (meV)")
    axes[0].set_xticks(clock)
    axes[0].grid(axis="y", color="#e2e8f0", linewidth=0.8)
    axes[0].set_axisbelow(True)
    axes[0].legend(frameon=False, fontsize=9, loc="upper right")
    x = [float(row["x"]) for row in cells]
    y = [float(row["y"]) for row in cells]
    energy = [float(row["E_bath_eV"]) * 1000 for row in cells]
    signed = min(energy) < 0
    scale = max(abs(value) for value in energy) or 1
    norm = TwoSlopeNorm(vmin=-scale, vcenter=0, vmax=scale) if signed else Normalize(vmin=0, vmax=scale)
    points = axes[1].scatter(x, y, c=energy, cmap="RdBu_r" if signed else "YlGnBu", norm=norm,
                              marker="s", s=80, edgecolors="#94a3b8", linewidths=0.4)
    for row in cells:
        if row["function"] in {"INPUT", "OUTPUT"}:
            axes[1].annotate(row["name"], (float(row["x"]), float(row["y"])),
                             xytext=(7, 6), textcoords="offset points", weight="bold", fontsize=11)
    axes[1].set(title="Bath energy by physical cell", xlabel="x (nm)", ylabel="y (nm)", aspect="equal")
    axes[1].invert_yaxis()
    fig.colorbar(points, ax=axes[1], label="Bath energy / cell (meV)", shrink=0.85, pad=0.04)
    for ax in axes:
        ax.spines[["top", "right"]].set_visible(False)
    fig.text(0.065, 0.087, "Actual simulator report · Initial counted cycle dominates the residual; time-step convergence is untested.",
             fontsize=10, color="#9a3412")
    fig.text(0.065, 0.044, "Model output for inspecting the workflow. Boolean DAG equivalence and engine agreement do not establish physical signoff.",
             fontsize=9.5, color="#64748b")
    for extension in ("png", "svg"):
        fig.savefig(output / f"xor2_energy.{extension}", dpi=180, facecolor="white")
    plt.close(fig)


def screenshots(output, target, env):
    target.mkdir(parents=True, exist_ok=True)
    gui = Path(configuration()["build_dir"]) / "fcnx_gui"
    views = [("01-source", "source", "xor2.v"),
             ("03-routing", "schematic", "xor2_layout.ifcn"),
             ("04-device", "layout", "xor2_device.qca"),
             ("06-waveform", "waveform", "xor2_bistable_baseline.rst"),
             ("06-coherence", "waveform", "xor2_coherence_baseline.rst"),
             ("05-structure", "structure", "xor2_device.qca")]
    for name, view, filename in views:
        capture = env.copy()
        capture.update(QT_QPA_PLATFORM="offscreen", IFCN_UI_SCREENSHOT=str(target / f"{name}.png"),
                       IFCN_UI_SCREENSHOT_INPUT=str(output / filename), IFCN_UI_SCREENSHOT_VIEW=view,
                       IFCN_UI_SOURCE_FILE=str(output / "xor2.v"))
        run([gui], output=output / f"screenshot-{name}.log", env=capture, timeout=60)
    shutil.copyfile(output / "xor2_dag.png", target / "02-logic.png")
    shutil.copyfile(output / "xor2_energy.png", target / "07-energy.png")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", "--output-dir", type=Path, default=ROOT / "output/readme-demo")
    parser.add_argument("--build-dir", type=Path, help="C++ binaries; defaults to IFCN_BUILD_DIR or build/")
    parser.add_argument("--python", help="Python interpreter containing the layout backend dependencies")
    parser.add_argument("--bindings-dir", type=Path, help="Directory containing the compiled gcn_rl_layout Python extension")
    parser.add_argument("--reuse-data", action="store_true", help="Validate/render existing artifacts without rerunning algorithms")
    parser.add_argument("--screenshots", nargs="?", type=Path, const=Path("__DEFAULT__"),
                        help="Also save native Qt screenshots; defaults to OUTPUT/screenshots")
    args = parser.parse_args()
    if args.build_dir:
        os.environ["IFCN_BUILD_DIR"] = str(args.build_dir.resolve())
    if args.python:
        os.environ["IFCN_PYTHON"] = str(Path(shutil.which(args.python) or args.python).resolve())
    if args.bindings_dir:
        os.environ["IFCN_GCN_RL_BINDINGS_DIR"] = str(args.bindings_dir.resolve())
    env = os.environ.copy()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if not args.reuse_data:
        compute(output, env)
    summary = summarize(output)
    render_dag(output)
    render_energy(output, summary)
    if args.screenshots:
        target = output / "screenshots" if args.screenshots == Path("__DEFAULT__") else args.screenshots.resolve()
        screenshots(output, target, env)
    print(f"Done: {output / 'summary.json'}", flush=True)


if __name__ == "__main__":
    main()
