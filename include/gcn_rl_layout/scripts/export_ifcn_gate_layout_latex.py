#!/usr/bin/env python3
"""Export self-contained cell-level gate-layout TikZ files from raw IFCN."""

import argparse
import csv
import re
from pathlib import Path


NODE_RE = re.compile(
    r"^(\d+)\s*,\s*(.*?)\s*,\s*([^,]+?)\s*,\s*\((-?\d+)\s*,\s*(-?\d+)\)\s*;$"
)
PATH_RE = re.compile(r"^\((-?\d+)\s*,\s*(-?\d+)\)\s*:\s*(.*?)\s*;$")
COORD_RE = re.compile(r"\((-?\d+)\s*,\s*(-?\d+)\)")
PHASE_RE = re.compile(r"\((-?\d+)\s*,\s*(-?\d+)\)\s*:\s*(-?\d+)\s*;")
AREA_RE = re.compile(
    r"#layout area:\s*width:\s*(\d+)\s*,\s*height:\s*(\d+)\s*,\s*area:\s*(\d+)"
)
CIRCUIT_RE = re.compile(r"#circuit name:\s*(.+)$")


def parse_args():
    parser = argparse.ArgumentParser(
        description="Generate gate-level layout LaTeX directly from IFCN files."
    )
    parser.add_argument("--collection-root", required=True)
    parser.add_argument(
        "--phase-label-area-limit",
        type=int,
        default=1600,
        help="Show numeric phase labels only at or below this layout area.",
    )
    parser.add_argument(
        "--detailed-cell-area-limit",
        type=int,
        default=5000,
        help="Use per-cell phase fill only at or below this layout area.",
    )
    return parser.parse_args()


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


def parse_ifcn(path):
    circuit = path.stem
    width = height = None
    nodes = []
    paths = []
    phases = {}
    section = ""
    for raw_line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = raw_line.strip()
        circuit_match = CIRCUIT_RE.match(line)
        if circuit_match:
            circuit = circuit_match.group(1).strip()
        area_match = AREA_RE.match(line)
        if area_match:
            width, height = int(area_match.group(1)), int(area_match.group(2))

        if line == "#nodes info":
            section = "" if section == "nodes" else "nodes"
            continue
        if line == "#paths info":
            section = "" if section == "paths" else "paths"
            continue
        if line == "#phase map":
            section = "" if section == "phases" else "phases"
            continue

        if section == "nodes":
            match = NODE_RE.match(line)
            if match:
                nodes.append(
                    {
                        "index": int(match.group(1)),
                        "name": match.group(2).strip(),
                        "type": match.group(3).strip().lower(),
                        "x": int(match.group(4)),
                        "y": int(match.group(5)),
                    }
                )
        elif section == "paths":
            match = PATH_RE.match(line)
            if match:
                coords = [
                    (int(x), int(y)) for x, y in COORD_RE.findall(match.group(3))
                ]
                if len(coords) >= 2:
                    paths.append(
                        {
                            "src": int(match.group(1)),
                            "dst": int(match.group(2)),
                            "coords": coords,
                        }
                    )
        elif section == "phases":
            for x, y, phase in PHASE_RE.findall(line):
                phases[(int(x), int(y))] = int(phase)

    if width is None or height is None:
        coords = list(phases) + [(node["x"], node["y"]) for node in nodes]
        if not coords:
            raise RuntimeError("IFCN has no layout coordinates: {}".format(path))
        width = max(x for x, _ in coords) - min(x for x, _ in coords) + 1
        height = max(y for _, y in coords) - min(y for _, y in coords) + 1
    if not nodes:
        raise RuntimeError("IFCN has no node section: {}".format(path))
    if not paths:
        raise RuntimeError("IFCN has no routed path section: {}".format(path))
    return {
        "circuit": circuit,
        "width": int(width),
        "height": int(height),
        "nodes": nodes,
        "paths": paths,
        "phases": phases,
    }


def gate_style(node_type):
    node_type = str(node_type).lower()
    if node_type == "input":
        return "gateinput"
    if node_type == "output":
        return "gateoutput"
    if node_type in {"maj", "majority"}:
        return "gatemaj"
    if node_type == "and":
        return "gateand"
    if node_type == "or":
        return "gateor"
    if node_type in {"not", "inv"}:
        return "gatenot"
    if node_type in {"fanout", "fo"}:
        return "gatefanout"
    return "gate"


def point(coord):
    x, y = coord
    return "({:.1f},{:.1f})".format(float(x) + 0.5, -float(y) - 0.5)


