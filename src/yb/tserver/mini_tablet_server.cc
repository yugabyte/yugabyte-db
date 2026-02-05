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

#include "yb/tserver/mini_tablet_server.h"

#include <functional>
#include <memory>
#include <string>
#include <utility>

#include "yb/common/schema.h"
#include "yb/consensus/consensus_util.h"

#include "yb/dockv/partition.h"

#include "yb/rpc/messenger.h"

#include "yb/server/rpc_server.h"

#include "yb/tablet/tablet-test-harness.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"

#include "yb/util/net/sockaddr.h"
#include "yb/util/net/tunnel.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status.h"
#include "yb/util/thread.h"

#include "yb/yql/pgwrapper/libpq_utils.h"

using std::pair;
using std::string;

using yb::consensus::RaftConfigPB;
using yb::consensus::RaftPeerPB;
using yb::tablet::TabletPeer;

DECLARE_bool(rpc_server_allow_ephemeral_ports);
DECLARE_double(leader_failure_max_missed_heartbeat_periods);
DECLARE_int32(TEST_nodes_per_cloud);

DEFINE_test_flag(bool, private_broadcast_address, false,
                 "Use private address for broadcast address in tests.");

namespace yb::tserver {

MiniTabletServer::MiniTabletServer(const std::vector<std::string>& wal_paths,
                                   const std::vector<std::string>& data_paths,
                                   uint16_t rpc_port,
                                   const TabletServerOptions& extra_opts, int index)
  : started_(false),
    opts_(extra_opts),
    index_(index + 1) {

  // Start RPC server on loopback.
  FLAGS_rpc_server_allow_ephemeral_ports = true;
  const std::string rpc_host = server::TEST_RpcAddress(index_, server::Private::kTrue);
  opts_.rpc_opts.rpc_bind_addresses = HostPortToString(rpc_host, rpc_port);
  // A.B.C.D.xip.io resolves to A.B.C.D so it is very useful for testing.
  opts_.broadcast_addresses = {
    HostPort(server::TEST_RpcAddress(index_,
                                     server::Private(FLAGS_TEST_private_broadcast_address)),
    rpc_port) };
  opts_.webserver_opts.port = 0;
  opts_.webserver_opts.bind_interface = rpc_host;
  if (!opts_.has_placement_cloud()) {
    opts_.SetPlacement(Format("cloud$0", (index_ + 1) / FLAGS_TEST_nodes_per_cloud),
                       Format("region$0", index_), "zone");
  }
  opts_.fs_opts.wal_paths = wal_paths;
  opts_.fs_opts.data_paths = data_paths;
}

MiniTabletServer::MiniTabletServer(const string& fs_root,
                                   uint16_t rpc_port,
                                   const TabletServerOptions& extra_opts,
                                   int index)
  : MiniTabletServer({ fs_root }, { fs_root }, rpc_port, extra_opts, index) {
}

MiniTabletServer::~MiniTabletServer() = default;

Result<std::unique_ptr<MiniTabletServer>> MiniTabletServer::CreateMiniTabletServer(
    const string& fs_root, uint16_t rpc_port, int index) {
  auto options_result = TabletServerOptions::CreateTabletServerOptions();
  RETURN_NOT_OK(options_result);
  return std::make_unique<MiniTabletServer>(fs_root, rpc_port, *options_result, index);
}

Result<std::unique_ptr<MiniTabletServer>> MiniTabletServer::CreateMiniTabletServer(
    const std::vector<string>& fs_roots, uint16_t rpc_port, int index) {
  auto options_result = TabletServerOptions::CreateTabletServerOptions();
  RETURN_NOT_OK(options_result);
  return std::make_unique<MiniTabletServer>(fs_roots, fs_roots, rpc_port, *options_result, index);
}

Status MiniTabletServer::Start(
    WaitTabletsBootstrapped wait_tablets_bootstrapped, WaitToAcceptPgConnections wait_for_pg) {
  CHECK(!started_);
  TEST_SetThreadPrefixScoped prefix_se(ToString());

  std::unique_ptr<TabletServer> server(new TabletServer(opts_));
  RETURN_NOT_OK(server->Init());

  RETURN_NOT_OK(server->Start());

  server_.swap(server);

  RETURN_NOT_OK(Reconnect());

  started_ = true;
  if (wait_tablets_bootstrapped) {
    RETURN_NOT_OK(WaitStarted());
  }
  return StartPgIfConfigured(wait_for_pg);
}

Status MiniTabletServer::StartPgIfConfigured(WaitToAcceptPgConnections wait_for_pg) {
  if (!start_pg_) {
    return Status::OK();
  }
  RETURN_NOT_OK(start_pg_());
  RETURN_NOT_OK(server_->StartYSQLLeaseRefresher());
  if (!wait_for_pg) {
    return Status::OK();
  }
  CHECK(get_pg_conn_settings_) << "Must set get_pg_conn_settings_ function when start_pg_ set";
  auto settings = get_pg_conn_settings_();
  settings.dbname = "yugabyte";
  settings.connect_timeout = 20;
  VERIFY_RESULT(pgwrapper::PGConnBuilder(settings).Connect());
  return Status::OK();
}

string MiniTabletServer::ToString() const { return Format("ts-$0", index_); }

void MiniTabletServer::Isolate() {
  server::TEST_Isolate(server_->messenger());
  tunnel_->Shutdown();
}

Status MiniTabletServer::Reconnect() {
  server::TEST_SetupConnectivity(server_->messenger(), index_);

  if (FLAGS_TEST_private_broadcast_address) {
    return Status::OK();
  }

  tunnel_ = std::make_unique<Tunnel>(&server_->messenger()->io_service());
  CancelableScopeExit shutdown_se{[this] {tunnel_->Shutdown(); }};

  std::vector<Endpoint> local;
  RETURN_NOT_OK(opts_.broadcast_addresses[0].ResolveAddresses(&local));
  Endpoint remote = VERIFY_RESULT(ParseEndpoint(opts_.rpc_opts.rpc_bind_addresses, 0));
  RETURN_NOT_OK(tunnel_->Start(
      local.front(), remote, [messenger = server_->messenger()](const IpAddress& address) {
    return !messenger->TEST_ShouldArtificiallyRejectIncomingCallsFrom(address);
  }));
  shutdown_se.Cancel();
  return Status::OK();
}

Status MiniTabletServer::WaitStarted() {
  return server_->WaitInited();
}

void MiniTabletServer::Shutdown() {
  TEST_SetThreadPrefixScoped prefix_se(Format("ts-$0", index_));
  if (tunnel_) {
    tunnel_->Shutdown();
  }
  if (started_) {
    if (shutdown_pg_) {
      shutdown_pg_();
    }
    // Save bind address and port so we can later restart the server.
    opts_.rpc_opts.rpc_bind_addresses = server::TEST_RpcBindEndpoint(
        index_, bound_rpc_addr().port());
    opts_.webserver_opts.port = bound_http_addr().port();
    server_->Shutdown();
    tunnel_.reset();
    server_.reset();
  }
  started_ = false;
}

namespace {

Status ForAllTablets(
    MiniTabletServer* mts,
    std::function<Status(TabletPeer* tablet_peer)> action) {
  if (!mts->server()) {
    return STATUS(IllegalState, "Server is not running");
  }
  auto tablets = mts->server()->tablet_manager()->GetTabletPeers();
  for (const auto& tablet : tablets) {
    RETURN_NOT_OK(action(tablet.get()));
  }
  return Status::OK();
}

}  // namespace

Status MiniTabletServer::FlushTablets(tablet::FlushMode mode, tablet::FlushFlags flags) {
  if (!server_) {
    return Status::OK();
  }
  return ForAllTablets(this, [mode, flags](TabletPeer* tablet_peer) -> Status {
    auto tablet = tablet_peer->shared_tablet();
    if (!tablet) {
      return Status::OK();
    }
    return tablet->Flush(mode, flags);
  });
}

Status MiniTabletServer::CompactTablets(docdb::SkipFlush skip_flush) {
  if (!server_) {
    return Status::OK();
  }
  return ForAllTablets(this, [skip_flush](TabletPeer* tablet_peer) -> Status {
    auto tablet = tablet_peer->shared_tablet();
    if (!tablet) {
      return Status::OK();
    }
    return tablet->ForceManualRocksDBCompact(skip_flush);
  });
}

Status MiniTabletServer::CompactTablet(const TabletId& tablet_id, docdb::SkipFlush skip_flush) {
  if (!server_) {
    return Status::OK();
  }
  auto tablet_peer = server_->tablet_manager()->LookupTablet(tablet_id);
  if (!tablet_peer) {
    return Status::OK();
  }
  auto tablet = tablet_peer->shared_tablet();
  return tablet->ForceManualRocksDBCompact(skip_flush);
}

Status MiniTabletServer::SwitchMemtables() {
  return ForAllTablets(this, [](TabletPeer* tablet_peer) -> Status {
    auto tablet = tablet_peer->shared_tablet();
    if (!tablet) {
      return Status::OK();
    }
    return tablet->TEST_SwitchMemtable();
  });
}

Status MiniTabletServer::CleanTabletLogs() {
  if (!server_) {
    // Nothing to clean.
    return Status::OK();
  }
  return ForAllTablets(this, [](TabletPeer* tablet_peer) { return tablet_peer->RunLogGC(); });
}

Status MiniTabletServer::Restart(WaitToAcceptPgConnections wait_for_pg) {
  CHECK(started_);
  Shutdown();
  return Start(WaitTabletsBootstrapped::kFalse, wait_for_pg);
}

Status MiniTabletServer::RestartStoppedServer() {
  Shutdown();
  return Start(WaitTabletsBootstrapped::kFalse);
}

RaftConfigPB MiniTabletServer::CreateLocalConfig() const {
  CHECK(started_) << "Must Start()";
  RaftConfigPB config;
  RaftPeerPB* peer = config.add_peers();
  peer->set_permanent_uuid(server_->instance_pb().permanent_uuid());
  peer->set_member_type(consensus::PeerMemberType::VOTER);
  auto host_port = peer->mutable_last_known_private_addr()->Add();
  host_port->set_host(bound_rpc_addr().address().to_string());
  host_port->set_port(bound_rpc_addr().port());
  return config;
}

Status MiniTabletServer::AddTestTablet(const std::string& ns_id,
                                       const std::string& table_id,
                                       const std::string& tablet_id,
                                       const Schema& schema,
                                       TableType table_type) {
  return AddTestTablet(ns_id, table_id, tablet_id, schema, CreateLocalConfig(), table_type);
}

Status MiniTabletServer::AddTestTablet(const std::string& ns_id,
                                       const std::string& table_id,
                                       const std::string& tablet_id,
                                       const Schema& schema,
                                       const RaftConfigPB& config,
                                       TableType table_type) {
  CHECK(started_) << "Must Start()";
  Schema schema_with_ids = SchemaBuilder(schema).Build();
  auto partition = tablet::CreateDefaultPartition(schema_with_ids);

  auto table_info = tablet::TableInfo::TEST_CreateWithLogPrefix(
      consensus::MakeTabletLogPrefix(tablet_id, server_->permanent_uuid()),
      table_id, ns_id, table_id, table_type, schema_with_ids, partition.first);

  return ResultToStatus(server_->tablet_manager()->CreateNewTablet(
      table_info, tablet_id, partition.second, config));
}

void MiniTabletServer::FailHeartbeats(bool fail_heartbeats_for_tests) {
  server_->set_fail_heartbeats_for_tests(fail_heartbeats_for_tests);
}

Endpoint MiniTabletServer::bound_rpc_addr() const {
  CHECK(started_);
  return server_->first_rpc_address();
}

Endpoint MiniTabletServer::bound_http_addr() const {
  CHECK(started_);
  // Try to get address from the running WebServer.
  Result<Endpoint> res_ep = server_->first_http_address();
  if (res_ep) {
    return *res_ep;
  }

  WARN_NOT_OK(res_ep.status(), "RpcAndWebServerBase error");
  // The WebServer may be not started. Return input bound address.
  HostPort web_input_hp;
  CHECK_OK(server_->web_server()->GetInputHostPort(&web_input_hp));
  return CHECK_RESULT(ParseEndpoint(web_input_hp.ToString(), web_input_hp.port()));
}

std::string MiniTabletServer::bound_http_addr_str() const {
  return HostPort::FromBoundEndpoint(bound_http_addr()).ToString();
}

std::string MiniTabletServer::bound_rpc_addr_str() const {
  return HostPort::FromBoundEndpoint(bound_rpc_addr()).ToString();
}

FsManager& MiniTabletServer::fs_manager() const {
  CHECK(started_);
  return *server_->fs_manager();
}

MetricEntity& MiniTabletServer::metric_entity() const {
  CHECK(started_);
  return *server_->metric_entity();
}

const MemTrackerPtr& MiniTabletServer::mem_tracker() const {
  CHECK(started_);
  return server_->mem_tracker();
}

HybridTime MiniTabletServer::Now() const {
  CHECK(started_);
  return server_->clock()->Now();
}

void MiniTabletServer::SetPgServerHandlers(
    std::function<Status(void)> start_pg, std::function<void(void)> shutdown_pg,
    std::function<pgwrapper::PGConnSettings(void)> get_pg_conn_settings) {
  start_pg_ = std::move(start_pg);
  shutdown_pg_ = std::move(shutdown_pg);
  get_pg_conn_settings_ = std::move(get_pg_conn_settings);
}

} // namespace yb::tserver
