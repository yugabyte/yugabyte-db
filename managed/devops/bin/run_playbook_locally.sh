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

. "${BASH_SOURCE%/*}/common.sh"

print_usage() {
  cat <<-EOT
Usage: ${0##*/} <playbook_path>
Runs the given playbook on the local machine.
EOT
}

cd "$yb_devops_home"

playbook_path="${1:-}"
if [ -z "${playbook_path}" ]; then
  print_usage >&2
  exit 1
fi

shift

if [ ! -f "$playbook_path" ]; then
  echo "Playbook not found at '$playbook_path'" >&2
  exit 1
fi

# Check if the user has already entered a sudo password on this machine and don't tell Ansible to
# ask for it unnecessarily.
ask_sudo_pass_opt=""

# -n means "non-interactive": don't block waiting for password if the user requires a password to
# run sudo commands.
if ! sudo -n true; then
  ask_sudo_pass_opt=--ask-sudo-pass
fi

activate_virtualenv

set -x
ansible-playbook -i "localhost," -c local "$playbook_path" $ask_sudo_pass_opt "$@"
