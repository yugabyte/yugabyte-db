#!/usr/bin/env bash

# Run the given command in the given directory and with the given PATH. This is invoked on a remote
# host using ssh during the distributed C++ build.

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
work_dir=$1

if [[ $# -lt 3 ]]; then
  echo "Too few arguments to remote_cmd.sh: expected at least <work_dir> <PATH> <executable>" >&2
  exit 1
fi

if ! cd "$work_dir"; then
  echo "Failed to set current directory to '$work_dir'" >&2
  exit 1
fi

export PATH=$2
shift 2

readonly ARG_SEPARATOR=$'=:\t:='

args=()
if [[ -z ${YB_ENCODED_REMOTE_CMD_LINE:-} ]]; then
  echo "YB_ENCODED_REMOTE_CMD_LINE is not set" >&2
  exit 1
fi

while [[ $YB_ENCODED_REMOTE_CMD_LINE == *$ARG_SEPARATOR ]]; do
  args+=( "${YB_ENCODED_REMOTE_CMD_LINE%%"$ARG_SEPARATOR"*}" )
  if [[ ${#args[@]} -gt 1000 ]]; then
    echo "Too many args encoded in YB_ENCODED_REMOTE_CMD_LINE (${#args[@]}): ${args[*]}" >&2
    exit 1
  fi
  YB_ENCODED_REMOTE_CMD_LINE=${YB_ENCODED_REMOTE_CMD_LINE#*"$ARG_SEPARATOR"}
done

if [[ -n $YB_ENCODED_REMOTE_CMD_LINE ]]; then
  echo "Got extra args in YB_ENCODED_REMOTE_CMD_LINE, could not unpack:" \
       "'${YB_ENCODED_REMOTE_CMD_LINE}'" >&2
  exit 1
fi

exec "$@" "${args[@]}"
