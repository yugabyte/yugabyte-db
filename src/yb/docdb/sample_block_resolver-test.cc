// Copyright (c) YugabyteDB, Inc.
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

#include "yb/docdb/sample_block_resolver.h"

#include <algorithm>
#include <optional>
#include <string>
#include <vector>

#include "yb/common/common.pb.h"
#include "yb/common/ql_value.h"
#include "yb/common/schema.h"

#include "yb/docdb/doc_read_context.h"
#include "yb/docdb/docdb_test_base.h"
#include "yb/docdb/key_bounds.h"
#include "yb/docdb/ql_rocksdb_storage.h"

#include "yb/dockv/doc_key.h"
#include "yb/dockv/doc_path.h"
#include "yb/dockv/partition.h"
#include "yb/dockv/value_type.h"

#include "yb/rocksdb/db.h"
#include "yb/rocksdb/table.h"
#include "yb/rocksdb/table_properties.h"

#include "yb/util/cast.h"
#include "yb/util/coding.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/test_macros.h"

DECLARE_bool(db_write_cumulative_index_entry_counts);
DECLARE_bool(rocksdb_disable_compactions);
DECLARE_int32(index_block_restart_interval);
DECLARE_int64(db_block_size_bytes);
DECLARE_int64(db_index_block_size_bytes);
DECLARE_int64(db_min_keys_per_index_block);

