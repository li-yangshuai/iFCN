#!/usr/bin/env python3
"""Run the README XOR example and render figures from its actual artifacts.

Requires the C++ command-line tools, the configured Python layout backend,
Graphviz, and Matplotlib. Generated circuits and reports stay in the selected
output directory.
"""

import argparse
import csv
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[2]
sys.path[:0] = [str(ROOT / "src/python"), str(ROOT / "tests/support")]

from ifcn.workflow import configuration, energy_summary
from ifcn.logic import analyze
from validate_sequential_energy_waveform import parse_rst
INPUT_COMBINATIONS = [(0, 0), (0, 1), (1, 0), (1, 1)] * 2
HOLD_CYCLES = 8
CHECK_CYCLES = 3
TOTAL_CYCLES = len(INPUT_COMBINATIONS) * HOLD_CYCLES
ENERGY_PROFILE = {"duration_s": 8e-11, "time_step_s": 1e-16,
                  "clock_period_s": 1e-11, "input_period_s": 1e-11,
                  "clock_slope_s": 1e-12, "input_cycles_per_combination": 1,
                  "scope": "Separate eight-cycle energy demonstration on the same device; not the slow-input functional run"}
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


def compute_mapping(output, env):
    """Run the same validated pipeline used by the complete demonstration."""
    adapter = [sys.executable, ROOT / "src/python/ifcn/workflow.py", "--runs-dir", output / "runs"]
    launched = json.loads(run(
        [*adapter, "start", ROOT / "tests/benchmarks_f/TOY/xor2_demo.v", "--algorithm", "normal_2ddwave",
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

    pnr = read_json(output / "xor2_pnr.json")
    logic = read_json(output / "xor2_logic.json")
    if not pnr["layout_legal"] or not pnr["clock_legal"] or logic["status"] != "equivalent":
        raise RuntimeError("Layout legality, clock legality, or source-to-DAG equivalence failed")
    summary = {
        "schema": "ifcn.readme.mapping.v1",
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "run_directory": str(directory),
        "algorithm": pnr["algorithm"],
        "layout": {key: pnr[key] for key in ("width", "height", "area_tiles", "phase_count", "layout_legal", "clock_legal")},
        "logic": {key: logic[key] for key in ("status", "vectors_checked", "expected_vector_count")},
        "device_cells": (output / "xor2_device.qca").read_text().count("[TYPE:QCADCell]"),
        "sha256": {name: hashlib.sha256((output / name).read_bytes()).hexdigest()
                   for name in ALIASES.values()},
        "scope": "Source equivalence and mapped layout only; physical simulation has not been run by this stage.",
    }
    write_json(output / "map_summary.json", summary)
    return summary


def compute(output, env):
    compute_mapping(output, env)
    cfg = configuration()

    vectors = output / "xor2_inputs.vt"
    vectors.write_text("a,b\n" + "".join(f"{a},{b}\n" * HOLD_CYCLES for a, b in INPUT_COMBINATIONS))
    energy_vectors = output / "xor2_energy_inputs.vt"
    energy_vectors.write_text("a,b\n" + "".join(f"{a},{b}\n" for a, b in INPUT_COMBINATIONS))
    write_json(output / "energy_profile.json", ENERGY_PROFILE)
    shared = [cfg["simulation_binary"], output / "xor2_device.qca",
              "--vectors", vectors, "--repetitions", "1", "--warmup", "0",
              "--require-equivalent", "--equivalence-tolerance", "0", "--output-prefix", output / "xor2"]
    commands = {
        "bistable": [*shared, "--model", "bistable", "--samples", "8192",
                     "--max-iterations", "1000", "--seed", "1", "--json", output / "bistable.json"],
        "coherence": [*shared, "--model", "coherence", "--numeric-method", "rk4",
                      "--time-step", "1e-16", "--duration", "6.4e-10", "--json", output / "coherence.json"],
        "energy": [cfg["energy_binary"], output / "xor2_device.qca", output / "xor2",
                   "--waveform", "--time-step", str(ENERGY_PROFILE["time_step_s"]),
                   "--duration", str(ENERGY_PROFILE["duration_s"]),
                   "--clock-period", str(ENERGY_PROFILE["clock_period_s"]),
                   "--input-period", str(ENERGY_PROFILE["input_period_s"]),
                   "--clock-slope", str(ENERGY_PROFILE["clock_slope_s"]), "--vectors", energy_vectors],
    }
    write_json(output / "commands.json", {key: [str(x) for x in value] for key, value in commands.items()})
    for name, command in commands.items():
        run(command, output=output / f"{name}.log", env=env, timeout=1800)
        if name in ("bistable", "coherence"):
            validate_function(output, name)


def validate_function(output, model):
    """Compare settled output hold windows against the independent RTL oracle.

    Simon records every Bistable step and decimates Coherence traces. Recover
    the exact recorded step indices from its documented sampling policy, then
    check the final three cycles of each eight-cycle input block.
    """
    source = analyze((output / "xor2_normalized.v").read_bytes())
    if source["input_ports"] != ["a", "b"] or source["output_ports"] != ["y"]:
        raise RuntimeError("README fixture must expose scalar inputs a,b and output y")
    pairs = read_json(output / f"{model}.json")["comparisons"]
    if len(pairs) != 1 or pairs[0]["model"] != model:
        raise RuntimeError(f"Unexpected simulation report for {model}")
    pair = pairs[0]
    steps = int(pair["work"]["samples"])
    stride = max(1, (steps - 1) // 3000) if model == "coherence" else 1
    sample_count, traces = parse_rst(output / f"xor2_{model}_baseline.rst")
    if sample_count != (steps - 1) // stride + 1:
        raise RuntimeError(f"{model}: recorded samples do not match the simulation sampling policy")
    outputs = []
    for cell in (output / "xor2_device.qca").read_text().split("[TYPE:QCADCell]")[1:]:
        cell = cell.split("[#TYPE:QCADCell]", 1)[0]
        if "cell_function=QCAD_CELL_OUTPUT" in cell:
            outputs.append((re.search(r"^psz=(.*)$", cell, re.M).group(1),
                            int(re.search(r"^cell_options.clock=(\d+)$", cell, re.M).group(1))))
    if len(outputs) != 1 or outputs[0][0] != "y":
        raise RuntimeError("README device must expose exactly one named output y")
    clock_name = f"Clock {outputs[0][1]}"
    clock_low = min(traces[clock_name])
    threshold = 0.5
    blocks = []
    for block, vector in enumerate(INPUT_COMBINATIONS):
        expected = source["truth_table"][vector[0] * 2 + vector[1]][0]
        indices = [index for index in range(sample_count)
                   if block * HOLD_CYCLES + HOLD_CYCLES - CHECK_CYCLES
                   <= index * stride * TOTAL_CYCLES / steps < (block + 1) * HOLD_CYCLES
                   and traces[clock_name][index] <= clock_low * (1 + 1e-6)]
        values = [traces["y"][index] for index in indices]
        unknown = sum(abs(value) < threshold for value in values)
        mismatches = sum((value >= threshold if expected == 0 else value <= -threshold) for value in values)
        stimulus_errors = sum(any(traces[name][index] != 2 * bit - 1
                                  for name, bit in zip(source["input_ports"], vector)) for index in indices)
        blocks.append({"block": block, "input": list(vector), "expected_y": expected,
                       "samples_checked": len(indices), "mismatch_samples": mismatches,
                       "unknown_samples": unknown, "stimulus_mismatch_samples": stimulus_errors,
                       "min_polarization": min(values) if values else None,
                       "max_polarization": max(values) if values else None,
                       "passed": bool(values) and not (mismatches or unknown or stimulus_errors)})
    report = {"schema": "ifcn.readme.settled_function.v1", "model": model,
              "source_sha256": hashlib.sha256((output / "xor2_normalized.v").read_bytes()).hexdigest(),
              "device_sha256": hashlib.sha256((output / "xor2_device.qca").read_bytes()).hexdigest(),
              "passed": all(block["passed"] for block in blocks), "total_clock_cycles": TOTAL_CYCLES,
              "input_hold_cycles": HOLD_CYCLES, "checked_tail_cycles": CHECK_CYCLES,
              "integration_samples": steps, "recorded_samples": sample_count, "record_stride": stride,
              "output_clock": clock_name, "logic_threshold": threshold,
              "sampling": "Last three cycles of each eight-cycle input block, output clock at clamped low",
              "samples_checked": sum(block["samples_checked"] for block in blocks), "blocks": blocks,
              "scope": "Settled XOR truth table for slow inputs in this model; no single-cycle throughput, process variation or all-circuit claim"}
    write_json(output / f"{model}_function.json", report)
    if not report["passed"]:
        raise RuntimeError(f"{model}: physical output failed the source truth table; see {model}_function.json")
    return report


def summarize(output):
    pnr = read_json(output / "xor2_pnr.json")
    logic = read_json(output / "xor2_logic.json")
    if not pnr["layout_legal"] or not pnr["clock_legal"] or logic["status"] != "equivalent":
        raise RuntimeError("Layout legality, clock legality, or source-to-DAG equivalence failed")
    comparisons = {}
    functions = {}
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
        functions[model] = validate_function(output, model)
    energy_profile = read_json(output / "energy_profile.json")
    energy = energy_summary(output / "xor2_energy.txt", energy_profile["clock_period_s"])
    summary = {
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "source": "tests/benchmarks_f/TOY/xor2_demo.v",
        "expression": "y = a ^ b",
        "algorithm": pnr["algorithm"],
        "layout": {key: pnr[key] for key in ("width", "height", "area_tiles", "phase_count", "layout_legal", "clock_legal")},
        "logic": {key: logic[key] for key in ("status", "vectors_checked", "expected_vector_count")},
        "device_cells": (output / "xor2_device.qca").read_text().count("[TYPE:QCADCell]"),
        "mapping_metric_cells_before_export_normalization": pnr["mapping_validation"]["cell_count"],
        "simulation": comparisons,
        "function": functions,
        "energy": energy,
        "energy_profile": energy_profile,
        "limitations": [
            "Both physical models are checked only after settling under slow inputs; single-cycle throughput and other circuits are unverified.",
            "Energy is a model diagnostic; the initial counted cycle has a large energy-balance residual.",
            "Time-step convergence has not been tested; these values are not a converged power estimate.",
        ],
        "sha256": {name: hashlib.sha256((output / name).read_bytes()).hexdigest()
                   for name in [*ALIASES.values(), "xor2_inputs.vt", "xor2_energy_inputs.vt", "energy_profile.json",
                                "bistable.json", "coherence.json", "bistable_function.json", "coherence_function.json", "xor2_energy.txt"]},
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
    profile = summary["energy_profile"]
    fig.text(0.065, 0.875, f"{summary['device_cells']} QCA cells   |   {profile['duration_s'] * 1e12:g} ps duration"
             f"   |   {profile['time_step_s'] * 1e15:g} fs time step"
             f"   |   {profile['clock_period_s'] * 1e12:g} / {profile['input_period_s'] * 1e12:g} ps clock / input", color="#475569", fontsize=12)
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
    fig.text(0.065, 0.044, "Separate short energy run on the verified XOR device. Model values require numerical convergence before physical signoff.",
             fontsize=9.5, color="#64748b")
    for extension in ("png", "svg"):
        fig.savefig(output / f"xor2_energy.{extension}", dpi=180, facecolor="white")
    plt.close(fig)


def screenshots(output, target, env):
    target.mkdir(parents=True, exist_ok=True)
    gui = Path(configuration()["build_dir"]) / "fcnx_gui"
    views = [("01-source", "source", "xor2.v"),
             ("03-algorithms", "algorithms", "xor2.v"),
             ("03-routing", "schematic", "xor2_layout.ifcn"),
             ("04-device", "layout", "xor2_device.qca"),
             ("05-structure", "structure", "xor2_device.qca"),
             ("06-waveform", "waveform", "xor2_bistable_baseline.rst"),
             ("06-coherence", "waveform", "xor2_coherence_baseline.rst"),
             ("08-export", "export-menu", "xor2_device.qca")]
    images = []
    for name, view, filename in views:
        capture = env.copy()
        capture.update(QT_QPA_PLATFORM="offscreen", IFCN_UI_SCREENSHOT=str(target / f"{name}.png"),
                       IFCN_UI_SCREENSHOT_INPUT=str(output / filename), IFCN_UI_SCREENSHOT_VIEW=view,
                       IFCN_UI_SOURCE_FILE=str(output / "xor2.v"))
        if view in {"algorithms", "export-menu"}:
            capture["QT_SCALE_FACTOR"] = "2"
        run([gui], output=output / f"screenshot-{name}.log", env=capture, timeout=60)
        images.append({"file": f"{name}.png", "kind": "native_qt", "view": view, "input": filename})
    shutil.copyfile(output / "xor2_dag.png", target / "02-logic.png")
    shutil.copyfile(output / "xor2_energy.png", target / "07-energy.png")
    images.extend([{"file": "02-logic.png", "kind": "graphviz", "input": "xor2_dag.json"},
                   {"file": "07-energy.png", "kind": "matplotlib", "input": "xor2_energy.txt"}])
    for item in images:
        item["sha256"] = hashlib.sha256((target / item["file"]).read_bytes()).hexdigest()
    write_json(output / "screenshots.json", {"directory": str(target), "image_count": len(images),
                                            "images": sorted(images, key=lambda item: item["file"])})


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", "--output-dir", type=Path, default=ROOT / "output/readme-verified-demo")
    parser.add_argument("--build-dir", type=Path, help="C++ binaries; defaults to IFCN_BUILD_DIR or build/")
    parser.add_argument("--python", help="Python interpreter containing the layout backend dependencies")
    parser.add_argument("--bindings-dir", type=Path, help="Directory containing the compiled layout Python extension")
    parser.add_argument("--reuse-data", action="store_true", help="Validate/render existing artifacts without rerunning algorithms")
    parser.add_argument("--map-only", action="store_true",
                        help="Run parse/layout/mapping and write map_summary.json without physical simulation or screenshots")
    parser.add_argument("--screenshots", nargs="?", type=Path, const=Path("__DEFAULT__"),
                        help="Also save native Qt screenshots; defaults to OUTPUT/screenshots")
    args = parser.parse_args()
    if args.map_only and (args.reuse_data or args.screenshots):
        parser.error("--map-only cannot be combined with --reuse-data or --screenshots")
    if args.build_dir:
        os.environ["IFCN_BUILD_DIR"] = str(args.build_dir.resolve())
    if args.python:
        # Preserve a virtualenv's executable path: resolving its symlink would
        # select system Python and silently lose the configured dependencies.
        selected_python = os.path.abspath(shutil.which(args.python) or os.path.expanduser(args.python))
        os.environ["IFCN_PYTHON"] = selected_python
        if os.path.abspath(sys.executable) != selected_python:
            os.execv(selected_python, [selected_python, str(Path(__file__).resolve()), *sys.argv[1:]])
    if args.bindings_dir:
        os.environ["IFCN_LAYOUT_BINDINGS_DIR"] = str(args.bindings_dir.resolve())
    env = os.environ.copy()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if args.map_only:
        compute_mapping(output, env)
        print(f"Done: {output / 'map_summary.json'}", flush=True)
        return
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
