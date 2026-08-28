#!/usr/bin/env python3
"""Generate compact, publication-ready SVG charts for the TOY comparison."""
from __future__ import annotations

import math
from pathlib import Path
from xml.sax.saxutils import escape


NAMES = [
    "00_xor2", "01_mux21", "02_xnor2", "03_par_gen", "04_c17",
    "05_1bitAdderAOIG", "06_t", "07_mux41", "08_par_check", "09_b1_r2",
    "10_newtag", "11_clpl", "12_RCA2", "13_xor5R", "14_parity",
]
AREA_MNT = [18, 12, 18, 40, 32, 66, 35, 45, 70, 78, 55, 45, 231, 175, 675]
AREA_IFCN = [12, 8, 12, 36, 25, 35, 35, 42, 45, 48, 42, 20, 198, 98, 595]
CELLS_MNT = [95, 62, 88, 207, 160, 321, 207, 276, 321, 369, 285, 191,
             1098, 807, 2697]
CELLS_IFCN = [72, 55, 68, 190, 141, 176, 201, 244, 245, 243, 205, 95,
              1041, 348, 2294]
POWER_MNT = [0.000042, 0.000050, 0.000041, 0.000115, 0.000076, 0.000147,
             0.000089, 0.000129, 0.000157, 0.000133, 0.000097, 0.000072,
             0.000427, 0.000289, 0.001063]
POWER_IFCN = [0.000047, 0.000041, 0.000034, 0.000094, 0.000074, 0.000098,
              0.000094, 0.000135, 0.000135, 0.000124, 0.000085, 0.000071,
              0.000437, 0.000170, 0.000913]
POWER_SAVING = [-13.15, 19.52, 16.74, 18.44, 3.07, 33.10, -5.97, -4.76,
                14.06, 7.23, 12.35, 1.39, -2.48, 41.22, 14.11]
CROSS_MNT = [1, 1, 1, 4, 1, 6, 5, 7, 6, 5, 4, 0, 37, 17, 41]
CROSS_IFCN = [1, 1, 1, 4, 4, 2, 5, 10, 5, 5, 1, 6, 53, 9, 37]

WIDTH, HEIGHT = 1040, 610
LEFT, RIGHT, TOP, BOTTOM = 180, 50, 42, 48
PLOT_W = WIDTH - LEFT - RIGHT
PLOT_H = HEIGHT - TOP - BOTTOM
MNT_COLOR = "#526C8A"
IFCN_COLOR = "#008C95"
NEGATIVE_COLOR = "#B84A4A"
GRID = "#D9E0E7"
TEXT = "#202A33"


def x_log(value: float, minimum: float, maximum: float) -> float:
    ratio = (math.log10(value) - math.log10(minimum)) / (math.log10(maximum) - math.log10(minimum))
    return LEFT + ratio * PLOT_W


def svg_chart(path: Path, mnt: list[float], ifcn: list[float], *, ticks: list[float],
              tick_labels: list[str], axis_label: str) -> None:
    minimum, maximum = ticks[0], ticks[-1]
    row_h = PLOT_H / len(NAMES)
    content = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{WIDTH}" height="{HEIGHT}" viewBox="0 0 {WIDTH} {HEIGHT}">',
        '<style>text{font-family:Helvetica,Arial,sans-serif;fill:' + TEXT + ';} .axis{font-size:15px;} .label{font-size:14px;} .tick{font-size:13px;}</style>',
        f'<rect width="{WIDTH}" height="{HEIGHT}" fill="white"/>',
        f'<rect x="{WIDTH-320}" y="11" width="14" height="14" fill="{MNT_COLOR}"/><text x="{WIDTH-300}" y="23" class="label">MNTDesigner GOLD</text>',
        f'<rect x="{WIDTH-142}" y="11" width="14" height="14" fill="{IFCN_COLOR}"/><text x="{WIDTH-122}" y="23" class="label">Proposed</text>',
    ]
    for tick, label in zip(ticks, tick_labels):
        x = x_log(tick, minimum, maximum)
        content.append(f'<line x1="{x:.2f}" y1="{TOP}" x2="{x:.2f}" y2="{TOP+PLOT_H}" stroke="{GRID}" stroke-width="1"/>')
        content.append(f'<text x="{x:.2f}" y="{HEIGHT-23}" text-anchor="middle" class="tick">{escape(label)}</text>')
    for index, name in enumerate(NAMES):
        y = TOP + index * row_h
        center = y + row_h / 2
        content.append(f'<line x1="{LEFT}" y1="{y+row_h:.2f}" x2="{LEFT+PLOT_W}" y2="{y+row_h:.2f}" stroke="#EEF2F5" stroke-width="1"/>')
        content.append(f'<text x="{LEFT-10}" y="{center+5:.2f}" text-anchor="end" class="label">{escape(name)}</text>')
        first_x = x_log(mnt[index], minimum, maximum)
        second_x = x_log(ifcn[index], minimum, maximum)
        content.append(f'<rect x="{LEFT}" y="{center-10:.2f}" width="{max(0.8, first_x-LEFT):.2f}" height="8" fill="{MNT_COLOR}"/>')
        content.append(f'<rect x="{LEFT}" y="{center+2:.2f}" width="{max(0.8, second_x-LEFT):.2f}" height="8" fill="{IFCN_COLOR}"/>')
    content += [
        f'<line x1="{LEFT}" y1="{TOP+PLOT_H}" x2="{LEFT+PLOT_W}" y2="{TOP+PLOT_H}" stroke="{TEXT}" stroke-width="1.2"/>',
        f'<text x="{LEFT+PLOT_W/2:.2f}" y="{HEIGHT-5}" text-anchor="middle" class="axis">{escape(axis_label)} (log scale; lower is better)</text>',
        '</svg>',
    ]
    path.write_text("\n".join(content) + "\n", encoding="utf-8")


