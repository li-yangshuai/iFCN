#!/usr/bin/env python3
"""Create disposable physical regression inputs in the CMake build tree."""

from pathlib import Path
import sys


def design_object(x: float, y: float) -> str:
    return f"""[TYPE:QCADDesignObject]
x={x}
y={y}
bSelected=FALSE
clr.red=0
clr.green=32768
clr.blue=65535
bounding_box.xWorld={x - 10}
bounding_box.yWorld={y - 10}
bounding_box.cxWorld=20
bounding_box.cyWorld=20
[#TYPE:QCADDesignObject]
"""


def cell(x: int, function: str, label: str = "") -> str:
    y = 240
    result = "[TYPE:QCADCell]\n" + design_object(x, y)
    result += f"""cell_options.cxCell=18
cell_options.cyCell=18
cell_options.dot_diameter=5
cell_options.clock=0
cell_options.mode=QCAD_CELL_MODE_NORMAL
cell_function=QCAD_CELL_{function}
number_of_dots=4
"""
    for dx, dy in ((4.5, -4.5), (4.5, 4.5), (-4.5, 4.5), (-4.5, -4.5)):
        result += f"""[TYPE:CELL_DOT]
x={x + dx}
y={y + dy}
diameter=5
charge=8.01088267e-20
spin=0
potential=0
[#TYPE:CELL_DOT]
"""
    if label:
        result += "[TYPE:QCADLabel]\n[TYPE:QCADStretchyObject]\n"
        result += design_object(x, y - 20)
        result += f"[#TYPE:QCADStretchyObject]\npsz={label}\n[#TYPE:QCADLabel]\n"
    return result + "[#TYPE:QCADCell]\n"


def generate(output: Path) -> None:
    output.mkdir(parents=True, exist_ok=True)
    # All three cells occupy one 5x5 clock tile. There is no removable I/O
    # stem or empty tile; toggling the GUI contraction control must exit
    # cleanly with its explicit "no change" result, without recursive signals.
    design = """[VERSION]
qcadesigner_version=2.000000
[#VERSION]
[TYPE:DESIGN]
[TYPE:QCADLayer]
type=1
status=0
pszDescription=Main Cell Layer
"""
    design += cell(200, "INPUT", "a")
    design += cell(220, "NORMAL")
    design += cell(240, "OUTPUT", "y")
    design += "[#TYPE:QCADLayer]\n[#TYPE:DESIGN]\n"
    (output / "io_no_change.qca").write_text(design, encoding="utf-8")
    (output / "io_no_change.vt").write_text("a\n0\n0\n1\n1\n0\n1\n0\n1\n", encoding="utf-8")
    # Repeated values distinguish steady holds from transitions in d = ~q.
    (output / "toggle_ff_cut.vt").write_text("q\n0\n0\n1\n1\n0\n0\n1\n1\n", encoding="utf-8")


if __name__ == "__main__":
    generate(Path(sys.argv[1]))
