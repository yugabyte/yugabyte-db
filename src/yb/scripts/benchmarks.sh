#!/bin/bash -xe
########################################################################
#
# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.
#
# Run and compare benchmarks.
#
# Allows for running comparisons either locally or as part of a
# Jenkins job which integrates with a historical stats DB.
# Run this script with -help for usage information.
#
# Jenkins job: http://sandbox.jenkins.cloudera.com/job/kudu-benchmarks
########################################################################

################################################################
# Constants
################################################################

MODE_JENKINS="jenkins"
MODE_LOCAL="local"

LOCAL_STATS_BASE="local-stats"

NUM_MT_TABLET_TESTS=5
MT_TABLET_TEST=mt-tablet-test
RPC_BENCH_TEST=RpcBenchBenchmark
CBTREE_TEST=cbtree-test
BLOOM_TEST=BloomfileBenchmark
MT_BLOOM_TEST=MultithreadedBloomfileBenchmark
WIRE_PROTOCOL_TEST=WireProtocolBenchmark
COMPACT_MERGE_BENCH=CompactBenchMerge
WITH_OVERLAP=Overlap
NO_OVERLAP=NoOverlap

MEMROWSET_BENCH=MemRowSetBenchmark
TS_INSERT_LATENCY=TabletServerInsertLatency
TS_8THREAD_BENCH=TabletServer8Threads
INSERT=Insert
SCAN_NONE_COMMITTED=ScanNoneCommitted
SCAN_ALL_COMMITTED=ScanAllCommitted

FS_SCANINSERT_MRS=FullStackScanInsertMRSOnly
FS_SCANINSERT_DISK=FullStackScanInsertWithDisk

LOG_DIR_NAME=build/latest/bench-logs
OUT_DIR_NAME=build/latest/bench-out
HTML_FILE="benchmarks.html"

# Most tests will run this many times.
NUM_SAMPLES=${NUM_SAMPLES:-10}

################################################################
# Global variables
################################################################

BENCHMARK_MODE=$MODE_JENKINS # we default to "jenkins mode"
BASE_DIR=""
LOGDIR=""
OUTDIR=""

################################################################
# Functions
################################################################

usage_and_die() {
  set +x
  echo "Usage: $0 [-local [git-hash-1 [git-hash-2 ...]]]"
  echo "       When -local is specified, perf of 1 or more git hashes are plotted."
  echo "       Otherwise, the script is run in 'Jenkins' mode and expects the"
  echo "       usual Jenkins environment variables to be defined, such as"
  echo "       BUILD_NUMBER and JOB_NAME."
  exit 1
}

ensure_cpu_scaling() {
  $(dirname $BASH_SOURCE)/ensure_cpu_scaling.sh "$@"
}

record_result() {
  local BUILD_IDENTIFIER=$1
  local TEST_NAME=$2
  local ITER=$3
  local VALUE=$4
  if [ $BENCHMARK_MODE = $MODE_JENKINS ]; then
    python write-jobs-stats-to-mysql.py $JOB_NAME $BUILD_IDENTIFIER $TEST_NAME $ITER $VALUE
  else
    local STATS_FILE="$OUTDIR/$LOCAL_STATS_BASE-$TEST_NAME.tsv"
    # Note: literal tabs in below string.
    echo "${TEST_NAME}	${VALUE}	${BUILD_IDENTIFIER}" >> "$STATS_FILE"
  fi
}

load_stats() {
  local TEST_NAME="$1"
  if [ "$BENCHMARK_MODE" = "$MODE_JENKINS" ]; then
    # Get last 4 weeks of stats
    python get-job-stats-from-mysql.py $TEST_NAME 28
  else
    # Convert MySQL wildcards to shell wildcards.
    local TEST_NAME=$(echo $TEST_NAME | perl -pe 's/%/*/g')
    local STATS_FILES=$(ls $OUTDIR/$LOCAL_STATS_BASE-$TEST_NAME.tsv)
    # Note: literal tabs in below string.
    echo "workload	runtime	build_number"
    for f in $STATS_FILES; do
      cat $f
    done
  fi
}

