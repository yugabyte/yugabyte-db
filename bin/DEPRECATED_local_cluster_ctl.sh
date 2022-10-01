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

print_help() {
  cat <<-EOT
Usage: ${0##*/} [<options>] <command>
Options:
  --verbose_level <level>
    Dictates amount logging on servers trace files. Default is 0, maximum is 4.
  --placement_cloud <cloud_name>
    Which cloud this instance is started in.
  --placement_region <region_name>
    Which region this instance is started in.
  --placement_zone <zone_name>
    Which zone this instance is started in.
  --yb_num_shards_per_tserver <num_shards>
    If specified, passed to tserver process. Otherwise tserver will use its own default.
  --tserver_db_block_cache_size_bytes <size in bytes>
    If specified, passed to tserver process. Otherwise tserver will use its own default.
  --replication_factor <num_replicas>
    If specified, passed to master process. Otherwise master will use its own default.
  --use_cassandra_authentication
    If specified, passed to tserver process as true.
Commands:
  start
    Start master & tablet server processes.
     --num-masters <num_masters>
       Number of master processes to start. 3 by default.
     --num-tservers <num_tablet_servers>
       Number of tablet server processes to start. 3 by default.
  stop
    Stop all master & tablet server processes.
  restart
    Stop the cluster and start it again (keep the data).
  status
    Display running status and process id of master & tablet server processes.
  destroy
    Stop all master & tablet server processes as well as remove any associated data.
  wipe-restart
    Stop the cluster, wipe all the data files, and start the cluster.
  start-master
    Start a new master process in shell mode (not part of cluster).
  add-tserver
    Start a new tablet server process and add it to the cluster.
  stop-master <master_index>
    Stop the master process with the given index. The index can be obtained from the "status"
    command.
  stop-tserver <tserver_index>
    Stop the tablet server process with the given index.
  restart-master <master_index>
    Restart the master server with the given index.
  restart-tserver <tserver_index>
    Restart the tablet server with the given index.
EOT
}

declare -i -r MAX_SERVERS=20

SEQ=seq
if [ "`uname`" == "Darwin" ]; then
  # The default seq command on Mac OS X has a different behavior if first > last.
  # On, Linux for example, seq 1 0 prints nothing, whereas on Mac OS X, seq prints
  # 1 and 0. We don't want the reverse behavior. So resort to using the GNU version
  # of seq.
  SEQ=gseq
fi

validate_num_servers() {
  local n="$1"
  if [[ ! "$n" =~ ^[0-9]+$ ]] || [ "$n" -lt 1 ] || [ "$n" -gt "$MAX_SERVERS" ]; then
    echo "Expected number of servers between 1 and $MAX_SERVERS, got: '$n'" >&2
    exit 1
  fi
}

validate_daemon_type() {
  local daemon_type="$1"
  if [[ ! "$daemon_type" =~ ^(master|tserver)$ ]]; then
    echo "Internal error: expected daemon type to be 'master' or 'tserver', got '$daemon_type'" >&2
    exit 1
  fi
}

validate_daemon_index() {
  local daemon_index="$1"
  if [[ ! "$daemon_index" =~ ^[0-9]+$ ]] || \
     [ "$daemon_index" -lt 1 ] || \
     [ "$daemon_index" -gt "$MAX_SERVERS" ]; then
    echo "Internal error: invalid daemon index '$daemon_index'" >&2
    exit 1
  fi
}

validate_running_daemon_index() {
  local daemon_type="$1"
  validate_daemon_type "$daemon_type"
  local daemon_index="$2"
  validate_daemon_index "$daemon_index"
  case "$daemon_type" in
    master)
      if [ "$daemon_index" -gt "$max_running_master_index" ]; then
        echo "Invalid master index: $daemon_index, max running master index is" \
             "$max_running_master_index" >&2
        exit 1
      fi
    ;;
    tserver)
      if [ "$daemon_index" -gt "$max_running_tserver_index" ]; then
        echo "Invalid tablet server index: $daemon_index, max running tablet server index is" \
             "$max_running_tserver_index" >&2
        exit 1
      fi
    ;;
  esac
}

