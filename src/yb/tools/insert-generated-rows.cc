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
// Simple tool to insert "random junk" rows into an arbitrary table.
// First column is in ascending order, the rest are random data.
// Helps make things like availability demos a little easier.

#include <gflags/gflags.h>
#include <glog/logging.h>
#include <iostream>
#include <memory>
#include <vector>

#include "yb/client/client.h"
#include "yb/gutil/stl_util.h"
#include "yb/gutil/strings/split.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/tools/data_gen_util.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/random.h"
#include "yb/util/random_util.h"

DEFINE_string(master_address, "localhost",
              "Comma separated list of master addresses to run against.");

namespace yb {
namespace tools {

using std::string;
using std::vector;

using client::YBClient;
using client::YBClientBuilder;
using client::YBColumnSchema;
using client::YBInsert;
using client::YBSchema;
using client::YBSession;
using client::YBTable;
using client::sp::shared_ptr;

void PrintUsage(char** argv) {
  std::cerr << "usage: " << argv[0] << " [--master_address localhost] <table_name>"
            << std::endl;
}

static int WriteRandomDataToTable(int argc, char** argv) {
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

  // Set up client.
  LOG(INFO) << "Connecting to YB Master...";
  shared_ptr<YBClient> client;
  CHECK_OK(YBClientBuilder()
           .master_server_addrs(addrs)
           .Build(&client));

  LOG(INFO) << "Opening table...";
  shared_ptr<YBTable> table;
  CHECK_OK(client->OpenTable(table_name, &table));
  YBSchema schema = table->schema();

  shared_ptr<YBSession> session = client->NewSession();
  session->SetTimeoutMillis(5000); // Time out after 5 seconds.
  CHECK_OK(session->SetFlushMode(YBSession::MANUAL_FLUSH));

  Random random(GetRandomSeed32());

  LOG(INFO) << "Inserting random rows...";
  for (uint64_t record_id = 0; true; ++record_id) {
    shared_ptr<YBInsert> insert(table->NewInsert());
    YBPartialRow* row = insert->mutable_row();
    GenerateDataForRow(schema, record_id, &random, row);

    LOG(INFO) << "Inserting record: " << row->ToString();
    CHECK_OK(session->Apply(insert));
    Status s = session->Flush();
    if (PREDICT_FALSE(!s.ok())) {
      std::vector<client::YBError*> errors;
      ElementDeleter d(&errors);
      bool overflow;
      session->GetPendingErrors(&errors, &overflow);
      CHECK(!overflow);
      for (const client::YBError* e : errors) {
        if (e->status().IsAlreadyPresent()) {
          LOG(WARNING) << "Ignoring insert error: " << e->status().ToString();
        } else {
          LOG(FATAL) << "Unexpected insert error: " << e->status().ToString();
        }
      }
      continue;
    }
    LOG(INFO) << "OK";
  }

  return 0;
}

} // namespace tools
} // namespace yb

int main(int argc, char** argv) {
  return yb::tools::WriteRandomDataToTable(argc, argv);
}
