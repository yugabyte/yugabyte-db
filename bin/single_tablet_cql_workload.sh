#!/usr/bin/env bash

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

readonly load_tester_cmd_prefix='java -jar java/yb-loadtester/target/yb-sample-apps.jar'

set -x

cd "${BASH_SOURCE%/*}"/..

bin/local_cluster_ctl.sh stop
bin/local_cluster_ctl.sh destroy
bin/local_cluster_ctl.sh \
  --default_num_replicas 1 \
  --num-masters 1 \
  --num-tservers 1 \
  --yb_num_shards_per_tserver 1 \
  create

sleep 5

set +e
# OK if we don't find a previous load tester.
pkill -f -9 "$load_tester_cmd_prefix"
set -e

$load_tester_cmd_prefix \
  --num_unique_keys 1000000000 --num_writes 50000000 --num_reads 0 --num_threads_read 0 \
  --num_threads_write 20 --workload CassandraKeyValue --nodes 127.0.0.1:9042
