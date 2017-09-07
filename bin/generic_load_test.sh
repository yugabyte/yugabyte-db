#!/bin/bash

#
# Copyright (c) YugaByte, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
# in compliance with the License.  You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software distributed under the License
# is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
# or implied.  See the License for the specific language governing permissions and limitations
# under the License.
#
set -euo pipefail

if [ "$#" -lt 1 ]; then
  echo "Need at least one argument for master addresses!"
  exit 1
fi

master_addresses=$1
shift

yugabyte_root=$( cd "$( dirname "$0" )"/.. && pwd )
cd "$yugabyte_root"

set -x
build/latest/bin/yb_load_test_tool \
  --use_kv_table \
  --logtostderr \
  --load_test_master_addresses "$master_addresses" "$@"
