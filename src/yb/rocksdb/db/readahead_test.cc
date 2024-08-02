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

#include "yb/rocksdb/env.h"

#include "yb/rocksutil/yb_rocksdb_logger.h"

#include "yb/util/compare_util.h"
#include "yb/util/size_literals.h"

DECLARE_uint64(rocksdb_iterator_sequential_disk_reads_for_auto_readahead);
DECLARE_uint64(rocksdb_iterator_init_readahead_size);
DECLARE_uint64(rocksdb_iterator_max_readahead_size);
DECLARE_bool(TEST_rocksdb_record_readahead_stats_only_for_data_blocks);

using namespace std::literals;

namespace rocksdb {

struct ReadaheadStats {
  void AddReadaheadCall(size_t bytes_read) {
    ++readahead_calls;
    readahead_bytes_read += bytes_read;
  }

  void IncreaseWindowAndAddReadaheadCall(size_t* readahead_size) {
    *readahead_size =
        std::min<size_t>(*readahead_size * 2, FLAGS_rocksdb_iterator_max_readahead_size);
    ++readahead_calls;
    readahead_bytes_read += *readahead_size;
  }

  void AddReadaheadReset(size_t* readahead_size) {
    LOG(INFO) << "Expecting reset of readahead";
    *readahead_size = FLAGS_rocksdb_iterator_init_readahead_size;
    ++readahead_reset;
  }

  std::string ToString() const {
    return YB_STRUCT_TO_STRING(readahead_calls, readahead_bytes_read, readahead_reset);
  }

  uint64_t readahead_calls = 0;
  uint64_t readahead_bytes_read = 0;
  uint64_t readahead_reset = 0;
};

inline bool operator==(const ReadaheadStats& lhs, const ReadaheadStats& rhs) {
  return YB_STRUCT_EQUALS(readahead_calls, readahead_bytes_read, readahead_reset);
}

class TestRandomAccessFile;

class TestEnv : public EnvWrapper {
 public:
  explicit TestEnv(Env* target) : EnvWrapper(target) {}

  Status NewRandomAccessFile(
      const std::string& f, std::unique_ptr<RandomAccessFile>* r,
      const EnvOptions& soptions) override;

  void OnRandomAccessFileDestroy(TestRandomAccessFile* file);

  TestRandomAccessFile* GetRandomAccessFile(const std::string& filename) {
    std::lock_guard<std::mutex> l(mutex_);
    auto it = random_access_files_.find(filename);
    if (it == random_access_files_.end()) {
      LOG(ERROR) << "Filenames in TestEnv:";
      for (auto file : random_access_files_) {
        LOG(ERROR) << file.first;
      }
      LOG(FATAL) << "No '" << filename << "' found in TestEnv";
    }
    return it->second;
  }

 private:
  std::mutex mutex_;
  std::unordered_map<std::string, TestRandomAccessFile*> random_access_files_;
};

class TestRandomAccessFile : public yb::RandomAccessFileWrapper {
 public:
  TestRandomAccessFile(std::unique_ptr<RandomAccessFile> target, TestEnv* env)
      : RandomAccessFileWrapper(std::move(target)), env_(env) {}

  ~TestRandomAccessFile() {
    env_->OnRandomAccessFileDestroy(this);
  }

  Status Read(uint64_t offset, size_t n, Slice* result, uint8_t* scratch) const override {
    auto s = target()->Read(offset, n, result, scratch);
    last_read_offset.store(offset);
    last_read_length.store(result->size());
    read_count.fetch_add(1);
    LOG(INFO) << "Disk read at: " << offset << " n: " << n << " filename: " << target()->filename();
    return s;
  }

  void Readahead(size_t offset, size_t length) override {
    target()->Readahead(offset, length);
    last_readahead_offset.store(offset);
    last_readahead_length.store(length);
  }

  size_t GetReadaheadLimit() const {
    return last_readahead_offset + last_readahead_length;
  }

  size_t GetLastReadEnd() const {
    return last_read_offset + last_read_length;
  }