namespace yb::docdb {

using dockv::DocKey;
using dockv::DocPath;
using dockv::KeyEntryValue;

namespace {

// Tiny blocks, so a few hundred rows give many data blocks per file and several index levels.
constexpr int64_t kBlockSize = 256;
constexpr int64_t kIndexBlockSize = 64;
constexpr int kMinIndexLevels = 3;
constexpr size_t kValueSize = 40;
// A row with this many wide columns spans several data blocks, so consecutive index entries share
// its DocKey.
constexpr int kNumWideColumns = 40;
constexpr size_t kWideValueSize = 100;
constexpr int kFirstWideColumn = 100;
// Above the number of blocks in any case, so GetSampleBlocks() keeps every block, in walk order.
constexpr size_t kNumBlocksForSample = 100000;

constexpr ColocationId kColocationIds[] = {16384, 20000, 30000};

std::string BlockToString(const std::optional<SampleBlock>& block) {
  if (!block) {
    return "none";
  }
  return Format(
      "[$0, $1)", block->first.AsSlice().ToDebugHexString(),
      block->second.AsSlice().ToDebugHexString());
}

DocKeyHash HashOfRow(int row) {
  return static_cast<DocKeyHash>(row * 100);
}

DocKey HashDocKey(int row) {
  return DocKey(HashOfRow(row), dockv::KeyEntryValues{KeyEntryValue::Int32(row)});
}

DocKey ColocatedDocKey(ColocationId colocation_id, int row) {
  DocKey doc_key(colocation_id);
  doc_key.range_group().push_back(KeyEntryValue::Int32(row));
  return doc_key;
}

KeyBuffer EncodedHashPartitionKey(DocKeyHash hash) {
  PartitionSchemaPB partition_schema;
  partition_schema.set_hash_schema(PartitionSchemaPB::MULTI_COLUMN_HASH_SCHEMA);
  return KeyBuffer(CHECK_RESULT(dockv::PartitionSchema::GetEncodedPartitionKey(
      dockv::PartitionSchema::EncodeMultiColumnHashValue(hash), partition_schema)));
}

Schema HashSchema() {
  return Schema(
      {ColumnSchema("k", DataType::INT32, ColumnKind::HASH), ColumnSchema("v", DataType::STRING)},
      {ColumnId(10), ColumnId(11)});
}

Schema ColocatedSchema(ColocationId colocation_id) {
  return Schema(
      {ColumnSchema("k", DataType::INT32, ColumnKind::RANGE_ASC_NULL_FIRST),
       ColumnSchema("v", DataType::STRING)},
      {ColumnId(10), ColumnId(11)}, TableProperties(), Uuid::Nil(), colocation_id);
}

struct CheckStats {
  uint64_t num_items = 0;
  size_t num_blocks = 0;
  size_t num_dropped = 0;
  // First blocks GetSampleBlocks() keeps although their lower bound is above their upper bound.
  size_t num_inverted = 0;
};

} // namespace

class SampleBlockResolverTest : public DocDBTestBase {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_db_block_size_bytes) = kBlockSize;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_db_index_block_size_bytes) = kIndexBlockSize;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_db_min_keys_per_index_block) = 2;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_db_write_cumulative_index_entry_counts) = true;
    // Every flush must stay its own SST file, so the files overlap.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_rocksdb_disable_compactions) = true;
    DocDBTestBase::SetUp();
  }

  Schema CreateSchema() override {
    return HashSchema();
  }

  void WriteColumns(
      const DocKey& doc_key, int first_column, int num_columns, size_t value_size) {
    const auto encoded_doc_key = doc_key.Encode();
    for (int column = first_column; column < first_column + num_columns; ++column) {
      ASSERT_OK(SetPrimitive(
          DocPath(encoded_doc_key, KeyEntryValue::MakeColumnId(ColumnId(column))),
          QLValue::Primitive(std::string(value_size, 'a' + column % 26)),
          HybridTime::FromMicros(++num_writes_)));
    }
  }

  void Flush() {
    ASSERT_OK(FlushRocksDbAndWait(rocksdb::FlushReason::kTestOnly));
  }

  // Files of hash-partitioned rows, each a window of rows overlapping the next file's. One row in
  // the second file is wide.
  void WriteHashFiles(int num_files) {
    constexpr int kRowsPerFile = 300;
    constexpr int kFileStride = 100;
    constexpr int kWideRow = 250;
    for (int file = 0; file < num_files; ++file) {
      for (int row = file * kFileStride; row < file * kFileStride + kRowsPerFile; ++row) {
        ASSERT_NO_FATALS(WriteColumns(HashDocKey(row), file, 1, kValueSize));
      }
      if (file == 1) {
        ASSERT_NO_FATALS(WriteColumns(
            HashDocKey(kWideRow), kFirstWideColumn, kNumWideColumns, kWideValueSize));
      }
      ASSERT_NO_FATALS(Flush());
    }
  }

  // Files holding rows of every table in kColocationIds, overlapping like WriteHashFiles(). One row
  // of the middle table is wide.
  void WriteColocatedFiles() {
    constexpr int kNumFiles = 3;
    constexpr int kRowsPerFile = 120;
    constexpr int kFileStride = 50;
    constexpr int kWideRow = 100;
    for (int file = 0; file < kNumFiles; ++file) {
      for (auto colocation_id : kColocationIds) {
        for (int row = file * kFileStride; row < file * kFileStride + kRowsPerFile; ++row) {
          ASSERT_NO_FATALS(WriteColumns(
              ColocatedDocKey(colocation_id, row), file, 1, kValueSize));
        }
      }
      if (file == 2) {
        ASSERT_NO_FATALS(WriteColumns(
            ColocatedDocKey(kColocationIds[1], kWideRow), kFirstWideColumn, kNumWideColumns,
            kWideValueSize));
      }
      ASSERT_NO_FATALS(Flush());
    }
  }

  // Returns the number of index levels of each SST file.
  Result<std::vector<uint32_t>> IndexLevels() {
    std::vector<uint32_t> result;
    rocksdb::TablePropertiesCollection props_by_file;
    RETURN_NOT_OK(rocksdb()->GetPropertiesOfAllTables(&props_by_file));
    for (const auto& [file_name, props] : props_by_file) {
      const auto& user_props = props->user_collected_properties;
      const auto it = user_props.find(rocksdb::BlockBasedTablePropertyNames::kNumIndexLevels);
      SCHECK(it != user_props.end(), NotFound, Format("No index level count in $0", file_name));
      result.push_back(DecodeFixed32(pointer_cast<const uint8_t*>(it->second.data())));
    }
    return result;
  }

  void CheckIndexLevels(size_t num_files) {
    const auto levels = ASSERT_RESULT(IndexLevels());
    ASSERT_EQ(levels.size(), num_files);
    for (auto num_levels : levels) {
      ASSERT_GE(num_levels, kMinIndexLevels) << AsString(levels);
    }
  }

  // The user keys of the index entries the walk in GetSampleBlocks() visits, in its order: the same
  // Seek(), step over an entry equal to the start key, and stop at the first DocKey at or past
  // `upper`.
  Result<std::vector<KeyBuffer>> WalkedEntryUserKeys(Slice lower, Slice upper) {
    auto iter = rocksdb()->NewDataBlockAwareIndexIterator(
        rocksdb::ReadOptions(), rocksdb::SkipLastEntry::kTrue);
    const auto start = TableRowsStart(lower);
    iter->Seek(start);
    if (iter->Valid() && iter->key() == start) {
      iter->Next();
    }
    std::vector<KeyBuffer> result;
    for (; iter->Valid(); iter->Next()) {
      if (!upper.empty() && TryExtractDocKey(iter->key()).compare(upper) >= 0) {
        break;
      }
      result.emplace_back(iter->key());
    }
    RETURN_NOT_OK(iter->status());
    return result;
  }

  // Checks every item against the SPLIT_INTERSECTING_BLOCKS_V3 walk in GetSampleBlocks(). The
  // reservoir only holds blocks, so the walk's result per entry is rebuilt from the entries it
  // visits, and the rebuild is checked against the reservoir first. The resolver must match it
  // except for an inverted first block, which it drops. Results are compared as sorted lists,
  // because the resolver numbers items file by file, not in the walk's order.
  void CheckItems(
      const DocReadContext& doc_read_context, const EncodedPartitionBounds& partition_bounds,
      CheckStats* stats_out) {
    CheckStats stats;
    // Same as GetSampleBlocks().
    Slice lower;
    Slice upper;
    if (doc_read_context.schema().is_colocated()) {
      lower = doc_read_context.table_key_prefix();
      upper = doc_read_context.upperbound();
    } else {
      lower = partition_bounds.start_key.AsSlice();
      upper = partition_bounds.end_key.AsSlice();
    }
    SCOPED_TRACE(Format(
        "lower: $0, upper: $1", lower.ToDebugHexString(), upper.ToDebugHexString()));

    QLRocksDBStorage storage("", doc_db(), partition_bounds);
    YQLStorageIf::BlocksSamplingState state{.num_blocks_processed = 0, .num_blocks_collected = 0};
    auto reservoir = ASSERT_RESULT(storage.GetSampleBlocks(
        doc_read_context, DocDbBlocksSamplingMethod::SPLIT_INTERSECTING_BLOCKS_V3,
        kNumBlocksForSample, &state));
    ASSERT_LT(state.num_blocks_collected, kNumBlocksForSample);
    reservoir.resize(state.num_blocks_collected);

    const auto walked = ASSERT_RESULT(WalkedEntryUserKeys(lower, upper));
    const auto make_block = [](Slice block_lower, Slice block_upper) -> std::optional<SampleBlock> {
      if (block_lower == block_upper && !block_lower.empty()) {
        return std::nullopt;
      }
      return SampleBlock(KeyBuffer(block_lower), KeyBuffer(block_upper));
    };
    const auto resolver_block = [&stats](const std::optional<SampleBlock>& block) {
      if (block && !block->second.empty() &&
          block->first.AsSlice().compare(block->second.AsSlice()) > 0) {
        ++stats.num_inverted;
        return std::optional<SampleBlock>();
      }
      return block;
    };
    std::vector<std::string> expected_items;
    std::vector<std::string> expected_blocks;
    for (size_t i = 0; i != walked.size(); ++i) {
      const auto block = make_block(
          i == 0 ? lower : TryExtractDocKey(walked[i - 1].AsSlice()),
          TryExtractDocKey(walked[i].AsSlice()));
      expected_items.push_back(BlockToString(resolver_block(block)));
      if (block) {
        expected_blocks.push_back(BlockToString(block));
      }
    }
    const auto final_block_v3 = make_block(
        walked.empty() ? lower : TryExtractDocKey(walked.back().AsSlice()), upper);
    const auto expected_final_block = resolver_block(final_block_v3);
    if (final_block_v3) {
      expected_blocks.push_back(BlockToString(final_block_v3));
    }
    std::vector<std::string> reservoir_blocks;
    for (const auto& block : reservoir) {
      reservoir_blocks.push_back(BlockToString(block));
    }
    ASSERT_EQ(expected_blocks, reservoir_blocks);

    auto sst_files = ASSERT_RESULT(rocksdb()->NewSstFileIndexIterators(rocksdb::ReadOptions()));
    SampleBlockResolver resolver(sst_files.get(), KeyBounds(lower, upper));
    ASSERT_OK(resolver.Init());
    stats.num_items = resolver.num_items();
    ASSERT_EQ(resolver.num_items(), walked.size() + 1);
    std::vector<std::string> actual_items;
    for (uint64_t position = 0; position < resolver.num_items(); ++position) {
      const auto block = ASSERT_RESULT(resolver.Resolve(position));
      actual_items.push_back(BlockToString(block));
      if (block) {
        ++stats.num_blocks;
      } else {
        ++stats.num_dropped;
      }
    }
    // The last item is the tail block.
    ASSERT_EQ(actual_items.back(), BlockToString(expected_final_block));
    actual_items.pop_back();
    std::sort(expected_items.begin(), expected_items.end());
    std::sort(actual_items.begin(), actual_items.end());
    ASSERT_EQ(expected_items, actual_items);
    ASSERT_EQ(stats.num_blocks + stats.num_inverted, reservoir.size());
    ASSERT_EQ(stats.num_items, stats.num_blocks + stats.num_dropped);
    LOG(INFO) << "lower: " << lower.ToDebugHexString() << ", upper: " << upper.ToDebugHexString()
              << ", items: " << stats.num_items << ", blocks: " << stats.num_blocks
              << ", dropped: " << stats.num_dropped << ", inverted: " << stats.num_inverted;
    if (stats_out) {
      *stats_out = stats;
    }
  }

  Status InitResolver(const rocksdb::ReadOptions& read_options) {
    auto sst_files = VERIFY_RESULT(rocksdb()->NewSstFileIndexIterators(read_options));
    SampleBlockResolver resolver(sst_files.get(), KeyBounds::kNoBounds);
    return resolver.Init();
  }

  void WriteFilesWithoutCounts(int num_files) {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_db_write_cumulative_index_entry_counts) = false;
    ASSERT_OK(ReinitDBOptions(tablet_id()));
    ASSERT_NO_FATALS(WriteHashFiles(num_files));
  }

  uint64_t num_writes_ = 0;
};

