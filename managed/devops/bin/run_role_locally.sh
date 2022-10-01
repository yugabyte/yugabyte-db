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

print_usage() {
  cat <<-EOT
Usage: ${0##*/} <role_name>
Runs a local playbook with the given role.
EOT
}

local_role="${1:-}"
if [ -z "$local_role" ]; then
  print_usage >&2
  exit 1
fi

role_playbook=local_role.yml

${BASH_SOURCE%/*}/run_playbook_locally.sh "$role_playbook" --extra-vars "local_role=$local_role"
