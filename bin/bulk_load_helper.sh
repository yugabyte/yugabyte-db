#!/usr/bin/env bash

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
  cat <<EOT
Usage: ${0##*/} [<options>]
  Helper script to copy generated SSTable files to a production cluster. This script is supposed to
  be used by yb-bulk load to copy files over to the production cluster so that the tool can then
  import those files into the production cluster.
Options:
  -t, --tablet_id <tablet_id>
    the tablet id to process.
  -r, --replicas <replicas>
    comma separated list of IP addresses for replicas of the given tablet_id.
  -i, --ssh_key_file <keyfile>
    path to the ssh key for the tservers.
  -u, --ssh_user_name <username>
    the username to use for ssh (default: yugabyte).
  -p, --ssh_port <port>
    the port to use for ssh (default: 54422).
  -d, --tablet_data_dir <tabletdatadir>
    directory for tablet data on local filesystem.
  -h, --help
    Show usage.
EOT
}

TSERVER_CONF_FILE=/home/yugabyte/tserver/conf/server.conf

tablet_id=""
replicas=""
ssh_key_file=""
tablet_data_dir=""
ssh_user_name="yugabyte"
ssh_port=54422
while [[ $# -gt 0 ]]; do
  case "$1" in
    -t|--tablet_id)
      tablet_id=$2
      shift
    ;;
    -r|--replicas)
      replicas=$2
      shift
    ;;
    -i|--ssh_key_file)
      ssh_key_file=$2
      shift
    ;;
    -d|--tablet_data_dir)
      tablet_data_dir=$2
      shift
    ;;
    -u|--ssh_user_name)
      ssh_user_name=$2
      shift
    ;;
    -p|--ssh_port)
      ssh_port=$2
      shift
    ;;
    -h|--help)
      print_help
      exit 0
    ;;
    *)
      echo "Invalid option: $1" >&2
      print_help
      exit 1
  esac
  shift
done

if [[ -z $tablet_id || -z $replicas || -z $ssh_key_file || -z $tablet_data_dir ]]; then
  echo "Need to specify --tablet_id, --replicas, --ssh_key_file and --tablet_data_dir" >&2
  print_help
  exit 1
fi

# Parse comma separated replicas into an array.
IFS=',' read -ra REPLICAS <<< "$replicas"

# Process each replica.
ssh_cmd_prefix="ssh -o 'StrictHostKeyChecking no' -p $ssh_port -i $ssh_key_file $ssh_user_name"
for replica in "${REPLICAS[@]}"; do
  # Find the data dirs for the tserver
  data_dirs="$(eval "$ssh_cmd_prefix@$replica" "grep fs_data_dirs $TSERVER_CONF_FILE | \
               cut -d'=' -f2" 2>/dev/null)"
  IFS=',' read -ra data_dir_arr <<< "$data_dirs"

  # Now find the tablet in the data dirs.
  remote_tablet_data_dir=""
  for data_dir in "${data_dir_arr[@]}"; do
    remote_tablet_data_dir="$(eval "$ssh_cmd_prefix@$replica" \
    "find $data_dir/yb-data/tserver/data/rocksdb/ -name \"*$tablet_id\"")"
    if [[ -n $remote_tablet_data_dir ]]; then
      break;
    fi
  done

  if [[ -z $remote_tablet_data_dir ]]; then
    echo "ERROR: Couldn't find tablet: $tablet_id on replica $replica, tried data directories:
    ${data_dir_arr[*]}" >&2
    exit 1
  fi

  echo "Found tablet dir $remote_tablet_data_dir on replica $replica" >&2
  tablet_parent="$(dirname "$remote_tablet_data_dir")"
  staging_dir="$tablet_parent/tablet-$tablet_id.bulk_load_staging"

  # Create staging directory and copy files over to remote tserver.
  echo "Creating staging directory $staging_dir on $replica" >&2
  eval "$ssh_cmd_prefix@$replica" "\"mkdir -p $staging_dir\""

  echo "Copying tablet data from $tablet_data_dir to $remote_tablet_data_dir on $replica" >&2
  scp -q -o 'StrictHostKeyChecking no' -P "$ssh_port" -i "$ssh_key_file" "$tablet_data_dir/*" \
  "$ssh_user_name@$replica:$staging_dir/"

  # Output the replica and staging directory location for other tools to process.
  echo "$replica,$staging_dir"
done
