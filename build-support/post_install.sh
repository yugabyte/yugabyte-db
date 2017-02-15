#!/usr/bin/env bash

# Post-installation script. Set dynamic linker path on executables in the "bin" directory. This
# script is expected to be installed into the "bin" directory of the YugaByte distribution.

set -euo pipefail

bin_dir=${BASH_SOURCE%/*}
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

for f in *; do
  if [[ -x $f && \
        $f != *.sh && \
        $f != patchelf && \
        $f != pprof ]]; then
    ( set -x; "$patchelf_path" --set-interpreter "$ld_path" "$f" )
  fi
done
