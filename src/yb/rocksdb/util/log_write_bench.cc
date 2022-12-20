//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License.  You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied.  See the License for the specific language governing permissions and limitations
// under the License.
//

#ifndef GFLAGS
#include <cstdio>
int main() {
  fprintf(stderr, "Please install gflags to run rocksdb tools\n");
  return 1;
}
#else

#include "yb/util/flags.h"

#include "yb/rocksdb/env.h"
#include "yb/rocksdb/util/histogram.h"
#include "yb/rocksdb/util/testharness.h"
#include "yb/rocksdb/util/testutil.h"

using GFLAGS::ParseCommandLineFlags;
using GFLAGS::SetUsageMessage;

// A simple benchmark to simulate transactional logs

DEFINE_UNKNOWN_int32(num_records, 6000, "Number of records.");
DEFINE_UNKNOWN_int32(record_size, 249, "Size of each record.");
DEFINE_UNKNOWN_int32(record_interval, 10000, "Interval between records (microSec)");
DEFINE_UNKNOWN_int32(bytes_per_sync, 0, "bytes_per_sync parameter in EnvOptions");
DEFINE_UNKNOWN_bool(enable_sync, false, "sync after each write.");

namespace rocksdb {
void RunBenchmark() {
  std::string file_name = test::TmpDir() + "/log_write_benchmark.log";
  Env* env = Env::Default();
  EnvOptions env_options;
  env_options.use_mmap_writes = false;
  env_options.bytes_per_sync = FLAGS_bytes_per_sync;
  unique_ptr<WritableFile> file;
  env->NewWritableFile(file_name, &file, env_options);

  std::string record;
  record.assign('X', FLAGS_record_size);

  HistogramImpl hist;

  uint64_t start_time = env->NowMicros();
  for (int i = 0; i < FLAGS_num_records; i++) {
    uint64_t start_nanos = env->NowNanos();
    file->Append(record);
    file->Flush();
    if (FLAGS_enable_sync) {
      file->Sync();
    }
    hist.Add(env->NowNanos() - start_nanos);

    if (i % 1000 == 1) {
      fprintf(stderr, "Wrote %d records...\n", i);
    }

    int time_to_sleep =
        (i + 1) * FLAGS_record_interval - (env->NowMicros() - start_time);
    if (time_to_sleep > 0) {
      env->SleepForMicroseconds(time_to_sleep);
    }
  }

  fprintf(stderr, "Distribution of latency of append+flush: \n%s",
          hist.ToString().c_str());
}
}  // namespace rocksdb

int main(int argc, char** argv) {
  SetUsageMessage(std::string("\nUSAGE:\n") + std::string(argv[0]) +
                  " [OPTIONS]...");
  ParseCommandLineFlags(&argc, &argv, true);

  rocksdb::RunBenchmark();
  return 0;
}

#endif  // GFLAGS
