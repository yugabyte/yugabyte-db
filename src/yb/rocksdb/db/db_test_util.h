// Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.
//
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#pragma once
#ifndef __STDC_FORMAT_MACROS
#define __STDC_FORMAT_MACROS
#endif

#include <fcntl.h>
#include <inttypes.h>
#ifndef OS_WIN
#include <unistd.h>
#endif

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <thread>
#include <unordered_set>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "yb/encryption/encryption_fwd.h"

#include "yb/rocksdb/db/db_impl.h"
#include "yb/rocksdb/db/dbformat.h"
#include "yb/rocksdb/db/filename.h"
#include "yb/rocksdb/memtable/hash_linklist_rep.h"
#include "yb/rocksdb/cache.h"
#include "yb/rocksdb/compaction_filter.h"
#include "yb/rocksdb/convenience.h"
#include "yb/rocksdb/db.h"
#include "yb/rocksdb/env.h"
#include "yb/rocksdb/filter_policy.h"
#include "yb/rocksdb/options.h"

#include "yb/rocksdb/table.h"
#include "yb/rocksdb/utilities/checkpoint.h"
#include "yb/rocksdb/table/block_based_table_factory.h"
#include "yb/rocksdb/table/mock_table.h"
#include "yb/rocksdb/table/plain_table_factory.h"
#include "yb/rocksdb/table/scoped_arena_iterator.h"
#include "yb/rocksdb/util/compression.h"
#include "yb/rocksdb/util/mock_env.h"
#include "yb/rocksdb/util/mutexlock.h"
#include "yb/rocksdb/util/testharness.h"
#include "yb/rocksdb/util/testutil.h"
#include "yb/rocksdb/utilities/merge_operators.h"

#include "yb/util/callsite_profiling.h"
#include "yb/util/slice.h"
#include "yb/util/string_util.h"
#include "yb/util/sync_point.h"
#include "yb/util/test_util.h"

namespace rocksdb {

uint64_t TestGetTickerCount(const Options& options, Tickers ticker_type) {
  return options.statistics->getTickerCount(ticker_type);
}

void TestResetTickerCount(const Options& options, Tickers ticker_type) {
  return options.statistics->setTickerCount(ticker_type, 0);
}

// Update options for RocksDB to be redirected to GLOG via YBRocksDBLogger.
void ConfigureLoggingToGlog(Options* options, const std::string& log_prefix = "TEST: ");

class OnFileDeletionListener : public EventListener {
 public:
  OnFileDeletionListener() :
      matched_count_(0),
      expected_file_name_("") {}

  void SetExpectedFileName(
      const std::string file_name) {
    expected_file_name_ = file_name;
  }

  void VerifyMatchedCount(size_t expected_value) {
    ASSERT_EQ(matched_count_, expected_value);
  }

  void OnTableFileDeleted(
      const TableFileDeletionInfo& info) override {
    if (expected_file_name_ != "") {
      ASSERT_EQ(expected_file_name_, info.file_path);
      expected_file_name_ = "";
      matched_count_++;
    }
  }

 private:
  size_t matched_count_;
  std::string expected_file_name_;
};

class CompactionStartedListener : public EventListener {
 public:
  void OnCompactionStarted() override {
    ++num_compactions_started_;
  }

  int GetNumCompactionsStarted() { return num_compactions_started_; }

 private:
  std::atomic<int> num_compactions_started_;
};

namespace anon {
class AtomicCounter {
 public:
  explicit AtomicCounter(Env* env = NULL)
      : env_(env), cond_count_(&mu_), count_(0) {}

  void Increment() {
    MutexLock l(&mu_);
    count_++;
    YB_PROFILE(cond_count_.SignalAll());
  }

  int Read() {
    MutexLock l(&mu_);
    return count_;
  }

  bool WaitFor(int count) {
    MutexLock l(&mu_);

    uint64_t start = env_->NowMicros();
    while (count_ < count) {
      uint64_t now = env_->NowMicros();
      cond_count_.TimedWait(now + /*1s*/ 1 * 1000 * 1000);
      if (env_->NowMicros() - start > /*10s*/ 10 * 1000 * 1000) {
        return false;
      }
      if (count_ < count) {
        GTEST_LOG_(WARNING) << "WaitFor is taking more time than usual";
      }
    }

    return true;
  }

  void Reset() {
    MutexLock l(&mu_);
    count_ = 0;
    YB_PROFILE(cond_count_.SignalAll());
  }

