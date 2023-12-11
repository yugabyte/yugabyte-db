#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

"${build_cmd[@]}" --cxx-test pgwrapper_pg_get_lock_status-test \
  --gtest_filter PgGetLockStatusTest.TestPgLocksWhileDDLInProgress
