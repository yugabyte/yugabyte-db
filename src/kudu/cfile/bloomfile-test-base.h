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
#ifndef KUDU_CFILE_BLOOMFILE_TEST_BASE_H
#define KUDU_CFILE_BLOOMFILE_TEST_BASE_H

#include <glog/logging.h>
#include <gtest/gtest.h>

#include "kudu/cfile/bloomfile.h"
#include "kudu/fs/fs_manager.h"
#include "kudu/gutil/endian.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/util/random.h"
#include "kudu/util/random_util.h"
#include "kudu/util/stopwatch.h"
#include "kudu/util/test_util.h"
#include "kudu/util/thread.h"

DEFINE_int32(bloom_size_bytes, 4*1024, "Size of each bloom filter");
DEFINE_int32(n_keys, 10*1000, "Number of keys to insert into the file");
DEFINE_double(fp_rate, 0.01f, "False positive rate to aim for");

DEFINE_int64(benchmark_queries, 1000000, "Number of probes to benchmark");
DEFINE_bool(benchmark_should_hit, false, "Set to true for the benchmark to query rows which match");

namespace kudu {
namespace cfile {

using fs::ReadableBlock;
using fs::WritableBlock;

static const int kKeyShift = 2;

class BloomFileTestBase : public KuduTest {
 public:
  void SetUp() OVERRIDE {
    KuduTest::SetUp();

    fs_manager_.reset(new FsManager(env_.get(), GetTestPath("fs_root")));
    ASSERT_OK(fs_manager_->CreateInitialFileSystemLayout());
    ASSERT_OK(fs_manager_->Open());
  }

  void AppendBlooms(BloomFileWriter *bfw) {
    uint64_t key_buf;
    Slice key_slice(reinterpret_cast<const uint8_t *>(&key_buf),
                    sizeof(key_buf));

    for (uint64_t i = 0; i < FLAGS_n_keys; i++) {
      // Shift the key left a bit so that while querying, we can
      // get a good mix of hits and misses while still staying within
      // the real key range.
      key_buf = BigEndian::FromHost64(i << kKeyShift);
      ASSERT_OK_FAST(bfw->AppendKeys(&key_slice, 1));
    }
  }

  void WriteTestBloomFile() {
    gscoped_ptr<WritableBlock> sink;
    ASSERT_OK(fs_manager_->CreateNewBlock(&sink));
    block_id_ = sink->id();

    // Set sizing based on flags
    BloomFilterSizing sizing = BloomFilterSizing::BySizeAndFPRate(
      FLAGS_bloom_size_bytes, FLAGS_fp_rate);
    ASSERT_NEAR(sizing.n_bytes(), FLAGS_bloom_size_bytes, FLAGS_bloom_size_bytes * 0.05);
    ASSERT_GT(FLAGS_n_keys, sizing.expected_count())
      << "Invalid parameters: --n_keys isn't set large enough to fill even "
      << "one bloom filter of the requested --bloom_size_bytes";

    BloomFileWriter bfw(sink.Pass(), sizing);

    ASSERT_OK(bfw.Start());
    AppendBlooms(&bfw);
    ASSERT_OK(bfw.Finish());
  }

  Status OpenBloomFile() {
    gscoped_ptr<ReadableBlock> source;
    RETURN_NOT_OK(fs_manager_->OpenBlock(block_id_, &source));

    return BloomFileReader::Open(source.Pass(), ReaderOptions(), &bfr_);
  }

  uint64_t ReadBenchmark() {
    Random rng(GetRandomSeed32());
    uint64_t count_present = 0;
    LOG_TIMING(INFO, strings::Substitute("Running $0 queries", FLAGS_benchmark_queries)) {

      for (uint64_t i = 0; i < FLAGS_benchmark_queries; i++) {
        uint64_t key = rng.Uniform(FLAGS_n_keys);
        key <<= kKeyShift;
        if (!FLAGS_benchmark_should_hit) {
          // Since the keys are bitshifted, setting the last bit
          // ensures that none of the queries will match.
          key |= 1;
        }

        key = BigEndian::FromHost64(key);

        Slice s(reinterpret_cast<uint8_t *>(&key), sizeof(key));
        bool present;
        CHECK_OK(bfr_->CheckKeyPresent(BloomKeyProbe(s), &present));
        if (present) count_present++;
      }
    }
    return count_present;
  }

 protected:
  gscoped_ptr<FsManager> fs_manager_;
  gscoped_ptr<BloomFileReader> bfr_;
  BlockId block_id_;
};

} // namespace cfile
} // namespace kudu

#endif
