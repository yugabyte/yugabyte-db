#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

"${build_cmd[@]}" --cxx-test pgwrapper_pg_stat_activity-test \
  --gtest_filter PgStatActivityTest.AllBackendsTransaction
"${build_cmd[@]}" --cxx-test pgwrapper_pg_stat_activity-test \
  --gtest_filter PgStatActivityTest.DDLInsideDMLTransaction
