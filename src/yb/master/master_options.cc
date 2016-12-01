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

#include "yb/master/master_options.h"

#include <glog/logging.h>
#include <gflags/gflags.h>

#include "yb/master/master.h"
#include "yb/util/flag_tags.h"

using std::make_shared;
using std::vector;

namespace yb {
namespace master {

DEFINE_string(master_addresses, "",
    "Comma-separated list of all the RPC addresses of the initial Masters when you "
    "create a cluster. This should only be set the very first time when the cluster is "
    "brought up and must be used together with the create_cluster flag. "
    "If this flag is set and it is not the first start of this node (local consensus metadata "
    "already exists) that is considered a fatal error and will intentionally crash the process! "
    "This flag also defaults to empty, which is overloaded to be able to either: "
    "a) start a non-distributed mode Master if create_cluster is true. "
    "b) allow for a Master to be restarted gracefully and get its peer list from the "
    "local cmeta file of the last committed config, if create_cluster is false.");
TAG_FLAG(master_addresses, experimental);

DEFINE_bool(create_cluster, false,
    "This should only be set the very first time a cluster is brought up. It signals that we wish "
    "to start a new clean deployment and generate the filesystem data from scratch. This has to "
    "be used in conjunction with the master_addresses flag, to inform the other masters of their "
    "starting peer group.");

MasterOptions::MasterOptions(
    server::ServerBaseOptions::addresses_shared_ptr master_addresses,
    bool is_creating)
    : is_creating_(is_creating),
      is_shell_mode_(false) {
  rpc_opts.default_port = Master::kDefaultPort;

  SetMasterAddresses(master_addresses);
  ValidateMasterAddresses();
}

MasterOptions::MasterOptions()
    : is_creating_(FLAGS_create_cluster),
      is_shell_mode_(false) {
  rpc_opts.default_port = Master::kDefaultPort;
  master_addresses_flag = FLAGS_master_addresses;
  if (FLAGS_master_addresses.empty()) {
    SetMasterAddresses(make_shared<vector<HostPort>>());
  } else {
    vector<HostPort> master_addresses;
    Status s = HostPort::ParseStrings(FLAGS_master_addresses, Master::kDefaultPort,
                                      &master_addresses);
    if (!s.ok()) {
      LOG(FATAL) << "Couldn't parse the master_addresses flag ('"
                 << FLAGS_master_addresses << "'): " << s.ToString();
    }
    SetMasterAddresses(make_shared<vector<HostPort>>(std::move(master_addresses)));
  }

  ValidateMasterAddresses();
}

MasterOptions::MasterOptions(const MasterOptions& other)
    : is_shell_mode_(false)  {
  SetMasterAddresses(other.GetMasterAddresses());
  is_creating_ = other.is_creating_;
  is_shell_mode_.Store(other.IsShellMode());
  rpc_opts.default_port = other.rpc_opts.default_port;

  ValidateMasterAddresses();
}

void MasterOptions::ValidateMasterAddresses() const {
  server::ServerBaseOptions::addresses_shared_ptr master_addresses = GetMasterAddresses();
  if (!master_addresses->empty()) {
    if (master_addresses->size() < 2) {
      LOG(FATAL) << "At least 2 masters are required for a distributed config, but "
                 << "master addresses flag " <<  FLAGS_master_addresses
                 << " only specifies " << master_addresses->size() << " masters.";
    }
  }

  server::ServerBaseOptions::ValidateMasterAddresses();
}

} // namespace master
} // namespace yb
