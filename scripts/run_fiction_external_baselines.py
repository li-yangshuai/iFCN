#!/usr/bin/env python3
"""Reproduce the fiction/MNT external context baselines without containers.

This runner pins fiction v0.7.0 and MNT Bench v0.3.8, downloads only the 13
Walter-2024 versatility functions from the official MNT Bench endpoint, builds
the upstream experiment natively, and executes both clock-number assignment
and a small GOLD combinational P&R subset.  It deliberately does not translate
iFCN's cyclic sequential layouts to FGL because that would discard iteration
distance, absolute epoch, and initiation-interval semantics.
"""

from __future__ import annotations

import argparse
import hashlib
import shutil
import subprocess
import sys
import urllib.parse
import urllib.request
import zipfile
from pathlib import Path


FICTION_TAG = "v0.7.0"
FICTION_COMMIT = "de99d1fb559c3e58b5b9b9abf1d09c665bbb2e90"
MNTBENCH_TAG = "v0.3.8"
MNTBENCH_COMMIT = "481e976c528fedc898aa6f64042b2c400dbdbd79"
MNTBENCH_SUBSET_SHA256 = "ae42b42f1df7e7cfebfb9ab2954a5383c88b20e2f9a8a657d3738202a3b4b29c"


def run(args: list[str], *, cwd: Path, stdout=None) -> None:
    print("+", " ".join(args), flush=True)
    subprocess.run(args, cwd=cwd, check=True, stdout=stdout)


def output(args: list[str], *, cwd: Path) -> str:
    return subprocess.run(
        args, cwd=cwd, check=True, text=True, stdout=subprocess.PIPE
    ).stdout.strip()


def clone_pinned(url: str, tag: str, commit: str, destination: Path, root: Path) -> None:
    if not destination.exists():
        destination.parent.mkdir(parents=True, exist_ok=True)
        run(
            ["git", "clone", "--depth", "1", "--branch", tag, url, str(destination)],
            cwd=root,
        )
    actual = output(["git", "rev-parse", "HEAD"], cwd=destination)
    if actual != commit:
        raise SystemExit(f"{destination}: expected {commit}, found {actual}")


def replace_once(text: str, old: str, new: str, label: str) -> str:
    if new in text:
        return text
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"cannot apply build-only overlay {label}: found {count} matches")
    return text.replace(old, new)


