# Copyright (c) YugaByte, Inc.

RAPIDJSON_VERSION=0.11
RAPIDJSON_DIR=$TP_SOURCE_DIR/rapidjson-${RAPIDJSON_VERSION}

build_rapidjson() {
  # just installing it into our prefix
  (
    set_build_env_vars
    set -x
    rsync -av --delete "$RAPIDJSON_DIR/include/rapidjson/" "$PREFIX/include/rapidjson/"
  )
}