def line_panel(content: list[str], *, panel_x: float, panel_w: float, mnt: list[float], ifcn: list[float],
               ticks: list[float], tick_labels: list[str], ylabel: str, panel_label: str) -> None:
    top, bottom = 56, 75
    left_pad, right_pad = 62, 18
    plot_x = panel_x + left_pad
    plot_w = panel_w - left_pad - right_pad
    plot_h = HEIGHT - top - bottom
    lo, hi = ticks[0], ticks[-1]

    def x(index: int) -> float:
        return plot_x + index * plot_w / (len(NAMES) - 1)

    def y(value: float) -> float:
        ratio = (math.log10(value) - math.log10(lo)) / (math.log10(hi) - math.log10(lo))
        return top + plot_h * (1.0 - ratio)

    content.append(f'<text x="{panel_x+4}" y="24" font-size="17" font-weight="700" fill="{TEXT}">{panel_label}</text>')
    for tick, tick_label in zip(ticks, tick_labels):
        yy = y(tick)
        content.append(f'<line x1="{plot_x}" y1="{yy:.2f}" x2="{plot_x+plot_w}" y2="{yy:.2f}" stroke="{GRID}" stroke-width="1"/>')
        content.append(f'<text x="{plot_x-8}" y="{yy+4:.2f}" text-anchor="end" class="tick">{escape(tick_label)}</text>')
    for index, name in enumerate(NAMES):
        xx = x(index)
        content.append(f'<line x1="{xx:.2f}" y1="{top}" x2="{xx:.2f}" y2="{top+plot_h}" stroke="#F0F3F6" stroke-width="0.8"/>')
        content.append(f'<text x="{xx:.2f}" y="{top+plot_h+20}" text-anchor="middle" class="tick">{index:02d}</text>')
    mnt_points = " ".join(f"{x(i):.2f},{y(value):.2f}" for i, value in enumerate(mnt))
    ifcn_points = " ".join(f"{x(i):.2f},{y(value):.2f}" for i, value in enumerate(ifcn))
    content.append(f'<polyline points="{mnt_points}" fill="none" stroke="{MNT_COLOR}" stroke-width="2.7"/>')
    content.append(f'<polyline points="{ifcn_points}" fill="none" stroke="{IFCN_COLOR}" stroke-width="2.7" stroke-dasharray="7 4"/>')
    for i, value in enumerate(mnt):
        content.append(f'<circle cx="{x(i):.2f}" cy="{y(value):.2f}" r="3.8" fill="white" stroke="{MNT_COLOR}" stroke-width="2"/>')
    for i, value in enumerate(ifcn):
        content.append(f'<rect x="{x(i)-3.4:.2f}" y="{y(value)-3.4:.2f}" width="6.8" height="6.8" fill="white" stroke="{IFCN_COLOR}" stroke-width="2"/>')
    content += [
        f'<line x1="{plot_x}" y1="{top+plot_h}" x2="{plot_x+plot_w}" y2="{top+plot_h}" stroke="{TEXT}" stroke-width="1.2"/>',
        f'<line x1="{plot_x}" y1="{top}" x2="{plot_x}" y2="{top+plot_h}" stroke="{TEXT}" stroke-width="1.2"/>',
        f'<text x="{plot_x+plot_w/2:.2f}" y="{HEIGHT-16}" text-anchor="middle" class="axis">Benchmark ID</text>',
        f'<text x="{panel_x+15}" y="{top+plot_h/2:.2f}" text-anchor="middle" class="axis" transform="rotate(-90 {panel_x+15} {top+plot_h/2:.2f})">{escape(ylabel)} (log scale)</text>',
    ]