TEST_F(SampleBlockResolverTest, HashPartitioned) {
  const auto doc_read_context = DocReadContext::TEST_Create(HashSchema());

  // No files: the only item is the tail block, which covers the whole tablet.
  CheckStats no_files;
  ASSERT_NO_FATALS(CheckItems(doc_read_context, EncodedPartitionBounds{}, &no_files));
  ASSERT_EQ(no_files.num_items, 1);
  ASSERT_EQ(no_files.num_blocks, 1);

  constexpr int kNumFiles = 4;
  ASSERT_NO_FATALS(WriteHashFiles(kNumFiles));
  ASSERT_NO_FATALS(CheckIndexLevels(kNumFiles));

  // Whole tablet. The wide row gives entries with equal DocKeys.
  CheckStats whole;
  ASSERT_NO_FATALS(CheckItems(doc_read_context, EncodedPartitionBounds{}, &whole));
  ASSERT_GT(whole.num_dropped, 0);

  // Split tablets: a middle one, the first and the last.
  const auto lower_key = EncodedHashPartitionKey(HashOfRow(120));
  const auto upper_key = EncodedHashPartitionKey(HashOfRow(380));
  for (const auto& bounds : {
           EncodedPartitionBounds{.start_key = lower_key, .end_key = upper_key},
           EncodedPartitionBounds{.start_key = KeyBuffer(), .end_key = upper_key},
           EncodedPartitionBounds{.start_key = lower_key, .end_key = KeyBuffer()}}) {
    CheckStats stats;
    ASSERT_NO_FATALS(CheckItems(doc_read_context, bounds, &stats));
    ASSERT_GT(stats.num_blocks, 1);
    ASSERT_LT(stats.num_items, whole.num_items);
  }

  // Bounds equal to index keys, which GetSampleBlocks() treats specially at the start, and to the
  // wide row's DocKey, inside which every entry has the same DocKey as the bound.
  const auto walked = ASSERT_RESULT(WalkedEntryUserKeys(Slice(), Slice()));
  ASSERT_GT(walked.size(), 20);
  ASSERT_NO_FATALS(CheckItems(
      doc_read_context, EncodedPartitionBounds{
          .start_key = walked[10], .end_key = walked[walked.size() - 10]},
      /* stats_out = */ nullptr));
  const KeyBuffer wide_row_key(HashDocKey(250).Encode().AsSlice());
  ASSERT_NO_FATALS(CheckItems(
      doc_read_context, EncodedPartitionBounds{.start_key = wide_row_key, .end_key = KeyBuffer()},
      /* stats_out = */ nullptr));
  ASSERT_NO_FATALS(CheckItems(
      doc_read_context, EncodedPartitionBounds{.start_key = KeyBuffer(), .end_key = wide_row_key},
      /* stats_out = */ nullptr));
  // A lower bound inside the wide row: the first index keys above it have the wide row's DocKey,
  // which is below the bound, so the first block is inverted.
  std::vector<KeyBuffer> wide_row_keys;
  for (const auto& key : walked) {
    if (TryExtractDocKey(key.AsSlice()) == wide_row_key.AsSlice() &&
        key.AsSlice() != wide_row_key.AsSlice()) {
      wide_row_keys.push_back(key);
    }
  }
  ASSERT_GT(wide_row_keys.size(), 2);
  CheckStats inside_wide_row;
  ASSERT_NO_FATALS(CheckItems(
      doc_read_context,
      EncodedPartitionBounds{.start_key = wide_row_keys[1], .end_key = KeyBuffer()},
      &inside_wide_row));
  ASSERT_EQ(inside_wide_row.num_inverted, 1);

  // Bounds with no index entry between them.
  const auto empty_lower = EncodedHashPartitionKey(static_cast<DocKeyHash>(HashOfRow(200) + 50));
  const auto empty_upper = EncodedHashPartitionKey(static_cast<DocKeyHash>(HashOfRow(200) + 60));
  CheckStats empty;
  ASSERT_NO_FATALS(CheckItems(
      doc_read_context, EncodedPartitionBounds{.start_key = empty_lower, .end_key = empty_upper},
      &empty));
  ASSERT_EQ(empty.num_items, 1);
}

