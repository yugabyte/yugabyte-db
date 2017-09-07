#!/usr/bin/env bash

# Post-installation script. Set dynamic linker path on executables in the "bin" directory. This
# script is expected to be installed into the "bin" directory of the YugaByte distribution.

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
set -euo pipefail

ELF_FILE_PATTERN='(^|[[:space:]])ELF([[:space:]]|$)'
bin_dir=$( cd "${BASH_SOURCE%/*}" && pwd )
distribution_dir=$( cd "$bin_dir/.." && pwd )
patchelf_path=$bin_dir/patchelf

if [[ ! -x $patchelf_path ]]; then
  echo >&2 "patchelf not found or is not executable: '$patchelf_path'"
  exit 1
fi

ld_path=$distribution_dir/lib/ld.so

if [[ ! -x $ld_path ]]; then
  echo >&2 "Dynamic linker not found or is not executable: '$ld_path'"
  exit 1
fi

cd "$bin_dir"
for f in *; do
  if [[ -x $f && $(file -b $f) =~ $ELF_FILE_PATTERN && $f != patchelf ]]; then
    ( set -x; "$patchelf_path" --set-interpreter "$ld_path" "$f" )
  fi
done
