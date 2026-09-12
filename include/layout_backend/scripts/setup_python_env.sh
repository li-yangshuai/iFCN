#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LAYOUT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
VENV_DIR="${IFCN_LAYOUT_VENV:-${LAYOUT_ROOT}/myenv}"
PYTHON_BIN="${PYTHON_BIN:-python3}"

if [[ ! -x "${VENV_DIR}/bin/python" ]]; then
  "${PYTHON_BIN}" -m venv "${VENV_DIR}"
fi

"${VENV_DIR}/bin/python" -m pip install --upgrade pip
"${VENV_DIR}/bin/python" -m pip install -r "${LAYOUT_ROOT}/requirements.txt"
"${VENV_DIR}/bin/python" - <<'PY'
import sys
import numpy
import scipy
import matplotlib
import networkx

print("Layout Python:", sys.executable)
print("NumPy:", numpy.__version__)
print("SciPy:", scipy.__version__)
print("Matplotlib:", matplotlib.__version__)
print("NetworkX:", networkx.__version__)
PY
