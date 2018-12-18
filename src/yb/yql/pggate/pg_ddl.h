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

#ifndef YB_YQL_PGGATE_PG_DDL_H_
#define YB_YQL_PGGATE_PG_DDL_H_

#include "yb/yql/pggate/pg_statement.h"

namespace yb {
namespace pggate {

class PgDdl : public PgStatement {
 public:
  PgDdl(PgSession::ScopedRefPtr pg_session, StmtOp stmt_op)
      : PgStatement(pg_session, stmt_op) {
  }

  virtual CHECKED_STATUS ClearBinds() {
    return STATUS(InvalidArgument, "This statement cannot be bound to any values");
  }
};

//--------------------------------------------------------------------------------------------------
// CREATE DATABASE
//--------------------------------------------------------------------------------------------------

class PgCreateDatabase : public PgDdl {
 public:
  // Public types.
  typedef scoped_refptr<PgCreateDatabase> ScopedRefPtr;
  typedef scoped_refptr<const PgCreateDatabase> ScopedRefPtrConst;

  typedef std::unique_ptr<PgCreateDatabase> UniPtr;
  typedef std::unique_ptr<const PgCreateDatabase> UniPtrConst;

  // Constructors.
  PgCreateDatabase(PgSession::ScopedRefPtr pg_session,
                   const char *database_name,
                   PgOid database_oid,
                   PgOid source_database_oid,
                   PgOid next_oid);
  virtual ~PgCreateDatabase();

  // Execute.
  CHECKED_STATUS Exec();

 private:
  const char *database_name_;
  const PgOid database_oid_;
  const PgOid source_database_oid_;
  const PgOid next_oid_;
};

class PgDropDatabase : public PgDdl {
 public:
  // Public types.
  typedef scoped_refptr<PgDropDatabase> ScopedRefPtr;
  typedef scoped_refptr<const PgDropDatabase> ScopedRefPtrConst;

  typedef std::unique_ptr<PgDropDatabase> UniPtr;
  typedef std::unique_ptr<const PgDropDatabase> UniPtrConst;

  // Constructors.
  PgDropDatabase(PgSession::ScopedRefPtr pg_session, const char *database_name, bool if_exist);
  virtual ~PgDropDatabase();

  // Execute.
  CHECKED_STATUS Exec();

 private:
  const char *database_name_;
  bool if_exist_;
};

//--------------------------------------------------------------------------------------------------
// CREATE SCHEMA
//
// TODO(neil) This is not yet supported.  After Mihnea figures out how PostgreSQL implemented it,
// we can add support for schema.
//--------------------------------------------------------------------------------------------------

class PgCreateSchema : public PgDdl {
 public:
  // Public types.
  typedef scoped_refptr<PgCreateSchema> ScopedRefPtr;
  typedef scoped_refptr<const PgCreateSchema> ScopedRefPtrConst;

  typedef std::unique_ptr<PgCreateSchema> UniPtr;
  typedef std::unique_ptr<const PgCreateSchema> UniPtrConst;

  // Constructors.
  PgCreateSchema(PgSession::ScopedRefPtr pg_session,
                 const char *database_name,
                 const char *schema_name,
                 bool if_not_exist);
  virtual ~PgCreateSchema();

  // Execute.
  CHECKED_STATUS Exec();

 private:
  const char *database_name_;
  const char *schema_name_;
  bool if_not_exist_;
};

class PgDropSchema : public PgDdl {
 public:
  // Public types.
  typedef scoped_refptr<PgDropSchema> ScopedRefPtr;
  typedef scoped_refptr<const PgDropSchema> ScopedRefPtrConst;

  typedef std::unique_ptr<PgDropSchema> UniPtr;
  typedef std::unique_ptr<const PgDropSchema> UniPtrConst;

  // Constructors.
  PgDropSchema(PgSession::ScopedRefPtr pg_session,
               const char *database_name,
               const char *schema_name,
               bool if_exist);
  virtual ~PgDropSchema();

  // Execute.
  CHECKED_STATUS Exec();

 private:
  const char *database_name_;
  const char *schema_name_;
  bool if_exist_;
};

//--------------------------------------------------------------------------------------------------
// CREATE TABLE
//--------------------------------------------------------------------------------------------------

class PgCreateTable : public PgDdl {
 public:
  // Public types.
  typedef scoped_refptr<PgCreateTable> ScopedRefPtr;
  typedef scoped_refptr<const PgCreateTable> ScopedRefPtrConst;

  typedef std::unique_ptr<PgCreateTable> UniPtr;
  typedef std::unique_ptr<const PgCreateTable> UniPtrConst;

  // Constructors.
  PgCreateTable(PgSession::ScopedRefPtr pg_session,
                const char *database_name,
                const char *schema_name,
                const char *table_name,
                PgOid database_oid,
                PgOid schema_oid,
                PgOid table_oid,
                bool is_shared_table,
                bool if_not_exist,
                bool add_primary_key);
  virtual ~PgCreateTable();

  CHECKED_STATUS AddColumn(const char *attr_name, int attr_num, int attr_ybtype,
                           bool is_hash, bool is_range);

  // Execute.
  CHECKED_STATUS Exec();

 private:
  client::YBTableName table_name_;
  const std::string table_id_;
  bool is_pg_catalog_table_;
  bool is_shared_table_;
  bool if_not_exist_;
  client::YBSchemaBuilder schema_builder_;
};

class PgDropTable : public PgDdl {
 public:
  // Public types.
  typedef scoped_refptr<PgDropTable> ScopedRefPtr;
  typedef scoped_refptr<const PgDropTable> ScopedRefPtrConst;

  typedef std::unique_ptr<PgDropTable> UniPtr;
  typedef std::unique_ptr<const PgDropTable> UniPtrConst;

  // Constructors.
  PgDropTable(PgSession::ScopedRefPtr pg_session,
              const char *database_name,
              const char *schema_name,
              const char *table_name,
              bool if_exist);
  virtual ~PgDropTable();

  // Execute.
  CHECKED_STATUS Exec();

 private:
  client::YBTableName table_name_;
  bool if_exist_;
};

}  // namespace pggate
}  // namespace yb

#endif // YB_YQL_PGGATE_PG_DDL_H_
