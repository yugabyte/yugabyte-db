#!/usr/bin/env bash
# Setup Node.js version using nvm or fallback methods.
# It will find .nvmrc in the same directory as this script, or use UI_DIR if provided.


set -ue -o pipefail

# Determine UI directory - use provided UI_DIR or find it relative to this script
if [[ -n "${UI_DIR:-}" ]]; then
  readonly NVMRC_FILE="${UI_DIR}/.nvmrc"
else
  readonly SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
  readonly NVMRC_FILE="${SCRIPT_DIR}/.nvmrc"
fi

# Read and validate .nvmrc file
if [[ ! -f "${NVMRC_FILE}" ]]; then
  echo "[setup-node.sh] ERROR: .nvmrc file not found at ${NVMRC_FILE}" >&2
  echo "[setup-node.sh]   Please create .nvmrc with the required Node.js version" >&2
  exit 1
fi

REQUIRED_NODE_VERSION=$(head -n 1 "${NVMRC_FILE}" | tr -d '[:space:]')
if [[ -z "${REQUIRED_NODE_VERSION}" ]]; then
  echo "[setup-node.sh] ERROR: .nvmrc file is empty" >&2
  exit 1
fi

REQUIRED_NODE_MAJOR_VERSION=$(echo "${REQUIRED_NODE_VERSION}" | cut -d'.' -f1)

# Logs the current Node.js version if available
check_nodejs_version() {
  if command -v node &> /dev/null; then
    echo "[setup-node.sh] Current Node.js version: $(node --version)"
    return 0
  else
    echo "[setup-node.sh] Node.js not found in PATH"
    return 1
  fi
}

# Attempts to use nvm to switch to or install the required Node.js version
# Returns 0 on success, 1 on failure
try_nvm_setup() {
  export NVM_DIR="$HOME/.nvm"
  
  if [[ ! -s "$NVM_DIR/nvm.sh" ]]; then
    return 1
  fi

  # Temporarily disable strict error checking for nvm.sh sourcing
  # nvm.sh may exit early in non-interactive shells, which is expected
  set +ue
  nvm_loaded=false
  
  \. "$NVM_DIR/nvm.sh" 2>/dev/null || true
  
  if type nvm &>/dev/null; then
    nvm_loaded=true
  fi
  set -ue -o pipefail

  if [[ "$nvm_loaded" != "true" ]]; then
    return 1
  fi

  # Try to use the version (fast if already installed)
  if nvm use "${REQUIRED_NODE_VERSION}" 2>/dev/null; then
    check_nodejs_version
    return 0
  fi

  # Version not installed, try to install it
  if nvm install "${REQUIRED_NODE_VERSION}" 2>/dev/null; then
    nvm use "${REQUIRED_NODE_VERSION}" 2>/dev/null
    check_nodejs_version
    return 0
  fi

  return 1
}

# Attempts to use nvm-installed Node.js directly from disk (fallback when nvm function unavailable)
# Checks for exact version first, then compatible major versions
# Returns 0 on success, 1 on failure
try_nvm_disk_install() {
  export NVM_DIR="$HOME/.nvm"
  
  # Check for exact version first
  local nvm_node_dir="${NVM_DIR}/versions/node/v${REQUIRED_NODE_VERSION}"
  if [[ -d "${nvm_node_dir}" ]] && [[ -x "${nvm_node_dir}/bin/node" ]]; then
    export PATH="${nvm_node_dir}/bin:${PATH}"
    check_nodejs_version
    return 0
  fi

  # Search for compatible major version
  if [[ -d "${NVM_DIR}/versions/node" ]]; then
    for node_version_dir in "${NVM_DIR}/versions/node"/v*; do
      if [[ -d "${node_version_dir}" ]] && [[ -x "${node_version_dir}/bin/node" ]]; then
        local version_str=$(basename "${node_version_dir}")
        local major_version="${version_str#v}"
        major_version="${major_version%%.*}"
        if [[ "${major_version}" -ge "${REQUIRED_NODE_MAJOR_VERSION}" ]]; then
          export PATH="${node_version_dir}/bin:${PATH}"
          check_nodejs_version
          return 0
        fi
      fi
    done
  fi

  return 1
}

