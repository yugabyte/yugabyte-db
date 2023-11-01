//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
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

#ifndef YB_ROCKSDB_DB_BUILDER_H
#define YB_ROCKSDB_DB_BUILDER_H

#include <string>
#include <utility>
#include <vector>

#include "yb/rocksdb/db/table_properties_collector.h"
#include "yb/rocksdb/env.h"
#include "yb/rocksdb/immutable_options.h"
#include "yb/rocksdb/options.h"
#include "yb/rocksdb/status.h"
#include "yb/rocksdb/table_properties.h"
#include "yb/rocksdb/types.h"

namespace rocksdb {

struct Options;
struct FileMetaData;

class Env;
struct EnvOptions;
class Iterator;
class TableCache;
class VersionEdit;
class TableBuilder;
class WritableFileWriter;
class InternalStats;
class InternalIterator;

std::unique_ptr<TableBuilder> NewTableBuilder(
    const ImmutableCFOptions& options,
    const InternalKeyComparatorPtr& internal_comparator,
    const IntTblPropCollectorFactories& int_tbl_prop_collector_factories,
    uint32_t column_family_id,
    WritableFileWriter* file,
    const CompressionType compression_type,
    const CompressionOptions& compression_opts,
    const bool skip_filters = false);

std::unique_ptr<TableBuilder> NewTableBuilder(
    const ImmutableCFOptions& options,
    const InternalKeyComparatorPtr& internal_comparator,
    const IntTblPropCollectorFactories& int_tbl_prop_collector_factories,
    uint32_t column_family_id,
    WritableFileWriter* metadata_file,
    WritableFileWriter* data_file,
    const CompressionType compression_type,
    const CompressionOptions& compression_opts,
    const bool skip_filters = false);

// Build a Table file from the contents of *iter.  The generated file
// will be named according to number specified in meta. On success, the rest of
// *meta will be filled with metadata about the generated table.
// If no data is present in *iter, meta->total_file_size will be set to
// zero, and no Table file will be produced.
extern Status BuildTable(
    const std::string& dbname,
    const DBOptions& db_options,
    const ImmutableCFOptions& options,
    const EnvOptions& env_options,
    TableCache* table_cache,
    InternalIterator* iter,
    FileMetaData* meta,
    const InternalKeyComparatorPtr& internal_comparator,
    const IntTblPropCollectorFactories& int_tbl_prop_collector_factories,
    uint32_t column_family_id,
    std::vector<SequenceNumber> snapshots,
    SequenceNumber earliest_write_conflict_snapshot,
    const CompressionType compression,
    const CompressionOptions& compression_opts,
    bool paranoid_file_checks,
    InternalStats* internal_stats,
    const yb::IOPriority io_priority = yb::IOPriority::kHigh,
    TableProperties* table_properties = nullptr);

// Check SST file to end with FLAGS_rocksdb_check_sst_file_tail_for_zeros zeros and log+return error
// if this is the case.
Status CheckSstTailForZeros(
    const DBOptions& db_options, const EnvOptions& env_options, const std::string& file_path,
    size_t check_size);

}  // namespace rocksdb

#endif  // YB_ROCKSDB_DB_BUILDER_H
