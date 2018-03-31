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
//
// PgEnv defines the interface for the environment where SQL engine is running.
//
// If we support different types of servers underneath SQL engine (which we don't), this class
// should be an abstract interface and let the server (such as proxy server) defines the content.
//
// NOTES:
// When modifying this file, please pay attention to the following notes.
// * PgEnv class:
//   - All connections & all processors will share the info in PgEnv
//   - Connections to master and tablet (such as YBClient client_).
//   - Cache that is used for all connections from all clients.
// * PgSession class: Each connection has one PgSession.
//   - The processors for requests of the same connection will share this.
//--------------------------------------------------------------------------------------------------

#ifndef YB_YQL_PGSQL_UTIL_PG_ENV_H_
#define YB_YQL_PGSQL_UTIL_PG_ENV_H_

#include "yb/client/client.h"
#include "yb/client/callbacks.h"

#include "yb/gutil/callback.h"
#include "yb/rpc/rpc_fwd.h"

namespace yb {
namespace pgsql {

class PgEnv {
 public:
  //------------------------------------------------------------------------------------------------
  // Public types and constants.
  typedef std::unique_ptr<PgEnv> UniPtr;
  typedef std::unique_ptr<const PgEnv> UniPtrConst;

  typedef std::shared_ptr<PgEnv> SharedPtr;
  typedef std::shared_ptr<const PgEnv> SharedPtrConst;

  //------------------------------------------------------------------------------------------------
  // Constructor & destructor.
  explicit PgEnv(std::shared_ptr<client::YBClient> client);
  virtual ~PgEnv();

  // Reset all env states or variables before executing the next statement.
  void Reset();

  //------------------------------------------------------------------------------------------------
  // Session operations.
  std::shared_ptr<client::YBSession> NewSession() {
    return client_->NewSession();
  }

  //------------------------------------------------------------------------------------------------
  // API for database operations.
  virtual CHECKED_STATUS CreateDatabase(const std::string& db_name);
  virtual CHECKED_STATUS DeleteDatabase(const std::string& db_name);
  virtual Result<bool> ConnectDatabase(const std::string& db_name);

  //------------------------------------------------------------------------------------------------
  // API for schema operations.
  // TODO(neil) Schema should be a sub-database that have some specialized property.
  virtual CHECKED_STATUS CreateSchema(const std::string& schema_name);
  virtual CHECKED_STATUS DeleteSchema(const std::string& schema_name);

  //------------------------------------------------------------------------------------------------
  // API for table operations.
  virtual client::YBTableCreator *NewTableCreator();
  virtual std::shared_ptr<client::YBTable> GetTableDesc(const client::YBTableName& table_name);
  virtual CHECKED_STATUS DeleteTable(const client::YBTableName& name);

 private:
  // YBClient, an API that SQL engine uses to communicate with all servers.
  std::shared_ptr<client::YBClient> client_;
};

}  // namespace pgsql
}  // namespace yb

#endif  // YB_YQL_PGSQL_UTIL_PG_ENV_H_
