#!/usr/bin/env python3
"""Create honest partial-routing views by replaying a prefix of recorded routes."""

from __future__ import annotations

import argparse
from pathlib import Path


def write_prefix(source: Path, output: Path, keep: int) -> None:
    route_index = 0
    rendered: list[str] = []
    for line in source.read_text(encoding="utf-8").splitlines():
        if line.lstrip().startswith(r"\draw[route]"):
            route_index += 1
            if route_index > keep:
                continue
        rendered.append(line)
    output.write_text("\n".join(rendered) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("output_dir", type=Path)
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    write_prefix(args.source, args.output_dir / "placement_exact.tex", 0)
    write_prefix(args.source, args.output_dir / "route_prefix_04.tex", 4)
    write_prefix(args.source, args.output_dir / "route_prefix_10.tex", 10)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
