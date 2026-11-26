#!/usr/bin/env bash

# Run the given command in the given directory and with the given PATH. This is invoked on a remote
# host using ssh during the distributed C++ build. stdin holds the number of remaining arguments and
# then the arguments, each followed by NUL.

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

if [[ -t 0 ]]; then
  echo "stdin is a terminal, but remote_cmd.sh reads its arguments from stdin" >&2
  exit 1
fi

# The count comes first, so that a stream that ends early fails here instead of running the command
# with only some of its arguments.  read returns an argument only once its NUL arrives, so a stream
# cut inside the last argument also comes up short.  No argument can contain NUL, because each one
# becomes a C string in the exec below.
IFS= read -r -d '' expected_count || expected_count=
args=()
while IFS= read -r -d '' arg; do
  args+=( "$arg" )
done
if [[ ${#args[@]} != "$expected_count" ]]; then
  echo "Expected ${expected_count:-an argument count and that many} arguments on stdin, got" \
       "${#args[@]} complete ones" >&2
  exit 1
fi

exec "$@" "${args[@]}"
