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

#include <string>

#include "yb/rocksdb/db/db_test_util.h"
#include "yb/rocksdb/perf_context.h"
#include "yb/rocksdb/perf_level.h"
#include "yb/rocksdb/port/stack_trace.h"

using std::unique_ptr;

namespace rocksdb {

class DBPropertiesTest : public DBTestBase {
 public:
  DBPropertiesTest() : DBTestBase("/db_properties_test") {}
};

TEST_F(DBPropertiesTest, Empty) {
  do {
    Options options;
    options.env = env_;
    options.write_buffer_size = 100000;  // Small write buffer
    options = CurrentOptions(options);
    CreateAndReopenWithCF({"pikachu"}, options);

    std::string num;
    ASSERT_TRUE(dbfull()->GetProperty(
        handles_[1], "rocksdb.num-entries-active-mem-table", &num));
    ASSERT_EQ("0", num);

    ASSERT_OK(Put(1, "foo", "v1"));
    ASSERT_EQ("v1", Get(1, "foo"));
    ASSERT_TRUE(dbfull()->GetProperty(
        handles_[1], "rocksdb.num-entries-active-mem-table", &num));
    ASSERT_EQ("1", num);

    // Block sync calls
    env_->delay_sstable_sync_.store(true, std::memory_order_release);
    ASSERT_OK(Put(1, "k1", std::string(100000, 'x')));  // Fill memtable
    ASSERT_TRUE(dbfull()->GetProperty(
        handles_[1], "rocksdb.num-entries-active-mem-table", &num));
    ASSERT_EQ("2", num);

    ASSERT_OK(Put(1, "k2", std::string(100000, 'y')));  // Trigger compaction
    ASSERT_TRUE(dbfull()->GetProperty(
        handles_[1], "rocksdb.num-entries-active-mem-table", &num));
    ASSERT_EQ("1", num);

    ASSERT_EQ("v1", Get(1, "foo"));
    // Release sync calls
    env_->delay_sstable_sync_.store(false, std::memory_order_release);

    ASSERT_OK(db_->DisableFileDeletions());
    ASSERT_TRUE(
        dbfull()->GetProperty("rocksdb.is-file-deletions-enabled", &num));
    ASSERT_EQ("1", num);

    ASSERT_OK(db_->DisableFileDeletions());
    ASSERT_TRUE(
        dbfull()->GetProperty("rocksdb.is-file-deletions-enabled", &num));
    ASSERT_EQ("2", num);

    ASSERT_OK(db_->DisableFileDeletions());
    ASSERT_TRUE(
        dbfull()->GetProperty("rocksdb.is-file-deletions-enabled", &num));
    ASSERT_EQ("3", num);

    ASSERT_OK(db_->EnableFileDeletions(false));
    ASSERT_TRUE(
        dbfull()->GetProperty("rocksdb.is-file-deletions-enabled", &num));
    ASSERT_EQ("2", num);

    ASSERT_OK(db_->EnableFileDeletions());
    ASSERT_TRUE(
        dbfull()->GetProperty("rocksdb.is-file-deletions-enabled", &num));
    ASSERT_EQ("0", num);
  } while (ChangeOptions());
}

TEST_F(DBPropertiesTest, CurrentVersionNumber) {
  uint64_t v1, v2, v3;
  ASSERT_TRUE(
      dbfull()->GetIntProperty("rocksdb.current-super-version-number", &v1));
  ASSERT_OK(Put("12345678", ""));
  ASSERT_TRUE(
      dbfull()->GetIntProperty("rocksdb.current-super-version-number", &v2));
  ASSERT_OK(Flush());;
  ASSERT_TRUE(
      dbfull()->GetIntProperty("rocksdb.current-super-version-number", &v3));

  ASSERT_EQ(v1, v2);
  ASSERT_GT(v3, v2);
}

TEST_F(DBPropertiesTest, GetAggregatedIntPropertyTest) {
  const int kKeySize = 100;
  const int kValueSize = 500;
  const int kKeyNum = 100;

  Options options;
  options.env = env_;
  options.create_if_missing = true;
  options.write_buffer_size = (kKeySize + kValueSize) * kKeyNum / 10;
  // Make them never flush
  options.min_write_buffer_number_to_merge = 1000;
  options.max_write_buffer_number = 1000;
  options = CurrentOptions(options);
  CreateAndReopenWithCF({"one", "two", "three", "four"}, options);

  Random rnd(301);
  for (auto* handle : handles_) {
    for (int i = 0; i < kKeyNum; ++i) {
      ASSERT_OK(db_->Put(
          WriteOptions(), handle, RandomString(&rnd, kKeySize), RandomString(&rnd, kValueSize)));
    }
  }

  uint64_t manual_sum = 0;
  uint64_t api_sum = 0;
  uint64_t value = 0;
  for (auto* handle : handles_) {
    ASSERT_TRUE(
        db_->GetIntProperty(handle, DB::Properties::kSizeAllMemTables, &value));
    manual_sum += value;
  }
  ASSERT_TRUE(db_->GetAggregatedIntProperty(DB::Properties::kSizeAllMemTables,
                                            &api_sum));
  ASSERT_GT(manual_sum, 0);
  ASSERT_EQ(manual_sum, api_sum);

  ASSERT_FALSE(db_->GetAggregatedIntProperty(DB::Properties::kDBStats, &value));

  uint64_t before_flush_trm;
  uint64_t after_flush_trm;
  for (auto* handle : handles_) {
    ASSERT_TRUE(db_->GetAggregatedIntProperty(
        DB::Properties::kEstimateTableReadersMem, &before_flush_trm));

    // Issue flush and expect larger memory usage of table readers.
    ASSERT_OK(db_->Flush(FlushOptions(), handle));

    ASSERT_TRUE(db_->GetAggregatedIntProperty(
        DB::Properties::kEstimateTableReadersMem, &after_flush_trm));
    ASSERT_GT(after_flush_trm, before_flush_trm);
  }
}

namespace {
void ResetTableProperties(TableProperties* tp) {
  tp->data_size = 0;
  tp->data_index_size = 0;
  tp->filter_size = 0;
  tp->filter_index_size = 0;
  tp->raw_key_size = 0;
  tp->raw_value_size = 0;
  tp->num_data_blocks = 0;
  tp->num_entries = 0;
  tp->num_filter_blocks = 0;
  tp->num_data_index_blocks = 0;
}

void ParseTablePropertiesString(std::string tp_string, TableProperties* tp) {
  double dummy_double;
  std::replace(tp_string.begin(), tp_string.end(), ';', ' ');
  std::replace(tp_string.begin(), tp_string.end(), '=', ' ');
  ResetTableProperties(tp);

  sscanf(tp_string.c_str(),
         "# data blocks %" SCNu64
         " # data index blocks %" SCNu64
         " # filter blocks %" SCNu64
         " # entries %" SCNu64
         " raw key size %" SCNu64
         " raw average key size %lf "
         " raw value size %" SCNu64
         " raw average value size %lf "
         " data blocks total size %" SCNu64
         " data index size %" SCNu64
         " filter blocks total size %" SCNu64
         " filter index block size %" SCNu64,
         &tp->num_data_blocks, &tp->num_data_index_blocks, &tp->num_filter_blocks, &tp->num_entries,
         &tp->raw_key_size, &dummy_double, &tp->raw_value_size, &dummy_double, &tp->data_size,
         &tp->data_index_size, &tp->filter_size, &tp->filter_index_size);
}

void VerifySimilar(uint64_t a, uint64_t b, double bias, const std::string& label) {
  ASSERT_EQ(a == 0U, b == 0U) << " " << label << ": " << a << " vs " << b;
  if (a == 0) {
    return;
  }
  const double dbl_a = static_cast<double>(a);
  const double dbl_b = static_cast<double>(b);
  ASSERT_LT(fabs(dbl_a - dbl_b) / (dbl_a + dbl_b), bias)
      << " " << label << ": " << a << " vs " << b;
}

void VerifyTableProperties(const TableProperties& base_tp,
                           const TableProperties& new_tp,
                           double filter_size_bias = 0.1,
                           double index_size_bias = 0.1,
                           double data_size_bias = 0.1,
                           double num_data_blocks_bias = 0.05) {
  VerifySimilar(base_tp.data_size, new_tp.data_size, data_size_bias, "data_size");
  VerifySimilar(
      base_tp.data_index_size, new_tp.data_index_size, index_size_bias, "data_index_size");
  VerifySimilar(base_tp.filter_size, new_tp.filter_size, filter_size_bias, "filter_size");
  VerifySimilar(
      base_tp.filter_index_size, new_tp.filter_index_size, index_size_bias, "filter_index_size");
  VerifySimilar(
      base_tp.num_data_blocks, new_tp.num_data_blocks, num_data_blocks_bias, "num_data_blocks");
  ASSERT_EQ(base_tp.raw_key_size, new_tp.raw_key_size);
  ASSERT_EQ(base_tp.raw_value_size, new_tp.raw_value_size);
  ASSERT_EQ(base_tp.num_entries, new_tp.num_entries);
}

void GetExpectedTableProperties(TableProperties* expected_tp,
                                const int kKeySize, const int kValueSize,
                                const int kKeysPerTable, const int kTableCount,
                                const int kBloomBitsPerKey,
                                const size_t kBlockSize) {
  const int kKeyCount = kTableCount * kKeysPerTable;
  const int kAvgSuccessorSize = kKeySize / 6;
  const int kEncodingSavePerKey = kKeySize / 4;
  expected_tp->raw_key_size = kKeyCount * (kKeySize + 8);
  expected_tp->raw_value_size = kKeyCount * kValueSize;
  expected_tp->num_entries = kKeyCount;
  expected_tp->num_data_blocks =
      kTableCount *
      (kKeysPerTable * (kKeySize - kEncodingSavePerKey + kValueSize)) /
      kBlockSize;
  expected_tp->data_size =
      kTableCount * (kKeysPerTable * (kKeySize + 8 + kValueSize));
  expected_tp->data_index_size =
      expected_tp->num_data_blocks * (kAvgSuccessorSize + 12);
  expected_tp->filter_index_size = 0; // Only applicable to fixed-size filter.
  expected_tp->filter_size =
      kTableCount * (kKeysPerTable * kBloomBitsPerKey / 8);
  // For block-based filter number of data and filter blocks is the same.
  expected_tp->num_filter_blocks = expected_tp->num_data_blocks;
}
}  // anonymous namespace

TEST_F(DBPropertiesTest, ValidatePropertyInfo) {
  for (const auto& ppt_name_and_info : InternalStats::ppt_name_to_info) {
    // If C++ gets a std::string_literal, this would be better to check at
    // compile-time using static_assert.
    ASSERT_TRUE(ppt_name_and_info.first.empty() ||
                !isdigit(ppt_name_and_info.first.back()));

    ASSERT_TRUE((ppt_name_and_info.second.handle_string == nullptr) !=
                (ppt_name_and_info.second.handle_int == nullptr));
  }
}

TEST_F(DBPropertiesTest, AggregatedTableProperties) {
  for (int kTableCount = 40; kTableCount <= 100; kTableCount += 30) {
    const int kKeysPerTable = 100;
    const int kKeySize = 80;
    const int kValueSize = 200;
    const int kBloomBitsPerKey = 20;

    Options options = CurrentOptions();
    options.level0_file_num_compaction_trigger = 8;
    options.compression = kNoCompression;
    options.create_if_missing = true;

    BlockBasedTableOptions table_options;
    table_options.filter_policy.reset(
        NewBloomFilterPolicy(kBloomBitsPerKey, false));
    table_options.block_size = 1024;
    options.table_factory.reset(new BlockBasedTableFactory(table_options));

    DestroyAndReopen(options);

    Random rnd(5632);
    for (int table = 1; table <= kTableCount; ++table) {
      for (int i = 0; i < kKeysPerTable; ++i) {
        ASSERT_OK(db_->Put(
            WriteOptions(), RandomString(&rnd, kKeySize), RandomString(&rnd, kValueSize)));
      }
      ASSERT_OK(db_->Flush(FlushOptions()));
    }
    std::string property;
    db_->GetProperty(DB::Properties::kAggregatedTableProperties, &property);

    TableProperties expected_tp;
    GetExpectedTableProperties(&expected_tp, kKeySize, kValueSize,
                               kKeysPerTable, kTableCount, kBloomBitsPerKey,
                               table_options.block_size);

    TableProperties output_tp;
    ParseTablePropertiesString(property, &output_tp);

    VerifyTableProperties(expected_tp, output_tp);
  }
}

TEST_F(DBPropertiesTest, ReadLatencyHistogramByLevel) {
  Options options = CurrentOptions();
  options.write_buffer_size = 110 << 10;
  options.level0_file_num_compaction_trigger = 6;
  options.num_levels = 4;
  options.compression = kNoCompression;
  options.max_bytes_for_level_base = 4500 << 10;
  options.target_file_size_base = 98 << 10;
  options.max_write_buffer_number = 2;
  options.statistics = rocksdb::CreateDBStatisticsForTests();
  options.max_open_files = 100;

  BlockBasedTableOptions table_options;
  table_options.no_block_cache = true;

  DestroyAndReopen(options);
  int key_index = 0;
  Random rnd(301);
  for (int num = 0; num < 8; num++) {
    ASSERT_OK(Put("foo", "bar"));
    GenerateNewFile(&rnd, &key_index);
    ASSERT_OK(dbfull()->TEST_WaitForCompact());
  }
  ASSERT_OK(dbfull()->TEST_WaitForCompact());

  std::string prop;
  ASSERT_TRUE(dbfull()->GetProperty("rocksdb.dbstats", &prop));

  // Get() after flushes, See latency histogram tracked.
  for (int key = 0; key < key_index; key++) {
    Get(Key(key));
  }
  ASSERT_TRUE(dbfull()->GetProperty("rocksdb.dbstats", &prop));
  ASSERT_NE(std::string::npos, prop.find("** Level 0 read latency histogram"));
  ASSERT_NE(std::string::npos, prop.find("** Level 1 read latency histogram"));
  ASSERT_EQ(std::string::npos, prop.find("** Level 2 read latency histogram"));

  // Reopen and issue Get(). See thee latency tracked
  Reopen(options);
  ASSERT_OK(dbfull()->TEST_WaitForCompact());
  for (int key = 0; key < key_index; key++) {
    Get(Key(key));
  }
  ASSERT_TRUE(dbfull()->GetProperty("rocksdb.dbstats", &prop));
  ASSERT_NE(std::string::npos, prop.find("** Level 0 read latency histogram"));
  ASSERT_NE(std::string::npos, prop.find("** Level 1 read latency histogram"));
  ASSERT_EQ(std::string::npos, prop.find("** Level 2 read latency histogram"));

  // Reopen and issue iterating. See thee latency tracked
  Reopen(options);
  ASSERT_TRUE(dbfull()->GetProperty("rocksdb.dbstats", &prop));
  ASSERT_EQ(std::string::npos, prop.find("** Level 0 read latency histogram"));
  ASSERT_EQ(std::string::npos, prop.find("** Level 1 read latency histogram"));
  ASSERT_EQ(std::string::npos, prop.find("** Level 2 read latency histogram"));
  {
    unique_ptr<Iterator> iter(db_->NewIterator(ReadOptions()));
    for (iter->Seek(Key(0)); ASSERT_RESULT(iter->CheckedValid()); iter->Next()) {
    }
  }
  ASSERT_TRUE(dbfull()->GetProperty("rocksdb.dbstats", &prop));
  ASSERT_NE(std::string::npos, prop.find("** Level 0 read latency histogram"));
  ASSERT_NE(std::string::npos, prop.find("** Level 1 read latency histogram"));
  ASSERT_EQ(std::string::npos, prop.find("** Level 2 read latency histogram"));

  // options.max_open_files preloads table readers.
  options.max_open_files = -1;
  Reopen(options);
  ASSERT_TRUE(dbfull()->GetProperty("rocksdb.dbstats", &prop));
  ASSERT_NE(std::string::npos, prop.find("** Level 0 read latency histogram"));
  ASSERT_NE(std::string::npos, prop.find("** Level 1 read latency histogram"));
  ASSERT_EQ(std::string::npos, prop.find("** Level 2 read latency histogram"));
  for (int key = 0; key < key_index; key++) {
    Get(Key(key));
  }
  ASSERT_TRUE(dbfull()->GetProperty("rocksdb.dbstats", &prop));
  ASSERT_NE(std::string::npos, prop.find("** Level 0 read latency histogram"));
  ASSERT_NE(std::string::npos, prop.find("** Level 1 read latency histogram"));
  ASSERT_EQ(std::string::npos, prop.find("** Level 2 read latency histogram"));
}

TEST_F(DBPropertiesTest, AggregatedTablePropertiesAtLevel) {
  const int kTableCount = 100;
  const int kKeysPerTable = 10;
  const int kKeySize = 50;
  const int kValueSize = 400;
  const int kMaxLevel = 7;
  const int kBloomBitsPerKey = 20;
  Random rnd(301);
  Options options = CurrentOptions();
  options.level0_file_num_compaction_trigger = 8;
  options.compression = kNoCompression;
  options.create_if_missing = true;
  options.level0_file_num_compaction_trigger = 2;
  options.target_file_size_base = 8192;
  options.max_bytes_for_level_base = 10000;
  options.max_bytes_for_level_multiplier = 2;
  // This ensures there no compaction happening when we call GetProperty().
  options.disable_auto_compactions = true;

  BlockBasedTableOptions table_options;
  table_options.filter_policy.reset(
      NewBloomFilterPolicy(kBloomBitsPerKey, false));
  table_options.block_size = 1024;
  options.table_factory.reset(new BlockBasedTableFactory(table_options));

  DestroyAndReopen(options);

  std::string level_tp_strings[kMaxLevel];
  std::string tp_string;
  TableProperties level_tps[kMaxLevel];
  TableProperties tp, sum_tp, expected_tp;
  for (int table = 1; table <= kTableCount; ++table) {
    for (int i = 0; i < kKeysPerTable; ++i) {
      ASSERT_OK(db_->Put(WriteOptions(), RandomString(&rnd, kKeySize),
                         RandomString(&rnd, kValueSize)));
    }
    ASSERT_OK(db_->Flush(FlushOptions()));
    ASSERT_OK(db_->CompactRange(CompactRangeOptions(), nullptr, nullptr));
    ResetTableProperties(&sum_tp);
    for (int level = 0; level < kMaxLevel; ++level) {
      db_->GetProperty(
          DB::Properties::kAggregatedTablePropertiesAtLevel + ToString(level),
          &level_tp_strings[level]);
      ParseTablePropertiesString(level_tp_strings[level], &level_tps[level]);
      sum_tp.data_size += level_tps[level].data_size;
      sum_tp.data_index_size += level_tps[level].data_index_size;
      sum_tp.filter_size += level_tps[level].filter_size;
      sum_tp.filter_index_size += level_tps[level].filter_index_size;
      sum_tp.raw_key_size += level_tps[level].raw_key_size;
      sum_tp.raw_value_size += level_tps[level].raw_value_size;
      sum_tp.num_data_blocks += level_tps[level].num_data_blocks;
      sum_tp.num_entries += level_tps[level].num_entries;
      sum_tp.num_filter_blocks += level_tps[level].num_filter_blocks;
      sum_tp.num_data_index_blocks += level_tps[level].num_data_index_blocks;
    }
    db_->GetProperty(DB::Properties::kAggregatedTableProperties, &tp_string);
    ParseTablePropertiesString(tp_string, &tp);
    ASSERT_EQ(sum_tp.data_size, tp.data_size);
    ASSERT_EQ(sum_tp.data_index_size, tp.data_index_size);
    ASSERT_EQ(sum_tp.filter_size, tp.filter_size);
    ASSERT_EQ(sum_tp.filter_index_size, tp.filter_index_size);
    ASSERT_EQ(sum_tp.raw_key_size, tp.raw_key_size);
    ASSERT_EQ(sum_tp.raw_value_size, tp.raw_value_size);
    ASSERT_EQ(sum_tp.num_data_blocks, tp.num_data_blocks);
    ASSERT_EQ(sum_tp.num_entries, tp.num_entries);
    ASSERT_EQ(sum_tp.num_filter_blocks, tp.num_filter_blocks);
    ASSERT_EQ(sum_tp.num_data_index_blocks, tp.num_data_index_blocks);
    if (table > 3) {
      GetExpectedTableProperties(&expected_tp, kKeySize, kValueSize,
                                 kKeysPerTable, table, kBloomBitsPerKey,
                                 table_options.block_size);
      // Gives larger bias here as index block size, filter block size,
      // and data block size become much harder to estimate in this test.
      VerifyTableProperties(tp, expected_tp, 0.5, 0.4, 0.4, 0.25);
    }
  }
}

TEST_F(DBPropertiesTest, NumImmutableMemTable) {
  do {
    Options options = CurrentOptions();
    WriteOptions writeOpt = WriteOptions();
    writeOpt.disableWAL = true;
    options.max_write_buffer_number = 4;
    options.min_write_buffer_number_to_merge = 3;
    options.max_write_buffer_number_to_maintain = 4;
    options.write_buffer_size = 1000000;
    CreateAndReopenWithCF({"pikachu"}, options);

    std::string big_value(1000000 * 2, 'x');
    std::string num;
    SetPerfLevel(PerfLevel::kEnableTime);
    ASSERT_TRUE(GetPerfLevel() == PerfLevel::kEnableTime);

    ASSERT_OK(dbfull()->Put(writeOpt, handles_[1], "k1", big_value));
    ASSERT_TRUE(dbfull()->GetProperty(handles_[1],
                                      "rocksdb.num-immutable-mem-table", &num));
    ASSERT_EQ(num, "0");
    ASSERT_TRUE(dbfull()->GetProperty(
        handles_[1], DB::Properties::kNumImmutableMemTableFlushed, &num));
    ASSERT_EQ(num, "0");
    ASSERT_TRUE(dbfull()->GetProperty(
        handles_[1], "rocksdb.num-entries-active-mem-table", &num));
    ASSERT_EQ(num, "1");
    perf_context.Reset();
    Get(1, "k1");
    ASSERT_EQ(1, static_cast<int>(perf_context.get_from_memtable_count));

    ASSERT_OK(dbfull()->Put(writeOpt, handles_[1], "k2", big_value));
    ASSERT_TRUE(dbfull()->GetProperty(handles_[1],
                                      "rocksdb.num-immutable-mem-table", &num));
    ASSERT_EQ(num, "1");
    ASSERT_TRUE(dbfull()->GetProperty(
        handles_[1], "rocksdb.num-entries-active-mem-table", &num));
    ASSERT_EQ(num, "1");
    ASSERT_TRUE(dbfull()->GetProperty(
        handles_[1], "rocksdb.num-entries-imm-mem-tables", &num));
    ASSERT_EQ(num, "1");

    perf_context.Reset();
    Get(1, "k1");
    ASSERT_EQ(2, static_cast<int>(perf_context.get_from_memtable_count));
    perf_context.Reset();
    Get(1, "k2");
    ASSERT_EQ(1, static_cast<int>(perf_context.get_from_memtable_count));

    ASSERT_OK(dbfull()->Put(writeOpt, handles_[1], "k3", big_value));
    ASSERT_TRUE(dbfull()->GetProperty(
        handles_[1], "rocksdb.cur-size-active-mem-table", &num));
    ASSERT_TRUE(dbfull()->GetProperty(handles_[1],
                                      "rocksdb.num-immutable-mem-table", &num));
    ASSERT_EQ(num, "2");
    ASSERT_TRUE(dbfull()->GetProperty(
        handles_[1], "rocksdb.num-entries-active-mem-table", &num));
    ASSERT_EQ(num, "1");
    ASSERT_TRUE(dbfull()->GetProperty(
        handles_[1], "rocksdb.num-entries-imm-mem-tables", &num));
    ASSERT_EQ(num, "2");
    perf_context.Reset();
    Get(1, "k2");
    ASSERT_EQ(2, static_cast<int>(perf_context.get_from_memtable_count));
    perf_context.Reset();
    Get(1, "k3");
    ASSERT_EQ(1, static_cast<int>(perf_context.get_from_memtable_count));
    perf_context.Reset();
    Get(1, "k1");
    ASSERT_EQ(3, static_cast<int>(perf_context.get_from_memtable_count));

    ASSERT_OK(Flush(1));
    ASSERT_TRUE(dbfull()->GetProperty(handles_[1],
                                      "rocksdb.num-immutable-mem-table", &num));
    ASSERT_EQ(num, "0");
    ASSERT_TRUE(dbfull()->GetProperty(
        handles_[1], DB::Properties::kNumImmutableMemTableFlushed, &num));
    ASSERT_EQ(num, "3");
    ASSERT_TRUE(dbfull()->GetProperty(
        handles_[1], "rocksdb.cur-size-active-mem-table", &num));
    // "192" is the size of the metadata of an empty skiplist, this would
    // break if we change the default skiplist implementation
    ASSERT_EQ(num, "192");

    uint64_t int_num;
    uint64_t base_total_size;
    ASSERT_TRUE(dbfull()->GetIntProperty(
        handles_[1], "rocksdb.estimate-num-keys", &base_total_size));

    ASSERT_OK(dbfull()->Delete(writeOpt, handles_[1], "k2"));
    ASSERT_OK(dbfull()->Put(writeOpt, handles_[1], "k3", ""));
    ASSERT_OK(dbfull()->Delete(writeOpt, handles_[1], "k3"));
    ASSERT_TRUE(dbfull()->GetIntProperty(
        handles_[1], "rocksdb.num-deletes-active-mem-table", &int_num));
    ASSERT_EQ(int_num, 2U);
    ASSERT_TRUE(dbfull()->GetIntProperty(
        handles_[1], "rocksdb.num-entries-active-mem-table", &int_num));
    ASSERT_EQ(int_num, 3U);

    ASSERT_OK(dbfull()->Put(writeOpt, handles_[1], "k2", big_value));
    ASSERT_OK(dbfull()->Put(writeOpt, handles_[1], "k2", big_value));
    ASSERT_TRUE(dbfull()->GetIntProperty(
        handles_[1], "rocksdb.num-entries-imm-mem-tables", &int_num));
    ASSERT_EQ(int_num, 4U);
    ASSERT_TRUE(dbfull()->GetIntProperty(
        handles_[1], "rocksdb.num-deletes-imm-mem-tables", &int_num));
    ASSERT_EQ(int_num, 2U);

    ASSERT_TRUE(dbfull()->GetIntProperty(
        handles_[1], "rocksdb.estimate-num-keys", &int_num));
    ASSERT_EQ(int_num, base_total_size + 1);

    SetPerfLevel(PerfLevel::kDisable);
    ASSERT_TRUE(GetPerfLevel() == PerfLevel::kDisable);
  } while (ChangeCompactOptions());
}

TEST_F(DBPropertiesTest, GetProperty) {
  // Set sizes to both background thread pool to be 1 and block them.
  env_->SetBackgroundThreads(1, Env::HIGH);
  env_->SetBackgroundThreads(1, Env::LOW);
  test::SleepingBackgroundTask sleeping_task_low;
  env_->Schedule(&test::SleepingBackgroundTask::DoSleepTask, &sleeping_task_low,
                 Env::Priority::LOW);
  test::SleepingBackgroundTask sleeping_task_high;
  env_->Schedule(&test::SleepingBackgroundTask::DoSleepTask,
                 &sleeping_task_high, Env::Priority::HIGH);

  Options options = CurrentOptions();
  WriteOptions writeOpt = WriteOptions();
  writeOpt.disableWAL = true;
  options.compaction_style = kCompactionStyleUniversal;
  options.level0_file_num_compaction_trigger = 1;
  options.compaction_options_universal.size_ratio = 50;
  options.max_background_compactions = 1;
  options.max_background_flushes = 1;
  options.max_write_buffer_number = 10;
  options.min_write_buffer_number_to_merge = 1;
  options.max_write_buffer_number_to_maintain = 0;
  options.write_buffer_size = 1000000;
  Reopen(options);

  std::string big_value(1000000 * 2, 'x');
  std::string num;
  uint64_t int_num;
  SetPerfLevel(PerfLevel::kEnableTime);

  ASSERT_TRUE(
      dbfull()->GetIntProperty("rocksdb.estimate-table-readers-mem", &int_num));
  ASSERT_EQ(int_num, 0U);
  ASSERT_TRUE(
      dbfull()->GetIntProperty("rocksdb.estimate-live-data-size", &int_num));
  ASSERT_EQ(int_num, 0U);

  ASSERT_OK(dbfull()->Put(writeOpt, "k1", big_value));
  ASSERT_TRUE(dbfull()->GetProperty("rocksdb.num-immutable-mem-table", &num));
  ASSERT_EQ(num, "0");
  ASSERT_TRUE(dbfull()->GetProperty("rocksdb.mem-table-flush-pending", &num));
  ASSERT_EQ(num, "0");
  ASSERT_TRUE(dbfull()->GetProperty("rocksdb.compaction-pending", &num));
  ASSERT_EQ(num, "0");
  ASSERT_TRUE(dbfull()->GetProperty("rocksdb.estimate-num-keys", &num));
  ASSERT_EQ(num, "1");
  perf_context.Reset();

  ASSERT_OK(dbfull()->Put(writeOpt, "k2", big_value));
  ASSERT_TRUE(dbfull()->GetProperty("rocksdb.num-immutable-mem-table", &num));
  ASSERT_EQ(num, "1");
  ASSERT_OK(dbfull()->Delete(writeOpt, "k-non-existing"));
  ASSERT_OK(dbfull()->Put(writeOpt, "k3", big_value));
  ASSERT_TRUE(dbfull()->GetProperty("rocksdb.num-immutable-mem-table", &num));
  ASSERT_EQ(num, "2");
  ASSERT_TRUE(dbfull()->GetProperty("rocksdb.mem-table-flush-pending", &num));
  ASSERT_EQ(num, "1");
  ASSERT_TRUE(dbfull()->GetProperty("rocksdb.compaction-pending", &num));
  ASSERT_EQ(num, "0");
  ASSERT_TRUE(dbfull()->GetProperty("rocksdb.estimate-num-keys", &num));
  ASSERT_EQ(num, "2");
  // Verify the same set of properties through GetIntProperty
  ASSERT_TRUE(
      dbfull()->GetIntProperty("rocksdb.num-immutable-mem-table", &int_num));
  ASSERT_EQ(int_num, 2U);
  ASSERT_TRUE(
      dbfull()->GetIntProperty("rocksdb.mem-table-flush-pending", &int_num));
  ASSERT_EQ(int_num, 1U);
  ASSERT_TRUE(dbfull()->GetIntProperty("rocksdb.compaction-pending", &int_num));
  ASSERT_EQ(int_num, 0U);
  ASSERT_TRUE(dbfull()->GetIntProperty("rocksdb.estimate-num-keys", &int_num));
  ASSERT_EQ(int_num, 2U);

  ASSERT_TRUE(
      dbfull()->GetIntProperty("rocksdb.estimate-table-readers-mem", &int_num));
  ASSERT_EQ(int_num, 0U);

  sleeping_task_high.WakeUp();
  sleeping_task_high.WaitUntilDone();
  ASSERT_OK(dbfull()->TEST_WaitForFlushMemTable());

  ASSERT_OK(dbfull()->Put(writeOpt, "k4", big_value));
  ASSERT_OK(dbfull()->Put(writeOpt, "k5", big_value));
  ASSERT_OK(dbfull()->TEST_WaitForFlushMemTable());
  ASSERT_TRUE(dbfull()->GetProperty("rocksdb.mem-table-flush-pending", &num));
  ASSERT_EQ(num, "0");
  ASSERT_TRUE(dbfull()->GetProperty("rocksdb.compaction-pending", &num));
  ASSERT_EQ(num, "1");
  ASSERT_TRUE(dbfull()->GetProperty("rocksdb.estimate-num-keys", &num));
  ASSERT_EQ(num, "4");

  ASSERT_TRUE(
      dbfull()->GetIntProperty("rocksdb.estimate-table-readers-mem", &int_num));
  ASSERT_GT(int_num, 0U);

  sleeping_task_low.WakeUp();
  sleeping_task_low.WaitUntilDone();

  // Wait for compaction to be done. This is important because otherwise RocksDB
  // might schedule a compaction when reopening the database, failing assertion
  // (A) as a result.
  ASSERT_OK(dbfull()->TEST_WaitForCompact());
  options.max_open_files = 10;
  Reopen(options);
  // After reopening, no table reader is loaded, so no memory for table readers
  ASSERT_TRUE(
      dbfull()->GetIntProperty("rocksdb.estimate-table-readers-mem", &int_num));
  ASSERT_EQ(int_num, 0U);  // (A)
  ASSERT_TRUE(dbfull()->GetIntProperty("rocksdb.estimate-num-keys", &int_num));
  ASSERT_GT(int_num, 0U);

  // After reading a key, at least one table reader is loaded.
  Get("k5");
  ASSERT_TRUE(
      dbfull()->GetIntProperty("rocksdb.estimate-table-readers-mem", &int_num));
  ASSERT_GT(int_num, 0U);

  // Test rocksdb.num-live-versions
  {
    options.level0_file_num_compaction_trigger = 20;
    Reopen(options);
    ASSERT_TRUE(
        dbfull()->GetIntProperty("rocksdb.num-live-versions", &int_num));
    ASSERT_EQ(int_num, 1U);

    // Use an iterator to hold current version
    std::unique_ptr<Iterator> iter1(dbfull()->NewIterator(ReadOptions()));

    ASSERT_OK(dbfull()->Put(writeOpt, "k6", big_value));
    ASSERT_OK(Flush());;
    ASSERT_TRUE(
        dbfull()->GetIntProperty("rocksdb.num-live-versions", &int_num));
    ASSERT_EQ(int_num, 2U);

    // Use an iterator to hold current version
    std::unique_ptr<Iterator> iter2(dbfull()->NewIterator(ReadOptions()));

    ASSERT_OK(dbfull()->Put(writeOpt, "k7", big_value));
    ASSERT_OK(Flush());;
    ASSERT_TRUE(
        dbfull()->GetIntProperty("rocksdb.num-live-versions", &int_num));
    ASSERT_EQ(int_num, 3U);

    iter2.reset();
    ASSERT_TRUE(
        dbfull()->GetIntProperty("rocksdb.num-live-versions", &int_num));
    ASSERT_EQ(int_num, 2U);

    iter1.reset();
    ASSERT_TRUE(
        dbfull()->GetIntProperty("rocksdb.num-live-versions", &int_num));
    ASSERT_EQ(int_num, 1U);
  }
}

TEST_F(DBPropertiesTest, ApproximateMemoryUsage) {
  const int kNumRounds = 10;
  // TODO(noetzli) kFlushesPerRound does not really correlate with how many
  // flushes happen.
  const int kFlushesPerRound = 10;
  const int kWritesPerFlush = 10;
  const int kKeySize = 100;
  const int kValueSize = 1000;
  Options options;
  options.write_buffer_size = 1000;  // small write buffer
  options.min_write_buffer_number_to_merge = 4;
  options.compression = kNoCompression;
  options.create_if_missing = true;
  options = CurrentOptions(options);
  DestroyAndReopen(options);

  Random rnd(301);

  std::vector<Iterator*> iters;

  uint64_t active_mem;
  uint64_t unflushed_mem;
  uint64_t all_mem;
  uint64_t prev_all_mem;

  // Phase 0. The verify the initial value of all these properties are the same
  // as we have no mem-tables.
  dbfull()->GetIntProperty("rocksdb.cur-size-active-mem-table", &active_mem);
  dbfull()->GetIntProperty("rocksdb.cur-size-all-mem-tables", &unflushed_mem);
  dbfull()->GetIntProperty("rocksdb.size-all-mem-tables", &all_mem);
  ASSERT_EQ(all_mem, active_mem);
  ASSERT_EQ(all_mem, unflushed_mem);

  // Phase 1. Simply issue Put() and expect "cur-size-all-mem-tables" equals to
  // "size-all-mem-tables"
  for (int r = 0; r < kNumRounds; ++r) {
    for (int f = 0; f < kFlushesPerRound; ++f) {
      for (int w = 0; w < kWritesPerFlush; ++w) {
        ASSERT_OK(Put(RandomString(&rnd, kKeySize), RandomString(&rnd, kValueSize)));
      }
    }
    // Make sure that there is no flush between getting the two properties.
    ASSERT_OK(dbfull()->TEST_WaitForFlushMemTable());
    dbfull()->GetIntProperty("rocksdb.cur-size-all-mem-tables", &unflushed_mem);
    dbfull()->GetIntProperty("rocksdb.size-all-mem-tables", &all_mem);
    // in no iterator case, these two number should be the same.
    ASSERT_EQ(unflushed_mem, all_mem);
  }
  prev_all_mem = all_mem;

  // Phase 2. Keep issuing Put() but also create new iterators. This time we
  // expect "size-all-mem-tables" > "cur-size-all-mem-tables".
  for (int r = 0; r < kNumRounds; ++r) {
    iters.push_back(db_->NewIterator(ReadOptions()));
    for (int f = 0; f < kFlushesPerRound; ++f) {
      for (int w = 0; w < kWritesPerFlush; ++w) {
        ASSERT_OK(Put(RandomString(&rnd, kKeySize), RandomString(&rnd, kValueSize)));
      }
    }
    // Force flush to prevent flush from happening between getting the
    // properties or after getting the properties and before the new round.
    ASSERT_OK(Flush());;

    // In the second round, add iterators.
    dbfull()->GetIntProperty("rocksdb.cur-size-active-mem-table", &active_mem);
    dbfull()->GetIntProperty("rocksdb.cur-size-all-mem-tables", &unflushed_mem);
    dbfull()->GetIntProperty("rocksdb.size-all-mem-tables", &all_mem);
    ASSERT_GT(all_mem, active_mem);
    ASSERT_GT(all_mem, unflushed_mem);
    ASSERT_GT(all_mem, prev_all_mem);
    prev_all_mem = all_mem;
  }

  // Phase 3. Delete iterators and expect "size-all-mem-tables" shrinks
  // whenever we release an iterator.
  for (auto* iter : iters) {
    delete iter;
    dbfull()->GetIntProperty("rocksdb.size-all-mem-tables", &all_mem);
    // Expect the size shrinking
    ASSERT_LT(all_mem, prev_all_mem);
    prev_all_mem = all_mem;
  }

  // Expect all these three counters to be the same.
  dbfull()->GetIntProperty("rocksdb.cur-size-active-mem-table", &active_mem);
  dbfull()->GetIntProperty("rocksdb.cur-size-all-mem-tables", &unflushed_mem);
  dbfull()->GetIntProperty("rocksdb.size-all-mem-tables", &all_mem);
  ASSERT_EQ(active_mem, unflushed_mem);
  ASSERT_EQ(unflushed_mem, all_mem);

  // Phase 5. Reopen, and expect all these three counters to be the same again.
  Reopen(options);
  dbfull()->GetIntProperty("rocksdb.cur-size-active-mem-table", &active_mem);
  dbfull()->GetIntProperty("rocksdb.cur-size-all-mem-tables", &unflushed_mem);
  dbfull()->GetIntProperty("rocksdb.size-all-mem-tables", &all_mem);
  ASSERT_EQ(active_mem, unflushed_mem);
  ASSERT_EQ(unflushed_mem, all_mem);
}

TEST_F(DBPropertiesTest, EstimatePendingCompBytes) {
  // Set sizes to both background thread pool to be 1 and block them.
  env_->SetBackgroundThreads(1, Env::HIGH);
  env_->SetBackgroundThreads(1, Env::LOW);
  test::SleepingBackgroundTask sleeping_task_low;
  env_->Schedule(&test::SleepingBackgroundTask::DoSleepTask, &sleeping_task_low,
                 Env::Priority::LOW);

  Options options = CurrentOptions();
  WriteOptions writeOpt = WriteOptions();
  writeOpt.disableWAL = true;
  options.compaction_style = kCompactionStyleLevel;
  options.level0_file_num_compaction_trigger = 2;
  options.max_background_compactions = 1;
  options.max_background_flushes = 1;
  options.max_write_buffer_number = 10;
  options.min_write_buffer_number_to_merge = 1;
  options.max_write_buffer_number_to_maintain = 0;
  options.write_buffer_size = 1000000;
  Reopen(options);

  std::string big_value(1000000 * 2, 'x');
  std::string num;
  uint64_t int_num;

  ASSERT_OK(dbfull()->Put(writeOpt, "k1", big_value));
  ASSERT_OK(Flush());;
  ASSERT_TRUE(dbfull()->GetIntProperty(
      "rocksdb.estimate-pending-compaction-bytes", &int_num));
  ASSERT_EQ(int_num, 0U);

  ASSERT_OK(dbfull()->Put(writeOpt, "k2", big_value));
  ASSERT_OK(Flush());;
  ASSERT_TRUE(dbfull()->GetIntProperty(
      "rocksdb.estimate-pending-compaction-bytes", &int_num));
  ASSERT_EQ(int_num, 0U);

  ASSERT_OK(dbfull()->Put(writeOpt, "k3", big_value));
  ASSERT_OK(Flush());;
  ASSERT_TRUE(dbfull()->GetIntProperty(
      "rocksdb.estimate-pending-compaction-bytes", &int_num));
  ASSERT_GT(int_num, 0U);

  sleeping_task_low.WakeUp();
  sleeping_task_low.WaitUntilDone();

  ASSERT_OK(dbfull()->TEST_WaitForCompact());
  ASSERT_TRUE(dbfull()->GetIntProperty(
      "rocksdb.estimate-pending-compaction-bytes", &int_num));
  ASSERT_EQ(int_num, 0U);
}

class CountingUserTblPropCollector : public TablePropertiesCollector {
 public:
  const char* Name() const override { return "CountingUserTblPropCollector"; }