create_directories_if_needed() {
  if [ -z "${cluster_base_dir:-}" ]; then
    echo "Internal error: cluster_base_dir is not set" >&2
    exit 1
  fi

  local daemon_type="$1"
  validate_daemon_type "$daemon_type"

  local num_servers="$2"
  validate_num_servers "$num_servers"
  local i
  for i in $( $SEQ 1 $num_servers ); do
    for subdir in logs disk1 disk2; do
      mkdir -p "$cluster_base_dir/$daemon_type-$i/$subdir"
    done
  done
}

ensure_binary_exists() {
  local binary_path="$1"
  if [ ! -f "$binary_path" ]; then
    echo "File '$binary_path' does not exist" >&2
    exit 1
  fi
  if [ ! -x "$binary_path" ]; then
    echo "File '$binary_path' is not executable" >&2
    exit 1
  fi
}

find_daemon_pid() {
  local daemon_type="$1"
  validate_daemon_type "$daemon_type"
  local daemon_index="$2"
  validate_daemon_index "$daemon_index"
  local rpc_port_base
  case "$daemon_type" in
    master) rpc_port_base=$master_rpc_port ;;
    tserver) rpc_port_base=$tserver_rpc_port ;;
    *)
      echo "Internal error: cannot determine base RPC port for daemon type '$daemon_type'" >&2
      exit 1
  esac
  set +e
  local daemon_pid=$(
    pgrep -f "yb-$daemon_type .* --rpc_bind_addresses 127.0.0.$daemon_index:$(( $rpc_port_base ))"
  )
  set -e
  echo "$daemon_pid"
}

wait_for_stop() {
  local daemon_pid=$1
  echo "Waiting for $daemon_pid to stop..."
  while $(kill -0 "$daemon_pid" 2>/dev/null); do
    sleep 0.1
  done
  echo "Daemon $daemon_pid stopped."
}

stop_daemon() {
  local daemon_type=$1
  validate_daemon_type "$daemon_type"
  local daemon_index=$2
  validate_daemon_index "$daemon_index"
  local daemon_pid=$( find_daemon_pid "$daemon_type" "$daemon_index" )
  if [ -n "$daemon_pid" ]; then
    echo "Killing $daemon_type $daemon_index (pid $daemon_pid)"
    ( set -x; kill $daemon_pid; wait_for_stop $daemon_pid )
  else
    echo "$daemon_type $daemon_index already stopped"
  fi
}

show_daemon_status() {
  local daemon_type=$1
  validate_daemon_type "$daemon_type"
  local daemon_index=$2
  validate_daemon_index "$i"
  local daemon_pid=$( find_daemon_pid "$daemon_type" "$daemon_index" )
  if [ -n "$daemon_pid" ]; then
    echo "$daemon_type $daemon_index is running as pid $daemon_pid"
  else
    echo "$daemon_type $daemon_index is not running"
  fi
}

count_running_daemons() {
  local daemon_type
  for daemon_type in master tserver; do
    local max_running_daemon_index=0
    for i in $( $SEQ 1 $MAX_SERVERS ); do
      local daemon_pid=$( find_daemon_pid "$daemon_type" $i )
      if [ -n "$daemon_pid" ]; then
        if [ $i -gt $max_running_daemon_index ]; then
          let max_running_daemon_index=$i
        fi
      fi
    done

    local daemon_indexes=$( $SEQ 1 $max_running_daemon_index )
    case "$daemon_type" in
      master)
        max_running_master_index=$max_running_daemon_index
        master_indexes=$daemon_indexes
      ;;
      tserver)
        max_running_tserver_index=$max_running_daemon_index
        tserver_indexes=$daemon_indexes
      ;;
    esac
  done
}

increment_tservers() {
  let max_running_tserver_index=max_running_tserver_index+1
  new_daemon_index=$max_running_tserver_index
  tserver_indexes=$( $SEQ 1 $max_running_tserver_index )
}

increment_masters() {
  let max_running_master_index=max_running_master_index+1
  new_daemon_index=$max_running_master_index
  master_indexes=$( $SEQ 1 $max_running_master_index )
}

