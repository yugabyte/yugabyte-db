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
// Tool to query tablet server operational data

#include <memory>

#include "yb/dockv/partition.h"
#include "yb/qlexpr/ql_rowblock.h"
#include "yb/common/schema_pbutil.h"
#include "yb/common/schema.h"
#include "yb/common/transaction.h"
#include "yb/common/wire_protocol.h"

#include "yb/consensus/consensus.proxy.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/proxy.h"
#include "yb/rpc/rpc_controller.h"
#include "yb/rpc/secure_stream.h"

#include "yb/consensus/metadata.pb.h"
#include "yb/rpc/secure.h"
#include "yb/server/server_base.proxy.h"

#include "yb/tablet/tablet.pb.h"

#include "yb/tserver/tablet_server.h"
#include "yb/tserver/tserver_admin.proxy.h"
#include "yb/tserver/tserver_service.proxy.h"

#include "yb/util/faststring.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/net/net_util.h"
#include "yb/util/protobuf_util.h"
#include "yb/util/result.h"

using std::ostringstream;
using std::shared_ptr;
using std::string;
using std::vector;
using yb::consensus::ConsensusServiceProxy;
using yb::consensus::RaftConfigPB;
using yb::rpc::Messenger;
using yb::rpc::MessengerBuilder;
using yb::rpc::RpcController;
using yb::server::ReloadCertificatesRequestPB;
using yb::server::ReloadCertificatesResponsePB;
using yb::server::ServerStatusPB;
using yb::tablet::TabletStatusPB;
using yb::tserver::ClearAllMetaCachesOnServerRequestPB;
using yb::tserver::ClearAllMetaCachesOnServerResponsePB;
using yb::tserver::ClearUniverseUuidRequestPB;
using yb::tserver::ClearUniverseUuidResponsePB;
using yb::tserver::CountIntentsRequestPB;
using yb::tserver::CountIntentsResponsePB;
using yb::tserver::DeleteTabletRequestPB;
using yb::tserver::DeleteTabletResponsePB;
using yb::tserver::FlushTabletsRequestPB;
using yb::tserver::FlushTabletsResponsePB;
using yb::tserver::IsTabletServerReadyRequestPB;
using yb::tserver::IsTabletServerReadyResponsePB;
using yb::tserver::ListTabletsRequestPB;
using yb::tserver::ListTabletsResponsePB;
using yb::tserver::TabletServerAdminServiceProxy;
using yb::tserver::TabletServerServiceProxy;
using yb::tserver::ListMasterServersRequestPB;
using yb::tserver::ListMasterServersResponsePB;
using yb::tserver::AcquireObjectLockRequestPB;
using yb::tserver::AcquireObjectLockResponsePB;
using yb::tserver::ReleaseObjectLockRequestPB;
using yb::tserver::ReleaseObjectLockResponsePB;
using yb::consensus::StartRemoteBootstrapRequestPB;
using yb::consensus::StartRemoteBootstrapResponsePB;

const char* const kListTabletsOp = "list_tablets";
const char* const kVerifyTabletOp = "verify_tablet";
const char* const kAreTabletsRunningOp = "are_tablets_running";
const char* const kIsServerReadyOp = "is_server_ready";
const char* const kSetFlagOp = "set_flag";
const char* const kValidateFlagValueOp = "validate_flag_value";
const char* const kRefreshFlagsOp = "refresh_flags";
const char* const kDumpTabletOp = "dump_tablet";
const char* const kTabletStateOp = "get_tablet_state";
const char* const kDeleteTabletOp = "delete_tablet";
const char* const kUnsafeConfigChange = "unsafe_config_change";
const char* const kCurrentHybridTime = "current_hybrid_time";
const char* const kStatus = "status";
const char* const kCountIntents = "count_intents";
const char* const kFlushTabletOp = "flush_tablet";
const char* const kFlushAllTabletsOp = "flush_all_tablets";
const char* const kCompactTabletOp = "compact_tablet";
const char* const kCompactAllTabletsOp = "compact_all_tablets";
const char* const kReloadCertificatesOp = "reload_certificates";
const char* const kRemoteBootstrapOp = "remote_bootstrap";
const char* const kListMasterServersOp = "list_master_servers";
const char* const kClearAllMetaCachesOnServerOp = "clear_server_metacache";
const char* const kClearUniverseUuidOp = "clear_universe_uuid";
const char* const kAcquireObjectLockOp = "acquire_object_lock";
const char* const kReleaseObjectLockOp = "release_object_lock";
const char* const kReleaseAllLocksForSessionOp = "release_all_locks_for_session";

DEFINE_NON_RUNTIME_string(server_address, "localhost",
              "Address of server to run against");
DEFINE_NON_RUNTIME_int64(timeout_ms, 1000 * 60, "RPC timeout in milliseconds");

DEFINE_NON_RUNTIME_bool(force, false, "set_flag: If true, allows command to set a flag "
            "which is not explicitly marked as runtime-settable. Such flag changes may be "
            "simply ignored on the server, or may cause the server to crash.\n"
            "delete_tablet: If true, command will delete the tablet and remove the tablet "
            "from the memory, otherwise tablet metadata will be kept in memory with state "
            "TOMBSTONED.");

DEFINE_NON_RUNTIME_string(certs_dir_name, "",
    "Directory with certificates to use for secure server connection.");

DEFINE_NON_RUNTIME_string(client_node_name, "", "Client node name.");

PB_ENUM_FORMATTERS(yb::consensus::LeaderLeaseStatus);

