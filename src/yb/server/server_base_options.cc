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

#include "yb/server/server_base_options.h"

#include <gflags/gflags.h>

#include "yb/rpc/yb_rpc.h"
#include "yb/util/flag_tags.h"

// The following flags related to the cloud, region and availability zone that an instance is
// started in. These are passed in from whatever provisioning mechanics start the servers. They
// are used for generic placement policies on table creation and tablet load balancing, to
// either constrain data to a certain location (table A should only live in aws.us-west2.a), or to
// define the required level of fault tolerance expected (table B should have N replicas, across
// two regions of AWS and one of GCE).
//
// These are currently for use in a cloud-based deployment, but could be retrofitted to work for
// an on-premise deployment as well, with datacenter, cluster and rack levels, for example.
DEFINE_string(placement_cloud, "cloud1",
              "The cloud in which this instance is started.");
DEFINE_string(placement_region, "datacenter1",
              "The cloud region in which this instance is started.");
DEFINE_string(placement_zone, "rack1",
              "The cloud availability zone in which this instance is started.");

namespace yb {
namespace server {

using std::vector;

DEFINE_string(server_dump_info_path, "",
              "Path into which the server information will be "
              "dumped after startup. The dumped data is described by "
              "ServerStatusPB in server_base.proto. The dump format is "
              "determined by --server_dump_info_format");
DEFINE_string(server_dump_info_format, "json",
              "Format for --server_dump_info_path. This may be either "
              "'pb' or 'json'.");
TAG_FLAG(server_dump_info_path, hidden);
TAG_FLAG(server_dump_info_format, hidden);

DEFINE_int32(metrics_log_interval_ms, 0,
             "Interval (in milliseconds) at which the server will dump its "
             "metrics to a local log file. The log files are located in the same "
             "directory as specified by the -log_dir flag. If this is not a positive "
             "value, then metrics logging will be disabled.");
TAG_FLAG(metrics_log_interval_ms, advanced);

ServerBaseOptions::ServerBaseOptions()
    : env(Env::Default()),
      dump_info_path(FLAGS_server_dump_info_path),
      dump_info_format(FLAGS_server_dump_info_format),
      metrics_log_interval_ms(FLAGS_metrics_log_interval_ms),
      placement_cloud(FLAGS_placement_cloud),
      placement_region(FLAGS_placement_region),
      placement_zone(FLAGS_placement_zone),
      connection_context_factory(&std::make_unique<rpc::YBConnectionContext>) {}

ServerBaseOptions::ServerBaseOptions(const ServerBaseOptions& options)
    : env(options.env),
      server_type(options.server_type),
      fs_opts(options.fs_opts),
      rpc_opts(options.rpc_opts),
      webserver_opts(options.webserver_opts),
      dump_info_path(options.dump_info_path),
      dump_info_format(options.dump_info_format),
      metrics_log_interval_ms(options.metrics_log_interval_ms),
      placement_cloud(options.placement_cloud),
      placement_region(options.placement_region),
      placement_zone(options.placement_zone),
      master_addresses_flag(options.master_addresses_flag),
      connection_context_factory(options.connection_context_factory) {
  SetMasterAddressesNoValidation(options.GetMasterAddresses());
}

// This implementation is better but it needs support of
//    atomic_load( shared_ptr<> ), atomic_store( shared_ptr<> )
//
// #include <atomic>
//
// void ServerBaseOptions::SetMasterAddresses(
//     addresses_shared_ptr master_addresses) {
//   std::atomic_store(&master_addresses_, master_addresses);
// }
//
// addresses_shared_ptr ServerBaseOptions::GetMasterAddresses() const {
//   addresses_shared_ptr local = std::atomic_load(&master_addresses_);
//   return local;
// }

void ServerBaseOptions::SetMasterAddressesNoValidation(
    ServerBaseOptions::addresses_shared_ptr master_addresses) {
  std::lock_guard<std::mutex> l(master_addresses_mtx_);
  master_addresses_ = master_addresses;
}

ServerBaseOptions::addresses_shared_ptr ServerBaseOptions::GetMasterAddresses() const {
  std::lock_guard<std::mutex> l(master_addresses_mtx_);
  return master_addresses_;
}

} // namespace server
} // namespace yb
