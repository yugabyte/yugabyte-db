#!/usr/bin/env bash

# shellcheck source=build-support/common-build-env.sh
. "${BASH_SOURCE%/*}/common-build-env.sh"

activate_virtualenv
set_pythonpath

#  usage:
#    for release build : ./tserver_lto.sh
#    for prof_gen build : ./tserver_lto.sh prof_gen

if [[ $# -gt 0 ]]; then
  build_type=$1
  validate_build_type "$build_type"
  shift
else
  build_type="release"
fi

if [[ $( uname -m ) == "x86_64" ]]; then
  build_root_basename="$build_type-clang16-linuxbrew-full-lto-ninja"
fi

dep_graph_cmd=(
  "$YB_SCRIPT_PATH_DEPENDENCY_GRAPH"
  "--build-root=${YB_SRC_ROOT}/build/${build_root_basename}"
  "--file-regex=^.*/yb-tserver-dynamic$"
)
dep_graph_cmd+=( link-whole-program "$@" )

set -x
"${dep_graph_cmd[@]}"
