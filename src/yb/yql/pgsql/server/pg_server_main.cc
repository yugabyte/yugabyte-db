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

#include <iostream>

#include <glog/logging.h>

#include "yb/gutil/strings/substitute.h"
#include "yb/yql/pgsql/server/pg_server.h"
#include "yb/util/flags.h"
#include "yb/util/init.h"
#include "yb/util/logging.h"
#include "yb/util/main_util.h"

using yb::pgserver::PgServer;

DEFINE_string(pgsql_proxy_bind_address, "",
              "Address to which the PostgreSQL proxy server is bound.");
DEFINE_string(pgsql_master_addresses, "",
              "Addresses of the master servers to which the PostgreSQL proxy server connects.");

namespace yb {
namespace pgserver {

// Entry for PGSQL server.
static int PgServerDriver(int argc, char** argv) {
  // Reset some default values before parsing gflags.
  if (FLAGS_pgsql_proxy_bind_address.empty()) {
    FLAGS_pgsql_proxy_bind_address = strings::Substitute("0.0.0.0:$0", PgServer::kDefaultPort);
  }
  ParseCommandLineFlags(&argc, &argv, true);
  if (argc != 1) {
    std::cerr << "usage: " << argv[0] << std::endl;
    return 1;
  }
  LOG_AND_RETURN_FROM_MAIN_NOT_OK(InitYB("pgsqlserver"));
  InitGoogleLoggingSafe(argv[0]);

  // Construct server.
  PgServerOptions opts;
  opts.rpc_opts.rpc_bind_addresses = FLAGS_pgsql_proxy_bind_address;
  opts.master_addresses_flag = FLAGS_pgsql_master_addresses;
  PgServer server(opts, nullptr /* tserver */);

  // Start server.
  LOG(INFO) << "Starting PGSQL server...";
  LOG_AND_RETURN_FROM_MAIN_NOT_OK(server.Start());
  LOG(INFO) << "PGSQL server successfully started.";

  // Keep server alive as long there is no error.
  while (server.KeepAlive().ok()) continue;

  // Shutdown and exit.
  server.Shutdown();
  return 0;
}

}  // namespace pgserver
}  // namespace yb

int main(int argc, char** argv) {
  return yb::pgserver::PgServerDriver(argc, argv);
}
