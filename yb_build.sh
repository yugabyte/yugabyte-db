#!/bin/bash 

set -euo pipefail

show_help() {
  cat >&2 <<-EOT
Usage: ${0##*/} [<options>] [<build_type>]
Options:
  -h, --help
    Show help
Build types:
  debug (default), fastdebug, release, profile_gen, profile_build
EOT
}

cmake_opts=""
cmake_build_type="debug"

while [ $# -gt 0 ]; do
  case "$1" in
    -h|--help)
      show_help >&2
      exit 1
    ;;
    debug|fastdebug|release|profile_gen|profile_build)
      cmake_build_type="$1"
    ;;
    *)
      echo "Invalid option: '$1'" >&2
      exit 1
  esac
  shift
done

cmake_opts+=" -DCMAKE_BUILD_TYPE=$cmake_build_type"

project_dir=$( cd `dirname $0` && pwd )
build_dir="$project_dir/build/$cmake_build_type"

mkdir -p "$build_dir"
cd "$build_dir"

# Even though thirdparty/build-if-necessary.sh has its own build stamp file,
# the logic here is simplified: we only build third-party dependencies once and
# never rebuild it.

thirdparty_built_flag_file="$PWD/built_thirdparty"
makefile_builds_third_party_flag_file="$PWD/makefile_builds_third_party"

export YB_MINIMIZE_RECOMPILATION=1

if [ ! -f Makefile ] || [ ! -f "$thirdparty_built_flag_file" ]; then
  if [ -f "$thirdparty_built_flag_file" ]; then
    echo "$thirdparty_built_flag_file is present, setting NO_REBUILD_THIRDPARTY=1" \
      "before running cmake"
    export NO_REBUILD_THIRDPARTY=1
  fi
  echo "Running cmake in $PWD"
  ( set -x; cmake -DYB_LINK=dynamic $cmake_opts "$project_dir" )
fi

echo Running make in $PWD
( set -x; make -j8 )

touch "$thirdparty_built_flag_file"

