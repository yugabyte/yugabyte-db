#!/usr/bin/env bash

# Copyright (c) YugaByte, Inc.
set -euo pipefail

print_help() {
  cat <<EOT
Usage: ${0##*/} [<options>]
  Script to cleanup left over files from a bulk load.
Options:
  -t, --tserver_ip <tserver_ip>
    tserver which needs to be cleaned up.
  -i, --ssh_key_file <keyfile>
    path to the ssh key for the tserver.
  -u, --ssh_user_name <username>
    the username to use for ssh (default: yugabyte).
  -p, --ssh_port <port>
    the port to use for ssh (default: 54422).
  -d, --staging_dir <stagin_dir>
    staging directory on the tserver to cleanup.
  -h, --help
    Show usage.
EOT
}

tserver_ip=""
ssh_key_file=""
staging_dir=""
ssh_user_name="yugabyte"
ssh_port=54422
while [[ $# -gt 0 ]]; do
  case "$1" in
    -t|--tserver_ip)
      tserver_ip=$2
      shift
    ;;
    -i|--ssh_key_file)
      ssh_key_file=$2
      shift
    ;;
    -d|--staging_dir)
      staging_dir=$2
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

if [[ -z $tserver_ip || -z $ssh_key_file || -z $staging_dir ]]; then
  echo "Need to specify --tserver_ip, --ssh_key_file and --staging_dir" >&2
  print_help
  exit 1
fi

set +eo pipefail
match=$(echo $staging_dir | grep bulk_load_staging)
set -eo pipefail

if [[ -z $match ]]; then
  echo "Invalid staging dir: $staging_dir" >&2
  exit 1
fi

# Delete the appropriate directory.
echo "Deleting $staging_dir on tserver $tserver_ip"
ssh -o 'StrictHostKeyChecking no' -p $ssh_port -i $ssh_key_file $ssh_user_name@$tserver_ip \
"rm -rf $staging_dir"
echo "Deleted $staging_dir on tserver $tserver_ip"
