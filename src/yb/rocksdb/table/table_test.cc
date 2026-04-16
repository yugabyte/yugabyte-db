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

#include <inttypes.h>
#include <stdio.h>

#include <algorithm>
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "yb/rocksdb/db/dbformat.h"
#include "yb/rocksdb/db/memtable.h"
#include "yb/rocksdb/db/write_batch_internal.h"
#include "yb/rocksdb/db/writebuffer.h"
#include "yb/rocksdb/env.h"
#include "yb/rocksdb/filter_policy.h"
#include "yb/rocksdb/flush_block_policy.h"
#include "yb/rocksdb/iterator.h"
#include "yb/rocksdb/memtablerep.h"
#include "yb/rocksdb/options.h"
#include "yb/rocksdb/perf_context.h"
#include "yb/rocksdb/slice_transform.h"
#include "yb/rocksdb/statistics.h"
#include "yb/rocksdb/table.h"
#include "yb/rocksdb/table/block.h"
#include "yb/rocksdb/table/block_based_table_builder.h"
#include "yb/rocksdb/table/block_based_table_factory.h"
#include "yb/rocksdb/table/block_based_table_reader.h"
#include "yb/rocksdb/table/block_builder.h"
#include "yb/rocksdb/table/format.h"
#include "yb/rocksdb/table/get_context.h"
#include "yb/rocksdb/table/internal_iterator.h"
#include "yb/rocksdb/table/meta_blocks.h"
#include "yb/rocksdb/table/plain_table_factory.h"
#include "yb/rocksdb/table/scoped_arena_iterator.h"
#include "yb/rocksdb/util/coding.h"
#include "yb/rocksdb/util/compression.h"
#include "yb/rocksdb/util/file_reader_writer.h"
#include "yb/rocksdb/util/logging.h"
#include "yb/rocksdb/util/random.h"
#include "yb/rocksdb/util/statistics.h"
#include "yb/rocksdb/util/testharness.h"
#include "yb/rocksdb/util/testutil.h"
#include "yb/rocksdb/utilities/checkpoint.h"

#include "yb/util/enums.h"
#include "yb/util/string_util.h"
#include "yb/util/test_macros.h"

using std::unique_ptr;

using namespace std::literals;

DECLARE_double(cache_single_touch_ratio);

namespace rocksdb {

extern const uint64_t kLegacyBlockBasedTableMagicNumber;
extern const uint64_t kLegacyPlainTableMagicNumber;
extern const uint64_t kBlockBasedTableMagicNumber;
extern const uint64_t kPlainTableMagicNumber;

namespace {

// Return reverse of "key".
// Used to test non-lexicographic comparators.
std::string Reverse(const Slice& key) {
  auto rev = key.ToString();
  std::reverse(rev.begin(), rev.end());
  return rev;
}

class ReverseKeyComparator : public Comparator {
 public:
  const char* Name() const override {
    return "rocksdb.ReverseBytewiseComparator";
  }

  int Compare(Slice a, Slice b) const override {
    return BytewiseComparator()->Compare(Reverse(a), Reverse(b));
  }

  virtual void FindShortestSeparator(std::string* start,
                                     const Slice& limit) const override {
    std::string s = Reverse(*start);
    std::string l = Reverse(limit);
    BytewiseComparator()->FindShortestSeparator(&s, l);
    *start = Reverse(s);
  }

  void FindShortSuccessor(std::string* key) const override {
    std::string s = Reverse(*key);
    BytewiseComparator()->FindShortSuccessor(&s);
    *key = Reverse(s);
  }
};

ReverseKeyComparator reverse_key_comparator;

void Increment(const Comparator* cmp, std::string* key) {
  if (cmp == BytewiseComparator()) {
    key->push_back('\0');
  } else {
    assert(cmp == &reverse_key_comparator);
    std::string rev = Reverse(*key);
    rev.push_back('\0');
    *key = Reverse(rev);
  }
}

void SetFixedSizeFilterPolicy(BlockBasedTableOptions* table_options) {
  table_options->filter_policy.reset(NewFixedSizeFilterPolicy(
      rocksdb::FilterPolicy::kDefaultFixedSizeFilterBits,
      rocksdb::FilterPolicy::kDefaultFixedSizeFilterErrorRate, nullptr));
}

}  // namespace

// Helper class for tests to unify the interface between
// BlockBuilder/TableBuilder and Block/Table.
class Constructor {
 public:
  explicit Constructor(const Comparator* cmp)
      : data_(stl_wrappers::LessOfComparator(cmp)) {}
  virtual ~Constructor() { }

  void Add(const std::string& key, const Slice& value) {
    data_[key] = value.ToString();
  }

  // Finish constructing the data structure with all the keys that have
  // been added so far.  Returns the keys in sorted order in "*keys"
  // and stores the key/value pairs in "*kvmap"
  void Finish(const Options& options, const ImmutableCFOptions& ioptions,
              const BlockBasedTableOptions& table_options,
              const InternalKeyComparatorPtr& internal_comparator,
              std::vector<std::string>* keys, stl_wrappers::KVMap* kvmap) {
    last_internal_key_ = internal_comparator;
    *kvmap = data_;
    keys->clear();
    for (const auto& kv : data_) {
      keys->push_back(kv.first);
    }
    data_.clear();
    Status s = FinishImpl(options, ioptions, table_options,
                          internal_comparator, *kvmap);
    ASSERT_TRUE(s.ok()) << s.ToString();
  }

  // Construct the data structure from the data in "data"
  virtual Status FinishImpl(const Options& options,
                            const ImmutableCFOptions& ioptions,
                            const BlockBasedTableOptions& table_options,
                            const InternalKeyComparatorPtr& internal_comparator,
                            const stl_wrappers::KVMap& data) = 0;

  virtual InternalIterator* NewIterator(const ReadOptions& ro = {}) const = 0;

  virtual const stl_wrappers::KVMap& data() { return data_; }

  virtual bool IsArenaMode() const { return false; }

  virtual DB* db() const { return nullptr; }  // Overridden in DBConstructor

  virtual bool AnywayDeleteIterator() const { return false; }

 protected:
  InternalKeyComparatorPtr last_internal_key_;

 private:
  stl_wrappers::KVMap data_;
};

class BlockConstructor: public Constructor {
 public:
  explicit BlockConstructor(
      const Comparator* cmp)
      : Constructor(cmp),
        comparator_(cmp),
        block_(nullptr) { }
  ~BlockConstructor() {
    delete block_;
  }
  virtual Status FinishImpl(const Options& options,
                            const ImmutableCFOptions& ioptions,
                            const BlockBasedTableOptions& table_options,
                            const InternalKeyComparatorPtr& internal_comparator,
                            const stl_wrappers::KVMap& kv_map) override {
    delete block_;
    block_ = nullptr;

    block_restart_interval_ = table_options.block_restart_interval;
    key_value_encoding_format_ = table_options.data_block_key_value_encoding_format;

    BlockBuilder builder(block_restart_interval_, key_value_encoding_format_);

    for (const auto& kv : kv_map) {
      builder.Add(kv.first, kv.second);
    }
    // Open the block
    data_ = builder.Finish().ToString();
    BlockContents contents;
    contents.data = data_;
    contents.cachable = false;
    block_ = new Block(std::move(contents));

    return Status::OK();
  }
  InternalIterator* NewIterator(const ReadOptions& ro = {}) const override {
    size_t restart_block_cache_capacity = ro.cache_restart_block_keys ? block_restart_interval_ : 0;
    return block_->NewIterator(
        comparator_, key_value_encoding_format_, /* iter = */ nullptr,
        /* total_order_seek = */ true, restart_block_cache_capacity);
  }

 private:
  const Comparator* comparator_;
  KeyValueEncodingFormat key_value_encoding_format_;
  int block_restart_interval_;
  std::string data_;
  Block* block_;

  BlockConstructor();
};

// A helper class that converts internal format keys into user keys
class KeyConvertingIterator final : public InternalIterator {
 public:
  explicit KeyConvertingIterator(InternalIterator* iter,
                                 bool arena_mode = false)
      : iter_(iter), arena_mode_(arena_mode) {}

  virtual ~KeyConvertingIterator() {
    if (arena_mode_) {
      iter_->~InternalIterator();
    } else {
      delete iter_;
    }
  }

  const KeyValueEntry& Seek(Slice target) override {
    ParsedInternalKey ikey(target, kMaxSequenceNumber, kTypeValue);
    std::string encoded;
    AppendInternalKey(&encoded, ikey);
    iter_->Seek(encoded);
    return Entry();
  }

  const KeyValueEntry& SeekToFirst() override {
    iter_->SeekToFirst();
    return Entry();
  }

  const KeyValueEntry& SeekToLast() override {
    iter_->SeekToLast();
    return Entry();
  }

  const KeyValueEntry& Next() override {
    iter_->Next();
    return Entry();
  }

  const KeyValueEntry& Prev() override {
    iter_->Prev();
    return Entry();
  }

  const KeyValueEntry& Entry() const override {
    const auto& res = iter_->Entry();
    if (!res) {
      return KeyValueEntry::Invalid();
    }
    ParsedInternalKey parsed_key;
    if (!ParseInternalKey(iter_->key(), &parsed_key)) {
      status_ = STATUS(Corruption, "malformed internal key");
      entry_.key = Slice("corrupted key");
    } else {
      entry_.key = parsed_key.user_key;
    }
    entry_.value = res.value;
    return entry_;
  }

  Status status() const override {
    return status_.ok() ? iter_->status() : status_;
  }

 private:
  mutable Status status_;
  InternalIterator* iter_;
  bool arena_mode_;
  mutable KeyValueEntry entry_;

  // No copying allowed
  KeyConvertingIterator(const KeyConvertingIterator&);
  void operator=(const KeyConvertingIterator&);
};

class TableConstructor: public Constructor {
 public:
  explicit TableConstructor(const Comparator* cmp,
                            bool convert_to_internal_key = false)
      : Constructor(cmp),
        convert_to_internal_key_(convert_to_internal_key) {}
  ~TableConstructor() { Reset(); }

  virtual Status FinishImpl(const Options& options,
                            const ImmutableCFOptions& ioptions,
                            const BlockBasedTableOptions& table_options,
                            const InternalKeyComparatorPtr& internal_comparator,
                            const stl_wrappers::KVMap& kv_map) override {
    Reset();
    soptions.use_mmap_reads = ioptions.allow_mmap_reads;
    file_writer_.reset(test::GetWritableFileWriter(new test::StringSink()));
    IntTblPropCollectorFactories int_tbl_prop_collector_factories;
    auto builder = ioptions.table_factory->NewTableBuilder(
        TableBuilderOptions(ioptions,
                            internal_comparator,
                            int_tbl_prop_collector_factories,
                            options.compression,
                            CompressionOptions(),
                            /* skip_filters */ false),
        TablePropertiesCollectorFactory::Context::kUnknownColumnFamily,
        file_writer_.get());
    if (TEST_skip_writing_key_value_encoding_format) {
      dynamic_cast<BlockBasedTableBuilder*>(builder.get())
          ->TEST_skip_writing_key_value_encoding_format();
    }

    for (const auto& kv : kv_map) {
      if (convert_to_internal_key_) {
        ParsedInternalKey ikey(kv.first, kMaxSequenceNumber, kTypeValue);
        std::string encoded;
        AppendInternalKey(&encoded, ikey);
        builder->Add(encoded, kv.second);
      } else {
        builder->Add(kv.first, kv.second);
      }
      EXPECT_TRUE(builder->status().ok());
    }
    Status s = builder->Finish();
    RETURN_NOT_OK(file_writer_->Flush());
    EXPECT_TRUE(s.ok()) << s.ToString();

    EXPECT_EQ(GetSink()->contents().size(), builder->TotalFileSize());
    table_properties_ = builder->GetTableProperties();

    // Open the table
    uniq_id_ = cur_uniq_id_++;
    file_reader_.reset(test::GetRandomAccessFileReader(new test::StringSource(
        GetSink()->contents(), uniq_id_, ioptions.allow_mmap_reads)));
    return ioptions.table_factory->NewTableReader(
        TableReaderOptions(ioptions, soptions, internal_comparator),
        std::move(file_reader_), GetSink()->contents().size(), &table_reader_);
  }

  InternalIterator* NewIterator(const ReadOptions& ro = {}) const override {
    InternalIterator* iter = table_reader_->NewIterator(ro);
    if (convert_to_internal_key_) {
      return new KeyConvertingIterator(iter);
    } else {
      return iter;
    }
  }

  uint64_t ApproximateOffsetOf(const Slice& key) const {
    return table_reader_->ApproximateOffsetOf(key);
  }

  virtual Status Reopen(const ImmutableCFOptions& ioptions) {
    file_reader_.reset(test::GetRandomAccessFileReader(new test::StringSource(
        GetSink()->contents(), uniq_id_, ioptions.allow_mmap_reads)));
    return ioptions.table_factory->NewTableReader(
        TableReaderOptions(ioptions, soptions, last_internal_key_),
        std::move(file_reader_), GetSink()->contents().size(), &table_reader_);
  }

  virtual TableReader* GetTableReader() {
    return table_reader_.get();
  }

  bool AnywayDeleteIterator() const override {
    return convert_to_internal_key_;
  }

  TableProperties GetTableProperties() const {
    return table_properties_;
  }

  bool TEST_skip_writing_key_value_encoding_format = false;

 private:
  void Reset() {
    uniq_id_ = 0;
    table_reader_.reset();
    file_writer_.reset();
    file_reader_.reset();
  }

  test::StringSink* GetSink() {
    return static_cast<test::StringSink*>(file_writer_->writable_file());
  }

  uint64_t uniq_id_;
  unique_ptr<WritableFileWriter> file_writer_;
  unique_ptr<RandomAccessFileReader> file_reader_;
  unique_ptr<TableReader> table_reader_;
  bool convert_to_internal_key_;

  TableConstructor();

  static uint64_t cur_uniq_id_;
  EnvOptions soptions;
  TableProperties table_properties_;
};
uint64_t TableConstructor::cur_uniq_id_ = 1;

class MemTableConstructor: public Constructor {
 public:
  explicit MemTableConstructor(const Comparator* cmp, WriteBuffer* wb)
      : Constructor(cmp),
        internal_comparator_(cmp),
        write_buffer_(wb),
        table_factory_(new SkipListFactory) {
    options_.memtable_factory = table_factory_;
    ImmutableCFOptions ioptions(options_);
    memtable_ = new MemTable(internal_comparator_, ioptions,
                             MutableCFOptions(options_, ioptions), wb,
                             kMaxSequenceNumber);
    memtable_->Ref();
  }
  ~MemTableConstructor() {
    delete memtable_->Unref();
  }
  virtual Status FinishImpl(const Options&, const ImmutableCFOptions& ioptions,
                            const BlockBasedTableOptions& table_options,
                            const InternalKeyComparatorPtr& internal_comparator,
                            const stl_wrappers::KVMap& kv_map) override {
    delete memtable_->Unref();
    ImmutableCFOptions mem_ioptions(ioptions);
    memtable_ = new MemTable(internal_comparator_, mem_ioptions,
                             MutableCFOptions(options_, mem_ioptions),
                             write_buffer_, kMaxSequenceNumber);
    memtable_->Ref();
    int seq = 1;
    for (const auto& kv : kv_map) {
      Slice key(kv.first);
      Slice value(kv.second);
      memtable_->Add(seq, kTypeValue, SliceParts(&key, 1), SliceParts(&value, 1));
      seq++;
    }
    return Status::OK();
  }
  InternalIterator* NewIterator(const ReadOptions& ro = {}) const override {
    return new KeyConvertingIterator(
        memtable_->NewIterator(ro, &arena_), true);
  }

