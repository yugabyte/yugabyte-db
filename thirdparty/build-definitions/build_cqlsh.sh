#!/usr/bin/env bash

# Copyright (c) YugaByte, Inc.

CQLSH_VERSION=3.10
CQLSH_DIR=$TP_SOURCE_DIR/cqlsh-${CQLSH_VERSION}
CQLSH_URL="https://github.com/YugaByte/cqlsh/archive/v${CQLSH_VERSION}.tar.gz"

TP_NAME_TO_SRC_DIR["cqlsh"]=$CQLSH_DIR
TP_NAME_TO_ARCHIVE_NAME["cqlsh"]="cqlsh-${CQLSH_VERSION}.tar.gz"
TP_NAME_TO_URL["cqlsh"]=$CQLSH_URL

build_cqlsh() {
  # cqlsh is already prebuilt, just need to install.
  create_build_dir_and_prepare "$CQLSH_DIR"
  ln -sf "${PWD##*/}" ../cqlsh
  log "Installing cqlsh..."
  (
    set_build_env_vars
    set -x
    rsync -av bin/ "$PREFIX/bin/"
    rsync -av lib/ "$PREFIX/lib/"
    rsync -av pylib/ "$PREFIX/pylib/"
  )
}
