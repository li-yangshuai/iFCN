#!/usr/bin/env python3
"""Run a reproducible fixed-throughput energy comparison for the TOY layouts.

The energy simulator's exhaustive source uses a different workload for every
number of primary inputs.  This utility instead replays the same deterministic
binary workload at one vector per 10 ps for both implementations.  The first
8 reported clock cycles are excluded as warm-up; the following 4 cycles are
the steady-state observation window.  Reported energy is bath dissipation, not
the signed bath-plus-clock exchange, whose clock component can be negative.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import hashlib
import json
import re
import subprocess
from pathlib import Path


WARMUP_CYCLES = 8
MEASURE_CYCLES = 4
TOTAL_VECTORS = WARMUP_CYCLES + MEASURE_CYCLES + 1
TIME_STEP = "2e-14"
CLOCK_PERIOD = "1e-11"
DURATION = "1.3e-10"
EV_TO_UW_AT_10PS = 1.602176634e-2

MNT_CP = {
    "00_xor2": 8, "01_mux21": 6, "02_xnor2": 8, "03_par_gen": 12,
    "04_c17": 10, "05_1bitAdderAOIG": 16, "06_t": 10, "07_mux41": 13,
    "08_par_check": 18, "09_b1_r2": 17, "10_newtag": 15, "11_clpl": 17,
    "12_RCA2": 31, "13_xor5R": 31, "14_parity": 83,
}
PROPOSED_CP = {
    "00_xor2": 6, "01_mux21": 6, "02_xnor2": 10, "03_par_gen": 12,
    "04_c17": 9, "05_1bitAdderAOIG": 13, "06_t": 9, "07_mux41": 12,
    "08_par_check": 17, "09_b1_r2": 12, "10_newtag": 12, "11_clpl": 17,
    "12_RCA2": 47, "13_xor5R": 21, "14_parity": 51,
}


def qca_io_names(qca_path: Path, function: str) -> list[str]:
    text = qca_path.read_text(encoding="utf-8")
    names: list[str] = []
    for block in text.split("[TYPE:QCADCell]")[1:]:
        block = block.split("[#TYPE:QCADCell]", 1)[0]
        if f"cell_function=QCAD_CELL_{function}" not in block:
            continue
        match = re.findall(r"^psz=(.*)$", block, flags=re.MULTILINE)
        if len(match) != 1 or not match[0]:
            raise ValueError(f"missing {function} label in {qca_path}")
        names.append(match[0])
    if not names:
        raise ValueError(f"no {function} cells in {qca_path}")
    return names


def bit(benchmark: str, cycle: int, signal: str) -> int:
    if cycle < WARMUP_CYCLES + 1:
        return 0
    digest = hashlib.sha256(f"iFCN-fixed-workload-v1:{benchmark}:{cycle}:{signal}".encode()).digest()
    return digest[0] & 1


def write_vectors(path: Path, benchmark: str, names: list[str]) -> None:
    lines = [",".join(names)]
    for cycle in range(TOTAL_VECTORS):
        lines.append(",".join(str(bit(benchmark, cycle, name)) for name in names))
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def parse_energy(path: Path) -> dict[str, float | int]:
    rows: list[tuple[int, float, float]] = []
    in_rows = False
    for line in path.read_text(encoding="utf-8").splitlines():
        if line == "[PER_CYCLE]":
            in_rows = True
            continue
        if line == "[#PER_CYCLE]":
            break
        if not in_rows or line.startswith("cycle,"):
            continue
        cells = line.split(",")
        rows.append((int(cells[0]), float(cells[1]), float(cells[5])))
    first = WARMUP_CYCLES + 2
    selected = [row for row in rows if first <= row[0] < first + MEASURE_CYCLES]
    if len(selected) != MEASURE_CYCLES:
        raise ValueError(f"{path}: expected {MEASURE_CYCLES} steady cycles, got {len(selected)}")
    bath = sum(row[1] for row in selected) / len(selected)
    bath_clock = sum(row[2] for row in selected) / len(selected)
    return {
        "first_cycle": selected[0][0],
        "last_cycle": selected[-1][0],
        "bath_eV_per_vector": bath,
        "bath_clock_eV_per_vector_signed": bath_clock,
        "bath_power_uW_at_10ps": bath * EV_TO_UW_AT_10PS,
    }


def run(command: list[str]) -> str:
    completed = subprocess.run(command, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if completed.returncode:
        raise RuntimeError(f"energy run failed ({completed.returncode}):\n{completed.stdout}")
    return completed.stdout


def update_latex(result_root: Path, summary: dict[str, object]) -> None:
    benchmarks = summary["benchmarks"]
    rows: list[str] = []
    mnt_energy: list[float] = []
    proposed_energy: list[float] = []
    mnt_power: list[float] = []
    proposed_power: list[float] = []
    for benchmark in sorted(benchmarks):
        result = benchmarks[benchmark]
        mnt = result["mntdesigner_gold"]
        proposed = result["proposed_io_contract"]
        me = float(mnt["bath_eV_per_vector"])
        pe = float(proposed["bath_eV_per_vector"])
        mp = float(mnt["bath_power_uW_at_10ps"])
        pp = float(proposed["bath_power_uW_at_10ps"])
        mnt_energy.append(me)
        proposed_energy.append(pe)
        mnt_power.append(mp)
        proposed_power.append(pp)
        rows.append(
            f"{benchmark.replace('_', r'\\_')} & {MNT_CP[benchmark]} & {me:.6f} & {mp:.6f} "
            f"& {PROPOSED_CP[benchmark]} & {pe:.6f} & {pp:.6f} & {float(result['bath_power_saving_percent']):.2f} \\\\"
        )
    mean_me = sum(mnt_energy) / len(mnt_energy)
    mean_pe = sum(proposed_energy) / len(proposed_energy)
    mean_mp = sum(mnt_power) / len(mnt_power)
    mean_pp = sum(proposed_power) / len(proposed_power)
    mean_saving = 100.0 * (1.0 - mean_pp / mean_mp)
    table = r"""\begin{longtable}{lrrr rrr r}
