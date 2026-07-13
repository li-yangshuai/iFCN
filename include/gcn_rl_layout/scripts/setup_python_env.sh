#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GCN_RL_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
VENV_DIR="${IFCN_GCN_RL_VENV:-${GCN_RL_ROOT}/myenv}"
PYTHON_BIN="${PYTHON_BIN:-python3}"
TORCH_INDEX_URL="${TORCH_INDEX_URL:-https://download.pytorch.org/whl/cu128}"
VENV_FLAGS=()
if [[ "${IFCN_GCN_RL_USE_SYSTEM_SITE:-0}" == "1" ]]; then
  VENV_FLAGS+=(--system-site-packages)
fi

if [[ ! -x "${VENV_DIR}/bin/python" ]]; then
  "${PYTHON_BIN}" -m venv "${VENV_FLAGS[@]}" "${VENV_DIR}"
fi

"${VENV_DIR}/bin/python" -m pip install --upgrade pip
if [[ "${IFCN_GCN_RL_SKIP_TORCH_INSTALL:-0}" != "1" ]]; then
  "${VENV_DIR}/bin/python" -m pip install torch torchvision torchaudio --index-url "${TORCH_INDEX_URL}"
fi
"${VENV_DIR}/bin/python" -m pip install torch_geometric scikit-learn matplotlib networkx

"${VENV_DIR}/bin/python" - <<'PY'
import sys
import torch
import torch_geometric
import sklearn
import matplotlib

print("GCN+RL Python:", sys.executable)
print("torch:", torch.__version__)
print("torch_geometric:", torch_geometric.__version__)
print("sklearn:", sklearn.__version__)
print("matplotlib:", matplotlib.__version__)
PY
