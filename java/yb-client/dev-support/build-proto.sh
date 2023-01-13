#!/usr/bin/env bash
#
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#
##
# Script to find and run protoc to generate protocol buf files.
# Should be used exclusively by Maven.
#

# The following only applies to changes made to this file as part of YugaByte development.
#
# Portions Copyright (c) YugaByte, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
# in compliance with the License.  You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software distributed under the License
# is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
# or implied.  See the License for the specific language governing permissions and limitations
# under the License.
#

. "${BASH_SOURCE%/*}/../../../build-support/common-build-env.sh"

if [[ -z ${BUILD_ROOT:-} ]]; then
  set_cmake_build_type_and_compiler_type
  set_build_root
fi

find_or_download_thirdparty

THIRDPARTY_BUILD_TYPE=uninstrumented

thirdparty_installed_dir_from_cmake_cache=$(
  grep --max-count=1 "YB_THIRDPARTY_INSTALLED_DIR:STRING=" "${BUILD_ROOT}/CMakeCache.txt"
)

if [[ -n ${thirdparty_installed_dir_from_cmake_cache} ]]; then
  thirdparty_installed_dir=${thirdparty_installed_dir_from_cmake_cache#*:STRING=}
else
  thirdparty_installed_dir="$YB_THIRDPARTY_DIR/installed/$THIRDPARTY_BUILD_TYPE"
fi
PROTOC_BIN=${thirdparty_installed_dir}/${THIRDPARTY_BUILD_TYPE}/bin/protoc

if [[ ! -f $PROTOC_BIN ]]; then
  if which protoc > /dev/null; then
    PROTOC_BIN=$( which protoc )
  else
    fatal "Error: protoc is missing at '$PROTOC_BIN' (YB_THIRDPARTY_DIR=$YB_THIRDPARTY_DIR) and " \
          "on the PATH"
  fi
fi

set +e
"$PROTOC_BIN" "$@"
exit_code=$?
set -e

if [[ $exit_code -ne 0 ]]; then
  log "protoc command failed with exit code $exit_code: ( cd \"$PWD\" && \"$PROTOC_BIN\" $* )"
fi
exit "$exit_code"
