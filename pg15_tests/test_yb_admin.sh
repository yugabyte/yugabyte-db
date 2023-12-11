#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

"${build_cmd[@]}" --cxx_test yb-admin-snapshot-schedule-test \
  --gtest-filter YbAdminSnapshotScheduleTest.SysCatalogRetentionWithFastPitr
