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

#include "yb/tablet/kv_formatter.h"

#include "yb/docdb/doc_read_context.h"
#include "yb/docdb/docdb_debug.h"

#include "yb/rocksdb/db/dbformat.h"

#include "yb/tablet/tablet_metadata.h"

using namespace std::literals;

namespace yb::tablet {

std::string KVFormatter::Format(
    const Slice& key, const Slice& value, docdb::StorageDbType type) const {
  return docdb::EntryToString(rocksdb::ExtractUserKey(key), value, schema_packing_storage_, type);
}

Status KVFormatter::ProcessArgument(const std::string& argument) {
  const auto kTabletMetadataPrefix = "tablet_metadata="s;
  if (boost::starts_with(argument, kTabletMetadataPrefix)) {
    auto path = argument.substr(kTabletMetadataPrefix.size());
    RaftGroupReplicaSuperBlockPB superblock;
    RETURN_NOT_OK(RaftGroupMetadata::ReadSuperBlockFromDisk(
        Env::Default(), path, &superblock));
    auto& table_info = superblock.kv_store().tables(0);
    docdb::DocReadContext doc_read_context("sst_dump: ", table_info.table_type());
    RETURN_NOT_OK(doc_read_context.LoadFromPB(table_info));
    schema_packing_storage_ = doc_read_context.schema_packing_storage;
    return Status::OK();
  }

  return STATUS_FORMAT(InvalidArgument, "Unknown formatter argument: $0", argument);
}

} // namespace yb::tablet