def composite_line_figure(path: Path) -> None:
    global WIDTH, HEIGHT
    original_width, original_height = WIDTH, HEIGHT
    WIDTH, HEIGHT = 1400, 500
    content = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{WIDTH}" height="{HEIGHT}" viewBox="0 0 {WIDTH} {HEIGHT}">',
        '<style>text{font-family:Helvetica,Arial,sans-serif;fill:' + TEXT + ';} .axis{font-size:15px;} .tick{font-size:12px;}</style>',
        f'<rect width="{WIDTH}" height="{HEIGHT}" fill="white"/>',
        f'<line x1="{WIDTH/2}" y1="22" x2="{WIDTH/2}" y2="{HEIGHT-30}" stroke="#C7D0D9" stroke-width="1"/>',
        f'<line x1="{WIDTH-310}" y1="23" x2="{WIDTH-285}" y2="23" stroke="{MNT_COLOR}" stroke-width="2.7"/><circle cx="{WIDTH-297.5}" cy="23" r="3.8" fill="white" stroke="{MNT_COLOR}" stroke-width="2"/><text x="{WIDTH-275}" y="28" class="tick">MNTDesigner GOLD</text>',
        f'<line x1="{WIDTH-150}" y1="23" x2="{WIDTH-125}" y2="23" stroke="{IFCN_COLOR}" stroke-width="2.7" stroke-dasharray="7 4"/><rect x="{WIDTH-140.9}" y="19.6" width="6.8" height="6.8" fill="white" stroke="{IFCN_COLOR}" stroke-width="2"/><text x="{WIDTH-115}" y="28" class="tick">Proposed</text>',
    ]
    line_panel(content, panel_x=55, panel_w=620, mnt=AREA_MNT, ifcn=AREA_IFCN,
               ticks=[5, 10, 25, 50, 100, 250, 500, 1000],
               tick_labels=["5", "10", "25", "50", "100", "250", "500", "1000"],
               ylabel="Layout area (tiles)", panel_label="(a) Optimized layout area")
    line_panel(content, panel_x=725, panel_w=620, mnt=CELLS_MNT, ifcn=CELLS_IFCN,
               ticks=[50, 100, 250, 500, 1000, 2500, 5000],
               tick_labels=["50", "100", "250", "500", "1000", "2500", "5000"],
               ylabel="Mapped QCA cells", panel_label="(b) Mapped QCA cells")
    content.append('</svg>')
    path.write_text("\n".join(content) + "\n", encoding="utf-8")
    WIDTH, HEIGHT = original_width, original_height


