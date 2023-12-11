#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

"${build_cmd[@]}" --cxx-test pgwrapper_pg_wait_on_conflict-test \
  --gtest_filter PgWaitQueuesReadCommittedTest.TestDeadlockSimple
