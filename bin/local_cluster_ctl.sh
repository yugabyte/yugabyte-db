#!/bin/bash

set -euo pipefail

print_help() {
  cat <<-EOT
Usage: ${0##*/} [<options>] <command>
Options (do not apply to status/stop/add/remove commands):
  --num-masters <num_masters>
    Number of master processes. 3 by default.
  --num-tservers <num_tablet_servers>
    Number of tablet server processes. 3 by default.
    We currently do not stop unneeded servers when the number of servers of any type is reduced,
    use a stop/start combination instead.

Commands:
  start  - start master & tablet server processes
  stop   - stop all master & tablet server processes
  status - display running status and process id of master & tablet server processes
  add    - add one tablet server process
  remove <daemon_index> - remove one tablet server process with given index (gotten from status)
EOT
}

declare -i -r MAX_SERVERS=20

validate_num_servers() {
  local n="$1"
  if [[ ! "$n" =~ ^[0-9]+$ ]] || [ "$n" -lt 1 ] || [ "$n" -gt "$MAX_SERVERS" ]; then
    echo "Expected a number between 1 and $MAX_SERVERS, got: '$n'" >&2
    exit 1
  fi
}

validate_daemon_type() {
  local daemon_type=$1
  if [[ ! "$daemon_type" =~ ^(master|tserver)$ ]]; then
    echo "Internal error: expected daemon type to be 'master' or 'tserver', got '$daemon_type'" >&2
    exit 1
  fi
}

validate_daemon_index() {
  local daemon_index="$1"
  if [[ ! "$daemon_index" =~ ^[0-9]+$ ]] || [ "$daemon_index" -lt 1 ] || \
      [ "$daemon_index" -gt "$MAX_SERVERS" ]; then
    echo "Internal error: invalid daemon index '$daemon_index'" >&2
    exit 1
  fi
}

create_directories() {
  if [ -z "${cluster_base_dir:-}" ]; then
    echo "Internal error: cluster_base_dir is not set" >&2
    exit 1
  fi

  local daemon_type="$1"
  validate_daemon_type "$daemon_type"

  local num_servers="$2"
  validate_num_servers "$num_servers"
  local i
  for i in $( seq 1 $num_servers ); do
    for subdir in logs wal data; do
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
    master) rpc_port_base=$master_rpc_port_base ;;
    tserver) rpc_port_base=$tserver_rpc_port_base ;;
    *)
      echo "Internal error: cannot determine base RPC port for daemon type '$daemon_type'" >&2
      exit 1
  esac
  set +e
  local daemon_pid=$(
    pgrep -f "yb-$daemon_type .* --rpc_bind_addresses 0.0.0.0:$(( $rpc_port_base + $daemon_index ))"
  )
  set -e
  echo "$daemon_pid"
}

stop_daemon() {
  local daemon_type=$1
  validate_daemon_type "$daemon_type"
  local daemon_index=$2
  validate_daemon_index "$daemon_index"
  local daemon_pid=$( find_daemon_pid "$daemon_type" "$daemon_index" )
  if [ -n "$daemon_pid" ]; then
    echo "Killing $daemon_type $daemon_index (pid $daemon_pid)"
    ( set -x; kill $daemon_pid )
  else
    echo "$daemon_type $i already stopped"
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

set_num_servers() {
  set_num_masters
  set_num_tservers
}

count_running_masters=0
# Count number of current masters.
set_num_masters() {
  if [ "$count_running_masters" -ne 0 ]; then
    echo "set_num_masters() cannot be called more than once." >&2
    exit 1
  fi
  for i in `seq 1 $MAX_SERVERS`; do
    local daemon_pid=$( find_daemon_pid "master" $i )
    if [ -n "$daemon_pid" ]; then
      let count_running_masters=count_running_masters+1
    fi
  done
  master_indexes=$( seq 1 $count_running_masters )
}

max_running_tserver_index=0
# Count number of current tablet servers.
set_num_tservers() {
  if [ "$max_running_tserver_index" -ne 0 ]; then
    echo "set_num_tservers() cannot be called more than once." >&2
    exit 1
  fi
  for i in `seq 1 $MAX_SERVERS`; do
    local daemon_pid=$( find_daemon_pid "tserver" $i )
    if [ -n "$daemon_pid" ]; then
      if [ $i -gt $max_running_tserver_index ]; then
        let max_running_tserver_index=$i
      fi
    fi
  done
  tserver_indexes=$( seq 1 $max_running_tserver_index )
}

increment_tservers() {
  let max_running_tserver_index=max_running_tserver_index+1
  tserver_indexes=$( seq 1 $max_running_tserver_index )
}

remove_daemon() {
  local daemon_type=$1
  validate_daemon_type "$daemon_type"
  local id=$2
  validate_daemon_index "$id"
  local daemon_pid=$( find_daemon_pid "$daemon_type" "$id" )
  if [ -n "$daemon_pid" ]; then
    echo "Killing $daemon_type $id (pid $daemon_pid)"
    ( set -x; kill $daemon_pid )
  else
    echo "$daemon_type $id already stopped"
    echo "$daemon_type $id already stopped"
  fi
}

set_master_addresses() {
  master_addresses=""
  for i in $master_indexes; do
    if [ -n "$master_addresses" ]; then
      master_addresses+=","
    fi
    master_addresses+="$bind_ip:$(( $master_rpc_port_base + $i ))"
  done
}

start_master() {
  master_base_dir="$cluster_base_dir/master-$1"
  if [ ! -d "$master_base_dir" ]; then
    echo "Internal error: directory '$master_base_dir' does not exist" >&2
    exit 1
  fi
  (
    echo "Starting master $1"
    set -x
    "$master_binary" \
      --fs_data_dirs "$master_base_dir/data" \
      --fs_wal_dir "$master_base_dir/wal" \
      --log_dir "$master_base_dir/logs" \
      --master_addresses "$master_addresses" \
      --webserver_port $(( $master_http_port_base + $1 )) \
      --rpc_bind_addresses 0.0.0.0:$(( $master_rpc_port_base + $1 )) &
  )
}

start_tserver() {
  tserver_base_dir="$cluster_base_dir/tserver-$1"
  if [ ! -d "$tserver_base_dir" ]; then
    echo "Internal error: directory '$tserver_base_dir' does not exist" >&2
    exit 1
  fi
  (
    echo "Starting tablet server $1"
    set -x
    "$tserver_binary" \
       --fs_data_dirs "$tserver_base_dir/data" \
       --fs_wal_dir "$tserver_base_dir/wal" \
       --log_dir "$tserver_base_dir/logs" \
       --tserver_master_addrs "$master_addresses" \
       --block_cache_capacity_mb 128 \
       --memory_limit_hard_bytes $(( 256 * 1024 * 1024)) \
       --webserver_port $(( $tserver_http_port_base + $1 )) \
       --rpc_bind_addresses 0.0.0.0:$(( $tserver_rpc_port_base + $1 )) &
  )
}

add_daemon() {
  local daemon_type=$1
  validate_daemon_type "$daemon_type"
  local id=$2
  validate_daemon_index "$id"

  for subdir in logs wal data; do
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

cmd=""
num_masters=3
num_tservers=3
rem_id=""
master_indexes=""
tserver_indexes=""

while [ $# -gt 0 ]; do
  case "$1" in
    --num-masters)
      num_masters="$2"
      shift
    ;;
    --num-tservers|--num-tablet-servers|--num_tservers)
      num_tservers="$2"
      shift
    ;;
    -h|--help)
      print_help
      exit
    ;;
    remove)
      if [ -n "$cmd" ] && [ "$cmd" != "$1" ]; then
        echo "More than one command specified: $cmd, $1" >&2
        exit 1
      fi

      cmd="$1"
      rem_id="$2"
      shift
    ;;
    start|stop|status|add)
      if [ -n "$cmd" ] && [ "$cmd" != "$1" ]; then
        echo "More than one command specified: $cmd, $1" >&2
        exit 1
      fi
      cmd="$1"
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