def render(layout, phase_label_area_limit, detailed_cell_area_limit):
    width = int(layout["width"])
    height = int(layout["height"])
    area = width * height
    scale = min(0.52, max(0.055, 24.0 / max(width, height)))
    phases = layout["phases"]
    regular_phase = bool(phases) and all(
        int(phase) == (int(x) + int(y)) % 4
        for (x, y), phase in phases.items()
    )
    show_phase_labels = area <= max(0, int(phase_label_area_limit))
    detailed_cells = area <= max(0, int(detailed_cell_area_limit))
    tex = [
        r"\documentclass[tikz]{standalone}",
        r"\usetikzlibrary{arrows.meta,calc}",
        r"\definecolor{phasezero}{gray}{0.92}",
        r"\definecolor{phaseone}{gray}{0.74}",
        r"\definecolor{phasetwo}{gray}{0.53}",
        r"\definecolor{phasethree}{gray}{0.32}",
        r"\begin{document}",
        r"\begin{{tikzpicture}}[scale={:.5f},transform shape,".format(scale),
        r"phase0/.style={fill=phasezero},phase1/.style={fill=phaseone},",
        r"phase2/.style={fill=phasetwo},phase3/.style={fill=phasethree},",
        r"route/.style={draw=blue!62,->,>={Stealth[length=2.2mm]},line width=0.55pt,rounded corners=1.2pt},",
        r"gate/.style={circle,draw=black,fill=white,line width=0.7pt,minimum size=0.68cm,inner sep=0pt,font=\scriptsize},",
        r"gateinput/.style={gate,fill=green!22},gateoutput/.style={gate,fill=orange!28},",
        r"gatemaj/.style={gate,fill=blue!20},gateand/.style={gate,fill=cyan!23},",
        r"gateor/.style={gate,fill=yellow!32},gatenot/.style={gate,fill=red!20},",
        r"gatefanout/.style={gate,fill=violet!20}]",
    ]

    if detailed_cells and regular_phase:
        tex.extend(
            [
                r"\foreach \x in {0,...,%d}{" % (width - 1),
                r"  \foreach \y in {0,...,%d}{" % (height - 1),
                r"    \pgfmathtruncatemacro{\p}{mod(\x+\y,4)}",
                r"    \path[phase\p] (\x,-\y) rectangle ++(1,-1);",
            ]
        )
        if show_phase_labels:
            tex.append(
                r"    \node[font=\tiny,text={black!65}] at ($(\x,-\y)+(0.5,-0.5)$){\p};"
            )
        tex.extend([r"  }", r"}"])
    elif detailed_cells:
        for y in range(height):
            for x in range(width):
                phase = int(phases.get((x, y), (x + y) % 4)) % 4
                tex.append(
                    r"\path[phase{}] ({},{}) rectangle ++(1,-1);".format(
                        phase, x, -y
                    )
                )
                if show_phase_labels:
                    tex.append(
                        r"\node[font=\tiny,text=black!65] at ({:.1f},{:.1f}){{{}}};".format(
                            x + 0.5, -y - 0.5, phase
                        )
                    )
    else:
        tex.extend(
            [
                r"\path[fill=phasezero] (0,0) rectangle ({},-{});".format(
                    width, height
                ),
                r"\node[anchor=west,font=\scriptsize] at (0,0.8) "
                r"{Sparse cell grid; $p(x,y)=(x+y)\bmod 4$};",
            ]
        )
    grid_step = 1 if detailed_cells else 5
    tex.append(
        r"\draw[step={},black!18,line width=0.18pt] (0,0) grid ({},-{});".format(
            grid_step, width, height
        )
    )

    for route in layout["paths"]:
        route_points = " -- ".join(point(coord) for coord in route["coords"])
        tex.append(r"\draw[route] {};".format(route_points))
    for node in sorted(layout["nodes"], key=lambda item: item["index"]):
        tex.append(
            r"\node[{}] at {} {{{}}};".format(
                gate_style(node["type"]),
                point((node["x"], node["y"])),
                node["index"],
            )
        )

    tex.extend(
        [
            r"\node[anchor=west,font=\normalsize] at (0,{:.1f}) "
            r"{{{}; {}$\times${} cells; {} gates; {} routed edges}};".format(
                -height - 1.1,
                latex_escape(layout["circuit"]),
                width,
                height,
                len(layout["nodes"]),
                len(layout["paths"]),
            ),
            r"\end{tikzpicture}",
            r"\end{document}",
            "",
        ]
    )
    return "\n".join(tex), regular_phase


def main():
    args = parse_args()
    root = Path(args.collection_root).resolve()
    if not root.is_dir():
        raise FileNotFoundError(root)
    ifcn_files = sorted(root.rglob("*_normal_graph_draw.ifcn"))
    if not ifcn_files:
        raise RuntimeError("no raw normal-graph IFCN files found in {}".format(root))

    index_rows = []
    for ifcn_path in ifcn_files:
        layout = parse_ifcn(ifcn_path)
        source, regular_phase = render(
            layout,
            args.phase_label_area_limit,
            args.detailed_cell_area_limit,
        )
        tex_path = ifcn_path.with_name(
            ifcn_path.name.replace("_normal_graph_draw.ifcn", "_gate_level_layout.tex")
        )
        tex_path.write_text(source, encoding="utf-8")
        index_rows.append(
            {
                "dataset": ifcn_path.relative_to(root).parts[0],
                "circuit": ifcn_path.parent.name,
                "width": layout["width"],
                "height": layout["height"],
                "area": layout["width"] * layout["height"],
                "nodes": len(layout["nodes"]),
                "routed_edges": len(layout["paths"]),
                "regular_2ddwave_phase": regular_phase,
                "detailed_phase_cells": (
                    layout["width"] * layout["height"]
                    <= max(0, int(args.detailed_cell_area_limit))
                ),
                "relative_tex": str(tex_path.relative_to(root)),
            }
        )

    with (root / "gate_level_latex_manifest.csv").open(
        "w", encoding="utf-8", newline=""
    ) as handle:
        writer = csv.DictWriter(handle, fieldnames=tuple(index_rows[0]))
        writer.writeheader()
        writer.writerows(index_rows)
    print("COLLECTION {}".format(root))
    print("GATE_LEVEL_TEX {}".format(len(index_rows)))


if __name__ == "__main__":
    main()
