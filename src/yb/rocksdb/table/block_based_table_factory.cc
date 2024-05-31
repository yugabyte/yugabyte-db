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
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "yb/rocksdb/table/block_based_table_factory.h"

#include <stdint.h>

#include <memory>
#include <string>

#include "yb/rocksdb/cache.h"
#include "yb/rocksdb/filter_policy.h"
#include "yb/rocksdb/flush_block_policy.h"
#include "yb/rocksdb/port/port.h"
#include "yb/rocksdb/table/block_based_table_builder.h"
#include "yb/rocksdb/table/block_based_table_reader.h"
#include "yb/rocksdb/table/format.h"

#include "yb/util/logging.h"

using std::unique_ptr;

namespace rocksdb {

BlockBasedTableFactory::BlockBasedTableFactory(
    const BlockBasedTableOptions& _table_options)
    : table_options_(_table_options) {
  if (table_options_.flush_block_policy_factory == nullptr) {
    table_options_.flush_block_policy_factory.reset(
        new FlushBlockBySizePolicyFactory());
  }
  if (table_options_.no_block_cache) {
    table_options_.block_cache.reset();
  } else if (table_options_.block_cache == nullptr) {
    table_options_.block_cache = NewLRUCache(8 << 20);
  }
  if (table_options_.block_size_deviation < 0 ||
      table_options_.block_size_deviation > 100) {
    table_options_.block_size_deviation = 0;
  }
  if (table_options_.block_restart_interval < 1) {
    table_options_.block_restart_interval = 1;
  }
  if (table_options_.index_block_restart_interval < 1) {
    table_options_.index_block_restart_interval = 1;
  }
}

Status BlockBasedTableFactory::NewTableReader(
    const TableReaderOptions& table_reader_options,
    unique_ptr<RandomAccessFileReader>&& file, uint64_t file_size,
    unique_ptr<TableReader>* table_reader) const {
  return NewTableReader(table_reader_options, std::move(file), file_size,
                        table_reader, DataIndexLoadMode::LAZY, PrefetchFilter::YES);
}

Status BlockBasedTableFactory::NewTableReader(
    const TableReaderOptions& table_reader_options,
    unique_ptr<RandomAccessFileReader>&& base_file, uint64_t base_file_size,
    unique_ptr<TableReader>* table_reader, DataIndexLoadMode prefetch_data_index,
    PrefetchFilter prefetch_filter) const {
  return BlockBasedTable::Open(
      table_reader_options.ioptions, table_reader_options.env_options,
      table_options_, table_reader_options.internal_comparator, std::move(base_file),
      base_file_size, table_reader, prefetch_data_index, prefetch_filter,
      table_reader_options.skip_filters);
}

std::unique_ptr<TableBuilder> BlockBasedTableFactory::NewTableBuilder(
    const TableBuilderOptions& table_builder_options, uint32_t column_family_id,
    WritableFileWriter* base_file, WritableFileWriter* data_file) const {
  // base_file should be not nullptr, data_file should either point to different file writer
  // or be nullptr in order to produce single SST file containing both data and metadata.
  DCHECK_ONLY_NOTNULL(base_file);
  DCHECK_NE(base_file, data_file);
  return std::make_unique<BlockBasedTableBuilder>(
      table_builder_options.ioptions, table_options_,
      table_builder_options.internal_comparator,
      *table_builder_options.int_tbl_prop_collector_factories,
      column_family_id,
      base_file,
      data_file,
      table_builder_options.compression_type,
      table_builder_options.compression_opts,
      table_builder_options.skip_filters);
}

Status BlockBasedTableFactory::SanitizeOptions(
    const DBOptions& db_opts,
    const ColumnFamilyOptions& cf_opts) const {
  if (table_options_.index_type == IndexType::kHashSearch &&
      cf_opts.prefix_extractor == nullptr) {
    return STATUS(InvalidArgument, "Hash index is specified for block-based "
        "table, but prefix_extractor is not given");
  }
  if (table_options_.cache_index_and_filter_blocks &&
      table_options_.no_block_cache) {
    return STATUS(InvalidArgument, "Enable cache_index_and_filter_blocks, "
        ", but block cache is disabled");
  }
  if (!BlockBasedTableSupportedVersion(table_options_.format_version)) {
    return STATUS(InvalidArgument,
        "Unsupported BlockBasedTable format_version. Please check "
        "include/rocksdb/table.h for more info");
  }
  return Status::OK();
}

std::string BlockBasedTableFactory::GetPrintableTableOptions() const {
  std::string ret;
  ret.reserve(20000);
  const int kBufferSize = 200;
  char buffer[kBufferSize];

  snprintf(buffer, kBufferSize, "  flush_block_policy_factory: %s (%p)\n",
           table_options_.flush_block_policy_factory->Name(),
           table_options_.flush_block_policy_factory.get());
  ret.append(buffer);
  snprintf(buffer, kBufferSize, "  cache_index_and_filter_blocks: %d\n",
           table_options_.cache_index_and_filter_blocks);
  ret.append(buffer);
  snprintf(buffer, kBufferSize, "  index_type: %d\n",
           yb::to_underlying(table_options_.index_type));
  ret.append(buffer);
  snprintf(buffer, kBufferSize, "  hash_index_allow_collision: %d\n",
           table_options_.hash_index_allow_collision);
  ret.append(buffer);
  snprintf(buffer, kBufferSize, "  checksum: %d\n",
           table_options_.checksum);
  ret.append(buffer);
  snprintf(buffer, kBufferSize, "  no_block_cache: %d\n",
           table_options_.no_block_cache);
  ret.append(buffer);
  snprintf(buffer, kBufferSize, "  block_cache: %p\n",
           table_options_.block_cache.get());
  ret.append(buffer);
  if (table_options_.block_cache) {
    snprintf(buffer, kBufferSize, "  block_cache_size: %" ROCKSDB_PRIszt "\n",
             table_options_.block_cache->GetCapacity());
    ret.append(buffer);
  }
  snprintf(buffer, kBufferSize, "  block_cache_compressed: %p\n",
           table_options_.block_cache_compressed.get());
  ret.append(buffer);
  if (table_options_.block_cache_compressed) {
    snprintf(buffer, kBufferSize,
             "  block_cache_compressed_size: %" ROCKSDB_PRIszt "\n",
             table_options_.block_cache_compressed->GetCapacity());
    ret.append(buffer);
  }
  snprintf(buffer, kBufferSize, "  block_size: %" ROCKSDB_PRIszt "\n",
           table_options_.block_size);
  ret.append(buffer);
  snprintf(buffer, kBufferSize, "  block_size_deviation: %d\n",
           table_options_.block_size_deviation);
  ret.append(buffer);
  snprintf(buffer, kBufferSize, "  block_restart_interval: %d\n",
           table_options_.block_restart_interval);
  ret.append(buffer);
  snprintf(buffer, kBufferSize, "  index_block_restart_interval: %d\n",
           table_options_.index_block_restart_interval);
  ret.append(buffer);
  snprintf(buffer, kBufferSize, "  filter_policy: %s\n",
           table_options_.filter_policy == nullptr ?
             "nullptr" : table_options_.filter_policy->Name());
  ret.append(buffer);
  snprintf(buffer, kBufferSize, "  whole_key_filtering: %d\n",
           table_options_.whole_key_filtering);
  ret.append(buffer);
  snprintf(buffer, kBufferSize, "  skip_table_builder_flush: %d\n",
           table_options_.skip_table_builder_flush);
  ret.append(buffer);
  snprintf(buffer, kBufferSize, "  format_version: %d\n",
           table_options_.format_version);
  ret.append(buffer);
  return ret;
}

const BlockBasedTableOptions& BlockBasedTableFactory::table_options() const {
  return table_options_;
}

TableFactory* NewBlockBasedTableFactory(
    const BlockBasedTableOptions& _table_options) {
  return new BlockBasedTableFactory(_table_options);
}

const char BlockBasedTablePropertyNames::kIndexType[] =
    "rocksdb.block.based.table.index.type";
const char BlockBasedTablePropertyNames::kNumIndexLevels[] =
    "rocksdb.block.based.table.index.num.levels";
const char BlockBasedTablePropertyNames::kWholeKeyFiltering[] =
    "rocksdb.block.based.table.whole.key.filtering";
const char BlockBasedTablePropertyNames::kPrefixFiltering[] =
    "rocksdb.block.based.table.prefix.filtering";
const char BlockBasedTablePropertyNames::kDataBlockKeyValueEncodingFormat[] =
    "rocksdb.block.based.table.data.block.key.value.encoding.format";
const char kHashIndexPrefixesBlock[] = "rocksdb.hashindex.prefixes";
const char kHashIndexPrefixesMetadataBlock[] =
    "rocksdb.hashindex.metadata";
const char kPropTrue[] = "1";
const char kPropFalse[] = "0";

}  // namespace rocksdb
