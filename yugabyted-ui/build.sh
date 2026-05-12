#!/usr/bin/env bash
set -ue -o pipefail

# Source common-build-env to get "log" function.
# shellcheck source=build-support/common-build-env.sh
. "${BASH_SOURCE[0]%/*}/../build-support/common-build-env.sh"

readonly BASEDIR=$YB_SRC_ROOT/yugabyted-ui

log "Starting Yugabyted UI Build"

cd "${BASEDIR}"
readonly API_SERVER_DIR=${BASEDIR}/apiserver/cmd/server
readonly UI_DIR=${BASEDIR}/ui
readonly OUT_DIR="${BUILD_ROOT:-/tmp/yugabyted-ui}/gobin"
readonly OUT_FILE="${OUT_DIR}/yugabyted-ui"
mkdir -p "${OUT_DIR}"

# Read required Node.js version from .nvmrc
readonly NVMRC_FILE="${BASEDIR}/.nvmrc"
if [[ -f "${NVMRC_FILE}" ]]; then
  REQUIRED_NODE_VERSION=$(head -n 1 "${NVMRC_FILE}" | tr -d '[:space:]')
  log "Required Node.js version from .nvmrc: ${REQUIRED_NODE_VERSION}"
else
  fatal ".nvmrc file not found at ${NVMRC_FILE}"
fi

REQUIRED_NODE_MAJOR_VERSION=$(echo "${REQUIRED_NODE_VERSION}" | cut -d'.' -f1)
NODE_SETUP_SUCCESS=false

# Function to check and log current node.js version
check_nodejs_version() {
  if command -v node &> /dev/null; then
    log "Current Node.js version: $(node --version)"
    return 0
  else
    log "Node.js not found in PATH"
    return 1
  fi
}

# Step 1: Check if nvm is available
log "Step 1: Checking for nvm..."

# Use the recommended nvm initialization pattern (works better in non-interactive shells)
export NVM_DIR="$HOME/.nvm"
log "NVM_DIR: $NVM_DIR"
log "Checking if nvm.sh exists..."
if [[ -f "$NVM_DIR/nvm.sh" ]]; then
  nvm_size=$(stat -c%s "$NVM_DIR/nvm.sh" 2>/dev/null || \
             stat -f%z "$NVM_DIR/nvm.sh" 2>/dev/null || \
             echo "unknown")
  log "nvm.sh exists at: $NVM_DIR/nvm.sh (size: ${nvm_size} bytes)"
else
  log "nvm.sh not found at: $NVM_DIR/nvm.sh"
fi

if [[ -s "$NVM_DIR/nvm.sh" ]]; then
  log "nvm.sh file exists and is non-empty"

  # Log environment details (helps debug non-interactive shell issues)
  log "Environment details:"
  log "  SHELL: ${SHELL:-not set}"
  log "  USER: ${USER:-not set}"
  log "  HOME: ${HOME:-not set}"
  log "  PWD: ${PWD:-not set}"
  log "  Is interactive shell: $([[ $- == *i* ]] && echo "yes" || echo "no")"
  log "  Shell options (\$-): $-"

  # Temporarily disable strict error checking for nvm.sh sourcing
  set +ue
  log "Attempting to source nvm.sh..."

  # Use the recommended pattern for non-interactive shells (Jenkins/CI)
  # Reference: https://github.com/nvm-sh/nvm/issues/1504
  export NVM_DIR

  nvm_loaded=false
  if [ -s "$NVM_DIR/nvm.sh" ]; then
    log "Trying to source nvm.sh from $NVM_DIR/nvm.sh"

    # The key is to source it in the current shell so the nvm function persists
    # shellcheck disable=SC1090,SC1091
    \. "$NVM_DIR/nvm.sh" 2>/dev/null || true

    # Check if nvm function is actually available after sourcing
    if type nvm &>/dev/null; then
      nvm_loaded=true
      log "Successfully sourced nvm.sh"
    else
      log "WARNING: Sourced nvm.sh but 'nvm' function is not available"
      log "  This can happen if nvm.sh detects a non-interactive shell and exits early"
    fi
  fi

  if [[ "$nvm_loaded" == "true" ]]; then
    log "Successfully loaded nvm"
    log "nvm version: $(nvm --version 2>/dev/null || echo 'unknown')"

    # Step 2: Try nvm use (in case version is already installed)
    log "Step 2: Trying 'nvm use ${REQUIRED_NODE_VERSION}'..."
    if nvm use "${REQUIRED_NODE_VERSION}" 2>/dev/null; then
      log "Successfully switched to Node.js ${REQUIRED_NODE_VERSION} via nvm use"
      check_nodejs_version
      NODE_SETUP_SUCCESS=true
    else
      # Step 3: nvm use failed, try nvm install
      log "Step 3: nvm use failed, trying 'nvm install ${REQUIRED_NODE_VERSION}'..."
      if nvm install "${REQUIRED_NODE_VERSION}" 2>/dev/null; then
        log "Successfully installed Node.js ${REQUIRED_NODE_VERSION} via nvm"
        nvm use "${REQUIRED_NODE_VERSION}" 2>/dev/null
        check_nodejs_version
        NODE_SETUP_SUCCESS=true
      else
        log "WARNING: nvm install failed"
      fi
    fi
  else
    log "WARNING: nvm could not be loaded"
    log "  - nvm.sh exists but failed to load in this non-interactive shell"
    log "  - This is common in Jenkins/CI environments"
    log "  - Will try fallback options (system Node.js or direct download)"
  fi
  set -ue