 private:
  Env* env_;
  port::Mutex mu_;
  port::CondVar cond_count_;
  int count_;
};

struct OptionsOverride {
  std::shared_ptr<const FilterPolicy> filter_policy = nullptr;
};

}  // namespace anon

// A hacky skip list mem table that triggers flush after number of entries.
class SpecialMemTableRep : public MemTableRep {
 public:
  explicit SpecialMemTableRep(MemTableAllocator* allocator,
                              MemTableRep* memtable, int num_entries_flush)
      : MemTableRep(allocator),
        memtable_(memtable),
        num_entries_flush_(num_entries_flush),
        num_entries_(0) {}

  virtual KeyHandle Allocate(const size_t len, char** buf) override {
    return memtable_->Allocate(len, buf);
  }

  // Insert key into the list.
  // REQUIRES: nothing that compares equal to key is currently in the list.
  virtual void Insert(KeyHandle handle) override {
    memtable_->Insert(handle);
    num_entries_++;
  }

  // Returns true iff an entry that compares equal to key is in the list.
  virtual bool Contains(const char* key) const override {
    return memtable_->Contains(key);
  }

  virtual size_t ApproximateMemoryUsage() override {
    // Return a high memory usage when number of entries exceeds the threshold
    // to trigger a flush.
    return (num_entries_ < num_entries_flush_) ? 0 : 1024 * 1024 * 1024;
  }

  virtual void Get(const LookupKey& k, void* callback_args,
                   bool (*callback_func)(void* arg,
                                         const char* entry)) override {
    memtable_->Get(k, callback_args, callback_func);
  }

  uint64_t ApproximateNumEntries(const Slice& start_ikey,
                                 const Slice& end_ikey) override {
    return memtable_->ApproximateNumEntries(start_ikey, end_ikey);
  }

  virtual MemTableRep::Iterator* GetIterator(Arena* arena = nullptr) override {
    return memtable_->GetIterator(arena);
  }

  virtual ~SpecialMemTableRep() override {}

 private:
  std::unique_ptr<MemTableRep> memtable_;
  int num_entries_flush_;
  int num_entries_;
};

// The factory for the hacky skip list mem table that triggers flush after
// number of entries exceeds a threshold.
class SpecialSkipListFactory : public MemTableRepFactory {
 public:
  // After number of inserts exceeds `num_entries_flush` in a mem table, trigger
  // flush.
  explicit SpecialSkipListFactory(int num_entries_flush)
      : num_entries_flush_(num_entries_flush) {}

  virtual MemTableRep* CreateMemTableRep(
      const MemTableRep::KeyComparator& compare, MemTableAllocator* allocator,
      const SliceTransform* transform, Logger* logger) override {
    return new SpecialMemTableRep(
        allocator, factory_.CreateMemTableRep(compare, allocator, transform, 0),
        num_entries_flush_);
  }
  virtual const char* Name() const override { return "SkipListFactory"; }

 private:
  SkipListFactory factory_;
  int num_entries_flush_;
};

// Special Env used to delay background operations
class SpecialEnv : public EnvWrapper {
 public:
  explicit SpecialEnv(Env* base);

