#!/bin/bash
#
# Copyright (c) YugaByte, Inc.
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
. "${BASH_SOURCE%/*}/../build-support/common-build-env.sh"

export YB_IS_BUILD_THIRDPARTY_SCRIPT=1
activate_virtualenv
export PYTHONPATH=$YB_SRC_ROOT/python:${PYTHONPATH:-}
detect_brew
add_brew_bin_to_path
echo "YB_LINUXBREW_DIR=${YB_LINUXBREW_DIR:-undefined}"
echo "YB_CUSTOM_HOMEBREW_DIR=${YB_CUSTOM_HOMEBREW_DIR:-undefined}"
python2 "$YB_SRC_ROOT/thirdparty/yb_build_thirdparty_main.py" "$@"