  TestEnv* env_;
  mutable std::atomic<uint64_t> read_count;
  mutable std::atomic<uint64_t> last_read_offset;
  mutable std::atomic<size_t> last_read_length;
  mutable std::atomic<size_t> last_readahead_offset;
  mutable std::atomic<size_t> last_readahead_length;
};

Status TestEnv::NewRandomAccessFile(
    const std::string& f, std::unique_ptr<RandomAccessFile>* r,
    const EnvOptions& soptions) {
  RETURN_NOT_OK(target()->NewRandomAccessFile(f, r, soptions));
  auto* file = new TestRandomAccessFile(std::move(*r), this);
  r->reset(file);
  std::lock_guard<std::mutex> l(mutex_);
  random_access_files_[file->filename()] = file;
  return Status::OK();
}

void TestEnv::OnRandomAccessFileDestroy(TestRandomAccessFile* file) {
  std::lock_guard<std::mutex> l(mutex_);
  random_access_files_.erase(file->filename());
}

class ReadaheadTest : public DBTestBase {
 public:
  ReadaheadTest() :
      DBTestBase("/readahead_test"), rnd_(301) {
    FLAGS_TEST_rocksdb_record_readahead_stats_only_for_data_blocks = true;
    FLAGS_rocksdb_iterator_init_readahead_size = kBlockSize * 4;
    FLAGS_rocksdb_iterator_max_readahead_size = kBlockSize * 32;

    num_keys_ = static_cast<int>(FLAGS_rocksdb_iterator_max_readahead_size * 10 / kValueSize);

    test_env_ = std::make_unique<TestEnv>(env_);

    BlockBasedTableOptions table_options;
    table_options.block_size = kBlockSize;
    table_options.index_type = IndexType::kMultiLevelBinarySearch;
    table_options.index_block_size = kBlockSize;
    table_factory_.reset(new BlockBasedTableFactory(table_options));
  }

  ~ReadaheadTest() {
    // Close DB so we can destroy of test_env_.
    Close();
  }

  Options CurrentOptions() {
    Options options = DBTestBase::CurrentOptions();
    options.env = test_env_.get();
    options.compaction_style = kCompactionStyleNone;
    options.num_levels = 1;
    options.statistics = rocksdb::CreateDBStatisticsForTests();
    options.info_log = std::make_shared<yb::YBRocksDBLogger>(options.log_prefix);
    // Large enough to prevent auto-flush.
    options.write_buffer_size = 1024_MB;

    options.table_factory = table_factory_;

    return options;
  }

  Status WriteData() {
    for (int k = 0; k < num_keys_; ++k) {
      RETURN_NOT_OK(Put(Key(k), RandomString(&rnd_, kValueSize)));
    }
    RETURN_NOT_OK(Flush());

    auto live_files_meta = db_->GetLiveFilesMetaData();
    SCHECK_EQ(live_files_meta.size(), 1, InternalError, "Expected single SST file");
    sst_metadata_ = live_files_meta.front();

    TablePropertiesCollection props;
    RETURN_NOT_OK(db_->GetPropertiesOfAllTables(&props));
    SCHECK_EQ(props.size(), 1, InternalError, "Expected single SST file");
    sst_props_ = *props.begin()->second;

    avg_compressed_data_block_size_ =  sst_props_.data_size / sst_props_.num_data_blocks;
    LOG(INFO) << "avg_compressed_data_block_size: " << avg_compressed_data_block_size_;
    avg_keys_per_block_ = num_keys_ / sst_props_.num_data_blocks;

    return Status::OK();
  }

  void PurgeBlockCache() {
    auto* block_cache = table_factory_->table_options().block_cache.get();
    auto capacity = block_cache->GetCapacity();
    block_cache->SetCapacity(0);
    block_cache->SetCapacity(capacity);
    LOG(INFO) << "Purged block cache";
  }

  ReadaheadStats GetReadaheadStats() {
    auto* stats = last_options_.statistics.get();
    return ReadaheadStats {
      .readahead_calls = stats->getTickerCount(Tickers::READAHEAD_CALLS),
      .readahead_bytes_read = stats->getTickerCount(Tickers::READAHEAD_BYTES_READ),
      .readahead_reset = stats->getTickerCount(Tickers::READAHEAD_RESET),
    };
  }

