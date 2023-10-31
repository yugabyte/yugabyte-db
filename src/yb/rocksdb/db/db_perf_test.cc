// Copyright (c) YugaByte, Inc.
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

#include "yb/rocksdb/db/db_test_util.h"

#include "yb/rocksdb/port/stack_trace.h"

#include "yb/rocksutil/yb_rocksdb_logger.h"

#include "yb/util/stopwatch.h"

DECLARE_uint64(rocksdb_check_sst_file_tail_for_zeros);
DECLARE_bool(TEST_simulate_fully_zeroed_file);

namespace rocksdb {

class DbPerfTest : public DBTestBase {
 public:
  DbPerfTest() : DBTestBase("/db_perf_test") {}
};

TEST_F(DbPerfTest, YB_DISABLE_TEST(SstTailZerosCheckFlushPerf)) {
  constexpr auto kNumKeys = 1000000;
  constexpr auto kNumIters = 3;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_check_sst_file_tail_for_zeros) = 1_KB;

  auto options = CurrentOptions();
  // Put some size huge enough to prevent auto flush.
  options.write_buffer_size = 200_MB;
  options.info_log_level = InfoLogLevel::INFO_LEVEL;
  options.info_log = std::make_shared<yb::YBRocksDBLogger>(options.log_prefix);

  for (int i = 0; i < kNumIters * 2; ++i) {
    DestroyAndReopen(options);

    Random rnd{301};
    for (int j = 0; j < kNumKeys; j++) {
      auto val = RandomString(&rnd, 100);
      CHECK_OK(Put(Key(j), val));
    }

    const auto zeroed_file = i % 2 > 0;

    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_simulate_fully_zeroed_file) = zeroed_file;

    yb::Stopwatch s(yb::Stopwatch::ALL_THREADS);
    s.start();
    ASSERT_EQ(!zeroed_file, Flush().ok());
    s.stop();

    LOG(INFO) << "Flush took: " << s.elapsed().ToString() << " zeroed_file: " << zeroed_file;
  }
}

}  // namespace rocksdb

int main(int argc, char** argv) {
  rocksdb::port::InstallStackTraceHandler();
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
