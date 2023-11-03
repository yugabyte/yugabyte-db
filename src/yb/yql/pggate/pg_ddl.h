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

namespace yb {
namespace pggate {

class PgDdl : public PgStatement {
 public:
  explicit PgDdl(PgSession::ScopedRefPtr pg_session) : PgStatement(pg_session) {
  }
};

//--------------------------------------------------------------------------------------------------
// CREATE DATABASE
//--------------------------------------------------------------------------------------------------

class PgCreateDatabase : public PgDdl {
 public:
  PgCreateDatabase(PgSession::ScopedRefPtr pg_session,
                   const char *database_name,
                   PgOid database_oid,
                   PgOid source_database_oid,
                   PgOid next_oid,
                   const bool colocated);
  virtual ~PgCreateDatabase();

  void UseTransaction() {
    req_.set_use_transaction(true);
  }

  StmtOp stmt_op() const override { return StmtOp::STMT_CREATE_DATABASE; }

  // Execute.
  Status Exec();

 private:
  tserver::PgCreateDatabaseRequestPB req_;
};

class PgDropDatabase : public PgDdl {
 public:
  PgDropDatabase(PgSession::ScopedRefPtr pg_session, const char *database_name, PgOid database_oid);
  virtual ~PgDropDatabase();

  StmtOp stmt_op() const override { return StmtOp::STMT_DROP_DATABASE; }

  // Execute.
  Status Exec();

 private:
  const char *database_name_;
  const PgOid database_oid_;
};

class PgAlterDatabase : public PgDdl {
 public:
  PgAlterDatabase(PgSession::ScopedRefPtr pg_session,
                  const char *database_name,
                  PgOid database_oid);
  virtual ~PgAlterDatabase();

  StmtOp stmt_op() const override { return StmtOp::STMT_ALTER_DATABASE; }

  void RenameDatabase(const char *newname);

  // Execute.
  Status Exec();

 private:
  tserver::PgAlterDatabaseRequestPB req_;
};

//--------------------------------------------------------------------------------------------------
// CREATE / DROP TABLEGROUP
//--------------------------------------------------------------------------------------------------

class PgCreateTablegroup : public PgDdl {
 public:
  // Assumes we are within a DDL transaction.
  PgCreateTablegroup(PgSession::ScopedRefPtr pg_session,
                     const char *database_name,
                     const PgOid database_oid,
                     const PgOid tablegroup_oid,
                     const PgOid tablespace_oid);
  virtual ~PgCreateTablegroup();

  StmtOp stmt_op() const override { return StmtOp::STMT_CREATE_TABLEGROUP; }

  // Execute.
  Status Exec();

 private:
  tserver::PgCreateTablegroupRequestPB req_;
};

class PgDropTablegroup : public PgDdl {
 public:
  PgDropTablegroup(PgSession::ScopedRefPtr pg_session,
                   PgOid database_oid,
                   PgOid tablegroup_oid);
  virtual ~PgDropTablegroup();

  StmtOp stmt_op() const override { return StmtOp::STMT_DROP_TABLEGROUP; }

  // Execute.
  Status Exec();

 private:
  tserver::PgDropTablegroupRequestPB req_;
};

//--------------------------------------------------------------------------------------------------
// CREATE TABLE
//--------------------------------------------------------------------------------------------------

class PgCreateTable : public PgDdl {
 public:
  PgCreateTable(PgSession::ScopedRefPtr pg_session,
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
                const PgObjectId& matview_pg_table_oid);

  void SetupIndex(
      const PgObjectId& base_table_id, bool is_unique_index, bool skip_index_backfill);

  StmtOp stmt_op() const override;

  Status AddColumn(const char *attr_name,
                   int attr_num,
                   int attr_ybtype,
                   bool is_hash,
                   bool is_range,
                   SortingType sorting_type = SortingType::kNotSpecified) {
    return AddColumnImpl(attr_name, attr_num, attr_ybtype, 20 /*INT8OID*/,
                         is_hash, is_range, sorting_type);
  }

  Status AddColumn(const char *attr_name,
                   int attr_num,
                   const YBCPgTypeEntity *attr_type,
                   bool is_hash,
                   bool is_range,
                   SortingType sorting_type = SortingType::kNotSpecified) {
    return AddColumnImpl(attr_name, attr_num, attr_type->yb_type, attr_type->type_oid,
                         is_hash, is_range, sorting_type);
  }

  // Specify the number of tablets explicitly.
  Status SetNumTablets(int32_t num_tablets);

  Status AddSplitBoundary(PgExpr **exprs, int expr_count);

  void UseTransaction() {
    req_.set_use_transaction(true);
  }