  Status NewWritableFile(const std::string& f, std::unique_ptr<WritableFile>* r,
                         const EnvOptions& soptions) override {
    class SSTableFile : public WritableFile {
     private:
      SpecialEnv* env_;
      std::unique_ptr<WritableFile> base_;

     public:
      SSTableFile(SpecialEnv* env, std::unique_ptr<WritableFile>&& base)
          : env_(env), base_(std::move(base)) {}
      Status Append(const Slice& data) override {
        if (env_->table_write_callback_) {
          (*env_->table_write_callback_)();
        }
        if (env_->drop_writes_.load(std::memory_order_acquire)) {
          // Drop writes on the floor
          return Status::OK();
        } else if (env_->no_space_.load(std::memory_order_acquire)) {
          return STATUS(IOError, "No space left on device");
        } else {
          env_->bytes_written_ += data.size();
          return base_->Append(data);
        }
      }
      Status Truncate(uint64_t size) override { return base_->Truncate(size); }
      Status Close() override {
// SyncPoint is not supported in Released Windows Mode.
#if !(defined NDEBUG) || !defined(OS_WIN)
        // Check preallocation size
        // preallocation size is never passed to base file.
        size_t preallocation_size = preallocation_block_size();
        TEST_SYNC_POINT_CALLBACK("DBTestWritableFile.GetPreallocationStatus",
                                 &preallocation_size);
#endif  // !(defined NDEBUG) || !defined(OS_WIN)
        return base_->Close();
      }
      Status Flush() override { return base_->Flush(); }
      Status Sync() override {
        ++env_->sync_counter_;
        while (env_->delay_sstable_sync_.load(std::memory_order_acquire)) {
          env_->SleepForMicroseconds(100000);
        }
        return base_->Sync();
      }
      void SetIOPriority(yb::IOPriority pri) override {
        base_->SetIOPriority(pri);
      }
      yb::IOPriority GetIOPriority() override {
        return base_->GetIOPriority();
      }
      const std::string& filename() const override { return base_->filename(); }
    };
    class ManifestFile : public WritableFile {
     public:
      ManifestFile(SpecialEnv* env, std::unique_ptr<WritableFile>&& b)
          : env_(env), base_(std::move(b)) {}
      Status Append(const Slice& data) override {
        if (env_->manifest_write_error_.load(std::memory_order_acquire)) {
          return STATUS(IOError, "simulated writer error");
        } else {
          return base_->Append(data);
        }
      }
      Status Truncate(uint64_t size) override { return base_->Truncate(size); }
      Status Close() override { return base_->Close(); }
      Status Flush() override { return base_->Flush(); }
      Status Sync() override {
        ++env_->sync_counter_;
        if (env_->manifest_sync_error_.load(std::memory_order_acquire)) {
          return STATUS(IOError, "simulated sync error");
        } else {
          return base_->Sync();
        }
      }
      uint64_t GetFileSize() override { return base_->GetFileSize(); }
      const std::string& filename() const override { return base_->filename(); }

     private:
      SpecialEnv* env_;
      std::unique_ptr<WritableFile> base_;
    };
    class WalFile : public WritableFile {
     public:
      WalFile(SpecialEnv* env, std::unique_ptr<WritableFile>&& b)
          : env_(env), base_(std::move(b)) {}
      Status Append(const Slice& data) override {
#if !(defined NDEBUG) || !defined(OS_WIN)
        TEST_SYNC_POINT("SpecialEnv::WalFile::Append:1");
#endif
        Status s;
        if (env_->log_write_error_.load(std::memory_order_acquire)) {
          s = STATUS(IOError, "simulated writer error");
        } else {
          int slowdown =
              env_->log_write_slowdown_.load(std::memory_order_acquire);
          if (slowdown > 0) {
            env_->SleepForMicroseconds(slowdown);
          }
          s = base_->Append(data);
        }
#if !(defined NDEBUG) || !defined(OS_WIN)
        TEST_SYNC_POINT("SpecialEnv::WalFile::Append:2");
#endif
        return s;
      }
      Status Truncate(uint64_t size) override { return base_->Truncate(size); }
      Status Close() override { return base_->Close(); }
      Status Flush() override { return base_->Flush(); }
      Status Sync() override {
        ++env_->sync_counter_;
        return base_->Sync();
      }
      bool IsSyncThreadSafe() const override {
        return env_->is_wal_sync_thread_safe_.load();
      }
      const std::string& filename() const override { return base_->filename(); }

     private:
      SpecialEnv* env_;
      std::unique_ptr<WritableFile> base_;
    };

    if (non_writeable_rate_.load(std::memory_order_acquire) > 0) {
      uint32_t random_number;
      {
        MutexLock l(&rnd_mutex_);
        random_number = rnd_.Uniform(100);
      }
      if (random_number < non_writeable_rate_.load()) {
        return STATUS(IOError, "simulated random write error");
      }
    }

    new_writable_count_++;

    if (non_writable_count_.load() > 0) {
      non_writable_count_--;
      return STATUS(IOError, "simulated write error");
    }

    Status s = target()->NewWritableFile(f, r, soptions);
    if (s.ok()) {
      if (strstr(f.c_str(), ".sst") != nullptr) {
        r->reset(new SSTableFile(this, std::move(*r)));
      } else if (strstr(f.c_str(), "MANIFEST") != nullptr) {
        r->reset(new ManifestFile(this, std::move(*r)));
      } else if (strstr(f.c_str(), "log") != nullptr) {
        r->reset(new WalFile(this, std::move(*r)));
      }
    }
    return s;
  }

