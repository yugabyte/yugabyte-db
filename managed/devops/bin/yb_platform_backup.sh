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

create_backup() {
  now=$(date +"%y-%m-%d-%H-%M")
  output_path=$1
  data_dir=$2
  exclude_prometheus_flag=" "
  if [[ "$3" = true ]]; then
    exclude_prometheus_flag=" --exclude prometheus* "
  fi
  tar="${output_path}/backup_${now}.tgz"
  trap "cleanup ${data_dir}/${YUGAWARE_DUMP_FNAME}" EXIT
  sh -c "docker exec -i postgres pg_dump -U postgres -Fc yugaware > \
                                 ${data_dir}/${YUGAWARE_DUMP_FNAME}"
  tar $exclude_prometheus_flag --exclude "postgresql" -czf $tar -C $data_dir .
  echo "Finished creating backup $tar"
}

restore_backup() {
  input_path=$1
  destination=$2
  yugaware_dump="${destination}/${YUGAWARE_DUMP_FNAME}"
  trap "cleanup $yugaware_dump" EXIT
  tar -xzf $input_path --directory $destination
  sh -c "docker exec -i postgres pg_restore -U postgres -d yugaware -c < ${yugaware_dump}"
  echo "Finished restoring backup"
}

print_backup_usage() {
  echo "ERROR: Backup Usage"
  echo "$0 backup --output <output_path> [--data_dir <data_dir>] [--exclude_prometheus]"
  echo "Backup YW to a specified output location"
}

print_restore_usage() {
  echo "ERROR: Restore usage"
  echo "$0 restore --input <input_path> [--destination <desination>]"
  echo "Restore YW from a specified input location"
}

cleanup () {
  rm -f "$1"
}

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
        *)
          echo "$1"
          print_backup_usage
          exit 1
      esac
    done

    if [[ -z "$output_path" ]]; then
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
        *)
          print_restore_usage
          exit 1
      esac
    done
    if [[ -z "$input_path" ]]; then
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
