#!/usr/bin/env bash
source "${BASH_SOURCE[0]%/*}"/common.sh

failing_java_test 'TestPgRegressPartitions#yb_partitions_tests'
grep_in_java_test \
  'failed tests: [yb_partition_pk]' \
  'TestPgRegressPartitions#yb_partitions_tests'
