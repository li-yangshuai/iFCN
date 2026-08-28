#!/usr/bin/env python3
"""Verify IFCN fixed cells are exported with Simon-readable polarization."""

from __future__ import annotations

import math
import re
import subprocess
import sys
import tempfile
from pathlib import Path


QCHARGE = 1.60217646200000007055474082341e-19


def fixed_cells(qca_path: Path) -> list[tuple[str, list[float]]]:
    text = qca_path.read_text(encoding="utf-8")
    result: list[tuple[str, list[float]]] = []
    for tail in text.split("[TYPE:QCADCell]")[1:]:
        block = tail.split("[#TYPE:QCADCell]", 1)[0]
        if "cell_function=QCAD_CELL_FIXED" not in block:
            continue
        label_match = re.search(r"(?m)^psz=(-?1\.00)$", block)
        assert label_match is not None, block
        charges = [
            float(value)
            for value in re.findall(r"(?m)^charge=([^\s]+)$", block)
        ]
        assert len(charges) == 4, (label_match.group(1), charges)
        result.append((label_match.group(1), charges))
    return result


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit(
            "usage: test_ifcn_energy_fixed_polarization.py "
            "<ifcn_energy_analysis> <layout.ifcn>"
        )

    binary = Path(sys.argv[1]).resolve()
    fixture = Path(sys.argv[2]).resolve()
    with tempfile.TemporaryDirectory(prefix="ifcn-fixed-polarization-") as tmp:
        prefix = Path(tmp) / "fixed_polarization"
        completed = subprocess.run(
            [str(binary), str(fixture), str(prefix), "--qca-only"],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        assert completed.returncode == 0, (
            completed.stdout,
            completed.stderr,
        )

        cells = fixed_cells(Path(f"{prefix}_energy_input.qca"))
        assert {label for label, _charges in cells} == {"-1.00", "1.00"}, cells
        for label, charges in cells:
            expected_polarization = -1.0 if label == "-1.00" else 1.0
            polarization = (charges[0] - charges[1]) / QCHARGE
            assert math.isclose(
                polarization,
                expected_polarization,
                rel_tol=1.0e-12,
                abs_tol=1.0e-12,
            ), (label, charges, polarization)
            assert math.isclose(
                charges[0], charges[2], rel_tol=0.0, abs_tol=1.0e-30
            )
            assert math.isclose(
                charges[1], charges[3], rel_tol=0.0, abs_tol=1.0e-30
            )
            assert math.isclose(sum(charges), 2.0 * QCHARGE, rel_tol=1.0e-12)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
