// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.
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

#include "yb/rocksdb/table/plain_table_factory.h"

#include <stdint.h>
#include <memory>

#include "yb/rocksdb/db/dbformat.h"
#include "yb/rocksdb/table/plain_table_builder.h"
#include "yb/rocksdb/table/plain_table_reader.h"
#include "yb/rocksdb/port/port.h"

using std::unique_ptr;

namespace rocksdb {

Status PlainTableFactory::NewTableReader(
    const TableReaderOptions& table_reader_options,
    unique_ptr<RandomAccessFileReader>&& file, uint64_t file_size,
    unique_ptr<TableReader>* table) const {
  return PlainTableReader::Open(
      table_reader_options.ioptions, table_reader_options.env_options,
      table_reader_options.internal_comparator, std::move(file), file_size,
      table, table_options_.bloom_bits_per_key, table_options_.hash_table_ratio,
      table_options_.index_sparseness, table_options_.huge_page_tlb_size,
      table_options_.full_scan_mode);
}

std::unique_ptr<TableBuilder> PlainTableFactory::NewTableBuilder(
    const TableBuilderOptions &table_builder_options,
    uint32_t column_family_id, WritableFileWriter *base_file, WritableFileWriter *data_file) const {
  // This table factory doesn't support separate files for metadata and data.
  assert(data_file == nullptr);
  // Ignore the skip_filters flag. PlainTable format is optimized for small
  // in-memory dbs. The skip_filters optimization is not useful for plain
  // tables
  //
  return std::make_unique<PlainTableBuilder>(
      table_builder_options.ioptions,
      *table_builder_options.int_tbl_prop_collector_factories,
      column_family_id,
      base_file,
      table_options_.user_key_len,
      table_options_.encoding_type,
      table_options_.index_sparseness,
      table_options_.bloom_bits_per_key,
      6,
      table_options_.huge_page_tlb_size,
      table_options_.hash_table_ratio,
      table_options_.store_index_in_file);
}

std::string PlainTableFactory::GetPrintableTableOptions() const {
  std::string ret;
  ret.reserve(20000);
  const int kBufferSize = 200;
  char buffer[kBufferSize];

  snprintf(buffer, kBufferSize, "  user_key_len: %u\n",
           table_options_.user_key_len);
  ret.append(buffer);
  snprintf(buffer, kBufferSize, "  bloom_bits_per_key: %d\n",
           table_options_.bloom_bits_per_key);
  ret.append(buffer);
  snprintf(buffer, kBufferSize, "  hash_table_ratio: %lf\n",
           table_options_.hash_table_ratio);
  ret.append(buffer);
  snprintf(buffer, kBufferSize, "  index_sparseness: %" ROCKSDB_PRIszt "\n",
           table_options_.index_sparseness);
  ret.append(buffer);
  snprintf(buffer, kBufferSize, "  huge_page_tlb_size: %" ROCKSDB_PRIszt "\n",
           table_options_.huge_page_tlb_size);
  ret.append(buffer);
  snprintf(buffer, kBufferSize, "  encoding_type: %d\n",
           table_options_.encoding_type);
  ret.append(buffer);
  snprintf(buffer, kBufferSize, "  full_scan_mode: %d\n",
           table_options_.full_scan_mode);
  ret.append(buffer);
  snprintf(buffer, kBufferSize, "  store_index_in_file: %d\n",
           table_options_.store_index_in_file);
  ret.append(buffer);
  return ret;
}

const PlainTableOptions& PlainTableFactory::table_options() const {
  return table_options_;
}

extern TableFactory* NewPlainTableFactory(const PlainTableOptions& options) {
  return new PlainTableFactory(options);
}

const char PlainTablePropertyNames::kPrefixExtractorName[] =
    "rocksdb.prefix.extractor.name";

const char PlainTablePropertyNames::kEncodingType[] =
    "rocksdb.plain.table.encoding.type";

const char PlainTablePropertyNames::kBloomVersion[] =
    "rocksdb.plain.table.bloom.version";

const char PlainTablePropertyNames::kNumBloomBlocks[] =
    "rocksdb.plain.table.bloom.numblocks";

}  // namespace rocksdb