  Status NewRandomAccessFile(const std::string& f,
                             std::unique_ptr<RandomAccessFile>* r,
                             const EnvOptions& soptions) override {
    class CountingFile : public yb::RandomAccessFileWrapper {
     public:
      CountingFile(std::unique_ptr<RandomAccessFile>&& target,
                   anon::AtomicCounter* counter)
          : RandomAccessFileWrapper(std::move(target)), counter_(counter) {}

      Status Read(uint64_t offset, size_t n, Slice* result, uint8_t* scratch) const override {
        counter_->Increment();
        return RandomAccessFileWrapper::Read(offset, n, result, scratch);
      }

     private:
      anon::AtomicCounter* counter_;
    };

    Status s = target()->NewRandomAccessFile(f, r, soptions);
    random_file_open_counter_++;
    if (s.ok() && count_random_reads_) {
      r->reset(new CountingFile(std::move(*r), &random_read_counter_));
    }
    return s;
  }

  Status NewSequentialFile(const std::string& f, std::unique_ptr<SequentialFile>* r,
                           const EnvOptions& soptions) override {
    class CountingFile : public yb::SequentialFileWrapper {
     public:
      CountingFile(std::unique_ptr<SequentialFile>&& target,
                   anon::AtomicCounter* counter)
          : yb::SequentialFileWrapper(std::move(target)), counter_(counter) {}

      Status Read(size_t n, Slice* result, uint8_t* scratch) override {
        counter_->Increment();
        return SequentialFileWrapper::Read(n, result, scratch);
      }

     private:
      anon::AtomicCounter* counter_;
    };

    Status s = target()->NewSequentialFile(f, r, soptions);
    if (s.ok() && count_sequential_reads_) {
      r->reset(new CountingFile(std::move(*r), &sequential_read_counter_));
    }
    return s;
  }

  virtual void SleepForMicroseconds(int micros) override {
    sleep_counter_.Increment();
    if (no_sleep_ || time_elapse_only_sleep_) {
      addon_time_.fetch_add(micros);
    }
    if (!no_sleep_) {
      target()->SleepForMicroseconds(micros);
    }
  }

  virtual Status GetCurrentTime(int64_t* unix_time) override {
    Status s;
    if (!time_elapse_only_sleep_) {
      s = target()->GetCurrentTime(unix_time);
    }
    if (s.ok()) {
      *unix_time += addon_time_.load();
    }
    return s;
  }

  virtual uint64_t NowNanos() override {
    return (time_elapse_only_sleep_ ? 0 : target()->NowNanos()) +
           addon_time_.load() * 1000;
  }

  virtual uint64_t NowMicros() override {
    return (time_elapse_only_sleep_ ? 0 : target()->NowMicros()) +
           addon_time_.load();
  }

  Random rnd_;
  port::Mutex rnd_mutex_;  // Lock to pretect rnd_

  // sstable Sync() calls are blocked while this pointer is non-nullptr.
  std::atomic<bool> delay_sstable_sync_;

  // Drop writes on the floor while this pointer is non-nullptr.
  std::atomic<bool> drop_writes_;

  // Simulate no-space errors while this pointer is non-nullptr.
  std::atomic<bool> no_space_;

  // Simulate non-writable file system while this pointer is non-nullptr
  std::atomic<bool> non_writable_;

  // Force sync of manifest files to fail while this pointer is non-nullptr
  std::atomic<bool> manifest_sync_error_;

  // Force write to manifest files to fail while this pointer is non-nullptr
  std::atomic<bool> manifest_write_error_;

  // Force write to log files to fail while this pointer is non-nullptr
  std::atomic<bool> log_write_error_;

  // Slow down every log write, in micro-seconds.
  std::atomic<int> log_write_slowdown_;

  bool count_random_reads_;
  anon::AtomicCounter random_read_counter_;
  std::atomic<int> random_file_open_counter_;

  bool count_sequential_reads_;
  anon::AtomicCounter sequential_read_counter_;

  anon::AtomicCounter sleep_counter_;

  std::atomic<int64_t> bytes_written_;

  std::atomic<int> sync_counter_;

  std::atomic<uint32_t> non_writeable_rate_;

  std::atomic<uint32_t> new_writable_count_;

  std::atomic<uint32_t> non_writable_count_;

  std::function<void()>* table_write_callback_;

  std::atomic<int64_t> addon_time_;

  bool time_elapse_only_sleep_;

  bool no_sleep_;

