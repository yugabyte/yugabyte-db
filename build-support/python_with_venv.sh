#!/usr/bin/env bash

# shellcheck source=build-support/common-build-env.sh
. "${BASH_SOURCE[0]%/*}/../build-support/common-build-env.sh"

activate_virtualenv
set_pythonpath
set -x
python3 "$@"