  bool AnywayDeleteIterator() const override { return true; }

  bool IsArenaMode() const override { return true; }

 private:
  mutable Arena arena_;
  InternalKeyComparator internal_comparator_;
  Options options_;
  WriteBuffer* write_buffer_;
  MemTable* memtable_;
  std::shared_ptr<SkipListFactory> table_factory_;
};

class InternalIteratorFromIterator : public InternalIterator {
 public:
  explicit InternalIteratorFromIterator(Iterator* it) : it_(it) {}

  const KeyValueEntry& Seek(Slice target) override {
    it_->Seek(target);
    return Entry();
  }
  const KeyValueEntry& SeekToFirst() override {
    it_->SeekToFirst();
    return Entry();
  }
  const KeyValueEntry& SeekToLast() override {
    it_->SeekToLast();
    return Entry();
  }

  const KeyValueEntry& Next() override {
    it_->Next();
    return Entry();
  }

  const KeyValueEntry& Prev() override {
    it_->Prev();
    return Entry();
  }

  const KeyValueEntry& Entry() const override {
    const auto& entry = it_->Entry();
    if (!entry) {
      return KeyValueEntry::Invalid();
    }
    return entry_ = entry;
  }

  Status status() const override { return it_->status(); }

 private:
  unique_ptr<Iterator> it_;
  mutable KeyValueEntry entry_;
};

class DBConstructor: public Constructor {
 public:
  explicit DBConstructor(const Comparator* cmp)
      : Constructor(cmp),
        comparator_(cmp) {
    db_ = nullptr;
    NewDB();
  }
  ~DBConstructor() {
    delete db_;
  }
  virtual Status FinishImpl(const Options& options,
                            const ImmutableCFOptions& ioptions,
                            const BlockBasedTableOptions& table_options,
                            const InternalKeyComparatorPtr& internal_comparator,
                            const stl_wrappers::KVMap& kv_map) override {
    delete db_;
    db_ = nullptr;
    NewDB();
    for (const auto& kv : kv_map) {
      WriteBatch batch;
      batch.Put(kv.first, kv.second);
      EXPECT_TRUE(db_->Write(WriteOptions(), &batch).ok());
    }
    return Status::OK();
  }

  InternalIterator* NewIterator(const ReadOptions& ro = {}) const override {
    return new InternalIteratorFromIterator(db_->NewIterator(ro));
  }

  DB* db() const override { return db_; }

 private:
  void NewDB() {
    std::string name = test::TmpDir() + "/table_testdb";

    Options options;
    options.comparator = comparator_;
    Status status = DestroyDB(name, options);
    ASSERT_TRUE(status.ok()) << status.ToString();

    options.create_if_missing = true;
    options.error_if_exists = true;
    options.write_buffer_size = 10000;  // Something small to force merging
    status = DB::Open(options, name, &db_);
    ASSERT_TRUE(status.ok()) << status.ToString();
  }

  const Comparator* comparator_;
  DB* db_;
};

enum TestType {
  BLOCK_BASED_TABLE_TEST,
  PLAIN_TABLE_SEMI_FIXED_PREFIX,
  PLAIN_TABLE_FULL_STR_PREFIX,
  PLAIN_TABLE_TOTAL_ORDER,
  BLOCK_TEST,
  MEMTABLE_TEST,
  DB_TEST
};

struct TestArgs {
  TestType type;
  bool reverse_compare;
  int restart_interval;
  CompressionType compression;
  uint32_t format_version;
  bool use_mmap;
};

static std::vector<TestArgs> GenerateArgList() {
  std::vector<TestArgs> test_args;
  std::vector<TestType> test_types = {
      BLOCK_BASED_TABLE_TEST,
      PLAIN_TABLE_SEMI_FIXED_PREFIX,
      PLAIN_TABLE_FULL_STR_PREFIX,
      PLAIN_TABLE_TOTAL_ORDER,
      BLOCK_TEST,
      MEMTABLE_TEST, DB_TEST};
  std::vector<bool> reverse_compare_types = {false, true};
  std::vector<int> restart_intervals = {16, 1, 1024};

  // Only add compression if it is supported
  std::vector<std::pair<CompressionType, bool>> compression_types;
  compression_types.emplace_back(kNoCompression, false);
  if (Snappy_Supported()) {
    compression_types.emplace_back(kSnappyCompression, false);
  }
  if (Zlib_Supported()) {
    compression_types.emplace_back(kZlibCompression, false);
    compression_types.emplace_back(kZlibCompression, true);
  }
  if (BZip2_Supported()) {
    compression_types.emplace_back(kBZip2Compression, false);
    compression_types.emplace_back(kBZip2Compression, true);
  }
  if (LZ4_Supported()) {
    compression_types.emplace_back(kLZ4Compression, false);
    compression_types.emplace_back(kLZ4Compression, true);
    compression_types.emplace_back(kLZ4HCCompression, false);
    compression_types.emplace_back(kLZ4HCCompression, true);
  }
  if (ZSTD_Supported()) {
    compression_types.emplace_back(kZSTDNotFinalCompression, false);
    compression_types.emplace_back(kZSTDNotFinalCompression, true);
  }

  for (auto test_type : test_types) {
    for (auto reverse_compare : reverse_compare_types) {
      if (test_type == PLAIN_TABLE_SEMI_FIXED_PREFIX ||
          test_type == PLAIN_TABLE_FULL_STR_PREFIX ||
          test_type == PLAIN_TABLE_TOTAL_ORDER) {
        // Plain table doesn't use restart index or compression.
        TestArgs one_arg;
        one_arg.type = test_type;
        one_arg.reverse_compare = reverse_compare;
        one_arg.restart_interval = restart_intervals[0];
        one_arg.compression = compression_types[0].first;
        one_arg.use_mmap = true;
        test_args.push_back(one_arg);
        one_arg.use_mmap = false;
        test_args.push_back(one_arg);
        continue;
      }

      for (auto restart_interval : restart_intervals) {
        for (auto compression_type : compression_types) {
          TestArgs one_arg;
          one_arg.type = test_type;
          one_arg.reverse_compare = reverse_compare;
          one_arg.restart_interval = restart_interval;
          one_arg.compression = compression_type.first;
          one_arg.format_version = compression_type.second ? 2 : 1;
          one_arg.use_mmap = false;
          test_args.push_back(one_arg);
        }
      }
    }
  }
  return test_args;
}

// In order to make all tests run for plain table format, including
// those operating on empty keys, create a new prefix transformer which
// return fixed prefix if the slice is not shorter than the prefix length,
// and the full slice if it is shorter.
class FixedOrLessPrefixTransform : public SliceTransform {
 private:
  const size_t prefix_len_;

 public:
  explicit FixedOrLessPrefixTransform(size_t prefix_len) :
      prefix_len_(prefix_len) {
  }

  const char* Name() const override { return "rocksdb.FixedPrefix"; }

  Slice Transform(const Slice& src) const override {
    assert(InDomain(src));
    if (src.size() < prefix_len_) {
      return src;
    }
    return Slice(src.data(), prefix_len_);
  }

  bool InDomain(const Slice& src) const override { return true; }

  bool InRange(const Slice& dst) const override {
    return (dst.size() <= prefix_len_);
  }
};

class HarnessTest : public RocksDBTest {
 public:
  HarnessTest()
      : ioptions_(options_),
        constructor_(nullptr),
        write_buffer_(options_.db_write_buffer_size) {}

  void Init(const TestArgs& args) {
    delete constructor_;
    constructor_ = nullptr;
    options_ = Options();
    options_.compression = args.compression;
    // Use shorter block size for tests to exercise block boundary
    // conditions more.
    if (args.reverse_compare) {
      options_.comparator = &reverse_key_comparator;
    }

    internal_comparator_.reset(
        new test::PlainInternalKeyComparator(options_.comparator));

    support_prev_ = true;
    only_support_prefix_seek_ = false;
    options_.allow_mmap_reads = args.use_mmap;
    switch (args.type) {
      case BLOCK_BASED_TABLE_TEST:
        table_options_.flush_block_policy_factory.reset(
            new FlushBlockBySizePolicyFactory());
        table_options_.block_size = 256;
        table_options_.block_restart_interval = args.restart_interval;
        table_options_.index_block_restart_interval = args.restart_interval;
        table_options_.format_version = args.format_version;
        options_.table_factory.reset(
            new BlockBasedTableFactory(table_options_));
        constructor_ = new TableConstructor(options_.comparator);
        break;
      case PLAIN_TABLE_SEMI_FIXED_PREFIX:
        support_prev_ = false;
        only_support_prefix_seek_ = true;
        options_.prefix_extractor.reset(new FixedOrLessPrefixTransform(2));
        options_.table_factory.reset(NewPlainTableFactory());
        constructor_ = new TableConstructor(options_.comparator, true);
        internal_comparator_.reset(
            new InternalKeyComparator(options_.comparator));
        break;
      case PLAIN_TABLE_FULL_STR_PREFIX:
        support_prev_ = false;
        only_support_prefix_seek_ = true;
        options_.prefix_extractor.reset(NewNoopTransform());
        options_.table_factory.reset(NewPlainTableFactory());
        constructor_ = new TableConstructor(options_.comparator, true);
        internal_comparator_.reset(
            new InternalKeyComparator(options_.comparator));
        break;
      case PLAIN_TABLE_TOTAL_ORDER:
        support_prev_ = false;
        only_support_prefix_seek_ = false;
        options_.prefix_extractor = nullptr;

        {
          PlainTableOptions plain_table_options;
          plain_table_options.user_key_len = kPlainTableVariableLength;
          plain_table_options.bloom_bits_per_key = 0;
          plain_table_options.hash_table_ratio = 0;

          options_.table_factory.reset(
              NewPlainTableFactory(plain_table_options));
        }
        constructor_ = new TableConstructor(options_.comparator, true);
        internal_comparator_.reset(
            new InternalKeyComparator(options_.comparator));
        break;
      case BLOCK_TEST:
        table_options_.block_size = 256;
        options_.table_factory.reset(
            new BlockBasedTableFactory(table_options_));
        constructor_ = new BlockConstructor(options_.comparator);
        break;
      case MEMTABLE_TEST:
        table_options_.block_size = 256;
        options_.table_factory.reset(
            new BlockBasedTableFactory(table_options_));
        constructor_ = new MemTableConstructor(options_.comparator,
                                               &write_buffer_);
        break;
      case DB_TEST:
        table_options_.block_size = 256;
        options_.table_factory.reset(
            new BlockBasedTableFactory(table_options_));
        constructor_ = new DBConstructor(options_.comparator);
        break;
    }
    ioptions_ = ImmutableCFOptions(options_);
  }

  ~HarnessTest() { delete constructor_; }

  void Add(const std::string& key, const std::string& value) {
    constructor_->Add(key, value);
  }

  void Test(Random* rnd) {
    std::vector<std::string> keys;
    stl_wrappers::KVMap data;
    constructor_->Finish(options_, ioptions_, table_options_,
                         internal_comparator_, &keys, &data);

    TestForwardScan(keys, data);
    if (support_prev_) {
      TestBackwardScan(keys, data);
    }
    TestRandomAccess(rnd, keys, data);
  }

  void TestForwardScan(const std::vector<std::string>& keys,
                       const stl_wrappers::KVMap& data) {
    InternalIterator* iter = constructor_->NewIterator();
    ASSERT_TRUE(!iter->Valid());
    iter->SeekToFirst();
    for (stl_wrappers::KVMap::const_iterator model_iter = data.begin();
         model_iter != data.end(); ++model_iter) {
      ASSERT_EQ(ToString(data, model_iter), ToString(iter));
      iter->Next();
    }
    ASSERT_TRUE(!iter->Valid());
    ASSERT_OK(iter->status());
    if (constructor_->IsArenaMode() && !constructor_->AnywayDeleteIterator()) {
      iter->~InternalIterator();
    } else {
      delete iter;
    }
  }

  void TestBackwardScan(const std::vector<std::string>& keys,
                        const stl_wrappers::KVMap& data) {
    ReadOptions ro;
    ro.cache_restart_block_keys = CacheRestartBlockKeys::kTrue;
    InternalIterator* iter = constructor_->NewIterator(ro);
    ASSERT_TRUE(!iter->Valid());
    iter->SeekToLast();
    for (stl_wrappers::KVMap::const_reverse_iterator model_iter = data.rbegin();
         model_iter != data.rend(); ++model_iter) {
      ASSERT_EQ(ToString(data, model_iter), ToString(iter));
      iter->Prev();
    }
    ASSERT_TRUE(!iter->Valid());
    ASSERT_OK(iter->status());
    if (constructor_->IsArenaMode() && !constructor_->AnywayDeleteIterator()) {
      iter->~InternalIterator();
    } else {
      delete iter;
    }
  }

  void TestRandomAccess(Random* rnd, const std::vector<std::string>& keys,
                        const stl_wrappers::KVMap& data) {
    static const bool kVerbose = false;
    InternalIterator* iter = constructor_->NewIterator();
    ASSERT_TRUE(!iter->Valid());
    stl_wrappers::KVMap::const_iterator model_iter = data.begin();
    if (kVerbose) fprintf(stderr, "---\n");
    for (int i = 0; i < 200; i++) {
      const int toss = rnd->Uniform(support_prev_ ? 5 : 3);
      switch (toss) {
        case 0: {
          if (iter->Valid()) {
            if (kVerbose) fprintf(stderr, "Next\n");
            iter->Next();
            ++model_iter;
            ASSERT_EQ(ToString(data, model_iter), ToString(iter));
          }
          break;
        }

        case 1: {
          if (kVerbose) fprintf(stderr, "SeekToFirst\n");
          iter->SeekToFirst();
          model_iter = data.begin();
          ASSERT_EQ(ToString(data, model_iter), ToString(iter));
          break;
        }

        case 2: {
          std::string key = PickRandomKey(rnd, keys);
          model_iter = data.lower_bound(key);
          if (kVerbose) fprintf(stderr, "Seek '%s'\n",
                                EscapeString(key).c_str());
          iter->Seek(Slice(key));
          ASSERT_EQ(ToString(data, model_iter), ToString(iter));
          break;
        }

        case 3: {
          if (iter->Valid()) {
            if (kVerbose) fprintf(stderr, "Prev\n");
            iter->Prev();
            if (model_iter == data.begin()) {
              model_iter = data.end();   // Wrap around to invalid value
            } else {
              --model_iter;
            }
            ASSERT_EQ(ToString(data, model_iter), ToString(iter));
          }
          break;
        }

        case 4: {
          if (kVerbose) fprintf(stderr, "SeekToLast\n");
          iter->SeekToLast();
          if (keys.empty()) {
            model_iter = data.end();
          } else {
            std::string last = data.rbegin()->first;
            model_iter = data.lower_bound(last);
          }
          ASSERT_EQ(ToString(data, model_iter), ToString(iter));
          break;
        }
      }
    }
    ASSERT_OK(iter->status());
    if (constructor_->IsArenaMode() && !constructor_->AnywayDeleteIterator()) {
      iter->~InternalIterator();
    } else {
      delete iter;
    }
  }

  std::string ToString(const stl_wrappers::KVMap& data,
                       const stl_wrappers::KVMap::const_iterator& it) {
    if (it == data.end()) {
      return "END";
    } else {
      return "'" + it->first + "->" + it->second + "'";
    }
  }

  std::string ToString(const stl_wrappers::KVMap& data,
                       const stl_wrappers::KVMap::const_reverse_iterator& it) {
    if (it == data.rend()) {
      return "END";
    } else {
      return "'" + it->first + "->" + it->second + "'";
    }
  }

