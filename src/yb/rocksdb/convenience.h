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

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <memory>
#include <stack>
#include <string>
#include <unordered_map>
#include <vector>

#include "yb/rocksdb/options.h"
#include "yb/rocksdb/table.h"
#include "yb/rocksdb/types.h"
#include "yb/rocksdb/write_batch_base.h"

namespace rocksdb {

// Take a map of option name and option value, apply them into the
// base_options, and return the new options as a result.
//
// If input_strings_escaped is set to true, then each escaped characters
// prefixed by '\' in the the values of the opts_map will be further
// converted back to the raw string before assigning to the associated
// options.
Status GetColumnFamilyOptionsFromMap(
    const ColumnFamilyOptions& base_options,
    const std::unordered_map<std::string, std::string>& opts_map,
    ColumnFamilyOptions* new_options, bool input_strings_escaped = false);

// Take a map of option name and option value, apply them into the
// base_options, and return the new options as a result.
//
// If input_strings_escaped is set to true, then each escaped characters
// prefixed by '\' in the the values of the opts_map will be further
// converted back to the raw string before assigning to the associated
// options.
Status GetDBOptionsFromMap(
    const DBOptions& base_options,
    const std::unordered_map<std::string, std::string>& opts_map,
    DBOptions* new_options, bool input_strings_escaped = false);

Status GetBlockBasedTableOptionsFromMap(
    const BlockBasedTableOptions& table_options,
    const std::unordered_map<std::string, std::string>& opts_map,
    BlockBasedTableOptions* new_table_options,
    bool input_strings_escaped = false);

Status GetPlainTableOptionsFromMap(
    const PlainTableOptions& table_options,
    const std::unordered_map<std::string, std::string>& opts_map,
    PlainTableOptions* new_table_options,
    bool input_strings_escaped = false);

// Take a string representation of option names and  values, apply them into the
// base_options, and return the new options as a result. The string has the
// following format:
//   "write_buffer_size=1024;max_write_buffer_number=2"
// Nested options config is also possible. For example, you can define
// BlockBasedTableOptions as part of the string for block-based table factory:
//   "write_buffer_size=1024;block_based_table_factory={block_size=4k};"
//   "max_write_buffer_num=2"
Status GetColumnFamilyOptionsFromString(
    const ColumnFamilyOptions& base_options,
    const std::string& opts_str,
    ColumnFamilyOptions* new_options);

Status GetDBOptionsFromString(
    const DBOptions& base_options,
    const std::string& opts_str,
    DBOptions* new_options);

Status GetStringFromDBOptions(std::string* opts_str,
                              const DBOptions& db_options,
                              const std::string& delimiter = ";  ");

Status GetStringFromColumnFamilyOptions(std::string* opts_str,
                                        const ColumnFamilyOptions& db_options,
                                        const std::string& delimiter = ";  ");

Status GetBlockBasedTableOptionsFromString(
    const BlockBasedTableOptions& table_options,
    const std::string& opts_str,
    BlockBasedTableOptions* new_table_options);

Status GetPlainTableOptionsFromString(
    const PlainTableOptions& table_options,
    const std::string& opts_str,
    PlainTableOptions* new_table_options);

Status GetMemTableRepFactoryFromString(
    const std::string& opts_str,
    std::unique_ptr<MemTableRepFactory>* new_mem_factory);

Status GetOptionsFromString(const Options& base_options,
                            const std::string& opts_str, Options* new_options);

// Request stopping background work, if wait is true wait until it's done
void CancelAllBackgroundWork(DB* db, bool wait = false);

// Delete files which are entirely in the given range
// Could leave some keys in the range which are in files which are not
// entirely in the range.
// Snapshots before the delete might not see the data in the given range.
Status DeleteFilesInRange(DB* db, ColumnFamilyHandle* column_family,
                          const Slice* begin, const Slice* end);

}  // namespace rocksdb
