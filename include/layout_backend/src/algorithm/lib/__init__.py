"""Locate native bindings in an external CMake build via PYTHONPATH.

The standard ``build`` and ``build-layout`` directories are found automatically.
For a custom build, add ``<build>/python`` to PYTHONPATH or set
IFCN_LAYOUT_BINDINGS_DIR to ``<build>/python/lib``.
"""

import os
from pathlib import Path
from pkgutil import extend_path

__path__ = extend_path(__path__, __name__)

_source = Path(__file__).resolve()
_repo_root = _source.parents[5]
_layout_root = _source.parents[3]
_override = os.environ.get("IFCN_LAYOUT_BINDINGS_DIR", "").strip()
_candidates = ([Path(_override).expanduser()] if _override else []) + [
    _repo_root / "build-layout" / "python" / "lib",
    _repo_root / "build" / "python" / "lib",
    _layout_root / "build" / "python" / "lib",
]
for _directory in reversed(_candidates):
    if _directory.is_dir() and str(_directory) not in __path__:
        __path__.insert(0, str(_directory))
