#!/usr/bin/env bash

. "${BASH_SOURCE%/*}/thirdparty-common.sh"

cd "$YB_THIRDPARTY_DIR"

set -x
# Do not remove downloaded third-party tarballs or Vim's temporary files.
git clean -dxf \
  --exclude download/ \
  --exclude '*.sw?' \
