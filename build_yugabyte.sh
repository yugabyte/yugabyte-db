#!/bin/bash 

set -euxo pipefail

project_dir=$( cd `dirname $0` && pwd )
build_dir="$project_dir/build/debug"

mkdir -p "$build_dir"
cd "$build_dir"

# Even though thirdparty/build-if-necessary.sh has its own build stamp file,
# the logic here is simplified: we only build third-party dependencies once and
# never rebuild it.

thirdparty_built_flag_file="$PWD/built_thirdparty"

if [ -f "$thirdparty_built_flag_file" ]; then
  export NO_REBUILT_THIRDPARTY=1
  cmake "${cmake_args[@]}"
fi

cmake -DKUDU_LINK=dynamic "$project_dir"

touch "$thirdparty_built_flag_file"

