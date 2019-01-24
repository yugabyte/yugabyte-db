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

#include <gflags/gflags.h>
#include <boost/optional.hpp>
#include "yb/rocksdb/db.h"
#include "yb/rocksdb/options.h"
#include "yb/common/wire_protocol.h"
#include "yb/consensus/opid_util.h"
#include "yb/docdb/docdb_rocksdb_util.h"
#include "yb/gutil/atomicops.h"
#include "yb/gutil/bind.h"
#include "yb/gutil/dynamic_annotations.h"
#include "yb/gutil/map-util.h"
#include "yb/gutil/stl_util.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/rocksutil/yb_rocksdb.h"
#include "yb/rocksutil/yb_rocksdb_logger.h"
#include "yb/server/metadata.h"
#include "yb/tablet/tablet_options.h"
#include "yb/util/debug/trace_event.h"
#include "yb/util/flag_tags.h"
#include "yb/util/logging.h"
#include "yb/util/pb_util.h"
#include "yb/util/random.h"
#include "yb/util/status.h"
#include "yb/util/trace.h"

DEFINE_bool(enable_tablet_orphaned_block_deletion, true,
            "Whether to enable deletion of orphaned blocks from disk. "
            "Note: This is only exposed for debugging purposes!");
TAG_FLAG(enable_tablet_orphaned_block_deletion, advanced);
TAG_FLAG(enable_tablet_orphaned_block_deletion, hidden);
TAG_FLAG(enable_tablet_orphaned_block_deletion, runtime);

using std::shared_ptr;

using base::subtle::Barrier_AtomicIncrement;
using strings::Substitute;

using yb::consensus::MinimumOpId;
using yb::consensus::RaftConfigPB;

