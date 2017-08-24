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

#include <iostream>

#include <glog/logging.h>

#include "yb/gutil/strings/substitute.h"
#include "yb/master/master.h"
#include "yb/util/flags.h"
#include "yb/util/init.h"
#include "yb/util/logging.h"

DECLARE_string(rpc_bind_addresses);
DECLARE_int32(webserver_port);
DECLARE_bool(evict_failed_followers);

namespace yb {
namespace master {

static int MasterMain(int argc, char** argv) {
  InitYBOrDie();

  // Reset some default values before parsing gflags.
  FLAGS_rpc_bind_addresses = strings::Substitute("0.0.0.0:$0", kMasterDefaultPort);
  FLAGS_webserver_port = kMasterDefaultWebPort;

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
  YB_EDITION_NS_PREFIX Master server(opts);
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
} // namespace yb

int main(int argc, char** argv) {
  return yb::master::MasterMain(argc, argv);
}
