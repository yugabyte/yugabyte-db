#!/bin/bash
#
# Copyright 2019 YugaByte, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

# Sets up common environment and command-line parameters for running ansible
# commands.

set -euo pipefail

. "${BASH_SOURCE%/*}"/common.sh

print_usage() {
  cat <<-EOT
Usage: ${0##/*} <ansible_tool> [<options>]
The tool name must start with "ansible-" (e.g. ansible-playbook, etc.)
EOT
}

if [[ $# -eq 0 ]]; then
  print_usage >&2
  exit 1
fi

ansible_tool=$1

if [[ $ansible_tool != ansible-* ]]; then
  echo "The first argument is expected to start with 'ansible-', found: '$1'" >&2
  exit 1
fi

shift

activate_virtualenv

set -x
cd "$devops_bin_dir/.."
"$ansible_tool" -i inventory "$@"