// The same rows in two files give the same index keys in both, which the resolver orders by file.
TEST_F(SampleBlockResolverTest, IdenticalIndexKeys) {
  constexpr int kNumRows = 300;
  for (int file = 0; file < 2; ++file) {
    for (int row = 0; row < kNumRows; ++row) {
      ASSERT_NO_FATALS(WriteColumns(HashDocKey(row), 0, 1, kValueSize));
    }
    ASSERT_NO_FATALS(Flush());
  }
  ASSERT_NO_FATALS(CheckIndexLevels(2));
  const auto walked = ASSERT_RESULT(WalkedEntryUserKeys(Slice(), Slice()));
  size_t num_identical = 0;
  for (size_t i = 1; i < walked.size(); ++i) {
    num_identical += walked[i] == walked[i - 1];
  }
  ASSERT_GT(num_identical, 0);
  CheckStats stats;
  ASSERT_NO_FATALS(CheckItems(
      DocReadContext::TEST_Create(HashSchema()), EncodedPartitionBounds{}, &stats));
  ASSERT_GE(stats.num_dropped, num_identical);
}

TEST_F(SampleBlockResolverTest, Colocated) {
  ASSERT_NO_FATALS(WriteColocatedFiles());
  ASSERT_NO_FATALS(CheckIndexLevels(3));
  // The first and last tables' bounds reach past the data at one end.
  for (auto colocation_id : kColocationIds) {
    SCOPED_TRACE(Format("colocation_id: $0", colocation_id));
    const auto doc_read_context = DocReadContext::TEST_Create(ColocatedSchema(colocation_id));
    CheckStats stats;
    ASSERT_NO_FATALS(CheckItems(doc_read_context, EncodedPartitionBounds{}, &stats));
    ASSERT_GT(stats.num_blocks, 1);
    if (colocation_id == kColocationIds[1]) {
      ASSERT_GT(stats.num_dropped, 0);
    }
  }
  // The whole tablet, across all tables.
  ASSERT_NO_FATALS(CheckItems(
      DocReadContext::TEST_Create(HashSchema()), EncodedPartitionBounds{},
      /* stats_out = */ nullptr));
}

