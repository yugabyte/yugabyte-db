#!/usr/bin/env bash

# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
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
# Portions Copyright (c) YugaByte, Inc.


set -euo pipefail

# We change YB_RUN_JAVA_TEST_METHODS_SEPARATELY in a subshell in a few places and that is OK.
# shellcheck disable=SC2031
export YB_RUN_JAVA_TEST_METHODS_SEPARATELY=1
export TSAN_OPTIONS=""
if is_mac; then
  # This is needed to make sure we're using Homebrew-installed CMake on Mac OS X.
  export PATH=/usr/local/bin:$PATH
fi
# gather core dumps
ulimit -c unlimited
detect_architecture
BUILD_TYPE=${BUILD_TYPE:-debug}
build_type=$BUILD_TYPE
normalize_build_type
readonly build_type
BUILD_TYPE=$build_type
readonly BUILD_TYPE
export BUILD_TYPE
log "BUILD_TYPE: ${BUILD_TYPE}"

log "YB_BUILD_OPTS: ${YB_BUILD_OPTS:-}"
for yb_opt in ${YB_BUILD_OPTS:-}; do
  if [[ $yb_opt =~ YB_.*=.* ]]; then
    log "  Setting from YB_BUILD_OPTS: $yb_opt"
    export "${yb_opt?}"
  fi
done

export YB_USE_NINJA=1
set_cmake_build_type_and_compiler_type
if [[ ${YB_DOWNLOAD_THIRDPARTY:-auto} == "auto" ]]; then
  log "Setting YB_DOWNLOAD_THIRDPARTY=1 automatically"
  export YB_DOWNLOAD_THIRDPARTY=1
fi
log "YB_DOWNLOAD_THIRDPARTY=$YB_DOWNLOAD_THIRDPARTY"
# This is normally done in set_build_root, but we need to decide earlier because this is factored
# into the decision of whether to use LTO.
decide_whether_to_use_linuxbrew
if [[ -z ${YB_LINKING_TYPE:-} ]]; then
  if ! is_mac && [[
        ${YB_COMPILER_TYPE} =~ ^clang[0-9]+$ &&
        ${BUILD_TYPE} == "release"
      ]]; then
    export YB_LINKING_TYPE=full-lto
  else
    export YB_LINKING_TYPE=dynamic
  fi
  log "Automatically decided to set YB_LINKING_TYPE to ${YB_LINKING_TYPE} based on:" \
      "YB_COMPILER_TYPE=${YB_COMPILER_TYPE}," \
      "BUILD_TYPE=${BUILD_TYPE}," \
      "YB_USE_LINUXBREW=${YB_USE_LINUXBREW}," \
      "YB_LINUXBREW_DIR=${YB_LINUXBREW_DIR:-undefined}."
else
  log "YB_LINKING_TYPE is already set to ${YB_LINKING_TYPE}"
fi
log "YB_LINKING_TYPE=${YB_LINKING_TYPE}"
export YB_LINKING_TYPE