  Status Finish(UserCollectedProperties* properties) override {
    std::string encoded;
    PutVarint32(&encoded, count_);
    *properties = UserCollectedProperties{
        {"CountingUserTblPropCollector", message_}, {"Count", encoded},
    };
    return Status::OK();
  }

  Status AddUserKey(const Slice& user_key, const Slice& value, EntryType type,
                    SequenceNumber seq, uint64_t file_size) override {
    ++count_;
    return Status::OK();
  }

  UserCollectedProperties GetReadableProperties() const override {
    return UserCollectedProperties{};
  }

 private:
  std::string message_ = "Rocksdb";
  uint32_t count_ = 0;
};

class CountingUserTblPropCollectorFactory
    : public TablePropertiesCollectorFactory {
 public:
  explicit CountingUserTblPropCollectorFactory(
      uint32_t expected_column_family_id)
      : expected_column_family_id_(expected_column_family_id),
        num_created_(0) {}
  virtual TablePropertiesCollector* CreateTablePropertiesCollector(
      TablePropertiesCollectorFactory::Context context) override {
    EXPECT_EQ(expected_column_family_id_, context.column_family_id);
    num_created_++;
    return new CountingUserTblPropCollector();
  }
  const char* Name() const override {
    return "CountingUserTblPropCollectorFactory";
  }
  void set_expected_column_family_id(uint32_t v) {
    expected_column_family_id_ = v;
  }
  uint32_t expected_column_family_id_;
  uint32_t num_created_;
};

class CountingDeleteTabPropCollector : public TablePropertiesCollector {
 public:
  const char* Name() const override { return "CountingDeleteTabPropCollector"; }