\toprule
\multirow{2}{*}{MNTBench}
& \multicolumn{3}{c}{MNTDesigner: Ortho + GOLD}
& \multicolumn{3}{c}{Proposed: Graph Draw + IO Contract}
& \multirow{2}{*}{Bath-power saving (\%)} \\
\cmidrule(lr){2-4}\cmidrule(lr){5-7}
& CP (static) & $E_{\mathrm{bath}}$ (eV/vector) & $P_{\mathrm{bath}}$ ($\mu$W)
& CP (static) & $E_{\mathrm{bath}}$ (eV/vector) & $P_{\mathrm{bath}}$ ($\mu$W) & \\
\midrule
\endfirsthead
\toprule
\multirow{2}{*}{MNTBench}
& \multicolumn{3}{c}{MNTDesigner: Ortho + GOLD}
& \multicolumn{3}{c}{Proposed: Graph Draw + IO Contract}
& \multirow{2}{*}{Bath-power saving (\%)} \\
\cmidrule(lr){2-4}\cmidrule(lr){5-7}
& CP (static) & $E_{\mathrm{bath}}$ (eV/vector) & $P_{\mathrm{bath}}$ ($\mu$W)
& CP (static) & $E_{\mathrm{bath}}$ (eV/vector) & $P_{\mathrm{bath}}$ ($\mu$W) & \\
\midrule
\endhead
""" + "\n".join(rows) + f"""
\\midrule
Mean & {sum(MNT_CP.values()) / len(MNT_CP):.2f} & {mean_me:.6f} & {mean_mp:.6f}
& {sum(PROPOSED_CP.values()) / len(PROPOSED_CP):.2f} & {mean_pe:.6f} & {mean_pp:.6f} & {mean_saving:.2f} \\\\
\\addlinespace
\\multicolumn{{8}}{{l}}{{\\footnotesize All layouts replay the same deterministic input vectors at one vector per 10 ps; the first 8 cycles are warm-up and the next 4 cycles are averaged.}} \\\\
\\multicolumn{{8}}{{l}}{{\\footnotesize $E_{{\\mathrm{{bath}}}}$ is heat dissipated to the thermal bath per driven vector. $P_{{\\mathrm{{bath}}}}=E_{{\\mathrm{{bath}}}}q_e/(10\\,\\mathrm{{ps}})$; CP is shown only as a static latency proxy.}} \\\\
\\multicolumn{{8}}{{l}}{{\\footnotesize Signed bath--clock exchange is excluded because it represents reversible clock work, not a positive dissipated-power metric.}} \\\\
\\bottomrule
\\end{{longtable}}"""
    tex_path = result_root / "layout_results.tex"
    tex = tex_path.read_text(encoding="utf-8")
    pattern = (r"\\begin\{longtable\}\{lrrr rrr r\}\n\\toprule\n"
               r"\\multirow\{2\}\{\*\}\{MNTBench\}.*?\\end\{longtable\}")
    updated, count = re.subn(pattern, table, tex, count=1, flags=re.DOTALL)
    if count != 1:
        raise ValueError(f"could not locate Table 2 in {tex_path}")
    tex_path.write_text(updated, encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[3])
    parser.add_argument("--jobs", type=int, default=4)
    args = parser.parse_args()
    repo = args.repo.resolve()
    result_root = repo / "tests/benchmarks_f/original_TOY_2ddwave_results"
    power_root = result_root / "power_analysis_results"
    output_root = power_root / "fixed_10ps_steady_state"
    mnt_in = power_root / "mntdesigner_gold"
    circuits = result_root / "circuits"
    binary = repo / "build/ifcn_energy_analysis"
    if not binary.is_file():
        raise FileNotFoundError(binary)
    output_root.mkdir(parents=True, exist_ok=True)

    tasks: list[tuple[str, list[str], list[str]]] = []
    workload: dict[str, dict[str, object]] = {}
    for circuit_dir in sorted(path for path in circuits.iterdir() if path.is_dir()):
        benchmark = circuit_dir.name
        mnt_qca = mnt_in / f"{benchmark}_energy_input.qca"
        ifcn = next(circuit_dir.glob("*_normal_graph_draw.ifcn"), None)
        if ifcn is None or not mnt_qca.is_file():
            raise FileNotFoundError(f"missing input for {benchmark}")
        mnt_names = qca_io_names(mnt_qca, "INPUT")
        proposed_qca = output_root / "proposed_io_contract" / f"{benchmark}_energy_input.qca"
        # The IFCN mapper preserves all PI names.  Use MNT's labels to make the
        # vector table first, then verify the generated proposed QCA after its run.
        vector_path = output_root / "vectors" / f"{benchmark}.vt"
        vector_path.parent.mkdir(parents=True, exist_ok=True)
        write_vectors(vector_path, benchmark, mnt_names)
        common = ["--vectors", str(vector_path), "--time-step", TIME_STEP,
                  "--duration", DURATION, "--clock-period", CLOCK_PERIOD,
                  "--input-period", CLOCK_PERIOD, "--clock-slope", "1e-12"]
        mnt_prefix = output_root / "mntdesigner_gold" / benchmark
        proposed_prefix = output_root / "proposed_io_contract" / benchmark
        mnt_prefix.parent.mkdir(parents=True, exist_ok=True)
        proposed_prefix.parent.mkdir(parents=True, exist_ok=True)
        tasks.append((benchmark, [str(binary), str(mnt_qca), str(mnt_prefix), *common],
                      [str(binary), str(ifcn), str(proposed_prefix), "--io-contraction", *common]))
        workload[benchmark] = {"input_names": mnt_names, "vector_table": str(vector_path.relative_to(result_root))}

    logs: dict[str, list[str]] = {}
    def execute(task: tuple[str, list[str], list[str]]) -> tuple[str, list[str]]:
        benchmark, mnt_cmd, proposed_cmd = task
        return benchmark, [run(mnt_cmd), run(proposed_cmd)]

    with concurrent.futures.ThreadPoolExecutor(max_workers=max(1, args.jobs)) as executor:
        futures = [executor.submit(execute, task) for task in tasks]
        for future in concurrent.futures.as_completed(futures):
            benchmark, output = future.result()
            logs[benchmark] = output
            print(f"completed {benchmark}", flush=True)

    summary: dict[str, object] = {
        "method": {
            "input_period_s": 1e-11,
            "clock_period_s": 1e-11,
            "time_step_s": 2e-14,
            "workload": "SHA-256 deterministic binary vectors; one vector per 10 ps",
            "warmup_cycles_excluded": WARMUP_CYCLES,
            "steady_cycles": MEASURE_CYCLES,
            "energy_metric": "mean bath dissipation per driven vector",
            "power_metric": "mean bath dissipation divided by common 10 ps vector period",
        },
        "benchmarks": {},
    }
    for benchmark in sorted(workload):
        mnt_report = output_root / "mntdesigner_gold" / f"{benchmark}_energy.txt"
        proposed_report = output_root / "proposed_io_contract" / f"{benchmark}_energy.txt"
        proposed_names = qca_io_names(output_root / "proposed_io_contract" / f"{benchmark}_energy_input.qca", "INPUT")
        if set(proposed_names) != set(workload[benchmark]["input_names"]):
            raise ValueError(f"input-name mismatch for {benchmark}: {proposed_names}")
        mnt = parse_energy(mnt_report)
        proposed = parse_energy(proposed_report)
        saving = 100.0 * (1.0 - float(proposed["bath_power_uW_at_10ps"]) /
                          float(mnt["bath_power_uW_at_10ps"]))
        summary["benchmarks"][benchmark] = {
            **workload[benchmark],
            "mntdesigner_gold": mnt,
            "proposed_io_contract": proposed,
            "bath_power_saving_percent": saving,
        }
    (output_root / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    (output_root / "run.log").write_text("\n".join(
        f"[{name}]\n{output[0]}\n{output[1]}" for name, output in sorted(logs.items())) + "\n",
        encoding="utf-8")
    update_latex(result_root, summary)
    print(output_root / "summary.json")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
