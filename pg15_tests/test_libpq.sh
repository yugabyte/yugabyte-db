#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

"${build_cmd[@]}" --cxx-test pgwrapper_pg_libpq-test \
  --gtest_filter PgLibPqTest.TablegroupCreationFailureWithRestart
"${build_cmd[@]}" --cxx-test pgwrapper_pg_libpq-test \
  --gtest_filter PgLibPqTest.TestConcurrentCounterReadCommitted
"${build_cmd[@]}" --cxx-test pgwrapper_pg_libpq-test \
  --gtest_filter PgLibPqTest.TxnConflictsForTablegroupsOrdered
"${build_cmd[@]}" --cxx-test pgwrapper_pg_libpq-test \
  --gtest_filter PgLibPqTest.TxnConflictsForTablegroupsYbSeq
"${build_cmd[@]}" --cxx-test pgwrapper_pg_libpq-test \
  --gtest_filter PgLibPqTest.TxnConflictsForTablegroupsYbSeq
