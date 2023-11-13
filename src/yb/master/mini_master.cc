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

#include "yb/master/mini_master.h"

#include <string>

#include "yb/util/logging.h"

#include "yb/master/catalog_manager.h"
#include "yb/master/master.h"

#include "yb/rpc/messenger.h"

#include "yb/util/net/net_util.h"
#include "yb/util/net/sockaddr.h"
#include "yb/util/net/tunnel.h"
#include "yb/util/status.h"
#include "yb/util/thread.h"

using std::string;
using std::vector;

using strings::Substitute;

DECLARE_bool(TEST_simulate_fs_create_failure);
DECLARE_bool(rpc_server_allow_ephemeral_ports);
DECLARE_double(leader_failure_max_missed_heartbeat_periods);
DECLARE_int32(TEST_nodes_per_cloud);
DECLARE_bool(durable_wal_write);

namespace yb {
namespace master {

MiniMaster::MiniMaster(Env* env, string fs_root, uint16_t rpc_port, uint16_t web_port, int index)
    : running_(false),
      env_(env),
      fs_root_(std::move(fs_root)),
      rpc_port_(rpc_port),
      web_port_(web_port),
      index_(index + 1) {}

MiniMaster::~MiniMaster() {
  if (running_) {
    LOG(WARNING) << "MiniMaster destructor called without clean shutdown for: "
                 << bound_rpc_addr_str();
  }
}

Status MiniMaster::Start(bool TEST_simulate_fs_create_failure) {
  CHECK(!running_);

  FLAGS_rpc_server_allow_ephemeral_ports = true;
  FLAGS_TEST_simulate_fs_create_failure = TEST_simulate_fs_create_failure;
  // Disable WAL fsync for tests
  FLAGS_durable_wal_write = false;
  RETURN_NOT_OK(StartOnPorts(rpc_port_, web_port_));
  return master_->WaitForCatalogManagerInit();
}


Status MiniMaster::StartDistributedMaster(const vector<uint16_t>& peer_ports) {
  CHECK(!running_);
  return StartDistributedMasterOnPorts(rpc_port_, web_port_, peer_ports);
}

void MiniMaster::Shutdown() {
  TEST_SetThreadPrefixScoped prefix_se(Format("m-$0", index_));
  if (tunnel_) {
    tunnel_->Shutdown();
  }
  if (running_) {
    master_->Shutdown();
  }
  tunnel_.reset();
  running_ = false;
  master_.reset();
}

Status MiniMaster::StartOnPorts(uint16_t rpc_port, uint16_t web_port) {
  CHECK(!running_);
  CHECK(!master_);

  auto master_addresses = std::make_shared<server::MasterAddresses>();
  if (use_custom_addresses_) {
    HostPort local_host_port;
    for (const auto & master_addr : custom_master_addresses_) {
      RETURN_NOT_OK(local_host_port.ParseString(master_addr, rpc_port));
      master_addresses->push_back({local_host_port});
    }
  } else if (pass_master_addresses_) {
    HostPort local_host_port;
    RETURN_NOT_OK(local_host_port.ParseString(
        server::TEST_RpcBindEndpoint(index_, rpc_port), rpc_port));
    master_addresses->push_back({local_host_port});
  }
  MasterOptions opts(master_addresses);

  Status start_status = StartOnPorts(rpc_port, web_port, &opts);
  if (!start_status.ok()) {
    LOG(ERROR) << "MiniMaster failed to start on RPC port " << rpc_port
               << ", web port " << web_port << ": " << start_status;
    // Don't crash here. Handle the error in the caller (e.g. could retry there).
  }
  return start_status;
}

Status MiniMaster::StartOnPorts(uint16_t rpc_port, uint16_t web_port,
                                MasterOptions* opts) {
  TEST_SetThreadPrefixScoped prefix_se(Format("m-$0", index_));
  if (use_custom_addresses_) {
    opts->rpc_opts.rpc_bind_addresses = Format(
        "$0:$1", custom_rpc_addresses_[0], rpc_port);
    for (size_t i = 1; i < custom_rpc_addresses_.size(); i++) {
      opts->rpc_opts.rpc_bind_addresses += Format(
          ",$0:$1", custom_rpc_addresses_[i], rpc_port);
    }

    opts->broadcast_addresses = {};
    HostPort host_port;
    for (const auto & broadcast_addr : custom_broadcast_addresses_) {
      RETURN_NOT_OK(host_port.ParseString(broadcast_addr, rpc_port));
      opts->broadcast_addresses.push_back(host_port);
    }
  } else {
    opts->rpc_opts.rpc_bind_addresses = server::TEST_RpcBindEndpoint(index_, rpc_port);
    opts->broadcast_addresses = {
        HostPort(server::TEST_RpcAddress(index_, server::Private::kFalse), rpc_port) };
  }

  opts->webserver_opts.port = web_port;
  opts->fs_opts.wal_paths = { fs_root_ };
  opts->fs_opts.data_paths = { fs_root_ };
  // A.B.C.D.xip.io resolves to A.B.C.D so it is very useful for testing.

  if (!opts->has_placement_cloud()) {
    opts->SetPlacement(
        Format("cloud$0", (index_ + 1) / FLAGS_TEST_nodes_per_cloud),
        Format("rack$0", index_), "zone");
  }

  std::unique_ptr<Master> server(new Master(*opts));
  RETURN_NOT_OK(server->Init());

  server::TEST_SetupConnectivity(server->messenger(), index_);

  RETURN_NOT_OK(server->StartAsync());

  master_.swap(server);

  tunnel_ = std::make_unique<Tunnel>(&master_->messenger()->io_service());
  std::vector<Endpoint> local;
  RETURN_NOT_OK(opts->broadcast_addresses[0].ResolveAddresses(&local));
  Endpoint remote = VERIFY_RESULT(ParseEndpoint(opts->rpc_opts.rpc_bind_addresses, 0));
  RETURN_NOT_OK(tunnel_->Start(local.front(), remote));

  running_ = true;

  return Status::OK();
}

Status MiniMaster::StartDistributedMasterOnPorts(uint16_t rpc_port, uint16_t web_port,
                                                 const vector<uint16_t>& peer_ports) {
  CHECK(!running_);
  CHECK(!master_);

  auto peer_addresses = std::make_shared<server::MasterAddresses>();
  if (pass_master_addresses_) {
    peer_addresses->resize(peer_ports.size());

    int index = 0;
    for (uint16_t peer_port : peer_ports) {
      auto& addresses = (*peer_addresses)[index];
      ++index;
      addresses.push_back(VERIFY_RESULT(HostPort::FromString(
          server::TEST_RpcBindEndpoint(index, peer_port), peer_port)));
      addresses.push_back(VERIFY_RESULT(HostPort::FromString(
          server::TEST_RpcAddress(index, server::Private::kFalse), peer_port)));
    }
  }
  MasterOptions opts(peer_addresses);

  return StartOnPorts(rpc_port, web_port, &opts);
}

Status MiniMaster::Restart() {
  CHECK(running_);

  auto prev_rpc = bound_rpc_addr();
  Endpoint prev_http = bound_http_addr();
  auto master_addresses = master_->opts().GetMasterAddresses();
  Shutdown();

  MasterOptions opts(master_addresses);
  RETURN_NOT_OK(StartOnPorts(prev_rpc.port(), prev_http.port(), &opts));
  CHECK(running_);
  return WaitForCatalogManagerInit();
}

Status MiniMaster::WaitForCatalogManagerInit() {
  RETURN_NOT_OK(master_->catalog_manager()->WaitForWorkerPoolTests());
  return master_->WaitForCatalogManagerInit();
}

Status MiniMaster::WaitUntilCatalogManagerIsLeaderAndReadyForTests() {
  return master_->WaitUntilCatalogManagerIsLeaderAndReadyForTests();
}

void MiniMaster::SetCustomAddresses(const std::vector<std::string> &master_addresses,
                                    const std::vector<std::string> &rpc_bind_addresses,
                                    const std::vector<std::string> &broadcast_addresses) {
  CHECK_GT(master_addresses.size(),  0);
  CHECK_GT(rpc_bind_addresses.size(), 0);
  CHECK_GT(broadcast_addresses.size(), 0);

  custom_master_addresses_ = master_addresses;
  custom_rpc_addresses_ = rpc_bind_addresses;
  custom_broadcast_addresses_ = broadcast_addresses;
  use_custom_addresses_ = true;
}

HostPort MiniMaster::bound_rpc_addr() const {
  CHECK(running_);
  return HostPort::FromBoundEndpoint(master_->first_rpc_address());
}

Endpoint MiniMaster::bound_http_addr() const {
  CHECK(running_);
  return CHECK_RESULT(master_->first_http_address());
}

std::string MiniMaster::permanent_uuid() const {
  CHECK(master_);
  return DCHECK_NOTNULL(master_->fs_manager())->uuid();
}

std::string MiniMaster::bound_rpc_addr_str() const {
  return bound_rpc_addr().ToString();
}

CatalogManagerIf& MiniMaster::catalog_manager() const {
  return *master_->catalog_manager();
}

CatalogManager& MiniMaster::catalog_manager_impl() const {
  return *master_->catalog_manager_impl();
}

tablet::TabletPeerPtr MiniMaster::tablet_peer() const {
  return catalog_manager().tablet_peer();
}

rpc::Messenger& MiniMaster::messenger() const {
  return *master_->messenger();
}

master::SysCatalogTable& MiniMaster::sys_catalog() const {
  return *catalog_manager().sys_catalog();
}

master::TSManager& MiniMaster::ts_manager() const {
  return *master_->ts_manager();
}

master::FlushManager& MiniMaster::flush_manager() const {
  return *master_->flush_manager();
}

FsManager& MiniMaster::fs_manager() const {
  return *master_->fs_manager();
}

} // namespace master
} // namespace yb
