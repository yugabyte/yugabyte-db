// Copyright (c) YugaByte, Inc.
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

#include <memory>
#include <ostream>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "yb/docdb/docdb_fwd.h"
#include "yb/docdb/docdb_types.h"

#include "yb/rocksdb/iterator.h"
#include "yb/rocksdb/listener.h"

namespace yb {
namespace docdb {

std::string EntryToString(
    const rocksdb::Iterator& iterator, const SchemaPackingStorage& schema_packing_storage,
    StorageDbType db_type = StorageDbType::kRegular);
std::string EntryToString(
    const Slice& key, const Slice& value, const SchemaPackingStorage& schema_packing_storage,
    StorageDbType db_type = StorageDbType::kRegular);

// Create a debug dump of the document database. Tries to decode all keys/values despite failures.
// Reports all errors to the output stream and returns the status of the first failed operation,
// if any.
void DocDBDebugDump(
    rocksdb::DB* rocksdb,
    std::ostream& out,
    const SchemaPackingStorage& schema_packing_storage,
    StorageDbType db_type,
    IncludeBinary include_binary = IncludeBinary::kFalse);

std::string DocDBDebugDumpToStr(
    rocksdb::DB* rocksdb, const SchemaPackingStorage& schema_packing_storage,
    StorageDbType db_type = StorageDbType::kRegular,
    IncludeBinary include_binary = IncludeBinary::kFalse);

std::string DocDBDebugDumpToStr(
    DocDB docdb, const SchemaPackingStorage& schema_packing_storage,
    IncludeBinary include_binary = IncludeBinary::kFalse);

void DocDBDebugDumpToContainer(
    DocDB docdb, const SchemaPackingStorage& schema_packing_storage,
    std::unordered_set<std::string>* out);

void DumpRocksDBToLog(
    rocksdb::DB* rocksdb, const SchemaPackingStorage& schema_packing_storage,
    StorageDbType db_type = StorageDbType::kRegular, const std::string& log_prefix = std::string());

}  // namespace docdb
}  // namespace yb
