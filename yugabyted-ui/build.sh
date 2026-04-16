#!/usr/bin/env bash
set -ue -o pipefail

# Source common-build-env to get "log" function.
# shellcheck source=build-support/common-build-env.sh
. "${BASH_SOURCE[0]%/*}/../build-support/common-build-env.sh"

readonly BASEDIR=$YB_SRC_ROOT/yugabyted-ui

cd "${BASEDIR}"
readonly API_SERVER_DIR=${BASEDIR}/apiserver/cmd/server
readonly UI_DIR=${BASEDIR}/ui
readonly OUT_DIR="${BUILD_ROOT:-/tmp/yugabyted-ui}/gobin"
readonly OUT_FILE="${OUT_DIR}/yugabyted-ui"
mkdir -p "${OUT_DIR}"

if ! command -v npm -version &> /dev/null
then
  fatal "npm could not be found"
fi

if ! command -v go version &> /dev/null
then
  fatal "go language (the go executable) could not be found"
fi

(
cd "$UI_DIR"
npm ci
npm run build
tar cz ui | tar -C "${API_SERVER_DIR}" -xz
)

cd "$API_SERVER_DIR"
rm -f "${OUT_FILE}"

# Make double sure that the Linuxbrew bin directory is not in the path and Go cannot use the ld
# linker executable from that directory.
remove_linuxbrew_bin_from_path
export PATH=/usr/bin:$PATH

go build -o "${OUT_FILE}"

if [[ ! -f "${OUT_FILE}" ]]; then
  fatal "Build Failed: file ${OUT_FILE} not found."
fi

log "Yugabyted UI Binary generated successfully at ${OUT_FILE}"

if is_mac; then
  # The shared library interpreter validation below is only relevant for Linux.
  exit
fi

echo "Running ldd on ${OUT_FILE}"
ldd "${OUT_FILE}"

ld_interpreter=$( patchelf --print-interpreter "${OUT_FILE}" )
if [[ ${ld_interpreter} == /lib*/ld-linux-* ]]; then
  log "${OUT_FILE} is correctly configured with the shared library interpreter: ${ld_interpreter}"
  exit
fi

log "WARNING: ${OUT_FILE} is not correctly configured with the shared library interpreter:" \
    "${ld_interpreter}. Patching it now."

default_interpreter=$( patchelf --print-interpreter /bin/bash )
(
  set -x
  patchelf --set-interpreter "${default_interpreter}" "${OUT_FILE}"
)