def table2_analysis_figure(path: Path) -> None:
    """Render a single-column absolute power comparison for Table 2."""
    width, height = 820, 460
    left, right, top, bottom = 100, 25, 48, 72
    plot_width = width - left - right
    plot_height = height - top - bottom
    lo, hi = 0.00002, 0.002

    def x(index: int) -> float:
        return left + index * plot_width / (len(NAMES) - 1)

    def y(value: float) -> float:
        ratio = (math.log10(value) - math.log10(lo)) / (math.log10(hi) - math.log10(lo))
        return top + plot_height * (1.0 - ratio)

    content = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{width}" height="{height}" viewBox="0 0 {width} {height}">',
        '<style>text{font-family:Helvetica,Arial,sans-serif;fill:' + TEXT
        + ';}.axis{font-size:20px}.tick{font-size:17px}.legend{font-size:18px}</style>',
        f'<rect width="{width}" height="{height}" fill="white"/>',
        f'<line x1="355" y1="22" x2="389" y2="22" stroke="{MNT_COLOR}" stroke-width="3.2"/>',
        f'<circle cx="372" cy="22" r="4.7" fill="white" stroke="{MNT_COLOR}" stroke-width="2.5"/>',
        '<text x="400" y="28" class="legend">MNTDesigner GOLD</text>',
        f'<line x1="590" y1="22" x2="624" y2="22" stroke="{IFCN_COLOR}" stroke-width="3.2" stroke-dasharray="9 5"/>',
        f'<rect x="602.3" y="17.3" width="9.4" height="9.4" fill="white" stroke="{IFCN_COLOR}" stroke-width="2.5"/>',
        '<text x="635" y="28" class="legend">Proposed</text>',
    ]
    power_ticks = [0.00002, 0.00005, 0.0001, 0.0002, 0.0005, 0.001, 0.002]
    power_labels = ["2e−5", "5e−5", "1e−4", "2e−4", "5e−4", "1e−3", "2e−3"]
    for tick, tick_label in zip(power_ticks, power_labels):
        yy = y(tick)
        content.append(f'<line x1="{left}" y1="{yy:.2f}" x2="{left+plot_width}" y2="{yy:.2f}" stroke="{GRID}" stroke-width="1.2"/>')
        content.append(f'<text x="{left-10}" y="{yy+6:.2f}" text-anchor="end" class="tick">{tick_label}</text>')
    for index in range(len(NAMES)):
        xx = x(index)
        content.append(f'<line x1="{xx:.2f}" y1="{top}" x2="{xx:.2f}" y2="{top+plot_height}" stroke="#F1F4F6" stroke-width="1"/>')
        content.append(f'<text x="{xx:.2f}" y="{top+plot_height+24}" text-anchor="middle" class="tick">{index:02d}</text>')
    mnt_points = " ".join(f"{x(i):.2f},{y(value):.2f}" for i, value in enumerate(POWER_MNT))
    ifcn_points = " ".join(f"{x(i):.2f},{y(value):.2f}" for i, value in enumerate(POWER_IFCN))
    content.append(f'<polyline points="{mnt_points}" fill="none" stroke="{MNT_COLOR}" stroke-width="3.2"/>')
    content.append(f'<polyline points="{ifcn_points}" fill="none" stroke="{IFCN_COLOR}" stroke-width="3.2" stroke-dasharray="9 5"/>')
    for index, value in enumerate(POWER_MNT):
        content.append(f'<circle cx="{x(index):.2f}" cy="{y(value):.2f}" r="4.7" fill="white" stroke="{MNT_COLOR}" stroke-width="2.5"/>')
    for index, value in enumerate(POWER_IFCN):
        content.append(f'<rect x="{x(index)-4.7:.2f}" y="{y(value)-4.7:.2f}" width="9.4" height="9.4" fill="white" stroke="{IFCN_COLOR}" stroke-width="2.5"/>')
    content += [
        f'<line x1="{left}" y1="{top}" x2="{left}" y2="{top+plot_height}" stroke="{TEXT}" stroke-width="1.6"/>',
        f'<line x1="{left}" y1="{top+plot_height}" x2="{left+plot_width}" y2="{top+plot_height}" stroke="{TEXT}" stroke-width="1.6"/>',
        f'<text x="{left+plot_width/2:.2f}" y="{height-14}" text-anchor="middle" class="axis">Benchmark ID</text>',
        f'<text x="24" y="{top+plot_height/2:.2f}" text-anchor="middle" class="axis" transform="rotate(-90 24 {top+plot_height/2:.2f})">Thermal-bath power (µW; log scale)</text>',
        '</svg>',
    ]
    path.write_text("\n".join(content) + "\n", encoding="utf-8")


def main() -> None:
    root = Path(__file__).resolve().parents[3] / "tests/benchmarks_f/original_TOY_2ddwave_results"
    svg_chart(root / "layout_area_comparison.svg", AREA_MNT, AREA_IFCN,
              ticks=[5, 10, 25, 50, 100, 250, 500, 1000],
              tick_labels=["5", "10", "25", "50", "100", "250", "500", "1000"],
              axis_label="Layout area (tiles)")
    svg_chart(root / "layout_power_comparison.svg", POWER_MNT, POWER_IFCN,
              ticks=[0.00002, 0.00005, 0.0001, 0.0002, 0.0005, 0.001, 0.002],
              tick_labels=["2e−5", "5e−5", "1e−4", "2e−4", "5e−4", "1e−3", "2e−3"],
              axis_label="Thermal-bath power (µW)")
    composite_line_figure(root / "layout_comparison_figure.svg")
    table2_analysis_figure(root / "table2_analysis_figure.svg")


if __name__ == "__main__":
    main()
