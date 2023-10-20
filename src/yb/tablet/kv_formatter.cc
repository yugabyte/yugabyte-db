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

using namespace std::literals;

namespace yb::tablet {

SchemaPackingProviderFromSuperblock::SchemaPackingProviderFromSuperblock(
    RaftGroupReplicaSuperBlockPB superblock)
    : superblock_(superblock), kv_store_(KvStoreId(superblock.kv_store().kv_store_id())) {}

Status SchemaPackingProviderFromSuperblock::Init() {
  primary_table_id_ = superblock_.primary_table_id();
  RETURN_NOT_OK(kv_store_.LoadFromPB(
      "fake_log_prefix", superblock_.kv_store(), primary_table_id_, true /*local_superblock*/));

  for (const auto& [id, info] : kv_store_.tables) {
    LOG(INFO) << "Found info for table ID " << id << " (namespace " << info->namespace_name
              << ", table_type " << TableType_Name(info->table_type) << ", name "
              << info->table_name << ", cotable_id " << ToString(info->cotable_id)
              << ", colocation_id " << info->schema().colocation_id() << ") in superblock";
  }
  return Status::OK();
}

Result<docdb::CompactionSchemaInfo> SchemaPackingProviderFromSuperblock::CotablePacking(
    const Uuid& cotable_id, uint32_t schema_version, HybridTime history_cutoff) {
  auto table_info =
      VERIFY_RESULT(GetTableInfo(cotable_id.IsNil() ? "" : cotable_id.ToHexString()));
  return TableInfo::Packing(table_info, schema_version, history_cutoff);
}

Result<docdb::CompactionSchemaInfo> SchemaPackingProviderFromSuperblock::ColocationPacking(
    ColocationId colocation_id, uint32_t schema_version, HybridTime history_cutoff) {
  return TableInfo::Packing(
      VERIFY_RESULT(GetTableInfo(colocation_id)), schema_version, history_cutoff);
}

Result<TableInfoPtr> SchemaPackingProviderFromSuperblock::GetTableInfo(
    const TableId& table_id) const {
  const auto& id = table_id.empty() ? primary_table_id_ : table_id;
  // Should be thread safe as kv_store_ is setup only once in this Init().
  const auto& tables = kv_store_.tables;
  const auto iter = tables.find(id);
  if (iter == tables.end()) {
    return STATUS_FORMAT(NotFound, "[Co]table id $0 not found in superblock information", id);
  }
  return iter->second;
}

Result<TableInfoPtr> SchemaPackingProviderFromSuperblock::GetTableInfo(
    ColocationId colocation_id) const {
  if (colocation_id == kColocationIdNotSet) {
    return GetTableInfo(primary_table_id_);
  }

  const auto& colocation_to_table = kv_store_.colocation_to_table;
  const auto iter = colocation_to_table.find(colocation_id);
  if (iter == colocation_to_table.end()) {
    return STATUS_FORMAT(
        NotFound, "Colocation id $0 not found in superblock information", colocation_id);
  }
  return iter->second;
}

std::string KVFormatter::Format(
    const Slice& key, const Slice& value, docdb::StorageDbType type) const {
  return docdb::EntryToString(
      rocksdb::ExtractUserKey(key), value, schema_packing_provider_.get(), type);
}

Status KVFormatter::ProcessArgument(const std::string& argument) {
  const auto kTabletMetadataPrefix = "tablet_metadata="s;
  if (boost::starts_with(argument, kTabletMetadataPrefix)) {
    auto path = argument.substr(kTabletMetadataPrefix.size());
    RaftGroupReplicaSuperBlockPB superblock;
    RETURN_NOT_OK(RaftGroupMetadata::ReadSuperBlockFromDisk(Env::Default(), path, &superblock));

    schema_packing_provider_ = std::make_unique<SchemaPackingProviderFromSuperblock>(superblock);
    return schema_packing_provider_->Init();
  }

  return STATUS_FORMAT(InvalidArgument, "Unknown formatter argument: $0", argument);
}

}  // namespace yb::tablet