set_master_addresses() {
  master_addresses=""
  for i in $master_indexes; do
    if [ -n "$master_addresses" ]; then
      master_addresses+=","
    fi
    master_addresses+="127.0.0.$i:$(( $master_rpc_port ))"
  done
}

start_master() {
  local  master_index=$1
  validate_daemon_index "$master_index"
  master_base_dir="$cluster_base_dir/master-$master_index"
  if [ ! -d "$master_base_dir" ]; then
    echo "Internal error: directory '$master_base_dir' does not exist" >&2
    exit 1
  fi
  local master_flags=""
  if [ "$shell_master" = false ]; then
    master_flags="--master_addresses $master_addresses"
  fi
  (
    echo "Starting master $master_index"
    set -x
    "$master_binary" \
      $common_params \
      --fs_data_dirs "$master_base_dir/disk1,$master_base_dir/disk2" \
      --log_dir "$master_base_dir/logs" \
      --v "$verbose_level" \
      $master_flags \
      --webserver_interface 127.0.0.$master_index \
      --webserver_port $(( $master_http_port_base + $master_index )) \
      --rpc_bind_addresses 127.0.0.$master_index:$(( $master_rpc_port )) \
      --placement_cloud "$placement_cloud" \
      --placement_region "$placement_region" \
      --placement_zone "$placement_zone" \
      $master_optional_params \
      >"$master_base_dir/master.out" \
      2>"$master_base_dir/master.err" &
  )
}

start_tserver() {
  local tserver_index=$1
  validate_daemon_index "$tserver_index"
  tserver_base_dir="$cluster_base_dir/tserver-$tserver_index"
  if [ ! -d "$tserver_base_dir" ]; then
    echo "Internal error: directory '$tserver_base_dir' does not exist" >&2
    exit 1
  fi
  (
    echo "Starting tablet server $tserver_index"
    set -x
    "$tserver_binary" \
       $common_params \
       --fs_data_dirs "$tserver_base_dir/disk1,$tserver_base_dir/disk2" \
       --log_dir "$tserver_base_dir/logs" \
       --v "$verbose_level" \
       --tserver_master_addrs "$master_addresses" \
       --memory_limit_hard_bytes $(( 1024 * 1024 * 1024)) \
       --webserver_interface 127.0.0.$tserver_index \
       --webserver_port $(( $tserver_http_port_base + $tserver_index )) \
       --rpc_bind_addresses 127.0.0.$tserver_index:$(( $tserver_rpc_port )) \
       --redis_proxy_webserver_port $(( $redis_http_port_base + $tserver_index )) \
       --redis_proxy_bind_address 127.0.0.$tserver_index:$(( $redis_rpc_port )) \
       --cql_proxy_webserver_port $(( $cql_http_port_base + $tserver_index )) \
       --cql_proxy_bind_address 127.0.0.$tserver_index:$(( $cql_rpc_port )) \
       --pgsql_proxy_webserver_port $(( $pgsql_http_port_base + $tserver_index )) \
       --pgsql_proxy_bind_address 127.0.0.$tserver_index:$(( $pgsql_rpc_port )) \
       --local_ip_for_outbound_sockets 127.0.0.$tserver_index \
      --placement_cloud "$placement_cloud" \
      --placement_region "$placement_region" \
      --placement_zone "$placement_zone" \
      --emulate_redis_responses=true \
      --vmodule="redis_rpc=4" \
      --tserver_yb_client_default_timeout_ms 20000 \
      $tserver_optional_params \
       >"$tserver_base_dir/tserver.out" \
       2>"$tserver_base_dir/tserver.err" &
  )
}

start_daemon() {
  local daemon_type=$1
  validate_daemon_type "$daemon_type"
  local id=$2
  validate_daemon_index "$id"

  for subdir in logs disk1 disk2; do
    mkdir -p "$cluster_base_dir/$daemon_type-$id/$subdir"
  done

  case "$daemon_type" in
    master)
      start_master $id
      ;;
    tserver)
      start_tserver $id
      ;;
  esac
}

