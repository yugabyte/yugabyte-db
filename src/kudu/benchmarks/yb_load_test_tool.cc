// Copyright (c) YugaByte, Inc. All rights reserved.

#include <glog/logging.h>

#include <boost/bind.hpp>

#include <glog/logging.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "kudu/benchmarks/tpch/line_item_tsv_importer.h"
#include "kudu/benchmarks/tpch/rpc_line_item_dao.h"
#include "kudu/benchmarks/tpch/tpch-schemas.h"
#include "kudu/gutil/stl_util.h"
#include "kudu/gutil/strings/join.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/integration-tests/external_mini_cluster.h"
#include "kudu/util/atomic.h"
#include "kudu/util/env.h"
#include "kudu/util/errno.h"
#include "kudu/util/flags.h"
#include "kudu/util/logging.h"
#include "kudu/util/monotime.h"
#include "kudu/util/stopwatch.h"
#include "kudu/util/subprocess.h"
#include "kudu/util/thread.h"


DEFINE_string(
    yb_load_test_master_addresses, "localhost",
    "Addresses of masters for the cluster to operate on");

using namespace kudu::client;
using namespace kudu::client::sp;

int main(int argc, char* argv[]) {
  gflags::SetUsageMessage(
    "Usage: yb_load_test_tool --yb_load_test_master_addresses master1:port1,...,masterN:portN"
  );
  kudu::ParseCommandLineFlags(&argc, &argv, true);
  kudu::InitGoogleLoggingSafe(argv[0]);

  shared_ptr<KuduClient> client;
  CHECK_OK(KuduClientBuilder()
     .add_master_server_addr(FLAGS_yb_load_test_master_addresses)
     .Build(&client));
  shared_ptr<KuduSession> session(client->NewSession());
  CHECK_OK(session->Close());
  LOG(INFO) << "Test completed";
  return 0;
}
