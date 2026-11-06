#!/usr/bin/env bash
# Copyright (c) YugabyteDB, Inc.
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

# Measures a fresh single master under master-tserver-sim and prints the JSON report.
#
#   run_baseline.sh <build dir> [master binary] [-- extra master-tserver-sim flags]
#
# The master binary defaults to yb-master in the build. It runs with the load balancer off (the
# simulator does not move replicas) and RF 3, on MASTER_IP (default 127.0.0.81). The fake tservers
# start at 127.1.0.1.

set -euo pipefail

build_dir=${1:?usage: run_baseline.sh <build dir> [master binary] [-- simulator flags]}
shift
master_bin=$build_dir/bin/yb-master
if [[ $# -gt 0 && $1 != "--" ]]; then
  master_bin=$1
  shift
fi
if [[ $# -gt 0 && $1 == "--" ]]; then
  shift
fi

master_ip=${MASTER_IP:-127.0.0.81}
data_dir=$(mktemp -d -t tserver-sim-master.XXXXXX)
master_pid=""
cleanup() {
  if [[ -n $master_pid ]]; then
    kill "$master_pid" 2>/dev/null || true
    wait "$master_pid" 2>/dev/null || true
  fi
  rm -rf "$data_dir"
}
trap cleanup EXIT

"$master_bin" \
  --fs_data_dirs="$data_dir" \
  --master_addresses="$master_ip:7100" \
  --rpc_bind_addresses="$master_ip:7100" \
  --webserver_interface="$master_ip" \
  --replication_factor=3 \
  --enable_load_balancing=false \
  --enable_ysql=false \
  --log_dir="$data_dir" \
  >"$data_dir/master.out" 2>&1 &
master_pid=$!

# The master is ready once it reports itself as leader.
for _ in $(seq 1 120); do
  if curl -sf "http://$master_ip:7000/api/v1/is-leader" >/dev/null; then
    break
  fi
  sleep 0.5
done

"$build_dir/bin/master-tserver-sim" \
  --tserver_sim_master_addresses="$master_ip:7100" \
  --log_dir="$data_dir" \
  "$@"