// Check that the value of argc matches what's expected, otherwise return a
// non-zero exit code. Should be used in main().
#define CHECK_ARGC_OR_RETURN_WITH_USAGE(op, expected) \
  do { \
    const string& _op = (op); \
    const int _expected = (expected); \
    if (argc != _expected) { \
      /* We substract 2 from _expected because we don't want to count argv[0] or [1]. */ \
      std::cerr << "Invalid number of arguments for " << _op \
                << ": expected " << (_expected - 2) << " arguments" << std::endl; \
      google::ShowUsageWithFlagsRestrict(argv[0], __FILE__); \
      return 2; \
    } \
  } while (0);

// Invoke 'to_call' and check its result. If it failed, print 'to_prepend' and
// the error to cerr and return a non-zero exit code. Should be used in main().
#define RETURN_NOT_OK_PREPEND_FROM_MAIN(to_call, to_prepend) \
  do { \
    ::yb::Status s = (to_call); \
    if (!s.ok()) { \
      std::cerr << (to_prepend) << ": " << s.ToString() << std::endl; \
      return 1; \
    } \
  } while (0);

namespace yb {
namespace tools {

typedef ListTabletsResponsePB::StatusAndSchemaPB StatusAndSchemaPB;
typedef ListMasterServersResponsePB::MasterServerAndTypePB MasterServerAndTypePB;

class TsAdminClient {
 public:
  // Creates an admin client for host/port combination e.g.,
  // "localhost" or "127.0.0.1:7050".
  TsAdminClient(std::string addr, int64_t timeout_millis);

  ~TsAdminClient();

  // Initialized the client and connects to the specified tablet
  // server.
  Status Init();

  // Make a generic proxy to the given address. Use generic_proxy_ instead for connections
  // to the address this client is pointed at.
  Result<std::shared_ptr<server::GenericServiceProxy>> MakeGenericServiceProxy(
      const std::string& addr);

  // Sets 'tablets' a list of status information for all tablets on a
  // given tablet server.
  Status ListTablets(std::vector<StatusAndSchemaPB>* tablets);

  // Gets the number of tablets waiting to be bootstrapped and prints to console.
  Status GetNumUnbootstrappedTablets(int64_t* num_unbootstrapped_tablets);

  // Sets the gflag 'flag' to 'val' on the remote server via RPC.
  // If 'force' is true, allows setting flags even if they're not marked as
  // safe to change at runtime.
  Status SetFlag(const string& flag, const string& val,
                 bool force);

  // Validates the value of a flag without actually setting it.
  Status ValidateFlagValue(const string& flag, const string& val);

  // Refreshes all gflags on the remote server to the flagfile, via RPC.
  Status RefreshFlags();

  // Get the schema for the given tablet.
  Status GetTabletSchema(const std::string& tablet_id, SchemaPB* schema);

  // Dump the contents of the given tablet, in key order, to the console.
  Status DumpTablet(const std::string& tablet_id);

  // Print the consensus state to the console.
  Status PrintConsensusState(const std::string& tablet_id);

  // Delete a tablet replica from the specified peer.
  // The 'reason' string is passed to the tablet server, used for logging.
  Status DeleteTablet(const std::string& tablet_id,
                      const std::string& reason,
                      tablet::TabletDataState delete_type);

  Status UnsafeConfigChange(const std::string& tablet_id,
                            const std::vector<string>& peers);


  // Sets hybrid_time to the value of the tablet server's current hybrid_time.
  Status CurrentHybridTime(uint64_t* hybrid_time);

  // Get the server status
  Status GetStatus(ServerStatusPB* pb, const std::string& addr = "");

  // Count write intents on all tablets.
  Status CountIntents(int64_t* num_intents);

  // Flush or compact a given tablet on a given tablet server.
  // If 'tablet_id' is empty string, flush or compact all tablets.
  Status FlushTablets(const std::string& tablet_id, bool is_compaction);

  // Verify the given tablet against its indexes
  // Assume the tablet belongs to a main table
  Status VerifyTablet(
      const std::string& tablet_id,
      const std::vector<string>& index_ids,
      const string& start_key,
      const int num_rows);

  // Trigger a reload of TLS certificates.
  Status ReloadCertificates();

  // Performs a manual remote bootstrap onto `target_server` for a given tablet.
  Status RemoteBootstrap(const std::string& target_server, const std::string& tablet_id);

  // List information for all master servers.
  Status ListMasterServers();

  Status ClearAllMetaCachesOnServer();

  // Clear Universe Uuid.
  Status ClearUniverseUuid();

  Status AcquireObjectLock(
      const std::string& session_id, const string& object_id, const std::string& lock_mode);
  Status ReleaseObjectLock(
      const std::string& session_id, const string& object_id, int argc, char** argv);
  Status ReleaseAllLocksForSession(const std::string& session_id);

 private:
  std::string addr_;
  MonoDelta timeout_;
  bool initted_;
  std::unique_ptr<rpc::SecureContext> secure_context_;
  std::unique_ptr<rpc::Messenger> messenger_;
  shared_ptr<server::GenericServiceProxy> generic_proxy_;
  std::unique_ptr<tserver::TabletServerServiceProxy> ts_proxy_;
  std::unique_ptr<tserver::TabletServerAdminServiceProxy> ts_admin_proxy_;
  std::unique_ptr<consensus::ConsensusServiceProxy> cons_proxy_;

