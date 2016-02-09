#!/bin/bash 

set -euxo pipefail

project_dir=$( cd `dirname $0` && pwd )
build_dir="$project_dir/build/debug"

mkdir -p "$build_dir"
cd "$build_dir"

cmake "$project_dir"
make
