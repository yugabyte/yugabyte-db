#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

failing_java_test TestPgRegressTypesNumeric
grep_in_java_test \
  "failed tests: [yb_pg_float4, yb_pg_int8]" \
  TestPgRegressTypesNumeric
