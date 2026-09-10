#!/usr/bin/env bash
#
# Copyright (c) YugabyteDB, Inc.
#
# Sourced by the openapi scripts to set NPX. The bin directory beside the global node_modules is
# where Jenkins' node keeps npx, and it is preferred so CI keeps using the npx it always has. It
# is only there when npm's global prefix is the one that shipped with node, though: with a custom
# prefix (npm config prefix, NPM_CONFIG_PREFIX) or an nvm node alongside an older prefix, that
# path has no npx, and the caller would fail after having already removed openapi.yaml.

export NPM_BIN="$(npm root -g 2>/dev/null)/../../bin"
NPX="${NPM_BIN}/npx"
if [[ ! -x "$NPX" ]]; then
  NPX="$(command -v npx || true)"
fi
if [[ -z "$NPX" ]]; then
  echo "ERROR: no npx in ${NPM_BIN} and none on PATH; install node/npm or fix npm's prefix" >&2
  exit 1
fi
echo "npm bin at: ${NPM_BIN}, using npx at: ${NPX}"
