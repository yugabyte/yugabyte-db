// Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant
// of patent rights can be found in the PATENTS file in the same directory.
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


#pragma once

#include "yb/rocksdb/sst_dump_tool.h"

#include <memory>
#include <string>
#include "yb/rocksdb/rocksdb_fwd.h"
#include "yb/rocksdb/db/dbformat.h"
#include "yb/rocksdb/immutable_options.h"
#include "yb/rocksdb/util/file_reader_writer.h"

namespace rocksdb {

class SstFileReader {
 public:
  SstFileReader(
      const std::string& file_name, bool verify_checksum, OutputFormat format,
      const DocDBKVFormatter* docdb_formatter = nullptr);
  ~SstFileReader();

  Status ReadSequential(bool print_kv, uint64_t read_num, bool has_from,
                        const std::string& from_key, bool has_to,
                        const std::string& to_key);

  Status ReadTableProperties(
      std::shared_ptr<const TableProperties>* table_properties);
  uint64_t GetReadNumber() { return read_num_; }
  TableProperties* GetInitTableProperties() { return table_properties_.get(); }

  Status DumpTable(const std::string& out_filename);
  Status getStatus() { return init_result_; }

  int ShowAllCompressionSizes(size_t block_size);

 private:
  // Get the TableReader implementation for the sst file
  Status GetTableReader(const std::string& file_path);
  Status ReadTableProperties(uint64_t table_magic_number,
                             RandomAccessFileReader* file, uint64_t file_size);

  uint64_t CalculateCompressedTableSize(const TableBuilderOptions& tb_options,
                                        size_t block_size);

  Status SetTableOptionsByMagicNumber(uint64_t table_magic_number);
  Status SetOldTableOptions();

  // Helper function to call the factory with settings specific to the
  // factory implementation
  Status NewTableReader(const ImmutableCFOptions& ioptions,
                        const EnvOptions& soptions,
                        const InternalKeyComparator& internal_comparator,
                        uint64_t file_size,
                        std::unique_ptr<TableReader>* table_reader);

  std::string file_name_;
  uint64_t read_num_;
  bool verify_checksum_;
  OutputFormat output_format_ = OutputFormat::kRaw;
  const DocDBKVFormatter* docdb_kv_formatter_;
  EnvOptions soptions_;

  Status init_result_;
  std::unique_ptr<TableReader> table_reader_;
  std::unique_ptr<RandomAccessFileReader> file_;
  // options_ and internal_comparator_ will also be used in
  // ReadSequential internally (specifically, seek-related operations)
  Options options_;
  const ImmutableCFOptions ioptions_;
  std::shared_ptr<InternalKeyComparator> internal_comparator_;
  std::unique_ptr<TableProperties> table_properties_;
};

}  // namespace rocksdb
