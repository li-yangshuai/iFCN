"""Read occupied coarse clock tiles for IFCN report regressions.

Decode the stored phase map independently of the production encoder. Packed
4-by-4 blocks contain padding and possibly unused board tiles; these are not
physical clock resources in the cyclic router's report.
"""

from __future__ import annotations

import re


def _section(text: str, marker: str) -> str:
    parts = text.split(marker + "\n")
    assert len(parts) == 3, f"expected one delimited {marker} section"
    return parts[1]


def occupied_tile_phases(ifcn: str) -> dict[tuple[int, int], int]:
    nodes = _section(ifcn, "#nodes info")
    paths = _section(ifcn, "#paths info")
    occupied = {
        (int(x), int(y))
        for x, y in re.findall(r"(?m)^\d+,[^\n]+,\s*\((-?\d+),(-?\d+)\);$", nodes)
    }
    for path in re.findall(r"(?m)^\(\d+,\d+\):\s*(.*);$", paths):
        occupied.update((int(x), int(y))
                        for x, y in re.findall(r"\((-?\d+),(-?\d+)\)", path))
    assert occupied, "no occupied clock tiles"

    section = _section(ifcn, "#phase map")
    data = " ".join(line.strip() for line in section.splitlines()
                    if line.strip() and not line.lstrip().startswith("#"))
    phases: dict[tuple[int, int], int] = {}
    if "#phase codec:" in section:
        metadata = re.findall(r"(?m)^#phase codec: (.*)$", section)
        assert metadata == ["phase_count=4, block_size=4, "
                            "encoding=packed_hex_2bit_row_major"]
        pattern = re.compile(r"tile\((\d+),(\d+)\):0x([0-9a-fA-F]{8});")
        assert not pattern.sub("", data).strip(), "invalid packed phase data"
        blocks = set()
        for match in pattern.finditer(data):
            tile_x, tile_y = int(match[1]), int(match[2])
            assert (tile_x, tile_y) not in blocks, "duplicate packed phase block"
            blocks.add((tile_x, tile_y))
            code = int(match[3], 16)
            for local_y in range(4):
                for local_x in range(4):
                    point = (4 * tile_x + local_x, 4 * tile_y + local_y)
                    phases[point] = (code >> (8 * (3 - local_y) + 2 * local_x)) & 3
    else:
        pattern = re.compile(r"\((-?\d+),(-?\d+)\):\s*([0-3]);")
        assert not pattern.sub("", data).strip(), "invalid expanded phase data"
        for match in pattern.finditer(data):
            point = (int(match[1]), int(match[2]))
            assert point not in phases, "duplicate expanded phase entry"
            phases[point] = int(match[3])
    assert occupied <= phases.keys(), "stored phase map is missing occupied tiles"
    return {point: phases[point] for point in occupied}