start_cluster() {
  create_directories_if_needed master $num_masters
  create_directories_if_needed tserver $num_tservers

  validate_num_servers "$num_masters"
  validate_num_servers "$num_tservers"

  master_indexes=$( $SEQ 1 $num_masters )
  tserver_indexes=$( $SEQ 1 $num_tservers )

  set_master_addresses

  local i
  for i in $master_indexes; do
    existing_master_pid=$( find_daemon_pid master $i )
    if [ -n "$existing_master_pid" ]; then
      echo "Master $i is already running as PID $existing_master_pid"
    else
      start_master $i
    fi
  done

  for i in $tserver_indexes; do
    existing_tserver_pid=$( find_daemon_pid tserver $i )
    if [ -n "$existing_tserver_pid" ]; then
      echo "Tablet server $i is already running as PID $existing_tserver_pid"
    else
      start_tserver $i
    fi
  done
}

start_cluster_as_before() {
  if [ "$max_running_master_index" -gt 0 ]; then
    num_masters=$max_running_master_index
  fi
  if [ "$max_running_tserver_index" -gt 0 ]; then
    num_tservers=$max_running_tserver_index
  fi
  start_cluster
}

stop_cluster() {
  set_master_addresses

  local i
  for i in $master_indexes; do
    stop_daemon "master" $i
  done

  for i in $tserver_indexes; do
    stop_daemon "tserver" $i
  done
}

destroy_cluster() {
  stop_cluster
  echo
  echo "Removing directory '$cluster_base_dir'"
  ( set -x; rm -rf "$cluster_base_dir" )
  echo
}

show_cluster_status() {
  set_master_addresses
  local i
  for i in $master_indexes; do
    show_daemon_status "master" $i
  done

  for i in $tserver_indexes; do
    show_daemon_status "tserver" $i
  done
}

set_cmd() {
  if [ -n "$cmd" ] && [ "$cmd" != "$1" ]; then
    echo "More than one command specified: $cmd, $1" >&2
    exit 1
  fi
  cmd="$1"
}

output_separator() {
  echo
  echo "------------------------------------------------------------------------------------------"
  echo
}

# Print out deprecation warning on every single command, to notify people aggresively to shift away
# scripts!
echo -en "\033[0;31m"
cat <<EOF
****************************************************************************************************
WARNING! This script is now deprecated! Please move your workflows to using 'bin/yb-ctl' instead!

Important notable changes:
- make sure you destroy old clusters first, before migrating to creating new ones with yb-ctl
- you will no longer rev up port numbers (7001-7003), but instead rev up IPs: 127.0.0.(1-3)
- you no longer need new diffs for custom flags, just use --tserver_flags/--master_flags
- yb-ctl is the same script the community will use for local testing

We will aim to maintain https://docs.yugabyte.com/admin/yb-ctl/ as public facing documentation!
****************************************************************************************************
EOF
echo -e "\033[0m"

cmd=""
num_masters=3
num_tservers=3
daemon_index_to_stop=""
daemon_index_to_restart=""
master_indexes=""
tserver_indexes=""
verbose_level=0
placement_cloud="cloud1"
placement_region="datacenter1"
placement_zone="rack1"
yb_num_shards_per_tserver=""
tserver_db_block_cache_size_bytes=""
replication_factor=""
use_cassandra_authentication=false
shell_master=false

while [ $# -gt 0 ]; do
  case "$1" in
    --placement_cloud|--placement_region|--placement_zone|--yb_num_shards_per_tserver|\
    --tserver_db_block_cache_size_bytes|--replication_factor)
      eval "${1#--}=$2"
      shift
    ;;
    --verbose_level)
      verbose_level="$2"
      shift
    ;;
    --num-masters)
      num_masters="$2"
      shift
    ;;
    --num-tservers|--num-tablet-servers|--num_tservers)
      num_tservers="$2"
      shift
    ;;
    --use_cassandra_authentication)
      use_cassandra_authentication=true
    ;;
    -h|--help)
      print_help
      exit
    ;;
    stop-master|stop-tserver)
      set_cmd "$1"
      daemon_index_to_stop="$2"
      shift
    ;;
    restart-master|restart-tserver)
      set_cmd "$1"
      daemon_index_to_restart="$2"
      shift
    ;;
    start-master)
      set_cmd "$1"
      shell_master=true
    ;;
    start|stop|status|add|add-tserver|destroy|restart|wipe-restart)
      set_cmd "$1"
    ;;
    *)
      echo "Invalid command line argument: $1"
      exit
  esac
  shift
