// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// Simple tool to send an CREATE TABLE request for one of the demo tablets.
// This will eventually be replaced by a proper shell -- just a quick
// hack for easy demo purposes.

#include <gflags/gflags.h>
#include <glog/logging.h>
#include <iostream>
#include <vector>

#include "kudu/benchmarks/tpch/tpch-schemas.h"
#include "kudu/benchmarks/ycsb-schema.h"
#include "kudu/client/client.h"
#include "kudu/gutil/strings/split.h"
#include "kudu/tserver/tserver.pb.h"
#include "kudu/tserver/tserver_service.proxy.h"
#include "kudu/twitter-demo/twitter-schema.h"
#include "kudu/util/env.h"
#include "kudu/util/faststring.h"
#include "kudu/util/flags.h"
#include "kudu/util/logging.h"

using kudu::client::KuduClient;
using kudu::client::KuduClientBuilder;
using kudu::client::KuduSchema;
using kudu::client::KuduTableCreator;
using kudu::client::sp::shared_ptr;
using kudu::rpc::RpcController;
using std::string;
using std::vector;

DEFINE_string(master_address, "localhost",
              "Comma separated list of master addresses to run against.");

static const char* const kTwitterTabletId = "twitter";
static const char* const kTPCH1TabletId = "tpch1";
static const char* const kYCSBTabletId = "ycsb";

namespace kudu {

void PrintUsage(char** argv) {
  std::cerr << "usage: " << argv[0] << " "
            << kTwitterTabletId << "|"
            << kTPCH1TabletId << "|"
            << kYCSBTabletId
            << std::endl;
}

string LoadFile(const string& path) {
  faststring buf;
  CHECK_OK(ReadFileToString(Env::Default(), path, &buf));
  return buf.ToString();
}

// TODO: refactor this and the associated constants into some sort of
// demo-tables.h class in a src/demos/ directory.
Status GetDemoSchema(const string& table_name, KuduSchema* schema) {
  if (table_name == kTwitterTabletId) {
    *schema = twitter_demo::CreateTwitterSchema();
  } else if (table_name == kTPCH1TabletId) {
    *schema = tpch::CreateLineItemSchema();
  } else if (table_name == kYCSBTabletId) {
    *schema = kudu::CreateYCSBSchema();
  } else {
    return Status::InvalidArgument("Invalid demo table name", table_name);
  }
  return Status::OK();
}

static int CreateDemoTable(int argc, char** argv) {
  ParseCommandLineFlags(&argc, &argv, true);
  if (argc != 2) {
    PrintUsage(argv);
    return 1;
  }
  InitGoogleLoggingSafe(argv[0]);
  FLAGS_logtostderr = true;

  string table_name = argv[1];

  vector<string> addrs = strings::Split(FLAGS_master_address, ",");
  CHECK(!addrs.empty()) << "At least one master address must be specified!";

  KuduSchema schema;
  CHECK_OK(GetDemoSchema(table_name, &schema));

  // Set up client.
  shared_ptr<KuduClient> client;
  CHECK_OK(KuduClientBuilder()
           .master_server_addrs(addrs)
           .Build(&client));

  gscoped_ptr<KuduTableCreator> table_creator(client->NewTableCreator());
  CHECK_OK(table_creator->table_name(table_name)
           .schema(&schema)
           .Create());
  return 0;
}

} // namespace kudu

int main(int argc, char** argv) {
  return kudu::CreateDemoTable(argc, argv);
}
