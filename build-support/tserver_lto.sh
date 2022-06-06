#!/usr/bin/env bash

. "${BASH_SOURCE%/*}/common-build-env.sh"

activate_virtualenv
set_pythonpath

if [[ $( uname -m ) == "x86_64" ]]; then
  build_root_basename=release-clang12-linuxbrew-full-lto-ninja
else
  build_root_basename=release-clang12-full-lto-ninja
fi

set -x
"$YB_SRC_ROOT/python/yb/dependency_graph.py" \
    --build-root "$YB_SRC_ROOT/build/${build_root_basename}" \
    --file-regex "^.*/yb-tserver$" \
    link-whole-program \
    "$@"