  std::atomic<bool> is_wal_sync_thread_safe_{true};
};

class DBHolder {
 protected:
  // Sequence of option configurations to try
  enum OptionConfig {
    kDefault = 0,
    kBlockBasedTableWithPrefixHashIndex = 1,
    kBlockBasedTableWithWholeKeyHashIndex = 2,
    kPlainTableFirstBytePrefix = 3,
    kPlainTableCappedPrefix = 4,
    kPlainTableCappedPrefixNonMmap = 5,
    kPlainTableAllBytesPrefix = 6,
    kVectorRep = 7,
    kHashLinkList = 8,
    kMergePut = 9,
    kFilter = 10,
    kFullFilterWithNewTableReaderForCompactions = 11,
    kUncompressed = 12,
    kNumLevel_3 = 13,
    kDBLogDir = 14,
    kWalDirAndMmapReads = 15,
    kManifestFileSize = 16,
    kPerfOptions = 17,
    kDeletesFilterFirst = 18,
    kHashSkipList = 19,
    kUniversalCompaction = 20,
    kUniversalCompactionMultiLevel = 21,
    kCompressedBlockCache = 22,
    kInfiniteMaxOpenFiles = 23,
    kxxHashChecksum = 24,
    kFIFOCompaction = 25,
    kOptimizeFiltersForHits = 26,
    kRowCache = 27,
    kRecycleLogFiles = 28,
    kConcurrentSkipList = 29,
    kEnd = 30,
    kLevelSubcompactions = 30,
    kUniversalSubcompactions = 31,
    kBlockBasedTableWithIndexRestartInterval = 32,
    kBlockBasedTableWithThreeSharedPartsKeyDeltaEncoding = 33,
  };
  int option_config_;

 public:
  std::string dbname_;
  std::string alternative_wal_dir_;
  std::string alternative_db_log_dir_;
  MockEnv* mem_env_;
  SpecialEnv* env_;
  DB* db_;
  std::vector<ColumnFamilyHandle*> handles_;

  Options last_options_;

  // For encryption
  std::unique_ptr<yb::encryption::UniverseKeyManager> universe_key_manager_;
  std::unique_ptr<rocksdb::Env> encrypted_env_;

  static const std::string kKeyId;
  static const std::string kKeyFile;

  // Skip some options, as they may not be applicable to a specific test.
  // To add more skip constants, use values 4, 8, 16, etc.
  enum OptionSkip {
    kNoSkip = 0,
    kSkipDeletesFilterFirst = 1,
    kSkipUniversalCompaction = 2,
    kSkipMergePut = 4,
    kSkipPlainTable = 8,
    kSkipHashIndex = 16,
    kSkipNoSeekToLast = 32,
    kSkipFIFOCompaction = 64,
    kSkipMmapReads = 128,
  };

  explicit DBHolder(std::string path, bool encryption_enabled = false);

  virtual ~DBHolder();

  void CreateEncryptedEnv();

  static std::string Key(int i) {
    char buf[100];
    snprintf(buf, sizeof(buf), "key%06d", i);
    return std::string(buf);
  }

  static bool ShouldSkipOptions(int option_config, int skip_mask = kNoSkip);

  // Switch to a fresh database with the next option configuration to
  // test.  Return false if there are no more configurations to test.
  bool ChangeOptions(int skip_mask = kNoSkip);

  // Switch between different compaction styles (we have only 2 now).
  bool ChangeCompactOptions();

  // Switch between different filter policy
  // Jump from kDefault to kFilter to kFullFilter
  bool ChangeFilterOptions();

  // Return the current option configuration.
  Options CurrentOptions(
      const anon::OptionsOverride& options_override = anon::OptionsOverride());

  Options CurrentOptions(
      const Options& defaultOptions,
      const anon::OptionsOverride& options_override = anon::OptionsOverride());

  DBImpl* dbfull() { return reinterpret_cast<DBImpl*>(db_); }

  void CreateColumnFamilies(const std::vector<std::string>& cfs,
                            const Options& options);

  void CreateAndReopenWithCF(const std::vector<std::string>& cfs,
                             const Options& options);

  void ReopenWithColumnFamilies(const std::vector<std::string>& cfs,
                                const std::vector<Options>& options);

  void ReopenWithColumnFamilies(const std::vector<std::string>& cfs,
                                const Options& options);

  Status TryReopenWithColumnFamilies(const std::vector<std::string>& cfs,
                                     const std::vector<Options>& options);

  Status TryReopenWithColumnFamilies(const std::vector<std::string>& cfs,
                                     const Options& options);

  void Reopen(const Options& options);

  void Close();

  void DestroyAndReopen(const Options& options);

  void Destroy(const Options& options);

  Status ReadOnlyReopen(const Options& options);