done

if [ -z "$cmd" ]; then
  print_help >&2
  echo >&2
  echo "Command not specified" >&2
  exit 1
fi

cluster_base_dir=/tmp/yugabyte-local-cluster

yugabyte_root=$( cd "$( dirname "$0" )"/.. && pwd )

if [ ! -d "$yugabyte_root/src" ] || \
   [ ! -d "$yugabyte_root/build" ]; then
  echo "Could not recognize '$yugabyte_root' as a valid YugaByte source root:" \
    "subdirectories 'src' and 'build' do not exist." >&2
  exit 1
fi

build_root="$yugabyte_root/build/latest"

if [ -f "$build_root" ]; then
  echo "Could not find build directory: '$build_root'" >&2
  exit 1
fi

master_addresses=""
master_http_port_base=7000
tserver_http_port_base=9000
redis_http_port_base=11000
cql_http_port_base=12000
pgsql_http_port_base=13000
master_rpc_port=7100
tserver_rpc_port=8100
redis_rpc_port=6379
# By default cqlsh contact the server via this port base although it's configurable in cqlsh.
cql_rpc_port=9042
pgsql_rpc_port=5433

common_params=" --version_file_json_path $build_root --callhome_enabled=false"

master_optional_params=" ${YB_LOCAL_MASTER_EXTRA_PARAMS:-}"
if [[ -n "$replication_factor" ]]; then
  master_optional_params+=" --replication_factor $replication_factor"
fi

tserver_optional_params=" ${YB_LOCAL_TSERVER_EXTRA_PARAMS:-}"
if [[ -n "$yb_num_shards_per_tserver" ]]; then
  tserver_optional_params+=" --yb_num_shards_per_tserver $yb_num_shards_per_tserver"
fi
if [[ -n "$tserver_db_block_cache_size_bytes" ]]; then
  tserver_optional_params+=" --db_block_cache_size_bytes $tserver_db_block_cache_size_bytes"
fi
tserver_optional_params+=" --use_cassandra_authentication=$use_cassandra_authentication"

master_binary="$build_root/bin/yb-master"
ensure_binary_exists "$master_binary"
tserver_binary="$build_root/bin/yb-tserver"
ensure_binary_exists "$tserver_binary"

yql_root="$yugabyte_root/yql"

export YB_HOME="$yugabyte_root"

count_running_daemons
if [[ "$cmd" =~ ^[a-z]+-(master|tserver)$ ]]; then
  daemon_type=${cmd#*-}
fi

case "$cmd" in
  start)
    start_cluster
  ;;
  stop) stop_cluster ;;
  restart)
    stop_cluster
    output_separator
    start_cluster_as_before
  ;;
  status) show_cluster_status ;;
  destroy) destroy_cluster ;;
  wipe-restart)
    stop_cluster
    output_separator
    destroy_cluster
    output_separator
    start_cluster_as_before
  ;;
  start-master|add-tserver)
    increment_${daemon_type}s
    validate_num_servers "$max_running_master_index"
    validate_num_servers "$max_running_tserver_index"
    set_master_addresses
    start_daemon "${daemon_type}" "$new_daemon_index"
  ;;
  stop-master|stop-tserver)
    set_master_addresses
    validate_running_daemon_index "$daemon_type" "$daemon_index_to_stop"
    stop_daemon "$daemon_type" "$daemon_index_to_stop"
  ;;
  restart-master|restart-tserver)
    validate_daemon_index "$daemon_index_to_restart"
    set_master_addresses
    stop_daemon "$daemon_type" "$daemon_index_to_restart"
    output_separator
    start_daemon "$daemon_type" "$daemon_index_to_restart"
  ;;
  *)
    echo "Command $cmd has not been implemented yet" >&2
    exit 1
esac
