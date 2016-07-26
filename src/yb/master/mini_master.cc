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

#include "yb/master/mini_master.h"

#include <string>

#include <glog/logging.h>

#include "yb/fs/fs_manager.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/server/rpc_server.h"
#include "yb/server/webserver.h"
#include "yb/master/master.h"
#include "yb/util/net/sockaddr.h"
#include "yb/util/status.h"

using strings::Substitute;

DECLARE_bool(rpc_server_allow_ephemeral_ports);

namespace yb {
namespace master {

MiniMaster::MiniMaster(Env* env, string fs_root, uint16_t rpc_port, bool is_creating)
    : running_(false),
      is_creating_(is_creating),
      env_(env),
      fs_root_(std::move(fs_root)),
      rpc_port_(rpc_port) {}

MiniMaster::~MiniMaster() {
  if (running_) {
    LOG(WARNING) << "MiniMaster destructor called without clean shutdown for: "
                 << bound_rpc_addr_str();
  }
}

Status MiniMaster::Start() {
  CHECK(!running_);
  FLAGS_rpc_server_allow_ephemeral_ports = true;
  RETURN_NOT_OK(StartOnPorts(rpc_port_, 0));
  return master_->WaitForCatalogManagerInit();
}


Status MiniMaster::StartDistributedMaster(const vector<uint16_t>& peer_ports) {
  CHECK(!running_);
  return StartDistributedMasterOnPorts(rpc_port_, 0, peer_ports);
}

void MiniMaster::Shutdown() {
  if (running_) {
    master_->Shutdown();
  }
  running_ = false;
  master_.reset();
}

Status MiniMaster::StartOnPorts(uint16_t rpc_port, uint16_t web_port) {
  CHECK(!running_);
  CHECK(!master_);

  MasterOptions opts(std::make_shared<std::vector<HostPort>>(), is_creating_);
  return StartOnPorts(rpc_port, web_port, &opts);
}

Status MiniMaster::StartOnPorts(uint16_t rpc_port, uint16_t web_port,
                                MasterOptions* opts) {
  opts->rpc_opts.rpc_bind_addresses = Substitute("127.0.0.1:$0", rpc_port);
  opts->webserver_opts.port = web_port;
  opts->fs_opts.wal_path = fs_root_;
  opts->fs_opts.data_paths = { fs_root_ };

  gscoped_ptr<Master> server(new Master(*opts));
  RETURN_NOT_OK(server->Init());
  RETURN_NOT_OK(server->StartAsync());

  master_.swap(server);
  running_ = true;

  return Status::OK();
}

Status MiniMaster::StartDistributedMasterOnPorts(uint16_t rpc_port, uint16_t web_port,
                                                 const vector<uint16_t>& peer_ports) {
  CHECK(!running_);
  CHECK(!master_);

  auto peer_addresses = std::make_shared<std::vector<HostPort>>();

  for (uint16_t peer_port : peer_ports) {
    HostPort peer_address("127.0.0.1", peer_port);
    peer_addresses->push_back(peer_address);
  }
  MasterOptions opts(peer_addresses, is_creating_);

  return StartOnPorts(rpc_port, web_port, &opts);
}

Status MiniMaster::Restart() {
  CHECK(running_);
  is_creating_ = false;

  Sockaddr prev_rpc = bound_rpc_addr();
  Sockaddr prev_http = bound_http_addr();
  Shutdown();

  MasterOptions opts(std::make_shared<std::vector<HostPort>>(), is_creating_);
  RETURN_NOT_OK(StartOnPorts(prev_rpc.port(), prev_http.port(), &opts));
  CHECK(running_);
  return WaitForCatalogManagerInit();
}

Status MiniMaster::WaitForCatalogManagerInit() {
  return master_->WaitForCatalogManagerInit();
}

const Sockaddr MiniMaster::bound_rpc_addr() const {
  CHECK(running_);
  return master_->first_rpc_address();
}

const Sockaddr MiniMaster::bound_http_addr() const {
  CHECK(running_);
  return master_->first_http_address();
}

std::string MiniMaster::permanent_uuid() const {
  CHECK(master_);
  return DCHECK_NOTNULL(master_->fs_manager())->uuid();
}

std::string MiniMaster::bound_rpc_addr_str() const {
  return bound_rpc_addr().ToString();
}

} // namespace master
} // namespace yb
