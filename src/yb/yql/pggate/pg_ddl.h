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
//--------------------------------------------------------------------------------------------------

#pragma once

#include "yb/common/constants.h"
#include "yb/common/transaction.h"

#include "yb/tserver/pg_client.messages.h"

#include "yb/yql/pggate/pg_statement.h"

namespace yb::pggate {

class PgDdl : public PgStatement {
 protected:
  explicit PgDdl(const PgSession::ScopedRefPtr& pg_session) : PgStatement(pg_session) {}
};

class PgCreateDatabase final : public PgStatementLeafBase<PgDdl, StmtOp::kCreateDatabase> {
 public:
  PgCreateDatabase(const PgSession::ScopedRefPtr& pg_session,
                   const char* database_name,
                   PgOid database_oid,
                   PgOid source_database_oid,
                   PgOid next_oid,
                   YbcCloneInfo* yb_clone_info,
                   bool colocated,
                   bool use_transaction,
                   bool use_regular_transaction_block);

  Status Exec();

 private:
  tserver::PgCreateDatabaseRequestPB req_;
};

class PgDropDatabase final : public PgStatementLeafBase<PgDdl, StmtOp::kDropDatabase> {
 public:
  PgDropDatabase(
      const PgSession::ScopedRefPtr& pg_session, const char* database_name, PgOid database_oid);

  Status Exec();

 private:
  const char *database_name_;
  const PgOid database_oid_;
};

class PgAlterDatabase final : public PgStatementLeafBase<PgDdl, StmtOp::kAlterDatabase> {
 public:
  PgAlterDatabase(
      const PgSession::ScopedRefPtr& pg_session, const char* database_name, PgOid database_oid);

  void RenameDatabase(const char *newname);

  Status Exec();

 private:
  tserver::PgAlterDatabaseRequestPB req_;
};

class PgCreateTablegroup final : public PgStatementLeafBase<PgDdl, StmtOp::kCreateTablegroup> {
 public:
  PgCreateTablegroup(
      const PgSession::ScopedRefPtr& pg_session, const char* database_name, PgOid database_oid,
      PgOid tablegroup_oid, PgOid tablespace_oid, bool use_regular_transaction_block);

  Status Exec();

 private:
  tserver::PgCreateTablegroupRequestPB req_;
};

class PgDropTablegroup final : public PgStatementLeafBase<PgDdl, StmtOp::kDropTablegroup> {
 public:
  PgDropTablegroup(
      const PgSession::ScopedRefPtr& pg_session, PgOid database_oid, PgOid tablegroup_oid,
      bool use_regular_transaction_block);

  Status Exec();

 private:
  tserver::PgDropTablegroupRequestPB req_;
};

class PgCreateTableBase : public PgDdl {
 public:
  Status AddColumn(
      const char* attr_name, int attr_num, int attr_ybtype, bool is_hash, bool is_range,
      SortingType sorting_type = SortingType::kNotSpecified) {
    return AddColumnImpl(
        attr_name, attr_num, attr_ybtype, 20 /*INT8OID*/, is_hash, is_range, sorting_type);
  }

  Status AddColumn(
      const char* attr_name, int attr_num, const YbcPgTypeEntity* attr_type, bool is_hash,
      bool is_range, SortingType sorting_type = SortingType::kNotSpecified) {
    return AddColumnImpl(
        attr_name, attr_num, attr_type->yb_type, attr_type->type_oid, is_hash, is_range,
        sorting_type);
  }

  Status SetNumTablets(int32_t num_tablets);

  Status SetHnswOptions(int m, int m0, int ef_construction);

  Status SetVectorOptions(YbcPgVectorIdxOptions* options);

  Status AddSplitBoundary(PgExpr** exprs, int expr_count);

  Status Exec();

 protected:
  PgCreateTableBase(const PgSession::ScopedRefPtr& pg_session,
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
                    bool use_regular_transaction_block);

  tserver::PgCreateTableRequestPB req_;

