// Copyright (c) YugaByte, Inc.
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

#include "yb/client/async_initializer.h"

#include "yb/common/wire_protocol.h"

using namespace std::literals;

namespace yb {
namespace client {

AsyncClientInitialiser::AsyncClientInitialiser(
    const std::string& client_name, const uint32_t num_reactors, const uint32_t timeout_seconds,
    const std::string& tserver_uuid, const yb::server::ServerBaseOptions* opts,
    scoped_refptr<MetricEntity> metric_entity)
    : client_future_(client_promise_.get_future()) {
  client_builder_.set_client_name(client_name);
  client_builder_.default_rpc_timeout(MonoDelta::FromSeconds(timeout_seconds));
  client_builder_.add_master_server_addr(opts->master_addresses_flag);
  client_builder_.set_skip_master_leader_resolution(opts->GetMasterAddresses()->size() == 1);
  client_builder_.set_metric_entity(metric_entity);
  if (num_reactors > 0) {
    client_builder_.set_num_reactors(num_reactors);
  }

  // Build cloud_info_pb.
  CloudInfoPB cloud_info_pb;
  cloud_info_pb.set_placement_cloud(opts->placement_cloud);
  cloud_info_pb.set_placement_region(opts->placement_region);
  cloud_info_pb.set_placement_zone(opts->placement_zone);
  client_builder_.set_cloud_info_pb(cloud_info_pb);

  if (!tserver_uuid.empty()) {
    client_builder_.set_tserver_uuid(tserver_uuid);
  }

  init_client_thread_ = std::thread(std::bind(&AsyncClientInitialiser::InitClient, this));
}

AsyncClientInitialiser::~AsyncClientInitialiser() {
  Shutdown();
  init_client_thread_.join();
}

const std::shared_ptr<client::YBClient>& AsyncClientInitialiser::client() const {
  return client_future_.get();
}

void AsyncClientInitialiser::InitClient() {
  LOG(INFO) << "Starting to init ybclient";
  while (!stopping_) {
    client::YBClientPtr client;

    auto status = client_builder_.Build(&client);
    if (status.ok()) {
      LOG(INFO) << "Successfully built ybclient";
      client_promise_.set_value(client);
      break;
    }

    LOG(ERROR) << "Failed to initialize client: " << status;
    std::this_thread::sleep_for(1s);
  }
}

}  // namespace client
}  // namespace yb
