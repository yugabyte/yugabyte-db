#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

"${build_cmd[@]}" --cxx-test pgwrapper_pg_op_buffering-test \
  --gtest_filter PgOpBufferingTest.FKCheckWithNonTxnWrites
