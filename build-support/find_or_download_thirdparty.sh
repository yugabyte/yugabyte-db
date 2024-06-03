#!/usr/bin/env bash

# Copyright (c) Yugabyte, Inc.
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

set -euo pipefail

# shellcheck source=build-support/common-build-env.sh
. "${BASH_SOURCE%/*}/common-build-env.sh"

if [[ -n ${BUILD_ROOT:-} ]]; then
  handle_predefined_build_root_quietly=true
  predefined_build_root=$BUILD_ROOT
  handle_predefined_build_root
fi

find_or_download_thirdparty
