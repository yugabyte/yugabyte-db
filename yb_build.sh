#!/bin/bash 

set -euo pipefail

project_dir=$( cd `dirname $0` && pwd )
build_dir="$project_dir/build/debug"

mkdir -p "$build_dir"
cd "$build_dir"

# Even though thirdparty/build-if-necessary.sh has its own build stamp file,
# the logic here is simplified: we only build third-party dependencies once and
# never rebuild it.

thirdparty_built_flag_file="$PWD/built_thirdparty"
makefile_builds_third_party_flag_file="$PWD/makefile_builds_third_party"

export YB_MINIMIZE_VERSION_DEFINES_CHANGES=1

if [ ! -f Makefile ] || [ ! -f "$thirdparty_built_flag_file" ]; then
  if [ -f "$thirdparty_built_flag_file" ]; then
    echo "$thirdparty_built_flag_file is present, setting NO_REBUILD_THIRDPARTY=1" \
      "before running cmake"
    export NO_REBUILD_THIRDPARTY=1
  fi
  echo "Running cmake in $PWD"
  ( set -x; cmake -DYB_LINK=dynamic "$project_dir" )
fi

echo Running make in $PWD
( set -x; make -j8 )

touch "$thirdparty_built_flag_file"

