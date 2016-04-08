#!/bin/bash

set -euxo pipefail
cd `dirname $0`/..
build/latest/bin/yb_load_test_tool \
  --load_test_master_addresses localhost:7101,localhost:7102,localhost:7103 "$@"
