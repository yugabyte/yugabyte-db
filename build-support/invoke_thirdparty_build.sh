#!/usr/bin/env bash

set -euo pipefail

# shellcheck source=build-support/common-build-env.sh
. "${BASH_SOURCE[0]%/*}/common-build-env.sh"

if [[ -n ${YB_THIRDPARTY_DIR:-} &&
      $YB_THIRDPARTY_DIR != "$YB_SRC_ROOT/thirdparty" ]]; then
  fatal "YB_THIRDPARTY_ROOT is set and is not the 'thirdparty' subdirectory of the source root" \
        "($YB_SRC_ROOT)."
fi

thirdparty_sha1=$( "$YB_BUILD_SUPPORT_DIR/thirdparty_tool" --get-sha1 )
if [[ ! $thirdparty_sha1 =~ ^[0-9a-f]{40}$ ]]; then
  fatal "Invalid thirdparty SHA1: $thirdparty_sha1"
fi
YB_THIRDPARTY_DIR=$YB_SRC_ROOT/thirdparty
if [[ ! -d "$YB_THIRDPARTY_DIR" ]]; then
  git clone https://github.com/yugabyte/yugabyte-db-thirdparty.git "$YB_THIRDPARTY_DIR"
fi
cd "$YB_THIRDPARTY_DIR"
current_sha1=$( git rev-parse HEAD )
if [[ ! $current_sha1 =~ ^[0-9a-f]{40}$ ]]; then
  fatal "Could not get current git SHA1 in $PWD"
fi
if [[ $current_sha1 != $thirdparty_sha1 ]]; then
  if ! git checkout "$thirdparty_sha1"; then
    git fetch
    git checkout "$thirdparty_sha1"
  fi
fi

./build_thirdparty.sh "$@"
