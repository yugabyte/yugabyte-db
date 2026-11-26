#!/usr/bin/env bash

# Run the given command in the given directory and with the given PATH. This is invoked on a remote
# host using ssh during the distributed C++ build.

#
# The following only applies to changes made to this file as part of YugabyteDB development.
#
# Portions Copyright (c) YugabyteDB, Inc.
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

if [[ $YB_ENCODED_REMOTE_CMD_LINE != *"$ARG_SEPARATOR" ]]; then
  echo "Got extra args in YB_ENCODED_REMOTE_CMD_LINE, could not unpack:" \
       "'${YB_ENCODED_REMOTE_CMD_LINE##*"$ARG_SEPARATOR"}'" >&2
  exit 1
fi

# Split the arguments in one awk pass.  Removing them from the front one at a time with parameter
# expansion copies the rest of the string each time, which takes seconds for a link command with
# thousands of arguments.  Arguments cannot contain NUL, so NUL separates them on the way back.
readarray -d '' args < <(
  awk -v sep="$ARG_SEPARATOR" 'BEGIN {
    n = split(ENVIRON["YB_ENCODED_REMOTE_CMD_LINE"], parts, sep)
    for (i = 1; i < n; i++) {
      printf "%s%c", parts[i], 0
    }
  }'
)

exec "$@" "${args[@]}"
