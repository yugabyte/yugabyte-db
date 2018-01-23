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
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include "yb/master/master_options.h"

#include <glog/logging.h>
#include <gflags/gflags.h>

#include "yb/master/master.h"
#include "yb/util/flag_tags.h"

#include "yb/gutil/strings/join.h"

using std::make_shared;
using std::vector;

using namespace std::literals;

namespace yb {
namespace master {

DEFINE_string(master_addresses, "",
    "Comma-separated list of the host/port RPC addresses of the peer masters. This is needed "
    "for initial cluster create, and is recreated from persisted metadata on master restart."
    "This flag also defaults to empty, which is overloaded to be able to either: "
    "a) start a non-distributed mode master if local instance file is not present. "
    "b) allow for a master to be restarted gracefully and get its peer list from the "
    "local cmeta file of the last committed config, if local instance file is present.");
TAG_FLAG(master_addresses, experimental);
DEFINE_uint64(master_replication_factor, 0,
    "Number of master replicas. By default it is detected based on master_addresses option, but "
    "could be specified explicitly together with passing one or more master service domain name and"
    " port through master_addresses for masters auto-discovery when running on Kubernetes.");

// NOTE: This flag is deprecated.
DEFINE_bool(create_cluster, false,
  "(DEPRECATED). This flag was earlier used to distinguish if the master process is "
  "being started to create a cluster or if this just a restart.");
TAG_FLAG(create_cluster, hidden);

const char* MasterOptions::kServerType = "master";

MasterOptions::MasterOptions(server::ServerBaseOptions::addresses_shared_ptr master_addresses) {
  server_type = kServerType;
  rpc_opts.default_port = kMasterDefaultPort;

  SetMasterAddresses(master_addresses);
}

Result<MasterOptions> MasterOptions::CreateMasterOptions() {
  const auto resolve_period = 1s;

  vector<HostPort> master_addresses = std::vector<HostPort>();
  if (!FLAGS_master_addresses.empty()) {
    Status s = HostPort::ParseStrings(FLAGS_master_addresses,
                                      kMasterDefaultPort,
                                      &master_addresses);
    if (!s.ok()) {
      LOG(FATAL) << "Couldn't parse the master_addresses flag ('"
                 << FLAGS_master_addresses << "'): " << s.ToString();
    }
  }

  std::string master_addresses_flag;
  if (FLAGS_master_replication_factor > 0) {
    vector<Endpoint> addrs;
    for (;;) {
      addrs.clear();
      for (auto hp : master_addresses) {
        auto s = hp.ResolveAddresses(&addrs);
        if (!s.ok()) {
          LOG(WARNING) << s;
        }
      }
      if (addrs.size() >= FLAGS_master_replication_factor) {
        break;
      }
      std::this_thread::sleep_for(resolve_period);
    }
    if (addrs.size() > FLAGS_master_replication_factor) {
      return STATUS_FORMAT(
          ConfigurationError, "Expected $0 master endpoints, but got: $1",
          FLAGS_master_replication_factor, ToString(addrs));
    }
    LOG(INFO) << Format("Resolved master addresses: $0", ToString(addrs));
    master_addresses.clear();
    vector<string> master_addr_strings;
    for (auto addr : addrs) {
      auto hp = HostPort(addr);
      master_addresses.emplace_back(hp);
      master_addr_strings.emplace_back(hp.ToString());
    }
    master_addresses_flag = JoinStrings(master_addr_strings, ",");
  } else {
    master_addresses_flag = FLAGS_master_addresses;
  }

  MasterOptions opts(make_shared<vector<HostPort>>(std::move(master_addresses)));
  opts.master_addresses_flag = master_addresses_flag;
  return opts;
}

} // namespace master
} // namespace yb
