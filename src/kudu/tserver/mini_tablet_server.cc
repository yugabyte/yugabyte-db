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

#include "kudu/tserver/mini_tablet_server.h"

#include <utility>

#include <glog/logging.h>

#include "kudu/common/schema.h"
#include "kudu/gutil/macros.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/server/metadata.h"
#include "kudu/server/rpc_server.h"
#include "kudu/server/webserver.h"
#include "kudu/tablet/maintenance_manager.h"
#include "kudu/tablet/tablet.h"
#include "kudu/tablet/tablet_peer.h"
#include "kudu/tablet/tablet-test-util.h"
#include "kudu/tserver/tablet_server.h"
#include "kudu/tserver/ts_tablet_manager.h"
#include "kudu/consensus/log.h"
#include "kudu/consensus/log.pb.h"
#include "kudu/consensus/consensus.h"
#include "kudu/consensus/consensus.pb.h"
#include "kudu/consensus/local_consensus.h"
#include "kudu/util/net/sockaddr.h"
#include "kudu/util/status.h"

using std::pair;

using kudu::consensus::Consensus;
using kudu::consensus::ConsensusOptions;
using kudu::consensus::OpId;
using kudu::consensus::RaftPeerPB;
using kudu::consensus::RaftConfigPB;
using kudu::log::Log;
using kudu::log::LogOptions;
using strings::Substitute;

DECLARE_bool(rpc_server_allow_ephemeral_ports);

namespace kudu {
namespace tserver {

MiniTabletServer::MiniTabletServer(const string& fs_root,
                                   uint16_t rpc_port)
  : started_(false) {

  // Start RPC server on loopback.
  FLAGS_rpc_server_allow_ephemeral_ports = true;
  opts_.rpc_opts.rpc_bind_addresses = Substitute("127.0.0.1:$0", rpc_port);
  opts_.webserver_opts.port = 0;
  opts_.fs_opts.wal_path = fs_root;
  opts_.fs_opts.data_paths = { fs_root };
}

MiniTabletServer::~MiniTabletServer() {
}

Status MiniTabletServer::Start() {
  CHECK(!started_);

  gscoped_ptr<TabletServer> server(new TabletServer(opts_));
  RETURN_NOT_OK(server->Init());
  RETURN_NOT_OK(server->Start());

  server_.swap(server);
  started_ = true;
  return Status::OK();
}

Status MiniTabletServer::WaitStarted() {
  return server_->WaitInited();
}

void MiniTabletServer::Shutdown() {
  if (started_) {
    server_->Shutdown();
    server_.reset();
  }
  started_ = false;
}

Status MiniTabletServer::Restart() {
  CHECK(started_);
  opts_.rpc_opts.rpc_bind_addresses = Substitute("127.0.0.1:$0", bound_rpc_addr().port());
  opts_.webserver_opts.port = bound_http_addr().port();
  Shutdown();
  RETURN_NOT_OK(Start());
  return Status::OK();
}

RaftConfigPB MiniTabletServer::CreateLocalConfig() const {
  CHECK(started_) << "Must Start()";
  RaftConfigPB config;
  config.set_local(true);
  RaftPeerPB* peer = config.add_peers();
  peer->set_permanent_uuid(server_->instance_pb().permanent_uuid());
  peer->set_member_type(RaftPeerPB::VOTER);
  peer->mutable_last_known_addr()->set_host(bound_rpc_addr().host());
  peer->mutable_last_known_addr()->set_port(bound_rpc_addr().port());
  return config;
}

Status MiniTabletServer::AddTestTablet(const std::string& table_id,
                                       const std::string& tablet_id,
                                       const Schema& schema) {
  return AddTestTablet(table_id, tablet_id, schema, CreateLocalConfig());
}

Status MiniTabletServer::AddTestTablet(const std::string& table_id,
                                       const std::string& tablet_id,
                                       const Schema& schema,
                                       const RaftConfigPB& config) {
  CHECK(started_) << "Must Start()";
  Schema schema_with_ids = SchemaBuilder(schema).Build();
  pair<PartitionSchema, Partition> partition = tablet::CreateDefaultPartition(schema_with_ids);

  return server_->tablet_manager()->CreateNewTablet(
    table_id, tablet_id, partition.second, table_id,
    schema_with_ids, partition.first, config, nullptr);
}

void MiniTabletServer::FailHeartbeats() {
  server_->set_fail_heartbeats_for_tests(true);
}

const Sockaddr MiniTabletServer::bound_rpc_addr() const {
  CHECK(started_);
  return server_->first_rpc_address();
}

const Sockaddr MiniTabletServer::bound_http_addr() const {
  CHECK(started_);
  return server_->first_http_address();
}

} // namespace tserver
} // namespace kudu
