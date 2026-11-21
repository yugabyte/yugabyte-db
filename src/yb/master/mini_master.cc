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
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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

#include "yb/gutil/dynamic_annotations.h"

#include "yb/master/master_cluster.proxy.h"
#include "yb/master/master_options.h"
#include "yb/master/sys_catalog.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/proxy.h"
#include "yb/rpc/rpc_controller.h"

#include "yb/server/server_base.pb.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/net/net_util.h"
#include "yb/util/net/sockaddr.h"
#include "yb/util/net/tunnel.h"
#include "yb/util/status.h"
#include "yb/util/thread.h"

using std::string;
using std::vector;


DECLARE_bool(TEST_simulate_fs_create_failure);
DECLARE_bool(rpc_server_allow_ephemeral_ports);
DECLARE_double(leader_failure_max_missed_heartbeat_periods);
DECLARE_int32(TEST_nodes_per_cloud);
DECLARE_bool(durable_wal_write);

namespace yb {
namespace master {

namespace {

// How long an embedded Rust master may take to open its sys catalog or become a ready leader.
const auto kRustMasterWaitTimeout = MonoDelta::FromSeconds(60);

}  // namespace

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

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_rpc_server_allow_ephemeral_ports) = true;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_simulate_fs_create_failure) =
      TEST_simulate_fs_create_failure;
  // Disable WAL fsync for tests
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_durable_wal_write) = false;
  RETURN_NOT_OK(StartOnPorts(rpc_port_, web_port_));
  return WaitForCatalogManagerInit();
}


Status MiniMaster::StartDistributedMaster(const vector<uint16_t>& peer_ports) {
  CHECK(!running_);
  return StartDistributedMasterOnPorts(rpc_port_, web_port_, peer_ports);
}

std::string MiniMaster::ToString() const {
  return Format("m-$0", index_);
}

void MiniMaster::Shutdown() {
  TEST_SetThreadPrefixScoped prefix_se(Format("m-$0", index_));
  if (tunnel_) {
    tunnel_->Shutdown();
  }
  if (running_) {
    embedded_->Shutdown();
  }
  tunnel_.reset();
  running_ = false;
  embedded_.reset();
}

Status MiniMaster::StartOnPorts(uint16_t rpc_port, uint16_t web_port) {
  CHECK(!running_);
  CHECK(!embedded_);

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

  // Don't crash here. Handle the error in the caller (e.g. could retry there).
  RETURN_NOT_OK_WITH_WARNING(
      StartOnPorts(rpc_port, web_port, &opts),
      Format("MiniMaster failed to start on RPC port $0, web port $1", rpc_port, web_port));
  return Status::OK();
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
        Format("region$0", index_), "zone");
  }

  embedded_ = VERIFY_RESULT(StartEmbeddedMaster(*opts));
  server::TEST_SetupConnectivity(embedded_->server().messenger(), index_);

  tunnel_ = std::make_unique<Tunnel>(&messenger().io_service());
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
  CHECK(!embedded_);

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

Status MiniMaster::Restart(bool wait_until_catalog_manager_is_leader) {
  CHECK(running_);

  auto prev_rpc = bound_rpc_addr();
  Endpoint prev_http = bound_http_addr();
  auto master_addresses = opts().GetMasterAddresses();
  Shutdown();

  MasterOptions opts(master_addresses);
  RETURN_NOT_OK(StartOnPorts(prev_rpc.port(), prev_http.port(), &opts));
  CHECK(running_);
  RETURN_NOT_OK(WaitForCatalogManagerInit());
  if (!wait_until_catalog_manager_is_leader) {
    return Status::OK();
  }
  return WaitUntilCatalogManagerIsLeaderAndReadyForTests();
}

Status MiniMaster::WaitForCatalogManagerInit() {
  return LoggedWaitFor(
      [this] { return embedded_->IsSysCatalogInitialized(); }, kRustMasterWaitTimeout,
      "the Rust master to open its sys catalog");
}

Status MiniMaster::WaitUntilCatalogManagerIsLeaderAndReadyForTests() {
  return LoggedWaitFor(
      [this]() -> Result<bool> { return IsLeaderAndReady(); }, kRustMasterWaitTimeout,
      "the Rust master to lead and be ready");
}

Result<bool> MiniMaster::IsLeaderAndReady() const {
  // The Rust master's leadership is only visible through its RPCs.
  rpc::ProxyCache proxy_cache(&messenger());
  MasterClusterProxy proxy(&proxy_cache, bound_rpc_addr());
  IsMasterLeaderReadyRequestPB req;
  IsMasterLeaderReadyResponsePB resp;
  rpc::RpcController controller;
  controller.set_timeout(MonoDelta::FromSeconds(10));
  RETURN_NOT_OK(proxy.IsMasterLeaderServiceReady(req, &resp, &controller));
  return !resp.has_error();
}

const MasterOptions& MiniMaster::opts() const {
  return embedded_->opts();
}

server::RpcAndWebServerBase& MiniMaster::server() const {
  return CHECK_NOTNULL(embedded_.get())->server();
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
  return HostPort::FromBoundEndpoint(server().first_rpc_address());
}

Endpoint MiniMaster::bound_http_addr() const {
  CHECK(running_);
  // The Rust master serves the web port; the host's own web server is on loopback.
  // MasterHost::GetStatusPB lists the Rust server's addresses, IPv4 first.
  server::ServerStatusPB status;
  server().GetStatusPB(&status);
  CHECK_GT(status.bound_http_addresses_size(), 0);
  const auto& hp = status.bound_http_addresses(0);
  return Endpoint(boost::asio::ip::make_address(hp.host()), hp.port());
}

std::string MiniMaster::permanent_uuid() const {
  CHECK(embedded_);
  return DCHECK_NOTNULL(server().fs_manager())->uuid();
}

std::string MiniMaster::bound_rpc_addr_str() const {
  return bound_rpc_addr().ToString();
}

tablet::TabletPeerPtr MiniMaster::tablet_peer() const {
  return sys_catalog().tablet_peer();
}

rpc::Messenger& MiniMaster::messenger() const {
  return *server().messenger();
}

master::SysCatalogTable& MiniMaster::sys_catalog() const {
  return embedded_->sys_catalog();
}

FsManager& MiniMaster::fs_manager() const {
  return *server().fs_manager();
}

HybridTime MiniMaster::Now() const {
  return server().clock()->Now();
}

} // namespace master
} // namespace yb
