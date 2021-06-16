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

print_help() {
  cat <<-EOT
Installs Ansible requirements, e.g. roles that our playbooks depend on.
Usage: ${0##*/} <options>
Options:
  -h, --help
    Show usage.
  -r, --reinstall <pattern>
    Reinstall dependencies that match the given regex pattern (non-anchored).
  -f, --force
    Force all dependencies to be reinstalled.
  --prod, --production
    Use the production version of the Ansible requirements file with a reduced set of third-party
    role dependencies.
EOT
}

. "${0%/*}/common.sh"
cd "$yb_devops_home"

reinstall_pattern=""
force=""
role_file=ansible_requirements.yml

while [ $# -gt 0 ]; do
  case "$1" in
    -h|--help)
      print_help
      exit 0
    ;;
    -r|--reinstall)
      reinstall_pattern="$2"
      shift
    ;;
    -f|--force)
      force="--force"
    ;;
    --prod|--production)
      role_file=ansible_requirements_prod.yml
    ;;
  esac
  shift
done

if [[ -n $reinstall_pattern ]]; then
  for third_party_role_dir in third-party/roles/*; do
    if [[ "${third_party_role_dir##*/}" =~ $reinstall_pattern ]]; then
      echo "Deleting directory '$third_party_role_dir'"
      rm -rf "$third_party_role_dir"
    fi
  done
fi

activate_virtualenv

(
  set -x
  "$PYTHON_EXECUTABLE" $(which ansible-galaxy) install \
      --role-file="$role_file" \
      --roles-path=./third-party/roles $force
)
