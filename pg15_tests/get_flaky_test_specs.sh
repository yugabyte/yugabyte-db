#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

sed 's/.*/\0\t(flaky)/' <pg15_tests/flaky_tests.tsv