yugabyte_root=$( cd `dirname $0`/.. && pwd )

if [ ! -d "$yugabyte_root/src" ] || \
   [ ! -d "$yugabyte_root/build" ]; then
  echo "Could not recognize '$yugabyte_root' as a valid YugaByte source root:" \
    "subdirectories 'src' and 'build' do not exist." >&2
  exit 1
fi

# TODO: allow testing of both debug and release builds.
build_root="$yugabyte_root/build/debug"

if [ -f "$build_root" ]; then
  echo "Could not find build directory: '$build_root'" >&2
  exit 1
fi

# TODO: use separate bind ips of the form 127.x.y.z for different daemons similarly to YB's mini
# test cluster to allow simulating network partitions.
bind_ip=127.0.0.1

master_addresses=""
master_http_port_base=7000
tserver_http_port_base=9000
master_rpc_port_base=7100
tserver_rpc_port_base=8100

master_binary="$build_root/bin/yb-master"
ensure_binary_exists "$master_binary"
tserver_binary="$build_root/bin/yb-tserver"
ensure_binary_exists "$tserver_binary"

export YB_HOME="$yugabyte_root"

if [ "$cmd" == "start" ]; then
  validate_num_servers "$num_masters"
  validate_num_servers "$num_tservers"

  create_directories master $num_masters
  create_directories tserver $num_tservers

  master_indexes=$( seq 1 $num_masters )
  tserver_indexes=$( seq 1 $num_tservers )

  set_master_addresses

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
      echo "Tabled server $i is already running as PID $existing_tserver_pid"
    else
      start_tserver $i
    fi
  done
elif [ "$cmd" == "stop" ]; then
  set_num_servers
  set_master_addresses

  for i in $master_indexes; do
    stop_daemon "master" $i
  done

  for i in $tserver_indexes; do
    stop_daemon "tserver" $i
  done
elif [ "$cmd" == "status" ]; then
  set_num_servers
  set_master_addresses
  for i in $master_indexes; do
    show_daemon_status "master" $i
  done

  for i in $tserver_indexes; do
    show_daemon_status "tserver" $i
  done
elif [ "$cmd" == "add" ]; then
  set_num_servers
  increment_tservers
  validate_num_servers "$max_running_tserver_index"
  set_master_addresses
  # TODO: add multiple
  add_daemon "tserver" $max_running_tserver_index
elif [ "$cmd" == "remove" ]; then
  set_num_tservers
  set_master_addresses
  if [ "$rem_id" -gt "$max_running_tserver_index" ]; then
    echo "Internal error: invalid daemon index '$rem_id'" >&2
    exit 1
  fi
  # TODO: remove multiple
  remove_daemon "tserver" $rem_id
else
  echo "Command $cmd has not been implemented yet" >&2
  exit 1
fi