write_img_plot() {
  local INPUT_FILE=$1
  local TEST_NAME=$2
  # Rscript fails when there's only a header, so just skip
  if [ `wc -l $INPUT_FILE | cut -d ' ' -f1` -gt 1 ]; then
    Rscript jobs_runtime.R $INPUT_FILE $TEST_NAME
  fi
}

write_mttablet_img_plots() {
  local INPUT_FILE=$1
  local TEST_NAME=$2
  xvfb-run Rscript mt-tablet-test-graph.R $INPUT_FILE $TEST_NAME
}

build_kudu() {
  # PATH=<toolchain_stuff>:$PATH
  export TOOLCHAIN=/mnt/toolchain/toolchain.sh
  if [ -f "$TOOLCHAIN" ]; then
    source $TOOLCHAIN
  fi

  # Build thirdparty
  $BASE_DIR/build-support/enable_devtoolset.sh thirdparty/build-if-necessary.sh

  # PATH=<thirdparty_stuff>:<toolchain_stuff>:$PATH
  THIRDPARTY_BIN=$BASE_DIR/thirdparty/installed/bin
  export PPROF_PATH=$THIRDPARTY_BIN/pprof

  BUILD_TYPE=release

  # Build Kudu
  mkdir -p build/$BUILD_TYPE
  pushd build/$BUILD_TYPE
  rm -rf CMakeCache.txt CMakeFiles/

  $BASE_DIR/build-support/enable_devtoolset.sh $THIRDPARTY_BIN/cmake -DCMAKE_BUILD_TYPE=${BUILD_TYPE} ../..

  # clean up before we run
  rm -Rf /tmp/kudutpch1-$UID
  mkdir -p /tmp/kudutpch1-$UID

  NUM_PROCS=$(cat /proc/cpuinfo | grep processor | wc -l)
  make -j${NUM_PROCS} 2>&1 | tee build.log
  popd

}

