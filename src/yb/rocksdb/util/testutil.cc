//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
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

#include "yb/rocksdb/util/testutil.h"

#include <boost/functional/hash.hpp>

#include <gtest/gtest.h>

#include "yb/gutil/casts.h"

#include "yb/rocksdb/port/port.h"
#include "yb/rocksdb/util/file_reader_writer.h"

using std::unique_ptr;

namespace rocksdb {
namespace test {

extern std::string RandomHumanReadableString(Random* rnd, int len) {
  std::string ret;
  ret.resize(len);
  for (int i = 0; i < len; ++i) {
    ret[i] = static_cast<char>('a' + rnd->Uniform(26));
  }
  return ret;
}

std::string RandomKey(Random* rnd, int len, RandomKeyType type) {
  // Make sure to generate a wide variety of characters so we
  // test the boundary conditions for short-key optimizations.
  static const char kTestChars[] = {
    '\0', '\1', 'a', 'b', 'c', 'd', 'e', '\xfd', '\xfe', '\xff'
  };
  std::string result;
  for (int i = 0; i < len; i++) {
    std::size_t indx = 0;
    switch (type) {
      case RandomKeyType::RANDOM:
        indx = rnd->Uniform(sizeof(kTestChars));
        break;
      case RandomKeyType::LARGEST:
        indx = sizeof(kTestChars) - 1;
        break;
      case RandomKeyType::MIDDLE:
        indx = sizeof(kTestChars) / 2;
        break;
      case RandomKeyType::SMALLEST:
        indx = 0;
        break;
    }
    result += kTestChars[indx];
  }
  return result;
}


WritableFileWriter* GetWritableFileWriter(WritableFile* wf) {
  unique_ptr<WritableFile> file(wf);
  return new WritableFileWriter(std::move(file), EnvOptions());
}

RandomAccessFileReader* GetRandomAccessFileReader(RandomAccessFile* raf) {
  unique_ptr<RandomAccessFile> file(raf);
  return new RandomAccessFileReader(std::move(file));
}

SequentialFileReader* GetSequentialFileReader(SequentialFile* se) {
  unique_ptr<SequentialFile> file(se);
  return new SequentialFileReader(std::move(file));
}

void CorruptKeyType(InternalKey* ikey) {
  std::string keystr = ikey->Encode().ToString();
  keystr[keystr.size() - 8] = kTypeLogData;
  *ikey = InternalKey::DecodeFrom(Slice(keystr.data(), keystr.size()));
}

std::string KeyStr(const std::string& user_key, const SequenceNumber& seq,
                   const ValueType& t, bool corrupt) {
  InternalKey k(user_key, seq, t);
  if (corrupt) {
    CorruptKeyType(&k);
  }
  return k.Encode().ToString();
}

std::string RandomName(Random* rnd, const size_t len) {
  std::stringstream ss;
  for (size_t i = 0; i < len; ++i) {
    ss << static_cast<char>(rnd->Uniform(26) + 'a');
  }
  return ss.str();
}

CompressionType RandomCompressionType(Random* rnd) {
  return static_cast<CompressionType>(rnd->Uniform(6));
}

void RandomCompressionTypeVector(const size_t count,
                                 std::vector<CompressionType>* types,
                                 Random* rnd) {
  types->clear();
  for (size_t i = 0; i < count; ++i) {
    types->emplace_back(RandomCompressionType(rnd));
  }
}

const SliceTransform* RandomSliceTransform(Random* rnd, int pre_defined) {
  int random_num = pre_defined >= 0 ? pre_defined : rnd->Uniform(4);
  switch (random_num) {
    case 0:
      return NewFixedPrefixTransform(rnd->Uniform(20) + 1);
    case 1:
      return NewCappedPrefixTransform(rnd->Uniform(20) + 1);
    case 2:
      return NewNoopTransform();
    default:
      return nullptr;
  }
}

BlockBasedTableOptions RandomBlockBasedTableOptions(Random* rnd) {
  BlockBasedTableOptions opt;
  opt.cache_index_and_filter_blocks = rnd->Uniform(2);
  opt.index_type = static_cast<IndexType>(rnd->Uniform(kElementsInIndexType));
  opt.hash_index_allow_collision = rnd->Uniform(2);
  opt.checksum = static_cast<ChecksumType>(rnd->Uniform(3));
  opt.block_size = rnd->Uniform(10000000);
  opt.block_size_deviation = rnd->Uniform(100);
  opt.block_restart_interval = rnd->Uniform(100);
  opt.index_block_restart_interval = rnd->Uniform(100);
  opt.whole_key_filtering = rnd->Uniform(2);

  return opt;
}

TableFactory* RandomTableFactory(Random* rnd, int pre_defined) {
  int random_num = pre_defined >= 0 ? pre_defined : rnd->Uniform(2);
  switch (random_num) {
    case 0:
      return NewPlainTableFactory();
    default:
      return NewBlockBasedTableFactory();
  }
}

MergeOperator* RandomMergeOperator(Random* rnd) {
  return new ChanglingMergeOperator(RandomName(rnd, 10));
}

CompactionFilter* RandomCompactionFilter(Random* rnd) {
  return new ChanglingCompactionFilter(RandomName(rnd, 10));
}

CompactionFilterFactory* RandomCompactionFilterFactory(Random* rnd) {
  return new ChanglingCompactionFilterFactory(RandomName(rnd, 10));
}

void RandomInitDBOptions(DBOptions* db_opt, Random* rnd) {
  // boolean options
  db_opt->advise_random_on_open = rnd->Uniform(2);
  db_opt->allow_mmap_reads = rnd->Uniform(2);
  db_opt->allow_mmap_writes = rnd->Uniform(2);
  db_opt->allow_os_buffer = rnd->Uniform(2);
  db_opt->create_if_missing = rnd->Uniform(2);
  db_opt->create_missing_column_families = rnd->Uniform(2);
  db_opt->disableDataSync = rnd->Uniform(2);
  db_opt->enable_thread_tracking = false;
  db_opt->error_if_exists = rnd->Uniform(2);
  db_opt->is_fd_close_on_exec = rnd->Uniform(2);
  db_opt->paranoid_checks = rnd->Uniform(2);
  db_opt->skip_log_error_on_recovery = rnd->Uniform(2);
  db_opt->skip_stats_update_on_db_open = rnd->Uniform(2);
  db_opt->use_adaptive_mutex = rnd->Uniform(2);
  db_opt->use_fsync = rnd->Uniform(2);
  db_opt->recycle_log_file_num = rnd->Uniform(2);

  // int options
  db_opt->max_background_compactions = rnd->Uniform(100);
  db_opt->max_background_flushes = rnd->Uniform(100);
  db_opt->max_file_opening_threads = rnd->Uniform(100);
  db_opt->max_open_files = rnd->Uniform(100);
  db_opt->table_cache_numshardbits = rnd->Uniform(100);

  // size_t options
  db_opt->db_write_buffer_size = rnd->Uniform(10000);
  db_opt->keep_log_file_num = rnd->Uniform(10000);
  db_opt->log_file_time_to_roll = rnd->Uniform(10000);
  db_opt->manifest_preallocation_size = rnd->Uniform(10000);
  db_opt->max_log_file_size = rnd->Uniform(10000);

  // std::string options
  db_opt->db_log_dir = "path/to/db_log_dir";
  db_opt->wal_dir = "path/to/wal_dir";

  // uint32_t options
  db_opt->max_subcompactions = rnd->Uniform(100000);

  // uint64_t options
  static const uint64_t uint_max = static_cast<uint64_t>(UINT_MAX);
  db_opt->WAL_size_limit_MB = uint_max + rnd->Uniform(100000);
  db_opt->WAL_ttl_seconds = uint_max + rnd->Uniform(100000);
  db_opt->bytes_per_sync = uint_max + rnd->Uniform(100000);
  db_opt->delayed_write_rate = uint_max + rnd->Uniform(100000);
  db_opt->delete_obsolete_files_period_micros = uint_max + rnd->Uniform(100000);
  db_opt->max_manifest_file_size = uint_max + rnd->Uniform(100000);
  db_opt->max_total_wal_size = uint_max + rnd->Uniform(100000);
  db_opt->wal_bytes_per_sync = uint_max + rnd->Uniform(100000);

  // unsigned int options
  db_opt->stats_dump_period_sec = rnd->Uniform(100000);
}

void RandomInitCFOptions(ColumnFamilyOptions* cf_opt, Random* rnd) {
  cf_opt->compaction_style = (CompactionStyle)(rnd->Uniform(4));

  // boolean options
  cf_opt->compaction_measure_io_stats = rnd->Uniform(2);
  cf_opt->disable_auto_compactions = rnd->Uniform(2);
  cf_opt->filter_deletes = rnd->Uniform(2);
  cf_opt->inplace_update_support = rnd->Uniform(2);
  cf_opt->level_compaction_dynamic_level_bytes = rnd->Uniform(2);
  cf_opt->optimize_filters_for_hits = rnd->Uniform(2);
  cf_opt->paranoid_file_checks = rnd->Uniform(2);
  cf_opt->purge_redundant_kvs_while_flush = rnd->Uniform(2);
  cf_opt->verify_checksums_in_compaction = rnd->Uniform(2);

  // double options
  cf_opt->hard_rate_limit = static_cast<double>(rnd->Uniform(10000)) / 13;
  cf_opt->soft_rate_limit = static_cast<double>(rnd->Uniform(10000)) / 13;

  // int options
  cf_opt->expanded_compaction_factor = rnd->Uniform(100);
  cf_opt->level0_file_num_compaction_trigger = rnd->Uniform(100);
  cf_opt->level0_slowdown_writes_trigger = rnd->Uniform(100);
  cf_opt->level0_stop_writes_trigger = rnd->Uniform(100);
  cf_opt->max_bytes_for_level_multiplier = rnd->Uniform(100);
  cf_opt->max_grandparent_overlap_factor = rnd->Uniform(100);
  cf_opt->max_mem_compaction_level = rnd->Uniform(100);
  cf_opt->max_write_buffer_number = rnd->Uniform(100);
  cf_opt->max_write_buffer_number_to_maintain = rnd->Uniform(100);
  cf_opt->min_write_buffer_number_to_merge = rnd->Uniform(100);
  cf_opt->num_levels = rnd->Uniform(100);
  cf_opt->source_compaction_factor = rnd->Uniform(100);
  cf_opt->target_file_size_multiplier = rnd->Uniform(100);

  // size_t options
  cf_opt->arena_block_size = rnd->Uniform(10000);
  cf_opt->inplace_update_num_locks = rnd->Uniform(10000);
  cf_opt->max_successive_merges = rnd->Uniform(10000);
  cf_opt->memtable_prefix_bloom_huge_page_tlb_size = rnd->Uniform(10000);
  cf_opt->write_buffer_size = rnd->Uniform(10000);

  // uint32_t options
  cf_opt->bloom_locality = rnd->Uniform(10000);
  cf_opt->memtable_prefix_bloom_bits = rnd->Uniform(10000);
  cf_opt->memtable_prefix_bloom_probes = rnd->Uniform(10000);
  cf_opt->min_partial_merge_operands = rnd->Uniform(10000);
  cf_opt->max_bytes_for_level_base = rnd->Uniform(10000);

  // uint64_t options
  static const uint64_t uint_max = static_cast<uint64_t>(UINT_MAX);
  cf_opt->max_sequential_skip_in_iterations = uint_max + rnd->Uniform(10000);
  cf_opt->target_file_size_base = uint_max + rnd->Uniform(10000);

  // unsigned int options
  cf_opt->rate_limit_delay_max_milliseconds = rnd->Uniform(10000);

  // pointer typed options
  cf_opt->prefix_extractor.reset(RandomSliceTransform(rnd));
  cf_opt->table_factory.reset(RandomTableFactory(rnd));
  cf_opt->merge_operator.reset(RandomMergeOperator(rnd));
  if (cf_opt->compaction_filter) {
    delete cf_opt->compaction_filter;
  }
  cf_opt->compaction_filter = RandomCompactionFilter(rnd);
  cf_opt->compaction_filter_factory.reset(RandomCompactionFilterFactory(rnd));

  // custom typed options
  cf_opt->compression = RandomCompressionType(rnd);
  RandomCompressionTypeVector(cf_opt->num_levels,
                              &cf_opt->compression_per_level, rnd);
}

namespace {

enum TestBoundaryUserValueTag {
  TAG_LEFT_VALUE = 10,
  TAG_RIGHT_VALUE,
};

// Left value - just left half of the key.
Slice ExtractLeftValue(Slice key) {
  return key.Prefix(key.size() / 2);
}

// Right value - just right half of the key.
Slice ExtractRightValue(Slice key) {
  return key.WithoutPrefix(key.size() / 2);
}

template <class Cmp>
void UpdateValue(const Slice& new_value, Cmp cmp, UserBoundaryValue::Value* value) {
  if (cmp(value->AsSlice().compare(new_value), 0)) {
    value->Assign(new_value);
  }
}

class TestBoundaryValuesExtractor: public BoundaryValuesExtractor {
 public:
  Status Extract(Slice user_key, UserBoundaryValueRefs *values) override {
    values->push_back(UserBoundaryValueRef {
      .tag = TAG_LEFT_VALUE,
      .value = ExtractLeftValue(user_key),
    });
    values->push_back(UserBoundaryValueRef {
      .tag = TAG_RIGHT_VALUE,
      .value = ExtractRightValue(user_key),
    });
    return Status::OK();
  }

