#!/bin/bash

set -euo pipefail
yugabyte_root=$( cd "$( dirname "$0" )"/.. && pwd )
cd "$yugabyte_root"

if [ "$( uname )" == "Darwin" ]; then
  set -x
  export DYLD_FALLBACK_LIBRARY_PATH="$yugabyte_root"/latest/rocksdb-build
  set +x
fi

set -x
build/latest/bin/yb_load_test_tool \
  --use_kv_table \
  --load_test_master_addresses localhost:7101,localhost:7102,localhost:7103 "$@"