 private:
  Status AddColumnImpl(
      const char* attr_name, int attr_num, int attr_ybtype, int pg_type_oid, bool is_hash,
      bool is_range, SortingType sorting_type = SortingType::kNotSpecified);
};

class PgCreateTable final : public PgStatementLeafBase<PgCreateTableBase, StmtOp::kCreateTable> {
 public:
  PgCreateTable(
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
      bool use_regular_transaction_block);
};

class PgCreateIndex final : public PgStatementLeafBase<PgCreateTableBase, StmtOp::kCreateIndex> {
 public:
  PgCreateIndex(
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
      bool use_regular_transaction_block,
      const PgObjectId& base_table_id,
      bool is_unique_index,
      bool skip_index_backfill);
};

class PgDropTable final : public PgStatementLeafBase<PgDdl, StmtOp::kDropTable> {
 public:
  PgDropTable(const PgSession::ScopedRefPtr& pg_session, const PgObjectId& table_id, bool if_exist);

  Status Exec();

 protected:
  const PgObjectId table_id_;
  const bool if_exist_;
};

class PgTruncateTable final : public PgStatementLeafBase<PgDdl, StmtOp::kTruncateTable> {
 public:
  PgTruncateTable(const PgSession::ScopedRefPtr& pg_session, const PgObjectId& table_id);

  Status Exec();

 private:
  tserver::PgTruncateTableRequestPB req_;
};

class PgDropIndex final : public PgStatementLeafBase<PgDdl, StmtOp::kDropIndex> {
 public:
  PgDropIndex(
      const PgSession::ScopedRefPtr& pg_session, const PgObjectId& index_id, bool if_exist,
      bool ddl_rollback_enabled);

  Status Exec();

 private:
  const PgObjectId index_id_;
  const bool if_exist_;
  const bool ddl_rollback_enabled_;
};

class PgAlterTable final : public PgStatementLeafBase<PgDdl, StmtOp::kAlterTable> {
 public:
  PgAlterTable(const PgSession::ScopedRefPtr& pg_session,
               const PgObjectId& table_id,
               bool use_transaction,
               bool use_regular_transaction_block);

  Status AddColumn(const char *name,
                   const YbcPgTypeEntity *attr_type,
                   int order,
                   YbcPgExpr missing_value);

  Status RenameColumn(const char *oldname, const char *newname);

  Status DropColumn(const char *name);

  Status RenameTable(const char *db_name, const char *newname);

  Status IncrementSchemaVersion();

  Status SetTableId(const PgObjectId& table_id);

  Status SetReplicaIdentity(const char identity_type);

  Status SetSchema(const char *schema_name);

  Status Exec();

  void InvalidateTableCacheEntry();

 private:
  tserver::PgAlterTableRequestPB req_;
};

class PgDropSequence final : public PgStatementLeafBase<PgDdl, StmtOp::kDropSequence> {
 public:
  PgDropSequence(const PgSession::ScopedRefPtr& pg_session, PgOid database_oid, PgOid sequence_oid);

  Status Exec();

 private:
  const PgOid database_oid_;
  const PgOid sequence_oid_;
};

class PgDropDBSequences final : public PgStatementLeafBase<PgDdl, StmtOp::kDropDbSequences> {
 public:
  PgDropDBSequences(const PgSession::ScopedRefPtr& pg_session, PgOid database_oid);

  Status Exec();

 private:
  const PgOid database_oid_;
};

class PgCreateReplicationSlot final : public PgStatementLeafBase<
                                                 PgDdl, StmtOp::kCreateReplicationSlot> {
 public:
  PgCreateReplicationSlot(
      const PgSession::ScopedRefPtr& pg_session, const char* slot_name, const char* plugin_name,
      PgOid database_oid, YbcPgReplicationSlotSnapshotAction snapshot_action,
      YbcLsnType lsn_type, YbcOrderingMode yb_ordering_mode);

  Result<tserver::PgCreateReplicationSlotResponsePB> Exec();

 private:
  tserver::PgCreateReplicationSlotRequestPB req_;
};

class PgDropReplicationSlot final : public PgStatementLeafBase<
                                               PgDdl, StmtOp::kDropReplicationSlot> {
 public:
  PgDropReplicationSlot(const PgSession::ScopedRefPtr& pg_session, const char *slot_name);

  Status Exec();

 private:
  tserver::PgDropReplicationSlotRequestPB req_;
};

}  // namespace yb::pggate
