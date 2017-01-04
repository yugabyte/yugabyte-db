// Copyright (c) YugaByte, Inc.

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

DEFINE_string(cql_proxy_bind_address, "", "Address to bind the CQL proxy to.");
DEFINE_string(cqlserver_master_addrs, "127.0.0.1:7051",
              "Comma-separated addresses of the masters the CQL server to connect to.");
TAG_FLAG(cqlserver_master_addrs, stable);

DECLARE_string(rpc_bind_addresses);

namespace yb {
namespace cqlserver {

using std::cout;
using std::endl;

static int CQLServerMain(int argc, char** argv) {
  InitYBOrDie();

  // Reset some default values before parsing gflags.
  FLAGS_cql_proxy_bind_address = strings::Substitute("0.0.0.0:$0", CQLServer::kDefaultPort);
  ParseCommandLineFlags(&argc, &argv, true);
  if (argc != 1) {
    std::cerr << "usage: " << argv[0] << std::endl;
    return 1;
  }
  InitGoogleLoggingSafe(argv[0]);

  CQLServerOptions opts;
  opts.rpc_opts.rpc_bind_addresses = FLAGS_cql_proxy_bind_address;
  opts.master_addresses_flag = FLAGS_cqlserver_master_addrs;

  CQLServer server(opts);
  LOG(INFO) << "Starting CQL server...";
  Status s = server.Start();
  CHECK(s.ok()) << s.ToString();
  LOG(INFO) << "CQL server successfully started.";

  while (true) {
    SleepFor(MonoDelta::FromSeconds(60));
  }

  return 0;
}

} // namespace cqlserver
} // namespace yb

int main(int argc, char** argv) {
  return yb::cqlserver::CQLServerMain(argc, argv);
}
