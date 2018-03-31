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
// This class represents a QL session of a client connection (e.g. CQL client connection).
//
// NOTES:
// When modifying this file, please pay attention to the following notes.
// * PgEnv class: Each server has one PgEnv
//   - All connections & all processors will share the info here.
//   - Connection to master and tablet (YBClient client_).
//   - Cache that is for all connection from all clients.
// * PgSession class: Each connection has one PgSession.
//   - The processors for requests of the same connection will share this.
//--------------------------------------------------------------------------------------------------
#ifndef YB_YQL_PGSQL_UTIL_PG_SESSION_H_
#define YB_YQL_PGSQL_UTIL_PG_SESSION_H_

#include "yb/yql/pgsql/util/pg_env.h"
#include "yb/yql/pgsql/ybpostgres/pg_pqcomm.h"
#include "yb/yql/pgsql/ybpostgres/pg_send.h"
#include "yb/yql/pgsql/ybpostgres/pg_port.h"

namespace yb {
namespace pgsql {

// TODO(neil) When combining RPC and Compiler diffs, replace all 3 pgport, pgsend, and pgrecv in
// PgConnectionContext with PgSession.
class PgSession {
 public:
  // Public types.
  typedef std::shared_ptr<PgSession> SharedPtr;
  typedef std::shared_ptr<const PgSession> SharedPtrConst;

  // Constructors.
  PgSession(const PgEnv::SharedPtr& pg_env,
            const pgapi::StringInfo& postgres_packet,
            pgapi::ProtocolVersion proto);
  virtual ~PgSession() { }

  //------------------------------------------------------------------------------------------------
  // API for read & write operations.
  CHECKED_STATUS Apply(const std::shared_ptr<client::YBPgsqlOp>& op);

  //------------------------------------------------------------------------------------------------
  // Access functions.

  const pgapi::PGSend& pgsend() const {
    return pgsend_;
  }
  const pgapi::PGPort& pgport() const {
    return pgport_;
  }

  // Access functions for current schema.
  // TODO(neil) Per Mikhail, need to do the following.
  // - Don't support SCHEMA. Error should be raised here.
  // - Treat PG DATABASE the same as CQL's keyspace. Will redo this work for DATABASE.
  // - This work can be done quickly, so get the DocDB working first, then go back to this.
  //   For now, just accept SCHEMA 'test'.
  //
  // TODO(neil) Need to double check these code later.
  // - This code in CQL processor has a lock. CQL comment: It can be accessed by mutiple calls in
  //   parallel so they need to be thread-safe for shared reads / exclusive writes.
  //
  // - Currently, for each session, server executes the client requests sequentially, so the
  //   the following mutex is not necessary. I don't think we're capable of parallel-processing
  //   multiple statements within one session.
  //
  // TODO(neil) MUST ADD A LOCK FOR ACCESSING CURRENT DATABASE BECAUSE WE USE THIS VARIABLE AS
  // INDICATOR FOR ALIVE OR DEAD SESSIONS.
  const string& current_database() const {
    return current_database_;
  }
  void set_current_database(const std::string& database) {
    current_database_ = database;
  }
  void reset_current_database() {
    current_database_ = "";
  }

 private:
  // The environment this session belongs to.
  PgEnv::SharedPtr pg_env_;

  // Caching the information of the connection that is associated with this ClientSession which can
  // include some or all of the following.
  // - application name (psql)
  // - host
  // - port
  // - user
  // - password
  // - dbname
  // - char encoding (We only support UTF8)
  // - datetime format (We can support only ISO at the start)
  // - integer_datetimes (ON or OFF)
  // - interval style
  pgapi::PGPort pgport_;
  pgapi::PGSend pgsend_;

  // Server states that are associated with this connection.
  // - Postgresql version: This is not Yugabyte version. Currently, our code is equivalent with
  //   postgresql 10.1.
  // - See "struct bkend" in file "postgresql::src/backend/.../postmaster.c" for info on pid & key.
  //
  // TODO(neil) pid_, key_, state_, and is_superuser should be used for connection status.
  //   int pid_ = 0;
  //   int key_ = 0;
  //   PgSessionState state_ = PgSessionState::STATE_IDLE;
  //   bool is_superuser_ = false;
  string char_encoding_ = "UTF8";
  string version_ = "10.1";
  string time_zone_;

  // YBSession to execute operations.
  std::shared_ptr<client::YBSession> session_;

  // Current database.
  std::string current_database_;
};

}  // namespace pgsql
}  // namespace yb

#endif  // YB_YQL_PGSQL_UTIL_PG_SESSION_H_