run_benchmarks() {
  # Create output directories if needed.
  mkdir -p "$LOGDIR"
  mkdir -p "$OUTDIR"

  # run all of the variations of mt-tablet-test
  ./build/latest/bin/mt-tablet-test \
    --gtest_filter=\*DoTestAllAtOnce\* \
    --num_counter_threads=0 \
    --tablet_test_flush_threshold_mb=32 \
    --num_slowreader_threads=0 \
    --flusher_backoff=1.0 \
    --flusher_initial_frequency_ms=1000 \
    --inserts_per_thread=1000000 \
    &> $LOGDIR/${MT_TABLET_TEST}.log

  # run rpc-bench test 5 times. 10 seconds per run
  for i in $(seq 1 $NUM_SAMPLES); do
    KUDU_ALLOW_SLOW_TESTS=true ./build/latest/bin/rpc-bench &> $LOGDIR/$RPC_BENCH_TEST$i.log
  done

  # run cbtree-test 5 times. 20 seconds per run
  for i in $(seq 1 $NUM_SAMPLES); do
    KUDU_ALLOW_SLOW_TESTS=true ./build/latest/bin/cbtree-test \
      --gtest_filter=TestCBTree.TestScanPerformance &> $LOGDIR/${CBTREE_TEST}$i.log
  done

  # run bloomfile-test 5 times. ~3.3 seconds per run
  for i in $(seq 1 $NUM_SAMPLES); do
    ./build/latest/bin/bloomfile-test --benchmark_queries=10000000 --bloom_size_bytes=32768 \
      --n_keys=100000 --gtest_filter=*Benchmark &> $LOGDIR/$BLOOM_TEST$i.log
  done

  # run mt-bloomfile-test 5 times. 20-30 seconds per run.
  # The block cache is set to 1MB to generate churn.
  for i in $(seq 1 $NUM_SAMPLES); do
    ./build/latest/bin/mt-bloomfile-test --benchmark_queries=2000000 --bloom_size_bytes=32768 \
      --n_keys=5000000 --block_cache_capacity_mb=1 &> $LOGDIR/$MT_BLOOM_TEST$i.log
  done

  # run wire_protocol-test 5 times. 6 seconds per run
  for i in $(seq 1 $NUM_SAMPLES); do
    KUDU_ALLOW_SLOW_TESTS=true ./build/latest/bin/wire_protocol-test --gtest_filter=*Benchmark \
      &> $LOGDIR/$WIRE_PROTOCOL_TEST$i.log
  done

  # run compaction-test 5 times, 6 seconds each
  for i in $(seq 1 $NUM_SAMPLES); do
    KUDU_ALLOW_SLOW_TESTS=true ./build/latest/bin/compaction-test \
      --gtest_filter=TestCompaction.BenchmarkMerge* &> $LOGDIR/${COMPACT_MERGE_BENCH}$i.log
  done

  # run memrowset benchmark 5 times, ~10 seconds per run
  for i in $(seq 1 $NUM_SAMPLES) ; do
    ./build/latest/bin/memrowset-test --roundtrip_num_rows=10000000 \
        --gtest_filter=\*InsertCount\* &> $LOGDIR/${MEMROWSET_BENCH}$i.log
  done

  # Run single-threaded TS insert latency benchmark, 5-6 seconds per run
  for i in $(seq 1 $NUM_SAMPLES) ; do
    KUDU_ALLOW_SLOW_TESTS=true ./build/latest/bin/tablet_server-test \
      --gtest_filter=*MicroBench* \
      --single_threaded_insert_latency_bench_warmup_rows=1000 \
      --single_threaded_insert_latency_bench_insert_rows=10000 &> $LOGDIR/${TS_INSERT_LATENCY}$i.log
  done

  # Run multi-threaded TS insert benchmark
  for i in $(seq 1 $NUM_SAMPLES) ; do
    KUDU_ALLOW_SLOW_TESTS=1 build/latest/bin/tablet_server-stress-test \
      --num_inserts_per_thread=30000 &> $LOGDIR/${TS_8THREAD_BENCH}$i.log
  done

  # Run full stack scan/insert test using MRS only, ~26s each
  for i in $(seq 1 $NUM_SAMPLES) ; do
    ./build/latest/bin/full_stack-insert-scan-test \
      --gtest_filter=FullStackInsertScanTest.MRSOnlyStressTest \
      --concurrent_inserts=50 \
      --inserts_per_client=200000 \
      --rows_per_batch=10000 \
      &> $LOGDIR/${FS_SCANINSERT_MRS}$i.log
  done

  # Run full stack scan/insert test with disk, ~50s each
  for i in $(seq 1 $NUM_SAMPLES) ; do
    ./build/latest/bin/full_stack-insert-scan-test \
      --gtest_filter=FullStackInsertScanTest.WithDiskStressTest \
      --concurrent_inserts=50 \
      --inserts_per_client=200000 \
      --rows_per_batch=10000 \
      &> $LOGDIR/${FS_SCANINSERT_DISK}$i.log
  done
}

