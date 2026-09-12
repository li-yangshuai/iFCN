#!/usr/bin/env python3
"""Packed IFCN phases must reach QCA unchanged in each mapping mode."""

from __future__ import annotations

import re
import subprocess
import sys
import tempfile
from pathlib import Path


def layout(phase_count: int, block_size: int, mode: str) -> tuple[str, str]:
    headers = "" if mode == "legacy" else f"#mapping mode: {mode}\n"
    if mode == "sequential":
        headers += "#phase granularity: tile\n"
    distance = "#iteration_distance=0\n" if mode == "sequential" else ""
    body = f"""#circuit name: packed_phase_export
{headers}#nodes info
0, a, input, (0,0);
1, y, output, (5,0);
#nodes info
#paths info
{distance}(0,1): (0,0),(1,0),(2,0),(3,0),(4,0),(5,0);
#paths info
#phase map
"""
    codec = (f"#phase codec: phase_count={phase_count}, block_size={block_size}, "
             "encoding=packed_hex_2bit_row_major\n")
    packed, expanded = [], []
    for tile_x in range(2):
        rows = []
        for y in range(block_size):
            byte = 0
            for column in range(block_size):
                x = tile_x * block_size + column
                phase = (x + y) % phase_count
                byte |= phase << (2 * column)
                expanded.append(f"({x},{y}):{phase};\n")
            rows.append(f"{byte:02x}")
        packed.append(f"tile({tile_x},0):0x{''.join(rows)};\n")
    return (body + codec + "".join(packed) + "#phase map\n",
            body + "".join(expanded) + "#phase map\n")


def export(binary: Path, root: Path, name: str, text: str) -> tuple[subprocess.CompletedProcess[str], Path]:
    source = root / f"{name}.ifcn"
    source.write_text(text)
    prefix = root / name
    result = subprocess.run([str(binary), str(source), str(prefix), "--qca-only"],
                            text=True, capture_output=True, timeout=10)
    return result, root / f"{name}_energy_input.qca"


def main() -> None:
    binary = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="ifcn-packed-phase-") as directory:
        root = Path(directory)
        for mode, count, size in [("legacy", 4, 4), ("combinational", 3, 3),
                                  ("sequential", 4, 4)]:
            packed, expanded = layout(count, size, mode)
            result, packed_qca = export(binary, root, mode + "_packed", packed)
            assert result.returncode == 0, result.stderr
            result, expanded_qca = export(binary, root, mode + "_expanded", expanded)
            assert result.returncode == 0, result.stderr
            assert packed_qca.read_bytes() == expanded_qca.read_bytes(), mode
            # Independent coordinate expectations prevent two equally wrong
            # all-zero exports from satisfying the byte-equivalence check.
            cells = re.findall(r"\[TYPE:QCADCell\](.*?)\[#TYPE:QCADCell\]",
                               packed_qca.read_text(), re.S)
            assert cells
            observed = set()
            for cell in cells:
                x = float(re.search(r"^x=(.*)$", cell, re.M)[1])
                y = float(re.search(r"^y=(.*)$", cell, re.M)[1])
                phase = int(re.search(r"cell_options.clock=(\d+)", cell)[1])
                tile_x, tile_y = int((x - 200) // 100), int((y - 200) // 100)
                assert phase == (tile_x + tile_y) % count, (mode, x, y, phase)
                observed.add(phase)
            assert len(observed) == count, (mode, observed)

        packed, _ = layout(4, 4, "legacy")
        invalid = {
            "bad_hex": re.sub(r"tile\(0,0\):[^;]+;", "tile(0,0):not_hex;", packed),
            "missing_tile": re.sub(r"tile\(1,0\):[^;]+;\n", "", packed),
            "empty_map": re.sub(r"tile\([^)]+\):[^;]+;\n", "", packed),
            "bad_codec": packed.replace("block_size=4", "block_size=5"),
            "bad_encoding": packed.replace("packed_hex_2bit_row_major", "unknown"),
        }
        for name, text in invalid.items():
            result, qca = export(binary, root, name, text)
            assert result.returncode != 0, (name, result.stdout)
            assert not qca.exists(), name
            assert "phase" in result.stderr, (name, result.stderr)
    print("packed/expanded QCA phases agree for legacy, combinational and sequential mappings")


if __name__ == "__main__":
    main()
