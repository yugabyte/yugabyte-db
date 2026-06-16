#!/bin/bash

set -e
. "${BASH_SOURCE%/*}"/common.sh

detect_os
test_path="$yb_devops_home/tests/add_superadmin"

# Use the PEX environment when we are not executing on macOS, and if the environment variable
# YB_USE_VIRTUAL_ENV is unset. Also, the pexEnv folder should exist before activating the
# PEX virtual environment.
if [ "$is_mac" == false ] && \
   [[ -z ${YB_USE_VIRTUAL_ENV:-} ]] && \
   [ -d "$yb_devops_home/pex/pexEnv" ]; then
    activate_pex
# The virtual environment is used in all other cases.
else
    activate_virtualenv
fi

# Help-output smoke check for externally used CLI script.
"$PYTHON_EXECUTABLE" "$yb_devops_home/bin/add_superadmin_user.py" -h > /dev/null

cd "$test_path"
exec "$PYTHON_EXECUTABLE" -m unittest discover
