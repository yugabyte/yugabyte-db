#!/bin/bash
# shellcheck disable=SC2034

# Usage: fio_disk_benchmark.sh <DIR> <DESCRIPTION>
#   fio_disk_benchmark.sh /mnt/d0 c3.4xlarge-instance
#   fio_disk_benchmark.sh /mnt/d2 ebs-gp2-3334gb

#
# The following only applies to changes made to this file as part of YugaByte development.
#
# Portions Copyright (c) YugaByte, Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
# in compliance with the License.  You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software distributed under the License
# is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
# or implied.  See the License for the specific language governing permissions and limitations
# under the License.
#
DIRECTORY=$1

function run_test {
  TEST=$1
  CMD=$2

  echo "$TEST: $CMD"
  FILE=/tmp/yb-bench-"$TEST".log
  $CMD > "$FILE" 2>&1
  export "${TEST}"_bw_kbps="$(grep 'bw=' "$FILE" | sed 's/.*bw=//' | sed 's/KB.*//')"
  export "${TEST}"_iops="$(grep 'iops=' "$FILE" | sed 's/.*iops=//' | sed 's/,.*//')"
  export "${TEST}"_latency_avg="$(grep ' lat (usec):' "$FILE" | sed 's/.*avg=//' | sed 's/,.*//')"
}

echo ""
echo "  $BENCHMARK_NAME Benchmark on $DIRECTORY"
echo ""

TEST1="WAL: odirect 4K multi-file writes"
TEST1_CMD="sudo fio --directory=$DIRECTORY --name fio_test_file --direct=1 --rw=randwrite --bs=4k --size=1G --numjobs=16 --time_based --runtime=30 --group_reporting --norandommap"

TEST2="WAL: buffered 1K multi-file writes"
TEST2_CMD="sudo fio --directory=$DIRECTORY --name fio_test_file --direct=0 --rw=randwrite --bs=1k --size=1G --numjobs=16 --time_based --runtime=30 --group_reporting --norandommap"

TEST3="WAL: odirect 8K single-file write"
TEST3_CMD="sudo fio --directory=$DIRECTORY --name fio_test_file --direct=1 --rw=write --bs=8k --size=1G --numjobs=1 --time_based --runtime=30 --group_reporting --norandommap"

TEST4="Compact: buffered 32K multi-file write"
TEST4_CMD="sudo fio --directory=$DIRECTORY --name fio_test_file --direct=0 --rw=randwrite --bs=32k --size=1G --numjobs=16 --time_based --runtime=30 --group_reporting --norandommap"

TEST5="Read: 8K multi-file reads"
TEST5_CMD="sudo fio --directory=$DIRECTORY --name fio_test_file --rw=randread --bs=8k --size=1G --numjobs=16 --time_based --runtime=30 --group_reporting --norandommap"

# Add all the tests into a single variable to loop through.
TESTS="TEST1 TEST2 TEST3 TEST4 TEST5"

for TEST in $TESTS
do
  cmd="${TEST}_CMD"
  run_test "$TEST" "${!cmd}"
done

echo "=============="
echo "   Results    "
echo "=============="
echo ""
printf '\n%-12s %-40s %-10s %-10s %-10s\n' "Directory" "TEST" "IOPS" "Lat(us)" "B/w(kbps)"
for TEST in $TESTS
do
  iops="${TEST}_iops"
  latency_avg="${TEST}_latency_avg"
  bw_kbps="${TEST}_bw_kbps"
  printf '%-12s %-40s %-10s %-10s %-10s\n' "$DIRECTORY" "${!TEST}" "${!iops}" "${!latency_avg}" \
    "${!bw_kbps}"
done