  Status AddUserKey(const Slice& user_key, const Slice& value, EntryType type,
                    SequenceNumber seq, uint64_t file_size) override {
    if (type == kEntryDelete) {
      num_deletes_++;
    }
    return Status::OK();
  }

  bool NeedCompact() const override { return num_deletes_ > 10; }

  UserCollectedProperties GetReadableProperties() const override {
    return UserCollectedProperties{};
  }

  Status Finish(UserCollectedProperties* properties) override {
    *properties =
        UserCollectedProperties{{"num_delete", ToString(num_deletes_)}};
    return Status::OK();
  }

 private:
  uint32_t num_deletes_ = 0;
};

class CountingDeleteTabPropCollectorFactory
    : public TablePropertiesCollectorFactory {
 public:
  virtual TablePropertiesCollector* CreateTablePropertiesCollector(
      TablePropertiesCollectorFactory::Context context) override {
    return new CountingDeleteTabPropCollector();
  }
  const char* Name() const override {
    return "CountingDeleteTabPropCollectorFactory";
  }
};

TEST_F(DBPropertiesTest, GetUserDefinedTableProperties) {
  Options options = CurrentOptions();
  options.level0_file_num_compaction_trigger = (1 << 30);
  options.max_background_flushes = 0;
  options.table_properties_collector_factories.resize(1);
  std::shared_ptr<CountingUserTblPropCollectorFactory> collector_factory =
      std::make_shared<CountingUserTblPropCollectorFactory>(0);
  options.table_properties_collector_factories[0] = collector_factory;
  Reopen(options);
  // Create 4 tables
  for (int table = 0; table < 4; ++table) {
    for (int i = 0; i < 10 + table; ++i) {
      ASSERT_OK(db_->Put(WriteOptions(), ToString(table * 100 + i), "val"));
    }
    ASSERT_OK(db_->Flush(FlushOptions()));
  }

  TablePropertiesCollection props;
  ASSERT_OK(db_->GetPropertiesOfAllTables(&props));
  ASSERT_EQ(4U, props.size());
  uint32_t sum = 0;
  for (const auto& item : props) {
    auto& user_collected = item.second->user_collected_properties;
    ASSERT_TRUE(user_collected.find("CountingUserTblPropCollector") !=
                user_collected.end());
    ASSERT_EQ(user_collected.at("CountingUserTblPropCollector"), "Rocksdb");
    ASSERT_TRUE(user_collected.find("Count") != user_collected.end());
    Slice key(user_collected.at("Count"));
    uint32_t count;
    ASSERT_TRUE(GetVarint32(&key, &count));
    sum += count;
  }
  ASSERT_EQ(10u + 11u + 12u + 13u, sum);

  ASSERT_GT(collector_factory->num_created_, 0U);
  collector_factory->num_created_ = 0;
  ASSERT_OK(dbfull()->TEST_CompactRange(0, nullptr, nullptr));
  ASSERT_GT(collector_factory->num_created_, 0U);
}

TEST_F(DBPropertiesTest, UserDefinedTablePropertiesContext) {
  Options options = CurrentOptions();
  options.level0_file_num_compaction_trigger = 3;
  options.max_background_flushes = 0;
  options.table_properties_collector_factories.resize(1);
  std::shared_ptr<CountingUserTblPropCollectorFactory> collector_factory =
      std::make_shared<CountingUserTblPropCollectorFactory>(1);
  options.table_properties_collector_factories[0] = collector_factory,
  CreateAndReopenWithCF({"pikachu"}, options);
  // Create 2 files
  for (int table = 0; table < 2; ++table) {
    for (int i = 0; i < 10 + table; ++i) {
      ASSERT_OK(Put(1, ToString(table * 100 + i), "val"));
    }
    ASSERT_OK(Flush(1));
  }
  ASSERT_GT(collector_factory->num_created_, 0U);

  collector_factory->num_created_ = 0;
  // Trigger automatic compactions.
  for (int table = 0; table < 3; ++table) {
    for (int i = 0; i < 10 + table; ++i) {
      ASSERT_OK(Put(1, ToString(table * 100 + i), "val"));
    }
    ASSERT_OK(Flush(1));
    ASSERT_OK(dbfull()->TEST_WaitForCompact());
  }
  ASSERT_GT(collector_factory->num_created_, 0U);

  collector_factory->num_created_ = 0;
  ASSERT_OK(dbfull()->TEST_CompactRange(0, nullptr, nullptr, handles_[1]));
  ASSERT_GT(collector_factory->num_created_, 0U);

  // Come back to write to default column family
  collector_factory->num_created_ = 0;
  collector_factory->set_expected_column_family_id(0);  // default CF
  // Create 4 tables in default column family
  for (int table = 0; table < 2; ++table) {
    for (int i = 0; i < 10 + table; ++i) {
      ASSERT_OK(Put(ToString(table * 100 + i), "val"));
    }
    ASSERT_OK(Flush());;
  }
  ASSERT_GT(collector_factory->num_created_, 0U);

  collector_factory->num_created_ = 0;
  // Trigger automatic compactions.
  for (int table = 0; table < 3; ++table) {
    for (int i = 0; i < 10 + table; ++i) {
      ASSERT_OK(Put(ToString(table * 100 + i), "val"));
    }
    ASSERT_OK(Flush());;
    ASSERT_OK(dbfull()->TEST_WaitForCompact());
  }
  ASSERT_GT(collector_factory->num_created_, 0U);

  collector_factory->num_created_ = 0;
  ASSERT_OK(dbfull()->TEST_CompactRange(0, nullptr, nullptr));
  ASSERT_GT(collector_factory->num_created_, 0U);
}

TEST_F(DBPropertiesTest, TablePropertiesNeedCompactTest) {
  Random rnd(301);

  Options options;
  options.create_if_missing = true;
  options.write_buffer_size = 4096;
  options.max_write_buffer_number = 8;
  options.level0_file_num_compaction_trigger = 2;
  options.level0_slowdown_writes_trigger = 2;
  options.level0_stop_writes_trigger = 4;
  options.target_file_size_base = 2048;
  options.max_bytes_for_level_base = 10240;
  options.max_bytes_for_level_multiplier = 4;
  options.soft_pending_compaction_bytes_limit = 1024 * 1024;
  options.num_levels = 8;

  std::shared_ptr<TablePropertiesCollectorFactory> collector_factory =
      std::make_shared<CountingDeleteTabPropCollectorFactory>();
  options.table_properties_collector_factories.resize(1);
  options.table_properties_collector_factories[0] = collector_factory;

  DestroyAndReopen(options);

  const int kMaxKey = 1000;
  for (int i = 0; i < kMaxKey; i++) {
    ASSERT_OK(Put(Key(i), RandomString(&rnd, 102)));
    ASSERT_OK(Put(Key(kMaxKey + i), RandomString(&rnd, 102)));
  }
  ASSERT_OK(Flush());;
  ASSERT_OK(dbfull()->TEST_WaitForCompact());
  if (NumTableFilesAtLevel(0) == 1) {
    // Clear Level 0 so that when later flush a file with deletions,
    // we don't trigger an organic compaction.
    ASSERT_OK(Put(Key(0), ""));
    ASSERT_OK(Put(Key(kMaxKey * 2), ""));
    ASSERT_OK(Flush());;
    ASSERT_OK(dbfull()->TEST_WaitForCompact());
  }
  ASSERT_EQ(NumTableFilesAtLevel(0), 0);

  {
    int c = 0;
    std::unique_ptr<Iterator> iter(db_->NewIterator(ReadOptions()));
    iter->Seek(Key(kMaxKey - 100));
    while (ASSERT_RESULT(iter->CheckedValid()) && iter->key().compare(Key(kMaxKey + 100)) < 0) {
      iter->Next();
      ++c;
    }
    ASSERT_EQ(c, 200);
  }

  ASSERT_OK(Delete(Key(0)));
  for (int i = kMaxKey - 100; i < kMaxKey + 100; i++) {
    ASSERT_OK(Delete(Key(i)));
  }
  ASSERT_OK(Delete(Key(kMaxKey * 2)));

  ASSERT_OK(Flush());;
  ASSERT_OK(dbfull()->TEST_WaitForCompact());

  {
    SetPerfLevel(PerfLevel::kEnableCount);
    perf_context.Reset();
    int c = 0;
    std::unique_ptr<Iterator> iter(db_->NewIterator(ReadOptions()));
    iter->Seek(Key(kMaxKey - 100));
    while (ASSERT_RESULT(iter->CheckedValid()) && iter->key().compare(Key(kMaxKey + 100)) < 0) {
      iter->Next();
    }
    ASSERT_EQ(c, 0);
    ASSERT_LT(perf_context.internal_delete_skipped_count, 30u);
    ASSERT_LT(perf_context.internal_key_skipped_count, 30u);
    SetPerfLevel(PerfLevel::kDisable);
  }
}

TEST_F(DBPropertiesTest, NeedCompactHintPersistentTest) {
  Random rnd(301);

  Options options;
  options.create_if_missing = true;
  options.max_write_buffer_number = 8;
  options.level0_file_num_compaction_trigger = 10;
  options.level0_slowdown_writes_trigger = 10;
  options.level0_stop_writes_trigger = 10;
  options.disable_auto_compactions = true;

  std::shared_ptr<TablePropertiesCollectorFactory> collector_factory =
      std::make_shared<CountingDeleteTabPropCollectorFactory>();
  options.table_properties_collector_factories.resize(1);
  options.table_properties_collector_factories[0] = collector_factory;

  DestroyAndReopen(options);

  const int kMaxKey = 100;
  for (int i = 0; i < kMaxKey; i++) {
    ASSERT_OK(Put(Key(i), ""));
  }
  ASSERT_OK(Flush());;
  ASSERT_OK(dbfull()->TEST_WaitForFlushMemTable());

  for (int i = 1; i < kMaxKey - 1; i++) {
    ASSERT_OK(Delete(Key(i)));
  }
  ASSERT_OK(Flush());;
  ASSERT_OK(dbfull()->TEST_WaitForFlushMemTable());
  ASSERT_EQ(NumTableFilesAtLevel(0), 2);

  // Restart the DB. Although number of files didn't reach
  // options.level0_file_num_compaction_trigger, compaction should
  // still be triggered because of the need-compaction hint.
  options.disable_auto_compactions = false;
  Reopen(options);
  ASSERT_OK(dbfull()->TEST_WaitForCompact());
  ASSERT_EQ(NumTableFilesAtLevel(0), 0);
  {
    SetPerfLevel(PerfLevel::kEnableCount);
    perf_context.Reset();
    int c = 0;
    std::unique_ptr<Iterator> iter(db_->NewIterator(ReadOptions()));
    for (iter->Seek(Key(0)); ASSERT_RESULT(iter->CheckedValid()); iter->Next()) {
      c++;
    }
    ASSERT_EQ(c, 2);
    ASSERT_EQ(perf_context.internal_delete_skipped_count, 0);
    // We iterate every key twice. Is it a bug?
    ASSERT_LE(perf_context.internal_key_skipped_count, 2);
    SetPerfLevel(PerfLevel::kDisable);
  }
}
}  // namespace rocksdb

int main(int argc, char** argv) {
  rocksdb::port::InstallStackTraceHandler();
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