parse_and_record_all_results() {
  local BUILD_IDENTIFIER="$1"

  if [ -z "$BUILD_IDENTIFIER" ]; then
    echo "ERROR: BUILD_IDENTIFIER not defined"
    exit 1
  fi

  pushd src
  pushd kudu
  pushd scripts

  # parse the number of ms out of "[       OK ] MultiThreadedTabletTest/5.DoTestAllAtOnce (14966 ms)"
  local MT_TABLET_TEST_TIMINGS="${MT_TABLET_TEST}-timings"
  grep OK $LOGDIR/${MT_TABLET_TEST}.log | cut -d "(" -f2 | cut -d ")" -f1 | cut -d " " -f1 \
    > $LOGDIR/${MT_TABLET_TEST_TIMINGS}.txt

  # The tests go from 0 to NUM_MT_TABLET_TEST, but files start at line one so we add +1 to the line number.
  # Then using the timing we found, we multiply it by 1000 to gets seconds in float, then send it to MySQL
  for i in $(seq 0 $NUM_MT_TABLET_TESTS); do
    linenumber=$[ $i + 1 ]
    timing=`sed -n "${linenumber}p" $LOGDIR/${MT_TABLET_TEST_TIMINGS}.txt`
    record_result $BUILD_IDENTIFIER MultiThreadedTabletTest_$i 1 `echo $timing / 1000 | bc -l`
  done

  # parse out the real time from: "Time spent Insert 10000000 keys: real 16.438s user 16.164s  sys 0.229s"
  for i in $(seq 1 $NUM_SAMPLES); do
    real=`grep "Time spent Insert" $LOGDIR/${CBTREE_TEST}$i.log | ./parse_real_out.sh`
    record_result $BUILD_IDENTIFIER ConcurrentBTreeScanInsert $i $real
  done

  for i in $(seq 1 $NUM_SAMPLES); do
    real=`grep "not frozen" $LOGDIR/${CBTREE_TEST}$i.log | ./parse_real_out.sh`
    record_result $BUILD_IDENTIFIER ConcurrentBTreeScanNotFrozen $i $real
  done

  for i in $(seq 1 $NUM_SAMPLES); do
    real=`grep "(frozen" $LOGDIR/${CBTREE_TEST}$i.log | ./parse_real_out.sh`
    record_result $BUILD_IDENTIFIER ConcurrentBTreeScanFrozen $i $real
  done

  # parse out the real time from "Time spent with overlap: real 0.557s user 0.546s sys 0.010s"
  for i in $(seq 1 $NUM_SAMPLES); do
    real=`grep "with overlap" $LOGDIR/${COMPACT_MERGE_BENCH}$i.log | ./parse_real_out.sh`
    record_result $BUILD_IDENTIFIER ${COMPACT_MERGE_BENCH}${WITH_OVERLAP} $i $real
  done

  for i in $(seq 1 $NUM_SAMPLES); do
    real=`grep "without overlap" $LOGDIR/${COMPACT_MERGE_BENCH}$i.log | ./parse_real_out.sh`
    record_result $BUILD_IDENTIFIER ${COMPACT_MERGE_BENCH}${NO_OVERLAP} $i $real
  done

  # parse out time from MRS benchmarks
  for i in $(seq 1 $NUM_SAMPLES); do
    log=$LOGDIR/${MEMROWSET_BENCH}$i.log
    real=`grep "Time spent Inserting" $log | ./parse_real_out.sh`
    record_result $BUILD_IDENTIFIER ${MEMROWSET_BENCH}${INSERT} $i $real
    real=`grep "Time spent Scanning rows where none" $log | ./parse_real_out.sh`
    record_result $BUILD_IDENTIFIER ${MEMROWSET_BENCH}${SCAN_NONE_COMMITTED} $i $real
    real=`grep "Time spent Scanning rows where all" $log | ./parse_real_out.sh`
    record_result $BUILD_IDENTIFIER ${MEMROWSET_BENCH}${SCAN_ALL_COMMITTED} $i $real
  done

  # Parse out the real time from: "Time spent Running 10000000 queries: real 3.281s  user 3.273s sys 0.000s"
  for i in $(seq 1 $NUM_SAMPLES); do
    real=`grep "Time spent Running" $LOGDIR/$BLOOM_TEST$i.log | ./parse_real_out.sh`
    record_result $BUILD_IDENTIFIER $BLOOM_TEST $i $real
  done

  # Parse out the real time from: "Time spent Running 2000000 queries: real 28.193s user 26.903s sys 1.032s"
  # Many threads output their value, we keep the last;
  for i in $(seq 1 $NUM_SAMPLES); do
    real=`grep "Time spent Running" $LOGDIR/$MT_BLOOM_TEST$i.log | tail -n1 | ./parse_real_out.sh`
    record_result $BUILD_IDENTIFIER $MT_BLOOM_TEST $i $real
  done

  # Parse out the real time from: "Time spent Converting to PB: real 5.962s  user 5.918s sys 0.025s"
  for i in $(seq 1 $NUM_SAMPLES); do
    real=`grep "Time spent Converting" $LOGDIR/$WIRE_PROTOCOL_TEST$i.log | ./parse_real_out.sh`
    record_result $BUILD_IDENTIFIER $WIRE_PROTOCOL_TEST $i $real
  done

  # parse the rate out of: "I1009 15:00:30.023576 27043 rpc-bench.cc:108] Reqs/sec:         84404.4"
  for i in $(seq 1 $NUM_SAMPLES); do
    rate=`grep Reqs $LOGDIR/$RPC_BENCH_TEST$i.log | cut -d ":" -f 5 | tr -d ' '`
    record_result $BUILD_IDENTIFIER $RPC_BENCH_TEST $i $rate
  done

  # parse latency numbers from single-threaded tserver benchmark
  for i in $(seq 1 $NUM_SAMPLES); do
    for metric in min mean percentile_95 percentile_99 percentile_99_9 ; do
      val=$(grep "\"$metric\": " $LOGDIR/${TS_INSERT_LATENCY}$i.log | awk '{print $2}' | sed -e 's/,//')
      record_result $BUILD_IDENTIFIER ${TS_INSERT_LATENCY}_$metric $i $val
    done
  done

  # parse latency and throughput numbers from multi-threaded tserver benchmark
  for i in $(seq 1 $NUM_SAMPLES); do
    local log=$LOGDIR/${TS_8THREAD_BENCH}$i.log
    for metric in min mean percentile_95 percentile_99 percentile_99_9 ; do
      val=$(grep "\"$metric\": " $log | awk '{print $2}' | sed -e 's/,//')
      record_result $BUILD_IDENTIFIER ${TS_8THREAD_BENCH}_${metric}_latency $i $val
    done
    rate=$(grep -o 'Throughput.*' $log | awk '{print $2}')
    record_result $BUILD_IDENTIFIER ${TS_8THREAD_BENCH}_throughput_wall $i $rate
    rate=$(grep -o 'CPU efficiency.*' $log | awk '{print $3}')
    record_result $BUILD_IDENTIFIER ${TS_8THREAD_BENCH}_throughput_cpu $i $rate
  done

  # parse scan timings for scans and inserts with MRS only
  for i in $(seq 1 $NUM_SAMPLES); do
    local log=$LOGDIR/${FS_SCANINSERT_MRS}$i.log
    insert=`grep "Time spent concurrent inserts" $log | ./parse_real_out.sh`
    scan_full=`grep "Time spent full schema scan" $log | ./parse_real_out.sh`
    scan_str=`grep "Time spent String projection" $log | ./parse_real_out.sh`
    scan_int32=`grep "Time spent Int32 projection" $log | ./parse_real_out.sh`
    scan_int64=`grep "Time spent Int64 projection" $log | ./parse_real_out.sh`
    record_result $BUILD_IDENTIFIER ${FS_SCANINSERT_MRS}_insert $i $insert
    record_result $BUILD_IDENTIFIER ${FS_SCANINSERT_MRS}_scan_full $i $scan_full
    record_result $BUILD_IDENTIFIER ${FS_SCANINSERT_MRS}_scan_str $i $scan_str
    record_result $BUILD_IDENTIFIER ${FS_SCANINSERT_MRS}_scan_int32 $i $scan_int32
    record_result $BUILD_IDENTIFIER ${FS_SCANINSERT_MRS}_scan_int64 $i $scan_int64
  done

  # parse scan timings for scans and inserts with disk
  for i in $(seq 1 $NUM_SAMPLES); do
    local log=$LOGDIR/${FS_SCANINSERT_DISK}$i.log
    insert=`grep "Time spent concurrent inserts" $log | ./parse_real_out.sh`
    scan_full=`grep "Time spent full schema scan" $log | ./parse_real_out.sh`
    scan_str=`grep "Time spent String projection" $log | ./parse_real_out.sh`
    scan_int32=`grep "Time spent Int32 projection" $log | ./parse_real_out.sh`
    scan_int64=`grep "Time spent Int64 projection" $log | ./parse_real_out.sh`
    record_result $BUILD_IDENTIFIER ${FS_SCANINSERT_DISK}_insert $i $insert
    record_result $BUILD_IDENTIFIER ${FS_SCANINSERT_DISK}_scan_full $i $scan_full
    record_result $BUILD_IDENTIFIER ${FS_SCANINSERT_DISK}_scan_str $i $scan_str
    record_result $BUILD_IDENTIFIER ${FS_SCANINSERT_DISK}_scan_int32 $i $scan_int32
    record_result $BUILD_IDENTIFIER ${FS_SCANINSERT_DISK}_scan_int64 $i $scan_int64
  done

  popd
  popd
  popd
}