// Every case the resolver can't handle returns NotSupported, so the caller falls back to the walk.
TEST_F(SampleBlockResolverTest, FallsBackWhenUnsupported) {
  ASSERT_NO_FATALS(WriteHashFiles(2));
  ASSERT_OK(InitResolver(rocksdb::ReadOptions()));

  // The merged index iterator would skip the files a filter rejects.
  class AcceptAllFilter : public rocksdb::ReadFileFilter {
   public:
    bool Filter(const rocksdb::FdWithBoundaries&) const override { return true; }
  };
  rocksdb::ReadOptions filtered_read_options;
  filtered_read_options.file_filter = std::make_shared<AcceptAllFilter>();
  auto status = InitResolver(filtered_read_options);
  ASSERT_TRUE(status.IsNotSupported()) << status;

  // The merged index iterator rejects this read tier too.
  rocksdb::ReadOptions cache_only_read_options;
  cache_only_read_options.read_tier = rocksdb::kBlockCacheTier;
  status = InitResolver(cache_only_read_options);
  ASSERT_TRUE(status.IsNotSupported()) << status;

  // Files without counts, next to files with them. A single-level index is read by position without
  // counts, so the files must have more.
  ASSERT_NO_FATALS(WriteFilesWithoutCounts(2));
  ASSERT_NO_FATALS(CheckIndexLevels(4));
  status = InitResolver(rocksdb::ReadOptions());
  ASSERT_TRUE(status.IsNotSupported()) << status;

  // Index blocks with restart interval above 1. PositionOf() lands in a block with at least two
  // entries (db_min_keys_per_index_block = 2), so Init() returns NotSupported.
  ASSERT_OK(DestroyRocksDB());
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_db_write_cumulative_index_entry_counts) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_index_block_restart_interval) = 2;
  ASSERT_OK(ReinitDBOptions(tablet_id()));
  ASSERT_OK(OpenRocksDB());
  ASSERT_NO_FATALS(WriteHashFiles(2));
  ASSERT_NO_FATALS(CheckIndexLevels(2));
  status = InitResolver(rocksdb::ReadOptions());
  ASSERT_TRUE(status.IsNotSupported()) << status;
}

// A data block count in the table properties that doesn't match the index is Corruption.
TEST_F(SampleBlockResolverTest, NumDataBlocksMismatch) {
  ASSERT_NO_FATALS(WriteHashFiles(2));
  for (int delta : {-1, 1}) {
    SCOPED_TRACE(Format("delta: $0", delta));
    auto sst_files = ASSERT_RESULT(rocksdb()->NewSstFileIndexIterators(rocksdb::ReadOptions()));
    ASSERT_FALSE(sst_files->files.empty());
    sst_files->files[0].num_data_blocks += delta;
    SampleBlockResolver resolver(sst_files.get(), KeyBounds::kNoBounds);
    const auto status = resolver.Init();
    ASSERT_TRUE(status.IsCorruption()) << status;
  }
}

} // namespace yb::docdb