# Attempts to use system-installed Node.js if major version is compatible
# Returns 0 on success, 1 on failure
try_system_node() {
  if ! command -v node &> /dev/null; then
    return 1
  fi

  local current_node_version=$(node --version)
  local current_major="${current_node_version#v}"
  current_major="${current_major%%.*}"

  if [[ "${current_major}" -ge "${REQUIRED_NODE_MAJOR_VERSION}" ]]; then
    echo "[setup-node.sh] Using system Node.js ${current_node_version}"
    return 0
  else
    echo "[setup-node.sh] System Node.js ${current_node_version} is too old (requires >= ${REQUIRED_NODE_MAJOR_VERSION})"
    return 1
  fi
}

# Downloads and installs Node.js from nodejs.org (last resort fallback)
# Returns 0 on success, exits with error on failure
download_nodejs() {
  # Determine architecture
  local arch=$(uname -m)
  local node_arch
  if [[ "${arch}" == "x86_64" ]]; then
    node_arch="x64"
  elif [[ "${arch}" == "aarch64" ]] || [[ "${arch}" == "arm64" ]]; then
    node_arch="arm64"
  else
    echo "[setup-node.sh] ERROR: Unsupported architecture: ${arch}" >&2
    echo "[setup-node.sh]   Supported architectures: x86_64, aarch64, arm64" >&2
    exit 1
  fi

  # Determine OS
  local node_os
  if [[ "$(uname -s)" == "Darwin" ]]; then
    node_os="darwin"
  else
    node_os="linux"
  fi

  local node_dist="node-v${REQUIRED_NODE_VERSION}-${node_os}-${node_arch}"
  local node_url="https://nodejs.org/dist/v${REQUIRED_NODE_VERSION}/${node_dist}.tar.xz"
  local install_dir="${BUILD_ROOT:-/tmp}/nodejs"

  mkdir -p "${install_dir}"

  if [[ ! -d "${install_dir}/${node_dist}" ]]; then
    echo "[setup-node.sh] Downloading Node.js ${REQUIRED_NODE_VERSION} from ${node_url}" >&2
    if ! curl -sL "${node_url}" | tar -xJ -C "${install_dir}"; then
      echo "[setup-node.sh] ERROR: Failed to download or extract Node.js from ${node_url}" >&2
      echo "[setup-node.sh]   Please check your internet connection and try again" >&2
      exit 1
    fi
  fi

  export PATH="${install_dir}/${node_dist}/bin:${PATH}"
  check_nodejs_version
}

# Main setup logic: try each method in order until one succeeds
echo "[setup-node.sh] Required Node.js version: ${REQUIRED_NODE_VERSION}"

if try_nvm_setup; then
  : # Success via nvm
elif try_nvm_disk_install; then
  : # Success via nvm disk installation
elif try_system_node; then
  : # Success via system Node.js
else
  # Last resort: download Node.js
  download_nodejs
fi

# Verify npm and node are available
if ! command -v npm &> /dev/null; then
  echo "[setup-node.sh] ERROR: npm not found after Node.js setup" >&2
  echo "[setup-node.sh]   This may indicate a corrupted Node.js installation" >&2
  exit 1
fi

if ! command -v node &> /dev/null; then
  echo "[setup-node.sh] ERROR: node not found after setup" >&2
  echo "[setup-node.sh]   PATH may not be configured correctly" >&2
  exit 1
fi

# Final version verification
NODE_VERSION_STR=$(node --version)
NODE_MAJOR_VERSION="${NODE_VERSION_STR#v}"
NODE_MAJOR_VERSION="${NODE_MAJOR_VERSION%%.*}"
if [[ "${NODE_MAJOR_VERSION}" -lt "${REQUIRED_NODE_MAJOR_VERSION}" ]]; then
  echo "[setup-node.sh] ERROR: Node.js version mismatch" >&2
  echo "[setup-node.sh]   Required: >= ${REQUIRED_NODE_MAJOR_VERSION} (from .nvmrc: ${REQUIRED_NODE_VERSION})" >&2
  echo "[setup-node.sh]   Current: ${NODE_VERSION_STR}" >&2
  echo "[setup-node.sh]   Please install the correct version: nvm install ${REQUIRED_NODE_VERSION}" >&2
  exit 1
fi

echo "[setup-node.sh] Node.js setup completed: $(node --version), npm $(npm --version)"

