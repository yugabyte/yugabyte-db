#!/usr/bin/env bash

# Run the given command in the given directory and with the given PATH. This is invoked on a remote
# host using ssh during the distributed C++ build.

cd "$1"
export PATH=$2
shift 2
exec "$@"
