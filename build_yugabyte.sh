#!/bin/bash 

set -euxo pipefail

build_dir=$( cd `dirname $0` && pwd )/build

mkdir -p "$build_dir"
cd "$build_dir"

cmake ..
make