else
  log "nvm not found at $NVM_DIR/nvm.sh (file doesn't exist or is empty)"
fi

# Step 4: If nvm function failed, try using nvm-installed Node.js directly from disk
if [[ "${NODE_SETUP_SUCCESS}" != "true" ]]; then
  log "Step 4: nvm function not available, checking for nvm-installed Node.js on disk..."

  # Check if the required version is installed in nvm's directory structure
  NVM_NODE_DIR="${NVM_DIR}/versions/node/v${REQUIRED_NODE_VERSION}"
  if [[ -d "${NVM_NODE_DIR}" ]] && [[ -x "${NVM_NODE_DIR}/bin/node" ]]; then
    log "Found nvm-installed Node.js at ${NVM_NODE_DIR}"
    export PATH="${NVM_NODE_DIR}/bin:${PATH}"
    check_nodejs_version
    NODE_SETUP_SUCCESS=true
  else
    log "Node.js ${REQUIRED_NODE_VERSION} not found in nvm directory (${NVM_NODE_DIR})"

    # Also check for any installed Node.js >= required major version in nvm
    if [[ -d "${NVM_DIR}/versions/node" ]]; then
      log "Checking for other nvm-installed Node.js versions >= ${REQUIRED_NODE_MAJOR_VERSION}..."
      for node_version_dir in "${NVM_DIR}/versions/node"/v*; do
        if [[ -d "${node_version_dir}" ]] && [[ -x "${node_version_dir}/bin/node" ]]; then
          version_str=$(basename "${node_version_dir}")
          major_version="${version_str#v}"
          major_version="${major_version%%.*}"
          if [[ "${major_version}" -ge "${REQUIRED_NODE_MAJOR_VERSION}" ]]; then
            log "Found compatible nvm-installed Node.js: ${version_str}"
            export PATH="${node_version_dir}/bin:${PATH}"
            check_nodejs_version
            NODE_SETUP_SUCCESS=true
            break
          fi
        fi
      done
    fi
  fi
fi

# Step 5: Check system Node.js
if [[ "${NODE_SETUP_SUCCESS}" != "true" ]]; then
  log "Step 5: Checking system Node.js..."

  if command -v node &> /dev/null; then
    CURRENT_NODE_VERSION=$(node --version)
    # Extract major version: v22.18.0 -> 22
    CURRENT_NODE_MAJOR="${CURRENT_NODE_VERSION#v}"
    CURRENT_NODE_MAJOR="${CURRENT_NODE_MAJOR%%.*}"
    log "System Node.js version: ${CURRENT_NODE_VERSION}"

    if [[ "${CURRENT_NODE_MAJOR}" -ge "${REQUIRED_NODE_MAJOR_VERSION}" ]]; then
      log "System Node.js ${CURRENT_NODE_VERSION} OK (>= ${REQUIRED_NODE_MAJOR_VERSION} required)"
      NODE_SETUP_SUCCESS=true
    else
      log "System Node.js ${CURRENT_NODE_VERSION} too old (need >= ${REQUIRED_NODE_MAJOR_VERSION})"
    fi
  else
    log "No system Node.js found"
  fi
fi