  std::string ToString(const InternalIterator* it) {
    if (!it->Valid()) {
      return it->status().ok() ? "END" : "Error: " + it->status().ToString();
    } else {
      return "'" + it->key().ToString() + "->" + it->value().ToString() + "'";
    }
  }

  std::string PickRandomKey(Random* rnd, const std::vector<std::string>& keys) {
    if (keys.empty()) {
      return "foo";
    } else {
      const int index = rnd->Uniform(static_cast<int>(keys.size()));
      std::string result = keys[index];
      switch (rnd->Uniform(support_prev_ ? 3 : 1)) {
        case 0:
          // Return an existing key
          break;
        case 1: {
          // Attempt to return something smaller than an existing key
          if (result.size() > 0 && result[result.size() - 1] > '\0'
              && (!only_support_prefix_seek_
                  || options_.prefix_extractor->Transform(result).size()
                  < result.size())) {
            result[result.size() - 1]--;
          }
          break;
      }
        case 2: {
          // Return something larger than an existing key
          Increment(options_.comparator, &result);
          break;
        }
      }
      return result;
    }
  }

  // Returns nullptr if not running against a DB
  DB* db() const { return constructor_->db(); }

 private:
  Options options_ = Options();
  ImmutableCFOptions ioptions_;
  BlockBasedTableOptions table_options_ = BlockBasedTableOptions();
  Constructor* constructor_;
  WriteBuffer write_buffer_;
  bool support_prev_;
  bool only_support_prefix_seek_;
  InternalKeyComparatorPtr internal_comparator_;
};

static bool Between(uint64_t val, uint64_t low, uint64_t high) {
  bool result = (val >= low) && (val <= high);
  if (!result) {
    fprintf(stderr, "Value %" PRIu64 " is not in range [%" PRIu64 ", %" PRIu64 "]\n",
            val, low, high);
  }
  return result;
}

// Tests against all kinds of tables
class TableTest : public RocksDBTest {
 public:
  const InternalKeyComparatorPtr& GetPlainInternalComparator(
      const Comparator* comp) {
    if (!plain_internal_comparator) {
      plain_internal_comparator = std::make_shared<test::PlainInternalKeyComparator>(comp);
    }
    return plain_internal_comparator;
  }

  void TestIndex(BlockBasedTableOptions table_options, int expected_num_index_levels);
  void TestTotalOrderSeekOnHashIndex(
      const BlockBasedTableOptions& table_options, const Options& options);

 private:
  InternalKeyComparatorPtr plain_internal_comparator;
};

class GeneralTableTest : public TableTest {};
class BlockBasedTableTest : public TableTest {
 public:
  void TestMiddleKey(
      uint64_t num_records, uint64_t index_step, size_t data_block_size,
      int expected_num_index_levels, int expected_num_data_blocks = 0);
};
class PlainTableTest : public TableTest {};
class TablePropertyTest : public RocksDBTest {};

// This test serves as the living tutorial for the prefix scan of user collected
// properties.
TEST_F(TablePropertyTest, PrefixScanTest) {
  UserCollectedProperties props{{"num.111.1", "1"},
                                {"num.111.2", "2"},
                                {"num.111.3", "3"},
                                {"num.333.1", "1"},
                                {"num.333.2", "2"},
                                {"num.333.3", "3"},
                                {"num.555.1", "1"},
                                {"num.555.2", "2"},
                                {"num.555.3", "3"}, };

  // prefixes that exist
  for (auto prefix : {"num.111"s, "num.333"s, "num.555"s}) {
    int num = 0;
    for (auto pos = props.lower_bound(prefix);
         pos != props.end() &&
             pos->first.compare(0, prefix.size(), prefix) == 0;
         ++pos) {
      ++num;
      auto key = prefix + "." + ToString(num);
      ASSERT_EQ(key, pos->first);
      ASSERT_EQ(ToString(num), pos->second);
    }
    ASSERT_EQ(3, num);
  }

  // prefixes that don't exist
  for (auto prefix : {"num.000"s, "num.222"s, "num.444"s, "num.666"s}) {
    auto pos = props.lower_bound(prefix);
    ASSERT_TRUE(pos == props.end() ||
                pos->first.compare(0, prefix.size(), prefix) != 0);
  }
}

// This test include all the basic checks except those for index size and block
// size, which will be conducted in separated unit tests.
TEST_F(BlockBasedTableTest, BasicBlockBasedTableProperties) {
  TableConstructor c(BytewiseComparator());

  c.Add("a1", "val1");
  c.Add("b2", "val2");
  c.Add("c3", "val3");
  c.Add("d4", "val4");
  c.Add("e5", "val5");
  c.Add("f6", "val6");
  c.Add("g7", "val7");
  c.Add("h8", "val8");
  c.Add("j9", "val9");

  std::vector<std::string> keys;
  stl_wrappers::KVMap kvmap;
  Options options;
  options.compression = kNoCompression;
  BlockBasedTableOptions table_options;
  table_options.block_restart_interval = 1;
  options.table_factory.reset(NewBlockBasedTableFactory(table_options));

  const ImmutableCFOptions ioptions(options);
  c.Finish(options, ioptions, table_options,
           GetPlainInternalComparator(options.comparator), &keys, &kvmap);

  auto& props = *c.GetTableReader()->GetTableProperties();
  ASSERT_EQ(kvmap.size(), props.num_entries);

  auto raw_key_size = kvmap.size() * 2ul;
  auto raw_value_size = kvmap.size() * 4ul;

  ASSERT_EQ(raw_key_size, props.raw_key_size);
  ASSERT_EQ(raw_value_size, props.raw_value_size);
  ASSERT_EQ(1ul, props.num_data_blocks);
  ASSERT_EQ("", props.filter_policy_name);  // no filter policy is used

  // Verify data size.
  BlockBuilder block_builder(1, table_options.data_block_key_value_encoding_format);
  for (const auto& item : kvmap) {
    block_builder.Add(item.first, item.second);
  }
  Slice content = block_builder.Finish();
  ASSERT_EQ(content.size() + kBlockTrailerSize, props.data_size);
}

TEST_F(BlockBasedTableTest, FilterPolicyNameProperties) {
  TableConstructor c(BytewiseComparator(), true);
  c.Add("a1", "val1");
  std::vector<std::string> keys;
  stl_wrappers::KVMap kvmap;

  BlockBasedTableOptions table_options;
  for (int i = 0; i < 2; i++) {
    switch (i) {
      case 0:
        table_options.filter_policy.reset(NewBloomFilterPolicy(10));
        break;
      default:
        SetFixedSizeFilterPolicy(&table_options);
    }
    Options options;
    options.table_factory.reset(NewBlockBasedTableFactory(table_options));

    const ImmutableCFOptions ioptions(options);
    c.Finish(options, ioptions, table_options,
        GetPlainInternalComparator(options.comparator), &keys, &kvmap);
    auto& props = *c.GetTableReader()->GetTableProperties();
    switch (i) {
      case 0:
        ASSERT_EQ("rocksdb.BuiltinBloomFilter", props.filter_policy_name);
        break;
      default:
        ASSERT_EQ("rocksdb.FixedSizeBloomFilter", props.filter_policy_name);
        break;
    }
  }
}

//
// BlockBasedTableTest::PrefetchTest
//
void AssertKeysInCache(BlockBasedTable* table_reader,
                       const std::vector<std::string>& keys_in_cache,
                       const std::vector<std::string>& keys_not_in_cache) {
  for (auto key : keys_in_cache) {
    ASSERT_TRUE(table_reader->TEST_KeyInCache(ReadOptions(), key));
  }

  for (auto key : keys_not_in_cache) {
    ASSERT_TRUE(!table_reader->TEST_KeyInCache(ReadOptions(), key));
  }
}

void PrefetchRange(TableConstructor* c, Options* opt,
                   BlockBasedTableOptions* table_options,
                   const std::vector<std::string>& keys, const char* key_begin,
                   const char* key_end,
                   const std::vector<std::string>& keys_in_cache,
                   const std::vector<std::string>& keys_not_in_cache,
                   const Status expected_status = Status::OK()) {
  // reset the cache and reopen the table
  table_options->block_cache =
      NewLRUCache((16 * 1024 * 1024) / FLAGS_cache_single_touch_ratio);
  opt->table_factory.reset(NewBlockBasedTableFactory(*table_options));
  const ImmutableCFOptions ioptions2(*opt);
  ASSERT_OK(c->Reopen(ioptions2));

  // prefetch
  auto* table_reader = dynamic_cast<BlockBasedTable*>(c->GetTableReader());
  // empty string replacement is a trick so we don't crash the test
  Slice begin(key_begin ? key_begin : "");
  Slice end(key_end ? key_end : "");
  Status s = table_reader->Prefetch(key_begin ? &begin : nullptr,
                                    key_end ? &end : nullptr);
  ASSERT_TRUE(s.code() == expected_status.code());

  // assert our expectation in cache warmup
  AssertKeysInCache(table_reader, keys_in_cache, keys_not_in_cache);
}

TEST_F(BlockBasedTableTest, PrefetchTest) {
  // The purpose of this test is to test the prefetching operation built into
  // BlockBasedTable.
  Options opt;
  auto ikc = std::make_shared<test::PlainInternalKeyComparator>(opt.comparator);
  opt.compression = kNoCompression;
  BlockBasedTableOptions table_options;
  table_options.block_size = 1024;
  // big enough so we don't ever lose cached values.
  table_options.block_cache =
      NewLRUCache((16 * 1024 * 1024) / FLAGS_cache_single_touch_ratio);
  opt.table_factory.reset(NewBlockBasedTableFactory(table_options));

  TableConstructor c(BytewiseComparator());
  c.Add("k01", "hello");
  c.Add("k02", "hello2");
  c.Add("k03", std::string(10000, 'x'));
  c.Add("k04", std::string(200000, 'x'));
  c.Add("k05", std::string(300000, 'x'));
  c.Add("k06", "hello3");
  c.Add("k07", std::string(100000, 'x'));
  std::vector<std::string> keys;
  stl_wrappers::KVMap kvmap;
  const ImmutableCFOptions ioptions(opt);
  c.Finish(opt, ioptions, table_options, ikc, &keys, &kvmap);

  // We get the following data spread :
  //
  // Data block         Index
  // ========================
  // [ k01 k02 k03 ]    k03
  // [ k04         ]    k04
  // [ k05         ]    k05
  // [ k06 k07     ]    k07


  // Simple
  PrefetchRange(&c, &opt, &table_options, keys,
                /*key_range=*/ "k01", "k05",
                /*keys_in_cache=*/ {"k01", "k02", "k03", "k04", "k05"},
                /*keys_not_in_cache=*/ {"k06", "k07"});
  PrefetchRange(&c, &opt, &table_options, keys,
                "k01", "k01",
                {"k01", "k02", "k03"},
                {"k04", "k05", "k06", "k07"});
  // odd
  PrefetchRange(&c, &opt, &table_options, keys,
                "a", "z",
                {"k01", "k02", "k03", "k04", "k05", "k06", "k07"},
                {});
  PrefetchRange(&c, &opt, &table_options, keys,
                "k00", "k00",
                {"k01", "k02", "k03"},
                {"k04", "k05", "k06", "k07"});
  // Edge cases
  PrefetchRange(&c, &opt, &table_options, keys,
                "k00", "k06",
                {"k01", "k02", "k03", "k04", "k05", "k06", "k07"},
                {});
  PrefetchRange(&c, &opt, &table_options, keys,
                "k00", "zzz",
                {"k01", "k02", "k03", "k04", "k05", "k06", "k07"},
                {});
  // null keys
  PrefetchRange(&c, &opt, &table_options, keys,
                nullptr, nullptr,
                {"k01", "k02", "k03", "k04", "k05", "k06", "k07"},
                {});
  PrefetchRange(&c, &opt, &table_options, keys,
                "k04", nullptr,
                {"k04", "k05", "k06", "k07"},
                {"k01", "k02", "k03"});
  PrefetchRange(&c, &opt, &table_options, keys,
                nullptr, "k05",
                {"k01", "k02", "k03", "k04", "k05"},
                {"k06", "k07"});
  // invalid
  PrefetchRange(&c, &opt, &table_options, keys,
                "k06", "k00", {}, {},
                STATUS(InvalidArgument, Slice("k06 "), Slice("k07")));
}

void TableTest::TestTotalOrderSeekOnHashIndex(
    const BlockBasedTableOptions& table_options, const Options& options) {
  TableConstructor c(BytewiseComparator(), true);
  c.Add("aaaa1", std::string('a', 56));
  c.Add("bbaa1", std::string('a', 56));
  c.Add("cccc1", std::string('a', 56));
  c.Add("bbbb1", std::string('a', 56));
  c.Add("baaa1", std::string('a', 56));
  c.Add("abbb1", std::string('a', 56));
  c.Add("cccc2", std::string('a', 56));
  std::vector<std::string> keys;
  stl_wrappers::KVMap kvmap;
  const ImmutableCFOptions ioptions(options);
  c.Finish(options, ioptions, table_options,
           GetPlainInternalComparator(options.comparator), &keys, &kvmap);
  auto props = c.GetTableReader()->GetTableProperties();
  ASSERT_EQ(7u, props->num_data_blocks);
  auto* reader = c.GetTableReader();
  ReadOptions ro;
  ro.total_order_seek = true;
  std::unique_ptr<InternalIterator> iter(reader->NewIterator(ro));

  iter->Seek(InternalKey("b", 0, kTypeValue).Encode());
  ASSERT_OK(iter->status());
  ASSERT_TRUE(iter->Valid());
  ASSERT_EQ("baaa1", ExtractUserKey(iter->key()).ToString());
  iter->Next();
  ASSERT_OK(iter->status());
  ASSERT_TRUE(iter->Valid());
  ASSERT_EQ("bbaa1", ExtractUserKey(iter->key()).ToString());

  iter->Seek(InternalKey("bb", 0, kTypeValue).Encode());
  ASSERT_OK(iter->status());
  ASSERT_TRUE(iter->Valid());
  ASSERT_EQ("bbaa1", ExtractUserKey(iter->key()).ToString());
  iter->Next();
  ASSERT_OK(iter->status());
  ASSERT_TRUE(iter->Valid());
  ASSERT_EQ("bbbb1", ExtractUserKey(iter->key()).ToString());

  iter->Seek(InternalKey("bbb", 0, kTypeValue).Encode());
  ASSERT_OK(iter->status());
  ASSERT_TRUE(iter->Valid());
  ASSERT_EQ("bbbb1", ExtractUserKey(iter->key()).ToString());
  iter->Next();
  ASSERT_OK(iter->status());
  ASSERT_TRUE(iter->Valid());
  ASSERT_EQ("cccc1", ExtractUserKey(iter->key()).ToString());
}

TEST_F(BlockBasedTableTest, TotalOrderSeekOnHashIndex) {
  BlockBasedTableOptions table_options;
  // Make each key/value an individual block
  table_options.block_size = 64;

  // Binary search index
  {
    Options options;
    table_options.index_type = IndexType::kBinarySearch;
    options.table_factory.reset(new BlockBasedTableFactory(table_options));
    TestTotalOrderSeekOnHashIndex(table_options, options);
  }
  // Hash search index
  {
    Options options;
    table_options.index_type = IndexType::kHashSearch;
    options.table_factory.reset(new BlockBasedTableFactory(table_options));
    options.prefix_extractor.reset(NewFixedPrefixTransform(4));
    TestTotalOrderSeekOnHashIndex(table_options, options);
  }
  {
    // Hash search index with hash_index_allow_collision
    Options options;
    table_options.index_type = IndexType::kHashSearch;
    table_options.hash_index_allow_collision = true;
    options.table_factory.reset(new BlockBasedTableFactory(table_options));
    options.prefix_extractor.reset(NewFixedPrefixTransform(4));
    TestTotalOrderSeekOnHashIndex(table_options, options);
  }
  {
    // Binary search index with fixed size filter policy
    Options options;
    table_options.index_type = IndexType::kBinarySearch;
    SetFixedSizeFilterPolicy(&table_options);
    options.table_factory.reset(new BlockBasedTableFactory(table_options));
    TestTotalOrderSeekOnHashIndex(table_options, options);
  }
  {
    // Hash search index with filter policy
    Options options;
    table_options.index_type = IndexType::kHashSearch;
    table_options.filter_policy.reset(NewBloomFilterPolicy(10));
    options.table_factory.reset(new BlockBasedTableFactory(table_options));
    options.prefix_extractor.reset(NewFixedPrefixTransform(4));
    TestTotalOrderSeekOnHashIndex(table_options, options);
  }
  {
    // Multi level sharded index with fixed size filter policy
    Options options;
    table_options.index_type = IndexType::kMultiLevelBinarySearch;
    SetFixedSizeFilterPolicy(&table_options);
    options.table_factory.reset(new BlockBasedTableFactory(table_options));
    TestTotalOrderSeekOnHashIndex(table_options, options);
  }
}

TEST_F(BlockBasedTableTest, NoopTransformSeek) {
  BlockBasedTableOptions table_options;
  for (int it = 0; it < 2; it++) {
    switch (it) {
      case 0:
        table_options.filter_policy.reset(NewBloomFilterPolicy(10));
        break;
      default:
        SetFixedSizeFilterPolicy(&table_options);
        break;
    }

    Options options;
    options.comparator = BytewiseComparator();
    options.table_factory.reset(new BlockBasedTableFactory(table_options));
    options.prefix_extractor.reset(NewNoopTransform());

    TableConstructor c(options.comparator);
    // To tickle the PrefixMayMatch bug it is important that the
    // user-key is a single byte so that the index key exactly matches
    // the user-key.
    InternalKey key("a", 1, kTypeValue);
    c.Add(key.Encode().ToBuffer(), "b");
    std::vector<std::string> keys;
    stl_wrappers::KVMap kvmap;
    const ImmutableCFOptions ioptions(options);
    auto internal_comparator = std::make_shared<InternalKeyComparator>(options.comparator);
    c.Finish(options, ioptions, table_options, internal_comparator, &keys, &kvmap);

    auto* reader = c.GetTableReader();
    for (int i = 0; i < 2; ++i) {
      ReadOptions ro;
      ro.total_order_seek = (i == 0);
      std::unique_ptr<InternalIterator> iter(reader->NewIterator(ro));

      iter->Seek(key.Encode());
      ASSERT_OK(iter->status());
      ASSERT_TRUE(iter->Valid());
      ASSERT_EQ("a", ExtractUserKey(iter->key()).ToString());
    }
  }
}

void AddInternalKey(TableConstructor* c, const std::string& prefix,
                    int suffix_len = 800) {
  static Random rnd(1023);
  InternalKey k(prefix + RandomString(&rnd, 800), 0, kTypeValue);
  c->Add(k.Encode().ToString(), "v");
}

void TableTest::TestIndex(BlockBasedTableOptions table_options, int expected_num_index_levels) {
  TableConstructor c(BytewiseComparator());

  // keys with prefix length 3, make sure the key/value is big enough to fill
  // one block
  AddInternalKey(&c, "0015");
  AddInternalKey(&c, "0035");

  AddInternalKey(&c, "0054");
  AddInternalKey(&c, "0055");

  AddInternalKey(&c, "0056");
  AddInternalKey(&c, "0057");

  AddInternalKey(&c, "0058");
  AddInternalKey(&c, "0075");

  AddInternalKey(&c, "0076");
  AddInternalKey(&c, "0095");

  std::vector<std::string> keys;
  stl_wrappers::KVMap kvmap;
  Options options;
  options.prefix_extractor.reset(NewFixedPrefixTransform(3));
  table_options.block_size = 1700;
  table_options.block_cache = NewLRUCache(1024);
  options.table_factory.reset(NewBlockBasedTableFactory(table_options));

  auto comparator = std::make_shared<InternalKeyComparator>(BytewiseComparator());
  const ImmutableCFOptions ioptions(options);
  c.Finish(options, ioptions, table_options, comparator, &keys, &kvmap);
  {
    auto props = c.GetTableProperties().user_collected_properties;
    auto pos = props.find(BlockBasedTablePropertyNames::kNumIndexLevels);
    DCHECK(pos != props.end());
    int num_index_levels = DecodeFixed32(pos->second.c_str());
    ASSERT_EQ(expected_num_index_levels, num_index_levels);
  }
  auto reader = c.GetTableReader();

  auto props = reader->GetTableProperties();
  ASSERT_EQ(5u, props->num_data_blocks);

  std::unique_ptr<InternalIterator> iter(reader->NewIterator(ReadOptions()));

  // -- Find keys do not exist, but have common prefix.
  std::vector<std::string> prefixes = {"001", "003", "005", "007", "009"};
  std::vector<std::string> lower_bound = {keys[0], keys[1], keys[2],
                                          keys[7], keys[9], };

  // find the lower bound of the prefix
  for (size_t i = 0; i < prefixes.size(); ++i) {
    iter->Seek(InternalKey(prefixes[i], 0, kTypeValue).Encode());
    ASSERT_OK(iter->status());
    ASSERT_TRUE(iter->Valid());

    // seek the first element in the block
    ASSERT_EQ(lower_bound[i], iter->key().ToString());
    ASSERT_EQ("v", iter->value().ToString());
  }

  // find the upper bound of prefixes
  std::vector<std::string> upper_bound = {keys[1], keys[2], keys[7], keys[9], };

  // find existing keys
  for (const auto& item : kvmap) {
    auto ukey = ExtractUserKey(item.first).ToString();
    iter->Seek(ukey);

    // ASSERT_OK(regular_iter->status());
    ASSERT_OK(iter->status());

    // ASSERT_TRUE(regular_iter->Valid());
    ASSERT_TRUE(iter->Valid());

    ASSERT_EQ(item.first, iter->key().ToString());
    ASSERT_EQ(item.second, iter->value().ToString());
  }

  for (size_t i = 0; i < prefixes.size(); ++i) {
    // the key is greater than any existing keys.
    auto key = prefixes[i] + "9";
    iter->Seek(InternalKey(key, 0, kTypeValue).Encode());

    ASSERT_OK(iter->status());
    if (i == prefixes.size() - 1) {
      // last key
      ASSERT_TRUE(!iter->Valid());
      ASSERT_OK(iter->status());
    } else {
      ASSERT_TRUE(iter->Valid());
      // seek the first element in the block
      ASSERT_EQ(upper_bound[i], iter->key().ToString());
      ASSERT_EQ("v", iter->value().ToString());
    }
  }

  // find keys with prefix that don't match any of the existing prefixes.
  std::vector<std::string> non_exist_prefixes = {"002", "004", "006", "008"};
  for (const auto& prefix : non_exist_prefixes) {
    iter->Seek(InternalKey(prefix, 0, kTypeValue).Encode());
    // regular_iter->Seek(prefix);

    // Seek to non-existing prefixes should yield either invalid, or a
    // key with prefix greater than the target.
    if (iter->Valid()) {
      Slice ukey = ExtractUserKey(iter->key());
      Slice ukey_prefix = options.prefix_extractor->Transform(ukey);
      ASSERT_LT(BytewiseComparator()->Compare(prefix, ukey_prefix), 0);
    }
    ASSERT_OK(iter->status());
  }
}

TEST_F(TableTest, BinaryIndexTest) {
  BlockBasedTableOptions table_options;
  table_options.index_type = IndexType::kBinarySearch;
  TestIndex(table_options, 1);
}

TEST_F(TableTest, HashIndexTest) {
  BlockBasedTableOptions table_options;
  table_options.index_type = IndexType::kHashSearch;
  table_options.hash_index_allow_collision = true;
  TestIndex(table_options, 1);
}

TEST_F(TableTest, MultiLevelIndexTest) {
  BlockBasedTableOptions table_options;
  table_options.index_type = IndexType::kMultiLevelBinarySearch;
  constexpr int keys = 5;
  for (int entries_per_index_block = 2; entries_per_index_block < keys; ++entries_per_index_block) {
    table_options.min_keys_per_index_block = 2;
    table_options.index_block_size = entries_per_index_block * 24;
    const int expected_index_levels = static_cast<int>(
        ceil(std::log(keys) / std::log(entries_per_index_block)));
    TestIndex(table_options, expected_index_levels);
  }
}

// It's very hard to figure out the index block size of a block accurately.
// To make sure we get the index size, we just make sure as key number
// grows, the filter block size also grows.
TEST_F(BlockBasedTableTest, IndexSizeStat) {
  uint64_t last_index_size = 0;

  // we need to use random keys since the pure human readable texts
  // may be well compressed, resulting insignificant change of index
  // block size.
  Random rnd(test::RandomSeed());
  std::vector<std::string> keys;

  for (int i = 0; i < 100; ++i) {
    keys.push_back(RandomString(&rnd, 10000));
  }

  // Each time we load one more key to the table. the table index block
  // size is expected to be larger than last time's.
  for (size_t i = 1; i < keys.size(); ++i) {
    TableConstructor c(BytewiseComparator());
    for (size_t j = 0; j < i; ++j) {
      c.Add(keys[j], "val");
    }

    std::vector<std::string> ks;
    stl_wrappers::KVMap kvmap;
    Options options;
    options.compression = kNoCompression;
    BlockBasedTableOptions table_options;
    table_options.block_restart_interval = 1;
    options.table_factory.reset(NewBlockBasedTableFactory(table_options));

    const ImmutableCFOptions ioptions(options);
    c.Finish(options, ioptions, table_options,
             GetPlainInternalComparator(options.comparator), &ks, &kvmap);
    auto index_size = c.GetTableReader()->GetTableProperties()->data_index_size;
    ASSERT_GT(index_size, last_index_size);
    last_index_size = index_size;
  }
}

TEST_F(BlockBasedTableTest, NumBlockStat) {
  Random rnd(test::RandomSeed());
  TableConstructor c(BytewiseComparator());
  Options options;
  options.compression = kNoCompression;
  BlockBasedTableOptions table_options;
  table_options.block_restart_interval = 1;
  table_options.block_size = 1000;
  options.table_factory.reset(NewBlockBasedTableFactory(table_options));

  for (int i = 0; i < 10; ++i) {
    // the key/val are slightly smaller than block size, so that each block
    // holds roughly one key/value pair.
    c.Add(RandomString(&rnd, 900), "val");
  }

  std::vector<std::string> ks;
  stl_wrappers::KVMap kvmap;
  const ImmutableCFOptions ioptions(options);
  c.Finish(options, ioptions, table_options,
           GetPlainInternalComparator(options.comparator), &ks, &kvmap);
  ASSERT_EQ(kvmap.size(),
            c.GetTableReader()->GetTableProperties()->num_data_blocks);
}

// A simple tool that takes the snapshot of block cache statistics.
class BlockCachePropertiesSnapshot {
 public:
  explicit BlockCachePropertiesSnapshot(Statistics* statistics) {
    block_cache_miss = statistics->getTickerCount(BLOCK_CACHE_MISS);
    block_cache_hit = statistics->getTickerCount(BLOCK_CACHE_HIT);
    index_block_cache_miss = statistics->getTickerCount(BLOCK_CACHE_INDEX_MISS);
    index_block_cache_hit = statistics->getTickerCount(BLOCK_CACHE_INDEX_HIT);
    data_block_cache_miss = statistics->getTickerCount(BLOCK_CACHE_DATA_MISS);
    data_block_cache_hit = statistics->getTickerCount(BLOCK_CACHE_DATA_HIT);
    filter_block_cache_miss =
        statistics->getTickerCount(BLOCK_CACHE_FILTER_MISS);
    filter_block_cache_hit = statistics->getTickerCount(BLOCK_CACHE_FILTER_HIT);
    block_cache_bytes_write =
        statistics->getTickerCount(BLOCK_CACHE_BYTES_WRITE);
  }