  Status TryReopen(const Options& options);

  Status Flush(int cf = 0);

  Status Put(const Slice& k, const Slice& v, WriteOptions wo = WriteOptions());

  Status Put(int cf, const Slice& k, const Slice& v,
             WriteOptions wo = WriteOptions());

  Status Delete(const std::string& k);

  Status Delete(int cf, const std::string& k);

  Status SingleDelete(const std::string& k);

  Status SingleDelete(int cf, const std::string& k);

  std::string Get(const std::string& k, const Snapshot* snapshot = nullptr);

  std::string Get(int cf, const std::string& k,
                  const Snapshot* snapshot = nullptr);

  uint64_t GetNumSnapshots();

  uint64_t GetTimeOldestSnapshots();

  // Return a string that contains all key,value pairs in order,
  // formatted like "(k1->v1)(k2->v2)".
  std::string Contents(int cf = 0);

  std::string AllEntriesFor(const Slice& user_key, int cf = 0);

  int NumSortedRuns(int cf = 0);

  uint64_t TotalSize(int cf = 0);

  uint64_t SizeAtLevel(int level);

  size_t TotalLiveFiles(int cf = 0);

  size_t CountLiveFiles();

  int NumTableFilesAtLevel(int level, int cf = 0);

  int TotalTableFiles(int cf = 0, int levels = -1);

  // Return spread of files per level
  std::string FilesPerLevel(int cf = 0);

  size_t CountFiles();

  uint64_t Size(const Slice& start, const Slice& limit, int cf = 0);

  void Compact(int cf, const Slice& start, const Slice& limit,
               uint32_t target_path_id);

  void Compact(int cf, const Slice& start, const Slice& limit);

  void Compact(const Slice& start, const Slice& limit);

  // Do n memtable compactions, each of which produces an sstable
  // covering the range [small,large].
  void MakeTables(int n, const std::string& small, const std::string& large,
                  int cf = 0);

  // Prevent pushing of new sstables into deeper levels by adding
  // tables that cover a specified range to all levels.
  void FillLevels(const std::string& smallest, const std::string& largest,
                  int cf);

  void MoveFilesToLevel(int level, int cf = 0);

  void DumpFileCounts(const char* label);

  std::string DumpSSTableList();

  void GetSstFiles(std::string path, std::vector<std::string>* files);

  int GetSstFileCount(std::string path);

  // this will generate non-overlapping files since it keeps increasing key_idx
  void GenerateNewFile(Random* rnd, int* key_idx, bool nowait = false);

  void GenerateNewFile(int fd, Random* rnd, int* key_idx, bool nowait = false);

  static const int kNumKeysByGenerateNewRandomFile;
  static const int KNumKeysByGenerateNewFile = 100;

  void GenerateNewRandomFile(Random* rnd, bool nowait = false);

  std::string IterStatus(Iterator* iter);

  Options OptionsForLogIterTest();

  std::string DummyString(size_t len, char c = 'a');

  void VerifyIterLast(std::string expected_key, int cf = 0);

  // Used to test InplaceUpdate

  // If previous value is nullptr or delta is > than previous value,
  //   sets newValue with delta
  // If previous value is not empty,
  //   updates previous value with 'b' string of previous value size - 1.
  static UpdateStatus updateInPlaceSmallerSize(char* prevValue,
                                               uint32_t* prevSize, Slice delta,
                                               std::string* newValue);

  static UpdateStatus updateInPlaceSmallerVarintSize(char* prevValue,
                                                     uint32_t* prevSize,
                                                     Slice delta,
                                                     std::string* newValue);

  static UpdateStatus updateInPlaceLargerSize(char* prevValue,
                                              uint32_t* prevSize, Slice delta,
                                              std::string* newValue);

  static UpdateStatus updateInPlaceNoAction(char* prevValue, uint32_t* prevSize,
                                            Slice delta, std::string* newValue);

  // Utility method to test InplaceUpdate
  void validateNumberOfEntries(int numValues, int cf = 0);

  void CopyFile(const std::string& source, const std::string& destination,
                uint64_t size = 0);

  std::unordered_map<std::string, uint64_t> GetAllSSTFiles(
      uint64_t* total_size = nullptr);
};

class DBTestBase : public RocksDBTest, public DBHolder {
 public:
  explicit DBTestBase(std::string path, bool encryption_enabled = false)
    : DBHolder(std::move(path), encryption_enabled)
  {}
};

}  // namespace rocksdb
