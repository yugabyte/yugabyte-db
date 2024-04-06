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

namespace yb {
namespace pggate {

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

PgCreateDatabase::PgCreateDatabase(PgSession::ScopedRefPtr pg_session,
                                   const char *database_name,
                                   const PgOid database_oid,
                                   const PgOid source_database_oid,
                                   const PgOid next_oid,
                                   const bool colocated)
    : PgDdl(std::move(pg_session)) {
  req_.set_database_name(database_name);
  req_.set_database_oid(database_oid);
  req_.set_source_database_oid(source_database_oid);
  req_.set_next_oid(next_oid);
  req_.set_colocated(colocated);
}

PgCreateDatabase::~PgCreateDatabase() {
}

Status PgCreateDatabase::Exec() {
  return pg_session_->pg_client().CreateDatabase(&req_, CreateDatabaseDeadline());
}

PgDropDatabase::PgDropDatabase(PgSession::ScopedRefPtr pg_session,
                               const char *database_name,
                               PgOid database_oid)
    : PgDdl(pg_session),
      database_name_(database_name),
      database_oid_(database_oid) {
}

PgDropDatabase::~PgDropDatabase() {
}

Status PgDropDatabase::Exec() {
  return pg_session_->DropDatabase(database_name_, database_oid_);
}

PgAlterDatabase::PgAlterDatabase(PgSession::ScopedRefPtr pg_session,
                                 const char *database_name,
                                 PgOid database_oid)
    : PgDdl(pg_session) {
  req_.set_database_name(database_name);
  req_.set_database_oid(database_oid);
}

PgAlterDatabase::~PgAlterDatabase() {
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

PgCreateTablegroup::PgCreateTablegroup(PgSession::ScopedRefPtr pg_session,
                                       const char *database_name,
                                       const PgOid database_oid,
                                       const PgOid tablegroup_oid,
                                       const PgOid tablespace_oid)
    : PgDdl(pg_session) {
  req_.set_database_name(database_name);
  PgObjectId(database_oid, tablegroup_oid).ToPB(req_.mutable_tablegroup_id());
  PgObjectId(database_oid, tablespace_oid).ToPB(req_.mutable_tablespace_id());
}

PgCreateTablegroup::~PgCreateTablegroup() {
}

Status PgCreateTablegroup::Exec() {
  return pg_session_->pg_client().CreateTablegroup(&req_, DdlDeadline());
}

PgDropTablegroup::PgDropTablegroup(PgSession::ScopedRefPtr pg_session,
                                   const PgOid database_oid,
                                   const PgOid tablegroup_oid)
    : PgDdl(pg_session) {
  PgObjectId(database_oid, tablegroup_oid).ToPB(req_.mutable_tablegroup_id());
}

PgDropTablegroup::~PgDropTablegroup() {
}

Status PgDropTablegroup::Exec() {
  return pg_session_->pg_client().DropTablegroup(&req_, DdlDeadline());
}

//--------------------------------------------------------------------------------------------------
// PgCreateTable
//--------------------------------------------------------------------------------------------------

PgCreateTable::PgCreateTable(PgSession::ScopedRefPtr pg_session,
                             const char *database_name,
                             const char *schema_name,
                             const char *table_name,
                             const PgObjectId& table_id,
                             bool is_shared_table,
                             bool if_not_exist,
                             bool add_primary_key,
                             bool is_colocated_via_database,
                             const PgObjectId& tablegroup_oid,
                             const ColocationId colocation_id,
                             const PgObjectId& tablespace_oid,
                             bool is_matview,
                             const PgObjectId& pg_table_oid,
                             const PgObjectId& old_relfilenode_oid,
                             bool is_truncate)
    : PgDdl(pg_session) {
  table_id.ToPB(req_.mutable_table_id());
  req_.set_database_name(database_name);
  req_.set_table_name(table_name);
  req_.set_num_tablets(-1);
  req_.set_is_pg_catalog_table(strcmp(schema_name, "pg_catalog") == 0 ||
                               strcmp(schema_name, "information_schema") == 0);
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

  // Add internal primary key column to a Postgres table without a user-specified primary key.
  if (add_primary_key) {
    // For regular user table, ybrowid should be a hash key because ybrowid is a random uuid.
    // For colocated or sys catalog table, ybrowid should be a range key because they are
    // unpartitioned tables in a single tablet.
    bool is_hash =
        !req_.is_pg_catalog_table() && !is_colocated_via_database && !tablegroup_oid.IsValid();
    CHECK_OK(AddColumn("ybrowid", static_cast<int32_t>(PgSystemAttrNum::kYBRowId),
                       YB_YQL_DATA_TYPE_BINARY, is_hash, true /* is_range */));
  }
}

Status PgCreateTable::AddColumnImpl(const char *attr_name,
                                    int attr_num,
                                    int attr_ybtype,
                                    int pg_type_oid,
                                    bool is_hash,
                                    bool is_range,
                                    SortingType sorting_type) {
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

Status PgCreateTable::SetNumTablets(int32_t num_tablets) {
  if (num_tablets < 0) {
    return STATUS(InvalidArgument, "num_tablets cannot be less than zero");
  }
  if (num_tablets > FLAGS_max_num_tablets_for_table) {
    return STATUS(InvalidArgument, "num_tablets exceeds system limit");
  }

  req_.set_num_tablets(num_tablets);
  return Status::OK();
}

Status PgCreateTable::AddSplitBoundary(PgExpr **exprs, int expr_count) {
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

Status PgCreateTable::Exec() {
  RETURN_NOT_OK(pg_session_->pg_client().CreateTable(&req_, DdlDeadline()));
  auto base_table_id = PgObjectId::FromPB(req_.base_table_id());
  if (base_table_id.IsValid()) {
    pg_session_->InvalidateTableCache(base_table_id, InvalidateOnPgClient::kFalse);
  }
  return Status::OK();
}

void PgCreateTable::SetupIndex(
    const PgObjectId& base_table_id, bool is_unique_index, bool skip_index_backfill) {
  base_table_id.ToPB(req_.mutable_base_table_id());
  req_.set_is_unique_index(is_unique_index);
  req_.set_skip_index_backfill(skip_index_backfill);
}

StmtOp PgCreateTable::stmt_op() const {
  return PgObjectId::FromPB(req_.base_table_id()).IsValid()
      ? StmtOp::STMT_CREATE_INDEX : StmtOp::STMT_CREATE_TABLE;
}

//--------------------------------------------------------------------------------------------------
// PgDropTable
//--------------------------------------------------------------------------------------------------

PgDropTable::PgDropTable(PgSession::ScopedRefPtr pg_session,
                         const PgObjectId& table_id,
                         bool if_exist)
    : PgDdl(pg_session),
      table_id_(table_id),
      if_exist_(if_exist) {
}

PgDropTable::~PgDropTable() {
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

PgTruncateTable::PgTruncateTable(PgSession::ScopedRefPtr pg_session,
                                 const PgObjectId& table_id)
    : PgDdl(pg_session) {
  table_id.ToPB(req_.mutable_table_id());
}

PgTruncateTable::~PgTruncateTable() {
}

Status PgTruncateTable::Exec() {
  return pg_session_->pg_client().TruncateTable(&req_, DdlDeadline());
}

//--------------------------------------------------------------------------------------------------
// PgDropIndex
//--------------------------------------------------------------------------------------------------

PgDropIndex::PgDropIndex(PgSession::ScopedRefPtr pg_session,
                         const PgObjectId& index_id,
                         bool if_exist)
    : PgDropTable(pg_session, index_id, if_exist) {
}

PgDropIndex::~PgDropIndex() {
}

Status PgDropIndex::Exec() {
  client::YBTableName indexed_table_name;
  Status s = pg_session_->DropIndex(table_id_, &indexed_table_name);
  if (s.ok() || (s.IsNotFound() && if_exist_)) {
    RSTATUS_DCHECK(!indexed_table_name.empty(), Uninitialized, "indexed_table_name uninitialized");
    PgObjectId indexed_table_id(indexed_table_name.table_id());

    pg_session_->InvalidateTableCache(table_id_, InvalidateOnPgClient::kFalse);
    pg_session_->InvalidateTableCache(indexed_table_id, InvalidateOnPgClient::kFalse);
    return Status::OK();
  }
  return s;
}

//--------------------------------------------------------------------------------------------------
// PgAlterTable
//--------------------------------------------------------------------------------------------------

PgAlterTable::PgAlterTable(PgSession::ScopedRefPtr pg_session,
                           const PgObjectId& table_id)
    : PgDdl(pg_session) {
  table_id.ToPB(req_.mutable_table_id());
}

Status PgAlterTable::AddColumn(const char *name,
                               const YBCPgTypeEntity *attr_type,
                               int order,
                               YBCPgExpr missing_value) {
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

PgAlterTable::~PgAlterTable() {
}

//--------------------------------------------------------------------------------------------------
// PgDropSequence
//--------------------------------------------------------------------------------------------------

PgDropSequence::PgDropSequence(PgSession::ScopedRefPtr pg_session,
                               PgOid database_oid,
                               PgOid sequence_oid)
  : PgDdl(std::move(pg_session)),
    database_oid_(database_oid),
    sequence_oid_(sequence_oid) {
}

PgDropSequence::~PgDropSequence() {
}

Status PgDropSequence::Exec() {
  return pg_session_->pg_client().DeleteSequenceTuple(database_oid_, sequence_oid_);
}

PgDropDBSequences::PgDropDBSequences(PgSession::ScopedRefPtr pg_session,
                                     PgOid database_oid)
  : PgDdl(std::move(pg_session)),
    database_oid_(database_oid) {
}

PgDropDBSequences::~PgDropDBSequences() {
}

Status PgDropDBSequences::Exec() {
  return pg_session_->pg_client().DeleteDBSequences(database_oid_);
}

// PgCreateReplicationSlot
//--------------------------------------------------------------------------------------------------

PgCreateReplicationSlot::PgCreateReplicationSlot(PgSession::ScopedRefPtr pg_session,
                                                 const char *slot_name,
                                                 PgOid database_oid,
                                                 YBCPgReplicationSlotSnapshotAction snapshot_action)
    : PgDdl(pg_session) {
  req_.set_database_oid(database_oid);
  req_.set_replication_slot_name(slot_name);

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
}

Result<tserver::PgCreateReplicationSlotResponsePB> PgCreateReplicationSlot::Exec() {
  return pg_session_->pg_client().CreateReplicationSlot(&req_, DdlDeadline());
}

PgCreateReplicationSlot::~PgCreateReplicationSlot() {
}

// PgDropReplicationSlot
//--------------------------------------------------------------------------------------------------

PgDropReplicationSlot::PgDropReplicationSlot(PgSession::ScopedRefPtr pg_session,
                                             const char *slot_name)
    : PgDdl(pg_session) {
  req_.set_replication_slot_name(slot_name);
}

Status PgDropReplicationSlot::Exec() {
  return pg_session_->pg_client().DropReplicationSlot(&req_, DdlDeadline());
}

PgDropReplicationSlot::~PgDropReplicationSlot() {
}

}  // namespace pggate
}  // namespace yb