  Status ExpectReadaheadStats(const ReadaheadStats& expected) {
    SCHECK_EQ(GetReadaheadStats(), expected, InternalError, "Unexpected stats");
    LOG(INFO) << "Readahead stats: " << expected.ToString();
    return Status::OK();
  }

  Status SeekToKey(const std::unique_ptr<Iterator>& iter, int key_idx) {
    LOG(INFO) << "Seeking to key_idx: " << key_idx;
    iter->Seek(Key(key_idx));
    SCHECK(VERIFY_RESULT(iter->CheckedValid()), InternalError, "Iterator is not valid");
    return Status::OK();
  }

  // Returns true if reached end of readahead window and false if reached end of data.
  Result<bool> ReadOneKeyPerBlockUntilOutOfReadaheadWindow(
      const std::unique_ptr<Iterator>& iter, TestRandomAccessFile* data_file,
      int* current_key_idx) {
    auto stats = GetReadaheadStats();
    auto readahead_limit = data_file->GetReadaheadLimit();
    for(;;) {
      if (data_file->GetLastReadEnd() > readahead_limit) {
        break;
      }
      RETURN_NOT_OK(ExpectReadaheadStats(stats));
      *current_key_idx += avg_keys_per_block_;
      if (*current_key_idx >= num_keys_) {
        return false;
      }
      RETURN_NOT_OK(SeekToKey(iter, *current_key_idx));
    }
    return true;
  }

  static constexpr auto kBlockSize = 8_KB;
  static constexpr auto kValueSize = kBlockSize / 16;

  int num_keys_;

  Random rnd_;
  std::unique_ptr<TestEnv> test_env_;
  std::shared_ptr<BlockBasedTableFactory> table_factory_;