  DISALLOW_COPY_AND_ASSIGN(TsAdminClient);
};

TsAdminClient::TsAdminClient(string addr, int64_t timeout_millis)
    : addr_(std::move(addr)),
      timeout_(MonoDelta::FromMilliseconds(timeout_millis)),
      initted_(false) {}

TsAdminClient::~TsAdminClient() {
  if (messenger_) {
    messenger_->Shutdown();
  }
}

Status TsAdminClient::Init() {
  CHECK(!initted_);

  HostPort host_port;
  RETURN_NOT_OK(host_port.ParseString(addr_, tserver::TabletServer::kDefaultPort));
  auto messenger_builder = MessengerBuilder("ts-cli");
  if (!FLAGS_certs_dir_name.empty()) {
    const std::string& cert_name = FLAGS_client_node_name;
    secure_context_ = VERIFY_RESULT(rpc::CreateSecureContext(
        FLAGS_certs_dir_name, rpc::UseClientCerts(!cert_name.empty()), cert_name));
    rpc::ApplySecureContext(secure_context_.get(), &messenger_builder);
  }
  messenger_ = VERIFY_RESULT(messenger_builder.Build());

  rpc::ProxyCache proxy_cache(messenger_.get());

  generic_proxy_.reset(new server::GenericServiceProxy(&proxy_cache, host_port));
  ts_proxy_.reset(new TabletServerServiceProxy(&proxy_cache, host_port));
  ts_admin_proxy_.reset(new TabletServerAdminServiceProxy(&proxy_cache, host_port));
  cons_proxy_.reset(new ConsensusServiceProxy(&proxy_cache, host_port));
  initted_ = true;

  VLOG(1) << "Connected to " << addr_;

  return Status::OK();
}

Result<std::shared_ptr<server::GenericServiceProxy>> TsAdminClient::MakeGenericServiceProxy(
    const std::string& addr) {
  HostPort host_port;
  RETURN_NOT_OK(host_port.ParseString(addr, tserver::TabletServer::kDefaultPort));

  rpc::ProxyCache proxy_cache(messenger_.get());

  auto proxy = std::make_shared<server::GenericServiceProxy>(&proxy_cache, host_port);

  VLOG(1) << "Connected to " << addr;

  return proxy;
}

Status TsAdminClient::ListTablets(vector<StatusAndSchemaPB>* tablets) {
  CHECK(initted_);

  ListTabletsRequestPB req;
  ListTabletsResponsePB resp;
  RpcController rpc;

  rpc.set_timeout(timeout_);
  RETURN_NOT_OK(ts_proxy_->ListTablets(req, &resp, &rpc));
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  tablets->assign(resp.status_and_schema().begin(), resp.status_and_schema().end());

  return Status::OK();
}

Status TsAdminClient::GetNumUnbootstrappedTablets(int64_t* num_unbootstrapped_tablets) {
  CHECK(initted_);

  IsTabletServerReadyRequestPB req;
  IsTabletServerReadyResponsePB resp;
  RpcController rpc;

  rpc.set_timeout(timeout_);
  RETURN_NOT_OK(ts_proxy_->IsTabletServerReady(req, &resp, &rpc));
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  std::cout << resp.num_tablets_not_running() << "/" << resp.total_tablets()
            << " tablets are not yet bootstrapped" << std::endl;
  *num_unbootstrapped_tablets = resp.num_tablets_not_running();

  return Status::OK();
}

Status TsAdminClient::SetFlag(const string& flag, const string& val,
                              bool force) {
  server::SetFlagRequestPB req;
  server::SetFlagResponsePB resp;
  RpcController rpc;

  rpc.set_timeout(timeout_);
  req.set_flag(flag);
  req.set_value(val);
  req.set_force(force);

  RETURN_NOT_OK(generic_proxy_->SetFlag(req, &resp, &rpc));
  switch (resp.result()) {
    case server::SetFlagResponsePB::SUCCESS:
      return Status::OK();
    case server::SetFlagResponsePB::NOT_SAFE:
      return STATUS(RemoteError, resp.msg() + " (use --force flag to allow anyway)");
    default:
      return STATUS(RemoteError, resp.ShortDebugString());
  }
}

Status TsAdminClient::ValidateFlagValue(const string& flag, const string& val) {
  server::ValidateFlagValueRequestPB req;
  server::ValidateFlagValueResponsePB resp;
  RpcController rpc;

  rpc.set_timeout(timeout_);
  req.set_flag_name(flag);
  req.set_flag_value(val);

  return generic_proxy_->ValidateFlagValue(req, &resp, &rpc);
}

Status TsAdminClient::RefreshFlags() {
  server::RefreshFlagsRequestPB req;
  server::RefreshFlagsResponsePB resp;
  RpcController rpc;

  rpc.set_timeout(timeout_);

  return generic_proxy_->RefreshFlags(req, &resp, &rpc);
}

Status TsAdminClient::VerifyTablet(
    const std::string& tablet_id,
    const std::vector<string>& index_ids,
    const string& start_key,
    const int num_rows) {
  tserver::VerifyTableRowRangeRequestPB req;
  tserver::VerifyTableRowRangeResponsePB resp;

  req.set_tablet_id(tablet_id);
  req.set_start_key("");
  req.set_num_rows(num_rows);
  for (const std::string& str : index_ids) {
    req.add_index_ids(str);
  }

  RpcController rpc;
  rpc.set_timeout(timeout_);

  RETURN_NOT_OK(ts_proxy_->VerifyTableRowRange(req, &resp, &rpc));
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  std::cout << "Reporting VerifyJob stats." << std::endl;
  for (auto it = resp.consistency_stats().begin(); it != resp.consistency_stats().end(); it++) {
    std::cout << "VerifyJob found " << it->second << " mismatched rows for index " << it->first
              << std::endl;
  }

  return Status::OK();
}

Status TsAdminClient::GetTabletSchema(const std::string& tablet_id,
                                      SchemaPB* schema) {
  VLOG(1) << "Fetching schema for tablet " << tablet_id;
  vector<StatusAndSchemaPB> tablets;
  RETURN_NOT_OK(ListTablets(&tablets));
  for (const StatusAndSchemaPB& pair : tablets) {
    if (pair.tablet_status().tablet_id() == tablet_id) {
      *schema = pair.schema();
      return Status::OK();
    }
  }
  return STATUS(NotFound, "Cannot find tablet", tablet_id);
}

Status TsAdminClient::PrintConsensusState(const std::string& tablet_id) {
  ServerStatusPB status_pb;
  RETURN_NOT_OK(GetStatus(&status_pb));

  consensus::GetConsensusStateRequestPB cons_reqpb;
  cons_reqpb.set_dest_uuid(status_pb.node_instance().permanent_uuid());
  cons_reqpb.set_tablet_id(tablet_id);

  consensus::GetConsensusStateResponsePB cons_resp_pb;
  RpcController rpc;
  RETURN_NOT_OK_PREPEND(
      cons_proxy_->GetConsensusState(cons_reqpb, &cons_resp_pb, &rpc),
      "Failed to query tserver for consensus state");
  std::cout << "Lease-Status"
            << "\t\t"
            << " Leader-UUID ";
  std::cout << PBEnumToString(cons_resp_pb.leader_lease_status()) << "\t\t"
            << cons_resp_pb.cstate().leader_uuid();

  return Status::OK();
}

Status TsAdminClient::DumpTablet(const std::string& tablet_id) {
  SchemaPB schema_pb;
  RETURN_NOT_OK(GetTabletSchema(tablet_id, &schema_pb));
  Schema schema;
  RETURN_NOT_OK(SchemaFromPB(schema_pb, &schema));

  tserver::ReadRequestPB req;
  tserver::ReadResponsePB resp;

  req.set_tablet_id(tablet_id);
  RpcController rpc;
  rpc.set_timeout(timeout_);
  RETURN_NOT_OK_PREPEND(ts_proxy_->Read(req, &resp, &rpc), "Read() failed");

  if (resp.has_error()) {
    return STATUS(IOError, "Failed to read: ", resp.error().ShortDebugString());
  }

  qlexpr::QLRowBlock row_block(schema);
  auto data_buffer = VERIFY_RESULT(rpc.ExtractSidecar(0));
  auto data = data_buffer.AsSlice();
  if (!data.empty()) {
    RETURN_NOT_OK(row_block.Deserialize(YQL_CLIENT_CQL, &data));
  }

  for (const auto& row : row_block.rows()) {
    std::cout << row.ToString() << std::endl;
  }

  return Status::OK();
}

Status TsAdminClient::DeleteTablet(const string& tablet_id,
                                   const string& reason,
                                   tablet::TabletDataState delete_type) {
  ServerStatusPB status_pb;
  RETURN_NOT_OK(GetStatus(&status_pb));

  DeleteTabletRequestPB req;
  DeleteTabletResponsePB resp;
  RpcController rpc;

  req.set_tablet_id(tablet_id);
  req.set_dest_uuid(status_pb.node_instance().permanent_uuid());
  req.set_reason(reason);
  req.set_delete_type(delete_type);
  rpc.set_timeout(timeout_);
  RETURN_NOT_OK_PREPEND(ts_admin_proxy_->DeleteTablet(req, &resp, &rpc),
                        "DeleteTablet() failed");

  if (resp.has_error()) {
    return STATUS(IOError, "Failed to delete tablet: ",
                           resp.error().ShortDebugString());
  }
  return Status::OK();
}

Status TsAdminClient::UnsafeConfigChange(const std::string& tablet_id,
                                         const std::vector<string>& peers) {
  ServerStatusPB status_pb;
  RETURN_NOT_OK(GetStatus(&status_pb));

  if (peers.empty()) {
    return STATUS(InvalidArgument, "No peer UUIDs specified for the new config");
  }
  RaftConfigPB new_config;
  for (const auto& arg : peers) {
    consensus::RaftPeerPB new_peer;
    new_peer.set_permanent_uuid(arg);
    new_config.add_peers()->CopyFrom(new_peer);
  }

  // Send a request to replace the config to node dst_address.
  consensus::UnsafeChangeConfigRequestPB req;
  consensus::UnsafeChangeConfigResponsePB resp;
  RpcController rpc;
  rpc.set_timeout(timeout_);
  req.set_dest_uuid(status_pb.node_instance().permanent_uuid());
  req.set_tablet_id(tablet_id);
  req.set_caller_id("yb-ts-cli");
  *req.mutable_new_config() = new_config;
  RETURN_NOT_OK(cons_proxy_->UnsafeChangeConfig(req, &resp, &rpc));
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  return Status::OK();
}

Status TsAdminClient::CurrentHybridTime(uint64_t* hybrid_time) {
  server::ServerClockRequestPB req;
  server::ServerClockResponsePB resp;
  RpcController rpc;
  rpc.set_timeout(timeout_);
  RETURN_NOT_OK(generic_proxy_->ServerClock(req, &resp, &rpc));
  CHECK(resp.has_hybrid_time()) << resp.DebugString();
  *hybrid_time = resp.hybrid_time();
  return Status::OK();
}

Status TsAdminClient::GetStatus(ServerStatusPB* pb, const std::string& addr) {
  auto proxy = addr.empty() || addr == addr_
      ? generic_proxy_
      : VERIFY_RESULT(MakeGenericServiceProxy(addr));

  server::GetStatusRequestPB req;
  server::GetStatusResponsePB resp;
  RpcController rpc;
  rpc.set_timeout(timeout_);
  RETURN_NOT_OK(proxy->GetStatus(req, &resp, &rpc));
  CHECK(resp.has_status()) << resp.DebugString();
  pb->Swap(resp.mutable_status());
  return Status::OK();
}

Status TsAdminClient::CountIntents(int64_t* num_intents) {
  CountIntentsRequestPB req;
  CountIntentsResponsePB resp;
  RpcController rpc;
  rpc.set_timeout(timeout_);
  RETURN_NOT_OK(ts_admin_proxy_->CountIntents(req, &resp, &rpc));
  *num_intents = resp.num_intents();
  return Status::OK();
}

Status TsAdminClient::FlushTablets(const std::string& tablet_id, bool is_compaction) {
  ServerStatusPB status_pb;
  RETURN_NOT_OK(GetStatus(&status_pb));

  FlushTabletsRequestPB req;
  FlushTabletsResponsePB resp;
  RpcController rpc;

  if (!tablet_id.empty()) {
    req.add_tablet_ids(tablet_id);
    req.set_all_tablets(false);
  } else {
    req.set_all_tablets(true);
  }
  req.set_dest_uuid(status_pb.node_instance().permanent_uuid());
  req.set_operation(is_compaction ? tserver::FlushTabletsRequestPB::COMPACT
                                  : tserver::FlushTabletsRequestPB::FLUSH);
  rpc.set_timeout(timeout_);
  RETURN_NOT_OK_PREPEND(ts_admin_proxy_->FlushTablets(req, &resp, &rpc),
                        "FlushTablets() failed");

  if (resp.has_error()) {
    return STATUS(IOError, "Failed to flush tablet: ",
                           resp.error().ShortDebugString());
  }
  std::cout << "Successfully " << (is_compaction ? "compacted " : "flushed ")
            << (tablet_id.empty() ? "all tablets" : "tablet <" + tablet_id + ">")
            << std::endl;
  return Status::OK();
}

Status TsAdminClient::ReloadCertificates() {
  CHECK(initted_);

  ReloadCertificatesRequestPB req;
  ReloadCertificatesResponsePB resp;
  RpcController rpc;

  rpc.set_timeout(timeout_);
  RETURN_NOT_OK(generic_proxy_->ReloadCertificates(req, &resp, &rpc));

  return Status::OK();
}

Status TsAdminClient::RemoteBootstrap(const std::string& source_server,
                                      const std::string& tablet_id) {
  ServerStatusPB status_pb;
  RETURN_NOT_OK(GetStatus(&status_pb));

  ServerStatusPB source_server_status_pb;
  RETURN_NOT_OK(GetStatus(&source_server_status_pb, source_server));

  StartRemoteBootstrapRequestPB req;
  StartRemoteBootstrapResponsePB resp;
  RpcController rpc;

  HostPort host_port;
  RETURN_NOT_OK(host_port.ParseString(source_server, tserver::TabletServer::kDefaultPort));

  req.set_dest_uuid(status_pb.node_instance().permanent_uuid());
  req.set_tablet_id(tablet_id);
  req.set_caller_term(std::numeric_limits<int64_t>::max());
  req.set_bootstrap_source_peer_uuid(source_server_status_pb.node_instance().permanent_uuid());
  HostPortToPB(host_port, req.mutable_bootstrap_source_private_addr()->Add());
  rpc.set_timeout(timeout_);
  RETURN_NOT_OK_PREPEND(cons_proxy_->StartRemoteBootstrap(req, &resp, &rpc),
                        "StartRemoteBootstrap() failed");

  if (resp.has_error()) {
    return STATUS(IOError, "Failed to start remote bootstrap: ",
                           resp.error().ShortDebugString());
  }

  std::cout << "Successfully started remote bootstrap for "
            << "tablet <" + tablet_id + ">"
            << " from server <" << source_server << ">"
            << std::endl;
  return Status::OK();
}

Status TsAdminClient::ListMasterServers() {
  CHECK(initted_);
  std::vector<MasterServerAndTypePB> master_servers;
  ListMasterServersRequestPB req;
  ListMasterServersResponsePB resp;
  RpcController rpc;

  rpc.set_timeout(timeout_);
  RETURN_NOT_OK(ts_proxy_->ListMasterServers(req, &resp, &rpc));
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  master_servers.assign(resp.master_server_and_type().begin(),
                          resp.master_server_and_type().end());

  std::cout << "RPC Host/Port\t\tRole" << std::endl;
  for (const auto &master_server_and_type : master_servers) {
    std::string leader_string = master_server_and_type.is_leader() ? "Leader" : "Follower";
    std::cout << master_server_and_type.master_server() << "\t\t" << leader_string << std::endl;
  }

  return Status::OK();
}

Status TsAdminClient::ClearAllMetaCachesOnServer() {
  CHECK(initted_);
  tserver::ClearAllMetaCachesOnServerRequestPB req;
  tserver::ClearAllMetaCachesOnServerResponsePB resp;
  RpcController rpc;
  rpc.set_timeout(timeout_);
  RETURN_NOT_OK(ts_proxy_->ClearAllMetaCachesOnServer(req, &resp, &rpc));
  return Status::OK();
}

Status TsAdminClient::ClearUniverseUuid() {
  CHECK(initted_);
  ClearUniverseUuidRequestPB req;
  ClearUniverseUuidResponsePB resp;
  RpcController rpc;

  rpc.set_timeout(timeout_);
  RETURN_NOT_OK(ts_proxy_->ClearUniverseUuid(req, &resp, &rpc));
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }

