#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

failing_java_test TestPgRegressTable
grep_in_java_test \
  "failed tests: [yb_alter_table, yb_alter_table_rewrite]" \
  TestPgRegressTable