  void AssertIndexBlockStat(int64_t expected_index_block_cache_miss,
                            int64_t expected_index_block_cache_hit) {
    ASSERT_EQ(expected_index_block_cache_miss, index_block_cache_miss);
    ASSERT_EQ(expected_index_block_cache_hit, index_block_cache_hit);
  }

  void AssertFilterBlockStat(int64_t expected_filter_block_cache_miss,
                             int64_t expected_filter_block_cache_hit) {
    ASSERT_EQ(expected_filter_block_cache_miss, filter_block_cache_miss);
    ASSERT_EQ(expected_filter_block_cache_hit, filter_block_cache_hit);
  }

  // Check if the fetched props matches the expected ones.
  // TODO(kailiu) Use this only when you disabled filter policy!
  void AssertEqual(int64_t expected_index_block_cache_miss,
                   int64_t expected_index_block_cache_hit,
                   int64_t expected_data_block_cache_miss,
                   int64_t expected_data_block_cache_hit) const {
    ASSERT_EQ(expected_index_block_cache_miss, index_block_cache_miss);
    ASSERT_EQ(expected_index_block_cache_hit, index_block_cache_hit);
    ASSERT_EQ(expected_data_block_cache_miss, data_block_cache_miss);
    ASSERT_EQ(expected_data_block_cache_hit, data_block_cache_hit);
    ASSERT_EQ(expected_index_block_cache_miss + expected_data_block_cache_miss,
              block_cache_miss);
    ASSERT_EQ(expected_index_block_cache_hit + expected_data_block_cache_hit,
              block_cache_hit);
  }

  int64_t GetCacheHit() { return block_cache_hit; }

  int64_t GetCacheBytesWrite() { return block_cache_bytes_write; }

