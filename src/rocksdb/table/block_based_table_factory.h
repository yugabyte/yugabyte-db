//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef ROCKSDB_TABLE_BLOCK_BASED_TABLE_FACTORY_H
#define ROCKSDB_TABLE_BLOCK_BASED_TABLE_FACTORY_H

#include <stdint.h>

#include <memory>
#include <string>

#include "rocksdb/flush_block_policy.h"
#include "rocksdb/table.h"
#include "db/dbformat.h"

namespace rocksdb {

struct EnvOptions;

using std::unique_ptr;
class BlockBasedTableBuilder;

class BlockBasedTableFactory : public TableFactory {
 public:
  explicit BlockBasedTableFactory(
      const BlockBasedTableOptions& table_options = BlockBasedTableOptions());

  ~BlockBasedTableFactory() {}

  const char* Name() const override { return "BlockBasedTable"; }

  Status NewTableReader(const TableReaderOptions& table_reader_options,
                        unique_ptr<RandomAccessFileReader>&& file,
                        uint64_t file_size,
                        unique_ptr<TableReader>* table_reader) const override;

  // This is a variant of virtual member function NewTableReader function with
  // added capability to disable pre-fetching of blocks on BlockBasedTable::Open
  Status NewTableReader(const TableReaderOptions& table_reader_options,
                        unique_ptr<RandomAccessFileReader>&& file,
                        uint64_t file_size,
                        unique_ptr<TableReader>* table_reader,
                        bool prefetch_index_and_filter) const;

  bool IsSplitSstForWriteSupported() const override { return true; }

  // base_file should be not nullptr, data_file should either point to different file writer
  // or be nullptr in order to produce single SST file containing both data and metadata.
  TableBuilder* NewTableBuilder(
      const TableBuilderOptions& table_builder_options,
      uint32_t column_family_id, WritableFileWriter* base_file,
      WritableFileWriter* data_file = nullptr) const override;

  // Sanitizes the specified DB Options.
  Status SanitizeOptions(const DBOptions& db_opts,
                         const ColumnFamilyOptions& cf_opts) const override;

  std::string GetPrintableTableOptions() const override;

  const BlockBasedTableOptions& table_options() const;

  void* GetOptions() override { return &table_options_; }

 private:
  BlockBasedTableOptions table_options_;
};

extern const char kHashIndexPrefixesBlock[];
extern const char kHashIndexPrefixesMetadataBlock[];
extern const char kPropTrue[];
extern const char kPropFalse[];

}  // namespace rocksdb

#endif  // ROCKSDB_TABLE_BLOCK_BASED_TABLE_FACTORY_H