  std::optional<LiveFileMetaData> sst_metadata_;
  TableProperties sst_props_;
  size_t avg_compressed_data_block_size_;
  int avg_keys_per_block_;
};

namespace {

void AddWholeFileReadaheads(size_t file_size, size_t* readahead_calls, size_t* readahead_bytes) {
  size_t readahead_size = FLAGS_rocksdb_iterator_init_readahead_size;
  for (;;) {
    ++*readahead_calls;
    *readahead_bytes += readahead_size;
    if (file_size <= readahead_size) {
      break;
    }
    file_size -= readahead_size;
    readahead_size =
        std::min<size_t>(readahead_size * 2, FLAGS_rocksdb_iterator_max_readahead_size);
  }
}

} // namespace

TEST_F(ReadaheadTest, SequentialScan) {
  Options options = CurrentOptions();
  Reopen(options);

  ASSERT_OK(WriteData());

  for (auto seq_disk_reads_for_readahead : {0, 1, 2, 3, 4, 5, 8, 16}) {
    LOG(INFO) << "Setting FLAGS_rocksdb_iterator_sequential_disk_reads_for_auto_readahead = "
              << seq_disk_reads_for_readahead;
    FLAGS_rocksdb_iterator_sequential_disk_reads_for_auto_readahead = seq_disk_reads_for_readahead;
    for (bool purge_block_cache : {true, false}) {
      if (purge_block_cache) {
        PurgeBlockCache();
      }

      auto* stats = options.statistics.get();
      stats->resetTickersForTest();

      auto iter = std::unique_ptr<Iterator>(db_->NewIterator(ReadOptions()));
      size_t num_keys_read = 0;
      for (iter->SeekToFirst(); ASSERT_RESULT(iter->CheckedValid());
           iter->Next(), ++num_keys_read) {
        if ((seq_disk_reads_for_readahead > 1) &&
            (num_keys_read ==
             (seq_disk_reads_for_readahead - 1) * num_keys_ / sst_props_.num_data_blocks)) {
          LOG(INFO) << "num_keys: " << num_keys_
                    << " num_data_blocks: " << sst_props_.num_data_blocks
                    << " num_keys_read: " << num_keys_read;
          // We are about to reach seq_disk_reads_for_readahead disk reads. Should be no readaheads
          // till now.
          ASSERT_OK(ExpectReadaheadStats(ReadaheadStats()));
        }
      }

      size_t expected_num_readaheads = 0;
      size_t expected_readahead_bytes_read = 0;
      if (seq_disk_reads_for_readahead > 0 && purge_block_cache) {
        const auto bytes_should_read_before_readahead =
            (seq_disk_reads_for_readahead - 1) * avg_compressed_data_block_size_;
        const auto data_size = sst_props_.data_size;
        if (data_size > bytes_should_read_before_readahead) {
          AddWholeFileReadaheads(
              data_size - bytes_should_read_before_readahead, &expected_num_readaheads,
              &expected_readahead_bytes_read);
        }
        LOG(INFO) << " data_size: " << data_size
                  << " bytes_should_read_before_readahead: " << bytes_should_read_before_readahead;
      }

      const auto num_readaheads = stats->getTickerCount(Tickers::READAHEAD_CALLS);
      const auto readahead_bytes_read = stats->getTickerCount(Tickers::READAHEAD_BYTES_READ);

      ASSERT_GE(num_readaheads, expected_num_readaheads);
      ASSERT_GE(readahead_bytes_read, expected_readahead_bytes_read);

      // We can readahead more in reality due to blocks located on readahead window boundary.
      ASSERT_LE(num_readaheads, expected_num_readaheads * 1.1);
      ASSERT_LE(
          readahead_bytes_read,
          expected_readahead_bytes_read + (num_readaheads - expected_num_readaheads) *
                                              FLAGS_rocksdb_iterator_max_readahead_size);

      ASSERT_EQ(stats->getTickerCount(Tickers::READAHEAD_RESET), 0);
    }
  }
}

TEST_F(ReadaheadTest, MixedReadsWith1SeqDiskReadsForReadahead) {
  FLAGS_rocksdb_iterator_sequential_disk_reads_for_auto_readahead = 1;

  Options options = CurrentOptions();
  Reopen(options);

  ASSERT_OK(WriteData());

  TestRandomAccessFile* data_file = test_env_->GetRandomAccessFile(sst_metadata_->DataFilePath());

  auto iter = std::unique_ptr<Iterator>(db_->NewIterator(ReadOptions()));

  size_t expected_readahead_size = FLAGS_rocksdb_iterator_init_readahead_size;
  ReadaheadStats expected_stats;

  int current_key_idx = 0;
  ASSERT_OK(SeekToKey(iter, current_key_idx));

  expected_stats.AddReadaheadCall(expected_readahead_size);
  ASSERT_OK(ExpectReadaheadStats(expected_stats));

  ASSERT_OK(ReadOneKeyPerBlockUntilOutOfReadaheadWindow(iter, data_file, &current_key_idx));

  expected_stats.IncreaseWindowAndAddReadaheadCall(&expected_readahead_size);
  ASSERT_OK(ExpectReadaheadStats(expected_stats));

  ASSERT_OK(ReadOneKeyPerBlockUntilOutOfReadaheadWindow(iter, data_file, &current_key_idx));

  expected_stats.IncreaseWindowAndAddReadaheadCall(&expected_readahead_size);
  ASSERT_OK(ExpectReadaheadStats(expected_stats));

  constexpr auto kBlocksToJump = 4;

  // Jump forward.
  current_key_idx += kBlocksToJump * avg_keys_per_block_;
  ASSERT_OK(SeekToKey(iter, current_key_idx));

  expected_stats.AddReadaheadReset(&expected_readahead_size);
  expected_stats.AddReadaheadCall(expected_readahead_size);
  ASSERT_OK(ExpectReadaheadStats(expected_stats));

  ASSERT_OK(ReadOneKeyPerBlockUntilOutOfReadaheadWindow(iter, data_file, &current_key_idx));
  expected_stats.IncreaseWindowAndAddReadaheadCall(&expected_readahead_size);
  ASSERT_OK(ExpectReadaheadStats(expected_stats));

  // Jump backward.
  current_key_idx -= kBlocksToJump * avg_keys_per_block_;
  ASSERT_OK(SeekToKey(iter, current_key_idx));

  // No disk reads, served from block cache but still should reset readahead.
  expected_stats.AddReadaheadReset(&expected_readahead_size);
  ASSERT_OK(ExpectReadaheadStats(expected_stats));

  // Read next blocks, served from block cache, no disk reads => no readahead.
  for (int i = 0; i < kBlocksToJump; ++i) {
    current_key_idx += avg_keys_per_block_;
    ASSERT_OK(SeekToKey(iter, current_key_idx));
    ASSERT_OK(ExpectReadaheadStats(expected_stats));
  }

  PurgeBlockCache();

  // Read next block after purging block cache, should do readahead.
  current_key_idx += avg_keys_per_block_;
  ASSERT_OK(SeekToKey(iter, current_key_idx));
  expected_stats.AddReadaheadCall(expected_readahead_size);
  ASSERT_OK(ExpectReadaheadStats(expected_stats));

  ASSERT_OK(ReadOneKeyPerBlockUntilOutOfReadaheadWindow(iter, data_file, &current_key_idx));
  expected_stats.IncreaseWindowAndAddReadaheadCall(&expected_readahead_size);
  ASSERT_OK(ExpectReadaheadStats(expected_stats));
}

TEST_F(ReadaheadTest, MixedReads) {
  constexpr auto kNumRandomSeeks = 200;

  Options options = CurrentOptions();
  Reopen(options);

  ASSERT_OK(WriteData());

  TestRandomAccessFile* data_file = test_env_->GetRandomAccessFile(sst_metadata_->DataFilePath());

  for (auto seq_disk_reads_for_readahead : {2, 3, 4, 5, 8, 16}) {
    LOG(INFO) << "Setting FLAGS_rocksdb_iterator_sequential_disk_reads_for_auto_readahead = "
              << seq_disk_reads_for_readahead;
    FLAGS_rocksdb_iterator_sequential_disk_reads_for_auto_readahead = seq_disk_reads_for_readahead;
    PurgeBlockCache();

    auto* stats = options.statistics.get();
    stats->resetTickersForTest();

    auto iter = std::unique_ptr<Iterator>(db_->NewIterator(ReadOptions()));

    size_t expected_readahead_size = FLAGS_rocksdb_iterator_init_readahead_size;
    ReadaheadStats expected_stats;

    int current_key_idx = -1;
    auto prev_num_disk_reads = data_file->read_count.load();
    for (int random_seek_iter = 0; random_seek_iter < kNumRandomSeeks; ++random_seek_iter) {
      // Seek to another random block (and not the next one).
      const auto prev_key_idx = current_key_idx;
      do {
        current_key_idx = rnd_.Uniform(num_keys_);
      } while (prev_key_idx >= 0 && current_key_idx >= prev_key_idx - avg_keys_per_block_ &&
               current_key_idx <= prev_key_idx + 2 * avg_keys_per_block_);

      auto num_disk_reads = data_file->read_count.load();
      if (num_disk_reads > prev_num_disk_reads) {
        expected_stats.AddReadaheadReset(&expected_readahead_size);
        prev_num_disk_reads = num_disk_reads;
      }
      LOG(INFO) << "Disk read count: " << num_disk_reads
                << ". Moving to random key: " << current_key_idx;

      for (; current_key_idx < num_keys_;
           current_key_idx += avg_keys_per_block_) {
        ASSERT_OK(SeekToKey(iter, current_key_idx));
        LOG(INFO) << "Disk read count: " << data_file->read_count;

        // No readahead until Nth seq disk reads.
        if (data_file->read_count == num_disk_reads + seq_disk_reads_for_readahead) {
          expected_stats.AddReadaheadCall(expected_readahead_size);
          ASSERT_OK(ExpectReadaheadStats(expected_stats));
          break;
        }
        ASSERT_OK(ExpectReadaheadStats(expected_stats));
      }

      for (int readahead_window_iter = 0; readahead_window_iter < 2; ++readahead_window_iter) {
        num_disk_reads = data_file->read_count.load();
        if (!ASSERT_RESULT(
                ReadOneKeyPerBlockUntilOutOfReadaheadWindow(iter, data_file, &current_key_idx))) {
          continue;
        }
        if (data_file->read_count > num_disk_reads) {
          expected_stats.IncreaseWindowAndAddReadaheadCall(&expected_readahead_size);
        }
        ASSERT_OK(ExpectReadaheadStats(expected_stats));
      }
    }
  }
}

}  // namespace rocksdb

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  google::ParseCommandLineFlags(&argc, &argv, /* remove_flags = */ true);
  return RUN_ALL_TESTS();
}
