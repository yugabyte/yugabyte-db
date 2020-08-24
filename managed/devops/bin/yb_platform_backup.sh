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

DOCKER_BASED="$(docker ps -a | grep yugaware)"
# VM based default.
PROMETHEUS_DATA_DIR="/var/lib/prometheus"
# Docker (Replicated) based default.
if [[ -z $DOCKER_BASED ]]; then
  PROMETHEUS_DATA_DIR="/opt/yugabyte/prometheusv2"
fi

# Takes docker container and command as arguments. Executes docker cmd if docker-based or not.
docker_aware_cmd() {
  if [[ -z "$DOCKER_BASED" ]]; then
    docker exec -it $0 $1
  else
    sh -c "$1"
  fi
}

create_backup() {
  now=$(date +"%y-%m-%d-%H-%M")
  output_path=$1
  data_dir=$2
  exclude_prometheus_flag=" "
  if [[ "$3" = true ]]; then
    exclude_prometheus_flag=" --exclude prometheus* "
  fi
  tarname="${output_path}/backup_${now}.tgz"
  trap "cleanup ${data_dir}/${YUGAWARE_DUMP_FNAME}" EXIT
  docker_aware_cmd "postgres" "pg_dump -U postgres -Fc yugaware > \
                                 ${data_dir}/${YUGAWARE_DUMP_FNAME}"
  # Backup prometheus data.
  if [[ "$3" = false ]]; then
    snapshot_dir=$(curl -X POST http://localhost:9090/api/v1/admin/tsdb/snapshot |
      python -c "import sys, json; print(json.load(sys.stdin)['data']['name'])")
    sudo cp -aR "$PROMETHEUS_DATA_DIR/snapshots/$snapshot_dir" "$data_dir/prometheus_snapshot"
  fi
  tar $exclude_prometheus_flag --exclude "postgresql" -czf $tarname -C $data_dir .
  echo "Finished creating backup $tarname"
}

restore_backup() {
  input_path=$1
  destination=$2
  yugaware_dump="${destination}/${YUGAWARE_DUMP_FNAME}"
  trap "cleanup $yugaware_dump" EXIT
  tar -xzf $input_path --directory $destination
  docker_aware_cmd "postgres" "pg_restore -U postgres -d yugaware -c < ${yugaware_dump}"
  # Restore prometheus data.
  if [[ -d "$destination/prometheus_snapshot" ]]; then
    set_prometheus_data_dir
    if [[ -z "$PROMETHEUS_DATA_DIR" ]]; then
      echo "Failed to find prometheud data directory."
      exit 1
    fi
    sudo rm -rf "${PROMETHEUS_DATA_DIR}/*"
    sudo mv "$destination/prometheus_snapshot/*" $PROMETHEUS_DATA_DIR
    if [[ -z "$DOCKER_BASED" ]]; then
      sudo chown -R nobody:nogroup "$PROMETHEUS_DATA_DIR"
    else
      sudo chown -R prometheus:prometheus "$PROMETHEUS_DATA_DIR"
    fi
  fi
  # Create following directory if it wasn't created yet so restore will succeed.
  mkdir "$destination/release"
  echo "Finished restoring backup"
}

set_prometheus_data_dir() {
  PROMETHEUS_DATA_DIR=$(curl http://localhost:9090/api/v1/status/flags |
    python -c "import sys, json; print(json.load(sys.stdin)['data']['storage.tsdb.path'])")
}

print_backup_usage() {
  echo "$0 backup --output <output_path> [--data_dir <data_dir>] [--exclude_prometheus]"
  echo "Backup YW to a specified output location"
}

print_restore_usage() {
  echo "$0 restore --input <input_path> [--destination <desination>]"
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