  // Execute.
  virtual Status Exec();

 protected:
  virtual Status AddColumnImpl(
      const char *attr_name, int attr_num, int attr_ybtype, int pg_type_oid, bool is_hash,
      bool is_range, SortingType sorting_type = SortingType::kNotSpecified);

 private:
  tserver::PgCreateTableRequestPB req_;
};

class PgDropTable : public PgDdl {
 public:
  PgDropTable(PgSession::ScopedRefPtr pg_session, const PgObjectId& table_id, bool if_exist);
  virtual ~PgDropTable();

  StmtOp stmt_op() const override { return StmtOp::STMT_DROP_TABLE; }

  // Execute.
  Status Exec();

 protected:
  const PgObjectId table_id_;
  bool if_exist_;
};

class PgTruncateTable : public PgDdl {
 public:
  PgTruncateTable(PgSession::ScopedRefPtr pg_session, const PgObjectId& table_id);
  virtual ~PgTruncateTable();

  StmtOp stmt_op() const override { return StmtOp::STMT_TRUNCATE_TABLE; }

  // Execute.
  Status Exec();

 private:
  tserver::PgTruncateTableRequestPB req_;
};

class PgDropIndex : public PgDropTable {
 public:
  PgDropIndex(PgSession::ScopedRefPtr pg_session, const PgObjectId& index_id, bool if_exist);
  virtual ~PgDropIndex();

  StmtOp stmt_op() const override { return StmtOp::STMT_DROP_INDEX; }

  // Execute.
  Status Exec();
};

//--------------------------------------------------------------------------------------------------
// ALTER TABLE
//--------------------------------------------------------------------------------------------------

class PgAlterTable : public PgDdl {
 public:
  PgAlterTable(PgSession::ScopedRefPtr pg_session,
               const PgObjectId& table_id);

  Status AddColumn(const char *name,
                   const YBCPgTypeEntity *attr_type,
                   int order,
                   YBCPgExpr missing_value);

  Status RenameColumn(const char *oldname, const char *newname);

  Status DropColumn(const char *name);

  Status RenameTable(const char *db_name, const char *newname);

  Status IncrementSchemaVersion();

  Status SetTableId(const PgObjectId& table_id);

  Status Exec();

  virtual ~PgAlterTable();

  StmtOp stmt_op() const override { return StmtOp::STMT_ALTER_TABLE; }

  void UseTransaction() {
    req_.set_use_transaction(true);
  }

 private:
  tserver::PgAlterTableRequestPB req_;
};

//--------------------------------------------------------------------------------------------------
// DROP SEQUENCE
//--------------------------------------------------------------------------------------------------

class PgDropSequence : public PgDdl {
 public:
  PgDropSequence(PgSession::ScopedRefPtr pg_session,
                 PgOid database_oid,
                 PgOid sequence_oid);
  virtual ~PgDropSequence();

  StmtOp stmt_op() const override { return StmtOp::STMT_DROP_SEQUENCE; }

  // Execute.
  Status Exec();

 private:
  PgOid database_oid_;
  PgOid sequence_oid_;
};

class PgDropDBSequences : public PgDdl {
 public:
  PgDropDBSequences(PgSession::ScopedRefPtr pg_session,
                    PgOid database_oid);
  virtual ~PgDropDBSequences();

  StmtOp stmt_op() const override { return StmtOp::STMT_DROP_DB_SEQUENCES; }

  // Execute.
  Status Exec();

 private:
  PgOid database_oid_;
};

// CREATE REPLICATION SLOT
//--------------------------------------------------------------------------------------------------

class PgCreateReplicationSlot : public PgDdl {
 public:
  PgCreateReplicationSlot(PgSession::ScopedRefPtr pg_session,
                          const char *slot_name,
                          PgOid database_oid);

  Status Exec();

  virtual ~PgCreateReplicationSlot();

  StmtOp stmt_op() const override { return StmtOp::STMT_CREATE_REPLICATION_SLOT; }

 private:
  tserver::PgCreateReplicationSlotRequestPB req_;
};

// DROP REPLICATION SLOT
//--------------------------------------------------------------------------------------------------

class PgDropReplicationSlot : public PgDdl {
 public:
  PgDropReplicationSlot(PgSession::ScopedRefPtr pg_session,
                        const char *slot_name);

  Status Exec();

  virtual ~PgDropReplicationSlot();

  StmtOp stmt_op() const override { return StmtOp::STMT_DROP_REPLICATION_SLOT; }

 private:
  tserver::PgDropReplicationSlotRequestPB req_;
};

}  // namespace pggate
}  // namespace yb
