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

#include <glog/logging.h>
#include <iostream>

#include "kudu/gutil/strings/substitute.h"
#include "kudu/master/master.h"
#include "kudu/util/flags.h"
#include "kudu/util/init.h"
#include "kudu/util/logging.h"

using kudu::master::Master;

DECLARE_string(rpc_bind_addresses);
DECLARE_int32(webserver_port);
DECLARE_bool(evict_failed_followers);

namespace kudu {
namespace master {

static int MasterMain(int argc, char** argv) {
  InitKuduOrDie();

  // Reset some default values before parsing gflags.
  FLAGS_rpc_bind_addresses = strings::Substitute("0.0.0.0:$0",
                                                 Master::kDefaultPort);
  FLAGS_webserver_port = Master::kDefaultWebPort;

  // A multi-node Master leader should not evict failed Master followers
  // because there is no-one to assign replacement servers in order to maintain
  // the desired replication factor. (It's not turtles all the way down!)
  FLAGS_evict_failed_followers = false;

  ParseCommandLineFlags(&argc, &argv, true);
  if (argc != 1) {
    std::cerr << "usage: " << argv[0] << std::endl;
    return 1;
  }
  InitGoogleLoggingSafe(argv[0]);

  MasterOptions opts;
  Master server(opts);
  LOG(INFO) << "Initializing master server...";
  CHECK_OK(server.Init());

  LOG(INFO) << "Starting Master server...";
  CHECK_OK(server.Start());

  LOG(INFO) << "Master server successfully started.";
  while (true) {
    SleepFor(MonoDelta::FromSeconds(60));
  }

  return 0;
}

} // namespace master
} // namespace kudu

int main(int argc, char** argv) {
  return kudu::master::MasterMain(argc, argv);
}
