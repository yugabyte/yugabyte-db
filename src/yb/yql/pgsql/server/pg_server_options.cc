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

#include "yb/yql/pgsql/server/pg_server_options.h"

#include "yb/util/flag_tags.h"
#include "yb/yql/pgsql/server/pg_rpc.h"
#include "yb/yql/pgsql/server/pg_server.h"

DEFINE_int32(pgsql_rpc_keepalive_time_ms, 0,
             "If an RPC connection from a client is idle for this amount of time, the server "
             "will disconnect the client. Setting flag to 0 disables this clean up.");
TAG_FLAG(pgsql_rpc_keepalive_time_ms, advanced);

namespace yb {
namespace pgserver {

PgServerOptions::PgServerOptions() {
  server_type = "tserver";
  rpc_opts.default_port = PgServer::kDefaultPort;
  rpc_opts.connection_keepalive_time_ms = FLAGS_pgsql_rpc_keepalive_time_ms;
}

} // namespace pgserver
} // namespace yb
