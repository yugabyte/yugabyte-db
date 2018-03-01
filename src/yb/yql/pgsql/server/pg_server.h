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
// This file contains the PGSQLServer class that listens for connections from Cassandra clients
// using the PGSQL native protocol.

#ifndef YB_YQL_PGSQL_SERVER_PG_SERVER_H
#define YB_YQL_PGSQL_SERVER_PG_SERVER_H

#include <string>

#include "yb/gutil/gscoped_ptr.h"
#include "yb/gutil/macros.h"
#include "yb/server/server_base.h"
#include "yb/tserver/tablet_server.h"
#include "yb/util/status.h"

#include "yb/yql/pgsql/server/pg_server_options.h"

namespace yb {
namespace pgserver {

class PgServer : public server::RpcAndWebServerBase {
 public:
  static const uint16_t kDefaultPort = 5432;
  static const uint16_t kDefaultWebPort = 13000;

  // Construct server.
  PgServer(const PgServerOptions& opts, const tserver::TabletServer* tserver);

  // Start server.
  CHECKED_STATUS Start();

  // Keep the server main thread running by checking server status periodically.
  CHECKED_STATUS KeepAlive();

  // Shutdown before exiting.
  void Shutdown();

  // Access methods.
  const tserver::TabletServer* tserver() const { return tserver_; }

 private:
  PgServerOptions opts_;
  const tserver::TabletServer* const tserver_;

  DISALLOW_COPY_AND_ASSIGN(PgServer);
};

} // namespace pgserver
} // namespace yb
#endif // YB_YQL_PGSQL_SERVER_PG_SERVER_H
