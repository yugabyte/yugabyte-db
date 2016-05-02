#!/bin/bash

set -euo pipefail

print_help() {
  cat <<-EOT
Usage: ${0##*/} [<options>] <command>
Options:
  --verbose_level <level>
    Dictates amount logging on servers trace files. Default is 0, maximum is 4.
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
  add-master
    Add one master process
  add-tserver
    Add one tablet server process
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
  for i in $( $SEQ 1 $num_servers ); do
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
    master_addresses+="$bind_ip:$(( $master_rpc_port_base + $i ))"
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
  (
    echo "Starting master $master_index"
    set -x
    "$master_binary" \
      --fs_data_dirs "$master_base_dir/data" \
      --fs_wal_dir "$master_base_dir/wal" \
      --log_dir "$master_base_dir/logs" \
      --v "$verbose_level" \
      --master_addresses "$master_addresses" \
      --webserver_port $(( $master_http_port_base + $master_index )) \
      --rpc_bind_addresses 0.0.0.0:$(( $master_rpc_port_base + $master_index )) \
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
       --fs_data_dirs "$tserver_base_dir/data" \
       --fs_wal_dir "$tserver_base_dir/wal" \
       --log_dir "$tserver_base_dir/logs" \
       --v "$verbose_level" \
       --tserver_master_addrs "$master_addresses" \
       --block_cache_capacity_mb 128 \
       --memory_limit_hard_bytes $(( 256 * 1024 * 1024)) \
       --webserver_port $(( $tserver_http_port_base + $tserver_index )) \
       --rpc_bind_addresses 0.0.0.0:$(( $tserver_rpc_port_base + $tserver_index )) \
       >"$tserver_base_dir/tserver.out" \
       2>"$tserver_base_dir/tserver.err" &
  )
}

start_daemon() {
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

start_cluster() {
  validate_num_servers "$num_masters"
  validate_num_servers "$num_tservers"

  create_directories master $num_masters
  create_directories tserver $num_tservers

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



cmd=""
num_masters=3
num_tservers=3
daemon_index_to_stop=""
daemon_index_to_restart=""
master_indexes=""
tserver_indexes=""
verbose_level=0

while [ $# -gt 0 ]; do
  case "$1" in
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
    start|stop|status|add|add-master|add-tserver|destroy|restart|wipe-restart)
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

# TODO: allow testing of both debug and release builds.
build_root="$yugabyte_root/build/debug"

if [ -f "$build_root" ]; then
  echo "Could not find build directory: '$build_root'" >&2
  exit 1
fi

if [ "`uname`" == "Darwin" ]; then
  export DYLD_FALLBACK_LIBRARY_PATH="$build_root/rocksdb-build"
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

count_running_daemons
if [[ "$cmd" =~ ^[a-z]+-(master|tserver)$ ]]; then
  daemon_type=${cmd#*-}
fi

case "$cmd" in
  start) start_cluster ;;
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
  add-master|add-tserver)
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
