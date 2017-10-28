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
#include <gflags/gflags.h>

#include "yb/gutil/strings/substitute.h"
#include "yb/cqlserver/cql_server.h"
#include "yb/util/flags.h"
#include "yb/util/flag_tags.h"
#include "yb/util/init.h"
#include "yb/util/logging.h"

using yb::cqlserver::CQLServer;

DEFINE_string(cql_proxy_broadcast_rpc_address, "",
              "RPC address to broadcast to other nodes. This is the broadcast_address used in the"
                  " system.local table");
DEFINE_string(cql_proxy_bind_address, "", "Address to bind the CQL proxy to");
DEFINE_int32(cql_proxy_webserver_port, 0, "Webserver port for CQL proxy");
DEFINE_string(cqlserver_master_addrs, "127.0.0.1:7100",
              "Comma-separated addresses of the masters the CQL server to connect to.");
TAG_FLAG(cqlserver_master_addrs, stable);

DECLARE_string(rpc_bind_addresses);

namespace yb {
namespace cqlserver {

static int CQLServerMain(int argc, char** argv) {
  // Reset some default values before parsing gflags.
  FLAGS_cql_proxy_bind_address = strings::Substitute("0.0.0.0:$0", CQLServer::kDefaultPort);
  ParseCommandLineFlags(&argc, &argv, true);
  if (argc != 1) {
    std::cerr << "usage: " << argv[0] << std::endl;
    return 1;
  }
  // TODO: update this to a valid server type if we ever use this!
  InitYBOrDie("");
  InitGoogleLoggingSafe(argv[0]);

  CQLServerOptions cql_server_options;
  cql_server_options.rpc_opts.rpc_bind_addresses = FLAGS_cql_proxy_bind_address;
  cql_server_options.webserver_opts.port = FLAGS_cql_proxy_webserver_port;
  cql_server_options.master_addresses_flag = FLAGS_cqlserver_master_addrs;
  cql_server_options.broadcast_rpc_address = FLAGS_cql_proxy_broadcast_rpc_address;
  boost::asio::io_service io;
  CQLServer server(cql_server_options, &io, nullptr);
  LOG(INFO) << "Starting CQL server...";
  CHECK_OK(server.Start());
  LOG(INFO) << "CQL server successfully started.";

  // Should continue running forever, unless there is some error.
  boost::system::error_code ec;
  io.run(ec);
  if (ec) {
    LOG(WARNING) << "IO service run failure: " << ec;
  }

  LOG (WARNING) << "Shutting down CQL server...";
  server.Shutdown();

  return 0;
}

}  // namespace cqlserver
}  // namespace yb

int main(int argc, char** argv) {
  return yb::cqlserver::CQLServerMain(argc, argv);
}
