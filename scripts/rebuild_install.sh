#!/usr/bin/env bash

# Reconfigure, rebuild, and install the local `agent` executable.
set -euo pipefail

readonly PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
readonly BUILD_DIR="${PROJECT_ROOT}/build"
readonly INSTALL_PREFIX="${HOME}/.local"

cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_INSTALL_PREFIX="${INSTALL_PREFIX}"

cmake --build "${BUILD_DIR}" --parallel
cmake --install "${BUILD_DIR}"

echo "Installed: ${INSTALL_PREFIX}/bin/agent"
