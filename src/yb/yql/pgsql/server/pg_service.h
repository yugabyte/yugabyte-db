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
// This file contains the CQLServiceImpl class that implements the CQL server to handle requests
// from Cassandra clients using the CQL native protocol.

#ifndef YB_YQL_PGSQL_SERVER_PG_SERVICE_H_
#define YB_YQL_PGSQL_SERVER_PG_SERVICE_H_

#include <memory>

#include "yb/yql/pgsql/util/pg_env.h"

#include "yb/yql/pgsql/server/pg_server.h"
#include "yb/yql/pgsql/server/pg_server_options.h"
#include "yb/yql/pgsql/server/pg_service.service.h"

namespace yb {
namespace pgserver {

class PgServiceImpl : public PgServerServiceIf {
 public:
  PgServiceImpl(PgServer *server, const PgServerOptions& opts);
  virtual ~PgServiceImpl();

  void Handle(yb::rpc::InboundCallPtr call) override;

  // Return the YBClient to communicate with either master or tserver.
  const std::shared_ptr<client::YBClient>& client() const;

 private:
  // YBClient is to communicate with either master or tserver.
  yb::client::AsyncClientInitialiser async_client_init_;

  // PgEnv is constructed here, and is used by PgProcessor to perform its task.
  // PgEnv contains all information that are shared for the entire PgSql server.
  // This is different from PgSession, whose contents are shared within one connection.
  pgsql::PgEnv::SharedPtr pg_env_;
};

}  // namespace pgserver
}  // namespace yb

#endif  // YB_YQL_PGSQL_SERVER_PG_SERVICE_H_