generate_ycsb_plots() {
  local WORKLOAD=$1
  local PHASE=$2
  METRIC_NAME=ycsb-$PHASE-$WORKLOAD

  # first plot the overall stats for that phase
  OVERALL_FILENAME=$METRIC_NAME-OVERALL
  load_stats $OVERALL_FILENAME-runtime_ms > $OUTDIR/$OVERALL_FILENAME-runtime_ms.tsv
  write_img_plot $OUTDIR/$OVERALL_FILENAME-runtime_ms.tsv $OVERALL_FILENAME-runtime_ms
  load_stats $OVERALL_FILENAME-throughput_ops_sec > $OUTDIR/$OVERALL_FILENAME-throughput_ops_sec.tsv
  write_img_plot $OUTDIR/$OVERALL_FILENAME-throughput_ops_sec.tsv $OVERALL_FILENAME-throughput_ops_sec

  # now plot the individual operations
  OPS="INSERT UPDATE READ"

  for op in $OPS; do
    OP_FILENAME=$METRIC_NAME-$op
    load_stats $OP_FILENAME-average_latency_us > $OUTDIR/$OP_FILENAME-average_latency_us.tsv
    write_img_plot $OUTDIR/$OP_FILENAME-average_latency_us.tsv $OP_FILENAME-average_latency_us

    load_stats $OP_FILENAME-95th_latency_ms > $OUTDIR/$OP_FILENAME-95th_latency_ms.tsv
    write_img_plot $OUTDIR/$OP_FILENAME-95th_latency_ms.tsv $OP_FILENAME-95th_latency_ms

    load_stats $OP_FILENAME-99th_latency_ms > $OUTDIR/$OP_FILENAME-99th_latency_ms.tsv
    write_img_plot $OUTDIR/$OP_FILENAME-99th_latency_ms.tsv $OP_FILENAME-99th_latency_ms
  done
}