 private:
  int64_t block_cache_miss = 0;
  int64_t block_cache_hit = 0;
  int64_t index_block_cache_miss = 0;
  int64_t index_block_cache_hit = 0;
  int64_t data_block_cache_miss = 0;
  int64_t data_block_cache_hit = 0;
  int64_t filter_block_cache_miss = 0;
  int64_t filter_block_cache_hit = 0;
  int64_t block_cache_bytes_write = 0;
};

// Make sure, by default, index/filter blocks were pre-loaded (meaning we won't
// use block cache to store them).
TEST_F(BlockBasedTableTest, BlockCacheDisabledTest) {
  Options options;
  options.create_if_missing = true;
  options.statistics = CreateDBStatisticsForTests();
  BlockBasedTableOptions table_options;
  table_options.block_cache = NewLRUCache(1024);
  table_options.filter_policy.reset(NewBloomFilterPolicy(10));
  options.table_factory.reset(new BlockBasedTableFactory(table_options));
  std::vector<std::string> keys;
  stl_wrappers::KVMap kvmap;

  TableConstructor c(BytewiseComparator(), true);
  c.Add("key", "value");
  const ImmutableCFOptions ioptions(options);
  c.Finish(options, ioptions, table_options,
           GetPlainInternalComparator(options.comparator), &keys, &kvmap);

  // preloading is enabled for filter blocks, disabled for index blocks.
  auto reader = dynamic_cast<BlockBasedTable*>(c.GetTableReader());
  ASSERT_TRUE(reader->TEST_filter_block_preloaded());
  ASSERT_FALSE(reader->TEST_index_reader_loaded());

  {
    // nothing happens in the beginning
    BlockCachePropertiesSnapshot props(options.statistics.get());
    props.AssertIndexBlockStat(0, 0);
    props.AssertFilterBlockStat(0, 0);
  }

  {
    GetContext get_context(options.comparator, nullptr, nullptr, nullptr,
                           GetContext::kNotFound, Slice(), nullptr, nullptr,
                           nullptr, nullptr);
    // a hack that just to trigger BlockBasedTable::GetFilter.
    ASSERT_OK(reader->Get(ReadOptions(), "non-exist-key", &get_context));
    BlockCachePropertiesSnapshot props(options.statistics.get());
    props.AssertIndexBlockStat(0, 0);
    props.AssertFilterBlockStat(0, 0);
  }

  // Index is loaded after first access.
  ASSERT_TRUE(reader->TEST_index_reader_loaded());
}

// Due to the difficulities of the intersaction between statistics, this test
// only tests the case when "index block is put to block cache"
TEST_F(BlockBasedTableTest, FilterBlockInBlockCache) {
  // -- Table construction
  Options options;
  options.create_if_missing = true;
  options.statistics = CreateDBStatisticsForTests();

  // Enable the cache for index/filter blocks
  BlockBasedTableOptions table_options;
  table_options.block_cache = NewLRUCache(1024 / FLAGS_cache_single_touch_ratio);
  table_options.cache_index_and_filter_blocks = true;
  options.table_factory.reset(new BlockBasedTableFactory(table_options));
  std::vector<std::string> keys;
  stl_wrappers::KVMap kvmap;

  TableConstructor c(BytewiseComparator());
  c.Add("key", "value");
  const ImmutableCFOptions ioptions(options);
  c.Finish(options, ioptions, table_options,
           GetPlainInternalComparator(options.comparator), &keys, &kvmap);
  // preloading filter/index blocks is prohibited.
  auto* reader = dynamic_cast<BlockBasedTable*>(c.GetTableReader());
  ASSERT_TRUE(!reader->TEST_filter_block_preloaded());
  ASSERT_TRUE(!reader->TEST_index_reader_loaded());

  // -- PART 1: Open with regular block cache.
  // Since block_cache is disabled, no cache activities will be involved.
  unique_ptr<InternalIterator> iter;

  int64_t last_cache_hit = 0;
  // At first, no block will be accessed.
  {
    BlockCachePropertiesSnapshot props(options.statistics.get());
    // index won't be added to block cache.
    props.AssertEqual(0,  // index block miss
                      0, 0, 0);
    ASSERT_EQ(props.GetCacheHit(), 0);
    ASSERT_EQ(props.GetCacheBytesWrite(),
              table_options.block_cache->GetUsage());
    last_cache_hit = props.GetCacheHit();
  }

  // Only index block will be accessed
  {
    iter.reset(c.NewIterator());
    BlockCachePropertiesSnapshot props(options.statistics.get());
    props.AssertEqual(1,  // index block miss
                      0, 0, 0);
    // Cache miss
    ASSERT_EQ(props.GetCacheHit(), last_cache_hit);
    ASSERT_EQ(props.GetCacheBytesWrite(),
              table_options.block_cache->GetUsage());
    last_cache_hit = props.GetCacheHit();
  }

  // Only data block will be accessed
  {
    iter->SeekToFirst();
    BlockCachePropertiesSnapshot props(options.statistics.get());
    // NOTE: to help better highlight the "detla" of each ticker, I use
    // <last_value> + <added_value> to indicate the increment of changed
    // value; other numbers remain the same.
    props.AssertEqual(1, 0, 0 + 1,  // data block miss
                      0);
    // Cache miss
    ASSERT_EQ(props.GetCacheHit(), last_cache_hit);
    ASSERT_EQ(props.GetCacheBytesWrite(),
              table_options.block_cache->GetUsage());
    last_cache_hit = props.GetCacheHit();
  }

  // Data block will be in cache
  {
    iter.reset(c.NewIterator());
    iter->SeekToFirst();
    BlockCachePropertiesSnapshot props(options.statistics.get());
    props.AssertEqual(1, 0 + 1, /* index block hit */
                      1, 0 + 1 /* data block hit */);
    // Cache hit
    ASSERT_GT(props.GetCacheHit(), last_cache_hit);
    ASSERT_EQ(props.GetCacheBytesWrite(),
              table_options.block_cache->GetUsage());
    last_cache_hit = props.GetCacheHit();
  }
  // release the iterator so that the block cache can reset correctly.
  iter.reset();

  // -- PART 2: Open with very small block cache
  // In this test, no block will ever get hit since the block cache is
  // too small to fit even one entry.
  table_options.block_cache = NewLRUCache(1);
  options.statistics = CreateDBStatisticsForTests();
  options.table_factory.reset(new BlockBasedTableFactory(table_options));
  const ImmutableCFOptions ioptions2(options);
  ASSERT_OK(c.Reopen(ioptions2));
  {
    BlockCachePropertiesSnapshot props(options.statistics.get());
    props.AssertEqual(0,  // index block miss
                      0, 0, 0);
    // Cache miss
    ASSERT_EQ(props.GetCacheHit(), 0);
  }

  {
    // Both index and data block get accessed.
    // It first cache index block then data block. But since the cache size
    // is only 1, index block will be purged after data block is inserted.
    iter.reset(c.NewIterator());
    BlockCachePropertiesSnapshot props(options.statistics.get());
    props.AssertEqual(0 + 1,  // index block miss
                      0, 0,   // data block miss
                      0);
    // Cache miss
    ASSERT_EQ(props.GetCacheHit(), 0);
  }

  {
    // SeekToFirst() accesses data block. With similar reason, we expect data
    // block's cache miss.
    iter->SeekToFirst();
    BlockCachePropertiesSnapshot props(options.statistics.get());
    props.AssertEqual(1, 0, 0 + 1,  // data block miss
                      0);
    // Cache miss
    ASSERT_EQ(props.GetCacheHit(), 0);
  }
  iter.reset();

  // -- PART 3: Open table with bloom filter enabled but not in SST file
  table_options.block_cache = NewLRUCache(4096);
  table_options.cache_index_and_filter_blocks = false;
  options.table_factory.reset(NewBlockBasedTableFactory(table_options));

  TableConstructor c3(BytewiseComparator());
  std::string user_key = "k01";
  InternalKey internal_key(user_key, 0, kTypeValue);
  const std::string encoded_key = internal_key.Encode().ToString();
  c3.Add(encoded_key, "hello");
  ImmutableCFOptions ioptions3(options);
  // Generate table without filter policy
  c3.Finish(options, ioptions3, table_options,
           GetPlainInternalComparator(options.comparator), &keys, &kvmap);
  // Open table with filter policy
  table_options.filter_policy.reset(NewBloomFilterPolicy(1));
  options.table_factory.reset(new BlockBasedTableFactory(table_options));
  options.statistics = CreateDBStatisticsForTests();
  ImmutableCFOptions ioptions4(options);
  ASSERT_OK(c3.Reopen(ioptions4));
  reader = dynamic_cast<BlockBasedTable*>(c3.GetTableReader());
  ASSERT_TRUE(!reader->TEST_filter_block_preloaded());
  std::string value;
  GetContext get_context(options.comparator, nullptr, nullptr, nullptr,
                         GetContext::kNotFound, user_key, &value, nullptr,
                         nullptr, nullptr);
  ASSERT_OK(reader->Get(ReadOptions(), encoded_key, &get_context));
  ASSERT_EQ(value, "hello");
  BlockCachePropertiesSnapshot props(options.statistics.get());
  props.AssertFilterBlockStat(0, 0);
}

void ValidateBlockSizeDeviation(int value, int expected) {
  BlockBasedTableOptions table_options;
  table_options.block_size_deviation = value;
  BlockBasedTableFactory* factory = new BlockBasedTableFactory(table_options);

  const BlockBasedTableOptions* normalized_table_options =
      (const BlockBasedTableOptions*)factory->GetOptions();
  ASSERT_EQ(normalized_table_options->block_size_deviation, expected);

  delete factory;
}

void ValidateBlockRestartInterval(int value, int expected) {
  BlockBasedTableOptions table_options;
  table_options.block_restart_interval = value;
  BlockBasedTableFactory* factory = new BlockBasedTableFactory(table_options);

  const BlockBasedTableOptions* normalized_table_options =
      (const BlockBasedTableOptions*)factory->GetOptions();
  ASSERT_EQ(normalized_table_options->block_restart_interval, expected);

  delete factory;
}

TEST_F(BlockBasedTableTest, InvalidOptions) {
  // invalid values for block_size_deviation (<0 or >100) are silently set to 0
  ValidateBlockSizeDeviation(-10, 0);
  ValidateBlockSizeDeviation(-1, 0);
  ValidateBlockSizeDeviation(0, 0);
  ValidateBlockSizeDeviation(1, 1);
  ValidateBlockSizeDeviation(99, 99);
  ValidateBlockSizeDeviation(100, 100);
  ValidateBlockSizeDeviation(101, 0);
  ValidateBlockSizeDeviation(1000, 0);

  // invalid values for block_restart_interval (<1) are silently set to 1
  ValidateBlockRestartInterval(-10, 1);
  ValidateBlockRestartInterval(-1, 1);
  ValidateBlockRestartInterval(0, 1);
  ValidateBlockRestartInterval(1, 1);
  ValidateBlockRestartInterval(2, 2);
  ValidateBlockRestartInterval(1000, 1000);
}

TEST_F(BlockBasedTableTest, BlockReadCountTest) {
  // bloom_filter_type = 0 -- block-based filter
  // bloom_filter_type = 1 -- full filter
  for (int bloom_filter_type = 0; bloom_filter_type < 2; ++bloom_filter_type) {
    for (int filter_in_cache = 0; filter_in_cache < 2;
         ++filter_in_cache) {
      Options options;
      options.create_if_missing = true;

      BlockBasedTableOptions table_options;
      table_options.block_cache = NewLRUCache(1, 0);
      table_options.cache_index_and_filter_blocks = filter_in_cache;
      table_options.filter_policy.reset(
          NewBloomFilterPolicy(10, bloom_filter_type == 0));
      options.table_factory.reset(new BlockBasedTableFactory(table_options));
      std::vector<std::string> keys;
      stl_wrappers::KVMap kvmap;

      TableConstructor c(BytewiseComparator());
      std::string user_key = "k04";
      InternalKey internal_key(user_key, 0, kTypeValue);
      std::string encoded_key = internal_key.Encode().ToString();
      c.Add(encoded_key, "hello");
      ImmutableCFOptions ioptions(options);
      perf_context.Reset();
      // Generate table with filter policy
      c.Finish(options, ioptions, table_options,
               GetPlainInternalComparator(options.comparator), &keys, &kvmap);
      auto reader = c.GetTableReader();
      // Meta, properties and filter blocks.
      ASSERT_EQ(perf_context.block_read_count, 3);
      std::string value;
      GetContext get_context(options.comparator, nullptr, nullptr, nullptr,
                             GetContext::kNotFound, user_key, &value, nullptr,
                             nullptr, nullptr);
      perf_context.Reset();
      ASSERT_OK(reader->Get(ReadOptions(), encoded_key, &get_context));
      if (filter_in_cache) {
        // Data, index and filter block.
        ASSERT_EQ(perf_context.block_read_count, 3);
      } else {
        // Index, data block (filter is preloaded on open).
        ASSERT_EQ(perf_context.block_read_count, 2);
      }
      ASSERT_EQ(get_context.State(), GetContext::kFound);
      ASSERT_EQ(value, "hello");

      // Get non-existing key
      user_key = "does-not-exist";
      internal_key = InternalKey(user_key, 0, kTypeValue);
      encoded_key = internal_key.Encode().ToString();

      get_context = GetContext(options.comparator, nullptr, nullptr, nullptr,
                               GetContext::kNotFound, user_key, &value, nullptr,
                               nullptr, nullptr);
      perf_context.Reset();
      ASSERT_OK(reader->Get(ReadOptions(), encoded_key, &get_context));
      ASSERT_EQ(get_context.State(), GetContext::kNotFound);

      if (filter_in_cache) {
        if (bloom_filter_type == 0) {
          // with block-based, we read index and then the filter
          ASSERT_EQ(perf_context.block_read_count, 2);
        } else {
          // with full-filter, we read filter first and then we stop
          ASSERT_EQ(perf_context.block_read_count, 1);
        }
      } else {
        // filter is already in memory and it figures out that the key doesn't
        // exist
        ASSERT_EQ(perf_context.block_read_count, 0);
      }
    }
  }
}

TEST_F(BlockBasedTableTest, BlockCacheLeak) {
  // Check that when we reopen a table we don't lose access to blocks already
  // in the cache. This test checks whether the Table actually makes use of the
  // unique ID from the file.

  Options opt;
  auto ikc = std::make_shared<test::PlainInternalKeyComparator>(opt.comparator);
  opt.compression = kNoCompression;
  BlockBasedTableOptions table_options;
  table_options.block_size = 1024;
  // big enough so we don't ever lose cached values.
  table_options.block_cache = NewLRUCache((16 * 1024 * 1024) / FLAGS_cache_single_touch_ratio);
  opt.table_factory.reset(NewBlockBasedTableFactory(table_options));

  TableConstructor c(BytewiseComparator());
  c.Add("k01", "hello");
  c.Add("k02", "hello2");
  c.Add("k03", std::string(10000, 'x'));
  c.Add("k04", std::string(200000, 'x'));
  c.Add("k05", std::string(300000, 'x'));
  c.Add("k06", "hello3");
  c.Add("k07", std::string(100000, 'x'));
  std::vector<std::string> keys;
  stl_wrappers::KVMap kvmap;
  const ImmutableCFOptions ioptions(opt);
  c.Finish(opt, ioptions, table_options, ikc, &keys, &kvmap);

  {
    // Put iterator into dedicated block, so it doesn't survive block_cache destruction.
    unique_ptr<InternalIterator> iter(c.NewIterator());
    iter->SeekToFirst();
    while (iter->Valid()) {
      iter->key();
      iter->value();
      iter->Next();
    }
    ASSERT_OK(iter->status());
  }

  const ImmutableCFOptions ioptions1(opt);
  ASSERT_OK(c.Reopen(ioptions1));
  auto table_reader = dynamic_cast<BlockBasedTable*>(c.GetTableReader());
  for (const std::string& key : keys) {
    ASSERT_TRUE(table_reader->TEST_KeyInCache(ReadOptions(), key));
  }

  // rerun with different block cache
  table_options.block_cache =
    NewLRUCache((16 * 1024 * 1024) / FLAGS_cache_single_touch_ratio);
  opt.table_factory.reset(NewBlockBasedTableFactory(table_options));
  const ImmutableCFOptions ioptions2(opt);
  ASSERT_OK(c.Reopen(ioptions2));
  table_reader = dynamic_cast<BlockBasedTable*>(c.GetTableReader());
  for (const std::string& key : keys) {
    ASSERT_TRUE(!table_reader->TEST_KeyInCache(ReadOptions(), key));
  }
}

std::string GenerateKey(int primary_key, int secondary_key, int padding_size, Random* rnd) {
  char buf[50];
  char* p = &buf[0];
  snprintf(buf, sizeof(buf), "%6d%4d", primary_key, secondary_key);
  std::string k(p);
  if (padding_size) {
    k += RandomString(rnd, padding_size);
  }

  return k;
}

YB_DEFINE_ENUM(WorkloadType, (kScanForward)(kScanBackward)(kSeek));
using WorkloadTypeSet = yb::EnumBitSet<WorkloadType>;

uint64_t RunPerformanceTest(
    WorkloadTypeSet workloads,
    size_t block_size,
    IndexType idx_type,
    int restart_interval = 1,
    size_t num_keys = 10000,
    bool use_delta_encoding = false,
    KeyValueEncodingFormat encoding_format = KeyValueEncodingFormat::kKeyDeltaEncodingSharedPrefix,
    const ReadOptions& ro = {}) {
  constexpr auto kBlockRestartInterval = 16;
  BlockBasedTableOptions table_options;
  table_options.index_type = idx_type;
  table_options.block_size = block_size;
  table_options.block_cache = NewLRUCache(1 * 1024 * 1024);
  table_options.block_restart_interval = kBlockRestartInterval;
  table_options.index_block_restart_interval = restart_interval;
  table_options.min_keys_per_index_block = 100;
  table_options.use_delta_encoding = use_delta_encoding;
  table_options.data_block_key_value_encoding_format = encoding_format;

  TableConstructor table_constructor(BytewiseComparator());
  Random rnd(test::RandomSeed());
  Slice value_slice;
  for (int i = 0; i < yb::narrow_cast<int>(num_keys); i++) {
    /*10 digits value is generated for primary and secondary key by GenerateKey*/
    auto key = GenerateKey(i, i + 1000, 32 - 10, &rnd);
    table_constructor.Add(key, value_slice);
  }

  std::vector<std::string> keys;
  stl_wrappers::KVMap kvmap;
  Options options;
  options.prefix_extractor.reset(NewFixedPrefixTransform(3));
  options.table_factory.reset(NewBlockBasedTableFactory(table_options));

  auto comparator = std::make_shared<InternalKeyComparator>(BytewiseComparator());
  const ImmutableCFOptions ioptions(options);
  table_constructor.Finish(options, ioptions, table_options, comparator, &keys, &kvmap);

  auto* reader = down_cast<BlockBasedTable*>(table_constructor.GetTableReader());
  LOG(INFO) << "IndexType: " << idx_type << ", KeyCount: " << num_keys
            << ", BlockSize: " << block_size << ", RestartInterval: " << restart_interval
            << ", UseDeltaEncoding: " << use_delta_encoding << ", Encoding: " << encoding_format;
  std::stringstream order_msg;
  for (const auto wl : workloads) {
    if (order_msg.tellp() > 0) order_msg << ", ";
    switch (wl) {
      case WorkloadType::kScanForward:
        order_msg << "Next in nanosecond";
        break;
      case WorkloadType::kScanBackward:
        order_msg << "Prev in nanosecond";
        break;
      case WorkloadType::kSeek:
        order_msg << "Incr order, Incr order with gaps, Decr order, Decr order with "
                     "gaps, Random seek, Random seek with gaps";
        break;
      default:
        FATAL_INVALID_ENUM_VALUE(WorkloadType, wl);
    }
  }
  LOG(INFO) << "Result order: " << order_msg.str();

  uint64_t time_taken = 0;
  unique_ptr<InternalIterator> iter;
  std::stringstream result;
  auto run_benchmark = [&](std::function<void()>&& callback, int multiplier = 1) {
    iter.reset(table_constructor.NewIterator(ro));

    auto start = Env::Default()->NowNanos();
    callback();
    auto time_spent = (Env::Default()->NowNanos() - start);
    auto per_key_ns = (time_spent * multiplier) / num_keys;
    if (result.tellp() > 0) {
      result << ", ";
    }
    result << per_key_ns;
    return time_spent;
  };

  if (workloads.Test(WorkloadType::kScanForward)) {
    // Next scan text.
    size_t counter = 0;
    time_taken += run_benchmark([&]() {
      for (iter->SeekToFirst(); iter->Valid(); iter->Next()) {
        /*const auto k =*/ iter->key();
        /*const auto v =*/ iter->value();
        ++counter;
      }
      ASSERT_OK(iter->status());
      ASSERT_EQ(counter, num_keys);
      return;
    });
  }

  if (workloads.Test(WorkloadType::kScanBackward)) {
    // Prev scan text.
    size_t counter = 0;
    time_taken += run_benchmark([&]() {
      for (iter->SeekToLast(); iter->Valid(); iter->Prev()) {
        /*const auto k =*/ iter->key();
        /*const auto v =*/ iter->value();
        ++counter;
      }
      ASSERT_OK(iter->status());
      ASSERT_EQ(counter, num_keys);
      return;
    });
  }

  if (workloads.Test(WorkloadType::kSeek)) {
    // Increasing order.
    time_taken += run_benchmark([&]() {
      for (size_t k = 0; k < keys.size(); k++) {
        iter->Seek(keys[k]);
      }
      ASSERT_OK(iter->status());
      return;
    });

    // Increasing order with gaps.
    time_taken += run_benchmark(
        [&]() {
          for (size_t k = 0; k < keys.size(); k += 5) {
            iter->Seek(keys[k]);
          }
          return;
        },
        5);

    // Decreasing order.
    time_taken += run_benchmark([&]() {
      for (int k = static_cast<int>(keys.size()) - 1; k >= 0; k--) {
        iter->Seek(keys[k]);
      }
    });

    // Decreasing order with gaps.
    time_taken += run_benchmark(
        [&]() {
          for (int k = static_cast<int>(keys.size()) - 1; k >= 0; k -= 5) {
            iter->Seek(keys[k]);
          }
        },
        5);

    // Build the order.
    std::vector<std::string> keys_random(keys.size());
    for (size_t k = 0; k < keys.size(); k++) {
      keys_random[k] = keys[rand() % keys.size()];
    }

    // Random seek.
    time_taken += run_benchmark([&]() {
      for (size_t k = 0; k < keys.size(); k++) {
        iter->Seek(keys_random[k]);
      }
    });

    // Random seek with gaps.
    time_taken += run_benchmark(
        [&]() {
          for (size_t k = 0; k < keys.size(); k += 5) {
            iter->Seek(keys_random[k]);
          }
        },
        5);
  }

  LOG(INFO) << result.str();
  return time_taken;
}

void TestSeekPerformance(
    size_t block_size, const IndexType& idx_type, int restart_interval = 1, int num_keys = 10000) {
  const auto workload = WorkloadTypeSet{WorkloadType::kSeek};
  // Run every test 2 times.
  RunPerformanceTest(workload, block_size, idx_type, restart_interval, num_keys);
  RunPerformanceTest(workload, block_size, idx_type, restart_interval, num_keys);
}

void TestScanPerformance(
    size_t block_size,
    bool use_delta_encoding = false,
    KeyValueEncodingFormat encoding_format =
        KeyValueEncodingFormat::kKeyDeltaEncodingSharedPrefix) {
  IndexType idx_type = IndexType::kBinarySearch;
  WorkloadTypeSet workload { WorkloadType::kScanForward, WorkloadType::kScanBackward };

  // Run every test 2 times.
  RunPerformanceTest(
      workload, block_size, IndexType::kBinarySearch, /* restart_interval = */ 1,
      /* num_keys = */ 10000, use_delta_encoding, encoding_format);
  RunPerformanceTest(
      workload, block_size, IndexType::kBinarySearch, /* restart_interval = */ 1,
      /* num_keys = */ 10000, use_delta_encoding, encoding_format);
}

// Supports only WorkloadType::kScanForward and WorkloadType::kScanBackward.
void TestScanPerformance(
    size_t num_repeats,
    WorkloadType scan_type,
    const ReadOptions& ro = {},
    size_t num_keys = 100000,
    size_t block_size = 32_KB,
    bool use_delta_encoding = true,
    KeyValueEncodingFormat encoding_format =
        KeyValueEncodingFormat::kKeyDeltaEncodingThreeSharedParts,
    IndexType idx_type = IndexType::kMultiLevelBinarySearch) {
  const int index_restart_interval = 1;
  uint64_t total_time = 0;
  size_t num_runs = 0;

  // A vector to store experiment times to calculate unbiased estimation of standard deviation.
  std::vector<double> run_time_us;
  run_time_us.reserve(num_runs);

  WorkloadTypeSet workload { scan_type };
  while (num_runs++ < num_repeats) {
    auto time_taken = RunPerformanceTest(
      workload, block_size, idx_type, index_restart_interval,
      num_keys, use_delta_encoding, encoding_format, ro);
    LOG(INFO) << "Run #" << num_runs << ": " << time_taken / 1000 << " us, "
              << "per key: " << time_taken / num_keys << " ns";
    total_time += time_taken;
    run_time_us.push_back(static_cast<double>(time_taken) / 1000);
  }

  const auto total_avg = total_time / static_cast<double>(num_repeats);
  const auto total_avg_us = total_avg / 1000;

  // Calculate standard deviation.
  double std_dev_us = 0.0;
  double num_std_dev = 0.0;
  double num_2x_std_dev = 0.0;
  double num_3x_std_dev = 0.0;
  if (num_repeats > 1) {
    for (const auto rt : run_time_us) {
      std_dev_us += std::pow(total_avg_us - rt, 2.0);
    }
    std_dev_us = std::sqrt(std_dev_us / (num_repeats - 1));
    const double std_dev_2x = std_dev_us * 2;
    const double std_dev_3x = std_dev_us * 3;
    for (const auto rt : run_time_us) {
      const auto delta = std::abs(rt - total_avg_us);
      if (delta <= std_dev_us) {
        ++num_std_dev;
        ++num_2x_std_dev;
        ++num_3x_std_dev;
      } else if (delta <= std_dev_2x) {
        ++num_2x_std_dev;
        ++num_3x_std_dev;
      } else if (delta <= std_dev_3x) {
        ++num_3x_std_dev;
      }
    }
    num_std_dev    /= num_repeats;
    num_2x_std_dev /= num_repeats;
    num_3x_std_dev /= num_repeats;
  }


  LOG(INFO) << "num keys: " << num_keys << ", "
            << "avg time per key: " << total_avg / num_keys << " ns, "
            << "avg time: " << total_avg_us << " us, "
            << "std dev: "  << std_dev_us   << " us, "
            << "hits in [avg - std dev, avg + 1x std dev]: "    << 100 * num_std_dev    << "%, "
            << "hits in [avg - 2x std dev, avg + 2x std dev]: " << 100 * num_2x_std_dev << "%, "
            << "hits in [avg - 3x std dev, avg + 3x std dev]: " << 100 * num_3x_std_dev << "%";
}

TEST_F(BlockBasedTableTest, DISABLED_ForwardScanPerformanceTest) {
  TestScanPerformance(/* num_repeats = */ 1000, WorkloadType::kScanForward);
}

TEST_F(BlockBasedTableTest, DISABLED_BackwardScanPerformanceTest) {
  TestScanPerformance(/* num_repeats = */ 1000, WorkloadType::kScanBackward);
}

TEST_F(BlockBasedTableTest, DISABLED_FastBackwardScanPerformanceTest) {
  ReadOptions read_options;
  read_options.cache_restart_block_keys = CacheRestartBlockKeys::kTrue;
  TestScanPerformance(/* num_repeats = */ 1000, WorkloadType::kScanBackward, read_options);
}

TEST_F(BlockBasedTableTest, DISABLED_ScanPerformanceTest) {
  bool use_delta_encoding = true;
  KeyValueEncodingFormat encoding_format =
      KeyValueEncodingFormat::kKeyDeltaEncodingThreeSharedParts;
  TestScanPerformance(8_KB,  use_delta_encoding, encoding_format);
  TestScanPerformance(16_KB, use_delta_encoding, encoding_format);
  TestScanPerformance(32_KB, use_delta_encoding, encoding_format);
  TestScanPerformance(64_KB, use_delta_encoding, encoding_format);
}

TEST_F(BlockBasedTableTest, DISABLED_IndexTypeSeekPerformanceTest) {
  TestSeekPerformance(8_KB,  IndexType::kBinarySearch);
  TestSeekPerformance(16_KB, IndexType::kBinarySearch);
  TestSeekPerformance(32_KB, IndexType::kBinarySearch);
  TestSeekPerformance(64_KB, IndexType::kBinarySearch);

  TestSeekPerformance(8_KB,  IndexType::kMultiLevelBinarySearch);
  TestSeekPerformance(16_KB, IndexType::kMultiLevelBinarySearch);
  TestSeekPerformance(32_KB, IndexType::kMultiLevelBinarySearch);
  TestSeekPerformance(64_KB, IndexType::kMultiLevelBinarySearch);
}

TEST_F(BlockBasedTableTest, DISABLED_RestartPointsSeekPerformanceTest) {
  // 1 restart point.
  TestSeekPerformance(16_KB, IndexType::kBinarySearch, 1);
  TestSeekPerformance(32_KB, IndexType::kBinarySearch, 1);
  TestSeekPerformance(16_KB, IndexType::kMultiLevelBinarySearch, 1);
  TestSeekPerformance(32_KB, IndexType::kMultiLevelBinarySearch, 1);

  // 16 restart point.
  TestSeekPerformance(16_KB, IndexType::kBinarySearch, 16);
  TestSeekPerformance(32_KB, IndexType::kBinarySearch, 16);
  TestSeekPerformance(16_KB, IndexType::kMultiLevelBinarySearch, 16);
  TestSeekPerformance(32_KB, IndexType::kMultiLevelBinarySearch, 16);
}

TEST_F(BlockBasedTableTest, DISABLED_KeysCountSeekPerformanceTest) {
  // 10k keys.
  TestSeekPerformance(16_KB, IndexType::kBinarySearch, 1);
  TestSeekPerformance(32_KB, IndexType::kBinarySearch, 1);
  TestSeekPerformance(16_KB, IndexType::kMultiLevelBinarySearch, 1);
  TestSeekPerformance(32_KB, IndexType::kMultiLevelBinarySearch, 1);

  // 100k keys.
  TestSeekPerformance(16_KB, IndexType::kBinarySearch, 1, 100000);
  TestSeekPerformance(32_KB, IndexType::kBinarySearch, 1, 100000);
  TestSeekPerformance(16_KB, IndexType::kMultiLevelBinarySearch, 1, 100000);
  TestSeekPerformance(32_KB, IndexType::kMultiLevelBinarySearch, 1, 100000);
}

TEST_F(PlainTableTest, BasicPlainTableProperties) {
  PlainTableOptions plain_table_options;
  plain_table_options.user_key_len = 8;
  plain_table_options.bloom_bits_per_key = 8;
  plain_table_options.hash_table_ratio = 0;

  PlainTableFactory factory(plain_table_options);
  test::StringSink sink;
  unique_ptr<WritableFileWriter> file_writer(
      test::GetWritableFileWriter(new test::StringSink()));
  Options options;
  const ImmutableCFOptions ioptions(options);
  auto ikc = std::make_shared<InternalKeyComparator>(options.comparator);
  IntTblPropCollectorFactories int_tbl_prop_collector_factories;
  std::unique_ptr<TableBuilder> builder(factory.NewTableBuilder(
      TableBuilderOptions(ioptions,
                          ikc,
                          int_tbl_prop_collector_factories,
                          kNoCompression,
                          CompressionOptions(),
                          /* skip_filters */ false),
      TablePropertiesCollectorFactory::Context::kUnknownColumnFamily,
      file_writer.get()));

  for (char c = 'a'; c <= 'z'; ++c) {
    std::string key(8, c);
    key.append("\1       ");  // PlainTable expects internal key structure
    std::string value(28, c + 42);
    builder->Add(key, value);
  }
  ASSERT_OK(builder->Finish());
  ASSERT_OK(file_writer->Flush());

  test::StringSink* ss =
    static_cast<test::StringSink*>(file_writer->writable_file());
  unique_ptr<RandomAccessFileReader> file_reader(
      test::GetRandomAccessFileReader(
          new test::StringSource(ss->contents(), 72242, true)));

  TableProperties* props = nullptr;
  auto s = ReadTableProperties(file_reader.get(), ss->contents().size(),
                               kPlainTableMagicNumber, Env::Default(), nullptr,
                               &props);
  std::unique_ptr<TableProperties> props_guard(props);
  ASSERT_OK(s);

  ASSERT_EQ(0ul, props->data_index_size);
  ASSERT_EQ(0ul, props->filter_size);
  ASSERT_EQ(16ul * 26, props->raw_key_size);
  ASSERT_EQ(28ul * 26, props->raw_value_size);
  ASSERT_EQ(26ul, props->num_entries);
  ASSERT_EQ(1ul, props->num_data_blocks);
}

TEST_F(GeneralTableTest, ApproximateOffsetOfPlain) {
  TableConstructor c(BytewiseComparator());
  c.Add("k01", "hello");
  c.Add("k02", "hello2");
  c.Add("k03", std::string(10000, 'x'));
  c.Add("k04", std::string(200000, 'x'));
  c.Add("k05", std::string(300000, 'x'));
  c.Add("k06", "hello3");
  c.Add("k07", std::string(100000, 'x'));
  std::vector<std::string> keys;
  stl_wrappers::KVMap kvmap;
  Options options;
  auto internal_comparator = std::make_shared<test::PlainInternalKeyComparator>(options.comparator);
  options.compression = kNoCompression;
  BlockBasedTableOptions table_options;
  table_options.block_size = 1024;
  const ImmutableCFOptions ioptions(options);
  c.Finish(options, ioptions, table_options, internal_comparator,
           &keys, &kvmap);

  ASSERT_TRUE(Between(c.ApproximateOffsetOf("abc"),       0,      0));
  ASSERT_TRUE(Between(c.ApproximateOffsetOf("k01"),       0,      0));
  ASSERT_TRUE(Between(c.ApproximateOffsetOf("k01a"),      0,      0));
  ASSERT_TRUE(Between(c.ApproximateOffsetOf("k02"),       0,      0));
  ASSERT_TRUE(Between(c.ApproximateOffsetOf("k03"),       0,      0));
  ASSERT_TRUE(Between(c.ApproximateOffsetOf("k04"),   10000,  11000));
  ASSERT_TRUE(Between(c.ApproximateOffsetOf("k04a"), 210000, 211000));
  ASSERT_TRUE(Between(c.ApproximateOffsetOf("k05"),  210000, 211000));
  ASSERT_TRUE(Between(c.ApproximateOffsetOf("k06"),  510000, 511000));
  ASSERT_TRUE(Between(c.ApproximateOffsetOf("k07"),  510000, 511000));
  ASSERT_TRUE(Between(c.ApproximateOffsetOf("xyz"),  610000, 612000));
}

static void DoCompressionTest(CompressionType comp) {
  Random rnd(301);
  TableConstructor c(BytewiseComparator());
  std::string tmp;
  c.Add("k01", "hello");
  c.Add("k02", CompressibleString(&rnd, 0.25, 10000, &tmp));
  c.Add("k03", "hello3");
  c.Add("k04", CompressibleString(&rnd, 0.25, 10000, &tmp));
  std::vector<std::string> keys;
  stl_wrappers::KVMap kvmap;
  Options options;
  auto ikc = std::make_shared<test::PlainInternalKeyComparator>(options.comparator);
  options.compression = comp;
  BlockBasedTableOptions table_options;
  table_options.block_size = 1024;
  const ImmutableCFOptions ioptions(options);
  c.Finish(options, ioptions, table_options, ikc, &keys, &kvmap);

  ASSERT_TRUE(Between(c.ApproximateOffsetOf("abc"),       0,      0));
  ASSERT_TRUE(Between(c.ApproximateOffsetOf("k01"),       0,      0));
  ASSERT_TRUE(Between(c.ApproximateOffsetOf("k02"),       0,      0));
  ASSERT_TRUE(Between(c.ApproximateOffsetOf("k03"),    2000,   3100));
  ASSERT_TRUE(Between(c.ApproximateOffsetOf("k04"),    2000,   3100));
  ASSERT_TRUE(Between(c.ApproximateOffsetOf("xyz"),    4000,   6200));
}

TEST_F(GeneralTableTest, ApproximateOffsetOfCompressed) {
  std::vector<CompressionType> compression_state;
  if (!Snappy_Supported()) {
    fprintf(stderr, "skipping snappy compression tests\n");
  } else {
    compression_state.push_back(kSnappyCompression);
  }

  if (!Zlib_Supported()) {
    fprintf(stderr, "skipping zlib compression tests\n");
  } else {
    compression_state.push_back(kZlibCompression);
  }

  // TODO(kailiu) DoCompressionTest() doesn't work with BZip2.
  /*
  if (!BZip2_Supported()) {
    fprintf(stderr, "skipping bzip2 compression tests\n");
  } else {
    compression_state.push_back(kBZip2Compression);
  }
  */

  if (!LZ4_Supported()) {
    fprintf(stderr, "skipping lz4 and lz4hc compression tests\n");
  } else {
    compression_state.push_back(kLZ4Compression);
    compression_state.push_back(kLZ4HCCompression);
  }

  for (auto state : compression_state) {
    DoCompressionTest(state);
  }
}

TEST_F(HarnessTest, Randomized) {
#if defined(THREAD_SANITIZER)
  static constexpr int kMaxNumEntries = 200;
#else
  static constexpr int kMaxNumEntries = 2000;
#endif
  std::vector<TestArgs> args = GenerateArgList();
  for (unsigned int i = 0; i < args.size(); i++) {
    Init(args[i]);
    Random rnd(test::RandomSeed() + 5);
    for (int num_entries = 0; num_entries < kMaxNumEntries;
         num_entries += (num_entries < 50 ? 1 : 200)) {
      if ((num_entries % 10) == 0) {
        fprintf(stderr, "case %d of %d: num_entries = %d\n", (i + 1),
                static_cast<int>(args.size()), num_entries);
      }
      for (int e = 0; e < num_entries; e++) {
        std::string v;
        Add(test::RandomKey(&rnd, rnd.Skewed(4)),
            RandomString(&rnd, rnd.Skewed(5), &v).ToString());
      }
      Test(&rnd);
    }
  }
}

TEST_F(HarnessTest, RandomizedLongDB) {
  Random rnd(test::RandomSeed());
  TestArgs args = {DB_TEST, false, 16, kNoCompression, 0, false};
  Init(args);
  int num_entries = 100000;
  for (int e = 0; e < num_entries; e++) {
    std::string v;
    Add(test::RandomKey(&rnd, rnd.Skewed(4)),
        RandomString(&rnd, rnd.Skewed(5), &v).ToString());
  }
  Test(&rnd);

  // We must have created enough data to force merging
  int files = 0;
  for (int level = 0; level < db()->NumberLevels(); level++) {
    std::string value;
    char name[100];
    snprintf(name, sizeof(name), "rocksdb.num-files-at-level%d", level);
    ASSERT_TRUE(db()->GetProperty(name, &value));
    files += atoi(value.c_str());
  }
  ASSERT_GT(files, 0);
}

class MemTableTest : public RocksDBTest {};

TEST_F(MemTableTest, Simple) {
  InternalKeyComparator cmp(BytewiseComparator());
  auto table_factory = std::make_shared<SkipListFactory>();
  Options options;
  options.memtable_factory = table_factory;
  ImmutableCFOptions ioptions(options);
  WriteBuffer wb(options.db_write_buffer_size);
  MemTable* memtable =
      new MemTable(cmp, ioptions, MutableCFOptions(options, ioptions), &wb,
                   kMaxSequenceNumber);
  memtable->Ref();
  WriteBatch batch;
  WriteBatchInternal::SetSequence(&batch, 100);
  batch.Put(std::string("k1"), std::string("v1"));
  batch.Put(std::string("k2"), std::string("v2"));
  batch.Put(std::string("k3"), std::string("v3"));
  batch.Put(std::string("largekey"), std::string("vlarge"));
  ColumnFamilyMemTablesDefault cf_mems_default(memtable);
  ASSERT_TRUE(
      WriteBatchInternal::InsertInto(&batch, &cf_mems_default, nullptr).ok());

  Arena arena;
  ScopedArenaIterator iter(memtable->NewIterator(ReadOptions(), &arena));
  iter->SeekToFirst();
  while (iter->Valid()) {
    fprintf(stderr, "key: '%s' -> '%s'\n",
            iter->key().ToString().c_str(),
            iter->value().ToString().c_str());
    iter->Next();
  }
  ASSERT_OK(iter->status());

  delete memtable->Unref();
}

// Test the empty key
TEST_F(HarnessTest, SimpleEmptyKey) {
  auto args = GenerateArgList();
  for (const auto& arg : args) {
    Init(arg);
    Random rnd(test::RandomSeed() + 1);
    Add("", "v");
    Test(&rnd);
  }
}

TEST_F(HarnessTest, SimpleSingle) {
  auto args = GenerateArgList();
  for (const auto& arg : args) {
    Init(arg);
    Random rnd(test::RandomSeed() + 2);
    Add("abc", "v");
    Test(&rnd);
  }
}

TEST_F(HarnessTest, SimpleMulti) {
  auto args = GenerateArgList();
  for (const auto& arg : args) {
    Init(arg);
    Random rnd(test::RandomSeed() + 3);
    Add("abc", "v");
    Add("abcd", "v");
    Add("ac", "v2");
    Test(&rnd);
  }
}

TEST_F(HarnessTest, SimpleSpecialKey) {
  auto args = GenerateArgList();
  for (const auto& arg : args) {
    Init(arg);
    Random rnd(test::RandomSeed() + 4);
    Add("\xff\xff", "v3");
    Test(&rnd);
  }
}

TEST_F(HarnessTest, FooterTests) {
  {
    // upconvert legacy block based
    std::string encoded;
    Footer footer(kLegacyBlockBasedTableMagicNumber, 0);
    BlockHandle meta_index(10, 5), index(20, 15);
    footer.set_metaindex_handle(meta_index);
    footer.set_index_handle(index);
    footer.AppendEncodedTo(&encoded);
    Footer decoded_footer;
    Slice encoded_slice(encoded);
    ASSERT_OK(decoded_footer.DecodeFrom(&encoded_slice));
    ASSERT_EQ(decoded_footer.table_magic_number(), kBlockBasedTableMagicNumber);
    ASSERT_EQ(decoded_footer.checksum(), kCRC32c);
    ASSERT_EQ(decoded_footer.metaindex_handle().offset(), meta_index.offset());
    ASSERT_EQ(decoded_footer.metaindex_handle().size(), meta_index.size());
    ASSERT_EQ(decoded_footer.index_handle().offset(), index.offset());
    ASSERT_EQ(decoded_footer.index_handle().size(), index.size());
    ASSERT_EQ(decoded_footer.version(), 0U);
  }
  {
    // xxhash block based
    std::string encoded;
    Footer footer(kBlockBasedTableMagicNumber, 1);
    BlockHandle meta_index(10, 5), index(20, 15);
    footer.set_metaindex_handle(meta_index);
    footer.set_index_handle(index);
    footer.set_checksum(kxxHash);
    footer.AppendEncodedTo(&encoded);
    Footer decoded_footer;
    Slice encoded_slice(encoded);
    ASSERT_OK(decoded_footer.DecodeFrom(&encoded_slice));
    ASSERT_EQ(decoded_footer.table_magic_number(), kBlockBasedTableMagicNumber);
    ASSERT_EQ(decoded_footer.checksum(), kxxHash);
    ASSERT_EQ(decoded_footer.metaindex_handle().offset(), meta_index.offset());
    ASSERT_EQ(decoded_footer.metaindex_handle().size(), meta_index.size());
    ASSERT_EQ(decoded_footer.index_handle().offset(), index.offset());
    ASSERT_EQ(decoded_footer.index_handle().size(), index.size());
    ASSERT_EQ(decoded_footer.version(), 1U);
  }
  {
    // upconvert legacy plain table
    std::string encoded;
    Footer footer(kLegacyPlainTableMagicNumber, 0);
    BlockHandle meta_index(10, 5), index(20, 15);
    footer.set_metaindex_handle(meta_index);
    footer.set_index_handle(index);
    footer.AppendEncodedTo(&encoded);
    Footer decoded_footer;
    Slice encoded_slice(encoded);
    ASSERT_OK(decoded_footer.DecodeFrom(&encoded_slice));
    ASSERT_EQ(decoded_footer.table_magic_number(), kPlainTableMagicNumber);
    ASSERT_EQ(decoded_footer.checksum(), kCRC32c);
    ASSERT_EQ(decoded_footer.metaindex_handle().offset(), meta_index.offset());
    ASSERT_EQ(decoded_footer.metaindex_handle().size(), meta_index.size());
    ASSERT_EQ(decoded_footer.index_handle().offset(), index.offset());
    ASSERT_EQ(decoded_footer.index_handle().size(), index.size());
    ASSERT_EQ(decoded_footer.version(), 0U);
  }
  {
    // xxhash block based
    std::string encoded;
    Footer footer(kPlainTableMagicNumber, 1);
    BlockHandle meta_index(10, 5), index(20, 15);
    footer.set_metaindex_handle(meta_index);
    footer.set_index_handle(index);
    footer.set_checksum(kxxHash);
    footer.AppendEncodedTo(&encoded);
    Footer decoded_footer;
    Slice encoded_slice(encoded);
    ASSERT_OK(decoded_footer.DecodeFrom(&encoded_slice));
    ASSERT_EQ(decoded_footer.table_magic_number(), kPlainTableMagicNumber);
    ASSERT_EQ(decoded_footer.checksum(), kxxHash);
    ASSERT_EQ(decoded_footer.metaindex_handle().offset(), meta_index.offset());
    ASSERT_EQ(decoded_footer.metaindex_handle().size(), meta_index.size());
    ASSERT_EQ(decoded_footer.index_handle().offset(), index.offset());
    ASSERT_EQ(decoded_footer.index_handle().size(), index.size());
    ASSERT_EQ(decoded_footer.version(), 1U);
  }
  {
    // version == 2
    std::string encoded;
    Footer footer(kBlockBasedTableMagicNumber, 2);
    BlockHandle meta_index(10, 5), index(20, 15);
    footer.set_metaindex_handle(meta_index);
    footer.set_index_handle(index);
    footer.AppendEncodedTo(&encoded);
    Footer decoded_footer;
    Slice encoded_slice(encoded);
    ASSERT_OK(decoded_footer.DecodeFrom(&encoded_slice));
    ASSERT_EQ(decoded_footer.table_magic_number(), kBlockBasedTableMagicNumber);
    ASSERT_EQ(decoded_footer.checksum(), kCRC32c);
    ASSERT_EQ(decoded_footer.metaindex_handle().offset(), meta_index.offset());
    ASSERT_EQ(decoded_footer.metaindex_handle().size(), meta_index.size());
    ASSERT_EQ(decoded_footer.index_handle().offset(), index.offset());
    ASSERT_EQ(decoded_footer.index_handle().size(), index.size());
    ASSERT_EQ(decoded_footer.version(), 2U);
  }
}

class IndexBlockRestartIntervalTest
    : public BlockBasedTableTest,
      public ::testing::WithParamInterface<int> {
 public:
  static std::vector<int> GetRestartValues() { return {-1, 0, 1, 8, 16, 32}; }
};

INSTANTIATE_TEST_CASE_P(
    IndexBlockRestartIntervalTest, IndexBlockRestartIntervalTest,
    ::testing::ValuesIn(IndexBlockRestartIntervalTest::GetRestartValues()));

TEST_P(IndexBlockRestartIntervalTest, IndexBlockRestartInterval) {
  const int kKeysInTable = 10000;
  const int kKeySize = 100;
  const int kValSize = 500;

  int index_block_restart_interval = GetParam();

  std::vector<std::optional<KeyValueEncodingFormat>> formats_to_test;
  for (const auto& format : KeyValueEncodingFormatList()) {
    formats_to_test.push_back(format);
  }
  // Also test backward compatibility with SST files without
  // BlockBasedTablePropertyNames::kDataBlockKeyValueEncodingFormat property.
  formats_to_test.push_back(std::nullopt);

  for (const auto& format : formats_to_test) {
    Options options;
    BlockBasedTableOptions table_options;
    table_options.block_size = 64;  // small block size to get big index block
    table_options.index_block_restart_interval = index_block_restart_interval;
    // SST files without BlockBasedTablePropertyNames::kDataBlockKeyValueEncodingFormat only could
    // be written with using KeyValueEncodingFormat::kKeyDeltaEncodingSharedPrefix, because there
    // were no other formats before we added this property.
    table_options.data_block_key_value_encoding_format =
        format.value_or(KeyValueEncodingFormat::kKeyDeltaEncodingSharedPrefix);
    options.table_factory.reset(new BlockBasedTableFactory(table_options));

    TableConstructor c(BytewiseComparator());
    if (!format.has_value()) {
      // Backward compatibility: test that we can read files without
      // BlockBasedTablePropertyNames::kDataBlockKeyValueEncodingFormat property.
      c.TEST_skip_writing_key_value_encoding_format = true;
    }
    static Random rnd(301);
    for (int i = 0; i < kKeysInTable; i++) {
      InternalKey k(RandomString(&rnd, kKeySize), 0, kTypeValue);
      c.Add(k.Encode().ToString(), RandomString(&rnd, kValSize));
    }

    std::vector<std::string> keys;
    stl_wrappers::KVMap kvmap;
    auto comparator = std::make_shared<InternalKeyComparator>(BytewiseComparator());
    const ImmutableCFOptions ioptions(options);
    c.Finish(options, ioptions, table_options, comparator, &keys, &kvmap);
    auto reader = c.GetTableReader();

    std::unique_ptr<InternalIterator> db_iter(reader->NewIterator(ReadOptions()));

    // Test point lookup
    for (auto& kv : kvmap) {
      db_iter->Seek(kv.first);

      ASSERT_TRUE(db_iter->Valid());
      ASSERT_OK(db_iter->status());
      ASSERT_EQ(db_iter->key(), kv.first);
      ASSERT_EQ(db_iter->value(), kv.second);
    }

    // Test iterating
    auto kv_iter = kvmap.begin();
    for (db_iter->SeekToFirst(); db_iter->Valid(); db_iter->Next()) {
      ASSERT_EQ(db_iter->key(), kv_iter->first);
      ASSERT_EQ(db_iter->value(), kv_iter->second);
      kv_iter++;
    }
    ASSERT_OK(db_iter->status());
    ASSERT_EQ(kv_iter, kvmap.end());
  }
}

class PrefixTest : public RocksDBTest {};

namespace {
// A simple PrefixExtractor that only works for test PrefixAndWholeKeyTest
class TestPrefixExtractor : public rocksdb::SliceTransform {
 public:
  ~TestPrefixExtractor() override{};
  const char* Name() const override { return "TestPrefixExtractor"; }

