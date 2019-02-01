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

#include "yb/client/yb_op.h"
#include "yb/common/entity_ids.h"

namespace yb {
namespace pggate {

using std::make_shared;
using std::shared_ptr;
using std::string;
using namespace std::literals;  // NOLINT

using client::YBClient;
using client::YBSession;
using client::YBMetaDataCache;

// TODO(neil) This should be derived from a GFLAGS.
static MonoDelta kSessionTimeout = 60s;

//--------------------------------------------------------------------------------------------------
// PgCreateDatabase
//--------------------------------------------------------------------------------------------------

PgCreateDatabase::PgCreateDatabase(PgSession::ScopedRefPtr pg_session,
                                   const char *database_name,
                                   const PgOid database_oid,
                                   const PgOid source_database_oid,
                                   const PgOid next_oid)
    : PgDdl(std::move(pg_session)),
      database_name_(database_name),
      database_oid_(database_oid),
      source_database_oid_(source_database_oid),
      next_oid_(next_oid) {
}

PgCreateDatabase::~PgCreateDatabase() {
}

Status PgCreateDatabase::Exec() {
  return pg_session_->CreateDatabase(database_name_, database_oid_, source_database_oid_,
                                     next_oid_);
}

PgDropDatabase::PgDropDatabase(PgSession::ScopedRefPtr pg_session,
                               const char *database_name,
                               bool if_exist)
    : PgDdl(pg_session),
      database_name_(database_name),
      if_exist_(if_exist) {
}

PgDropDatabase::~PgDropDatabase() {
}

Status PgDropDatabase::Exec() {
  return pg_session_->DropDatabase(database_name_, if_exist_);
}

//--------------------------------------------------------------------------------------------------
// PgCreateSchema
//--------------------------------------------------------------------------------------------------

PgCreateSchema::PgCreateSchema(PgSession::ScopedRefPtr pg_session,
                               const char *database_name,
                               const char *schema_name,
                               bool if_not_exist)
    : PgDdl(pg_session),
      database_name_(database_name),
      schema_name_(schema_name),
      if_not_exist_(if_not_exist) {
}

PgCreateSchema::~PgCreateSchema() {
}

Status PgCreateSchema::Exec() {
  LOG(FATAL) << "Create schema (" << database_name_ << "," << schema_name_ << "," << if_not_exist_
             << ") is under development";
  return STATUS(NotSupported, "SCHEMA is not yet implemented");
}

PgDropSchema::PgDropSchema(PgSession::ScopedRefPtr pg_session,
                           const char *database_name,
                           const char *schema_name,
                           bool if_exist)
    : PgDdl(pg_session),
      database_name_(database_name),
      schema_name_(schema_name),
      if_exist_(if_exist) {
}

PgDropSchema::~PgDropSchema() {
}

Status PgDropSchema::Exec() {
  LOG(FATAL) << "Drop schema " << database_name_ << "." << schema_name_ << "," << if_exist_
             << ") is underdevelopment";
  return STATUS(NotSupported, "SCHEMA is not yet implemented");
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
                             bool add_primary_key)
    : PgDdl(pg_session),
      table_name_(database_name, table_name),
      table_id_(table_id),
      is_pg_catalog_table_(strcmp(schema_name, "pg_catalog") == 0 ||
                           strcmp(schema_name, "information_schema") == 0),
      is_shared_table_(is_shared_table),
      if_not_exist_(if_not_exist) {
  // Add internal primary key column to a Postgres table without a user-specified primary key.
  if (add_primary_key) {
    CHECK_OK(AddColumn("ybrowid", static_cast<int32_t>(PgSystemAttrNum::kYBRowIdAttributeNumber),
                       YB_YQL_DATA_TYPE_BINARY, true /* is_hash */, true /* is_range */));
  }
}

PgCreateTable::~PgCreateTable() {
}

Status PgCreateTable::AddColumn(const char *attr_name, int attr_num, int attr_ybtype,
                                bool is_hash, bool is_range) {
  shared_ptr<QLType> yb_type = QLType::Create(static_cast<DataType>(attr_ybtype));
  client::YBColumnSpec* col = schema_builder_.AddColumn(attr_name)->Type(yb_type)->Order(attr_num);

  if (is_hash || is_range) {
    // If this is a catalog table, coerce hash column to range column since the table is
    // non-partitioned.
    if (is_hash && !is_pg_catalog_table_) {
      col->HashPrimaryKey();
    } else {
      col->PrimaryKey();
    }
  }
  return Status::OK();
}

Status PgCreateTable::Exec() {
  // Construct schema.
  client::YBSchema schema;
  if (!is_pg_catalog_table_) {
    TableProperties table_properties;
    const char* pg_txn_enabled_env_var = getenv("YB_PG_TRANSACTIONS_ENABLED");
    const bool transactional =
        !pg_txn_enabled_env_var || strcmp(pg_txn_enabled_env_var, "1") == 0;
    LOG(INFO) << Format(
        "PgCreateTable: creating a $0 table: $1",
        transactional ? "transactional" : "non-transactional", table_name_.ToString());
    if (transactional) {
      table_properties.SetTransactional(true);
      schema_builder_.SetTableProperties(table_properties);
    }
  }
  RETURN_NOT_OK(schema_builder_.Build(&schema));

  // Create table.
  shared_ptr<client::YBTableCreator> table_creator(pg_session_->NewTableCreator());
  table_creator->table_name(table_name_).table_type(client::YBTableType::PGSQL_TABLE_TYPE)
                .table_id(table_id_.GetYBTableId())
                .schema(&schema);
  if (is_pg_catalog_table_) {
    table_creator->is_pg_catalog_table();
  } else {
    table_creator->hash_schema(YBHashSchema::kPgsqlHash);
  }
  if (is_shared_table_) {
    table_creator->is_pg_shared_table();
  }

  // For index, set indexed (base) table id.
  if (indexed_table_id()) {
    table_creator->indexed_table_id(indexed_table_id()->GetYBTableId());
  }
  if (is_unique_index()) {
    table_creator->is_unique_index(true);
  }

  const Status s = table_creator->Create();
  if (PREDICT_FALSE(!s.ok())) {
    if (s.IsAlreadyPresent()) {
      if (if_not_exist_) {
        return Status::OK();
      }
      return STATUS(InvalidArgument, "Duplicate table");
    }
    if (s.IsNotFound()) {
      return STATUS(InvalidArgument, "Schema not found");
    }
    return STATUS_FORMAT(InvalidArgument, "Invalid table definition: $0", s.ToString());
  }

  return Status::OK();
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
    : PgDdl(pg_session),
      table_id_(table_id) {
}

PgTruncateTable::~PgTruncateTable() {
}

Status PgTruncateTable::Exec() {
  return pg_session_->TruncateTable(table_id_);
}

//--------------------------------------------------------------------------------------------------
// PgCreateIndex
//--------------------------------------------------------------------------------------------------

PgCreateIndex::PgCreateIndex(PgSession::ScopedRefPtr pg_session,
                             const char *database_name,
                             const char *schema_name,
                             const char *index_name,
                             const PgObjectId& index_id,
                             const PgObjectId& base_table_id,
                             bool is_shared_index,
                             bool is_unique_index,
                             bool if_not_exist)
    : PgCreateTable(pg_session, database_name, schema_name, index_name, index_id,
                    is_shared_index, if_not_exist, false /* add_primary_key */),
      base_table_id_(base_table_id),
      is_unique_index_(is_unique_index) {
}

PgCreateIndex::~PgCreateIndex() {
}

Status PgCreateIndex::Exec() {
  // Add ybbasectid column to store the ybctid of the rows in the indexed table.
  RETURN_NOT_OK(AddColumn("ybbasectid",
                          static_cast<int32_t>(PgSystemAttrNum::kYBBaseTupleIdAttributeNumber),
                          YB_YQL_DATA_TYPE_BINARY,
                          false /* is_hash */,
                          !is_unique_index_  /* is_range */));
  return PgCreateTable::Exec();
}

}  // namespace pggate
}  // namespace yb
