#!/bin/bash
#
# Copyright 2019 YugaByte, Inc. and Contributors
#
# Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
# may not use this file except in compliance with the License. You
# may obtain a copy of the License at
#
# https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt

set -e

. "${BASH_SOURCE%/*}"/common.sh

detect_os

# The virtual environment is used if the operating system is macOS, or if the
# environment variable YB_USE_VIRTUAL_ENV is set (default unset).
if [ "$is_mac" == true ] || [[ -n ${YB_USE_VIRTUAL_ENV:-} ]]; then
    activate_virtualenv
    cd "$yb_devops_home"
    "$PYTHON_EXECUTABLE" "$(which ybcloud.py)" "$@"
# The PEX environment is used in all other cases.
else
    $PYTHON_EXECUTABLE $PEX_PATH $SCRIPT_PATH "$@"
fi
