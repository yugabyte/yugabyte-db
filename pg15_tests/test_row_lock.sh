#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

"${build_cmd[@]}" --cxx-test pgwrapper_pg_row_lock-test \
  --gtest_filter PgRowLockTest.SelectForKeyShareWithRestart