  std::cout << "Universe UUID cleared from Instance Metadata" << std::endl;
  return Status::OK();
}

Status TsAdminClient::AcquireObjectLock(
    const string& session_id, const string& object_id, const std::string& lock_mode) {
  SCHECK(initted_, IllegalState, "TsAdminClient not initialized");
  static const std::unordered_map<std::string, TableLockType> lock_key_entry_map = {
    {"ACCESS_SHARE", ACCESS_SHARE},
    {"ROW_SHARE", ROW_SHARE},
    {"ROW_EXCLUSIVE", ROW_EXCLUSIVE},
    {"SHARE_UPDATE_EXCLUSIVE", SHARE_UPDATE_EXCLUSIVE},
    {"SHARE", SHARE},
    {"SHARE_ROW_EXCLUSIVE", SHARE_ROW_EXCLUSIVE},
    {"EXCLUSIVE", EXCLUSIVE},
    {"ACCESS_EXCLUSIVE", ACCESS_EXCLUSIVE},
  };
  auto it = lock_key_entry_map.find(lock_mode);
  SCHECK(it != lock_key_entry_map.end(), InvalidArgument, "Unsupported lock mode");

  tserver::AcquireObjectLockRequestPB req;
  tserver::AcquireObjectLockResponsePB resp;
  RpcController rpc;
  rpc.set_timeout(timeout_);

  req.set_session_id(stoi(session_id));
  req.set_session_host_uuid(FLAGS_server_address);
  auto* object_lock_req = req.add_object_locks();
  object_lock_req->set_id(stoi(object_id));
  object_lock_req->set_lock_type(it->second);

  RETURN_NOT_OK(ts_proxy_->AcquireObjectLocks(req, &resp, &rpc));

  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  std::cout << "Acquired lock for session id=" << session_id
            << " on object with id=" << object_id
            << " and mode " << lock_mode << " at tserver local object lock manager" << std::endl;
  return Status::OK();
}

Status TsAdminClient::ReleaseObjectLock(
    const std::string& session_id, const string& object_id, int argc, char** argv) {
  SCHECK(initted_, IllegalState, "TsAdminClient not initialized");

  tserver::ReleaseObjectLockRequestPB req;
  tserver::ReleaseObjectLockResponsePB resp;
  RpcController rpc;
  rpc.set_timeout(timeout_);

  req.set_session_id(stoi(session_id));
  req.set_session_host_uuid(FLAGS_server_address);
  req.add_object_ids(stoi(object_id));
  for (int i = 4; i < argc; i++) {
    req.add_object_ids(atoi(argv[i]));
  }

  RETURN_NOT_OK(ts_proxy_->ReleaseObjectLocks(req, &resp, &rpc));

  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  std::cout << "Released all locks on object with id=" << object_id
            << " for session id=" << session_id
            << " at tserver local object lock manager" << std::endl;
  return Status::OK();
}

Status TsAdminClient::ReleaseAllLocksForSession(const std::string& session_id) {
  SCHECK(initted_, IllegalState, "TsAdminClient not initialized");

  tserver::ReleaseObjectLockRequestPB req;
  tserver::ReleaseObjectLockResponsePB resp;
  RpcController rpc;
  rpc.set_timeout(timeout_);

  req.set_session_id(stoi(session_id));
  req.set_session_host_uuid(FLAGS_server_address);
  req.set_release_all_locks(true);
  RETURN_NOT_OK(ts_proxy_->ReleaseObjectLocks(req, &resp, &rpc));
  if (resp.has_error()) {
    return StatusFromPB(resp.error().status());
  }
  std::cout << "Released all locks for session id=" << session_id
            << " at tserver local object lock manager" << std::endl;
  return Status::OK();
}

namespace {

void SetUsage(const char* argv0) {
  ostringstream str;

  str << argv0 << " [--server_address=<addr>] <operation> <flags>\n"
      << "<operation> must be one of:\n"
      << "  " << kListTabletsOp << "\n"
      << "  " << kAreTabletsRunningOp << "\n"
      << "  " << kIsServerReadyOp << "\n"
      << "  " << kSetFlagOp << " [-force] <flag> <value>\n"
      << "  " << kValidateFlagValueOp << " <flag> <value>\n"
      << "  " << kRefreshFlagsOp << "\n"
      << "  " << kTabletStateOp << " <tablet_id>\n"
      << "  " << kDumpTabletOp << " <tablet_id>\n"
      << "  " << kDeleteTabletOp << " [-force] <tablet_id> <reason string>\n"
      << "  " << kUnsafeConfigChange << " <tablet_id> <peer1> [<peer2>...]\n"
      << "  " << kCurrentHybridTime << "\n"
      << "  " << kStatus << "\n"
      << "  " << kCountIntents << "\n"
      << "  " << kFlushTabletOp << " <tablet_id>\n"
      << "  " << kFlushAllTabletsOp << "\n"
      << "  " << kCompactTabletOp << " <tablet_id>\n"
      << "  " << kCompactAllTabletsOp << "\n"
      << "  " << kVerifyTabletOp
      << " <tablet_id> <number of indexes> <index list> <start_key> <number of rows>\n"
      << "  " << kReloadCertificatesOp << "\n"
      << "  " << kRemoteBootstrapOp << " <server address to bootstrap from> <tablet_id>\n"
      << "  " << kListMasterServersOp << "\n"
      << "  " << kClearUniverseUuidOp << "\n"
      << "  " << kAcquireObjectLockOp << " <session id> <object_id> <lock type>\n"
      << "  " << kReleaseObjectLockOp << " <session id> <object_id> [<object_id> <object_id>...]\n"
      << "  " << kReleaseAllLocksForSessionOp << " <session id>\n";
  google::SetUsageMessage(str.str());
}

string GetOp(int argc, char** argv) {
  if (argc < 2) {
    google::ShowUsageWithFlagsRestrict(argv[0], __FILE__);
    exit(1);
  }

  return argv[1];
}

} // anonymous namespace

static int TsCliMain(int argc, char** argv) {
  FLAGS_logtostderr = 1;
  FLAGS_minloglevel = 2;
  SetUsage(argv[0]);
  ParseCommandLineFlags(&argc, &argv, true);
  InitGoogleLoggingSafe(argv[0]);
  const string addr = FLAGS_server_address;

  string op = GetOp(argc, argv);

  TsAdminClient client(addr, FLAGS_timeout_ms);

  RETURN_NOT_OK_PREPEND_FROM_MAIN(client.Init(),
                                  "Unable to establish connection to " + addr);

  // TODO add other operations here...
  if (op == kListTabletsOp) {
    CHECK_ARGC_OR_RETURN_WITH_USAGE(op, 2);

    vector<StatusAndSchemaPB> tablets;
    RETURN_NOT_OK_PREPEND_FROM_MAIN(client.ListTablets(&tablets),
                                    "Unable to list tablets on " + addr);
    for (const StatusAndSchemaPB& status_and_schema : tablets) {
      Schema schema;
      RETURN_NOT_OK_PREPEND_FROM_MAIN(SchemaFromPB(status_and_schema.schema(), &schema),
                                      "Unable to deserialize schema from " + addr);
      dockv::PartitionSchema partition_schema;
      RETURN_NOT_OK_PREPEND_FROM_MAIN(
          dockv::PartitionSchema::FromPB(
              status_and_schema.partition_schema(), schema, &partition_schema),
          "Unable to deserialize partition schema from " + addr);


      TabletStatusPB ts = status_and_schema.tablet_status();

      dockv::Partition partition;
      dockv::Partition::FromPB(ts.partition(), &partition);

      string state = tablet::RaftGroupStatePB_Name(ts.state());
      std::cout << "Tablet id: " << ts.tablet_id() << std::endl;
      std::cout << "State: " << state << std::endl;
      std::cout << "Table name: " << ts.table_name() << std::endl;
      std::cout << "Partition: " << partition_schema.PartitionDebugString(partition, schema)
                << std::endl;
      std::cout << "Schema: " << schema.ToString() << std::endl;
    }
  } else if (op == kVerifyTabletOp) {
    string tablet_id = argv[2];
    int num_indexes = std::stoi(argv[3]);
    std::vector<string> index_ids;
    for (int i = 0; i < num_indexes; i++) {
      index_ids.push_back(argv[4 + i]);
    }
    string start_key = argv[num_indexes + 4];
    int num_rows = std::stoi(argv[num_indexes + 5]);

    RETURN_NOT_OK_PREPEND_FROM_MAIN(
        client.VerifyTablet(tablet_id, index_ids, start_key, num_rows),
        "Unable to verify tablet " + tablet_id);

  } else if (op == kAreTabletsRunningOp) {
    CHECK_ARGC_OR_RETURN_WITH_USAGE(op, 2);

    vector<StatusAndSchemaPB> tablets;
    RETURN_NOT_OK_PREPEND_FROM_MAIN(client.ListTablets(&tablets),
                                    "Unable to list tablets on " + addr);
    bool all_running = true;
    for (const StatusAndSchemaPB& status_and_schema : tablets) {
      TabletStatusPB ts = status_and_schema.tablet_status();
      if (ts.state() != tablet::RUNNING) {
        std::cout << "Tablet id: " << ts.tablet_id() << " is "
                  << tablet::RaftGroupStatePB_Name(ts.state()) << std::endl;
        all_running = false;
      }
    }

    if (all_running) {
      std::cout << "All tablets are running" << std::endl;
    } else {
      std::cout << "Not all tablets are running" << std::endl;
      return 1;
    }
  } else if (op == kIsServerReadyOp) {
    CHECK_ARGC_OR_RETURN_WITH_USAGE(op, 2);

    int64_t unbootstrapped_tablets;
    RETURN_NOT_OK_PREPEND_FROM_MAIN(
        client.GetNumUnbootstrappedTablets(&unbootstrapped_tablets), "Unable to read server state");

    if (unbootstrapped_tablets > 0) {
      std::cout << "Tablet server is not ready" << std::endl;
      return 1;
    } else {
      std::cout << "Tablet server is ready" << std::endl;
    }
  } else if (op == kSetFlagOp) {
    CHECK_ARGC_OR_RETURN_WITH_USAGE(op, 4);

    RETURN_NOT_OK_PREPEND_FROM_MAIN(client.SetFlag(argv[2], argv[3], FLAGS_force),
                                    "Unable to set flag");

  } else if (op == kValidateFlagValueOp) {
    CHECK_ARGC_OR_RETURN_WITH_USAGE(op, 4);

    RETURN_NOT_OK_PREPEND_FROM_MAIN(
        client.ValidateFlagValue(argv[2], argv[3]), "Invalid flag value");
    std::cout << "Flag value is valid" << std::endl;

  } else if (op == kRefreshFlagsOp) {
    CHECK_ARGC_OR_RETURN_WITH_USAGE(op, 2);

    RETURN_NOT_OK_PREPEND_FROM_MAIN(client.RefreshFlags(),
        "Unable to refresh flags");
  } else if (op == kTabletStateOp) {
    CHECK_ARGC_OR_RETURN_WITH_USAGE(op, 3);

    string tablet_id = argv[2];
    RETURN_NOT_OK_PREPEND_FROM_MAIN(
        client.PrintConsensusState(tablet_id), "Unable to print tablet state");
  } else if (op == kDumpTabletOp) {
    CHECK_ARGC_OR_RETURN_WITH_USAGE(op, 3);

    string tablet_id = argv[2];
    RETURN_NOT_OK_PREPEND_FROM_MAIN(client.DumpTablet(tablet_id),
                                    "Unable to dump tablet");
  } else if (op == kDeleteTabletOp) {
    CHECK_ARGC_OR_RETURN_WITH_USAGE(op, 4);

    string tablet_id = argv[2];
    string reason = argv[3];
    tablet::TabletDataState state = FLAGS_force ? tablet::TABLET_DATA_DELETED :
                                                  tablet::TABLET_DATA_TOMBSTONED;
    RETURN_NOT_OK_PREPEND_FROM_MAIN(client.DeleteTablet(tablet_id, reason, state),
                                    "Unable to delete tablet");
  } else if (op == kUnsafeConfigChange) {
    if (argc < 4) {
      CHECK_ARGC_OR_RETURN_WITH_USAGE(op, 4);
    }

    string tablet_id = argv[2];
    vector<string> peers;
    for (int i = 3; i < argc; i++) {
      peers.push_back(argv[i]);
    }

    RETURN_NOT_OK_PREPEND_FROM_MAIN(
        client.UnsafeConfigChange(tablet_id, peers), "Unable to change config unsafely.");
  } else if (op == kCurrentHybridTime) {
    CHECK_ARGC_OR_RETURN_WITH_USAGE(op, 2);

    uint64_t hybrid_time;
    RETURN_NOT_OK_PREPEND_FROM_MAIN(client.CurrentHybridTime(&hybrid_time),
                                    "Unable to get hybrid_time");
    std::cout << hybrid_time << std::endl;
  } else if (op == kStatus) {
    CHECK_ARGC_OR_RETURN_WITH_USAGE(op, 2);

    ServerStatusPB status;
    RETURN_NOT_OK_PREPEND_FROM_MAIN(client.GetStatus(&status),
                                    "Unable to get status");
    std::cout << status.DebugString() << std::endl;
  } else if (op == kCountIntents) {
    CHECK_ARGC_OR_RETURN_WITH_USAGE(op, 2);
    int64_t num_intents = 0;

    RETURN_NOT_OK_PREPEND_FROM_MAIN(client.CountIntents(&num_intents),
                                    "Unable to count intents");

    std::cout << num_intents << std::endl;
  } else if (op == kFlushTabletOp) {
    CHECK_ARGC_OR_RETURN_WITH_USAGE(op, 3);

    string tablet_id = argv[2];
    RETURN_NOT_OK_PREPEND_FROM_MAIN(client.FlushTablets(tablet_id, false /* is_compaction */),
                                    "Unable to flush tablet");
  } else if (op == kFlushAllTabletsOp) {
    CHECK_ARGC_OR_RETURN_WITH_USAGE(op, 2);

    RETURN_NOT_OK_PREPEND_FROM_MAIN(client.FlushTablets(std::string(), false /* is_compaction */),
                                    "Unable to flush all tablets");
  } else if (op == kCompactTabletOp) {
    CHECK_ARGC_OR_RETURN_WITH_USAGE(op, 3);

    string tablet_id = argv[2];
    RETURN_NOT_OK_PREPEND_FROM_MAIN(client.FlushTablets(tablet_id, true /* is_compaction */),
                                    "Unable to compact tablet");
  } else if (op == kCompactAllTabletsOp) {
    CHECK_ARGC_OR_RETURN_WITH_USAGE(op, 2);

    RETURN_NOT_OK_PREPEND_FROM_MAIN(client.FlushTablets(std::string(), true /* is_compaction */),
                                    "Unable to compact all tablets");
  } else if (op == kReloadCertificatesOp) {
    CHECK_ARGC_OR_RETURN_WITH_USAGE(op, 2);

    RETURN_NOT_OK_PREPEND_FROM_MAIN(client.ReloadCertificates(),
                                    "Unable to reload TLS certificates");
  } else if (op == kRemoteBootstrapOp) {
    CHECK_ARGC_OR_RETURN_WITH_USAGE(op, 4);

    string target_server = argv[2];
    string tablet_id = argv[3];
    RETURN_NOT_OK_PREPEND_FROM_MAIN(client.RemoteBootstrap(target_server, tablet_id),
                                    "Unable to run remote bootstrap");
  } else if (op == kListMasterServersOp) {
    CHECK_ARGC_OR_RETURN_WITH_USAGE(op, 2);

    RETURN_NOT_OK_PREPEND_FROM_MAIN(client.ListMasterServers(),
                                    "Unable to list master servers on " + addr);
  } else if (op == kClearAllMetaCachesOnServerOp) {
    CHECK_ARGC_OR_RETURN_WITH_USAGE(op, 2);
    RETURN_NOT_OK_PREPEND_FROM_MAIN(
        client.ClearAllMetaCachesOnServer(),
        "Unable to clear the meta-cache on tablet server with address " + addr);
  } else if (op == kClearUniverseUuidOp) {
    CHECK_ARGC_OR_RETURN_WITH_USAGE(op, 2);

    RETURN_NOT_OK_PREPEND_FROM_MAIN(client.ClearUniverseUuid(),
                                    "Unable to clear universe uuid on " + addr);
  } else if (op == kAcquireObjectLockOp) {
    CHECK_ARGC_OR_RETURN_WITH_USAGE(op, 5);
    RETURN_NOT_OK_PREPEND_FROM_MAIN(client.AcquireObjectLock(argv[2], argv[3], argv[4]),
                                    "Unable to acquire object lock");
  } else if (op == kReleaseObjectLockOp) {
    if (argc < 4) {
      CHECK_ARGC_OR_RETURN_WITH_USAGE(op, 4);
    }
    RETURN_NOT_OK_PREPEND_FROM_MAIN(client.ReleaseObjectLock(argv[2], argv[3], argc, argv),
                                    "Unable to release object lock");
  } else if (op == kReleaseAllLocksForSessionOp) {
    CHECK_ARGC_OR_RETURN_WITH_USAGE(op, 3);
    RETURN_NOT_OK_PREPEND_FROM_MAIN(client.ReleaseAllLocksForSession(argv[2]),
                                    "Unale to release all locks for given session");
  } else {
    std::cerr << "Invalid operation: " << op << std::endl;
    google::ShowUsageWithFlagsRestrict(argv[0], __FILE__);
    return 2;
  }

  return 0;
}

} // namespace tools
} // namespace yb

int main(int argc, char** argv) {
  return yb::tools::TsCliMain(argc, argv);
}
