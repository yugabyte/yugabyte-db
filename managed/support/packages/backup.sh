#!/bin/bash
#
# Copyright 2019 YugaByte, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

set -euo pipefail

YUGAWARE_DUMP_FNAME="yugaware_dump.sql"

# When false, we won't stop/start platform and prometheus services when executing the script
RESTART_PROCESSES=true

set +e
DOCKER_CMD=$(command -v docker)
DOCKER_BASED=""
if [[ ! -z "$DOCKER_CMD" ]]; then
  DOCKER_BASED="$(docker ps -a | grep yugaware)"
fi
set -e

# VM based default.
PROMETHEUS_DATA_DIR="/var/lib/prometheus"
# Docker (Replicated) based default.
if [[ -z $DOCKER_BASED ]]; then
  PROMETHEUS_DATA_DIR="/opt/yugabyte/prometheusv2"
fi
PROMETHEUS_SNAPSHOT_DIR="prometheus_snapshot"

# Takes docker container and command as arguments. Executes docker cmd if docker-based or not.
docker_aware_cmd() {
  if [[ -z "$DOCKER_BASED" ]]; then
    sh -c "$2"
  else
    docker exec -i $1 $2
  fi
}

create_backup() {
  now=$(date +"%y-%m-%d-%H-%M")
  output_path="$1"
  data_dir="$2"
  exclude_prometheus="$3"
  exclude_prometheus_flag=" "
  if [[ "$exclude_prometheus" = true ]]; then
    exclude_prometheus_flag=" --exclude prometheus* "
  fi

  modify_service yb-platform stop

  tarname="${output_path}/backup_${now}.tgz"
  trap "cleanup ${data_dir}/${YUGAWARE_DUMP_FNAME}" EXIT
  echo "Creating snapshot of platform data"
  docker_aware_cmd "postgres" "pg_dump -U postgres -Fc yugaware" > \
                              "${data_dir}/${YUGAWARE_DUMP_FNAME}"

  # Backup prometheus data.
  if [[ "$exclude_prometheus" = false ]]; then
    trap "sudo rm -rf ${data_dir}/${PROMETHEUS_SNAPSHOT_DIR}" RETURN
    echo "Creating prometheus snapshot"
    set_prometheus_data_dir
    snapshot_dir=$(curl -X POST http://localhost:9090/api/v1/admin/tsdb/snapshot |
      python -c "import sys, json; print(json.load(sys.stdin)['data']['name'])")
    mkdir -p "$data_dir/$PROMETHEUS_SNAPSHOT_DIR"
    sudo cp -aR "$PROMETHEUS_DATA_DIR/snapshots/$snapshot_dir" "$data_dir/$PROMETHEUS_SNAPSHOT_DIR"
    sudo rm -rf "$PROMETHEUS_DATA_DIR/snapshots/$snapshot_dir"
  fi
  echo "Creating platform backup package"

  exclude_dirs="--exclude postgres* --exclude devops --exclude yugaware/lib \
  --exclude yugaware/logs --exclude yugaware/README.md --exclude yugaware/bin \
  --exclude yugaware/conf --exclude backup_*.tgz --exclude helm --exclude prometheusv2"
  tar $exclude_prometheus_flag ${exclude_dirs} -czf $tarname -C $data_dir .
  echo "Finished creating backup $tarname"

  modify_service yb-platform restart
  echo "Done!"
}

