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

#include <gtest/gtest.h>

#include "yb/docdb/docdb_test_base.h"

#include "yb/util/random_util.h"
#include "yb/util/tsan_util.h"

namespace yb::docdb {

class DocDBPerfTest : public DocDBTestBase {
 public:
  // Size of block cache for RocksDB, 0 means don't use block cache.
  size_t block_cache_size() const override {
    return 64_MB;
  }

  Schema CreateSchema() override {
    return Schema();
  }

  template <class F>
  void TestScanPerformance(
      size_t expected_num_keys, const std::string& perf_test_string, const F& f,
      bool need_warmup = false) {
    constexpr int kIteration = 3;

    auto statistics = regular_db_options().statistics.get();
    auto initial_block_cache_miss = statistics->getTickerCount(rocksdb::BLOCK_CACHE_MISS);

    for (int i = need_warmup ? -1 : 0; i < kIteration; i++) {
      rocksdb::ReadOptions read_opts;
      read_opts.query_id = rocksdb::kDefaultQueryId;
      std::unique_ptr<rocksdb::Iterator> iter(rocksdb()->NewIterator(read_opts));
      auto start = MonoTime::Now();
      iter->SeekToFirst();
      auto result = f(iter.get());
      auto time_taken = MonoTime::Now() - start;
      ASSERT_EQ(expected_num_keys, result.first);
      ASSERT_OK(iter->status());
      auto current_block_cache_miss = statistics->getTickerCount(rocksdb::BLOCK_CACHE_MISS);
      if (i >= 0) {
        // If we have cache misses after warmup please consider increasing block_cache_size.
        ASSERT_EQ(current_block_cache_miss, initial_block_cache_miss);
        LOG(INFO) << "Test - " << perf_test_string << ", time taken - " << time_taken
                  << ", cache miss - " << (current_block_cache_miss - initial_block_cache_miss)
                  << ", total size: " << result.second;
      }
      initial_block_cache_miss = current_block_cache_miss;
    }
  }

  void TestScanForward(
      size_t expected_num_keys, const std::string& perf_test_string, bool use_key_filter_callback,
      Slice upperbound) {
    TestScanPerformance(
        expected_num_keys, perf_test_string,
        [use_key_filter_callback, upperbound](auto* iter) -> std::pair<size_t, size_t> {
          size_t scanned_keys = 0;
          size_t total_size = 0;
          rocksdb::ScanCallback scan_callback = [&scanned_keys, &total_size](
                                                    const Slice& key, const Slice& value) -> bool {
            scanned_keys++;
            total_size += key.size() + value.size();
            return true;
          };

          rocksdb::KeyFilterCallback kf_callback =
              [](Slice prefixed_key, size_t shared_bytes,
                 Slice delta) -> rocksdb::KeyFilterCallbackResult {
            return rocksdb::KeyFilterCallbackResult{.skip_key = false, .cache_key = false};
          };

          auto key_filter_callback = use_key_filter_callback ? &kf_callback : nullptr;
          EXPECT_TRUE(iter->ScanForward(upperbound, key_filter_callback, &scan_callback));

          return {scanned_keys, total_size};
        });
  }

  void TestScanNext(
      size_t expected_num_keys, const std::string& perf_test_string, bool use_fast_next = false) {
    TestScanPerformance(
        expected_num_keys, perf_test_string,
        [use_fast_next](auto* iter) -> std::pair<size_t, size_t> {
      size_t scanned_keys = 0;
      size_t total_size = 0;
      iter->UseFastNext(use_fast_next);
      const auto* entry = &iter->Entry();
      while (*entry) {
        total_size += entry->TotalSize();
        ++scanned_keys;
        entry = &iter->Next();
      }
      return {scanned_keys, total_size};
    }, !use_fast_next);
  }

  template <class GetKey>
  void WriteRows(int count, int flush_count, const GetKey& get_key) {
    int next_flush = flush_count;
    for (int i = 1; i <= count; ++i) {
      ASSERT_OK(WriteSimple(get_key(i)));
      if (i >= next_flush) {
        rocksdb::FlushOptions options;
        options.wait = true;
        ASSERT_OK(rocksdb()->Flush(options));
        next_flush += flush_count;
      }
    }
  }

  void WriteSeqRows(int count, int flush_count) {
    WriteRows(count, flush_count, [](auto i) { return i; });
  }

  void WriteRandomRows(int count, int flush_count) {
    std::vector<int> keys;
    keys.reserve(count);
    for (int i = 1; i <= count; ++i) {
      keys.push_back(i);
    }
    std::shuffle(keys.begin(), keys.end(), ThreadLocalRandom());
    WriteRows(count, flush_count, [&keys](auto i) { return keys[i - 1]; });
  }

  void Scan(int count) {
    dockv::KeyBytes upperbound;
    {
      // Compute Upperbound.
      rocksdb::ReadOptions read_opts;
      read_opts.query_id = rocksdb::kDefaultQueryId;
      std::unique_ptr<rocksdb::Iterator> iter(doc_db().regular->NewIterator(read_opts));
      iter->SeekToLast();
      if (!iter->Valid()) {
        iter->Prev();
      }
      ASSERT_TRUE(iter->Valid());
      ASSERT_OK(iter->status());

      upperbound.Reset(iter->key());
    }

    upperbound.AppendKeyEntryTypeBeforeGroupEnd(dockv::KeyEntryType::kHighest);

    LOG(INFO) << "Wrote " << count << ", validating performance";
    TestScanNext(count, "Next");
    TestScanNext(count, "FastNext", /* use_fast_next */ true);
    TestScanForward(
        count, "ScanForward (no kf and no upperbound)", /* use_filter_callback = */ false,
        Slice());
    TestScanForward(
        count, "ScanForward (with kf and no upperbound)", /* use_filter_callback = */ true,
        Slice());
    TestScanForward(
        count, "ScanForward (with kf+upperbound)", /* use_filter_callback = */ true,
        upperbound.AsSlice());
    TestScanForward(
        count, "ScanForward (with upperbound and no kf)", /* use_filter_callback = */ false,
        upperbound.AsSlice());
  }
};

constexpr int kScanRows = RegularBuildVsDebugVsSanitizers(1000000, 100000, 10000);
constexpr int kScanFlushRows = (kScanRows + 3) / 4;

TEST_F(DocDBPerfTest, YB_DISABLE_TEST_IN_TSAN(ScanForwardVsNextVsFastNext)) {
  WriteSeqRows(kScanRows, kScanRows + 1);
  Scan(kScanRows);
}

TEST_F(DocDBPerfTest, YB_DISABLE_TEST_IN_TSAN(ScanMultipleSst)) {
  WriteSeqRows(kScanRows, kScanFlushRows);
  Scan(kScanRows);
}

TEST_F(DocDBPerfTest, YB_DISABLE_TEST_IN_TSAN(ScanMultipleSstRandomWrite)) {
  WriteRandomRows(kScanRows, kScanFlushRows);
  Scan(kScanRows);
}

}  // namespace yb::docdb
