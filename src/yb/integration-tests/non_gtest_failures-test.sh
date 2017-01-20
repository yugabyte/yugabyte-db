#!/usr/bin/env bash

set -e -u -o pipefail

# Exit on purpose to test jenkins test results reporting.
echo "$0"
# List processes running for debugging purposes. This test will only be executed during
# debugging of build support scripts.
ps -f
exit 1