load_and_generate_plot() {
  local TEST_NAME=$1
  local PLOT_NAME=$2
  load_stats "$TEST_NAME" > $OUTDIR/$PLOT_NAME.tsv
  write_img_plot $OUTDIR/$PLOT_NAME.tsv $PLOT_NAME
}

load_stats_and_generate_plots() {
  pushd src
  pushd kudu
  pushd scripts

  load_and_generate_plot "%MultiThreadedTabletTest%" mt-tablet-test-runtime

  load_and_generate_plot ConcurrentBTreeScanInsert cb-tree-insert
  load_and_generate_plot ConcurrentBTreeScanNotFrozen cb-ctree-not-frozen
  load_and_generate_plot ConcurrentBTreeScanFrozen cb-ctree-frozen

  load_and_generate_plot "${COMPACT_MERGE_BENCH}%" compact-merge-bench

  load_and_generate_plot "${MEMROWSET_BENCH}${INSERT}" memrowset-bench-insert
  load_and_generate_plot "${MEMROWSET_BENCH}Scan%" memrowset-bench-scan

  load_and_generate_plot $BLOOM_TEST bloom-test
  load_and_generate_plot $MT_BLOOM_TEST mt-bloom-test

  load_and_generate_plot $WIRE_PROTOCOL_TEST wire-protocol-test

  load_and_generate_plot $RPC_BENCH_TEST rpc-bench-test

  load_and_generate_plot "${TS_INSERT_LATENCY}%" ts-insert-latency

  load_and_generate_plot "${TS_8THREAD_BENCH}%_latency" ts-8thread-insert-latency
  load_and_generate_plot "${TS_8THREAD_BENCH}%_throughput_%" ts-8thread-insert-throughput

  load_and_generate_plot "${FS_SCANINSERT_MRS}%_insert" fs-mrsonly-insert
  load_and_generate_plot "${FS_SCANINSERT_MRS}%_scan%" fs-mrsonly-scan
  load_and_generate_plot "${FS_SCANINSERT_DISK}%_insert" fs-withdisk-insert
  load_and_generate_plot "${FS_SCANINSERT_DISK}%_scan%" fs-withdisk-scan

  # Generate all the pngs for all the mt-tablet tests
  for i in $(seq 0 $NUM_MT_TABLET_TESTS); do
    cat $LOGDIR/${MT_TABLET_TEST}.log | ./graph-metrics.py MultiThreadedTabletTest/$i > $OUTDIR/test$i.tsv
    # Don't bail on failure (why not?)
    write_mttablet_img_plots $OUTDIR/test$i.tsv test$i || true
  done

  if [ "${BENCHMARK_MODE}" = "${MODE_JENKINS}" ]; then
    ################################################################
    # Plot the separately-recorded TPCH and YCSB graphs as well
    # (only for Jenkins)
    ################################################################

    # TPC-H 1 runs separately, let's just get those graphs
    load_and_generate_plot query_1_1gb tpch1-query
    load_and_generate_plot insert_1gb tpch1-insert

    # YCSB which runs the 5nodes_workload on a cluster
    # First we process the loading phase
    generate_ycsb_plots 5nodes_workload load

    # Then the running phase
    generate_ycsb_plots 5nodes_workload run
  fi

  # Move all the pngs to OUT_DIR.
  mv *.png $OUTDIR/

  # Generate an HTML file aggregating the PNGs.
  # Mostly for local usage, but somewhat useful to check the Jenkins runs too.
  pushd $OUTDIR/
  PNGS=$(ls *.png)
  echo -n > "$OUTDIR/$HTML_FILE"
  echo "<title>Kudu Benchmarks</title>" >> "$OUTDIR/$HTML_FILE"
  echo "<h1 align=center>Kudu Benchmarks</h1>" >> "$OUTDIR/$HTML_FILE"
  for png in $PNGS; do
    echo "<img src=$png><br>" >> "$OUTDIR/$HTML_FILE"
  done
  popd

  popd
  popd
  popd
}

