#!/usr/bin/env python3
"""Replace Table 2 with a physically consistent fixed-window comparison.

The existing reports all use an 80 ps window and a 10 ps clock/input period.
This script excludes the initial transient cycle, averages thermal-bath heat
over cycles 3--8, and derives power using the common 10 ps cycle.  It never
uses static graph CP as the power denominator.
"""
from __future__ import annotations

import re
from pathlib import Path

QE = 1.602176634e-2  # eV per 10 ps -> microW
MNT_CP = [8, 6, 8, 12, 10, 16, 10, 13, 18, 17, 15, 17, 31, 31, 83]
IFCN_CP = [6, 6, 10, 12, 9, 13, 9, 12, 17, 12, 12, 17, 47, 21, 51]
CIRCUIT_INFO = [
    (2, 1, 6), (3, 1, 7), (2, 1, 8), (3, 1, 13), (5, 2, 12),
    (3, 2, 12), (5, 2, 15), (6, 1, 18), (4, 1, 18), (3, 4, 14),
    (8, 1, 23), (11, 5, 21), (5, 3, 27), (5, 1, 21), (16, 1, 119),
]
MNT_CROSS = [1, 1, 1, 4, 1, 6, 5, 7, 6, 5, 4, 0, 37, 17, 41]
IFCN_CROSS = [1, 1, 1, 4, 4, 2, 5, 10, 5, 5, 1, 6, 53, 9, 37]


def steady_bath_energy(report: Path) -> float:
    rows: list[tuple[int, float]] = []
    active = False
    for line in report.read_text(encoding="utf-8").splitlines():
        if line == "[PER_CYCLE]":
            active = True
            continue
        if line == "[#PER_CYCLE]":
            break
        if not active or line.startswith("cycle,"):
            continue
        fields = line.split(",")
        rows.append((int(fields[0]), float(fields[1])))
    selected = [energy for cycle, energy in rows if 3 <= cycle <= 8]
    if len(selected) != 6:
        raise ValueError(f"{report}: expected cycles 3--8")
    return sum(selected) / len(selected)


def main() -> None:
    repo = Path(__file__).resolve().parents[3]
    root = repo / "tests/benchmarks_f/original_TOY_2ddwave_results"
    power = root / "power_analysis_results"
    mnt_dir = power / "mntdesigner_gold"
    ifcn_dir = power / "proposed_io_contract"
    names = [f"{idx:02d}_{name}" for idx, name in enumerate([
        "xor2", "mux21", "xnor2", "par_gen", "c17", "1bitAdderAOIG", "t", "mux41",
        "par_check", "b1_r2", "newtag", "clpl", "RCA2", "xor5R", "parity",
    ])]
    rows: list[str] = []
    mnt_e: list[float] = []
    ifcn_e: list[float] = []
    for name, mcp, icp, mcross, icross, (pis, pos, nodes) in zip(
        names, MNT_CP, IFCN_CP, MNT_CROSS, IFCN_CROSS, CIRCUIT_INFO
    ):
        me = steady_bath_energy(mnt_dir / f"{name}_energy.txt")
        ie = steady_bath_energy(ifcn_dir / f"{name}_energy.txt")
        mp, ip = me * QE, ie * QE
        saving = 100.0 * (1.0 - ip / mp)
        mnt_e.append(me)
        ifcn_e.append(ie)
        rows.append(
            f"{name.replace('_', r'\_')} & {pis}/{pos} & {nodes} & {mcp} & {mcross} "
            f"& {me:.6f} & {mp:.6f} & {icp} & {icross} & {ie:.6f} & {ip:.6f} "
            f"& {saving:.2f} \\\\")
    mean_m = sum(mnt_e) / len(mnt_e)
    mean_i = sum(ifcn_e) / len(ifcn_e)
    mean_mp, mean_ip = mean_m * QE, mean_i * QE
    mean_saving = 100.0 * (1.0 - mean_ip / mean_mp)
    table = r"""\begin{longtable}{lrr rrrr rrrr r}
\caption{Fixed-window thermal-bath dissipation comparison. CP is retained only as a static latency proxy.}\label{tab:power-comparison}\\
\toprule
\multirow{2}{*}{MNTBench}
& \multicolumn{2}{c}{Circuit information}
& \multicolumn{4}{c}{MNTDesigner: Ortho + GOLD}
& \multicolumn{4}{c}{Proposed: Graph Draw + IO Contract}
& \multirow{2}{*}{\shortstack{Bath-power\\saving (\%)}} \\
\cmidrule(lr){2-3}\cmidrule(lr){4-7}\cmidrule(lr){8-11}
& I/O & Nodes
& CP & Cross. & $E_{\mathrm{bath}}$ (eV/vector) & $P_{\mathrm{bath}}$ ($\mu$W)
& CP & Cross. & $E_{\mathrm{bath}}$ (eV/vector) & $P_{\mathrm{bath}}$ ($\mu$W) & \\
\midrule
\endfirsthead
\toprule
\multirow{2}{*}{MNTBench}
& \multicolumn{2}{c}{Circuit information}
& \multicolumn{4}{c}{MNTDesigner: Ortho + GOLD}
& \multicolumn{4}{c}{Proposed: Graph Draw + IO Contract}
& \multirow{2}{*}{\shortstack{Bath-power\\saving (\%)}} \\
\cmidrule(lr){2-3}\cmidrule(lr){4-7}\cmidrule(lr){8-11}
& I/O & Nodes
& CP & Cross. & $E_{\mathrm{bath}}$ (eV/vector) & $P_{\mathrm{bath}}$ ($\mu$W)
& CP & Cross. & $E_{\mathrm{bath}}$ (eV/vector) & $P_{\mathrm{bath}}$ ($\mu$W) & \\
\midrule
\endhead
""" + "\n".join(rows) + f"""
\\midrule
\\textbf{{Total / Mean}}
& \\textbf{{{sum(info[0] for info in CIRCUIT_INFO)}/{sum(info[1] for info in CIRCUIT_INFO)}}}
& \\textbf{{{sum(info[2] for info in CIRCUIT_INFO)}}}
& {sum(MNT_CP) / len(MNT_CP):.2f} & \\textbf{{{sum(MNT_CROSS)}}}
& \\textbf{{{mean_m:.6f}}} & \\textbf{{{mean_mp:.6f}}}
& {sum(IFCN_CP) / len(IFCN_CP):.2f} & \\textbf{{{sum(IFCN_CROSS)}}}
& \\textbf{{{mean_i:.6f}}} & \\textbf{{{mean_ip:.6f}}} & \\textbf{{{mean_saving:.2f}}} \\\\
\\bottomrule
\\end{{longtable}}"""
    tex_path = root / "layout_results.tex"
    tex = tex_path.read_text(encoding="utf-8")
    expression = (r"\\begin\{longtable\}\{[^}]+\}\n"
                  r"\\caption\{Fixed-window thermal-bath dissipation comparison\..*?"
                  r"\\end\{longtable\}")
    result, count = re.subn(expression, lambda _match: table, tex, count=1, flags=re.DOTALL)
    if count != 1:
        raise RuntimeError("Table 2 not found")
    tex_path.write_text(result, encoding="utf-8")


if __name__ == "__main__":
    main()
