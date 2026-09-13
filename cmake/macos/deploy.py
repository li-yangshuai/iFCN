#!/usr/bin/env python3
"""Deploy Qt and the complete native dylib closure into a relocatable macOS app.

Run on macOS after cmake --install. No package-manager paths may remain in any
Mach-O dependency after deployment. Python is a build tool, not a runtime.
"""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import plistlib
import re
import shutil
import subprocess


def run(*args: object, check: bool = True, env: dict | None = None) -> str:
    command = [str(arg) for arg in args]
    result = subprocess.run(command, check=False, env=env,
                            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if check and result.stderr:
        print(result.stderr, end="", flush=True)
    if check and result.returncode:
        if result.stdout:
            print(result.stdout, end="", flush=True)
        raise subprocess.CalledProcessError(result.returncode, command,
                                            output=result.stdout, stderr=result.stderr)
    return result.stdout


def is_macho(path: Path) -> bool:
    if not path.is_file():
        return False
    with path.open("rb") as stream:
        return stream.read(4) in (b"\xfe\xed\xfa\xce", b"\xce\xfa\xed\xfe",
                                  b"\xfe\xed\xfa\xcf", b"\xcf\xfa\xed\xfe",
                                  b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca")


def machos(root: Path) -> list[Path]:
    return sorted({path.resolve() for path in root.rglob("*") if is_macho(path)})


def dependencies(path: Path) -> list[str]:
    lines = run("otool", "-L", path).splitlines()[1:]
    names = [line.strip().split(" (compatibility version", 1)[0] for line in lines]
    # A dylib's LC_ID_DYLIB is printed as the first entry but is not a dependency.
    identities = run("otool", "-D", path, check=False).splitlines()[1:]
    return [name for name in names if name not in identities]


def rpaths(path: Path) -> list[str]:
    return re.findall(r"cmd LC_RPATH\s+cmdsize \d+\s+path (.*?) \(offset",
                      run("otool", "-l", path))


def is_system(name: str) -> bool:
    return name.startswith(("/System/Library/", "/usr/lib/"))


def resolve_dependency(name: str, owner: Path, app: Path) -> Path:
    def expand(value: str) -> Path:
        return Path(value.replace("@loader_path", str(owner.parent))
                         .replace("@executable_path", str(app / "Contents/MacOS")))
    if name.startswith("@rpath/"):
        suffix = name[len("@rpath/"):]
        search = rpaths(owner) + rpaths(app / "Contents/MacOS/iFCN")
        candidates = [expand(base) / suffix for base in search]
        candidates.append(app / "Contents/Frameworks" / suffix)
    else:
        candidates = [expand(name)]
    for candidate in candidates:
        if candidate.is_file():
            return candidate.resolve()
    raise RuntimeError(f"Unresolved dependency {name!r} in {owner}: {candidates}")


def copy_dependency(source: Path, app: Path, provenance: dict[str, str]) -> Path:
    framework_dir = app / "Contents/Frameworks"
    for parent in source.parents:
        if parent.suffix == ".framework":
            target_root = framework_dir / parent.name
            if not target_root.exists():
                shutil.copytree(parent, target_root, symlinks=True)
            target = (target_root / source.relative_to(parent)).resolve()
            provenance[str(target.relative_to(app))] = str(source)
            return target
    target = framework_dir / source.name
    if not target.exists():
        shutil.copy2(source, target)
    elif (str(target.relative_to(app)) in provenance
          and provenance[str(target.relative_to(app))] != str(source)):
        raise RuntimeError(f"Conflicting library basename: {source.name}")
    provenance[str(target.relative_to(app))] = str(source)
    return target.resolve()


def relocate(app: Path) -> dict[str, str]:
    (app / "Contents/Frameworks").mkdir(exist_ok=True)
    done: set[Path] = set()
    provenance: dict[str, str] = {}
    while True:
        pending = [path for path in machos(app) if path not in done]
        if not pending:
            break
        for owner in pending:
            owner.chmod(owner.stat().st_mode | 0o200)
            run("codesign", "--remove-signature", owner, check=False)
            for name in dependencies(owner):
                if is_system(name):
                    continue
                target = resolve_dependency(name, owner, app)
                if app not in target.parents:
                    target = copy_dependency(target, app, provenance)
                replacement = "@loader_path/" + os.path.relpath(target, owner.parent)
                if name != replacement:
                    run("install_name_tool", "-change", name, replacement, owner)
            # Remove development-machine search paths even when no dependency
            # currently happens to use them.
            for path in rpaths(owner):
                if path.startswith("/") and not is_system(path + "/"):
                    run("install_name_tool", "-delete_rpath", path, owner)
            if run("otool", "-D", owner, check=False).splitlines()[1:]:
                run("install_name_tool", "-id", "@rpath/" + owner.name, owner)
            done.add(owner)
    for owner in machos(app):
        for name in dependencies(owner):
            if is_system(name):
                continue
            if not name.startswith("@loader_path/"):
                raise RuntimeError(f"Nonportable dependency {name} in {owner}")
            if app not in resolve_dependency(name, owner, app).parents:
                raise RuntimeError(f"Dependency escapes application: {name} in {owner}")
    return provenance


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stage", type=Path, required=True)
    parser.add_argument("--qt-prefix", type=Path, required=True)
    parser.add_argument("--graphviz-prefix", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--version", required=True)
    args = parser.parse_args()
    stage = args.stage.resolve()
    app = stage / "iFCN.app"
    if not (app / "Contents/MacOS/iFCN").is_file():
        raise RuntimeError(f"Installed iFCN.app not found under {stage}")
    resources = app / "Contents/Resources"
    resources.mkdir(exist_ok=True)
    binaries = app / "Contents/MacOS"
    plugins = app / "Contents/PlugIns/graphviz"
    plugins.mkdir(parents=True, exist_ok=True)
    for child in list(stage.iterdir()):
        if child == app:
            continue
        if child.name == "bin":
            for executable in child.iterdir():
                shutil.move(str(executable), binaries / executable.name)
            child.rmdir()
        else:
            shutil.move(str(child), resources / child.name)
    for plugin in (args.graphviz_prefix / "lib/graphviz").glob("*.dylib"):
        shutil.copy2(plugin.resolve(), plugins / plugin.name)
    if not list(plugins.glob("*dot_layout*.dylib")):
        raise RuntimeError("Graphviz dot layout plugin was not packaged")
    shutil.copy2(args.graphviz_prefix / "bin/dot", binaries / "dot")
    extras = [f"-executable={path}" for path in sorted(binaries.iterdir())
              if path.name != "iFCN" and is_macho(path)]
    run(args.qt_prefix / "bin/macdeployqt", app, "-always-overwrite", "-verbose=2", *extras)
    provenance = relocate(app)
    (resources / "native-dependency-provenance.json").write_text(
        json.dumps(provenance, indent=2) + "\n", encoding="utf-8")
    # install_name_tool invalidates arm64 code signatures. Sign the relocated
    # tools before executing dot to generate its plugin registry.
    for path in sorted(machos(app), key=lambda value: len(value.parts), reverse=True):
        run("codesign", "--force", "--sign", "-", "--timestamp=none", path)
    # Generate the plugin registry inside the bundle, never copy Homebrew's
    # registry with paths that can point back to the build machine.
    environment = os.environ | {"GVBINDIR": str(plugins)}
    run(binaries / "dot", "-c", env=environment)
    if not list(plugins.glob("config*")):
        raise RuntimeError("Graphviz did not generate a bundled plugin registry")
    # Every headless tool shares the bundle's Graphviz registry. The GUI sets
    # GVBINDIR itself before constructing its Graphviz context.
    for executable in list(binaries.iterdir()):
        if executable.name == "iFCN" or not is_macho(executable):
            continue
        actual = executable.with_name(executable.name + ".bin")
        executable.rename(actual)
        executable.write_text(
            '#!/bin/sh\nset -eu\n'
            'ifcn_bin=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)\n'
            'export GVBINDIR="$ifcn_bin/../PlugIns/graphviz"\n'
            f'exec "$ifcn_bin/{actual.name}" "$@"\n', encoding="utf-8")
        executable.chmod(0o755)
    shutil.copy2(Path(__file__).with_name("README.txt"), stage / "README.txt")
    shutil.copy2(stage / "README.txt", resources / "README-macOS.txt")
    with (app / "Contents/Info.plist").open("rb") as stream:
        metadata = plistlib.load(stream)
    if metadata["CFBundleShortVersionString"] != args.version:
        raise RuntimeError("Bundle version does not match the requested release")
    # Required for modified arm64 Mach-O files. This is deliberately ad-hoc,
    # without Developer ID, hardened runtime, entitlements, or notarization.
    for path in sorted(machos(app), key=lambda value: len(value.parts), reverse=True):
        run("codesign", "--force", "--sign", "-", "--timestamp=none", path)
    run("codesign", "--force", "--deep", "--sign", "-", "--timestamp=none", app)
    run("codesign", "--verify", "--deep", "--strict", app)
    (stage / "Applications").symlink_to("/Applications")
    args.output.mkdir(parents=True, exist_ok=True)
    artifact = args.output.resolve() / f"iFCN-{args.version}-macos-arm64.dmg"
    run("hdiutil", "create", "-volname", f"iFCN {args.version}", "-srcfolder", stage,
        "-ov", "-format", "UDZO", artifact)
    run("hdiutil", "verify", artifact)
    archive = artifact.with_suffix(".zip")
    run("ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", app, archive)
    print(artifact, flush=True)


if __name__ == "__main__":
    main()