  rocksdb::Slice Transform(const rocksdb::Slice& src) const override {
    assert(IsValid(src));
    return rocksdb::Slice(src.data(), 3);
  }

  bool InDomain(const rocksdb::Slice& src) const override {
    assert(IsValid(src));
    return true;
  }

  bool InRange(const rocksdb::Slice& dst) const override { return true; }

  bool IsValid(const rocksdb::Slice& src) const {
    if (src.size() != 4) {
      return false;
    }
    if (src[0] != '[') {
      return false;
    }
    if (src[1] < '0' || src[1] > '9') {
      return false;
    }
    if (src[2] != ']') {
      return false;
    }
    if (src[3] < '0' || src[3] > '9') {
      return false;
    }
    return true;
  }
};
}  // namespace

TEST_F(PrefixTest, PrefixAndWholeKeyTest) {
  rocksdb::BlockBasedTableOptions bbto;
  bbto.block_size = 262144;
  bbto.whole_key_filtering = true;

  rocksdb::Options options;
  options.compaction_style = rocksdb::kCompactionStyleUniversal;
  options.num_levels = 20;
  options.create_if_missing = true;
  options.optimize_filters_for_hits = false;
  options.target_file_size_base = 268435456;
  options.prefix_extractor = std::make_shared<TestPrefixExtractor>();

  for (int it = 0; it < 2; it++) {
    switch (it) {
      case 0:
        bbto.filter_policy.reset(rocksdb::NewBloomFilterPolicy(10));
        break;
      default:
        SetFixedSizeFilterPolicy(&bbto);
        break;
    }

    const std::string kDBPath = test::TmpDir() + "/prefix_test";
    options.table_factory.reset(NewBlockBasedTableFactory(bbto));
    ASSERT_OK(DestroyDB(kDBPath, options));
    rocksdb::DB* db;
    ASSERT_OK(rocksdb::DB::Open(options, kDBPath, &db));

    // Create a bunch of keys with 10 filters.
    for (int i = 0; i < 10; i++) {
      std::string prefix = "[" + std::to_string(i) + "]";
      for (int j = 0; j < 10; j++) {
        std::string key = prefix + std::to_string(j);
        ASSERT_OK(db->Put(rocksdb::WriteOptions(), key, "1"));
      }
    }

    // Trigger compaction.
    ASSERT_OK(db->CompactRange(CompactRangeOptions(), nullptr, nullptr));
    delete db;
    // In the second round, turn whole_key_filtering off and expect
    // rocksdb still works.
  }
}

namespace {

void GenerateSSTFile(rocksdb::DB* db, int start_index, int num_records) {
  for (int j = start_index; j < start_index + num_records; j++) {
    ASSERT_OK(db->Put(rocksdb::WriteOptions(), std::to_string(j), "1"));
  }
  ASSERT_OK(db->Flush(FlushOptions()));
}

} // namespace

TEST_F(TableTest, MiddleOfMiddleKey) {
  rocksdb::Options options;
  options.compaction_style = rocksdb::kCompactionStyleNone;
  options.num_levels = 1;
  options.create_if_missing = true;
  const std::string kDBPath = test::TmpDir() + "/mid_key";
  ASSERT_OK(DestroyDB(kDBPath, options));
  rocksdb::DB* db;
  ASSERT_OK(rocksdb::DB::Open(options, kDBPath, &db));

  // Create two files with 200 and 300 records.
  GenerateSSTFile(db, 0, 200);
  GenerateSSTFile(db, 200, 300);

  // Same as the midkey of the largest sst which has 300 records.
  const Slice kEmptyKey;
  const Slice kEmptyInternalKey;
  const auto mkey_first = ASSERT_RESULT(db->GetMiddleKey(kEmptyKey));
  const auto tw = ASSERT_RESULT(db->TEST_GetLargestSstTableReader());
  const auto mid_key_of_sst = ASSERT_RESULT(tw->GetMiddleKey(kEmptyInternalKey));
  ASSERT_EQ(mkey_first, mid_key_of_sst);

  // Create a file with 400 records. This is largest sst.
  GenerateSSTFile(db, 500, 400);

  const auto mkey_second = ASSERT_RESULT(db->GetMiddleKey(kEmptyKey));
  // Still the same as the midkey of the previous largest sst.
  ASSERT_EQ(mkey_second, mid_key_of_sst);
  delete db;
}

// Open the first DB and generate some keys. Then open the second DB using a checkpoint
// from the first one. Verify that both databases share the same block cache key prefix.
TEST_F(TableTest, YB_LINUX_ONLY_TEST(BlockCacheWithHardlink)) {
  BlockBasedTableOptions table_options;
  table_options.block_size = 1024;
  table_options.block_cache = NewLRUCache(16_MB / FLAGS_cache_single_touch_ratio);

  rocksdb::Options options;
  options.compaction_style = rocksdb::kCompactionStyleNone;
  options.num_levels = 1;
  options.create_if_missing = true;
  options.table_factory.reset(NewBlockBasedTableFactory(table_options));

  const std::string kDBPath = test::TmpDir() + "/hardlink";
  ASSERT_OK(DestroyDB(kDBPath, options));
  rocksdb::DB* db;
  ASSERT_OK(rocksdb::DB::Open(options, kDBPath, &db));

  GenerateSSTFile(db, 0, 10);

  const Slice kEmptyKey;
  const auto mkey = ASSERT_RESULT(db->GetMiddleKey(kEmptyKey));
  const auto tw_1 = ASSERT_RESULT(db->TEST_GetLargestSstTableReader());
  auto* table_reader_1 = dynamic_cast<BlockBasedTable*>(tw_1);
  ASSERT_TRUE(table_reader_1->TEST_KeyInCache(ReadOptions(), mkey));

  LOG(INFO) << "Opening checkpoint db";
  auto checkpoint_dir = kDBPath + "/checkpoints";
  ASSERT_OK(checkpoint::CreateCheckpoint(db, checkpoint_dir));
  rocksdb::DB* checkpoint_db;
  ASSERT_OK(rocksdb::DB::Open(options, checkpoint_dir, &checkpoint_db));
  const auto tw_2 = ASSERT_RESULT(checkpoint_db->TEST_GetLargestSstTableReader());
  auto* table_reader_2 = dynamic_cast<BlockBasedTable*>(tw_2);
  ASSERT_TRUE(table_reader_2->TEST_KeyInCache(ReadOptions(), mkey));

  delete db;
  delete checkpoint_db;
}

void BlockBasedTableTest::TestMiddleKey(
    uint64_t num_records, uint64_t index_step, size_t data_block_size,
    int expected_num_index_levels, int expected_num_data_blocks) {
  // Magic numbers to produce test index structures.
  constexpr auto kIndexBlockSize = 1_KB;
  constexpr auto kRecordValSize = 48;

  // Reverse comparator is used to preserve order since the table keys are encoded in little-endian
  // format.
  TableConstructor c(&reverse_key_comparator);
  const std::string val(kRecordValSize, 'v');
  for (uint64_t key = 0; key < num_records; ++key) {
    std::string key_str;
    PutFixed64(&key_str, key);
    c.Add(key_str, val);
  }

  std::vector<std::string> keys;
  stl_wrappers::KVMap kvmap;
  Options options;
  options.compression = kNoCompression;
  BlockBasedTableOptions table_options;
  table_options.block_size = data_block_size;
  table_options.index_block_size = kIndexBlockSize;
  options.table_factory.reset(NewBlockBasedTableFactory(table_options));
  const ImmutableCFOptions ioptions(options);
  auto comparator = GetPlainInternalComparator(&reverse_key_comparator);
  c.Finish(options, ioptions, table_options, comparator, &keys, &kvmap);
  // Verify inserts.
  ASSERT_EQ(num_records, keys.size());
  // Verify number of index levels.
  auto* reader = c.GetTableReader();
  {
    auto props = c.GetTableProperties().user_collected_properties;
    auto pos = props.find(BlockBasedTablePropertyNames::kNumIndexLevels);
    DCHECK(pos != props.end());
    int num_index_levels = DecodeFixed32(pos->second.c_str());
    ASSERT_EQ(expected_num_index_levels, num_index_levels);
  }
  // If applicable, verify number of data blocks.
  if (expected_num_data_blocks > 0)  {
    auto props = reader->GetTableProperties();
    ASSERT_EQ(expected_num_data_blocks, props->num_data_blocks);
  }

  auto lower_bound = num_records - index_step;
  for (int i = 1; i <= 5; ++i) {
    LOG(INFO) << "Testing with " << i << " top-level index entries";
    std::string lower_bound_str;
    PutFixed64(&lower_bound_str, lower_bound);
    auto middle_key_str = ASSERT_RESULT(reader->GetMiddleKey(lower_bound_str));
    Slice middle_key_buf(middle_key_str);
    LOG(INFO) << "Middle Key: " << middle_key_buf.ToDebugHexString();
    uint64_t middle_key;
    ASSERT_TRUE(GetFixed64(&middle_key_buf, &middle_key));
    ASSERT_GE(middle_key, lower_bound);
    auto split_percent =
        narrow_cast<int>(100 * (middle_key - lower_bound + 1) / (num_records - lower_bound));
    LOG(INFO) << "Split Ratio: " << split_percent << "%";
    auto deviation = abs(split_percent - 50);
    if (i <= 3) {
      ASSERT_LE(deviation, 16);
    } else {
      ASSERT_LE(deviation, 10);
    }
    lower_bound -= index_step;
  }

  // Notes on deviation:
  // - With three qualifying top-level index entries, the middle key is chosen as the second key.
  // The second restart key represents the upper bound of the key range corresponding to that
  // restart. Hence we expect this calculated midpoint to skew greater (~66th percentile) than the
  // actual midpoint.
  // - The above-mentioned skew is absent when the number of qualifying entries is even. For
  // instance, here the second restart key of the four entries is chosen, and that key is the upper
  // bound of the first and second key ranges, i.e., the midpoint of the four key ranges.
  // - The above-mentioned skew for odd number of entries reduces as number of entries increase
  // since the fraction of the total key range represented by each restart reduces.
}

TEST_F(BlockBasedTableTest, SplitKeyOneLevelIndex) {
  // Magic numbers to produce test index structures.
  constexpr auto kDataBlockSize = 4_KB;
  constexpr auto kNumRecords = 544;

  constexpr auto kExpectedNumIndexLevels = 1;
  constexpr auto kExpectedNumDataBlocks = 8;
  constexpr auto kNumTopLevelEntries = kExpectedNumDataBlocks;
  constexpr auto kApproxIndexEntryStep = kNumRecords / kNumTopLevelEntries;

  TestMiddleKey(
      kNumRecords, kApproxIndexEntryStep, kDataBlockSize, kExpectedNumIndexLevels,
      kExpectedNumDataBlocks);
  // Note: These are the exact split ratios:
  // 48% when 1 out of 8 entries qualify in the top-level block.
  // 50% when 2 out of 8 entries qualify in the top-level block.
  // 66% when 3 out of 8 entries qualify in the top-level block.
  // 50% when 4 out of 8 entries qualify in the top-level block.
  // 60% when 5 out of 8 entries qualify in the top-level block.
}

TEST_F(BlockBasedTableTest, SplitKeyTwoLevelIndex) {
  // Magic numbers to produce test index structures.
  constexpr auto kDataBlockSize = 1_KB;
  constexpr auto kNumRecords = 8568;

  constexpr auto kExpectedNumIndexLevels = 2;
  constexpr auto kNumTopLevelEntries = 8;
  // Note: kNumTopLevelEntries is not verfied by the test and may change with changes to storage
  // organization.
  constexpr auto kApproxIndexEntryStep = kNumRecords / kNumTopLevelEntries;

  TestMiddleKey(kNumRecords, kApproxIndexEntryStep, kDataBlockSize, kExpectedNumIndexLevels);
  // Note: These are the exact split ratios:
  // 50% when 1 out of 8 entries qualify in the top-level block.
  // 50% when 2 out of 8 entries qualify in the top-level block.
  // 66% when 3 out of 8 entries qualify in the top-level block.
  // 50% when 4 out of 8 entries qualify in the top-level block.
  // 60% when 5 out of 8 entries qualify in the top-level block.
}

}  // namespace rocksdb

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
