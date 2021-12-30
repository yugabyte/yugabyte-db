#!/usr/bin/env bash

. "${BASH_SOURCE[0]%/*}/../build-support/common-build-env.sh"

activate_virtualenv
set_pythonpath
set -x
python3 "$@"
