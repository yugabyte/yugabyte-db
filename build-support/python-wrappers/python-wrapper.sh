#!/usr/bin/env bash

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

# A wrapper script that pretends to be a Python interpreter.

set -euo pipefail

yb_is_python_wrapper_script=true

. "${0%/*}/../common-build-env.sh"

detect_brew
if [[ -n ${YB_PREVENT_PYTHON_WRAPPER_RECURSION:-} ]]; then
  fatal "python wrapper script appears to have gone into recursion: $*"
fi

python_interpreter_dirs=()
if [[ -n ${VIRTUAL_ENV:-} ]]; then
  python_interpreter_dirs+=( "$VIRTUAL_ENV/bin" )
fi

if using_custom_homebrew; then
  python_interpreter_dirs+=( "$YB_CUSTOM_HOMEBREW_DIR/bin" )
fi

# System Python installation. Also Homebrew Python on macOS.
python_interpreter_dirs+=(
  /usr/local/bin
  /usr/bin
)

# On Linux, don't use Linuxbrew Python unless absolutely necessary (put it last).
# We currently run into this error when we use it:
# https://gist.githubusercontent.com/mbautin/d75fbc212a029c65577c4880aefcbb07/raw
if using_linuxbrew; then
  python_interpreter_dirs+=( "$YB_LINUXBREW_DIR/bin" )
fi

interpreter_name=${0##*/}

interpreter_names=( "$interpreter_name" )
if [[ $interpreter_name == "python2" ]]; then
  interpreter_names+=( python2.7 )
fi

for python_dir in "${python_interpreter_dirs[@]}"; do
  for interpreter_name in "${interpreter_names[@]}"; do
    if [[ -n $python_dir && -x $python_dir/$interpreter_name ]]; then
      if [[ ${YB_PYTHON_WRAPPER_DEBUG:-} == "1" ]]; then
        log "Invoking Python: $python_dir/$interpreter_name $*"
      fi
      exec "$python_dir/$interpreter_name" "$@"
    fi
  done
done

export YB_PREVENT_PYTHON_WRAPPER_RECURSION=1
remove_path_entry "$YB_PYTHON_WRAPPERS_DIR"
"$interpreter_name" "$@"
