#!/bin/bash

# Copyright (c) YugabyteDB, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
# in compliance with the License.  You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software distributed under the License
# is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
# or implied.  See the License for the specific language governing permissions and limitations under
# the License.

set -euo pipefail

ALLOWED_LIBRARIES="
ld-linux-aarch64
ld-linux-x86-64
libc
libdl
libm
libpthread
libresolv
librt
linux-vdso
"

for binary in "$@"; do
  # Get only lines that appear in second argument.
  #
  # ldd output looks like:
  #   <TAB>libpthread.so.0 => /lib/x86_64-linux-gnu/libpthread.so.0 (0x00007c84c602f000)
  #   <TAB>/lib64/ld-linux-x86-64.so.2 (0x00007c84c6057000)
  # ...
  # so we just strip this down to the filename in the first part, without extension:
  #   libpthread
  #   ld-linux-86-64
  # to match $ALLOWED_LIBRARIES.
  readarray -t disallowed < <( \
      comm -13  \
          <(echo "$ALLOWED_LIBRARIES" | sort) \
          <(ldd "$binary" | cut -d $'\t' -f 2 | cut -d ' ' -f 1 | xargs -n 1 basename | \
            cut -d '.' -f 1 | sort))
  if [[ "${#disallowed[@]}" -gt 0 ]]; then
    echo >&2 "Found ${#disallowed[@]} disallowed libraries for $binary: ${disallowed[*]}"
    exit 1
  fi
done
