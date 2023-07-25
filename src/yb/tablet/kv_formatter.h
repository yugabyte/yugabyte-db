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

#include "yb/rocksdb/sst_dump_tool.h"

#include "yb/tablet/tablet_metadata.h"

namespace yb::tablet {

class SchemaPackingProviderFromSuperblock : public docdb::SchemaPackingProvider {
 public:
  explicit SchemaPackingProviderFromSuperblock(RaftGroupReplicaSuperBlockPB superblock);

  // After construction, must successfully call Init() before any other methods.
  Status Init();

  Result<docdb::CompactionSchemaInfo> CotablePacking(
      const Uuid& cotable_id, uint32_t schema_version, HybridTime history_cutoff) override;
  Result<docdb::CompactionSchemaInfo> ColocationPacking(
      ColocationId colocation_id, uint32_t schema_version, HybridTime history_cutoff) override;

 private:
  RaftGroupReplicaSuperBlockPB superblock_;
  std::string primary_table_id_;
  KvStoreInfo kv_store_;

  Result<TableInfoPtr> GetTableInfo(const TableId& table_id) const;
  Result<TableInfoPtr> GetTableInfo(ColocationId colocation_id) const;
};

class KVFormatter : public rocksdb::DocDBKVFormatter {
 public:
  std::string Format(
      const Slice& key, const Slice& value, docdb::StorageDbType type) const override;

  Status ProcessArgument(const std::string& argument) override;

 private:
  std::unique_ptr<SchemaPackingProviderFromSuperblock> schema_packing_provider_;
};

}  // namespace yb::tablet