namespace yb {
namespace tablet {

const int64 kNoDurableMemStore = -1;
const std::string kIntentsSubdir = "intents";
const std::string kIntentsDBSuffix = ".intents";

// ============================================================================
//  Tablet Metadata
// ============================================================================

TableInfo::TableInfo(std::string table_id,
                     std::string table_name,
                     TableType table_type,
                     const Schema& schema,
                     const IndexMap& index_map,
                     const boost::optional<IndexInfo>& index_info,
                     const uint32_t schema_version,
                     PartitionSchema partition_schema)
    : table_id(std::move(table_id)),
      table_name(std::move(table_name)),
      table_type(table_type),
      schema(schema),
      index_map(index_map),
      index_info(index_info ? new IndexInfo(*index_info) : nullptr),
      schema_version(schema_version),
      partition_schema(std::move(partition_schema)) {
}

TableInfo::TableInfo(const TableInfo& other,
                     const Schema& schema,
                     const IndexMap& index_map,
                     const std::vector<DeletedColumn>& deleted_cols,
                     const uint32_t schema_version)
    : table_id(other.table_id),
      table_name(other.table_name),
      table_type(other.table_type),
      schema(schema),
      index_map(index_map),
      index_info(other.index_info ? new IndexInfo(*other.index_info) : nullptr),
      schema_version(schema_version),
      partition_schema(other.partition_schema),
      deleted_cols(other.deleted_cols) {
  this->deleted_cols.insert(this->deleted_cols.end(), deleted_cols.begin(), deleted_cols.end());
}

Status TableInfo::LoadFromSuperBlock(const TabletSuperBlockPB& superblock) {
  table_id = superblock.primary_table_id();
  table_name = superblock.deprecated_table_name();
  table_type = superblock.deprecated_table_type();

  RETURN_NOT_OK(SchemaFromPB(superblock.deprecated_schema(), &schema));
  if (superblock.has_deprecated_index_info()) {
    index_info.reset(new IndexInfo(superblock.deprecated_index_info()));
  }
  index_map.FromPB(superblock.deprecated_indexes());
  schema_version = superblock.deprecated_schema_version();

  RETURN_NOT_OK(PartitionSchema::FromPB(superblock.deprecated_partition_schema(),
                                        schema, &partition_schema));

  for (const DeletedColumnPB& deleted_col : superblock.deprecated_deleted_cols()) {
    DeletedColumn col;
    RETURN_NOT_OK(DeletedColumn::FromPB(deleted_col, &col));
    deleted_cols.push_back(col);
  }

  return Status::OK();
}

void TableInfo::ToSuperBlock(TabletSuperBlockPB* superblock) {
  superblock->set_primary_table_id(table_id);
  superblock->set_deprecated_table_name(table_name);
  superblock->set_deprecated_table_type(table_type);

  DCHECK(schema.has_column_ids());
  SchemaToPB(schema, superblock->mutable_deprecated_schema());
  if (index_info) {
    index_info->ToPB(superblock->mutable_deprecated_index_info());
  }
  index_map.ToPB(superblock->mutable_deprecated_indexes());
  superblock->set_deprecated_schema_version(schema_version);

  partition_schema.ToPB(superblock->mutable_deprecated_partition_schema());

  for (const DeletedColumn& deleted_col : deleted_cols) {
    deleted_col.CopyToPB(superblock->mutable_deprecated_deleted_cols()->Add());
  }
}

Status TableInfo::LoadFromPB(const TableInfoPB& pb) {
  table_id = pb.table_id();
  table_name = pb.table_name();
  table_type = pb.table_type();

  RETURN_NOT_OK(SchemaFromPB(pb.schema(), &schema));
  if (pb.has_index_info()) {
    index_info.reset(new IndexInfo(pb.index_info()));
  }
  index_map.FromPB(pb.indexes());
  schema_version = pb.schema_version();

  RETURN_NOT_OK(PartitionSchema::FromPB(pb.partition_schema(), schema, &partition_schema));

  for (const DeletedColumnPB& deleted_col : pb.deleted_cols()) {
    DeletedColumn col;
    RETURN_NOT_OK(DeletedColumn::FromPB(deleted_col, &col));
    deleted_cols.push_back(col);
  }

  return Status::OK();
}

void TableInfo::ToPB(TableInfoPB* pb) const {
  pb->set_table_id(table_id);
  pb->set_table_name(table_name);
  pb->set_table_type(table_type);

  DCHECK(schema.has_column_ids());
  SchemaToPB(schema, pb->mutable_schema());
  if (index_info) {
    index_info->ToPB(pb->mutable_index_info());
  }
  index_map.ToPB(pb->mutable_indexes());
  pb->set_schema_version(schema_version);

  partition_schema.ToPB(pb->mutable_partition_schema());

  for (const DeletedColumn& deleted_col : deleted_cols) {
    deleted_col.CopyToPB(pb->mutable_deleted_cols()->Add());
  }
}

// ============================================================================

Status TabletMetadata::CreateNew(FsManager* fs_manager,
                                 const string& table_id,
                                 const string& tablet_id,
                                 const string& table_name,
                                 const TableType table_type,
                                 const Schema& schema,
                                 const IndexMap& index_map,
                                 const PartitionSchema& partition_schema,
                                 const Partition& partition,
                                 const boost::optional<IndexInfo>& index_info,
                                 const uint32_t schema_version,
                                 const TabletDataState& initial_tablet_data_state,
                                 scoped_refptr<TabletMetadata>* metadata,
                                 const string& data_root_dir,
                                 const string& wal_root_dir) {

  // Verify that no existing tablet exists with the same ID.
  if (fs_manager->env()->FileExists(fs_manager->GetTabletMetadataPath(tablet_id))) {
    return STATUS(AlreadyPresent, "Tablet already exists", tablet_id);
  }

  auto wal_top_dir = wal_root_dir;
  auto data_top_dir = data_root_dir;
  // Use the original randomized logic if the indices are not explicitly passed in
  yb::Random rand(GetCurrentTimeMicros());
  if (data_root_dir.empty()) {
    auto data_root_dirs = fs_manager->GetDataRootDirs();
    CHECK(!data_root_dirs.empty()) << "No data root directories found";
    data_top_dir = data_root_dirs[rand.Uniform(data_root_dirs.size())];
  }

  if (wal_root_dir.empty()) {
    auto wal_root_dirs = fs_manager->GetWalRootDirs();
    CHECK(!wal_root_dirs.empty()) << "No wal root directories found";
    wal_top_dir = wal_root_dirs[rand.Uniform(wal_root_dirs.size())];
  }

  auto table_dir = Substitute("table-$0", table_id);
  auto tablet_dir = Substitute("tablet-$0", tablet_id);

  auto wal_table_top_dir = JoinPathSegments(wal_top_dir, table_dir);
  auto wal_dir = JoinPathSegments(wal_table_top_dir, tablet_dir);

  auto rocksdb_dir = JoinPathSegments(
      data_top_dir, FsManager::kRocksDBDirName, table_dir, tablet_dir);

  scoped_refptr<TabletMetadata> ret(new TabletMetadata(fs_manager,
                                                       table_id,
                                                       tablet_id,
                                                       table_name,
                                                       table_type,
                                                       rocksdb_dir,
                                                       wal_dir,
                                                       schema,
                                                       index_map,
                                                       partition_schema,
                                                       partition,
                                                       index_info,
                                                       schema_version,
                                                       initial_tablet_data_state));
  RETURN_NOT_OK(ret->Flush());
  metadata->swap(ret);
  return Status::OK();
}

Status TabletMetadata::Load(FsManager* fs_manager,
                            const string& tablet_id,
                            scoped_refptr<TabletMetadata>* metadata) {
  scoped_refptr<TabletMetadata> ret(new TabletMetadata(fs_manager, tablet_id));
  RETURN_NOT_OK(ret->LoadFromDisk());
  metadata->swap(ret);
  return Status::OK();
}

Status TabletMetadata::LoadOrCreate(FsManager* fs_manager,
                                    const string& table_id,
                                    const string& tablet_id,
                                    const string& table_name,
                                    TableType table_type,
                                    const Schema& schema,
                                    const PartitionSchema& partition_schema,
                                    const Partition& partition,
                                    const boost::optional<IndexInfo>& index_info,
                                    const TabletDataState& initial_tablet_data_state,
                                    scoped_refptr<TabletMetadata>* metadata) {
  Status s = Load(fs_manager, tablet_id, metadata);
  if (s.ok()) {
    if (!(*metadata)->schema().Equals(schema)) {
      return STATUS(Corruption, Substitute("Schema on disk ($0) does not "
        "match expected schema ($1)", (*metadata)->schema().ToString(),
        schema.ToString()));
    }
    return Status::OK();
  } else if (s.IsNotFound()) {
    return CreateNew(fs_manager, table_id, tablet_id, table_name, table_type,
                     schema, IndexMap(), partition_schema, partition, index_info,
                     0 /* schema_version */, initial_tablet_data_state, metadata);
  } else {
    return s;
  }
}

template <class TablesMap>
CHECKED_STATUS MakeTableNotFound(const std::string& table_id, const std::string& tablet_id,
                                 const TablesMap& tables) {
  std::string suffix;
#ifndef NDEBUG
  suffix = Format(". Tables: $0.", tables);
#endif
  return STATUS_FORMAT(NotFound, "Table $0 not found in tablet $1$2", table_id, tablet_id, suffix);
}

Result<const TableInfo*> TabletMetadata::GetTableInfo(const std::string& table_id) const {
  std::lock_guard<LockType> l(data_lock_);
  const auto iter = tables_.find(!table_id.empty() ? table_id : primary_table_id_);
  if (iter == tables_.end()) {
    return MakeTableNotFound(table_id, tablet_id_, tables_);
  }
  return iter->second.get();
}

Result<TableInfo*> TabletMetadata::GetTableInfo(const std::string& table_id) {
  std::lock_guard<LockType> l(data_lock_);
  const auto iter = tables_.find(!table_id.empty() ? table_id : primary_table_id_);
  if (iter == tables_.end()) {
    return MakeTableNotFound(table_id, tablet_id_, tables_);
  }
  return iter->second.get();
}

Status TabletMetadata::DeleteTabletData(TabletDataState delete_type,
                                        const yb::OpId& last_logged_opid) {
  CHECK(delete_type == TABLET_DATA_DELETED ||
        delete_type == TABLET_DATA_TOMBSTONED)
      << "DeleteTabletData() called with unsupported delete_type on tablet "
      << tablet_id_ << ": " << TabletDataState_Name(delete_type)
      << " (" << delete_type << ")";

  // First add all of our blocks to the orphan list
  // and clear our rowsets. This serves to erase all the data.
  //
  // We also set the state in our persisted metadata to indicate that
  // we have been deleted.
  {
    std::lock_guard<LockType> l(data_lock_);
    tablet_data_state_ = delete_type;
    if (last_logged_opid) {
      tombstone_last_logged_opid_ = last_logged_opid;
    }
  }

  rocksdb::Options rocksdb_options;
  TabletOptions tablet_options;
  docdb::InitRocksDBOptions(
      &rocksdb_options, tablet_id_, nullptr /* statistics */, tablet_options);

  LOG(INFO) << "Destroying regular db at: " << rocksdb_dir_;
  rocksdb::Status status = rocksdb::DestroyDB(rocksdb_dir_, rocksdb_options);

  if (!status.ok()) {
    LOG(ERROR) << "Failed to destroy regular DB at: " << rocksdb_dir_ << ": " << status;
  } else {
    LOG(INFO) << "Successfully destroyed regular DB at: " << rocksdb_dir_;
  }

  auto intents_dir = rocksdb_dir_ + kIntentsDBSuffix;
  if (fs_manager_->env()->FileExists(intents_dir)) {
    auto status = rocksdb::DestroyDB(intents_dir, rocksdb_options);

    if (!status.ok()) {
      LOG(ERROR) << "Failed to destroy provisional records DB at: " << intents_dir << ": "
                 << status;
    } else {
      LOG(INFO) << "Successfully destroyed provisional records DB at: " << intents_dir;
    }
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

Status TabletMetadata::DeleteSuperBlock() {
  std::lock_guard<LockType> l(data_lock_);
  if (tablet_data_state_ != TABLET_DATA_DELETED) {
    return STATUS(IllegalState,
        Substitute("Tablet $0 is not in TABLET_DATA_DELETED state. "
                   "Call DeleteTabletData(TABLET_DATA_DELETED) first. "
                   "Tablet data state: $1 ($2)",
                   tablet_id_,
                   TabletDataState_Name(tablet_data_state_),
                   tablet_data_state_));
  }

  string path = fs_manager_->GetTabletMetadataPath(tablet_id_);
  RETURN_NOT_OK_PREPEND(fs_manager_->env()->DeleteFile(path),
                        "Unable to delete superblock for tablet " + tablet_id_);
  return Status::OK();
}

TabletMetadata::TabletMetadata(FsManager* fs_manager,
                               string table_id,
                               string tablet_id,
                               string table_name,
                               TableType table_type,
                               const string rocksdb_dir,
                               const string wal_dir,
                               const Schema& schema,
                               const IndexMap& index_map,
                               PartitionSchema partition_schema,
                               Partition partition,
                               const boost::optional<IndexInfo>& index_info,
                               const uint32_t schema_version,
                               const TabletDataState& tablet_data_state)
    : state_(kNotWrittenYet),
      tablet_id_(std::move(tablet_id)),
      partition_(std::move(partition)),
      fs_manager_(fs_manager),
      last_durable_mrs_id_(kNoDurableMemStore),
      rocksdb_dir_(rocksdb_dir),
      wal_dir_(wal_dir),
      tablet_data_state_(tablet_data_state) {
  CHECK(schema.has_column_ids());
  CHECK_GT(schema.num_key_columns(), 0);
  primary_table_id_ = table_id;
  tables_.emplace(primary_table_id_,
                  std::make_unique<TableInfo>(std::move(table_id),
                                              std::move(table_name),
                                              table_type,
                                              schema,
                                              index_map,
                                              index_info,
                                              schema_version,
                                              std::move(partition_schema)));
}

TabletMetadata::~TabletMetadata() {
}

TabletMetadata::TabletMetadata(FsManager* fs_manager, string tablet_id)
    : state_(kNotLoadedYet),
      tablet_id_(std::move(tablet_id)),
      fs_manager_(fs_manager) {
}

Status TabletMetadata::LoadFromDisk() {
  TRACE_EVENT1("tablet", "TabletMetadata::LoadFromDisk",
               "tablet_id", tablet_id_);

  CHECK_EQ(state_, kNotLoadedYet);

  TabletSuperBlockPB superblock;
  RETURN_NOT_OK(ReadSuperBlockFromDisk(&superblock));
  RETURN_NOT_OK_PREPEND(LoadFromSuperBlock(superblock),
                        "Failed to load data from superblock protobuf");
  state_ = kInitialized;
  return Status::OK();
}

Status TabletMetadata::LoadFromSuperBlock(const TabletSuperBlockPB& superblock) {
  VLOG(2) << "Loading TabletMetadata from SuperBlockPB:" << std::endl
          << superblock.DebugString();

  bool need_flush = false;
  {
    std::lock_guard<LockType> l(data_lock_);

    // Verify that the tablet id matches with the one in the protobuf
    if (superblock.tablet_id() != tablet_id_) {
      return STATUS(Corruption, "Expected id=" + tablet_id_ +
                                " found " + superblock.tablet_id(),
                                superblock.DebugString());
    }
    Partition::FromPB(superblock.partition(), &partition_);
    for (const TableInfoPB& pb : superblock.tables()) {
      std::unique_ptr<TableInfo> table_info(new TableInfo());
      RETURN_NOT_OK(table_info->LoadFromPB(pb));
      if (table_info->table_id != superblock.primary_table_id()) {
        Uuid cotable_id;
        CHECK_OK(cotable_id.FromHexString(table_info->table_id));
        table_info->schema.set_cotable_id(cotable_id);
      }
      tables_[table_info->table_id] = std::move(table_info);
    }

    last_durable_mrs_id_ = superblock.last_durable_mrs_id();
    rocksdb_dir_ = superblock.rocksdb_dir();
    wal_dir_ = superblock.wal_dir();
    tablet_data_state_ = superblock.tablet_data_state();

    if (superblock.has_tombstone_last_logged_opid()) {
      tombstone_last_logged_opid_ = yb::OpId::FromPB(superblock.tombstone_last_logged_opid());
    } else {
      tombstone_last_logged_opid_ = OpId();
    }

    std::unique_ptr<TableInfo> table_info(new TableInfo());
    RETURN_NOT_OK(table_info->LoadFromSuperBlock(superblock));
    primary_table_id_ = table_info->table_id;
    if (tables_.count(primary_table_id_) == 0) {
      tables_[primary_table_id_] = std::move(table_info);
      need_flush = true;
    }
  }

  if (need_flush) {
    RETURN_NOT_OK(Flush());
  }

  return Status::OK();
}

Status TabletMetadata::Flush() {
  TRACE_EVENT1("tablet", "TabletMetadata::Flush",
               "tablet_id", tablet_id_);

  MutexLock l_flush(flush_lock_);
  TabletSuperBlockPB pb;
  {
    std::lock_guard<LockType> l(data_lock_);
    ToSuperBlockUnlocked(&pb);
  }
  RETURN_NOT_OK(ReplaceSuperBlockUnlocked(pb));
  TRACE("Metadata flushed");

  return Status::OK();
}

Status TabletMetadata::ReplaceSuperBlock(const TabletSuperBlockPB &pb) {
  {
    MutexLock l(flush_lock_);
    RETURN_NOT_OK_PREPEND(ReplaceSuperBlockUnlocked(pb), "Unable to replace superblock");
  }

  RETURN_NOT_OK_PREPEND(LoadFromSuperBlock(pb),
                        "Failed to load data from superblock protobuf");

  return Status::OK();
}

Status TabletMetadata::ReplaceSuperBlockUnlocked(const TabletSuperBlockPB &pb) {
  flush_lock_.AssertAcquired();

  string path = fs_manager_->GetTabletMetadataPath(tablet_id_);
  RETURN_NOT_OK_PREPEND(pb_util::WritePBContainerToPath(
                            fs_manager_->env(), path, pb,
                            pb_util::OVERWRITE, pb_util::SYNC),
                        Substitute("Failed to write tablet metadata $0", tablet_id_));

  return Status::OK();
}

Status TabletMetadata::ReadSuperBlockFromDisk(TabletSuperBlockPB* superblock) const {
  string path = fs_manager_->GetTabletMetadataPath(tablet_id_);
  RETURN_NOT_OK_PREPEND(
      pb_util::ReadPBContainerFromPath(fs_manager_->env(), path, superblock),
      Substitute("Could not load tablet metadata from $0", path));
  if (superblock->deprecated_table_type() == TableType::REDIS_TABLE_TYPE &&
      superblock->deprecated_table_name() == kTransactionsTableName) {
    superblock->set_deprecated_table_type(TableType::TRANSACTION_STATUS_TABLE_TYPE);
  }
  return Status::OK();
}

void TabletMetadata::ToSuperBlock(TabletSuperBlockPB* superblock) const {
  // acquire the lock so that rowsets_ doesn't get changed until we're finished.
  std::lock_guard<LockType> l(data_lock_);
  ToSuperBlockUnlocked(superblock);
}

void TabletMetadata::ToSuperBlockUnlocked(TabletSuperBlockPB* superblock) const {
  DCHECK(data_lock_.is_locked());
  // Convert to protobuf
  TabletSuperBlockPB pb;
  pb.set_tablet_id(tablet_id_);
  partition_.ToPB(pb.mutable_partition());

  for (const auto& iter : tables_) {
    iter.second->ToPB(pb.add_tables());
  }

  pb.set_last_durable_mrs_id(last_durable_mrs_id_);
  pb.set_rocksdb_dir(rocksdb_dir_);
  pb.set_wal_dir(wal_dir_);
  pb.set_tablet_data_state(tablet_data_state_);
  if (tombstone_last_logged_opid_) {
    tombstone_last_logged_opid_.ToPB(pb.mutable_tombstone_last_logged_opid());
  }

  tables_.find(primary_table_id_)->second->ToSuperBlock(&pb);

  superblock->Swap(&pb);
}

void TabletMetadata::SetSchema(const Schema& schema,
                               const IndexMap& index_map,
                               const std::vector<DeletedColumn>& deleted_cols,
                               const uint32_t version) {
  DCHECK(schema.has_column_ids());
  std::unique_ptr<TableInfo> new_table_info(new TableInfo(*primary_table_info(),
                                                          schema,
                                                          index_map,
                                                          deleted_cols,
                                                          version));
  std::lock_guard<LockType> l(data_lock_);
  tables_[primary_table_id_].swap(new_table_info);
  if (new_table_info) {
    old_tables_.push_back(std::move(new_table_info));
  }
}

void TabletMetadata::SetPartitionSchema(const PartitionSchema& partition_schema) {
  std::lock_guard<LockType> l(data_lock_);
  DCHECK(tables_.find(primary_table_id_) != tables_.end());
  tables_[primary_table_id_]->partition_schema = partition_schema;
}

void TabletMetadata::SetTableName(const string& table_name) {
  std::lock_guard<LockType> l(data_lock_);
  tables_[primary_table_id_]->table_name = table_name;
}

void TabletMetadata::AddTable(const std::string& table_id,
                              const std::string& table_name,
                              const TableType table_type,
                              const Schema& schema,
                              const IndexMap& index_map,
                              const PartitionSchema& partition_schema,
                              const boost::optional<IndexInfo>& index_info,
                              const uint32_t schema_version) {
  DCHECK(schema.has_column_ids());
  std::unique_ptr<TableInfo> new_table_info(new TableInfo(table_id,
                                                          table_name,
                                                          table_type,
                                                          schema,
                                                          index_map,
                                                          index_info,
                                                          schema_version,
                                                          partition_schema));
  if (table_id != primary_table_id_) {
    Uuid cotable_id;
    CHECK_OK(cotable_id.FromHexString(table_id));
    new_table_info->schema.set_cotable_id(cotable_id);
  }
  std::lock_guard<LockType> l(data_lock_);
  tables_[table_id].swap(new_table_info);
  DCHECK(!new_table_info) << "table " << table_id << " already exists";
}

string TabletMetadata::data_root_dir() const {
  if (rocksdb_dir_.empty()) {
    return "";
  } else {
    auto data_root_dir = DirName(DirName(rocksdb_dir_));
    if (strcmp(BaseName(data_root_dir).c_str(), FsManager::kRocksDBDirName) == 0) {
      data_root_dir = DirName(data_root_dir);
    }
    return data_root_dir;
  }
}

string TabletMetadata::wal_root_dir() const {
  if (wal_dir_.empty()) {
    return "";
  } else {
    auto wal_root_dir = DirName(wal_dir_);
    if (strcmp(BaseName(wal_root_dir).c_str(), FsManager::kWalDirName) != 0) {
      wal_root_dir = DirName(wal_root_dir);
    }
    return wal_root_dir;
  }
}

void TabletMetadata::set_tablet_data_state(TabletDataState state) {
  std::lock_guard<LockType> l(data_lock_);
  tablet_data_state_ = state;
}

string TabletMetadata::LogPrefix() const {
  return Substitute("T $0 P $1: ", tablet_id_, fs_manager_->uuid());
}

TabletDataState TabletMetadata::tablet_data_state() const {
  std::lock_guard<LockType> l(data_lock_);
  return tablet_data_state_;
}

} // namespace tablet
} // namespace yb
