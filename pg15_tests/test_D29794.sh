#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

"${build_cmd[@]}" --gtest_filter PgSingleTServerTest.ScanComplexPK
