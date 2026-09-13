"""Assemble a relocatable Windows runtime from an MSYS2 UCRT64 installation.

Only application dependencies are copied: Qt plugins, Graphviz engines, the
Python standard library and the scientific packages required by classic P&R.
PE imports determine the native DLL closure; missing imports fail packaging.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.metadata
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import sysconfig


def copy_file(source: Path, target: Path) -> None:
    target.parent.mkdir(parents=True, exist_ok=True)
    if target.exists() and source.read_bytes() != target.read_bytes():
        raise RuntimeError(f"Conflicting runtime files: {source} and {target}")
    if not target.exists():
        shutil.copy2(source, target)


def ignored_runtime_file(path: Path) -> bool:
    return (any(part in {"__pycache__", "tests", "test", "idlelib", "ensurepip"}
                for part in path.parts)
            or path.suffix.lower() in {".pyc", ".pyo", ".a", ".h", ".hpp", ".c", ".cpp"})


def bundle_python(prefix: Path, stage: Path, owners: set[Path]) -> None:
    from packaging.requirements import Requirement

    runtime = stage / "runtime/python"
    executable = Path(sys.executable).resolve()
    if not executable.is_relative_to(prefix):
        raise RuntimeError("Run this script with the MSYS2 Python used to compile iFCN_Lab")
    copy_file(executable, runtime / "bin/python.exe")
    owners.add(executable)
    stdlib = Path(sysconfig.get_path("stdlib")).resolve()
    destination = runtime / stdlib.relative_to(prefix)
    for source in stdlib.rglob("*"):
        relative = source.relative_to(stdlib)
        if (not source.is_file() or "site-packages" in relative.parts
                or ignored_runtime_file(relative)):
            continue
        copy_file(source, destination / relative)

    # Use installed distribution metadata to preserve package data and DLLs,
    # recursively following non-extra dependencies instead of copying the SDK.
    pending = ["numpy", "scipy", "matplotlib", "networkx"]
    visited: set[str] = set()
    versions = {}
    while pending:
        name = pending.pop()
        key = re.sub(r"[-_.]+", "-", name).lower()
        if key in visited:
            continue
        visited.add(key)
        distribution = importlib.metadata.distribution(name)
        versions[distribution.metadata["Name"]] = distribution.version
        for requirement in distribution.requires or []:
            requirement = Requirement(requirement)
            if requirement.marker is None or requirement.marker.evaluate({"extra": ""}):
                pending.append(requirement.name)
        for record in distribution.files or []:
            source = Path(distribution.locate_file(record)).resolve()
            if not source.is_file() or not source.is_relative_to(prefix):
                continue
            relative = source.relative_to(prefix)
            if "site-packages" not in relative.parts or ignored_runtime_file(relative):
                continue
            copy_file(source, runtime / relative)
            if source.name == "METADATA":
                owners.add(source)

    # Python 3.8+ extension loading does not search PATH. Keep the DLL directory
    # handles alive, and make the same setup available to normal Python imports.
    (destination / "sitecustomize.py").write_text(
        "import os\nfrom pathlib import Path\n"
        "_ifcn_root = Path(__file__).resolve().parents[4]\n"
        "_ifcn_dll_handles = []\n"
        "if hasattr(os, 'add_dll_directory'):\n"
        "    _ifcn_dll_handles.append(os.add_dll_directory(str(_ifcn_root / 'bin')))\n",
        encoding="utf-8")
    (runtime / "packages.json").write_text(json.dumps(versions, indent=2) + "\n",
                                            encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--prefix", required=True, type=Path)
    parser.add_argument("--stage", required=True, type=Path)
    args = parser.parse_args()
    prefix, stage = args.prefix.resolve(), args.stage.resolve()
    if os.name != "nt":
        raise RuntimeError("Windows runtime packaging must run on the Windows build host")
    binary = stage / "bin"
    if not (binary / "fcnx_gui.exe").is_file():
        raise RuntimeError("Install the iFCN application before bundling its runtime")
    owners: set[Path] = set()
    bundle_python(prefix, stage, owners)

    # Qt Widgets, SVG/export and the offscreen release check need these plugins.
    qt_plugins = prefix / "share/qt5/plugins"
    if not qt_plugins.is_dir():
        qt_plugins = prefix / "lib/qt5/plugins"
    for directory in ("platforms", "imageformats", "styles", "printsupport"):
        for source in (qt_plugins / directory).glob("*.dll"):
            copy_file(source, binary / directory / source.name)
            owners.add(source)
    if not (binary / "platforms/qwindows.dll").is_file():
        raise RuntimeError(f"Qt Windows platform plugin missing in {qt_plugins}")
    (binary / "qt.conf").write_text("[Paths]\nPrefix=.\nPlugins=.\n", encoding="utf-8")

    # Graphviz loads engines at run time, so they must be explicit closure roots.
    plugins = list((prefix / "lib/graphviz").glob("*gvplugin*.dll"))
    plugins += list((prefix / "bin").glob("*gvplugin*.dll"))
    if not plugins:
        raise RuntimeError("No Graphviz runtime plugins were found")
    for source in plugins:
        copy_file(source, binary / "graphviz" / source.name)
        owners.add(source)
    copy_file(prefix / "bin/dot.exe", binary / "dot.exe")
    owners.add(prefix / "bin/dot.exe")
    # Fontconfig's Windows configuration includes the system fonts directory;
    # ship it instead of relying on the build machine's MSYS2 installation.
    fontconfig = prefix / "etc/fonts"
    for source in fontconfig.rglob("*"):
        if source.is_file():
            copy_file(source, stage / "etc/fonts" / source.relative_to(fontconfig))

    available = {}
    for directory in (prefix / "bin", prefix / "lib/graphviz"):
        for source in directory.glob("*.dll"):
            available[source.name.lower()] = source
    system = Path(os.environ["SystemRoot"]) / "System32"
    system_names = {path.name.lower() for path in system.glob("*.dll")}
    pending = [path for path in stage.rglob("*")
               if path.suffix.lower() in {".exe", ".dll", ".pyd"}]
    checked: set[Path] = set()
    objdump = prefix / "bin/objdump.exe"
    while pending:
        source = pending.pop()
        if source in checked:
            continue
        checked.add(source)
        output = subprocess.check_output([str(objdump), "-p", str(source)], text=True,
                                         encoding="utf-8", errors="replace")
        for name in re.findall(r"DLL Name:\s*(\S+)", output):
            key = name.lower()
            if key in available:
                dependency = available[key]
                destination = binary / dependency.name
                copy_file(dependency, destination)
                owners.add(dependency)
                pending.append(destination)
            elif key in system_names or key.startswith(("api-ms-win-", "ext-ms-win-")):
                continue
            elif not any(path.name.lower() == key for path in checked | set(pending)):
                raise RuntimeError(f"Unresolved DLL {name} required by {source}")

    # The interpreter must find its own Python DLL before Python/sitecustomize
    # starts. It otherwise receives bin through PATH from the GUI launcher.
    for source in binary.glob("*python*.dll"):
        copy_file(source, stage / "runtime/python/bin" / source.name)

    env = dict(os.environ)
    env["PATH"] = str(binary) + os.pathsep + str(system)
    env["GVBINDIR"] = str(binary / "graphviz")
    env["GV_PLUGIN_PATH"] = str(binary / "graphviz")
    env["FONTCONFIG_PATH"] = str(stage / "etc/fonts")
    subprocess.run([str(binary / "dot.exe"), "-c"], env=env, check=True)
    configs = list((binary / "graphviz").glob("config*"))
    if not configs:
        raise RuntimeError("Graphviz failed to generate its bundled plugin configuration")

    # Copy licenses for the actual native/Python runtime package owners.
    package_names: set[str] = set()
    pacman = shutil.which("pacman")
    if not pacman:
        raise RuntimeError("MSYS2 pacman is required to collect dependency license notices")
    for source in owners:
        native = subprocess.check_output(["cygpath", "-u", str(source)], text=True).strip()
        package_names.update(subprocess.check_output(
            [pacman, "-Qqo", native], text=True).splitlines())
    versions = subprocess.check_output([pacman, "-Q", *sorted(package_names)], text=True)
    license_dir = stage / "LICENSES/runtime"
    license_dir.mkdir(parents=True, exist_ok=True)
    (license_dir / "packages.txt").write_text(versions, encoding="utf-8")
    for package in sorted(package_names):
        files = subprocess.check_output([pacman, "-Qlq", package], text=True).splitlines()
        for value in files:
            if "/share/licenses/" not in value or value.endswith("/"):
                continue
            # pacman reports MSYS paths; cygpath resolves both UCRT and /usr.
            native = subprocess.check_output(["cygpath", "-am", value], text=True).strip()
            source = Path(native)
            if source.is_file():
                tail = value.split("/share/licenses/", 1)[1]
                copy_file(source, license_dir / tail)

    manifest = [{"path": path.relative_to(stage).as_posix(),
                 "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
                for path in sorted(stage.rglob("*")) if path.is_file()]
    (stage / "runtime-manifest.json").write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Bundled {len(manifest)} runtime files; resolved {len(checked)} PE objects")


if __name__ == "__main__":
    main()
