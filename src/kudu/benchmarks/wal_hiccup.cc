// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.

#include <algorithm>
#include <vector>

#include <fcntl.h>

#include <gflags/gflags.h>
#include <glog/logging.h>
#include "kudu/gutil/strings/substitute.h"
#include "kudu/gutil/stl_util.h"
#include "kudu/gutil/walltime.h"
#include "kudu/util/countdown_latch.h"
#include "kudu/util/hdr_histogram.h"
#include "kudu/util/logging.h"
#include "kudu/util/path_util.h"
#include "kudu/util/stopwatch.h"
#include "kudu/util/thread.h"

DEFINE_int32(num_files, 40, "number of files to write");
DEFINE_int32(file_size_mb, 16, "size of each file");
DEFINE_int32(num_rounds, 1, "number of times to try each setup");
DEFINE_int32(wal_interval_us, 1000, "number of microseconds to sleep between WAL writes");
DEFINE_string(file_path, "", "path where all files are written; defaults to cwd");

DEFINE_bool(initiate_writeback_each_file, false,
            "SYNC_FILE_RANGE_WRITE each file before closing it");
DEFINE_bool(await_writeback_each_file, false,
            "SYNC_FILE_RANGE_WAIT_BEFORE each file before closing it");
DEFINE_bool(fdatasync_each_file, false,
            "fdatasync() each file before closing it");

DEFINE_bool(initiate_writeback_at_end, false,
            "SYNC_FILE_RANGE_WRITE each file after writing all files");
DEFINE_bool(await_writeback_at_end, false,
            "SYNC_FILE_RANGE_WAIT_BEFORE each file after writing all files");
DEFINE_bool(fdatasync_at_end, true,
            "fdatasync each file after writing all files");

DEFINE_bool(page_align_wal_writes, false,
            "write to the fake WAL with exactly 4KB writes to never cross pages");

using std::string;

namespace kudu {

class WalHiccupBenchmarker {
 public:
  WalHiccupBenchmarker()
    : finished_(1),
      cur_histo_(NULL) {
  }
  ~WalHiccupBenchmarker() {
    STLDeleteElements(&wal_histos_);
  }

