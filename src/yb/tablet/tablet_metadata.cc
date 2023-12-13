// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
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

#include "yb/tablet/tablet_metadata.h"

#include <algorithm>
#include <mutex>
#include <string>

#include <boost/optional.hpp>

#include "yb/ash/wait_state.h"

#include "yb/common/colocated_util.h"
#include "yb/common/entity_ids.h"
#include "yb/common/schema.h"
#include "yb/common/transaction.h"
#include "yb/common/wire_protocol.h"

#include "yb/consensus/consensus_util.h"
#include "yb/consensus/opid_util.h"

#include "yb/docdb/doc_read_context.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/docdb/pgsql_operation.h"

#include "yb/dockv/reader_projection.h"

#include "yb/gutil/atomicops.h"
#include "yb/gutil/dynamic_annotations.h"
#include "yb/gutil/map-util.h"
#include "yb/gutil/stl_util.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/master/sys_catalog_constants.h"

#include "yb/qlexpr/index.h"

#include "yb/rocksdb/db.h"
#include "yb/rocksdb/options.h"

#include "yb/rpc/lightweight_message.h"

#include "yb/tablet/metadata.pb.h"
#include "yb/tablet/tablet_options.h"

#include "yb/util/debug/trace_event.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/pb_util.h"
#include "yb/util/random.h"
#include "yb/util/result.h"
#include "yb/util/status.h"
#include "yb/util/status_log.h"
#include "yb/util/trace.h"

DEPRECATE_FLAG(bool, enable_tablet_orphaned_block_deletion, "10_2022");

DEFINE_test_flag(bool, invalidate_last_change_metadata_op, false,
    "Used in tests to update last_flushed_change_metadata_op_id to -1.-1 to simulate "
    "behavior of old code");

DEFINE_test_flag(bool, skip_metadata_backfill_done, false,
    "Used in tests to skip triggering of RaftGroupMetadata::OnBackfillDone().");

// Only used for colocated table creation currently.
// The flag is non-runtime so that if it is changed from true to false, the node restarts and the
// unflushed committed CHANGE_METADATA_OP WAL entries are applied and flushed during the tablet
// bootstrap.
DEFINE_NON_RUNTIME_bool(lazily_flush_superblock, true,
    "Flushes the superblock lazily on metadata update. Only used for colocated table creation "
    "currently.");

using std::string;

using strings::Substitute;

