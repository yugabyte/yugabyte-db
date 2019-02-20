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

#ifndef YB_YQL_PGGATE_PGGATE_H_
#define YB_YQL_PGGATE_PGGATE_H_

#include <algorithm>
#include <functional>
#include <thread>
#include <unordered_map>

#include <boost/preprocessor/cat.hpp>
#include <boost/preprocessor/seq/for_each.hpp>

#include "yb/util/metrics.h"
#include "yb/util/mem_tracker.h"
#include "yb/util/ybc_util.h"

#include "yb/client/client.h"
#include "yb/client/callbacks.h"
#include "yb/client/async_initializer.h"
#include "yb/server/server_base_options.h"

#include "yb/yql/pggate/pg_env.h"
#include "yb/yql/pggate/pg_session.h"
#include "yb/yql/pggate/pg_statement.h"
#include "yb/yql/pggate/type_mapping.h"
#include "yb/yql/pggate/pggate_if_cxx_decl.h"

#include "yb/server/hybrid_clock.h"

namespace yb {
namespace pggate {

//--------------------------------------------------------------------------------------------------

class PggateOptions : public yb::server::ServerBaseOptions {
 public:
  static const uint16_t kDefaultPort = 5432;
  static const uint16_t kDefaultWebPort = 13000;

  PggateOptions();
  virtual ~PggateOptions() {}
};

//--------------------------------------------------------------------------------------------------
// Implements support for CAPI.
class PgApiImpl {
 public:
  PgApiImpl(const YBCPgTypeEntity *YBCDataTypeTable, int count);
  virtual ~PgApiImpl();

  //------------------------------------------------------------------------------------------------
  // Access function to Pggate attribute.
  std::shared_ptr<client::YBClient> client() {
    return async_client_init_.client();
  }

  // Initialize ENV within which PGSQL calls will be executed.
  CHECKED_STATUS CreateEnv(PgEnv **pg_env);
  CHECKED_STATUS DestroyEnv(PgEnv *pg_env);

  // Initialize a session to process statements that come from the same client connection.
  // If database_name is empty, a session is created without connecting to any database.
  CHECKED_STATUS CreateSession(const PgEnv *pg_env,
                               const string& database_name,
                               PgSession **pg_session);
  CHECKED_STATUS DestroySession(PgSession *pg_session);

  // Read session.
  PgSession::ScopedRefPtr GetSession(PgSession *handle);

  // Read statement.
  PgStatement::ScopedRefPtr GetStatement(PgStatement *handle);

  // Delete statement.
  CHECKED_STATUS DeleteStatement(PgStatement *handle);

  // Remove all values and expressions that were bound to the given statement.
  CHECKED_STATUS ClearBinds(PgStatement *handle);

  // Search for type_entity.
  const YBCPgTypeEntity *FindTypeEntity(int type_oid);

  //------------------------------------------------------------------------------------------------
  // Connect database. Switch the connected database to the given "database_name".
  CHECKED_STATUS ConnectDatabase(PgSession *pg_session, const char *database_name);

  // Create database.
  CHECKED_STATUS NewCreateDatabase(PgSession *pg_session,
                                   const char *database_name,
                                   PgOid database_oid,
                                   PgOid source_database_oid,
                                   PgOid next_oid,
                                   PgStatement **handle);
  CHECKED_STATUS ExecCreateDatabase(PgStatement *handle);

  // Drop database.
  CHECKED_STATUS NewDropDatabase(PgSession *pg_session,
                                 const char *database_name,
                                 bool if_exist,
                                 PgStatement **handle);
  CHECKED_STATUS ExecDropDatabase(PgStatement *handle);

  // Reserve oids.
  CHECKED_STATUS ReserveOids(PgSession *pg_session,
                             PgOid database_oid,
                             PgOid next_oid,
                             uint32_t count,
                             PgOid *begin_oid,
                             PgOid *end_oid);

  //------------------------------------------------------------------------------------------------
  // Create and drop schema.
  // - When "database_name" is NULL, the connected database name is used.
  CHECKED_STATUS NewCreateSchema(PgSession *pg_session,
                                 const char *database_name,
                                 const char *schema_name,
                                 bool if_not_exist,
                                 PgStatement **handle);

  CHECKED_STATUS ExecCreateSchema(PgStatement *handle);

  CHECKED_STATUS NewDropSchema(PgSession *pg_session,
                               const char *database_name,
                               const char *schema_name,
                               bool if_exist,
                               PgStatement **handle);

  CHECKED_STATUS ExecDropSchema(PgStatement *handle);

