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

#include "yb/util/metrics.h"
#include "yb/util/mem_tracker.h"

#include "yb/client/client.h"
#include "yb/client/callbacks.h"
#include "yb/client/async_initializer.h"
#include "yb/server/server_base_options.h"

#include "yb/yql/pggate/pg_env.h"
#include "yb/yql/pggate/pg_session.h"
#include "yb/yql/pggate/pg_statement.h"

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
  PgApiImpl();
  virtual ~PgApiImpl() {
  }

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
  PgSession::SharedPtr GetSession(PgSession *handle);

  // Read statement.
  PgStatement::SharedPtr GetStatement(PgStatement *handle);

  //------------------------------------------------------------------------------------------------
  // Connect database. Switch the connected database to the given "database_name".
  CHECKED_STATUS ConnectDatabase(PgSession *pg_session, const char *database_name);

  // Create database.
  CHECKED_STATUS AllocCreateDatabase(PgSession *pg_session,
                                     const char *database_name,
                                     PgStatement **handle);
  CHECKED_STATUS ExecCreateDatabase(PgStatement *handle);

  // Drop database.
  CHECKED_STATUS AllocDropDatabase(PgSession *pg_session,
                                   const char *database_name,
                                   bool if_exist,
                                   PgStatement **handle);
  CHECKED_STATUS ExecDropDatabase(PgStatement *handle);

  //------------------------------------------------------------------------------------------------
  // Create and drop schema.
  // - When "database_name" is NULL, the connected database name is used.
  CHECKED_STATUS AllocCreateSchema(PgSession *pg_session,
                                   const char *database_name,
                                   const char *schema_name,
                                   bool if_not_exist,
                                   PgStatement **handle);

  CHECKED_STATUS ExecCreateSchema(PgStatement *handle);

  CHECKED_STATUS AllocDropSchema(PgSession *pg_session,
                                 const char *database_name,
                                 const char *schema_name,
                                 bool if_exist,
                                 PgStatement **handle);

  CHECKED_STATUS ExecDropSchema(PgStatement *handle);

  //------------------------------------------------------------------------------------------------
  // Create and drop table.
  CHECKED_STATUS AllocCreateTable(PgSession *pg_session,
                                  const char *database_name,
                                  const char *schema_name,
                                  const char *table_name,
                                  bool if_not_exist,
                                  PgStatement **handle);

  CHECKED_STATUS CreateTableAddColumn(PgStatement *handle, const char *attr_name, int attr_num,
                                      int attr_ybtype, bool is_hash, bool is_range);

  CHECKED_STATUS ExecCreateTable(PgStatement *handle);

  CHECKED_STATUS AllocDropTable(PgSession *pg_session,
                                const char *database_name,
                                const char *schema_name,
                                const char *table_name,
                                bool if_exist,
                                PgStatement **handle);

  CHECKED_STATUS ExecDropTable(PgStatement *handle);

  //------------------------------------------------------------------------------------------------
  // Insert.
  CHECKED_STATUS AllocInsert(PgSession *pg_session,
                             const char *database_name,
                             const char *schema_name,
                             const char *table_name,
                             PgStatement **handle);

  CHECKED_STATUS InsertSetColumnInt2(PgStatement *handle, int attr_num, int16_t attr_value);

  CHECKED_STATUS InsertSetColumnInt4(PgStatement *handle, int attr_num, int32_t attr_value);

  CHECKED_STATUS InsertSetColumnInt8(PgStatement *handle, int attr_num, int64_t attr_value);

  CHECKED_STATUS InsertSetColumnFloat4(PgStatement *handle, int attr_num, float attr_value);

  CHECKED_STATUS InsertSetColumnFloat8(PgStatement *handle, int attr_num, double attr_value);

  CHECKED_STATUS InsertSetColumnText(PgStatement *handle, int attr_num, const char *attr_value,
                                     int attr_bytes);

  CHECKED_STATUS InsertSetColumnSerializedData(PgStatement *handle, int attr_num,
                                               const char *attr_value, int attr_bytes);

  CHECKED_STATUS ExecInsert(PgStatement *handle);

  //------------------------------------------------------------------------------------------------
  // Update.

  //------------------------------------------------------------------------------------------------
  // Delete.

  //------------------------------------------------------------------------------------------------
  // Select.

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

  // List of session shared_ptr. When destroying session, remove it from this list.
  // Our internal might still have reference to this session while the Postgres API might instruct
  // YugaByte to destroy the session whenever users cancel a connection / session. Removing
  // shared_ptr from map instead of calling "free(ptr)" will save us from crashing.
  std::unordered_map<PgSession*, PgSession::SharedPtr> sessions_;

  // List of handle shared_ptr. When destroying a handle, remove it from this list.
  // Our internal might still have reference to this handle while the Postgres API might instruct
  // YugaByte to destroy the handle whenever users cancel a connection / session. Removing
  // shared_ptr instead of calling "free(ptr)" will save us from crashing.
  std::unordered_map<PgStatement*, PgStatement::SharedPtr> statements_;
};

}  // namespace pggate
}  // namespace yb

#endif // YB_YQL_PGGATE_PGGATE_H_