  UserFrontierPtr CreateFrontier() override {
    return new TestUserFrontier(0);
  }

  virtual ~TestBoundaryValuesExtractor() {}
 private:
};

} // namespace

std::string TestUserFrontier::ToString() const {
  return YB_CLASS_TO_STRING(value);
}

Slice GetBoundaryLeft(const UserBoundaryValues& values) {
  auto value = TEST_UserValueWithTag(values, TAG_LEFT_VALUE);
  EXPECT_NE(value, nullptr);
  return value->AsSlice();
}

Slice GetBoundaryRight(const UserBoundaryValues& values) {
  auto value = TEST_UserValueWithTag(values, TAG_RIGHT_VALUE);
  EXPECT_NE(value, nullptr);
  return value->AsSlice();
}

std::shared_ptr<BoundaryValuesExtractor> MakeBoundaryValuesExtractor() {
  return std::make_shared<TestBoundaryValuesExtractor>();
}

UserBoundaryValue MakeLeftBoundaryValue(const Slice& value) {
  return UserBoundaryValue(UserBoundaryValueRef {
    .tag = TAG_LEFT_VALUE,
    .value = value,
  });
}

UserBoundaryValue MakeRightBoundaryValue(const Slice& value) {
  return UserBoundaryValue(UserBoundaryValueRef {
    .tag = TAG_RIGHT_VALUE,
    .value = value,
  });
}

void BoundaryTestValues::Feed(Slice key) {
  auto left_value = ExtractLeftValue(key);
  auto right_value = ExtractRightValue(key);
  if (min_right.empty()) {
    min_left.Assign(left_value);
    max_left.Assign(left_value);
    min_right.Assign(right_value);
    max_right.Assign(right_value);
    return;
  }
  UpdateValue(left_value, std::greater<>(), &min_left);
  UpdateValue(left_value, std::less<>(), &max_left);
  UpdateValue(right_value, std::greater<>(), &min_right);
  UpdateValue(right_value, std::less<>(), &max_right);
}

void AssertSlicesEq(const Slice& lhs, const Slice& rhs) {
  ASSERT_EQ(lhs.compare(rhs), 0) << lhs.ToDebugHexString() << " vs " << rhs.ToDebugHexString();
}

void BoundaryTestValues::Check(const FileBoundaryValues<InternalKey>& smallest,
                               const FileBoundaryValues<InternalKey>& largest) {
  AssertSlicesEq(min_left.AsSlice(), GetBoundaryLeft(smallest.user_values));
  AssertSlicesEq(max_left.AsSlice(), GetBoundaryLeft(largest.user_values));
  AssertSlicesEq(min_right.AsSlice(), GetBoundaryRight(smallest.user_values));
  AssertSlicesEq(max_right.AsSlice(), GetBoundaryRight(largest.user_values));
}

}  // namespace test
}  // namespace rocksdb