  //------------------------------------------------------------------------------------------------
  // Create, alter and drop table.
  CHECKED_STATUS NewCreateTable(PgSession *pg_session,
                                const char *database_name,
                                const char *schema_name,
                                const char *table_name,
                                const PgObjectId& table_id,
                                bool is_shared_table,
                                bool if_not_exist,
                                bool add_primary_key,
                                PgStatement **handle);

  CHECKED_STATUS CreateTableAddColumn(PgStatement *handle, const char *attr_name, int attr_num,
                                      const YBCPgTypeEntity *attr_type, bool is_hash,
                                      bool is_range);

  CHECKED_STATUS ExecCreateTable(PgStatement *handle);

  CHECKED_STATUS NewAlterTable(PgSession *pg_session,
                               const PgObjectId& table_id,
                               PgStatement **handle);

  CHECKED_STATUS AlterTableAddColumn(PgStatement *handle, const char *name,
                                     int order, const YBCPgTypeEntity *attr_type, bool is_not_null);

  CHECKED_STATUS AlterTableRenameColumn(PgStatement *handle, const char *oldname,
                                        const char *newname);

  CHECKED_STATUS AlterTableDropColumn(PgStatement *handle, const char *name);

  CHECKED_STATUS AlterTableRenameTable(PgStatement *handle, const char *db_name,
                                       const char *newname);

  CHECKED_STATUS ExecAlterTable(PgStatement *handle);

  CHECKED_STATUS NewDropTable(PgSession *pg_session,
                              const PgObjectId& table_id,
                              bool if_exist,
                              PgStatement **handle);

  CHECKED_STATUS ExecDropTable(PgStatement *handle);

  CHECKED_STATUS NewTruncateTable(PgSession *pg_session,
                                  const PgObjectId& table_id,
                                  PgStatement **handle);

  CHECKED_STATUS ExecTruncateTable(PgStatement *handle);

  CHECKED_STATUS GetTableDesc(PgSession *pg_session,
                              const PgObjectId& table_id,
                              PgTableDesc **handle);

  CHECKED_STATUS DeleteTableDesc(PgTableDesc *handle);

  CHECKED_STATUS GetColumnInfo(YBCPgTableDesc table_desc,
                               int16_t attr_number,
                               bool *is_primary,
                               bool *is_hash);

  //------------------------------------------------------------------------------------------------
  // Create and drop index.
  CHECKED_STATUS NewCreateIndex(PgSession *pg_session,
                                const char *database_name,
                                const char *schema_name,
                                const char *index_name,
                                const PgObjectId& index_id,
                                const PgObjectId& table_id,
                                bool is_shared_index,
                                bool is_unique_index,
                                bool if_not_exist,
                                PgStatement **handle);

  CHECKED_STATUS CreateIndexAddColumn(PgStatement *handle, const char *attr_name, int attr_num,
                                      const YBCPgTypeEntity *attr_type, bool is_hash,
                                      bool is_range);

  CHECKED_STATUS ExecCreateIndex(PgStatement *handle);

  //------------------------------------------------------------------------------------------------
  // All DML statements
  CHECKED_STATUS DmlAppendTarget(PgStatement *handle, PgExpr *expr);

  // Binding Columns: Bind column with a value (expression) in a statement.
  // + This API is used to identify the rows you want to operate on. If binding columns are not
  //   there, that means you want to operate on all rows (full scan). You can view this as a
  //   a definitions of an initial rowset or an optimization over full-scan.
  //
  // + There are some restrictions on when BindColumn() can be used.
  //   Case 1: INSERT INTO tab(x) VALUES(x_expr)
  //   - BindColumn() can be used for BOTH primary-key and regular columns.
  //   - This bind-column function is used to bind "x" with "x_expr", and "x_expr" that can contain
  //     bind-variables (placeholders) and contants whose values can be updated for each execution
  //     of the same allocated statement.
  //
  //   Case 2: SELECT / UPDATE / DELETE <WHERE key = "key_expr">
  //   - BindColumn() can only be used for primary-key columns.
  //   - This bind-column function is used to bind the primary column "key" with "key_expr" that can
  //     contain bind-variables (placeholders) and contants whose values can be updated for each
  //     execution of the same allocated statement.
  CHECKED_STATUS DmlBindColumn(YBCPgStatement handle, int attr_num, YBCPgExpr attr_value);
  CHECKED_STATUS DmlBindIndexColumn(YBCPgStatement handle, int attr_num, YBCPgExpr attr_value);

  // API for SET clause.
  CHECKED_STATUS DmlAssignColumn(YBCPgStatement handle, int attr_num, YBCPgExpr attr_value);

