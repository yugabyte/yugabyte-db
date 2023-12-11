#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

"${build_cmd[@]}" --cxx-test integration-tests_cdcsdk_snapshot-test \
  --gtest_filter CDCSDKYsqlTest.TestCommitTimeRecordTimeAndNoSafepointRecordForSnapshot
"${build_cmd[@]}" --cxx-test integration-tests_cdcsdk_snapshot-test \
  --gtest_filter CDCSDKYsqlTest.TestCompactionDuringSnapshot
"${build_cmd[@]}" --cxx-test integration-tests_cdcsdk_tablet_split-test \
  --gtest_filter CDCSDKYsqlTest.TestRecordCountsAfterMultipleTabletSplits
"${build_cmd[@]}" --cxx-test integration-tests_cdcsdk_tablet_split-test \
  --gtest_filter CDCSDKYsqlTest.TestRecordCountsAfterMultipleTabletSplitsInMultiNodeCluster
"${build_cmd[@]}" --cxx-test integration-tests_cdcsdk_ysql-test \
  --gtest_filter CDCSDKYsqlTest.TestCDCLagMetric
"${build_cmd[@]}" --cxx-test integration-tests_cdcsdk_ysql-test \
  --gtest_filter CDCSDKYsqlTest.TestEnumOnRestart
"${build_cmd[@]}" --cxx-test integration-tests_cdcsdk_ysql-test \
  --gtest_filter CDCSDKYsqlTest.TestLargeTxnWithExplicitStream
