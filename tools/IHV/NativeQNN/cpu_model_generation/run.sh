#!/usr/bin/env bash
# Copyright (c) 2026 Qualcomm Innovation Center, Inc. All rights reserved.
# Licensed under the Apache License, Version 2.0
#
# Linux CPU model bin generator for QAIRT GenAI composer.
# Steps:
#   1. Download QAIRT SDK (if missing)
#   2. Create Python 3.10 venv
#   3. Source QAIRT envsetup.sh
#   4. Run check-linux-dependency.sh
#   5. Run check-python-dependency
#   6. Clone HF model repo (git-lfs)
#   7. Run qnn-genai-transformer-composer

set -euo pipefail

# -------- config --------
QAIRT_VERSION="${QAIRT_VERSION:-2.46.0.260424}"
QAIRT_URL="${QAIRT_URL:-https://softwarecenter.qualcomm.com/api/download/software/sdks/Qualcomm_AI_Runtime_Community/All/${QAIRT_VERSION}/v${QAIRT_VERSION}.zip}"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
QAIRT_ROOT="${SCRIPT_DIR}/qairt"
VENV_DIR="${SCRIPT_DIR}/venv"
PYTHON_BIN="${PYTHON_BIN:-python3.10}"

MODEL="${1:-phi4-mini}"

case "${MODEL}" in
  llama3)
    REPO_URL="https://huggingface.co/meta-llama/Llama-3.1-8B-Instruct"
    REPO_DIR="Llama-3.1-8B-Instruct"
    OUT_BIN="llama3_cpu.bin"
    CONFIG_FILE=""
    ;;
  phi4)
    REPO_URL="https://huggingface.co/microsoft/Phi-4-reasoning"
    REPO_DIR="Phi-4-reasoning"
    OUT_BIN="phi4_cpu.bin"
    CONFIG_FILE="${SCRIPT_DIR}/phi4_config_file.json"
    ;;
  phi4-mini)
    REPO_URL="https://huggingface.co/microsoft/Phi-4-mini-instruct"
    REPO_DIR="Phi-4-mini-instruct"
    OUT_BIN="phi4_mini_cpu.bin"
    CONFIG_FILE="${SCRIPT_DIR}/phi4_mini_config_file.json"
    ;;
  *)
    echo "Unknown model: ${MODEL}. Use one of: llama3 | phi4 | phi4-mini"
    exit 1
    ;;
esac

cd "${SCRIPT_DIR}"

# -------- 1. download QAIRT SDK --------
if [[ ! -d "${QAIRT_ROOT}" ]]; then
  echo "[1/7] Downloading QAIRT SDK ${QAIRT_VERSION}..."
  zip="qairt_${QAIRT_VERSION}.zip"
  curl -L --fail -o "${zip}" "${QAIRT_URL}"
  mkdir -p "${QAIRT_ROOT}"
  unzip -q "${zip}" -d "${QAIRT_ROOT}"
  rm -f "${zip}"
else
  echo "[1/7] QAIRT SDK already present at ${QAIRT_ROOT}"
fi

# Locate the SDK root by finding envsetup.sh; the zip may nest it at an
# arbitrary depth (e.g. qairt/<ver>/ or qairt/qairt/<ver>/).
envsetup_path="$(find "${QAIRT_ROOT}" -name envsetup.sh -path '*/bin/*' | head -n1)"
if [[ -z "${envsetup_path}" ]]; then
  echo "ERROR: could not locate bin/envsetup.sh under ${QAIRT_ROOT}"
  exit 1
fi
QAIRT_SDK_ROOT="$(dirname "$(dirname "${envsetup_path}")")"
echo "      QAIRT_SDK_ROOT=${QAIRT_SDK_ROOT}"

# -------- 2. python 3.10 venv --------
if [[ ! -d "${VENV_DIR}" ]]; then
  echo "[2/7] Creating Python 3.10 venv..."
  "${PYTHON_BIN}" -m venv "${VENV_DIR}"
fi
# shellcheck disable=SC1091
source "${VENV_DIR}/bin/activate"
pip install --quiet --upgrade pip
pip install --quiet requests tqdm numpy sentencepiece cmake

# -------- 3. envsetup --------
echo "[3/7] Sourcing QAIRT envsetup.sh..."
# envsetup.sh references unset vars (e.g. PYTHONPATH); relax nounset around it.
: "${PYTHONPATH:=}"
set +u
# shellcheck disable=SC1091
source "${QAIRT_SDK_ROOT}/bin/envsetup.sh"
set -u

# -------- 4. check-linux-dependency --------
echo "[4/7] Checking Linux dependencies..."
bash "${QAIRT_SDK_ROOT}/bin/check-linux-dependency.sh"

# -------- 5. check-python-dependency --------
echo "[5/7] Checking Python dependencies..."
"${QAIRT_SDK_ROOT}/bin/check-python-dependency"

# -------- 6. clone HF repo with LFS --------
echo "[6/7] Cloning ${REPO_URL} ..."
if ! command -v git-lfs >/dev/null 2>&1; then
  echo "git-lfs is required. Install it (e.g. 'sudo apt install git-lfs') and rerun."
  exit 1
fi
git lfs install --skip-repo
if [[ ! -d "${REPO_DIR}" ]]; then
  git clone "${REPO_URL}" "${REPO_DIR}"
else
  echo "      ${REPO_DIR} already present, skipping clone."
fi

# -------- 7. run composer --------
echo "[7/7] Running qnn-genai-transformer-composer..."
COMPOSER="${QAIRT_SDK_ROOT}/bin/x86_64-linux-clang/qnn-genai-transformer-composer"
MODEL_PATH="${SCRIPT_DIR}/${REPO_DIR}"
OUT_PATH="${SCRIPT_DIR}/${OUT_BIN}"

cmd=(python "${COMPOSER}" --quantize Z8 --outfile "${OUT_PATH}" --model "${MODEL_PATH}")
if [[ -n "${CONFIG_FILE}" ]]; then
  cmd+=(--config_file "${CONFIG_FILE}")
fi
echo "      ${cmd[*]}"
"${cmd[@]}"

echo "Done. Output: ${OUT_PATH}"
