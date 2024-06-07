#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

failing_java_test TestPgRegressTypesString
grep_in_java_test \
  "failed tests: [yb_collate_icu_utf8, yb_pg_tsearch, yb_pg_tstypes]" \
  TestPgRegressTypesString
