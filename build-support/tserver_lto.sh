#!/usr/bin/env bash

. "${BASH_SOURCE%/*}/common-build-env.sh"

activate_virtualenv
set_pythonpath

set -x
"$YB_SRC_ROOT/python/yb/dependency_graph.py" \
    --build-root "$YB_SRC_ROOT/build/release-clang12-linuxbrew-dynamic-ninja" \
    --file-regex "^.*/yb-tserver$" \
    link-whole-program \
    "$@"
