#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

# TODO(#19488)
if ! cxx_test \
  pgwrapper_pg_index_backfill-test \
  PgIndexBackfillTest.InsertsWhileCreatingIndex; then
  grep_in_cxx_test \
    'TRAP: FailedAssertion("ItemPointerIsValid(&scan->xs_heaptid)"' \
    PgIndexBackfillTest.InsertsWhileCreatingIndex
fi