build_run_record() {
  local BUILD_IDENTIFIER=$1
  build_kudu
  run_benchmarks
  parse_and_record_all_results "$BUILD_IDENTIFIER"
}

git_checkout() {
  local GIT_HASH=$1
  git checkout $GIT_HASH
}

run() {

  # Parse command-line options.
  if [ -n "$1" ]; then
    [ "$1" = "-local" ] || usage_and_die
    shift

    BENCHMARK_MODE=$MODE_LOCAL

    # If no hashes are provided, run against the current HEAD.
    if [ -z "$1" ]; then
      build_run_record "working_tree"
    else
      # Convert the passed-in git refs into their hashes.
      # This allows you to use "HEAD~3 HEAD" as arguments
      # and end up with those being evaluated with regard to
      # the _current_ branch, instead of evaluating the second
      # "HEAD" after checking out the first.
      local ref
      local hashes
      for ref in "$@" ; do
        hashes="$hashes $(git rev-parse "$ref")"
      done
      set $hashes
      while [ -n "$1" ]; do
        local GIT_HASH="$1"
        shift
        git_checkout "$GIT_HASH"
        build_run_record "$GIT_HASH"
      done
    fi

  else
    [ -n "$BUILD_NUMBER" ] || usage_and_die
    build_run_record "$BUILD_NUMBER"
  fi

  # The last step is the same for both modes.
  load_stats_and_generate_plots
}

################################################################
# main
################################################################

# Figure out where we are, store in global variables.
BASE_DIR=$(pwd)
LOGDIR="$BASE_DIR/$LOG_DIR_NAME"
OUTDIR="$BASE_DIR/$OUT_DIR_NAME"

# Ensure we are in KUDU_HOME
if [ ! -f "$BASE_DIR/LICENSE.txt" ]; then
  set +x
  echo "Error: must run from top of Kudu source tree"
  usage_and_die
fi

# Set up environment.
ulimit -m $[3000*1000]
ulimit -c unlimited   # gather core dumps

# Set CPU governor, and restore it on exit.
old_governor=$(ensure_cpu_scaling performance)
restore_governor() {
  ensure_cpu_scaling $old_governor >/dev/null
}
trap restore_governor EXIT

# Kick off the benchmark script.
run $*

exit 0
