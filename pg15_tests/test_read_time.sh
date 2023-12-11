#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

"${build_cmd[@]}" --cxx-test pgwrapper_pg_read_time-test \
  --gtest_filter PgReadTimeTest.CheckReadTimePickingLocation
