//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#include "yb/yql/pggate/pg_ddl.h"

#include "yb/client/yb_table_name.h"

#include "yb/common/common.pb.h"
#include "yb/common/constants.h"
#include "yb/common/entity_ids.h"
#include "yb/common/pg_system_attr.h"

#include "yb/util/flags.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/tsan_util.h"

#include "yb/yql/pggate/pg_client.h"

DEFINE_test_flag(int32, user_ddl_operation_timeout_sec, 0,
                 "Adjusts the timeout for a DDL operation from the YBClient default, if non-zero.");

DECLARE_int32(max_num_tablets_for_table);
DECLARE_int32(yb_client_admin_operation_timeout_sec);

namespace yb::pggate {

using namespace std::literals;  // NOLINT

// TODO(neil) This should be derived from a GFLAGS.
static MonoDelta kDdlTimeout = 60s * kTimeMultiplier;

namespace {

CoarseTimePoint DdlDeadline() {
  auto timeout = MonoDelta::FromSeconds(FLAGS_TEST_user_ddl_operation_timeout_sec);
  if (timeout == MonoDelta::kZero) {
    timeout = kDdlTimeout;
  }
  return CoarseMonoClock::now() + timeout;
}

// Make a special case for create database because it is a well-known slow operation in YB.
CoarseTimePoint CreateDatabaseDeadline() {
  int32 timeout = FLAGS_TEST_user_ddl_operation_timeout_sec;
  if (timeout == 0) {
    timeout = FLAGS_yb_client_admin_operation_timeout_sec *
              RegularBuildVsDebugVsSanitizers(1, 2, 2);
  }
  return CoarseMonoClock::now() + MonoDelta::FromSeconds(timeout);
}

} // namespace

//--------------------------------------------------------------------------------------------------
// PgCreateDatabase
//--------------------------------------------------------------------------------------------------

PgCreateDatabase::PgCreateDatabase(const PgSession::ScopedRefPtr& pg_session,
                                   const char *database_name,
                                   PgOid database_oid,
                                   PgOid source_database_oid,
                                   PgOid next_oid,
                                   YbcCloneInfo *yb_clone_info,
                                   bool colocated,
                                   bool use_transaction)
    : BaseType(pg_session) {
  req_.set_database_name(database_name);
  req_.set_database_oid(database_oid);
  req_.set_source_database_oid(source_database_oid);
  req_.set_next_oid(next_oid);
  req_.set_colocated(colocated);
  req_.set_use_transaction(use_transaction);
  if (yb_clone_info) {
    req_.set_source_database_name(yb_clone_info->src_db_name);
    req_.set_clone_time(yb_clone_info->clone_time);
    req_.set_source_owner(yb_clone_info->src_owner);
    req_.set_target_owner(yb_clone_info->tgt_owner);
  }
}

Status PgCreateDatabase::Exec() {
  return pg_session_->pg_client().CreateDatabase(&req_, CreateDatabaseDeadline());
}

PgDropDatabase::PgDropDatabase(
    const PgSession::ScopedRefPtr& pg_session, const char* database_name, PgOid database_oid)
    : BaseType(pg_session),
      database_name_(database_name),
      database_oid_(database_oid) {
}

Status PgDropDatabase::Exec() {
  return pg_session_->DropDatabase(database_name_, database_oid_);
}

PgAlterDatabase::PgAlterDatabase(
    const PgSession::ScopedRefPtr& pg_session, const char* database_name, PgOid database_oid)
    : BaseType(pg_session) {
  req_.set_database_name(database_name);
  req_.set_database_oid(database_oid);
}

Status PgAlterDatabase::Exec() {
  return pg_session_->pg_client().AlterDatabase(&req_, DdlDeadline());
}

void PgAlterDatabase::RenameDatabase(const char *newname) {
  req_.set_new_name(newname);
}

//--------------------------------------------------------------------------------------------------
// PgCreateTablegroup / PgDropTablegroup
//--------------------------------------------------------------------------------------------------

PgCreateTablegroup::PgCreateTablegroup(
    const PgSession::ScopedRefPtr& pg_session, const char* database_name, const PgOid database_oid,
    const PgOid tablegroup_oid, const PgOid tablespace_oid)
    : BaseType(pg_session) {
  req_.set_database_name(database_name);
  PgObjectId(database_oid, tablegroup_oid).ToPB(req_.mutable_tablegroup_id());
  PgObjectId(database_oid, tablespace_oid).ToPB(req_.mutable_tablespace_id());
}

Status PgCreateTablegroup::Exec() {
  return pg_session_->pg_client().CreateTablegroup(&req_, DdlDeadline());
}

PgDropTablegroup::PgDropTablegroup(
    const PgSession::ScopedRefPtr& pg_session, PgOid database_oid, PgOid tablegroup_oid)
    : BaseType(pg_session) {
  PgObjectId(database_oid, tablegroup_oid).ToPB(req_.mutable_tablegroup_id());
}

Status PgDropTablegroup::Exec() {
  return pg_session_->pg_client().DropTablegroup(&req_, DdlDeadline());
}

//--------------------------------------------------------------------------------------------------
// PgCreateTable
//--------------------------------------------------------------------------------------------------

PgCreateTableBase::PgCreateTableBase(
    const PgSession::ScopedRefPtr& pg_session,
    const char* database_name,
    const char* schema_name,
    const char* table_name,
    const PgObjectId& table_id,
    bool is_shared_table,
    bool is_sys_catalog_table,
    bool if_not_exist,
    YbcPgYbrowidMode ybrowid_mode,
    bool is_colocated_via_database,
    const PgObjectId& tablegroup_oid,
    ColocationId colocation_id,
    const PgObjectId& tablespace_oid,
    bool is_matview,
    const PgObjectId& pg_table_oid,
    const PgObjectId& old_relfilenode_oid,
    bool is_truncate,
    bool use_transaction)
    : PgDdl(pg_session) {
  table_id.ToPB(req_.mutable_table_id());
  req_.set_database_name(database_name);
  req_.set_table_name(table_name);
  req_.set_num_tablets(-1);
  req_.set_is_pg_catalog_table(is_sys_catalog_table);
  req_.set_is_shared_table(is_shared_table);
  req_.set_if_not_exist(if_not_exist);
  req_.set_is_colocated_via_database(is_colocated_via_database);
  req_.set_schema_name(schema_name);
  tablegroup_oid.ToPB(req_.mutable_tablegroup_oid());
  if (colocation_id != kColocationIdNotSet) {
    req_.set_colocation_id(colocation_id);
  }
  tablespace_oid.ToPB(req_.mutable_tablespace_oid());
  req_.set_is_matview(is_matview);
  pg_table_oid.ToPB(req_.mutable_pg_table_oid());
  old_relfilenode_oid.ToPB(req_.mutable_old_relfilenode_oid());
  req_.set_is_truncate(is_truncate);
  req_.set_use_transaction(use_transaction);

  // Add internal primary key column to a Postgres table without a user-specified primary key.
  switch (ybrowid_mode) {
    case PG_YBROWID_MODE_NONE:
      return;
    case PG_YBROWID_MODE_HASH: FALLTHROUGH_INTENDED;
    case PG_YBROWID_MODE_RANGE:
      bool is_hash = ybrowid_mode == PG_YBROWID_MODE_HASH;
      CHECK_OK(AddColumn("ybrowid", static_cast<int32_t>(PgSystemAttrNum::kYBRowId),
                         YB_YQL_DATA_TYPE_BINARY, is_hash, true /* is_range */));
      break;
  }
}

Status PgCreateTableBase::AddColumnImpl(
    const char* attr_name, int attr_num, int attr_ybtype, int pg_type_oid, bool is_hash,
    bool is_range, SortingType sorting_type) {
  auto& column = *req_.mutable_create_columns()->Add();
  column.set_attr_name(attr_name);
  column.set_attr_num(attr_num);
  column.set_attr_ybtype(attr_ybtype);
  column.set_is_hash(is_hash);
  column.set_is_range(is_range);
  column.set_sorting_type(to_underlying(sorting_type));
  column.set_attr_pgoid(pg_type_oid);
  return Status::OK();
}

Status PgCreateTableBase::SetNumTablets(int32_t num_tablets) {
  if (num_tablets < 0) {
    return STATUS(InvalidArgument, "num_tablets cannot be less than zero");
  }
  if (num_tablets > FLAGS_max_num_tablets_for_table) {
    return STATUS(InvalidArgument, "num_tablets exceeds system limit");
  }

  req_.set_num_tablets(num_tablets);
  return Status::OK();
}

Status PgCreateTableBase::SetVectorOptions(YbcPgVectorIdxOptions* options) {
  auto options_pb = req_.mutable_vector_idx_options();
  options_pb->set_dist_type(static_cast<PgVectorDistanceType>(options->dist_type));
  options_pb->set_idx_type(static_cast<PgVectorIndexType>(options->idx_type));
  options_pb->set_dimensions(options->dimensions);

  PgTable table(VERIFY_RESULT(pg_session_->LoadTable(PgObjectId::FromPB(req_.base_table_id()))));
  options_pb->set_column_id(VERIFY_RESULT_REF(table.ColumnForAttr(options->attnum)).id());

  req_.set_is_unique_index(false);

  if (options->idx_type == YbcPgVectorIdxType::YB_VEC_DUMMY) {
    // Disable multi-tablet for this for now.
    RETURN_NOT_OK(SetNumTablets(1));
  }
  return Status::OK();
}

Status PgCreateTableBase::AddSplitBoundary(PgExpr** exprs, int expr_count) {
  auto* values = req_.mutable_split_bounds()->Add()->mutable_values();
  for (int i = 0; i < expr_count; ++i) {
    auto temp_value = VERIFY_RESULT(exprs[i]->Eval());
    auto out = values->Add();
    if (temp_value) {
      temp_value->ToGoogleProtobuf(out);
    }
  }
  return Status::OK();
}

Status PgCreateTableBase::Exec() {
  RETURN_NOT_OK(pg_session_->pg_client().CreateTable(&req_, DdlDeadline()));

  const auto base_table_id = PgObjectId::FromPB(req_.base_table_id());
  if (base_table_id.IsValid()) {
    pg_session_->InvalidateTableCache(base_table_id, InvalidateOnPgClient::kFalse);
  }
  return Status::OK();
}

PgCreateTable::PgCreateTable(
    const PgSession::ScopedRefPtr& pg_session,
    const char* database_name,
    const char* schema_name,
    const char* table_name,
    const PgObjectId& table_id,
    bool is_shared_table,
    bool is_sys_catalog_table,
    bool if_not_exist,
    YbcPgYbrowidMode ybrowid_mode,
    bool is_colocated_via_database,
    const PgObjectId& tablegroup_oid,
    ColocationId colocation_id,
    const PgObjectId& tablespace_oid,
    bool is_matview,
    const PgObjectId& pg_table_oid,
    const PgObjectId& old_relfilenode_oid,
    bool is_truncate,
    bool use_transaction)
    : BaseType(
          pg_session, database_name, schema_name, table_name, table_id, is_shared_table,
          is_sys_catalog_table, if_not_exist, ybrowid_mode, is_colocated_via_database,
          tablegroup_oid, colocation_id, tablespace_oid, is_matview, pg_table_oid,
          old_relfilenode_oid, is_truncate, use_transaction) {}

PgCreateIndex::PgCreateIndex(
    const PgSession::ScopedRefPtr& pg_session,
    const char* database_name,
    const char* schema_name,
    const char* table_name,
    const PgObjectId& table_id,
    bool is_shared_table,
    bool is_sys_catalog_table,
    bool if_not_exist,
    YbcPgYbrowidMode ybrowid_mode,
    bool is_colocated_via_database,
    const PgObjectId& tablegroup_oid,
    ColocationId colocation_id,
    const PgObjectId& tablespace_oid,
    bool is_matview,
    const PgObjectId& pg_table_oid,
    const PgObjectId& old_relfilenode_oid,
    bool is_truncate,
    bool use_transaction,
    const PgObjectId& base_table_id,
    bool is_unique_index,
    bool skip_index_backfill)
    : BaseType(
          pg_session, database_name, schema_name, table_name, table_id, is_shared_table,
          is_sys_catalog_table, if_not_exist, ybrowid_mode, is_colocated_via_database,
          tablegroup_oid, colocation_id, tablespace_oid, is_matview, pg_table_oid,
          old_relfilenode_oid, is_truncate, use_transaction) {
  base_table_id.ToPB(req_.mutable_base_table_id());
  req_.set_is_unique_index(is_unique_index);
  req_.set_skip_index_backfill(skip_index_backfill);
}

//--------------------------------------------------------------------------------------------------
// PgDropTable
//--------------------------------------------------------------------------------------------------

PgDropTable::PgDropTable(
    const PgSession::ScopedRefPtr& pg_session, const PgObjectId& table_id, bool if_exist)
    : BaseType(pg_session), table_id_(table_id), if_exist_(if_exist) {
}

Status PgDropTable::Exec() {
  Status s = pg_session_->DropTable(table_id_);
  pg_session_->InvalidateTableCache(table_id_, InvalidateOnPgClient::kFalse);
  if (s.ok() || (s.IsNotFound() && if_exist_)) {
    return Status::OK();
  }
  return s;
}

//--------------------------------------------------------------------------------------------------
// PgTruncateTable
//--------------------------------------------------------------------------------------------------

PgTruncateTable::PgTruncateTable(
    const PgSession::ScopedRefPtr& pg_session, const PgObjectId& table_id)
    : BaseType(pg_session) {
  table_id.ToPB(req_.mutable_table_id());
}

Status PgTruncateTable::Exec() {
  return pg_session_->pg_client().TruncateTable(&req_, DdlDeadline());
}

//--------------------------------------------------------------------------------------------------
// PgDropIndex
//--------------------------------------------------------------------------------------------------

PgDropIndex::PgDropIndex(
    const PgSession::ScopedRefPtr& pg_session, const PgObjectId& index_id, bool if_exist,
    bool ddl_rollback_enabled)
    : BaseType(pg_session),
      index_id_(index_id), if_exist_(if_exist), ddl_rollback_enabled_(ddl_rollback_enabled) {
}

Status PgDropIndex::Exec() {
  client::YBTableName indexed_table_name;
  auto s = pg_session_->DropIndex(index_id_, &indexed_table_name);
  if (s.ok() || (s.IsNotFound() && if_exist_)) {
    RSTATUS_DCHECK(!indexed_table_name.empty(), Uninitialized, "indexed_table_name uninitialized");
    PgObjectId indexed_table_id(indexed_table_name.table_id());

    pg_session_->InvalidateTableCache(index_id_, InvalidateOnPgClient::kFalse);
    pg_session_->InvalidateTableCache(indexed_table_id,
        ddl_rollback_enabled_ ? InvalidateOnPgClient::kTrue : InvalidateOnPgClient::kFalse);
    return Status::OK();
  }
  return s;
}

//--------------------------------------------------------------------------------------------------
// PgAlterTable
//--------------------------------------------------------------------------------------------------

PgAlterTable::PgAlterTable(
    const PgSession::ScopedRefPtr& pg_session, const PgObjectId& table_id, bool use_transaction)
    : BaseType(pg_session) {
  table_id.ToPB(req_.mutable_table_id());
  req_.set_use_transaction(use_transaction);
}

Status PgAlterTable::AddColumn(const char *name,
                               const YbcPgTypeEntity *attr_type,
                               int order,
                               YbcPgExpr missing_value) {
  auto& col = *req_.mutable_add_columns()->Add();
  col.set_attr_name(name);
  col.set_attr_ybtype(attr_type->yb_type);
  col.set_attr_num(order);
  col.set_attr_pgoid(attr_type->type_oid);
  if (missing_value) {
    auto value = VERIFY_RESULT(missing_value->Eval());
    value->ToGoogleProtobuf(col.mutable_attr_missing_val());
  }
  return Status::OK();
}

Status PgAlterTable::RenameColumn(const char *oldname, const char *newname) {
  auto& rename = *req_.mutable_rename_columns()->Add();
  rename.set_old_name(oldname);
  rename.set_new_name(newname);
  return Status::OK();
}

Status PgAlterTable::DropColumn(const char *name) {
  req_.mutable_drop_columns()->Add(name);
  return Status::OK();
}

Status PgAlterTable::SetReplicaIdentity(const char identity_type) {
  auto replica_identity_pb = std::make_unique<tserver::PgReplicaIdentityPB>();
  tserver::PgReplicaIdentityType replica_identity_type;
  switch (identity_type) {
    case 'd': replica_identity_type = tserver::DEFAULT; break;
    case 'n': replica_identity_type = tserver::NOTHING; break;
    case 'f': replica_identity_type = tserver::FULL; break;
    case 'c': replica_identity_type = tserver::CHANGE; break;
    default:
      RSTATUS_DCHECK(false, InvalidArgument, "Invalid Replica Identity Type");
  }
  replica_identity_pb->set_replica_identity(replica_identity_type);
  req_.set_allocated_replica_identity(replica_identity_pb.release());
  return Status::OK();
}

Status PgAlterTable::RenameTable(const char *db_name, const char *newname) {
  auto& rename = *req_.mutable_rename_table();
  rename.set_database_name(db_name);
  rename.set_table_name(newname);
  return Status::OK();
}

Status PgAlterTable::IncrementSchemaVersion() {
  req_.set_increment_schema_version(true);
  return Status::OK();
}

Status PgAlterTable::SetTableId(const PgObjectId& table_id) {
  table_id.ToPB(req_.mutable_table_id());
  return Status::OK();
}

Status PgAlterTable::SetSchema(const char *schema_name) {
  auto& rename = *req_.mutable_rename_table();
  rename.set_schema_name(schema_name);
  return Status::OK();
}

Status PgAlterTable::Exec() {
  RETURN_NOT_OK(pg_session_->pg_client().AlterTable(&req_, DdlDeadline()));
  pg_session_->InvalidateTableCache(
      PgObjectId::FromPB(req_.table_id()), InvalidateOnPgClient::kFalse);
  return Status::OK();
}

void PgAlterTable::InvalidateTableCacheEntry() {
  pg_session_->InvalidateTableCache(
      PgObjectId::FromPB(req_.table_id()), InvalidateOnPgClient::kTrue);
}

//--------------------------------------------------------------------------------------------------
// PgDropSequence
//--------------------------------------------------------------------------------------------------

PgDropSequence::PgDropSequence(
    const PgSession::ScopedRefPtr& pg_session, PgOid database_oid, PgOid sequence_oid)
    : BaseType(pg_session), database_oid_(database_oid), sequence_oid_(sequence_oid) {
}

Status PgDropSequence::Exec() {
  return pg_session_->pg_client().DeleteSequenceTuple(database_oid_, sequence_oid_);
}

PgDropDBSequences::PgDropDBSequences(const PgSession::ScopedRefPtr& pg_session,  PgOid database_oid)
    : BaseType(pg_session), database_oid_(database_oid) {
}

Status PgDropDBSequences::Exec() {
  return pg_session_->pg_client().DeleteDBSequences(database_oid_);
}

// PgCreateReplicationSlot
//--------------------------------------------------------------------------------------------------

PgCreateReplicationSlot::PgCreateReplicationSlot(
    const PgSession::ScopedRefPtr& pg_session, const char* slot_name, const char* plugin_name,
    PgOid database_oid, YbcPgReplicationSlotSnapshotAction snapshot_action, YbcLsnType lsn_type)
    : BaseType(pg_session) {
  req_.set_database_oid(database_oid);
  req_.set_replication_slot_name(slot_name);
  req_.set_output_plugin_name(plugin_name);

  switch (snapshot_action) {
    case YB_REPLICATION_SLOT_NOEXPORT_SNAPSHOT:
      req_.set_snapshot_action(
          tserver::PgReplicationSlotSnapshotActionPB::REPLICATION_SLOT_NOEXPORT_SNAPSHOT);
      break;
    case YB_REPLICATION_SLOT_USE_SNAPSHOT:
      req_.set_snapshot_action(
          tserver::PgReplicationSlotSnapshotActionPB::REPLICATION_SLOT_USE_SNAPSHOT);
      break;
    default:
      DCHECK(false) << "Unknown snapshot_action " << snapshot_action;
  }

  if (yb_allow_replication_slot_lsn_types) {
    switch (lsn_type) {
      case YB_REPLICATION_SLOT_LSN_TYPE_SEQUENCE:
        req_.set_lsn_type(tserver::PGReplicationSlotLsnType::ReplicationSlotLsnTypePg_SEQUENCE);
        break;
      case YB_REPLICATION_SLOT_LSN_TYPE_HYBRID_TIME:
        req_.set_lsn_type(tserver::PGReplicationSlotLsnType::ReplicationSlotLsnTypePg_HYBRID_TIME);
        break;
      default:
        req_.set_lsn_type(tserver::PGReplicationSlotLsnType::ReplicationSlotLsnTypePg_SEQUENCE);
    }
  }
}

Result<tserver::PgCreateReplicationSlotResponsePB> PgCreateReplicationSlot::Exec() {
  return pg_session_->pg_client().CreateReplicationSlot(&req_, DdlDeadline());
}

// PgDropReplicationSlot
//--------------------------------------------------------------------------------------------------

PgDropReplicationSlot::PgDropReplicationSlot(
    const PgSession::ScopedRefPtr& pg_session, const char* slot_name)
    : BaseType(pg_session) {
  req_.set_replication_slot_name(slot_name);
}

Status PgDropReplicationSlot::Exec() {
  return pg_session_->pg_client().DropReplicationSlot(&req_, DdlDeadline());
}

}  // namespace yb::pggate
