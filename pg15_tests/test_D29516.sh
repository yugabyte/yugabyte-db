#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

# TODO(#19488)
"${build_cmd[@]}" --cxx-test pgwrapper_pg_index_backfill-test \
  --gtest_filter PgIndexBackfillTest.InsertsWhileCreatingIndex \
  || grep 'TRAP: FailedAssertion("ItemPointerIsValid(&scan->xs_heaptid)"' \
          'build/latest/yb-test-logs/tests-pgwrapper__pg_index_backfill-test/PgIndexBackfillTest_InsertsWhileCreatingIndex.log'
