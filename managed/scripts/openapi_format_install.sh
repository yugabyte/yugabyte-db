#!/bin/bash
# Ensure openapi-format is available locally so the batch formatter (openapi_format_batch.js) can
# `require` it. We install into scripts/node_modules (gitignored) rather than relying on a global
# install or per-file `npx` - the latter re-resolved the package on every one of the ~350 files and
# dominated build time.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
openapi_format_ver="1.17.1"
pkg_json="$SCRIPT_DIR/node_modules/openapi-format/package.json"

# Skip the install when the correct version is already present locally.
if [ -f "$pkg_json" ]; then
  installed_ver=$(node -e "process.stdout.write(require('$pkg_json').version)" 2>/dev/null \
    || echo "")
  if [ "$installed_ver" == "$openapi_format_ver" ]; then
    echo "Using local openapi-format version: $installed_ver"
    exit 0
  fi
fi

echo "Installing openapi-format@$openapi_format_ver into $SCRIPT_DIR/node_modules ..."
npm install --prefix "$SCRIPT_DIR" --no-save --no-package-lock --no-fund --no-audit \
  "openapi-format@$openapi_format_ver"
