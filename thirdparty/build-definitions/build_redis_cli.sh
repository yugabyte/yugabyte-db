#!/usr/bin/env bash

# Copyright (c) YugaByte, Inc.

REDIS_VERSION=4.0.1
REDIS_DIR=$TP_SOURCE_DIR/redis-${REDIS_VERSION}
TP_NAME_TO_SRC_DIR["redis"]=$REDIS_DIR
REDIS_URL="s3://binaries.yugabyte.com/redis/redis-${REDIS_VERSION}.tar.gz"

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