def apply_build_only_overlay(fiction: Path) -> None:
    """Pin floating FetchContent inputs and support archive-without-submodules builds."""
    dependencies_path = fiction / "cmake/Dependencies.cmake"
    text = dependencies_path.read_text(encoding="utf-8")
    replacements = [
        (
            "FetchContent_Declare(\n  nlohmann_json\n  GIT_REPOSITORY "
            "https://github.com/nlohmann/json.git\n  GIT_TAG v3.12.0)",
            "FetchContent_Declare(\n  nlohmann_json\n  URL "
            "https://github.com/nlohmann/json/archive/refs/tags/v3.12.0.tar.gz\n"
            "  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)",
            "nlohmann_json",
        ),
        (
            "FetchContent_Declare(\n  pybind11\n  GIT_REPOSITORY "
            "https://github.com/pybind/pybind11.git\n  GIT_TAG v3.0.1)",
            "FetchContent_Declare(\n  pybind11\n  URL "
            "https://github.com/pybind/pybind11/archive/refs/tags/v3.0.1.tar.gz\n"
            "  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)",
            "pybind11",
        ),
        (
            "FetchContent_Declare(\n  parallel-hashmap\n  GIT_REPOSITORY "
            "https://github.com/greg7mdp/parallel-hashmap.git\n  GIT_TAG v2.0.0)",
            "FetchContent_Declare(\n  parallel-hashmap\n  URL "
            "https://github.com/greg7mdp/parallel-hashmap/archive/refs/tags/v2.0.0.tar.gz\n"
            "  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)",
            "parallel-hashmap",
        ),
        (
            "FetchContent_Declare(\n  tinyxml2\n  GIT_REPOSITORY "
            "https://github.com/leethomason/tinyxml2.git\n  GIT_TAG 11.0.0)",
            "FetchContent_Declare(\n  tinyxml2\n  URL "
            "https://github.com/leethomason/tinyxml2/archive/refs/tags/11.0.0.tar.gz\n"
            "  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)",
            "tinyxml2",
        ),
        (
            "FetchContent_Declare(\n  alice\n  GIT_REPOSITORY "
            "https://github.com/marcelwa/alice.git\n  GIT_TAG master # Using master as per submodule\n)",
            "FetchContent_Declare(\n  alice\n  URL "
            "https://github.com/marcelwa/alice/archive/"
            "6b7f941ca44f38226f5e2545224fa1194940cd73.tar.gz\n"
            "  DOWNLOAD_EXTRACT_TIMESTAMP TRUE\n)",
            "alice",
        ),
        (
            "FetchContent_Declare(\n  mockturtle\n  GIT_REPOSITORY "
            "https://github.com/marcelwa/mockturtle.git\n  GIT_TAG mnt # Using mnt branch as per submodule\n)",
            "FetchContent_Declare(\n  mockturtle\n  URL "
            "https://github.com/marcelwa/mockturtle/archive/"
            "b856d3e0028d3578ed6739d2885c4931db8bb837.tar.gz\n"
            "  DOWNLOAD_EXTRACT_TIMESTAMP TRUE\n)",
            "mockturtle",
        ),
    ]
    for old, new, label in replacements:
        text = replace_once(text, old, new, label)
    dependencies_path.write_text(text, encoding="utf-8")

    vendors_path = fiction / "vendors/CMakeLists.txt"
    vendors = vendors_path.read_text(encoding="utf-8")
    vendors = replace_once(
        vendors,
        "target_include_directories(libfiction SYSTEM INTERFACE\n"
        "    $<BUILD_INTERFACE:${parallel-hashmap_SOURCE_DIR}/parallel_hashmap>\n)",
        "target_include_directories(libfiction SYSTEM INTERFACE\n"
        "    $<BUILD_INTERFACE:${parallel-hashmap_SOURCE_DIR}>\n"
        "    $<BUILD_INTERFACE:${parallel-hashmap_SOURCE_DIR}/parallel_hashmap>\n)",
        "parallel-hashmap include roots",
    )
    vendors_path.write_text(vendors, encoding="utf-8")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def download_mntbench_subset(archive: Path) -> None:
    if archive.is_file():
        actual = sha256(archive)
        if actual != MNTBENCH_SUBSET_SHA256:
            raise SystemExit(
                f"{archive}: expected SHA-256 {MNTBENCH_SUBSET_SHA256}, found {actual}"
            )
        return
    archive.parent.mkdir(parents=True, exist_ok=True)
    benchmark_ids = [1, 3, 6, 7, 8, 9, 12, 13, 14, 18, 19, 20, 21]
    form: dict[str, str] = {"button": "submit"}
    form.update({f"selectBench_{identifier}": "true" for identifier in benchmark_ids})
    form.update(
        {
            "gate": "true",
            "one": "true",
            "twoddwave": "true",
            "use": "true",
            "res": "true",
            "nanoplacer": "true",
        }
    )
    request = urllib.request.Request(
        "https://www.cda.cit.tum.de/mntbench/download",
        data=urllib.parse.urlencode(form).encode("utf-8"),
        method="POST",
    )
    with urllib.request.urlopen(request, timeout=180) as response, archive.open("wb") as handle:
        shutil.copyfileobj(response, handle)
    actual = sha256(archive)
    if actual != MNTBENCH_SUBSET_SHA256:
        raise SystemExit(
            f"downloaded MNT Bench subset: expected SHA-256 "
            f"{MNTBENCH_SUBSET_SHA256}, found {actual}"
        )


def safe_extract(archive: Path, destination: Path) -> None:
    destination.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(archive) as bundle:
        root = destination.resolve()
        for member in bundle.infolist():
            target = (destination / member.filename).resolve()
            if not target.is_relative_to(root):
                raise SystemExit(f"unsafe archive member: {member.filename}")
        bundle.extractall(destination)