namespace yb::tablet {

using dockv::Partition;
using util::DereferencedEqual;
using util::MapsEqual;
using qlexpr::IndexInfo;
using qlexpr::IndexMap;

namespace {

Result<Uuid> ParseCotableId(Primary primary, const TableId& table_id) {
  return primary ? Uuid::Nil() : Uuid::FromHexString(table_id);
}

std::string MakeTableInfoLogPrefix(
    const std::string& tablet_log_prefix, Primary primary, const TableId& table_id) {
  return primary ? tablet_log_prefix : Format("TBL $0 $1", table_id, tablet_log_prefix);
}

} // namespace

const int64 kNoDurableMemStore = -1;
const std::string kIntentsSubdir = "intents";
const std::string kIntentsDBSuffix = ".intents";
const std::string kSnapshotsDirSuffix = ".snapshots";

// ============================================================================
//  Raft group metadata
// ============================================================================

TableInfo::TableInfo(const std::string& log_prefix_, TableType table_type, PrivateTag)
    : log_prefix(log_prefix_),
      doc_read_context(new docdb::DocReadContext(log_prefix, table_type, docdb::Index::kFalse)),
      index_map(std::make_shared<IndexMap>()) {
  CompleteInit();
}

TableInfo::TableInfo(const std::string& tablet_log_prefix,
                     Primary primary,
                     std::string table_id_,
                     std::string namespace_name,
                     std::string table_name,
                     TableType table_type,
                     const Schema& schema,
                     const IndexMap& index_map,
                     const boost::optional<IndexInfo>& index_info,
                     const SchemaVersion schema_version,
                     dockv::PartitionSchema partition_schema,
                     TableId pg_table_id_)
    : table_id(std::move(table_id_)),
      namespace_name(std::move(namespace_name)),
      table_name(std::move(table_name)),
      table_type(table_type),
      cotable_id(CHECK_RESULT(ParseCotableId(primary, table_id))),
      log_prefix(MakeTableInfoLogPrefix(tablet_log_prefix, primary, table_id)),
      doc_read_context(std::make_shared<docdb::DocReadContext>(
          log_prefix, table_type, docdb::Index(index_info.has_value()), schema,
          schema_version)),
      index_map(std::make_shared<IndexMap>(index_map)),
      index_info(index_info ? new IndexInfo(*index_info) : nullptr),
      schema_version(schema_version),
      partition_schema(std::move(partition_schema)),
      pg_table_id(std::move(pg_table_id_)) {
  CompleteInit();
}

TableInfo::TableInfo(const TableInfo& other,
                     const Schema& schema,
                     const IndexMap& index_map,
                     const std::vector<DeletedColumn>& deleted_cols,
                     const SchemaVersion schema_version)
    : table_id(other.table_id),
      namespace_name(other.namespace_name),
      table_name(other.table_name),
      table_type(other.table_type),
      cotable_id(other.cotable_id),
      log_prefix(other.log_prefix),
      doc_read_context(schema_version != other.schema_version
          ? std::make_shared<docdb::DocReadContext>(
              *other.doc_read_context, schema, schema_version)
          : std::make_shared<docdb::DocReadContext>(*other.doc_read_context)),
      index_map(std::make_shared<IndexMap>(index_map)),
      index_info(other.index_info ? new IndexInfo(*other.index_info) : nullptr),
      schema_version(schema_version),
      partition_schema(other.partition_schema),
      deleted_cols(other.deleted_cols) {
  this->deleted_cols.insert(this->deleted_cols.end(), deleted_cols.begin(), deleted_cols.end());
  CompleteInit();
}

// Specific case, use for schema update when schema version change does not required.
// Example: MarkBackfillDone, refer to https://github.com/yugabyte/yugabyte-db/issues/19544.
TableInfo::TableInfo(const TableInfo& other,
                     const Schema& schema)
    : table_id(other.table_id),
      namespace_name(other.namespace_name),
      table_name(other.table_name),
      table_type(other.table_type),
      cotable_id(other.cotable_id),
      log_prefix(other.log_prefix),
      doc_read_context(std::make_shared<docdb::DocReadContext>(*other.doc_read_context, schema)),
      index_map(std::make_shared<IndexMap>(*other.index_map)),
      index_info(other.index_info ? new IndexInfo(*other.index_info) : nullptr),
      schema_version(other.schema_version),
      partition_schema(other.partition_schema),
      deleted_cols(other.deleted_cols) {
  CompleteInit();
}

TableInfo::TableInfo(const TableInfo& other, SchemaVersion min_schema_version)
    : table_id(other.table_id),
      namespace_name(other.namespace_name),
      table_name(other.table_name),
      table_type(other.table_type),
      cotable_id(other.cotable_id),
      log_prefix(other.log_prefix),
      doc_read_context(std::make_shared<docdb::DocReadContext>(
          *other.doc_read_context, std::min(min_schema_version, other.schema_version))),
      index_map(std::make_shared<IndexMap>(*other.index_map)),
      index_info(other.index_info ? new IndexInfo(*other.index_info) : nullptr),
      schema_version(other.schema_version),
      partition_schema(other.partition_schema),
      deleted_cols(other.deleted_cols) {
  CompleteInit();
}

TableInfo::~TableInfo() = default;

void TableInfo::CompleteInit() {
  if (!index_info || !index_info->is_unique()) {
    return;
  }
  unique_index_key_projection = std::make_shared<dockv::ReaderProjection>(
      doc_read_context->schema(), index_info->index_key_column_ids());
}

Result<TableInfoPtr> TableInfo::LoadFromPB(
    const std::string& tablet_log_prefix, const TableId& primary_table_id, const TableInfoPB& pb) {
  Primary primary(primary_table_id == pb.table_id());
  auto log_prefix = MakeTableInfoLogPrefix(tablet_log_prefix, primary, pb.table_id());
  auto result = std::make_shared<TableInfo>(log_prefix, pb.table_type(), PrivateTag());
  RETURN_NOT_OK(result->DoLoadFromPB(primary, pb));
  result->CompleteInit();
  return result;
}

Status TableInfo::DoLoadFromPB(Primary primary, const TableInfoPB& pb) {
  table_id = pb.table_id();
  namespace_name = pb.namespace_name();
  namespace_id = pb.namespace_id();
  table_name = pb.table_name();
  table_type = pb.table_type();
  cotable_id = VERIFY_RESULT(ParseCotableId(primary, table_id));
  pg_table_id = pb.pg_table_id();

  RETURN_NOT_OK(doc_read_context->LoadFromPB(pb));
  if (pb.has_index_info()) {
    index_info.reset(new IndexInfo(pb.index_info()));
  }
  index_map->FromPB(pb.indexes());
  schema_version = pb.schema_version();

  if (pb.has_wal_retention_secs()) {
    wal_retention_secs = pb.wal_retention_secs();
  }

  RETURN_NOT_OK(dockv::PartitionSchema::FromPB(pb.partition_schema(), schema(), &partition_schema));

  for (const DeletedColumnPB& deleted_col : pb.deleted_cols()) {
    DeletedColumn col;
    RETURN_NOT_OK(DeletedColumn::FromPB(deleted_col, &col));
    deleted_cols.push_back(col);
  }

  return Status::OK();
}

Result<SchemaVersion> TableInfo::GetSchemaPackingVersion(
    const Schema& schema) const {
  return doc_read_context->schema_packing_storage.GetSchemaPackingVersion(
      table_type, schema);
}

Status TableInfo::MergeSchemaPackings(
    const TableInfoPB& pb, dockv::OverwriteSchemaPacking overwrite) {
  // If we are merging in the case of an out of cluster restore,
  // the schema version should already have been incremented to
  // match the snapshot.
  if (overwrite) {
    LOG_IF_WITH_PREFIX(DFATAL, schema_version < pb.schema_version())
        << "In order to merge schema packings during restore, "
        << "it is expected that schema version be at least "
        << pb.schema_version() << " for table " << table_id
        << " but version is " << schema_version;
  }
  RETURN_NOT_OK(doc_read_context->MergeWithRestored(pb, overwrite));
  // After the merge, the latest packing should be in sync with
  // the latest schema.
  const dockv::SchemaPacking& latest_packing = VERIFY_RESULT(
      doc_read_context->schema_packing_storage.GetPacking(schema_version));
  LOG_IF_WITH_PREFIX(DFATAL,
                     !latest_packing.SchemaContainsPacking(table_type, doc_read_context->schema()))
      << "After merging schema packings during restore, latest schema does not"
      << " have the same packing as the corresponding latest packing for table "
      << table_id;
  return Status::OK();
}

void TableInfo::ToPB(TableInfoPB* pb) const {
  pb->set_table_id(table_id);
  pb->set_namespace_name(namespace_name);
  pb->set_namespace_id(namespace_id);
  pb->set_table_name(table_name);
  pb->set_table_type(table_type);

  doc_read_context->ToPB(schema_version, pb);
  if (index_info) {
    index_info->ToPB(pb->mutable_index_info());
  }
  index_map->ToPB(pb->mutable_indexes());
  pb->set_schema_version(schema_version);
  pb->set_wal_retention_secs(wal_retention_secs);

  partition_schema.ToPB(pb->mutable_partition_schema());

  for (const DeletedColumn& deleted_col : deleted_cols) {
    deleted_col.CopyToPB(pb->mutable_deleted_cols()->Add());
  }
  pb->set_pg_table_id(pg_table_id);
}

const Schema& TableInfo::schema() const {
  return doc_read_context->schema();
}

SchemaPtr TableInfo::SharedSchema() const {
  return SchemaPtr(doc_read_context, const_cast<Schema*>(&doc_read_context->schema()));
}

Result<docdb::CompactionSchemaInfo> TableInfo::Packing(
    const TableInfoPtr& self, SchemaVersion schema_version, HybridTime history_cutoff) {
  if (schema_version == docdb::kLatestSchemaVersion) {
    schema_version = self->schema_version;
  }
  auto packing = self->doc_read_context->schema_packing_storage.GetPacking(schema_version);
  if (!packing.ok()) {
    return STATUS_FORMAT(Corruption, "Cannot find packing for table: $0, schema version: $1",
                         self->table_id, schema_version);
  }
  docdb::ColumnIds deleted_before_history_cutoff;
  for (const auto& deleted_col : self->deleted_cols) {
    if (deleted_col.ht < history_cutoff) {
      deleted_before_history_cutoff.insert(deleted_col.id);
    }
  }
  return docdb::CompactionSchemaInfo {
    .table_type = self->table_type,
    .schema_version = schema_version,
    .schema_packing = rpc::SharedField(self, packing.get_ptr()),
    .cotable_id = self->cotable_id,
    .deleted_cols = std::move(deleted_before_history_cutoff),
    .packed_row_version = docdb::PackedRowVersion(
        self->table_type, self->doc_read_context->schema().is_colocated()),
    .schema = rpc::SharedField(self->doc_read_context, &self->doc_read_context->schema())
  };
}

bool TableInfo::TEST_Equals(const TableInfo& lhs, const TableInfo& rhs) {
  return YB_STRUCT_EQUALS(table_id,
                          namespace_name,
                          table_name,
                          table_type,
                          cotable_id,
                          schema_version,
                          deleted_cols,
                          wal_retention_secs) &&
         DereferencedEqual(lhs.doc_read_context,
                           rhs.doc_read_context,
                           &docdb::DocReadContext::TEST_Equals) &&
         DereferencedEqual(lhs.index_map,
                           rhs.index_map,
                           IndexMap::TEST_Equals) &&
         DereferencedEqual(lhs.index_info,
                           rhs.index_info,
                           IndexInfo::TEST_Equals) &&
         lhs.partition_schema.Equals(rhs.partition_schema);
}

Status KvStoreInfo::LoadTablesFromPB(
    const std::string& tablet_log_prefix,
    const google::protobuf::RepeatedPtrField<TableInfoPB>& pbs, const TableId& primary_table_id) {
  tables.clear();
  for (const auto& table_pb : pbs) {
    TableInfoPtr table_info = VERIFY_RESULT(TableInfo::LoadFromPB(
        tablet_log_prefix, primary_table_id, table_pb));
    tables.emplace(table_info->table_id, table_info);

    const Schema& schema = table_info->schema();
    if (!table_info->primary() && schema.table_properties().is_ysql_catalog_table()) {
      // TODO(#79): when adding for multiple KV-stores per Raft group support - check if we need
      // to set cotable ID.
      table_info->doc_read_context->SetCotableId(table_info->cotable_id);
    }
    if (schema.has_colocation_id()) {
      colocation_to_table.emplace(schema.colocation_id(), table_info);
    }
  }
  return Status::OK();
}

Status KvStoreInfo::LoadFromPB(const std::string& tablet_log_prefix,
                               const KvStoreInfoPB& pb,
                               const TableId& primary_table_id,
                               bool local_superblock) {
  kv_store_id = KvStoreId(pb.kv_store_id());
  if (local_superblock) {
    rocksdb_dir = pb.rocksdb_dir();
  }
  lower_bound_key = pb.lower_bound_key();
  upper_bound_key = pb.upper_bound_key();
  parent_data_compacted = pb.parent_data_compacted();
  last_full_compaction_time = pb.last_full_compaction_time();
  if (pb.has_post_split_compaction_file_number_upper_bound()) {
    post_split_compaction_file_number_upper_bound =
        pb.post_split_compaction_file_number_upper_bound();
  } else {
    post_split_compaction_file_number_upper_bound.reset();
  }

  for (const auto& schedule_id : pb.snapshot_schedules()) {
    snapshot_schedules.insert(VERIFY_RESULT(FullyDecodeSnapshotScheduleId(schedule_id)));
  }

  return LoadTablesFromPB(tablet_log_prefix, pb.tables(), primary_table_id);
}

Status KvStoreInfo::MergeWithRestored(
    const KvStoreInfoPB& snapshot_kvstoreinfo, const TableId& primary_table_id, bool colocated,
    dockv::OverwriteSchemaPacking overwrite) {
  lower_bound_key = snapshot_kvstoreinfo.lower_bound_key();
  upper_bound_key = snapshot_kvstoreinfo.upper_bound_key();
  parent_data_compacted = snapshot_kvstoreinfo.parent_data_compacted();
  last_full_compaction_time = snapshot_kvstoreinfo.last_full_compaction_time();
  if (snapshot_kvstoreinfo.has_post_split_compaction_file_number_upper_bound()) {
    post_split_compaction_file_number_upper_bound =
        snapshot_kvstoreinfo.post_split_compaction_file_number_upper_bound();
  } else {
    post_split_compaction_file_number_upper_bound.reset();
  }

  return RestoreMissingValuesAndMergeTableSchemaPackings(
      snapshot_kvstoreinfo, primary_table_id, colocated, overwrite);
}

Status KvStoreInfo::RestoreMissingValuesAndMergeTableSchemaPackings(
    const KvStoreInfoPB& snapshot_kvstoreinfo, const TableId& primary_table_id, bool colocated,
    dockv::OverwriteSchemaPacking overwrite) {
  if (!colocated) {
    SCHECK(
        snapshot_kvstoreinfo.tables_size() == 1 && tables.size() == 1, Corruption,
        Format(
            "Unexpected table counts during schema merge. Snapshot tables and restored tables "
            "should both be non-colocated (singular). Snapshot table count: $0, restored table "
            "count: $1",
            snapshot_kvstoreinfo.tables_size(), tables.size()));
    auto schema = tables.begin()->second->doc_read_context->mutable_schema();
    if (overwrite) {
      schema->UpdateMissingValuesFrom(snapshot_kvstoreinfo.tables(0).schema().columns());
    }
    return tables.begin()->second->MergeSchemaPackings(snapshot_kvstoreinfo.tables(0), overwrite);
  }

  for (const auto& snapshot_table_pb : snapshot_kvstoreinfo.tables()) {
    TableInfo* target_table = VERIFY_RESULT(FindMatchingTable(snapshot_table_pb, primary_table_id));
    if (target_table != nullptr) {
      auto schema = target_table->doc_read_context->mutable_schema();
      if (overwrite) {
        schema->UpdateMissingValuesFrom(snapshot_table_pb.schema().columns());
      }
      RETURN_NOT_OK(target_table->MergeSchemaPackings(snapshot_table_pb, overwrite));
    }
  }
  return Status::OK();
}

Result<TableInfo*> KvStoreInfo::FindMatchingTable(
    const TableInfoPB& snapshot_table, const TableId& primary_table_id) {
  const auto snapshot_table_id = snapshot_table.table_id();
  if (IsColocationParentTableId(snapshot_table_id)) {
    auto table_it = tables.find(primary_table_id);
    SCHECK(
        table_it != tables.end(), Corruption,
        Format("Cannot find parent table with id $0 of a colocated tablet", primary_table_id));
    return table_it->second.get();
  }
  const auto& schema = snapshot_table.schema();
  SCHECK(
      schema.has_colocated_table_id() && schema.colocated_table_id().has_colocation_id(),
      Corruption,
      Format(
          "Missing colocation id on the colocated table $0 from the snapshot", snapshot_table_id));

  const auto& colocation_id = schema.colocated_table_id().colocation_id();
  auto table_it = colocation_to_table.find(colocation_id);
  if (table_it == colocation_to_table.end()) {
    // Backups made prior to 29681f579760703663cdcbd2abbfe4c9eb6e533c will not include colocation
    // ids for partitioned tables on colocated tablets in the YSQL dump. To gracefully handle tables
    // in such backups we need to tolerate failed matches.
    LOG(WARNING) << Format(
        "Skipping schema merging for snapshot table $0 with colocation id $1 because a "
        "matching colocation id cannot be found",
        snapshot_table.table_name(),
        snapshot_table.schema().colocated_table_id().colocation_id());
    return nullptr;
  }
  // Sanity check names and schemas. Because colocation ids are chosen at restore time for colocated
  // partitioned tables in backups made prior to 29681f579760703663cdcbd2abbfe4c9eb6e533c yb-master
  // may have randomly chosen a colocation id for a partitioned table that matches the colocation id
  // of a partitioned table in the snapshot with an incompatible schema.
  auto& local_table = table_it->second;
  if (local_table->table_name != snapshot_table.table_name() ||
      (snapshot_table.schema().has_pgschema_name() && local_table->schema().has_pgschema_name() &&
       snapshot_table.schema().pgschema_name() != local_table->schema().SchemaName())) {
    LOG(WARNING) << Format(
        "Skipping schema merging for snapshot table $0.$1 due to mismatch with local "
        "table names, local table is $2.$3",
        snapshot_table.schema().pgschema_name(),
        snapshot_table.table_name(),
        local_table->schema().SchemaName(),
        local_table->table_name);
    return nullptr;
  }
  return table_it->second.get();
}

void KvStoreInfo::ToPB(const TableId& primary_table_id, KvStoreInfoPB* pb) const {
  pb->set_kv_store_id(kv_store_id.ToString());
  pb->set_rocksdb_dir(rocksdb_dir);
  if (lower_bound_key.empty()) {
    pb->clear_lower_bound_key();
  } else {
    pb->set_lower_bound_key(lower_bound_key);
  }
  if (upper_bound_key.empty()) {
    pb->clear_upper_bound_key();
  } else {
    pb->set_upper_bound_key(upper_bound_key);
  }
  pb->set_parent_data_compacted(parent_data_compacted);
  pb->set_last_full_compaction_time(last_full_compaction_time);
  if (post_split_compaction_file_number_upper_bound.has_value()) {
    pb->set_post_split_compaction_file_number_upper_bound(
        *post_split_compaction_file_number_upper_bound);
  } else {
    pb->clear_post_split_compaction_file_number_upper_bound();
  }

  // Putting primary table first, then all other tables.
  pb->mutable_tables()->Reserve(narrow_cast<int>(tables.size() + 1));
  const auto& it = tables.find(primary_table_id);
  if (it != tables.end()) {
    it->second->ToPB(pb->add_tables());
  }
  for (const auto& [id, table_info] : tables) {
    if (id != primary_table_id) {
      table_info->ToPB(pb->add_tables());
    }
  }

  for (const auto& schedule_id : snapshot_schedules) {
    pb->add_snapshot_schedules(schedule_id.data(), schedule_id.size());
  }
}

void KvStoreInfo::UpdateColocationMap(const TableInfoPtr& table_info) {
  auto colocation_id = table_info->schema().colocation_id();
  if (colocation_id) {
    colocation_to_table.emplace(colocation_id, table_info);
  }
}

// TODO(pscompact): do we need to update this for file number upper bound?
bool KvStoreInfo::TEST_Equals(const KvStoreInfo& lhs, const KvStoreInfo& rhs) {
  auto eq = [](const auto& lhs, const auto& rhs) {
    return DereferencedEqual(lhs, rhs, TableInfo::TEST_Equals);
  };
  return YB_STRUCT_EQUALS(kv_store_id,
                          rocksdb_dir,
                          lower_bound_key,
                          upper_bound_key,
                          parent_data_compacted,
                          snapshot_schedules) &&
         MapsEqual(lhs.tables, rhs.tables, eq) &&
         MapsEqual(lhs.colocation_to_table, rhs.colocation_to_table, eq);
}

namespace {

std::string MakeTabletDirName(const TabletId& tablet_id) {
  return Format("tablet-$0", tablet_id);
}

} // namespace

// ============================================================================

Result<RaftGroupMetadataPtr> RaftGroupMetadata::CreateNew(
    const RaftGroupMetadataData& data, const std::string& data_root_dir,
    const std::string& wal_root_dir) {
  auto* fs_manager = data.fs_manager;
  // Verify that no existing Raft group exists with the same ID.
  if (fs_manager->LookupTablet(data.raft_group_id)) {
    return STATUS(AlreadyPresent, "Raft group already exists", data.raft_group_id);
  }

  auto wal_top_dir = wal_root_dir;
  auto data_top_dir = data_root_dir;
  // Use first dirs if the indices are not explicitly passed in.
  // Master don't pass dirs for SysCatalog.
  if (data_root_dir.empty()) {
    auto data_root_dirs = fs_manager->GetDataRootDirs();
    CHECK(!data_root_dirs.empty()) << "No data root directories found";
    data_top_dir = data_root_dirs[0];
  }

  if (wal_root_dir.empty()) {
    auto wal_root_dirs = fs_manager->GetWalRootDirs();
    CHECK(!wal_root_dirs.empty()) << "No wal root directories found";
    wal_top_dir = wal_root_dirs[0];
  }

  const string table_dir_name = Substitute("table-$0", data.table_info->table_id);
  const string tablet_dir_name = MakeTabletDirName(data.raft_group_id);
  const string wal_dir = JoinPathSegments(wal_top_dir, table_dir_name, tablet_dir_name);
  const string rocksdb_dir = JoinPathSegments(
      data_top_dir, FsManager::kRocksDBDirName, table_dir_name, tablet_dir_name);

  RaftGroupMetadataPtr ret(new RaftGroupMetadata(data, rocksdb_dir, wal_dir));
  RETURN_NOT_OK(ret->Flush());
  return ret;
}

Result<RaftGroupMetadataPtr> RaftGroupMetadata::Load(
    FsManager* fs_manager, const RaftGroupId& raft_group_id) {
  RaftGroupMetadataPtr ret(new RaftGroupMetadata(fs_manager, raft_group_id));
  RETURN_NOT_OK(ret->LoadFromDisk());
  return ret;
}

Result<RaftGroupMetadataPtr> RaftGroupMetadata::TEST_LoadOrCreate(
    const RaftGroupMetadataData& data) {
  if (data.fs_manager->LookupTablet(data.raft_group_id)) {
    auto metadata = Load(data.fs_manager, data.raft_group_id);
    if (metadata.ok()) {
      if (!(**metadata).schema()->Equals(data.table_info->schema())) {
        return STATUS_FORMAT(
            Corruption, "Schema on disk ($0) does not match expected schema ($1)",
            *(*metadata)->schema(), data.table_info->schema());
      }
      return *metadata;
    }
    return metadata.status();
  }
  string data_root_dir = data.fs_manager->GetDataRootDirs()[0];
  data.fs_manager->SetTabletPathByDataPath(data.raft_group_id, data_root_dir);
  return CreateNew(data, data_root_dir);
}

template <class TablesMap>
Status MakeTableNotFound(const TableId& table_id, const RaftGroupId& raft_group_id,
                         const TablesMap& tables, const char* file_name, int line_number) {
  std::string table_name = "<unknown_table_name>";
  if (!table_id.empty()) {
    const auto iter = tables.find(table_id);
    if (iter != tables.end()) {
      table_name = iter->second->table_name;
    }
  }
  std::ostringstream string_stream;
  string_stream << "Table " << table_name << " (" << table_id << ") not found in Raft group "
      << raft_group_id;
  std::string msg = string_stream.str();
#ifndef NDEBUG
  // This very large message should be logged instead of being appended to STATUS.
  std::string suffix = Format(". Tables: $0.", tables);
  VLOG(1) << msg << suffix;
#endif
  return Status(Status::kNotFound, file_name, line_number, msg);
}

#define RETURN_TABLE_NOT_FOUND(table_id, tables) \
    return MakeTableNotFound((table_id), raft_group_id_, (tables), __FILE__, __LINE__)

Status MakeColocatedTableNotFound(
    ColocationId colocation_id, const RaftGroupId& raft_group_id) {
  std::ostringstream string_stream;
  string_stream << "Table with colocation id " << colocation_id << " not found in Raft group "
                << raft_group_id;
  std::string msg = string_stream.str();
  return STATUS(NotFound, msg);
}

Result<TableInfoPtr> RaftGroupMetadata::GetTableInfo(const TableId& table_id) const {
  std::lock_guard lock(data_mutex_);
  return GetTableInfoUnlocked(table_id);
}

Result<TableInfoPtr> RaftGroupMetadata::GetTableInfoUnlocked(const TableId& table_id) const {
  const auto& tables = kv_store_.tables;

  const auto& id = !table_id.empty() ? table_id : primary_table_id_;
  const auto iter = tables.find(id);
  if (iter == tables.end()) {
    RETURN_TABLE_NOT_FOUND(table_id, tables);
  }
  return iter->second;
}

Result<TableInfoPtr> RaftGroupMetadata::GetTableInfoUnlocked(ColocationId colocation_id) const {
  if (colocation_id == kColocationIdNotSet) {
    return GetTableInfoUnlocked(primary_table_id_);
  }

  const auto& colocation_to_table = kv_store_.colocation_to_table;
  const auto iter = colocation_to_table.find(colocation_id);
  if (iter == colocation_to_table.end()) {
    return MakeColocatedTableNotFound(colocation_id, raft_group_id_);
  }
  return iter->second;
}

Result<TableInfoPtr> RaftGroupMetadata::GetTableInfo(ColocationId colocation_id) const {
  std::lock_guard lock(data_mutex_);
  return GetTableInfoUnlocked(colocation_id);
}

Status RaftGroupMetadata::DeleteTabletData(TabletDataState delete_type,
                                           const OpId& last_logged_opid) {
  CHECK(delete_type == TABLET_DATA_DELETED ||
        delete_type == TABLET_DATA_TOMBSTONED)
      << "DeleteTabletData() called with unsupported delete_type on tablet "
      << raft_group_id_ << ": " << TabletDataState_Name(delete_type)
      << " (" << delete_type << ")";

  // First add all of our blocks to the orphan list
  // and clear our rowsets. This serves to erase all the data.
  //
  // We also set the state in our persisted metadata to indicate that
  // we have been deleted.
  {
    std::lock_guard lock(data_mutex_);
    tablet_data_state_ = delete_type;
    if (!last_logged_opid.empty()) {
      tombstone_last_logged_opid_ = last_logged_opid;
    }
  }

  rocksdb::Options rocksdb_options;
  TabletOptions tablet_options;
  docdb::InitRocksDBOptions(
      &rocksdb_options, log_prefix_, raft_group_id_, nullptr /* statistics */, tablet_options);

  const auto& rocksdb_dir = this->rocksdb_dir();
  LOG_WITH_PREFIX(INFO) << "Destroying regular db at: " << rocksdb_dir;
  rocksdb::Status status = rocksdb::DestroyDB(rocksdb_dir, rocksdb_options);

  if (!status.ok()) {
    LOG_WITH_PREFIX(ERROR) << "Failed to destroy regular DB at: " << rocksdb_dir << ": " << status;
  } else {
    LOG_WITH_PREFIX(INFO) << "Successfully destroyed regular DB at: " << rocksdb_dir;
  }

  if (fs_manager_->env()->FileExists(rocksdb_dir)) {
    auto s = fs_manager_->env()->DeleteRecursively(rocksdb_dir);
    LOG_IF_WITH_PREFIX(WARNING, !s.ok())
        << "Unable to delete rocksdb data directory " << rocksdb_dir;
  }

  const auto intents_dir = this->intents_rocksdb_dir();
  if (fs_manager_->env()->FileExists(intents_dir)) {
    status = rocksdb::DestroyDB(intents_dir, rocksdb_options);

    if (!status.ok()) {
      LOG_WITH_PREFIX(ERROR) << "Failed to destroy provisional records DB at: " << intents_dir
                             << ": " << status;
    } else {
      LOG_WITH_PREFIX(INFO) << "Successfully destroyed provisional records DB at: " << intents_dir;
    }
  }

  if (fs_manager_->env()->FileExists(intents_dir)) {
    auto s = fs_manager_->env()->DeleteRecursively(intents_dir);
    LOG_IF_WITH_PREFIX(WARNING, !s.ok()) << "Unable to delete intents directory " << intents_dir;
  }

  // TODO(tsplit): decide what to do with snapshots for split tablets that we delete after split.
  // As for now, snapshots will be deleted as well.
  const auto snapshots_dir = this->snapshots_dir();
  if (fs_manager_->env()->FileExists(snapshots_dir)) {
    auto s = fs_manager_->env()->DeleteRecursively(snapshots_dir);
    LOG_IF_WITH_PREFIX(WARNING, !s.ok())
        << "Unable to delete snapshots directory " << snapshots_dir;
  }

  // Flushing will sync the new tablet_data_state_ to disk and will now also
  // delete all the data.
  RETURN_NOT_OK(Flush());

  // Re-sync to disk one more time.
  // This call will typically re-sync with an empty orphaned blocks list
  // (unless deleting any orphans failed during the last Flush()), so that we
  // don't try to re-delete the deleted orphaned blocks on every startup.
  return Flush();
}

bool RaftGroupMetadata::IsTombstonedWithNoRocksDBData() const {
  std::lock_guard lock(data_mutex_);
  const auto& rocksdb_dir = kv_store_.rocksdb_dir;
  const auto intents_dir = rocksdb_dir + kIntentsDBSuffix;
  return tablet_data_state_ == TABLET_DATA_TOMBSTONED &&
      !fs_manager_->env()->FileExists(rocksdb_dir) &&
      !fs_manager_->env()->FileExists(intents_dir);
}

Status RaftGroupMetadata::DeleteSuperBlock() {
  std::lock_guard lock(data_mutex_);
  if (tablet_data_state_ != TABLET_DATA_DELETED) {
    return STATUS(IllegalState,
        Substitute("Tablet $0 is not in TABLET_DATA_DELETED state. "
                   "Call DeleteTabletData(TABLET_DATA_DELETED) first. "
                   "Tablet data state: $1 ($2)",
                   raft_group_id_,
                   TabletDataState_Name(tablet_data_state_),
                   tablet_data_state_));
  }

  string path = VERIFY_RESULT(FilePath());
  RETURN_NOT_OK_PREPEND(fs_manager_->env()->DeleteFile(path),
                        "Unable to delete superblock for Raft group " + raft_group_id_);
  return Status::OK();
}

RaftGroupMetadata::RaftGroupMetadata(
    const RaftGroupMetadataData& data, const std::string& data_dir, const std::string& wal_dir)
    : state_(kNotWrittenYet),
      raft_group_id_(data.raft_group_id),
      partition_(std::make_shared<Partition>(data.partition)),
      primary_table_id_(data.table_info->table_id),
      kv_store_(KvStoreId(raft_group_id_), data_dir, data.snapshot_schedules),
      fs_manager_(data.fs_manager),
      wal_dir_(wal_dir),
      tablet_data_state_(data.tablet_data_state),
      colocated_(data.colocated),
      cdc_min_replicated_index_(std::numeric_limits<int64_t>::max()),
      cdc_sdk_min_checkpoint_op_id_(OpId::Invalid()),
      cdc_sdk_safe_time_(HybridTime::kInvalid),
      log_prefix_(consensus::MakeTabletLogPrefix(raft_group_id_, fs_manager_->uuid())),
      hosted_services_(data.hosted_services) {
  CHECK(data.table_info->schema().has_column_ids());
  CHECK_GT(data.table_info->schema().num_key_columns(), 0);
  kv_store_.tables.emplace(primary_table_id_, data.table_info);
  kv_store_.UpdateColocationMap(data.table_info);
  if (FLAGS_TEST_invalidate_last_change_metadata_op) {
    last_applied_change_metadata_op_id_ = OpId::Invalid();
  } else {
    last_applied_change_metadata_op_id_ = OpId::Min();
  }
}

RaftGroupMetadata::~RaftGroupMetadata() {
}

RaftGroupMetadata::RaftGroupMetadata(FsManager* fs_manager, const RaftGroupId& raft_group_id)
    : state_(kNotLoadedYet),
      raft_group_id_(std::move(raft_group_id)),
      kv_store_(KvStoreId(raft_group_id_)),
      fs_manager_(fs_manager),
      log_prefix_(consensus::MakeTabletLogPrefix(raft_group_id_, fs_manager_->uuid())) {
}

Status RaftGroupMetadata::LoadFromDisk(const std::string& path) {
  TRACE_EVENT1("raft_group", "RaftGroupMetadata::LoadFromDisk",
               "raft_group_id", raft_group_id_);

  CHECK_EQ(state_, kNotLoadedYet);

  RaftGroupReplicaSuperBlockPB superblock;
  RETURN_NOT_OK(ReadSuperBlockFromDisk(&superblock, path));
  RETURN_NOT_OK_PREPEND(LoadFromSuperBlock(superblock, /* local_superblock = */ true),
                        "Failed to load data from superblock protobuf");
  state_ = kInitialized;
  return Status::OK();
}

Status RaftGroupMetadata::LoadFromSuperBlock(const RaftGroupReplicaSuperBlockPB& superblock,
                                             bool local_superblock) {
  if (!superblock.has_kv_store()) {
    // Backward compatibility for tablet=KV-store=raft-group.
    RaftGroupReplicaSuperBlockPB superblock_migrated(superblock);
    RETURN_NOT_OK(MigrateSuperblock(&superblock_migrated));
    RETURN_NOT_OK(LoadFromSuperBlock(superblock_migrated, local_superblock));
    return Flush();
  }

  VLOG_WITH_PREFIX(2) << "Loading RaftGroupMetadata from SuperBlockPB:" << std::endl
                      << superblock.DebugString();

  {
    std::lock_guard lock(data_mutex_);

    // Verify that the Raft group id matches with the one in the protobuf.
    if (superblock.raft_group_id() != raft_group_id_) {
      return STATUS(Corruption, "Expected id=" + raft_group_id_ +
                                " found " + superblock.raft_group_id(),
                                superblock.DebugString());
    }
    Partition partition;
    Partition::FromPB(superblock.partition(), &partition);
    partition_ = std::make_shared<Partition>(partition);
    primary_table_id_ = superblock.primary_table_id();
    colocated_ = superblock.colocated();

    RETURN_NOT_OK(kv_store_.LoadFromPB(
        log_prefix_, superblock.kv_store(), primary_table_id_, local_superblock));

    wal_dir_ = superblock.wal_dir();
    tablet_data_state_ = superblock.tablet_data_state();

    if (superblock.has_tombstone_last_logged_opid()) {
      tombstone_last_logged_opid_ = OpId::FromPB(superblock.tombstone_last_logged_opid());
    } else {
      tombstone_last_logged_opid_ = OpId();
    }
    cdc_min_replicated_index_ = superblock.cdc_min_replicated_index();

    if (superblock.has_cdc_sdk_min_checkpoint_op_id()) {
      auto cdc_sdk_checkpoint = OpId::FromPB(superblock.cdc_sdk_min_checkpoint_op_id());
      if (cdc_sdk_checkpoint == OpId() && (!superblock.has_is_under_cdc_sdk_replication() ||
                                           !superblock.is_under_cdc_sdk_replication())) {
        // This indiactes that 'cdc_sdk_min_checkpoint_op_id' has been set to 0.0 during a prior
        // upgrade even without CDC running. Hence we reset it to -1.-1.
        LOG_WITH_PREFIX(WARNING) << "Setting cdc_sdk_min_checkpoint_op_id_ to OpId::Invalid(), "
                                    "since 'is_under_cdc_sdk_replication' is not set";
        cdc_sdk_min_checkpoint_op_id_ = OpId::Invalid();
      } else {
        cdc_sdk_min_checkpoint_op_id_ = cdc_sdk_checkpoint;
        is_under_cdc_sdk_replication_ = superblock.is_under_cdc_sdk_replication();
      }
    } else {
      // If a cluster is upgraded from any version lesser than 2.14, 'cdc_sdk_min_checkpoint_op_id'
      // would be absent from the superblock, and we need to set 'cdc_sdk_min_checkpoint_op_id_' to
      // OpId::Invalid() as this indicates that there are no active CDC streams on this tablet.
      cdc_sdk_min_checkpoint_op_id_ = OpId::Invalid();
    }

    cdc_sdk_safe_time_ = HybridTime::FromPB(superblock.cdc_sdk_safe_time());
    is_under_xcluster_replication_ = superblock.is_under_xcluster_replication();
    hidden_ = superblock.hidden();
    auto restoration_hybrid_time = HybridTime::FromPB(superblock.restoration_hybrid_time());
    if (restoration_hybrid_time) {
      restoration_hybrid_time_ = restoration_hybrid_time;
    }

    if (superblock.has_split_op_id()) {
      split_op_id_ = OpId::FromPB(superblock.split_op_id());

      SCHECK_EQ(implicit_cast<size_t>(superblock.split_child_tablet_ids().size()),
                split_child_tablet_ids_.size(),
                Corruption, "Expected exact number of child tablet ids");
      for (size_t i = 0; i != split_child_tablet_ids_.size(); ++i) {
        split_child_tablet_ids_[i] = superblock.split_child_tablet_ids(narrow_cast<int>(i));
      }
    }

    if (!superblock.active_restorations().empty()) {
      active_restorations_.reserve(superblock.active_restorations().size());
      for (const auto& id : superblock.active_restorations()) {
        active_restorations_.push_back(VERIFY_RESULT(FullyDecodeTxnSnapshotRestorationId(id)));
      }
    }

    if (!superblock.hosted_stateful_services().empty()) {
      hosted_services_.clear();
      hosted_services_.reserve(superblock.hosted_stateful_services().size());
      for (auto& service_kind : superblock.hosted_stateful_services()) {
        hosted_services_.insert((StatefulServiceKind)service_kind);
      }
    }
    // If new code is reading old data then this field won't exist. In such cases,
    // we start with an invalid value of -1.-1.
    if (superblock.has_last_flushed_change_metadata_op_id()) {
      last_flushed_change_metadata_op_id_ =
          OpId::FromPB(superblock.last_flushed_change_metadata_op_id());
    } else {
      last_flushed_change_metadata_op_id_ = OpId::Invalid();
    }

    last_applied_change_metadata_op_id_ = last_flushed_change_metadata_op_id_;
  }

  return Status::OK();
}

Status RaftGroupMetadata::Flush(OnlyIfDirty only_if_dirty) {
  TRACE_EVENT1("raft_group", "RaftGroupMetadata::Flush",
               "raft_group_id", raft_group_id_);

  MutexLock l_flush(flush_lock_);
  RaftGroupReplicaSuperBlockPB pb;
  OpId last_applied_change_metadata_op_id;
  {
    std::lock_guard lock(data_mutex_);
    SCHECK_FORMAT(
        last_flushed_change_metadata_op_id_ <= last_applied_change_metadata_op_id_, IllegalState,
        "Superblock flush marker $0 ahead of apply marker $1", last_flushed_change_metadata_op_id_,
        last_applied_change_metadata_op_id_);
    bool is_drty = last_flushed_change_metadata_op_id_ < last_applied_change_metadata_op_id_;
    if (only_if_dirty && !is_drty) {
      // Skipping flush as in-memory metadata is not dirty.
      return Status::OK();
    }
    ToSuperBlockUnlocked(&pb);
    last_applied_change_metadata_op_id = last_applied_change_metadata_op_id_;
    ResetMinUnflushedChangeMetadataOpIdUnlocked();
  }
  RETURN_NOT_OK(SaveToDiskUnlocked(pb));
  {
    // Update last_flushed_change_metadata_op_id_ only after disk write is complete. This removes
    // the need to hold flush_lock_ for reading last_flushed_change_metadata_op_id_.
    std::lock_guard lock(data_mutex_);
    last_flushed_change_metadata_op_id_ = last_applied_change_metadata_op_id;
  }
  TRACE("Metadata flushed");
  VLOG_WITH_PREFIX(3) << "RaftGroupMetadata flushed";

  return Status::OK();
}

Status RaftGroupMetadata::SaveTo(const std::string& path) {
  RaftGroupReplicaSuperBlockPB pb;
  {
    std::lock_guard lock(data_mutex_);
    ToSuperBlockUnlocked(&pb);
  }

  return SaveToDiskUnlocked(pb, path);
}

Status RaftGroupMetadata::ReplaceSuperBlock(const RaftGroupReplicaSuperBlockPB &pb) {
  {
    MutexLock l(flush_lock_);
    RETURN_NOT_OK_PREPEND(SaveToDiskUnlocked(pb), "Unable to replace superblock");
  }

  RETURN_NOT_OK_PREPEND(LoadFromSuperBlock(pb, /* local_superblock = */ false),
                        "Failed to load data from superblock protobuf");

  return Status::OK();
}

Result<std::string> RaftGroupMetadata::FilePath() const {
  return fs_manager_->GetRaftGroupMetadataPath(raft_group_id_);
}

Status RaftGroupMetadata::SaveToDiskUnlocked(
    const RaftGroupReplicaSuperBlockPB &pb, const std::string& path) {
  if (path.empty()) {
    flush_lock_.AssertAcquired();
    return SaveToDiskUnlocked(pb, VERIFY_RESULT(FilePath()));
  }

  SCOPED_WAIT_STATUS(SaveRaftGroupMetadataToDisk);
  RETURN_NOT_OK_PREPEND(pb_util::WritePBContainerToPath(
                            fs_manager_->encrypted_env(), path, pb,
                            pb_util::OVERWRITE, pb_util::SYNC),
                        Substitute("Failed to write Raft group metadata $0", raft_group_id_));

  return Status::OK();
}

Status RaftGroupMetadata::MergeWithRestored(
    const std::string& path, dockv::OverwriteSchemaPacking overwrite) {
  RaftGroupReplicaSuperBlockPB snapshot_superblock;
  RETURN_NOT_OK(ReadSuperBlockFromDisk(&snapshot_superblock, path));
  std::lock_guard lock(data_mutex_);
  return kv_store_.MergeWithRestored(
      snapshot_superblock.kv_store(), primary_table_id_, colocated_, overwrite);
}

Status RaftGroupMetadata::ReadSuperBlockFromDisk(
    RaftGroupReplicaSuperBlockPB* superblock, const std::string& path) const {
  if (path.empty()) {
    return ReadSuperBlockFromDisk(superblock, VERIFY_RESULT(FilePath()));
  }

  return ReadSuperBlockFromDisk(fs_manager_->encrypted_env(), path, superblock);
}

Status RaftGroupMetadata::ReadSuperBlockFromDisk(
    Env* env, const std::string& path, RaftGroupReplicaSuperBlockPB* superblock) {
  RETURN_NOT_OK_PREPEND(
      pb_util::ReadPBContainerFromPath(env, path, superblock),
      Substitute("Could not load Raft group metadata from $0", path));
  // Migration for backward compatibility with versions which don't have separate
  // TableType::TRANSACTION_STATUS_TABLE_TYPE.
  if (superblock->obsolete_table_type() == TableType::REDIS_TABLE_TYPE &&
      superblock->obsolete_table_name() == kGlobalTransactionsTableName) {
    superblock->set_obsolete_table_type(TableType::TRANSACTION_STATUS_TABLE_TYPE);
  }
  return Status::OK();
}

void RaftGroupMetadata::ToSuperBlock(RaftGroupReplicaSuperBlockPB* superblock) const {
  // acquire the lock so that rowsets_ doesn't get changed until we're finished.
  std::lock_guard lock(data_mutex_);
  ToSuperBlockUnlocked(superblock);
}

void RaftGroupMetadata::ToSuperBlockUnlocked(RaftGroupReplicaSuperBlockPB* superblock) const {
  // Convert to protobuf.
  RaftGroupReplicaSuperBlockPB pb;
  pb.set_raft_group_id(raft_group_id_);
  partition_->ToPB(pb.mutable_partition());

  kv_store_.ToPB(primary_table_id_, pb.mutable_kv_store());

  pb.set_wal_dir(wal_dir_);
  pb.set_tablet_data_state(tablet_data_state_);
  if (!tombstone_last_logged_opid_.empty()) {
    tombstone_last_logged_opid_.ToPB(pb.mutable_tombstone_last_logged_opid());
  }

  pb.set_primary_table_id(primary_table_id_);
  pb.set_colocated(colocated_);
  pb.set_cdc_min_replicated_index(cdc_min_replicated_index_);
  cdc_sdk_min_checkpoint_op_id_.ToPB(pb.mutable_cdc_sdk_min_checkpoint_op_id());
  pb.set_cdc_sdk_safe_time(cdc_sdk_safe_time_.ToUint64());
  pb.set_is_under_xcluster_replication(is_under_xcluster_replication_);
  pb.set_hidden(hidden_);
  if (restoration_hybrid_time_) {
    pb.set_restoration_hybrid_time(restoration_hybrid_time_.ToUint64());
  }
  pb.set_is_under_cdc_sdk_replication(is_under_cdc_sdk_replication_);

  if (!split_op_id_.empty()) {
    split_op_id_.ToPB(pb.mutable_split_op_id());
    auto& split_child_table_ids = *pb.mutable_split_child_tablet_ids();
    split_child_table_ids.Reserve(narrow_cast<int>(split_child_tablet_ids_.size()));
    for (const auto& split_child_tablet_id : split_child_tablet_ids_) {
      *split_child_table_ids.Add() = split_child_tablet_id;
    }
  }

  if (!active_restorations_.empty()) {
    auto& active_restorations = *pb.mutable_active_restorations();
    active_restorations.Reserve(narrow_cast<int>(active_restorations_.size()));
    for (const auto& id : active_restorations_) {
      active_restorations.Add()->assign(id.AsSlice().cdata(), id.size());
    }
  }

  if (!hosted_services_.empty()) {
    auto& hosted_services = *pb.mutable_hosted_stateful_services();
    hosted_services.Reserve(narrow_cast<int>(hosted_services_.size()));
    for (const auto& service_kind : hosted_services_) {
      *hosted_services.Add() = service_kind;
    }
  }

  if (last_applied_change_metadata_op_id_.valid()) {
    last_applied_change_metadata_op_id_.ToPB(pb.mutable_last_flushed_change_metadata_op_id());
  }

  superblock->Swap(&pb);
}

void RaftGroupMetadata::SetSchema(const Schema& schema,
                                  const IndexMap& index_map,
                                  const std::vector<DeletedColumn>& deleted_cols,
                                  const SchemaVersion version,
                                  const OpId& op_id,
                                  const TableId& table_id) {
  std::lock_guard lock(data_mutex_);
  SetSchemaUnlocked(schema, index_map, deleted_cols, version, op_id, table_id);
}

void RaftGroupMetadata::SetSchemaUnlocked(const Schema& schema,
                                  const IndexMap& index_map,
                                  const std::vector<DeletedColumn>& deleted_cols,
                                  const SchemaVersion version,
                                  const OpId& op_id,
                                  const TableId& table_id) {
  DCHECK(data_mutex_.is_locked());
  DCHECK(schema.has_column_ids());
  TableId target_table_id = table_id.empty() ? primary_table_id_ : table_id;
  auto it = kv_store_.tables.find(target_table_id);
  CHECK(it != kv_store_.tables.end());
  TableInfoPtr new_table_info = std::make_shared<TableInfo>(*it->second,
                                                            schema,
                                                            index_map,
                                                            deleted_cols,
                                                            version);
  if (target_table_id != primary_table_id_) {
    if (schema.table_properties().is_ysql_catalog_table()) {
      // TODO(alex): cotable_id should be copied from original schema, do we need this section?
      //             Might be related to #5017, #6107
      auto cotable_id = CHECK_RESULT(Uuid::FromHexString(target_table_id));
      new_table_info->doc_read_context->SetCotableId(cotable_id);
    }
    // Ensure colocation ID remains unchanged.
    const auto& old_schema = it->second->schema();
    CHECK(old_schema.has_colocation_id() == schema.has_colocation_id())
            << "Attempted to change colocation state for table " << table_id
            << " from " << old_schema.has_colocation_id()
            << " to " << schema.has_colocation_id();
    CHECK(!old_schema.has_colocation_id() ||
          old_schema.colocation_id() == schema.colocation_id())
            << "Attempted to change colocation ID for table " << table_id
            << " from " << old_schema.colocation_id()
            << " to " << schema.colocation_id();

    if (schema.has_colocation_id()) {
      auto colocation_it = kv_store_.colocation_to_table.find(schema.colocation_id());
      CHECK(colocation_it != kv_store_.colocation_to_table.end());
      colocation_it->second = new_table_info;
    }
  }
  VLOG_WITH_PREFIX(1) << raft_group_id_ << " Updating table " << target_table_id
                      << " to Schema version " << version
                      << " from \n" << AsString(it->second)
                      << " to \n" << AsString(new_table_info);
  it->second.swap(new_table_info);
  // Update op id if it is a change metadata operation.
  // We don't update if the passed op id is invalid. Cases when this will happen:
  // 1. If we are replaying during tablet bootstrap and last_flushed_change_metadata_op_id
  // was invalid - For e.g. after upgrade when new code reads old data.
  // In such a case, we ensure that we are no worse than old code's behavior
  // which essentially implies that we mute this new logic until
  // we get a new change metadata operation request post tablet bootstrap.
  // 2. During remote bootstrap in RemoteBootstrapClient::Start.
  OnChangeMetadataOperationAppliedUnlocked(op_id);
}

void RaftGroupMetadata::SetPartitionSchema(const dockv::PartitionSchema& partition_schema) {
  std::lock_guard lock(data_mutex_);
  auto& tables = kv_store_.tables;
  auto it = tables.find(primary_table_id_);
  CHECK(it != tables.end());
  it->second->partition_schema = partition_schema;
}

void RaftGroupMetadata::SetTableName(
    const string& namespace_name, const string& table_name,
    const OpId& op_id, const TableId& table_id) {
  std::lock_guard lock(data_mutex_);
  SetTableNameUnlocked(namespace_name, table_name, op_id, table_id);
}

void RaftGroupMetadata::SetTableNameUnlocked(
    const string& namespace_name, const string& table_name,
    const OpId& op_id, const TableId& table_id) {
  DCHECK(data_mutex_.is_locked());
  auto& tables = kv_store_.tables;
  auto& id = table_id.empty() ? primary_table_id_ : table_id;
  auto it = tables.find(id);
  CHECK(it != tables.end());
  it->second->namespace_name = namespace_name;
  it->second->table_name = table_name;
  OnChangeMetadataOperationAppliedUnlocked(op_id);
}

void RaftGroupMetadata::SetSchemaAndTableName(
    const Schema& schema, const IndexMap& index_map,
    const std::vector<DeletedColumn>& deleted_cols,
    const SchemaVersion version, const std::string& namespace_name,
    const std::string& table_name, const OpId& op_id, const TableId& table_id) {
  std::lock_guard lock(data_mutex_);
  SetSchemaUnlocked(schema, index_map, deleted_cols, version, op_id, table_id);
  SetTableNameUnlocked(namespace_name, table_name, op_id, table_id);
}

void RaftGroupMetadata::AddTable(
    const std::string& table_id, const std::string& namespace_name, const std::string& table_name,
    const TableType table_type, const Schema& schema, const IndexMap& index_map,
    const dockv::PartitionSchema& partition_schema, const boost::optional<IndexInfo>& index_info,
    const SchemaVersion schema_version, const OpId& op_id, const TableId& pg_table_id) {
  DCHECK(schema.has_column_ids());
  std::lock_guard lock(data_mutex_);
  Primary primary(table_id == primary_table_id_);
  TableInfoPtr new_table_info = std::make_shared<TableInfo>(log_prefix_,
                                                            primary,
                                                            table_id,
                                                            namespace_name,
                                                            table_name,
                                                            table_type,
                                                            schema,
                                                            index_map,
                                                            index_info,
                                                            schema_version,
                                                            partition_schema,
                                                            pg_table_id);
  if (!primary) {
    if (schema.table_properties().is_ysql_catalog_table()) {
      // TODO(alex): cotable_id seems to be properly copied from schema, do we need this section?
      //             Might be related to #5017, #6107
      new_table_info->doc_read_context->SetCotableId(new_table_info->cotable_id);
    }
  }
  auto& tables = kv_store_.tables;
  auto[iter, inserted] = tables.emplace(table_id, new_table_info);
  OnChangeMetadataOperationAppliedUnlocked(op_id);
  if (inserted) {
    VLOG_WITH_PREFIX(1) << "Added table with schema version " << schema_version << "\n"
                        << AsString(new_table_info);
    kv_store_.UpdateColocationMap(new_table_info);
    return;
  }

  const auto& existing_table = *iter->second;
  VLOG_WITH_PREFIX(1) << "Updating to Schema version " << schema_version << " from\n"
                      << AsString(existing_table) << "\nto\n"
                      << AsString(new_table_info);

  if (!existing_table.schema().table_properties().is_ysql_catalog_table() &&
      schema.table_properties().is_ysql_catalog_table()) {
    // This must be the one-time migration with transactional DDL being turned on for the first
    // time on this cluster.
    return;
  }

  // We never expect colocation IDs to mismatch.
  const auto& existing_schema = existing_table.schema();
  CHECK(existing_schema.has_colocation_id() == schema.has_colocation_id())
      << "Attempted to change colocation state for table " << table_id << " from "
      << existing_schema.has_colocation_id() << " to " << schema.has_colocation_id();

  CHECK(
      !existing_schema.has_colocation_id() ||
      existing_schema.colocation_id() == schema.colocation_id())
      << "Attempted to change colocation ID for table " << table_id << " from "
      << existing_schema.colocation_id() << " to " << schema.colocation_id();

  CHECK(!schema.colocation_id() || kv_store_.colocation_to_table.count(schema.colocation_id()))
      << "Missing entry in colocation table: " << schema.colocation_id() << ", "
      << AsString(kv_store_.colocation_to_table);
}

void RaftGroupMetadata::RemoveTable(const TableId& table_id, const OpId& op_id) {
  std::lock_guard lock(data_mutex_);
  auto& tables = kv_store_.tables;
  auto it = tables.find(table_id);
  if (it != tables.end()) {
    auto colocation_id = it->second->schema().colocation_id();
    if (colocation_id) {
      kv_store_.colocation_to_table.erase(colocation_id);
    }
    tables.erase(it);
  }
  OnChangeMetadataOperationAppliedUnlocked(op_id);
}

string RaftGroupMetadata::data_root_dir() const {
  const auto& rocksdb_dir = kv_store_.rocksdb_dir;
  if (rocksdb_dir.empty()) {
    return "";
  } else {
    auto data_root_dir = DirName(DirName(rocksdb_dir));
    if (strcmp(BaseName(data_root_dir).c_str(), FsManager::kRocksDBDirName) == 0) {
      data_root_dir = DirName(data_root_dir);
    }
    return data_root_dir;
  }
}

string RaftGroupMetadata::wal_root_dir() const {
  std::string wal_dir = this->wal_dir();

  if (wal_dir.empty()) {
    return "";
  }

  auto wal_root_dir = DirName(wal_dir);
  if (strcmp(BaseName(wal_root_dir).c_str(), FsManager::kWalDirName) != 0) {
    wal_root_dir = DirName(wal_root_dir);
  }
  return wal_root_dir;
}

Status RaftGroupMetadata::set_namespace_id(const NamespaceId& namespace_id) {
  {
    std::lock_guard lock(data_mutex_);
    primary_table_info_unlocked()->namespace_id = namespace_id;
  }
  return Flush();
}

void RaftGroupMetadata::set_wal_retention_secs(uint32 wal_retention_secs) {
  std::lock_guard lock(data_mutex_);
  auto it = kv_store_.tables.find(primary_table_id_);
  if (it == kv_store_.tables.end()) {
    LOG_WITH_PREFIX(DFATAL) << "Unable to set WAL retention time for primary table "
                            << primary_table_id_;
    return;
  }
  it->second->wal_retention_secs = wal_retention_secs;
  LOG_WITH_PREFIX(INFO) << "Set RaftGroupMetadata wal retention time to "
                        << wal_retention_secs << " seconds";
}

uint32_t RaftGroupMetadata::wal_retention_secs() const {
  std::lock_guard lock(data_mutex_);
  auto it = kv_store_.tables.find(primary_table_id_);
  if (it == kv_store_.tables.end()) {
    return 0;
  }
  return it->second->wal_retention_secs;
}

Status RaftGroupMetadata::set_cdc_min_replicated_index(int64 cdc_min_replicated_index) {
  {
    std::lock_guard lock(data_mutex_);
    cdc_min_replicated_index_ = cdc_min_replicated_index;
  }
  return Flush();
}

int64_t RaftGroupMetadata::cdc_min_replicated_index() const {
  std::lock_guard lock(data_mutex_);
  return cdc_min_replicated_index_;
}

OpId RaftGroupMetadata::cdc_sdk_min_checkpoint_op_id() const {
  std::lock_guard lock(data_mutex_);
  return cdc_sdk_min_checkpoint_op_id_;
}

HybridTime RaftGroupMetadata::cdc_sdk_safe_time() const {
  std::lock_guard lock(data_mutex_);
  return cdc_sdk_safe_time_;
}

bool RaftGroupMetadata::is_under_cdc_sdk_replication() const {
  std::lock_guard lock(data_mutex_);
  return is_under_cdc_sdk_replication_;
}

Status RaftGroupMetadata::set_cdc_sdk_min_checkpoint_op_id(const OpId& cdc_min_checkpoint_op_id) {
  {
    std::lock_guard lock(data_mutex_);
    cdc_sdk_min_checkpoint_op_id_ = cdc_min_checkpoint_op_id;

    if (cdc_min_checkpoint_op_id == OpId::Max() || cdc_min_checkpoint_op_id == OpId::Invalid()) {
      // This means we no longer have an active CDC stream for the tablet.
      is_under_cdc_sdk_replication_ = false;
    } else if (cdc_min_checkpoint_op_id.valid()) {
      // Any OpId less than OpId::Max() indicates we are actively streaming from this tablet.
      is_under_cdc_sdk_replication_ = true;
    }
  }
  return Flush();
}

Status RaftGroupMetadata::set_cdc_sdk_safe_time(const HybridTime& cdc_sdk_safe_time) {
  {
    std::lock_guard lock(data_mutex_);
    cdc_sdk_safe_time_ = cdc_sdk_safe_time;
  }
  return Flush();
}

Result<bool> RaftGroupMetadata::SetAllCDCRetentionBarriers(
    int64 cdc_wal_index, OpId cdc_sdk_intents_op_id, HybridTime cdc_sdk_history_cutoff,
    bool require_history_cutoff, bool initial_retention_barrier) {

  // WAL retention
  //  cdc_min_replicated_index : indicates if a WAL segment is being used by CDC
  //                             and thus impacts GC of the WAL segments
  if (!initial_retention_barrier || cdc_min_replicated_index() > cdc_wal_index) {
    VLOG_WITH_PREFIX(1) << "Setting cdc_min_replicated index WAL retention barrier to "
                        << cdc_wal_index;
    RETURN_NOT_OK(set_cdc_min_replicated_index(cdc_wal_index));
  } else {
    VLOG_WITH_PREFIX(1) << "Skipping setting cdc_min_replicated index WAL retention barrier. "
                        << "Stricter requirement at " << cdc_min_replicated_index()
                        << ", current requirement is for " << cdc_wal_index;
  }

  // History Retention
  if (require_history_cutoff) {
    if (!initial_retention_barrier ||
        cdc_sdk_safe_time() == HybridTime::kInvalid ||
        cdc_sdk_safe_time() > cdc_sdk_history_cutoff) {
      VLOG_WITH_PREFIX(1) << "Setting history retention barrier to " << cdc_sdk_history_cutoff;
      RETURN_NOT_OK(set_cdc_sdk_safe_time(cdc_sdk_history_cutoff));
    } else {
      VLOG_WITH_PREFIX(1) << "Skipping setting history retention barrier. "
                          << "Stricter requirement at " << cdc_sdk_safe_time()
                          << ", current requirement is for " << cdc_sdk_history_cutoff;
    }
  }

  // Intents Retention
  //  set_cdc_sdk_min_checkpoint_op_id - opid beyond which GC will not happen
  if (!initial_retention_barrier ||
      cdc_sdk_min_checkpoint_op_id() == OpId::Invalid() ||
      cdc_sdk_min_checkpoint_op_id() > cdc_sdk_intents_op_id) {
    VLOG_WITH_PREFIX(1) << "Setting intents retention barrier to " << cdc_sdk_intents_op_id;
    RETURN_NOT_OK(set_cdc_sdk_min_checkpoint_op_id(cdc_sdk_intents_op_id));
  } else {
    VLOG_WITH_PREFIX(1) << "Skipping setting intents retention barrier. "
                        << "Stricter requirement at " << cdc_sdk_min_checkpoint_op_id()
                        << ", current requirement is for " << cdc_sdk_intents_op_id;
    return false;
  }

  return true;
}

Status RaftGroupMetadata::SetIsUnderXClusterReplicationAndFlush(
    bool is_under_xcluster_replication) {
  {
    std::lock_guard lock(data_mutex_);
    is_under_xcluster_replication_ = is_under_xcluster_replication;
  }
  return Flush();
}

bool RaftGroupMetadata::IsUnderXClusterReplication() const {
  std::lock_guard lock(data_mutex_);
  return is_under_xcluster_replication_;
}

void RaftGroupMetadata::SetHidden(bool value) {
  std::lock_guard lock(data_mutex_);
  hidden_ = value;
}

bool RaftGroupMetadata::hidden() const {
  std::lock_guard lock(data_mutex_);
  return hidden_;
}

void RaftGroupMetadata::SetRestorationHybridTime(HybridTime value) {
  std::lock_guard lock(data_mutex_);
  restoration_hybrid_time_ = std::max(restoration_hybrid_time_, value);
}

HybridTime RaftGroupMetadata::restoration_hybrid_time() const {
  std::lock_guard lock(data_mutex_);
  return restoration_hybrid_time_;
}

void RaftGroupMetadata::set_tablet_data_state(TabletDataState state) {
  std::lock_guard lock(data_mutex_);
  tablet_data_state_ = state;
}

const std::string& RaftGroupMetadata::LogPrefix() const {
  return log_prefix_;
}

OpId RaftGroupMetadata::tombstone_last_logged_opid() const {
  std::lock_guard lock(data_mutex_);
  return tombstone_last_logged_opid_;
}

bool RaftGroupMetadata::IsSysCatalog() const {
  std::lock_guard lock(data_mutex_);
  return primary_table_id_ == master::kSysCatalogTableId;
}

bool RaftGroupMetadata::colocated() const {
  std::lock_guard lock(data_mutex_);
  return colocated_;
}

// Returns whether lazy superblock flush is enabled for the tablet. It requires
// lazily_flush_superblock flag to be true and the tablet to be colocated (currently this feature is
// only applicable on colocated table creation). This feature depends on
// last_flushed_change_metadata_op_id to be valid. Hence, additionally requires
// FLAGS_TEST_invalidate_last_change_metadata_op to be false.
LazySuperblockFlushEnabled RaftGroupMetadata::IsLazySuperblockFlushEnabled() const {
  bool lazy_superblock_flush_enabled = !FLAGS_TEST_invalidate_last_change_metadata_op &&
                                       FLAGS_lazily_flush_superblock && colocated() &&
                                       !IsSysCatalog();
  return LazySuperblockFlushEnabled(lazy_superblock_flush_enabled);
}

TabletDataState RaftGroupMetadata::tablet_data_state() const {
  std::lock_guard lock(data_mutex_);
  return tablet_data_state_;
}

std::array<TabletId, kNumSplitParts> RaftGroupMetadata::split_child_tablet_ids() const {
  std::lock_guard lock(data_mutex_);
  return split_child_tablet_ids_;
}

OpId RaftGroupMetadata::split_op_id() const {
  std::lock_guard lock(data_mutex_);
  return split_op_id_;
}

OpId RaftGroupMetadata::GetOpIdToDeleteAfterAllApplied() const {
  std::lock_guard lock(data_mutex_);
  if (tablet_data_state_ != TabletDataState::TABLET_DATA_SPLIT_COMPLETED || hidden_) {
    return OpId::Invalid();
  }
  return split_op_id_;
}

void RaftGroupMetadata::SetSplitDone(
    const OpId& op_id, const TabletId& child1, const TabletId& child2) {
  std::lock_guard lock(data_mutex_);
  tablet_data_state_ = TabletDataState::TABLET_DATA_SPLIT_COMPLETED;
  split_op_id_ = op_id;
  split_child_tablet_ids_[0] = child1;
  split_child_tablet_ids_[1] = child2;
}

bool RaftGroupMetadata::has_active_restoration() const {
  std::lock_guard lock(data_mutex_);
  return !active_restorations_.empty();
}

void RaftGroupMetadata::RegisterRestoration(const TxnSnapshotRestorationId& restoration_id) {
  std::lock_guard lock(data_mutex_);
  if (tablet_data_state_ == TabletDataState::TABLET_DATA_SPLIT_COMPLETED) {
    tablet_data_state_ = TabletDataState::TABLET_DATA_READY;
    split_op_id_ = OpId();
    split_child_tablet_ids_[0] = std::string();
    split_child_tablet_ids_[1] = std::string();
  }
  active_restorations_.push_back(restoration_id);
}

void RaftGroupMetadata::UnregisterRestoration(const TxnSnapshotRestorationId& restoration_id) {
  std::lock_guard lock(data_mutex_);
  Erase(restoration_id, &active_restorations_);
}

HybridTime RaftGroupMetadata::CheckCompleteRestorations(
    const RestorationCompleteTimeMap& restoration_complete_time) {
  std::lock_guard lock(data_mutex_);
  auto result = HybridTime::kMin;
  for (const auto& restoration_id : active_restorations_) {
    auto it = restoration_complete_time.find(restoration_id);
    if (it != restoration_complete_time.end() && it->second) {
      result = std::max(result, it->second);
    }
  }
  return result;
}

bool RaftGroupMetadata::CleanupRestorations(
    const RestorationCompleteTimeMap& restoration_complete_time) {
  bool result = false;
  std::lock_guard lock(data_mutex_);
  for (auto it = active_restorations_.begin(); it != active_restorations_.end();) {
    auto known_restoration_it = restoration_complete_time.find(*it);
    if (known_restoration_it == restoration_complete_time.end() || known_restoration_it->second) {
      it = active_restorations_.erase(it);
      result = true;
    } else {
      ++it;
    }
  }
  return result;
}

std::unordered_set<StatefulServiceKind> RaftGroupMetadata::GetHostedServiceList() const {
  std::lock_guard lock(data_mutex_);
  return hosted_services_;
}

void RaftGroupMetadata::DisableSchemaGC() {
  std::lock_guard lock(data_mutex_);
  ++disable_schema_gc_counter_;
}

void RaftGroupMetadata::EnableSchemaGC() {
  std::lock_guard lock(data_mutex_);
  --disable_schema_gc_counter_;
  LOG_IF(DFATAL, disable_schema_gc_counter_ < 0)
      << "Disable GC counter underflow: " << disable_schema_gc_counter_;
}

Status RaftGroupMetadata::OldSchemaGC(
    const std::unordered_map<Uuid, SchemaVersion, UuidHash>& versions) {
  bool need_flush = false;
  {
    std::lock_guard lock(data_mutex_);
    if (disable_schema_gc_counter_ != 0) {
      // Could skip schema GC at all, because it will be cleaned after next compaction.
      return Status::OK();
    }
    for (const auto& [table_id, schema_version] : versions) {
      auto it = table_id.IsNil() ? kv_store_.tables.find(primary_table_id_)
                                 : kv_store_.tables.find(table_id.ToHexString());
      if (it == kv_store_.tables.end()) {
        YB_LOG_EVERY_N_SECS(WARNING, 1)
            << "Unknown table during " << __func__ << ": " << table_id.ToString();
        continue;
      }
      if (!it->second->doc_read_context->schema_packing_storage.HasVersionBelow(schema_version)) {
        continue;
      }
      auto new_value = std::make_shared<TableInfo>(
          *it->second, schema_version);
      it->second = new_value;
      need_flush = true;
    }
  }

  if (!need_flush) {
    return Status::OK();
  }
  return Flush();
}

Result<docdb::CompactionSchemaInfo> RaftGroupMetadata::CotablePacking(
    const Uuid& cotable_id, uint32_t schema_version, HybridTime history_cutoff) {
  if (cotable_id.IsNil()) {
    return TableInfo::Packing(primary_table_info(), schema_version, history_cutoff);
  }

  auto res = GetTableInfo(cotable_id.ToHexString());
  if (!res.ok()) {
    return STATUS_FORMAT(
        NotFound, "Cannot find table info for: $0, raft group id: $1",
        cotable_id, raft_group_id_);
  }
  return TableInfo::Packing(*res, schema_version, history_cutoff);
}

Result<docdb::CompactionSchemaInfo> RaftGroupMetadata::ColocationPacking(
    ColocationId colocation_id, uint32_t schema_version, HybridTime history_cutoff) {
  auto it = kv_store_.colocation_to_table.find(colocation_id);
  if (it == kv_store_.colocation_to_table.end()) {
    return STATUS_FORMAT(
        NotFound, "Cannot find table info for colocation: $0, raft group id: $1",
        colocation_id, raft_group_id_);
  }
  return TableInfo::Packing(it->second, schema_version, history_cutoff);
}

std::string RaftGroupMetadata::GetSubRaftGroupWalDir(const RaftGroupId& raft_group_id) const {
  std::lock_guard lock(data_mutex_);
  return JoinPathSegments(DirName(wal_dir_), MakeTabletDirName(raft_group_id));
}

std::string RaftGroupMetadata::GetSubRaftGroupDataDir(const RaftGroupId& raft_group_id) const {
  return JoinPathSegments(DirName(kv_store_.rocksdb_dir), MakeTabletDirName(raft_group_id));
}

// We directly init fields of a new metadata, so have to use NO_THREAD_SAFETY_ANALYSIS here.
Result<RaftGroupMetadataPtr> RaftGroupMetadata::CreateSubtabletMetadata(
    const RaftGroupId& raft_group_id, const Partition& partition,
    const std::string& lower_bound_key, const std::string& upper_bound_key)
    const NO_THREAD_SAFETY_ANALYSIS {
  RaftGroupReplicaSuperBlockPB superblock;
  ToSuperBlock(&superblock);
  fs_manager_->SetTabletPathByDataPath(raft_group_id,
                                         DirName(DirName(DirName(kv_store_.rocksdb_dir))));
  RaftGroupMetadataPtr metadata(new RaftGroupMetadata(fs_manager_, raft_group_id_));
  RETURN_NOT_OK(metadata->LoadFromSuperBlock(superblock, /* local_superblock = */ true));
  metadata->raft_group_id_ = raft_group_id;
  metadata->log_prefix_ = consensus::MakeTabletLogPrefix(raft_group_id, fs_manager_->uuid());
  metadata->wal_dir_ = GetSubRaftGroupWalDir(raft_group_id);
  metadata->kv_store_.kv_store_id = KvStoreId(raft_group_id);
  metadata->kv_store_.lower_bound_key = lower_bound_key;
  metadata->kv_store_.upper_bound_key = upper_bound_key;
  metadata->kv_store_.rocksdb_dir = GetSubRaftGroupDataDir(raft_group_id);
  metadata->kv_store_.parent_data_compacted = false;
  metadata->kv_store_.last_full_compaction_time = kNoLastFullCompactionTime;
  metadata->kv_store_.post_split_compaction_file_number_upper_bound.reset();
  *metadata->partition_ = partition;
  metadata->state_ = kInitialized;
  metadata->tablet_data_state_ = TABLET_DATA_INIT_STARTED;
  metadata->split_op_id_ = OpId();
  RETURN_NOT_OK(metadata->Flush());
  return metadata;
}

Result<std::string> RaftGroupMetadata::TopSnapshotsDir() const {
  auto result = snapshots_dir();
  RETURN_NOT_OK_PREPEND(
      fs_manager()->CreateDirIfMissingAndSync(result),
      Format("Unable to create snapshots directory $0", result));
  return result;
}

namespace {
// MigrateSuperblockForDXXXX functions are only needed for backward compatibility with
// YugabyteDB versions which don't have changes from DXXXX revision.
// Each MigrateSuperblockForDXXXX could be removed after all YugabyteDB installations are
// upgraded to have revision DXXXX.

Status MigrateSuperblockForD5900(RaftGroupReplicaSuperBlockPB* superblock) {
  // In previous version of superblock format we stored primary table metadata in superblock's
  // top-level fields (deprecated table_* and other). TableInfo objects were stored inside
  // RaftGroupReplicaSuperBlockPB.tables.
  //
  // In new format TableInfo objects and some other top-level fields are moved from superblock's
  // top-level fields into RaftGroupReplicaSuperBlockPB.kv_store. Primary table (see
  // RaftGroupMetadata::primary_table_id_ field description) metadata is stored inside one of
  // RaftGroupReplicaSuperBlockPB.kv_store.tables objects and is referenced by
  // RaftGroupReplicaSuperBlockPB.primary_table_id.
  if (superblock->has_kv_store()) {
    return Status::OK();
  }

  LOG(INFO) << "Migrating superblock for raft group " << superblock->raft_group_id();

  KvStoreInfoPB* kv_store_pb = superblock->mutable_kv_store();
  kv_store_pb->set_kv_store_id(superblock->raft_group_id());
  kv_store_pb->set_rocksdb_dir(superblock->obsolete_rocksdb_dir());
  kv_store_pb->mutable_rocksdb_files()->CopyFrom(superblock->obsolete_rocksdb_files());
  kv_store_pb->mutable_snapshot_files()->CopyFrom(superblock->obsolete_snapshot_files());

  TableInfoPB* primary_table = kv_store_pb->add_tables();
  primary_table->set_table_id(superblock->primary_table_id());
  primary_table->set_table_name(superblock->obsolete_table_name());
  primary_table->set_table_type(superblock->obsolete_table_type());
  primary_table->mutable_schema()->CopyFrom(superblock->obsolete_schema());
  primary_table->set_schema_version(superblock->obsolete_schema_version());
  primary_table->mutable_partition_schema()->CopyFrom(superblock->obsolete_partition_schema());
  primary_table->mutable_indexes()->CopyFrom(superblock->obsolete_indexes());
  primary_table->mutable_index_info()->CopyFrom(superblock->obsolete_index_info());
  primary_table->mutable_deleted_cols()->CopyFrom(superblock->obsolete_deleted_cols());

  kv_store_pb->mutable_tables()->MergeFrom(superblock->obsolete_tables());

  return Status::OK();
}

} // namespace

Status MigrateSuperblock(RaftGroupReplicaSuperBlockPB* superblock) {
  return MigrateSuperblockForD5900(superblock);
}

std::shared_ptr<std::vector<DeletedColumn>> RaftGroupMetadata::deleted_cols(
    const TableId& table_id) const {
  DCHECK_NE(state_, kNotLoadedYet);
  const TableInfoPtr table_info =
      table_id.empty() ? primary_table_info() : CHECK_RESULT(GetTableInfo(table_id));
  return std::shared_ptr<std::vector<DeletedColumn>>(table_info, &table_info->deleted_cols);
}

std::string RaftGroupMetadata::namespace_name(const TableId& table_id) const {
  DCHECK_NE(state_, kNotLoadedYet);
  if (table_id.empty()) {
    return primary_table_info()->namespace_name;
  }
  const auto& table_info = CHECK_RESULT(GetTableInfo(table_id));
  return table_info->namespace_name;
}

NamespaceId RaftGroupMetadata::namespace_id() const {
  DCHECK_NE(state_, kNotLoadedYet);
  return primary_table_info()->namespace_id;
}

std::string RaftGroupMetadata::table_name(const TableId& table_id) const {
  DCHECK_NE(state_, kNotLoadedYet);
  return CHECK_RESULT(GetTableInfo(table_id))->table_name;
}

TableType RaftGroupMetadata::table_type(const TableId& table_id) const {
  DCHECK_NE(state_, kNotLoadedYet);
  if (table_id.empty()) {
    return primary_table_info()->table_type;
  }
  const auto& table_info = CHECK_RESULT(GetTableInfo(table_id));
  return table_info->table_type;
}

SchemaPtr RaftGroupMetadata::schema(const TableId& table_id) const {
  DCHECK_NE(state_, kNotLoadedYet);
  const TableInfoPtr table_info = CHECK_RESULT(GetTableInfo(table_id));
  return table_info->SharedSchema();
}

std::shared_ptr<IndexMap> RaftGroupMetadata::index_map(const TableId& table_id) const {
  DCHECK_NE(state_, kNotLoadedYet);
  const TableInfoPtr table_info =
      table_id.empty() ? primary_table_info() : CHECK_RESULT(GetTableInfo(table_id));
  return table_info->index_map;
}

SchemaVersion RaftGroupMetadata::schema_version(const TableId& table_id) const {
  DCHECK_NE(state_, kNotLoadedYet);
  const TableInfoPtr table_info = CHECK_RESULT(GetTableInfo(table_id));
  return table_info->schema_version;
}

Result<SchemaVersion> RaftGroupMetadata::schema_version(ColocationId colocation_id) const {
  DCHECK_NE(state_, kNotLoadedYet);
  auto colocation_it = kv_store_.colocation_to_table.find(colocation_id);
  if (colocation_it == kv_store_.colocation_to_table.end()) {
    return STATUS_FORMAT(NotFound, "Cannot find table info for colocation: $0", colocation_id);
  }
  return colocation_it->second->schema_version;
}

Result<SchemaVersion> RaftGroupMetadata::schema_version(const Uuid& cotable_id) const {
  DCHECK_NE(state_, kNotLoadedYet);
  if (cotable_id.IsNil()) {
    // Return the parent table schema version
    return schema_version();
  }

  auto res = GetTableInfo(cotable_id.ToHexString());
  if (!res.ok()) {
    return STATUS_FORMAT(
        NotFound, "Cannot find table info for: $0, raft group id: $1", cotable_id, raft_group_id_);
  }

  return res->get()->schema_version;
}

const std::string& RaftGroupMetadata::indexed_table_id(const TableId& table_id) const {
  DCHECK_NE(state_, kNotLoadedYet);
  static const std::string kEmptyString = "";
  std::lock_guard lock(data_mutex_);
  const TableInfoPtr table_info = table_id.empty() ?
      primary_table_info_unlocked() : CHECK_RESULT(GetTableInfoUnlocked(table_id));
  const auto* index_info = table_info->index_info.get();
  return index_info ? index_info->indexed_table_id() : kEmptyString;
}

bool RaftGroupMetadata::is_index(const TableId& table_id) const {
  return !indexed_table_id(table_id).empty();
}

bool RaftGroupMetadata::is_local_index(const TableId& table_id) const {
  DCHECK_NE(state_, kNotLoadedYet);
  std::lock_guard lock(data_mutex_);
  const TableInfoPtr table_info = table_id.empty() ?
      primary_table_info_unlocked() : CHECK_RESULT(GetTableInfoUnlocked(table_id));
  const auto* index_info = table_info->index_info.get();
  return index_info && index_info->is_local();
}

bool RaftGroupMetadata::is_unique_index(const TableId& table_id) const {
  DCHECK_NE(state_, kNotLoadedYet);
  std::lock_guard lock(data_mutex_);
  const TableInfoPtr table_info = table_id.empty() ?
      primary_table_info_unlocked() : CHECK_RESULT(GetTableInfoUnlocked(table_id));
  const auto* index_info = table_info->index_info.get();
  return index_info && index_info->is_unique();
}

std::vector<ColumnId> RaftGroupMetadata::index_key_column_ids(const TableId& table_id) const {
  DCHECK_NE(state_, kNotLoadedYet);
  std::lock_guard lock(data_mutex_);
  const TableInfoPtr table_info = table_id.empty() ?
      primary_table_info_unlocked() : CHECK_RESULT(GetTableInfoUnlocked(table_id));
  const auto* index_info = table_info->index_info.get();
  return index_info ? index_info->index_key_column_ids() : std::vector<ColumnId>();
}

bool RaftGroupMetadata::UsePartialRangeKeyIntents() const {
  return table_type() == TableType::PGSQL_TABLE_TYPE;
}

std::vector<TableId> RaftGroupMetadata::GetAllColocatedTables() const {
  std::lock_guard lock(data_mutex_);
  std::vector<TableId> table_ids;
  table_ids.reserve(kv_store_.tables.size());
  for (const auto& id_and_info : kv_store_.tables) {
    table_ids.emplace_back(id_and_info.first);
  }
  return table_ids;
}

size_t RaftGroupMetadata::GetColocatedTablesCount() const {
  DCHECK_NE(state_, kNotLoadedYet);
  std::lock_guard lock(data_mutex_);
  return kv_store_.tables.size();
}

void RaftGroupMetadata::IterateColocatedTables(
    std::function<void(const TableInfo&)> callback) const {
  DCHECK_NE(state_, kNotLoadedYet);
  CHECK(static_cast<bool>(callback));

  std::lock_guard lock(data_mutex_);
  for (const auto& it : kv_store_.tables) {
    callback(*CHECK_NOTNULL(it.second));
  }
}

std::unordered_map<TableId, ColocationId>
RaftGroupMetadata::GetAllColocatedTablesWithColocationId() const {
  DCHECK_NE(state_, kNotLoadedYet);

  std::lock_guard lock(data_mutex_);
  std::unordered_map<TableId, ColocationId> table_colocation_id_map;
  for (const auto& [id, info] : kv_store_.colocation_to_table) {
    table_colocation_id_map[info->table_id] = id;
  }
  return table_colocation_id_map;
}

Status CheckCanServeTabletData(const RaftGroupMetadata& metadata) {
  auto data_state = metadata.tablet_data_state();
  if (!CanServeTabletData(data_state)) {
    return STATUS_FORMAT(
        IllegalState, "Tablet $0 data state not ready: $1", metadata.raft_group_id(),
        TabletDataState_Name(data_state));
  }
  return Status::OK();
}

OpId RaftGroupMetadata::LastFlushedChangeMetadataOperationOpId() const {
  // Since last_flushed_change_metadata_op_id_ is updated only after the superblock is persisted
  // to disk, flush_lock_ is not required to read it.
  std::lock_guard lock(data_mutex_);
  return last_flushed_change_metadata_op_id_;
}

OpId RaftGroupMetadata::TEST_LastAppliedChangeMetadataOperationOpId() const {
  std::lock_guard lock(data_mutex_);
  return last_applied_change_metadata_op_id_;
}

void RaftGroupMetadata::SetLastAppliedChangeMetadataOperationOpIdUnlocked(const OpId& op_id) {
  if (FLAGS_TEST_invalidate_last_change_metadata_op) {
    last_applied_change_metadata_op_id_ = OpId::Invalid();
    return;
  }
  if (op_id.valid()) {
    last_applied_change_metadata_op_id_ = op_id;
  }
}

void RaftGroupMetadata::SetLastAppliedChangeMetadataOperationOpId(const OpId& op_id) {
  std::lock_guard lock(data_mutex_);
  SetLastAppliedChangeMetadataOperationOpIdUnlocked(op_id);
}

void RaftGroupMetadata::OnChangeMetadataOperationAppliedUnlocked(const OpId& applied_op_id) {
  SetLastAppliedChangeMetadataOperationOpIdUnlocked(applied_op_id);
  if (applied_op_id.valid()) {
    // If min_unflushed_change_metadata_op_id_ == OpId::Max(), set it to applied_op_id.
    min_unflushed_change_metadata_op_id_ =
        std::min(min_unflushed_change_metadata_op_id_, applied_op_id);
  }
}

void RaftGroupMetadata::OnChangeMetadataOperationApplied(const OpId& applied_op_id) {
  std::lock_guard lock(data_mutex_);
  OnChangeMetadataOperationAppliedUnlocked(applied_op_id);
}

void RaftGroupMetadata::ResetMinUnflushedChangeMetadataOpIdUnlocked() {
  // On a flush, min_unflushed_change_metadata_op_id_ is reset to OpId::Max().
  min_unflushed_change_metadata_op_id_ = OpId::Max();
}

OpId RaftGroupMetadata::MinUnflushedChangeMetadataOpId() const {
  // flush_lock_ is required because min_unflushed_change_metadata_op_id_ is updated during
  // superblock flush.
  MutexLock l_flush(flush_lock_);
  std::lock_guard lock(data_mutex_);
  return min_unflushed_change_metadata_op_id_;
}

Status RaftGroupMetadata::OnBackfillDone(const TableId& table_id) {
  std::lock_guard lock(data_mutex_);
  return OnBackfillDoneUnlocked(table_id);
}

Status RaftGroupMetadata::OnBackfillDone(const OpId& op_id, const TableId& table_id) {
  std::lock_guard lock(data_mutex_);
  RETURN_NOT_OK(OnBackfillDoneUnlocked(table_id));
  OnChangeMetadataOperationAppliedUnlocked(op_id);
  return Status::OK();
}

Status RaftGroupMetadata::OnBackfillDoneUnlocked(const TableId& table_id) {
  if (FLAGS_TEST_skip_metadata_backfill_done) {
    LOG_WITH_PREFIX(INFO) << "Skipping RaftGroupMetadata::OnBackfillDoneUnlocked()";
    return Status::OK();
  }

  TableId target_table_id = table_id.empty() ? primary_table_id_ : table_id;
  auto it = kv_store_.tables.find(target_table_id);
  if (it == kv_store_.tables.end()) {
    RETURN_TABLE_NOT_FOUND(table_id, kv_store_.tables);
  }

  Schema new_schema = it->second->schema();
  new_schema.SetRetainDeleteMarkers(false);

  TableInfoPtr new_table_info = std::make_shared<TableInfo>(*it->second, new_schema);
  VLOG_WITH_PREFIX(1) << raft_group_id_ << " Updating table " << target_table_id
                      << " to Schema version " << new_table_info->schema_version
                      << " from \n" << AsString(it->second)
                      << " to \n" << AsString(new_table_info);
  it->second.swap(new_table_info);
  return Status::OK();
}

bool RaftGroupMetadata::OnPostSplitCompactionDone() {
  std::lock_guard lock(data_mutex_);
  bool updated = false;

  if (!kv_store_.parent_data_compacted) {
    kv_store_.parent_data_compacted = true;
    updated = true;
  }

  if (kv_store_.post_split_compaction_file_number_upper_bound != 0) {
    kv_store_.post_split_compaction_file_number_upper_bound = 0;
    updated = true;
  }

  return updated;
}

} // namespace yb::tablet