  void WALThread();
  void PrintConfig();
  void RunOnce();
  void Run();
 protected:
  CountDownLatch finished_;
  std::vector<HdrHistogram*> wal_histos_;
  HdrHistogram* cur_histo_;
};


void WalHiccupBenchmarker::WALThread() {
  string name = "wal";
  if (!FLAGS_file_path.empty()) {
    name = JoinPathSegments(FLAGS_file_path, name);
  }
  int fd = open(name.c_str(), O_WRONLY | O_TRUNC | O_CREAT, 0666);
  PCHECK(fd >= 0) << "open() failed";
  char buf[4096];
  memset(buf, 0xFF, sizeof(buf));
  const MonoDelta sleepDelta = MonoDelta::FromMicroseconds(FLAGS_wal_interval_us);
  while (finished_.count() > 0) {
    SleepFor(sleepDelta);
    MicrosecondsInt64 st = GetCurrentTimeMicros();
    size_t num_bytes = FLAGS_page_align_wal_writes ? sizeof(buf) : sizeof(buf) - 1;
    PCHECK(write(fd, buf, num_bytes) == num_bytes);
    PCHECK(fdatasync(fd) == 0);
    MicrosecondsInt64 et = GetCurrentTimeMicros();
    MicrosecondsInt64 value = et - st;
    cur_histo_->IncrementWithExpectedInterval(value, FLAGS_wal_interval_us);
    if (value > FLAGS_wal_interval_us) {
      LOG(WARNING) << "slow wal write: " <<  value << "us";
    }
  }
}

void WriteFile(const string& name,
               bool initiate_writeback,
               bool wait_writeback,
               bool datasync,
               int* fd) {
  string full_name = name;
  if (!FLAGS_file_path.empty()) {
    full_name = JoinPathSegments(FLAGS_file_path, full_name);
  }
  *fd = open(full_name.c_str(), O_WRONLY|O_TRUNC|O_CREAT, 0666);
  PCHECK(*fd >= 0) << "open() failed";

  char buf[1024];
  memset(buf, 0xFF, sizeof(buf));
  for (int i = 0; i < FLAGS_file_size_mb; i++) {
    for (int j = 0; j < 1024; j++) {
      PCHECK(write(*fd, buf, sizeof(buf)) > 0);
    }
  }

  if (initiate_writeback) {
    PCHECK(sync_file_range(*fd, 0, 0, SYNC_FILE_RANGE_WRITE) == 0);
  }

  if (wait_writeback) {
    PCHECK(sync_file_range(*fd, 0, 0, SYNC_FILE_RANGE_WAIT_BEFORE) == 0);
  }

  if (datasync) {
    PCHECK(fdatasync(*fd) == 0);
  }
}

void SetFlags(uint32_t setup) {
  FLAGS_initiate_writeback_each_file = setup & (1 << 0);
  FLAGS_await_writeback_each_file = setup & (1 << 1);
  FLAGS_fdatasync_each_file = setup & (1 << 2);

  FLAGS_initiate_writeback_at_end = setup & (1 << 3);
  FLAGS_await_writeback_at_end = setup & (1 << 4);
  FLAGS_fdatasync_at_end = setup & (1 << 5);

  FLAGS_page_align_wal_writes = setup & (1 << 6);
}

void WalHiccupBenchmarker::Run() {
  int num_setups = 1 << 7;
  wal_histos_.resize(num_setups);

  vector<double> total_time;
  total_time.resize(num_setups);

  vector<uint32_t> setups;
  setups.reserve(num_setups);
  for (uint32_t setup = 0; setup < num_setups; setup++) {
    setups.push_back(setup);
  }

  for (int round = 0; round < FLAGS_num_rounds; round++) {
    // Randomize the order of setups in each round.
    std::random_shuffle(setups.begin(), setups.end());

    for (uint32_t setup : setups) {
      SetFlags(setup);
      if (!FLAGS_fdatasync_each_file && !FLAGS_fdatasync_at_end) {
        // Skip non-durable configuration
        continue;
      }

      LOG(INFO) << "----------------------------------------------------------------------";
      LOG(INFO) << "Continuing setup " << setup << ":";
      PrintConfig();
      LOG(INFO) << "----------------------------------------------------------------------";

      sync();
      if (wal_histos_[setup] == NULL) {
        wal_histos_[setup] = new HdrHistogram(1000000, 4);
      }
      cur_histo_ = wal_histos_[setup];

      Stopwatch s;
      s.start();
      RunOnce();
      s.stop();
      total_time[setup] += s.elapsed().wall_seconds();
    }
    LOG(INFO) << "----------------------------------------------------------------------";
    LOG(INFO) << "Ran " << setups.size() << " setups";
    LOG(INFO) << "----------------------------------------------------------------------";
  }

  for (uint32_t setup : setups) {
    SetFlags(setup);
    if (!FLAGS_fdatasync_each_file && !FLAGS_fdatasync_at_end) {
      // Skip non-durable configuration
      continue;
    }

    cur_histo_ = wal_histos_[setup];

    double throughput = (FLAGS_num_rounds * FLAGS_num_files * FLAGS_file_size_mb) /
      total_time[setup];

    LOG(INFO) << "----------------------------------------------------------------------";
    LOG(INFO) << "Test results for setup " << setup << ":";
    PrintConfig();
    LOG(INFO);
    LOG(INFO) << "throughput: " << throughput;
    LOG(INFO) << "p95: " << cur_histo_->ValueAtPercentile(95.0);
    LOG(INFO) << "p99: " << cur_histo_->ValueAtPercentile(99.0);
    LOG(INFO) << "p99.99: " << cur_histo_->ValueAtPercentile(99.99);
    LOG(INFO) << "max: " << cur_histo_->MaxValue();
    LOG(INFO) << "----------------------------------------------------------------------";
  }

}

void WalHiccupBenchmarker::PrintConfig() {
  LOG(INFO) << "initiate_writeback_each_file: " << FLAGS_initiate_writeback_each_file;
  LOG(INFO) << "await_writeback_each_file: " << FLAGS_await_writeback_each_file;
  LOG(INFO) << "fdatasync_each_file: " << FLAGS_fdatasync_each_file;
  LOG(INFO) << "initiate_writeback_at_end: " << FLAGS_initiate_writeback_at_end;
  LOG(INFO) << "await_writeback_at_end: " << FLAGS_await_writeback_at_end;
  LOG(INFO) << "fdatasync_at_end: " << FLAGS_fdatasync_at_end;
  LOG(INFO) << "page_align_wal_writes: " << FLAGS_page_align_wal_writes;
}

void WalHiccupBenchmarker::RunOnce() {
  finished_.Reset(1);
  scoped_refptr<Thread> thr;
  CHECK_OK(Thread::Create("test", "wal", &WalHiccupBenchmarker::WALThread, this, &thr));


  int fds[FLAGS_num_files];
  for (int i = 0; i < FLAGS_num_files; i++) {
    WriteFile(strings::Substitute("file-$0", i),
              FLAGS_initiate_writeback_each_file,
              FLAGS_await_writeback_each_file,
              FLAGS_fdatasync_each_file,
              &fds[i]);
  }

  LOG(INFO) << "Done writing...";
  if (FLAGS_initiate_writeback_at_end) {
    LOG(INFO) << "Post-write initiating writeback...";
    for (int i = 0; i < FLAGS_num_files; i++) {
      PCHECK(sync_file_range(fds[i], 0, 0, SYNC_FILE_RANGE_WRITE) == 0);
    }
  }
  if (FLAGS_await_writeback_at_end) {
    LOG(INFO) << "Post-write awaiting writeback...";
    for (int i = 0; i < FLAGS_num_files; i++) {
      PCHECK(sync_file_range(fds[i], 0, 0, SYNC_FILE_RANGE_WAIT_BEFORE) == 0);
    }
  }
  if (FLAGS_fdatasync_at_end) {
    LOG(INFO) << "Post-write fdatasync...";
    for (int i = 0; i < FLAGS_num_files; i++) {
      PCHECK(fdatasync(fds[i]) == 0);
    }
  }
  for (int i = 0; i < FLAGS_num_files; i++) {
    PCHECK(close(fds[i]) == 0);
  }

  LOG(INFO) << "Done closing...";
  finished_.CountDown();
  thr->Join();
}

} // namespace kudu

int main(int argc, char* argv[]) {
  google::ParseCommandLineFlags(&argc, &argv, true);
  kudu::InitGoogleLoggingSafe(argv[0]);

  kudu::WalHiccupBenchmarker benchmarker;
  benchmarker.Run();

  return 0;
}
