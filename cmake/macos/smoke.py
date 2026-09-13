#!/usr/bin/env python3
"""Exercise the distributed macOS app after relocation and without Homebrew."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument("--evidence", type=Path, required=True)
    parser.add_argument("--hide-prefix", type=Path)
    args = parser.parse_args()
    evidence = args.evidence.resolve()
    evidence.mkdir(parents=True, exist_ok=True)
    relocated = evidence / "relocated app/iFCN.app"
    shutil.copytree(args.app, relocated, symlinks=True)
    binaries = relocated / "Contents/MacOS"
    sample = relocated / "Contents/Resources/examples/regular_2ddwave/TOY/xor2_demo.ifcn"
    assert sample.is_file(), sample
    assert len(list((relocated / "Contents/Resources/examples").rglob("*.ifcn"))) == 91
    environment = {
        "HOME": str(evidence), "PATH": "/usr/bin:/bin:/usr/sbin:/sbin",
        "TMPDIR": str(evidence), "QT_QPA_PLATFORM": "offscreen",
        "IFCN_ENABLE_CONSOLE_LOG": "1",
    }

    def execute(name: str, arguments: list, extra: dict | None = None) -> str:
        result = subprocess.run([str(arg) for arg in arguments], env=environment | (extra or {}),
                                cwd=evidence, text=True, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT, timeout=60)
        (evidence / (name + ".log")).write_text(result.stdout, encoding="utf-8")
        if result.returncode:
            raise RuntimeError(f"{name} exited {result.returncode}: {result.stdout}")
        return result.stdout

    hidden = None
    try:
        if args.hide_prefix:
            prefix = args.hide_prefix.resolve()
            if prefix not in (Path("/opt/homebrew"), Path("/usr/local/Homebrew")):
                raise ValueError("Only an explicit CI Homebrew prefix may be hidden")
            hidden = prefix.with_name(prefix.name + ".ifcn-smoke-hidden")
            if hidden.exists():
                raise RuntimeError(f"Refusing to overwrite {hidden}")
            subprocess.run(["/usr/bin/sudo", "/bin/mv", str(prefix), str(hidden)], check=True)
        for view in ("layout", "schematic"):
            png = evidence / (view + ".png")
            execute("gui-" + view, [binaries / "iFCN"], {
                "IFCN_UI_SCREENSHOT": str(png), "IFCN_UI_SCREENSHOT_INPUT": str(sample),
                "IFCN_UI_SCREENSHOT_VIEW": view,
            })
            assert png.stat().st_size > 1000 and png.read_bytes()[:8] == b"\x89PNG\r\n\x1a\n"
        execute("mapping", [binaries / "ifcn_mapping_metrics", sample, "--no-io-contraction"])
        dot = evidence / "probe.dot"
        dot.write_text("digraph probe { input -> gate -> output; }\n", encoding="utf-8")
        svg = evidence / "probe.svg"
        execute("graphviz", [binaries / "dot", "-Tsvg", dot, "-o", svg])
        assert "<svg" in svg.read_text(encoding="utf-8")
        source = evidence / "and2.v"
        source.write_text("module and2(a, b, y);\ninput a, b;\noutput y;\n"
                          "assign y = a & b;\nendmodule\n", encoding="utf-8")
        candidate = evidence / "and2.json"
        execute("native-pnr", [binaries / "ifcn_combinational_pnr", "irregular", source,
                               candidate, "--budget", "8", "--attempts", "32"])
        data = json.loads(candidate.read_text(encoding="utf-8"))
        assert data["routed"] is True and data["native_mapping_valid"] is True
        (evidence / "result.json").write_text(json.dumps({
            "passed": True, "relocated": True, "homebrew_hidden": hidden is not None,
            "examples": 91, "gui_views": ["layout", "schematic"],
            "mapping": True, "graphviz_svg": True, "native_irregular_pnr": True,
            "python_backend_bundled": False,
        }, indent=2) + "\n", encoding="utf-8")
    finally:
        if hidden is not None and hidden.exists():
            subprocess.run(["/usr/bin/sudo", "/bin/mv", str(hidden), str(args.hide_prefix)], check=True)
        # The release assets carry the application; evidence only needs logs
        # and screenshots, not another copy of all native libraries.
        shutil.rmtree(relocated.parent)


if __name__ == "__main__":
    main()
