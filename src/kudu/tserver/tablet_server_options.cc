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

#include "kudu/tserver/tablet_server_options.h"

#include <glog/logging.h>
#include <gflags/gflags.h>

#include "kudu/master/master.h"
#include "kudu/tserver/tablet_server.h"
#include "kudu/util/flag_tags.h"

namespace kudu {
namespace tserver {

DEFINE_string(tserver_master_addrs, "127.0.0.1:7051",
              "Comma separated addresses of the masters which the "
              "tablet server should connect to. The masters do not "
              "read this flag -- configure the masters separately "
              "using 'rpc_bind_addresses'.");
TAG_FLAG(tserver_master_addrs, stable);


TabletServerOptions::TabletServerOptions() {
  rpc_opts.default_port = TabletServer::kDefaultPort;

  Status s = HostPort::ParseStrings(FLAGS_tserver_master_addrs,
                                    master::Master::kDefaultPort,
                                    &master_addresses);
  if (!s.ok()) {
    LOG(FATAL) << "Couldn't parse tablet_server_master_addrs flag: " << s.ToString();
  }
}

} // namespace tserver
} // namespace kudu
