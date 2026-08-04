#!/bin/bash
set -e
. "${BASH_SOURCE%/*}"/common.sh

activate_virtualenv
test_path="$yb_devops_home"/tests/opscli
cd $test_path

# Note: this requires that you would have installed ybops, for any tests that depend on that to
# work! Also, make sure the test files start with test_ to be auto-discovered.
"$PYTHON_EXECUTABLE" -m unittest discover
