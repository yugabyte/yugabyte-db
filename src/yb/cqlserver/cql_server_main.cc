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

DEFINE_string(cqlserver_master_addrs, "127.0.0.1:7051",
              "Comma-separated addresses of the masters the CQL server to connect to.");
TAG_FLAG(cqlserver_master_addrs, stable);

DECLARE_string(rpc_bind_addresses);
DECLARE_int32(rpc_num_service_threads);

namespace yb {
namespace cqlserver {

static int CQLServerMain(int argc, char** argv) {
  InitYBOrDie();

  ParseCommandLineFlags(&argc, &argv, true);
  if (argc != 1) {
    std::cerr << "usage: " << argv[0] << std::endl;
    return 1;
  }
  InitGoogleLoggingSafe(argv[0]);

  CQLServerOptions opts;
  opts.master_addresses_flag = FLAGS_cqlserver_master_addrs;
  CQLServer server(opts);
  LOG(INFO) << "Initializing CQL server...";
  CHECK_OK(server.Init());

  LOG(INFO) << "Starting CQL server...";
  CHECK_OK(server.Start());

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