  // This function is to fetch the targets in YBCPgDmlAppendTarget() from the rows that were defined
  // by YBCPgDmlBindColumn().
  CHECKED_STATUS DmlFetch(PgStatement *handle, int32_t natts, uint64_t *values, bool *isnulls,
                          PgSysColumns *syscols, bool *has_data);

  // DB Operations: SET, WHERE, ORDER_BY, GROUP_BY, etc.
  // + The following operations are run by DocDB.
  //   - API for "set_clause" (not yet implemented).
  //
  // + The following operations are run by Postgres layer. An API might be added to move these
  //   operations to DocDB.
  //   - API for "where_expr"
  //   - API for "order_by_expr"
  //   - API for "group_by_expr"

  //------------------------------------------------------------------------------------------------
  // Insert.
  CHECKED_STATUS NewInsert(PgSession *pg_session, const PgObjectId& table_id, PgStatement **handle);

  CHECKED_STATUS ExecInsert(PgStatement *handle);

  //------------------------------------------------------------------------------------------------
  // Update.
  CHECKED_STATUS NewUpdate(PgSession *pg_session, const PgObjectId& table_id, PgStatement **handle);

  CHECKED_STATUS ExecUpdate(PgStatement *handle);

  //------------------------------------------------------------------------------------------------
  // Delete.
  CHECKED_STATUS NewDelete(PgSession *pg_session, const PgObjectId& table_id, PgStatement **handle);

  CHECKED_STATUS ExecDelete(PgStatement *handle);

  //------------------------------------------------------------------------------------------------
  // Select.
  CHECKED_STATUS NewSelect(PgSession *pg_session, const PgObjectId& table_id,
                           const PgObjectId& index_id, PgStatement **handle, uint64_t* read_time);

  CHECKED_STATUS ExecSelect(PgStatement *handle);

  //------------------------------------------------------------------------------------------------
  // Transaction control.
  PgTxnManager* GetPgTxnManager() { return pg_txn_manager_.get(); }

  //------------------------------------------------------------------------------------------------
  // Expressions.
  //------------------------------------------------------------------------------------------------
  // Column reference.
  CHECKED_STATUS NewColumnRef(PgStatement *handle, int attr_num, const PgTypeEntity *type_entity,
                              const PgTypeAttrs *type_attrs, PgExpr **expr_handle);

  // Constant expressions.
  CHECKED_STATUS NewConstant(YBCPgStatement stmt, const YBCPgTypeEntity *type_entity,
                             uint64_t datum, bool is_null, YBCPgExpr *expr_handle);

  // TODO(neil) UpdateConstant should be merged into one.
  // Update constant.
  template<typename value_type>
  CHECKED_STATUS UpdateConstant(PgExpr *expr, value_type value, bool is_null) {
    if (expr->opcode() != PgExpr::Opcode::PG_EXPR_CONSTANT) {
      // Invalid handle.
      return STATUS(InvalidArgument, "Invalid expression handle for constant");
    }
    down_cast<PgConstant*>(expr)->UpdateConstant(value, is_null);
    return Status::OK();
  }
  CHECKED_STATUS UpdateConstant(PgExpr *expr, const char *value, bool is_null);
  CHECKED_STATUS UpdateConstant(PgExpr *expr, const void *value, int64_t bytes, bool is_null);

  // Operators.
  CHECKED_STATUS NewOperator(PgStatement *stmt, const char *opname,
                             const YBCPgTypeEntity *type_entity,
                             PgExpr **op_handle);
  CHECKED_STATUS OperatorAppendArg(PgExpr *op_handle, PgExpr *arg);

 private:
  // Control variables.
  PggateOptions pggate_options_;

  // Metrics.
  gscoped_ptr<MetricRegistry> metric_registry_;
  scoped_refptr<MetricEntity> metric_entity_;

  // Memory tracker.
  std::shared_ptr<MemTracker> mem_tracker_;

  // YBClient is to communicate with either master or tserver.
  yb::client::AsyncClientInitialiser async_client_init_;

  // TODO(neil) Map for environments (we should have just one ENV?). Environments should contain
  // all the custom flags the PostgreSQL sets. We ignore them all for now.
  PgEnv::SharedPtr pg_env_;

  scoped_refptr<server::HybridClock> clock_;
  scoped_refptr<PgTxnManager> pg_txn_manager_;

  // Mapping table of YugaByte and PostgreSQL datatypes.
  std::unordered_map<int, const YBCPgTypeEntity *>type_map_;
};

}  // namespace pggate
}  // namespace yb

#endif // YB_YQL_PGGATE_PGGATE_H_
