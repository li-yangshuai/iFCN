#!/usr/bin/env python3
"""Render an iFCN-generated QCADesigner file as a compact TikZ cell layout."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


def parse_cells(path: Path) -> list[dict[str, object]]:
    cells: list[dict[str, object]] = []
    current: dict[str, object] | None = None
    depth = 0
    for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = raw.strip()
        if line == "[TYPE:QCADCell]":
            if current is None:
                current = {}
                depth = 1
            else:
                depth += 1
            continue
        if current is None:
            continue
        if line == "[#TYPE:QCADCell]":
            depth -= 1
            if depth == 0:
                if {"x", "y", "clock"} <= current.keys():
                    cells.append(current)
                current = None
            continue
        match = re.fullmatch(r"x=([-+0-9.]+)", line)
        if match and "x" not in current:
            current["x"] = float(match.group(1))
            continue
        match = re.fullmatch(r"y=([-+0-9.]+)", line)
        if match and "y" not in current:
            current["y"] = float(match.group(1))
            continue
        match = re.fullmatch(r"cell_options\.clock=([0-3])", line)
        if match:
            current["clock"] = int(match.group(1))
            continue
        match = re.fullmatch(r"cell_function=(\S+)", line)
        if match:
            current["function"] = match.group(1)
            continue
        match = re.fullmatch(r"psz=(.+)", line)
        if match and "label" not in current:
            current["label"] = match.group(1).strip()
    return cells


def tex_escape(value: str) -> str:
    return value.replace("_", r"\_").replace("%", r"\%")


def render(cells: list[dict[str, object]], output: Path) -> None:
    if not cells:
        raise RuntimeError("No QCAD cells found")
    xs = [float(cell["x"]) for cell in cells]
    ys = [float(cell["y"]) for cell in cells]
    x0, y0 = min(xs), min(ys)
    grid = 20.0
    normalized = []
    for cell in cells:
        gx = int(round((float(cell["x"]) - x0) / grid))
        gy = int(round((float(cell["y"]) - y0) / grid))
        normalized.append((gx, gy, cell))
    max_x = max(item[0] for item in normalized)
    max_y = max(item[1] for item in normalized)
    tiles_x = max_x // 5 + 1
    tiles_y = max_y // 5 + 1

    lines = [
        r"\documentclass[tikz,border=1pt]{standalone}",
        r"\usepackage{xcolor}",
        r"\definecolor{phasezero}{HTML}{50BBD5}",
        r"\definecolor{phaseone}{HTML}{77C879}",
        r"\definecolor{phasetwo}{HTML}{D78BCB}",
        r"\definecolor{phasethree}{HTML}{E7E9EC}",
        r"\definecolor{tilezero}{HTML}{F3F8FB}",
        r"\definecolor{tileone}{HTML}{E8EDF2}",
        r"\definecolor{tiletwo}{HTML}{D4DAE1}",
        r"\definecolor{tilethree}{HTML}{AEB7C2}",
        r"\begin{document}",
        r"\begin{tikzpicture}[x=.47mm,y=-.47mm,line cap=round,line join=round]",
    ]
    tile_colors = ["tilezero", "tileone", "tiletwo", "tilethree"]
    for ty in range(tiles_y):
        for tx in range(tiles_x):
            phase = (tx + ty) % 4
            lines.append(
                rf"\fill[{tile_colors[phase]}] ({tx * 5 - .5},{ty * 5 - .5}) rectangle ({tx * 5 + 4.5},{ty * 5 + 4.5});"
            )
    lines.append(
        rf"\draw[black!14,line width=.15pt] (-.5,-.5) grid[xstep=5,ystep=5] ({tiles_x * 5 - .5},{tiles_y * 5 - .5});"
    )

    phase_colors = ["phasezero", "phaseone", "phasetwo", "phasethree"]
    for gx, gy, cell in normalized:
        function = str(cell.get("function", ""))
        clock = int(cell["clock"])
        fill = phase_colors[clock]
        outline = "black!75"
        dot = "black!75"
        if function.endswith("FIXED"):
            fill, outline, dot = "black", "black", "white"
        elif function.endswith("INPUT"):
            outline = "blue!80!black"
        elif function.endswith("OUTPUT"):
            outline = "red!80!black"
        lines.append(rf"\filldraw[fill={fill},draw={outline},line width=.22pt] ({gx-.39},{gy-.39}) rectangle ({gx+.39},{gy+.39});")
        for dx, dy in ((-.19, -.19), (.19, -.19), (-.19, .19), (.19, .19)):
            lines.append(rf"\fill[{dot}] ({gx+dx:.2f},{gy+dy:.2f}) circle[radius=.055];")
        label = str(cell.get("label", "")).strip()
        if label and label not in {"-1.00", "1.00"} and (function.endswith("INPUT") or function.endswith("OUTPUT")):
            lines.append(rf"\node[font=\sffamily\tiny,anchor=west,text=black!75] at ({gx+.55},{gy}) {{{tex_escape(label)}}};")

    lines.extend(
        [
            r"\end{tikzpicture}",
            r"\end{document}",
        ]
    )
    output.write_text("\n".join(lines) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    render(parse_cells(args.input), args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
