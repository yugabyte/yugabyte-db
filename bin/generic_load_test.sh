#!/bin/bash

set -euo pipefail

if [ "$#" -lt 1 ]; then
  echo "Need at least one argument for master addresses!"
  exit 1
fi

master_addresses=$1
shift

yugabyte_root=$( cd "$( dirname "$0" )"/.. && pwd )
cd "$yugabyte_root"

if [ "$( uname )" == "Darwin" ]; then
  set -x
  export DYLD_FALLBACK_LIBRARY_PATH="$yugabyte_root"/build/latest/rocksdb-build
  set +x
fi

set -x
build/latest/bin/yb_load_test_tool \
  --use_kv_table \
  --logtostderr \
  --load_test_master_addresses "$master_addresses" "$@"
