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

patch_binary() {
  if [[ $# -ne 1 ]]; then
    echo >&2 "patch_binary expects exactly one argument, the binary name to patch"
    exit 1
  fi
  local f=$1
  (
    set -x;
    "$patchelf_path" --set-interpreter "$ld_path" "$f";
    "$patchelf_path" --set-rpath "$rpath" "$f";
  )
}

bin_dir=$( cd "${BASH_SOURCE%/*}" && pwd )
distribution_dir=$( cd "$bin_dir/.." && pwd )
lib_dir="$distribution_dir/lib"
rpath="$lib_dir/yb:$lib_dir/yb-thirdparty:$lib_dir/linuxbrew"
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
# ${...} macro variables will be substituted during packaging.
for f in ${main_elf_names_to_patch}; do
  patch_binary "$f"
done

cd "$bin_dir/../postgres/bin"
for f in ${postgres_elf_names_to_patch}; do
  patch_binary "$f"
done

find $lib_dir -name "*.so*" ! -name "ld.so" | xargs -I{} "$patchelf_path" --set-rpath "$rpath" {} \
  2> >(grep -v 'warning: working around a Linux kernel bug by creating a hole' >&2)
