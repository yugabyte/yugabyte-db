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

#include "kudu/master/master_options.h"

#include <glog/logging.h>
#include <gflags/gflags.h>

#include "kudu/master/master.h"
#include "kudu/util/flag_tags.h"

namespace kudu {
namespace master {

DEFINE_string(master_addresses, "",
              "Comma-separated list of all the RPC addresses for Master config. "
              "This is used to configure the replicated Master process "
              "(currently considered experimental). "
              "NOTE: if not specified, configures a non-replicated Master.");
TAG_FLAG(master_addresses, experimental);

MasterOptions::MasterOptions() {
  rpc_opts.default_port = Master::kDefaultPort;

  if (!FLAGS_master_addresses.empty()) {
    Status s = HostPort::ParseStrings(FLAGS_master_addresses, Master::kDefaultPort,
                                      &master_addresses);
    if (!s.ok()) {
      LOG(FATAL) << "Couldn't parse the master_addresses flag('" << FLAGS_master_addresses << "'): "
                 << s.ToString();
    }
    if (master_addresses.size() < 2) {
      LOG(FATAL) << "At least 2 masters are required for a distributed config, but "
          "master_addresses flag ('" << FLAGS_master_addresses << "') only specifies "
                 << master_addresses.size() << " masters.";
    }
    if (master_addresses.size() == 2) {
      LOG(WARNING) << "Only 2 masters are specified by master_addresses_flag ('" <<
          FLAGS_master_addresses << "'), but minimum of 3 are required to tolerate failures"
          " of any one master. It is recommended to use at least 3 masters.";
    }
  }
}

bool MasterOptions::IsDistributed() const {
  return !master_addresses.empty();
}

} // namespace master
} // namespace kudu
