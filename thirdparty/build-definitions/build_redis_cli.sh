#!/usr/bin/env bash

# Copyright (c) YugaByte, Inc.

REDIS_VERSION=4.0.1
REDIS_DIR=$TP_SOURCE_DIR/redis-${REDIS_VERSION}
REDIS_URL="https://github.com/YugaByte/redis/archive/${REDIS_VERSION}.tar.gz"

TP_NAME_TO_SRC_DIR["redis_cli"]=$REDIS_DIR
TP_NAME_TO_ARCHIVE_NAME["redis_cli"]="redis-${REDIS_VERSION}.tar.gz"
TP_NAME_TO_URL["redis_cli"]=$REDIS_URL

build_redis_cli() {
  create_build_dir_and_prepare "$REDIS_DIR"
  (
    set_build_env_vars
    set_thirdparty_flags_for_autotools_projects
    set -x
    run_make redis-cli
    cp src/redis-cli $PREFIX/bin/
  )
}