# Step 6: Download Node.js if needed (last resort fallback)
if [[ "${NODE_SETUP_SUCCESS}" != "true" ]]; then
  log "Step 6: Downloading Node.js ${REQUIRED_NODE_VERSION} (fallback)..."

  # Determine architecture
  ARCH=$(uname -m)
  if [[ "${ARCH}" == "x86_64" ]]; then
    NODE_ARCH="x64"
  elif [[ "${ARCH}" == "aarch64" ]] || [[ "${ARCH}" == "arm64" ]]; then
    NODE_ARCH="arm64"
  else
    fatal "Unsupported architecture: ${ARCH}"
  fi

  # Determine OS
  if is_mac; then
    NODE_OS="darwin"
  else
    NODE_OS="linux"
  fi

  NODE_DIST="node-v${REQUIRED_NODE_VERSION}-${NODE_OS}-${NODE_ARCH}"
  NODE_URL="https://nodejs.org/dist/v${REQUIRED_NODE_VERSION}/${NODE_DIST}.tar.xz"
  NODE_INSTALL_DIR="${BUILD_ROOT:-/tmp}/nodejs"

  mkdir -p "${NODE_INSTALL_DIR}"

  if [[ ! -d "${NODE_INSTALL_DIR}/${NODE_DIST}" ]]; then
    log "Downloading from ${NODE_URL}"
    if curl -sL "${NODE_URL}" | tar -xJ -C "${NODE_INSTALL_DIR}"; then
      log "Successfully downloaded and extracted Node.js ${REQUIRED_NODE_VERSION}"
    else
      fatal "Failed to download Node.js from ${NODE_URL}"
    fi
  else
    log "Using cached Node.js at ${NODE_INSTALL_DIR}/${NODE_DIST}"
  fi

  export PATH="${NODE_INSTALL_DIR}/${NODE_DIST}/bin:${PATH}"
  log "Added ${NODE_INSTALL_DIR}/${NODE_DIST}/bin to PATH"
  check_nodejs_version
  NODE_SETUP_SUCCESS=true
fi

# Verify required tools
if ! command -v npm &> /dev/null; then
  fatal "npm could not be found"
fi

if ! command -v node &> /dev/null; then
  fatal "node could not be found"
fi

if ! command -v go &> /dev/null; then
  fatal "go language (the go executable) could not be found"
fi

log "Using Node.js version: $(node --version)"
log "Using npm version: $(npm --version)"

# Final verification of Node.js version
NODE_VERSION_STR=$(node --version)
NODE_MAJOR_VERSION="${NODE_VERSION_STR#v}"
NODE_MAJOR_VERSION="${NODE_MAJOR_VERSION%%.*}"
if [[ "${NODE_MAJOR_VERSION}" -lt "${REQUIRED_NODE_MAJOR_VERSION}" ]]; then
  fatal "Node.js version ${REQUIRED_NODE_VERSION} or higher is required (from .nvmrc). " \
        "Current version: $(node --version). " \
        "Please install using: 'nvm install ${REQUIRED_NODE_VERSION}'"
fi

log "Building UI Frontend"
(
cd "$UI_DIR"
npm ci || fatal "npm ci failed"
npm run build || fatal "npm run build failed"
tar cz ui | tar -C "${API_SERVER_DIR}" -xz || fatal "Failed to package UI artifacts"
log "UI build completed successfully"
)

log "Building Go Backend"
cd "$API_SERVER_DIR"
rm -f "${OUT_FILE}"

# Make double sure that the Linuxbrew bin directory is not in the path and Go cannot use the ld
# linker executable from that directory.
remove_linuxbrew_bin_from_path
export PATH=/usr/bin:$PATH

log "Building Go binary..."
go build -o "${OUT_FILE}" || fatal "Go build failed"

if [[ ! -f "${OUT_FILE}" ]]; then
  fatal "Build Failed: file ${OUT_FILE} not found."
fi

log "Build Completed Successfully!"
log "Yugabyted UI Binary generated successfully at ${OUT_FILE}"
log "Binary size: $(du -h "${OUT_FILE}" | cut -f1)"

if is_mac; then
  # The shared library interpreter validation below is only relevant for Linux.
  exit
fi

echo "Running ldd on ${OUT_FILE}"
ldd "${OUT_FILE}"

ld_interpreter=$( patchelf --print-interpreter "${OUT_FILE}" )
if [[ ${ld_interpreter} != /lib*/ld-linux-* ]]; then
  log "WARNING: ${OUT_FILE} is not correctly configured with the shared library interpreter:" \
      "${ld_interpreter}. Patching it now."

  default_interpreter=$( patchelf --print-interpreter /bin/bash )
  (
    set -x
    patchelf --set-interpreter "${default_interpreter}" "${OUT_FILE}"
  )
else
  log "${OUT_FILE} is correctly configured with the shared library interpreter: ${ld_interpreter}"
fi