restore_backup() {
  input_path="$1"
  destination="$2"
  is_prometheus=false

  prometheus_dir_regex="${PROMETHEUS_SNAPSHOT_DIR}/$"
  if tar -tf $input_path | grep $prometheus_dir_regex; then
    is_prometheus=true
    set_prometheus_data_dir
  fi

  modify_service yb-platform stop

  if [[ "$is_prometheus" = true ]]; then
    modify_service prometheus stop
  fi

  yugaware_dump="${destination}/${YUGAWARE_DUMP_FNAME}"
  trap "cleanup $yugaware_dump" EXIT
  sudo tar -xzf $input_path --directory $destination
  echo "Restoring platform data to database"
  docker_aware_cmd "postgres" "pg_restore -U postgres -d yugaware -c" < "${yugaware_dump}"
  # Restore prometheus data.
  if [[ "$is_prometheus" = true ]]; then
    if [[ -z "$PROMETHEUS_DATA_DIR" ]]; then
      echo "Failed to find prometheus data directory."
      exit 1
    fi
    sudo rm -rf ${PROMETHEUS_DATA_DIR}/*
    sudo mv $destination/$PROMETHEUS_SNAPSHOT_DIR/* $PROMETHEUS_DATA_DIR
    if [[ -z "$DOCKER_BASED" ]]; then
      sudo chown -R prometheus:prometheus "$PROMETHEUS_DATA_DIR"
    else
      sudo chown -R nobody:nogroup "$PROMETHEUS_DATA_DIR"
    fi
  fi
  # Create following directory if it wasn't created yet so restore will succeed.
  mkdir -p "$destination/release"

  modify_service yb-platform restart

  if [[ "$is_prometheus" = true ]]; then
    modify_service prometheus restart
  fi

  echo "Finished restoring backup"
}

set_prometheus_data_dir() {
  PROMETHEUS_DATA_DIR=$(curl http://localhost:9090/api/v1/status/flags |
    python -c "import sys, json; print(json.load(sys.stdin)['data']['storage.tsdb.path'])")
}

modify_service() {
  if [[ -z "$DOCKER_BASED" ]] && [[ "$RESTART_PROCESSES" = true ]]; then
    set +e
    service="$1"
    operation="$2"
    echo "Performing operation $operation on service $service"
    sudo systemctl "$operation" "$service"
  fi
}

print_backup_usage() {
  echo "$0 backup --output <output_path> [--data_dir <data_dir>]"\
       "[--exclude_prometheus] [--skip_restart]"
  echo "Backup YW to a specified output location"
}

print_restore_usage() {
  echo "$0 restore --input <input_path> [--destination <desination>] [--skip_restart]"
  echo "Restore YW from a specified input location"
}

cleanup () {
  rm -f "$1"
}

if [[ $# -eq 0 ]]; then
  echo "ERROR: Please use one of the following:"
  echo ""
  print_backup_usage
  echo ""
  print_restore_usage
  exit 1
fi

command=$1
shift

case $command in
  backup)
    exclude_prometheus=false
    data_dir=/opt/yugabyte
    while (( "$#" )); do
      case "$1" in
        --output)
          output_path=$2
          shift 2
          ;;
        --exclude_prometheus)
          exclude_prometheus=true
          shift
          ;;
        --data_dir)
          data_dir=$2
          shift 2
          ;;
        --skip_restart)
          RESTART_PROCESSES=false
          shift
          ;;
        --verbose)
          set -x
          shift
          ;;
        *)
          echo "$1"
          echo "ERROR: Backup Usage"
          print_backup_usage
          exit 1
      esac
    done

    if [[ -z "$output_path" ]]; then
      echo "ERROR: Backup Usage"
      print_backup_usage
      exit 1
    fi
    create_backup "$output_path" "$data_dir" "$exclude_prometheus"
    exit 0
    ;;
  restore)
    destination=/opt/yugabyte
    while (( "$#" )); do
      case "$1" in
        --input)
          input_path=$2
          shift 2
          ;;
        --destination)
          destination=$2
          shift 2
          ;;
        --skip_restart)
          RESTART_PROCESSES=false
          shift
          ;;
        --verbose)
          set -x
          shift
          ;;
        *)
          echo "ERROR: Restore usage"
          print_restore_usage
          exit 1
      esac
    done
    if [[ -z "$input_path" ]]; then
      echo "ERROR: Restore usage"
      print_restore_usage
      exit 1
    fi
    restore_backup "$input_path" "$destination"
    exit 0
    ;;
  *)
    echo "ERROR: Command must be either 'backup' or 'restore'"
    exit 1
esac
