#!/bin/bash

set -euxo pipefail
cd `dirname $0`/..
export DYLD_FALLBACK_LIBRARY_PATH=$HOME/code/yugabyte/build/latest/rocksdb-build
build/latest/bin/yb_load_test_tool \
  --load_test_master_addresses localhost:7101,localhost:7102,localhost:7103 "$@"