def install_versatility_layouts(layouts: Path, fiction: Path) -> None:
    selected = sorted(layouts.glob("*_UnOpt_UnOrd_area.fgl"))
    if len(selected) != 39:
        raise SystemExit(f"expected 39 unoptimized layouts, found {len(selected)}")
    destination = fiction / "experiments/clock_number_assignment/versatility_benchmarks"
    destination.mkdir(parents=True, exist_ok=True)
    expected_names = {path.name for path in selected}
    unexpected = sorted(
        path.name for path in destination.glob("*.fgl") if path.name not in expected_names
    )
    if unexpected:
        raise SystemExit(
            f"{destination}: unexpected pre-existing FGL files would change the official "
            f"experiment input set: {unexpected}"
        )
    for path in selected:
        target = destination / path.name
        if target.exists() and target.samefile(path):
            continue
        if target.is_symlink():
            target.unlink()
        shutil.copy2(path, target)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--repetitions", type=int, default=10)
    parser.add_argument("--jobs", type=int, default=2)
    parser.add_argument("--clean-build", action="store_true")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    if args.repetitions <= 0 or args.jobs <= 0:
        raise SystemExit("--repetitions and --jobs must be positive")

    root = Path(__file__).resolve().parents[1]
    tools = root / "build/tools/external"
    fiction = tools / "fiction-v0.7.0"
    mntbench = tools / "mnt-bench-v0.3.8"
    build = tools / "fiction-v0.7.0-build"
    artifacts = root / "build/artifacts/external_baselines"
    mnt_artifacts = artifacts / "mntbench_v0.3.8"
    archive = mnt_artifacts / "walter2024_versatility_mntbench.zip"
    layouts = mnt_artifacts / "layouts"
    walter_results = artifacts / "fiction_v0.7.0/walter2024"
    gold_results = artifacts / "fiction_v0.7.0/gold_subset"

    clone_pinned("https://github.com/cda-tum/fiction.git", FICTION_TAG, FICTION_COMMIT, fiction, root)
    clone_pinned(
        "https://github.com/cda-tum/mnt-bench.git",
        MNTBENCH_TAG,
        MNTBENCH_COMMIT,
        mntbench,
        root,
    )
    apply_build_only_overlay(fiction)
    download_mntbench_subset(archive)
    safe_extract(archive, layouts)
    install_versatility_layouts(layouts, fiction)

    wrapper_destination = fiction / "experiments/ifcn_external_baseline/fiction_gold_subset.cpp"
    wrapper_destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(root / "scripts/external_baselines/fiction_gold_subset.cpp", wrapper_destination)

    if args.clean_build and build.exists():
        print(f"removing runner-owned build directory {build}")
        shutil.rmtree(build)
    configure = [
        "cmake",
        "-S",
        str(fiction),
        "-B",
        str(build),
        "-DCMAKE_BUILD_TYPE=Release",
        "-DFICTION_CLI=OFF",
        "-DFICTION_EXPERIMENTS=ON",
        "-DFICTION_TEST=OFF",
        "-DFICTION_PROGRESS_BARS=OFF",
        "-DFICTION_ENABLE_CACHE=OFF",
        f"-DGIT_SHA={FICTION_COMMIT}",
    ]
    run(configure, cwd=root)
    run(
        [
            "cmake",
            "--build",
            str(build),
            "--target",
            "clock_number_assignment_versatility",
            "fiction_gold_subset",
            f"-j{args.jobs}",
        ],
        cwd=root,
    )

    walter_binary = build / "experiments/clock_number_assignment_versatility"
    upstream_json = fiction / "experiments/clock number assignment.json"
    for repetition in range(1, args.repetitions + 1):
        destination = walter_results / f"run_{repetition:02d}"
        destination.mkdir(parents=True, exist_ok=True)
        run([str(walter_binary)], cwd=root, stdout=subprocess.DEVNULL)
        shutil.copy2(upstream_json, destination / "results.json")

    gold_binary = build / "experiments/fiction_gold_subset"
    networks = [
        fiction / "benchmarks/trindade16/mux21.v",
        fiction / "benchmarks/trindade16/xor2.v",
        fiction / "benchmarks/trindade16/xnor2.v",
        fiction / "benchmarks/trindade16/par_gen.v",
    ]
    gold_results.mkdir(parents=True, exist_ok=True)
    for repetition in range(1, args.repetitions + 1):
        with (gold_results / f"run_{repetition:02d}.csv").open("w", encoding="utf-8") as handle:
            run(
                [str(gold_binary), "--mode", "high-efficiency", "--timeout-ms", "60000"]
                + [str(path) for path in networks],
                cwd=root,
                stdout=handle,
            )

    run(
        [
            sys.executable,
            str(root / "scripts/aggregate_fiction_clocking_baseline.py"),
            "--runs-dir",
            str(walter_results),
            "--expected-layouts",
            "39",
            "--expected-runs",
            str(args.repetitions),
        ],
        cwd=root,
    )
    run(
        [
            sys.executable,
            str(root / "scripts/build_walter2024_reported_comparison.py"),
        ],
        cwd=root,
    )
    run(
        [
            sys.executable,
            str(root / "scripts/aggregate_fiction_gold_baseline.py"),
            "--runs-dir",
            str(gold_results),
            "--expected-runs",
            str(args.repetitions),
        ],
        cwd=root,
    )
    run(
        [
            sys.executable,
            str(root / "scripts/build_external_baseline_scope.py"),
        ],
        cwd=root,
    )
    run(
        [
            sys.executable,
            str(root / "scripts/record_fiction_baseline_provenance.py"),
            "--repetitions",
            str(args.repetitions),
        ],
        cwd=root,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
