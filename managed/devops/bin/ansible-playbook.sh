#!/bin/bash
#
# Copyright 2019 YugabyteDB, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

set -euo pipefail

. "${BASH_SOURCE%/*}/common.sh"

detect_os

# Use the PEX environment when we are not executing on macOS, and if the environment variable
# YB_USE_VIRTUAL_ENV is unset. Also, the pexEnv folder should exist before activating the
# PEX virtual environment.
if [ "$is_mac" == false ] && \
   [[ -z ${YB_USE_VIRTUAL_ENV:-} ]] && \
   [ -d "$yb_devops_home/pex/pexEnv" ]; then

  activate_pex
  pythonpath=$(head -n 1 "$pex_venv_dir/bin/ansible-playbook")
  if [[ "$pythonpath" =~ ^#\!\/.* ]]; then
    # Remove the #! from beginning of shebang
    ${pythonpath:2} $pex_venv_dir/bin/ansible-playbook "$@"
  else
    log "Found \"$pythonpath\" shebang in PEX ansible-playbook script, invoking directly."
    ansible-playbook "$@"
  fi

else
  activate_virtualenv
  ansible-playbook "$@"
fi

