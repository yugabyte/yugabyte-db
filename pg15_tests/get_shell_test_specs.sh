#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

cd pg15_tests
find . -name 'test_*.sh' \
  | grep -oE 'test_[^.]+' \
  | sed 's/^/SHELL\t/'
