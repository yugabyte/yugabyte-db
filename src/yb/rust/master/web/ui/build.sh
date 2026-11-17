#!/usr/bin/env bash
# Builds the Rust master's web UI into ${BUILD_ROOT}/master-ui, where the C++ build points
# YB_MASTER_UI_DIR for master-web to embed it. yb_build.sh --build-master-ui runs this.
set -euo pipefail

# shellcheck source=build-support/common-build-env.sh
. "${BASH_SOURCE[0]%/*}/../../../../../../build-support/common-build-env.sh"

readonly UI_DIR=$YB_SRC_ROOT/src/yb/rust/master/web/ui

if [[ -z ${BUILD_ROOT:-} ]]; then
  fatal "BUILD_ROOT must be set: the UI is built into \${BUILD_ROOT}/master-ui"
fi
readonly OUT_DIR=$BUILD_ROOT/master-ui

REQUIRED_NODE_MAJOR=$(cut -d. -f1 < "$UI_DIR/.nvmrc")
readonly REQUIRED_NODE_MAJOR

node_major() {
  local version
  version=$(node --version 2>/dev/null) || return 1
  version=${version#v}
  echo "${version%%.*}"
}

node_ok() {
  local major
  major=$(node_major) && [[ $major -ge $REQUIRED_NODE_MAJOR ]]
}

# Node.js from PATH, else one installed by nvm, as in yugabyted-ui/build.sh.
if ! node_ok; then
  NVM_DIR=${NVM_DIR:-$HOME/.nvm}
  for node_bin in "$NVM_DIR"/versions/node/v*/bin; do
    if [[ -x $node_bin/node ]]; then
      version=${node_bin%/bin}
      version=${version##*/v}
      if [[ ${version%%.*} -ge $REQUIRED_NODE_MAJOR ]]; then
        export PATH=$node_bin:$PATH
        break
      fi
    fi
  done
fi
if ! node_ok; then
  fatal "Node.js >= ${REQUIRED_NODE_MAJOR} is required to build the master UI" \
        "(found: $(node --version 2>/dev/null || echo none)). Install it, e.g." \
        "'nvm install $(cat "$UI_DIR/.nvmrc")'."
fi
if ! command -v npm &>/dev/null; then
  fatal "npm could not be found next to $(command -v node)"
fi
log "Building the master UI with Node.js $(node --version), npm $(npm --version)"

cd "$UI_DIR"
npm ci --no-audit --no-fund || fatal "npm ci failed"
npm run build -- --outDir "$OUT_DIR" || fatal "npm run build failed"
log "Master UI built into $OUT_DIR"
