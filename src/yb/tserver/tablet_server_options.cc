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

#include "yb/tserver/tablet_server_options.h"

#include <glog/logging.h>
#include <gflags/gflags.h>

#include "yb/master/master.h"
#include "yb/tserver/tablet_server.h"
#include "yb/util/flag_tags.h"
#include "yb/util/flags.h"

using std::vector;

namespace yb {
namespace tserver {

TabletServerOptions::TabletServerOptions() {
  rpc_opts.default_port = TabletServer::kDefaultPort;

  master_addresses_flag = FLAGS_tserver_master_addrs;

  vector<HostPort> master_addresses;
  Status s = HostPort::ParseStrings(FLAGS_tserver_master_addrs,
                                    master::kMasterDefaultPort,
                                    &master_addresses);
  SetMasterAddresses(std::make_shared<std::vector<HostPort>>(std::move(master_addresses)));
  if (!s.ok()) {
    LOG(FATAL) << "Couldn't parse " << FLAGS_tserver_master_addrs << " flag: " << s.ToString();
  }

  ValidateMasterAddresses();
}

void TabletServerOptions::ValidateMasterAddresses() const {
  addresses_shared_ptr master_addresses = GetMasterAddresses();
  if (master_addresses->empty()) {
    LOG(FATAL) << "No masters were specified in the master addresses flag '"
               << master_addresses_flag << "', but a minimum of one is required.";
  }
}

}  // namespace tserver
}  // namespace yb
