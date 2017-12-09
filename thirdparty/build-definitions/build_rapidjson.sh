# Copyright (c) YugaByte, Inc.


RAPIDJSON_VERSION=1.1.0
RAPID_JSON_URL="https://github.com/Tencent/rapidjson/archive/v${RAPIDJSON_VERSION}.zip"
RAPIDJSON_DIR=$TP_SOURCE_DIR/rapidjson-${RAPIDJSON_VERSION}
TP_NAME_TO_SRC_DIR["rapidjson"]=$RAPIDJSON_DIR
TP_NAME_TO_ARCHIVE_NAME["rapidjson"]="rapidjson-${RAPIDJSON_VERSION}.zip"
TP_NAME_TO_URL["rapidjson"]="$RAPID_JSON_URL"

build_rapidjson() {
  # just installing it into our prefix
  (
    set_build_env_vars
    set -x
    rsync -av --delete "$RAPIDJSON_DIR/include/rapidjson/" "$PREFIX/include/rapidjson/"
  )
}
