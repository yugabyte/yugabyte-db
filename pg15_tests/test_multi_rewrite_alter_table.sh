#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

if ! cxx_test \
  pgwrapper_pg_ddl_atomicity-test \
  PgDdlAtomicitySanityTest.TestMultiRewriteAlterTable; then
  grep_in_cxx_test \
    'does not exist: OBJECT_NOT_FOUND' \
    PgDdlAtomicitySanityTest.TestMultiRewriteAlterTable
fi
