// Copyright (c) YugabyteDB, Inc.
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

#include "yb/cdc/xcluster_producer_bootstrap.h"

#include "yb/cdc/cdc_error.h"
#include "yb/cdc/cdc_service_context.h"
#include "yb/cdc/cdc_state_table.h"
#include "yb/client/meta_cache.h"
#include "yb/client/xcluster_client.h"
#include "yb/consensus/consensus.h"
#include "yb/consensus/log.h"
#include "yb/consensus/log_cache.h"
#include "yb/docdb/ql_rowwise_iterator_interface.h"
#include "yb/dockv/reader_projection.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/util/logging.h"

DEFINE_test_flag(bool, cdc_inject_replication_index_update_failure, false,
    "Injects an error after updating a tablet's replication index entry");

DECLARE_int32(cdc_write_rpc_timeout_ms);

namespace yb {
namespace cdc {

namespace {

Result<bool> IsDataPresent(tablet::TabletPeerPtr tablet_peer) {
  // This is done by calling FetchNext on NewRowIterator to see if there is any row is visible
  // (not deleted). For colocated tablets, each table has to be checked.
  //
  // We cannot rely on existence of data in WAL. Only locally generated data is
  // replicated via xcluster. This is done to prevent infinite replication loop in bidirectional
  // mode. If the data in the WAL was from a prior xcluster stream (xcluster DR cases) then it
  // will not get replicated even if we can read the log. Reading the entire log to determine if
  // any entries are external is can be too expensive. Also the user could have accidentally
  // inserted data but then deleted it before adding the table to replication, which we cannot
  // detect by scanning the WAL.

  const auto tablet = VERIFY_RESULT(tablet_peer->shared_tablet_safe());
  auto table_ids = tablet->metadata()->GetAllColocatedTables();
  const dockv::ReaderProjection empty_projection;

  // We will have multiple tables when this is a colocated table.
  for (const auto& table_id : table_ids) {
    auto iter = VERIFY_RESULT(
        tablet->NewRowIterator(empty_projection, /* read_hybrid_time */ {}, table_id));
    if (VERIFY_RESULT(iter->FetchNext(nullptr))) {
      LOG(INFO) << "Tablet " << tablet_peer->tablet_id() << " has rows in table " << table_id
                << ". Bootstrap is required when setting up xCluster.";
      return true;
    }
  }
  return false;
}

}  // namespace

Result<bool> IsBootstrapRequiredForTablet(
    tablet::TabletPeerPtr tablet_peer, const OpId& min_op_id, const CoarseTimePoint& deadline) {
  if (min_op_id.index < 0) {
    // Bootstrap is needed if there is any data in the tablet.
    return IsDataPresent(tablet_peer);
  }

  auto log = tablet_peer->log();
  const auto latest_opid = log->GetLatestEntryOpId();
  if (min_op_id.index == latest_opid.index) {
    // Consumer has caught up to producer.
    return false;
  }

  OpId next_index = min_op_id;
  next_index.index++;

  int64_t last_readable_opid_index;
  auto consensus = VERIFY_RESULT_OR_SET_CODE(
      tablet_peer->GetConsensus(), CDCError(CDCErrorPB::LEADER_NOT_READY));

  auto log_result = consensus->ReadReplicatedMessagesForCDC(
      next_index, &last_readable_opid_index, deadline, true /* fetch_single_entry */);

  if (!log_result.ok()) {
    if (log_result.status().IsNotFound()) {
      LOG(INFO) << "Couldn't read index " << next_index << " for tablet "
                << tablet_peer->tablet_id() << ": " << log_result.status()
                << ". Re-bootstrap of the xCluster stream is required";
      return true;
    }

    return log_result.status().CloneAndAddErrorCode(CDCError(CDCErrorPB::INTERNAL_ERROR));
  }

  return false;
}

Status XClusterProducerBootstrap::RunBootstrapProducer() {
  LOG_WITH_FUNC(INFO) << "Initializing xCluster Streams";
  RETURN_NOT_OK(CreateAllBootstrapStreams());
  RETURN_NOT_OK(ConstructServerToTabletsMapping());

  LOG_WITH_FUNC(INFO) << "Retrieving Latest OpIDs for each tablet.";
  RETURN_NOT_OK(GetLatestOpIdsFromLocalPeers());
  RETURN_NOT_OK(FetchLatestOpIdsFromRemotePeers());
  RETURN_NOT_OK(VerifyTabletOpIds());

  LOG_WITH_FUNC(INFO) << "Updating OpIDs for Log Retention.";
  SCHECK(
      !FLAGS_TEST_cdc_inject_replication_index_update_failure, InternalError,
      "Simulated error when setting the replication index");
  RETURN_NOT_OK(SetLogRetentionForLocalTabletPeers());
  RETURN_NOT_OK(SetLogRetentionForRemoteTabletPeers());

  LOG_WITH_FUNC(INFO) << "Updating cdc_state table with checkpoints.";
  RETURN_NOT_OK(UpdateCdcStateTableWithCheckpoints());

  if (req_.check_if_bootstrap_required()) {
    resp_->set_bootstrap_required(VERIFY_RESULT(IsBootstrapRequired()));
  }

  PrepareResponse();

  LOG_WITH_FUNC(INFO) << "Finished.";
  return Status::OK();
}

Status XClusterProducerBootstrap::CreateAllBootstrapStreams() {
  if (!req_.xrepl_stream_ids().empty()) {
    SCHECK_EQ(
        req_.xrepl_stream_ids_size(), req_.table_ids_size(), InvalidArgument,
        "xrepl_stream_ids must be empty or have the same number of entries as table_ids");

    for (int i = 0; i < req_.xrepl_stream_ids_size(); i++) {
      auto stream_id = VERIFY_RESULT(xrepl::StreamId::FromString(req_.xrepl_stream_ids(i)));
      bootstrap_ids_and_tables_.emplace_back(stream_id, req_.table_ids(i));
    }
    return Status::OK();
  }

  // Generate bootstrap ids & setup the CDC streams, for use with the XCluster Consumer.
  for (const auto& table_id : req_.table_ids()) {
    // Mark this stream as being bootstrapped, to help in finding dangling streams. Set state to
    // inactive since it will take some time for the streams to be used. When the target cluster is
    // setup it will switch the streams to active state. Transactional flag will also get set at
    // that point.
    // TODO: Turn this into a batch RPC.
    const auto& bootstrap_id = VERIFY_RESULT(
        client::XClusterClient(*cdc_service_->client())
            .CreateXClusterStream(table_id, /* active */ false, StreamModeTransactional::kFalse));
    creation_state_->created_cdc_streams.push_back(bootstrap_id);

    bootstrap_ids_and_tables_.emplace_back(bootstrap_id, table_id);
  }
  return Status::OK();
}

Status XClusterProducerBootstrap::ConstructServerToTabletsMapping() {
  for (const auto& [bootstrap_id, table_id] : bootstrap_ids_and_tables_) {
    google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
    RETURN_NOT_OK(cdc_service_->client()->GetTabletsFromTableId(table_id, 0, &tablets));

    for (const auto& tablet : tablets) {
      const std::string& tablet_id = tablet.tablet_id();
      auto bootstrap_tablet_pair = std::make_pair(bootstrap_id, tablet_id);
      std::shared_ptr<tablet::TabletPeer> tablet_peer;

      // Mark the tablet for rollback.
      creation_state_->producer_entries_modified.push_back(
          {.stream_id = bootstrap_id, .tablet_id = tablet_id});

      // Initially store invalid op_id to catch any missed streams later in VerifyTabletOpIds.
      tablet_op_ids_[bootstrap_tablet_pair] = yb::OpId::Invalid();

      auto remote_tablet = VERIFY_RESULT(cdc_service_->GetRemoteTablet(tablet_id));

      std::vector<client::internal::RemoteTabletServer*> servers;
      remote_tablet->GetRemoteTabletServers(&servers);

      auto ts_leader = remote_tablet->LeaderTServer();
      SCHECK(ts_leader, NotFound, "Tablet leader not found for tablet", tablet_id);
      if (ts_leader->IsLocal()) {
        local_leader_tablets_.emplace_back(tablet_id);
      } else {
        server_to_remote_leader_tablets_[ts_leader->permanent_uuid()].emplace_back(tablet_id);
      }

      // Store remote tablet information so we can do batched rpc calls.
      bool has_any_local_peer = false;
      for (const auto& server : servers) {
        // We modify our log directly. Avoid calling itself through the proxy.
        if (server->IsLocal()) {
          has_any_local_peer = true;
          local_tablets_.emplace(bootstrap_id, tablet_id);
          continue;
        }

        // Save server_id to proxy mapping.
        const std::string server_id = server->permanent_uuid();
        InsertIfNotPresent(&server_to_proxy_, server_id, cdc_service_->GetCDCServiceProxy(server));
        // Add tablet to the tablet list for this server
        server_to_remote_tablets_[server_id].push_back(bootstrap_tablet_pair);
      }
      if (!has_any_local_peer) {
        remote_tablets_to_fetch_opids_for_.emplace(bootstrap_id, tablet_id);
      }
    }
  }
  return Status::OK();
}

const std::string GetErrMsgForUninitializedLocalPeer(
    const TabletId& tablet_id, const std::shared_ptr<yb::tablet::TabletPeer> tablet_peer) {
  return Format(
      "Unable to get the latest entry op id "
      "from peer $0 and tablet $1 because its log object hasn't been initialized.",
      tablet_peer->permanent_uuid(), tablet_id);
}

Status XClusterProducerBootstrap::GetLatestOpIdsFromLocalPeers() {
  HybridTime ht;
  OpId op_id = yb::OpId::Invalid();
  for (const auto& [bootstrap_id, tablet_id] : local_tablets_) {
    // Check if this tablet has local information cached.
    auto tablet_peer = VERIFY_RESULT(cdc_service_context_->GetServingTablet(tablet_id));

    if (!tablet_peer->log_available()) {
      const std::string err_msg = GetErrMsgForUninitializedLocalPeer(tablet_id, tablet_peer);
      LOG(WARNING) << err_msg;
      if (tablet_peer->IsNotLeader()) {
        // We are a peer that has not been fully initialized yet. Fetch from the leader instead.
        remote_tablets_to_fetch_opids_for_.emplace(bootstrap_id, tablet_id);
        continue;
      }
      return STATUS(InternalError, err_msg);
    }

    const auto& res = tablet_peer->GetOpIdAndSafeTimeForXReplBootstrap();
    if (res.ok()) {
      std::tie(op_id, ht) = *res;
    }

    if (!res.ok() || !op_id.is_valid_not_empty()) {
      if (tablet_peer->IsNotLeader()) {
        LOG(WARNING) << GetErrMsgForUninitializedLocalPeer(tablet_id, tablet_peer)
                     << (res.ok() ? "" : res.ToString());
        // We are a peer that has an invalid opid. Fetch from the leader instead.
        remote_tablets_to_fetch_opids_for_.emplace(bootstrap_id, tablet_id);
        continue;
      }

      // If we are the leader and have an invalid opid, exit early.
      return STATUS_FORMAT(
          InternalError, "Could not retrieve op id for tablet $0$1", tablet_id,
          (res.ok() ? "" : ", " + res.ToString()));
    }
    bootstrap_time_.MakeAtLeast(ht);

    // Add (bootstrap_id, tablet_id) to op_id entry.
    tablet_op_ids_[std::make_pair(bootstrap_id, tablet_id)] = op_id;
  }
  return Status::OK();
}

Status XClusterProducerBootstrap::FetchLatestOpIdsFromRemotePeers() {
  if (remote_tablets_to_fetch_opids_for_.empty()) {
    return Status::OK();
  }

  // Fetch and store the leader tservers so we can batch fetch opids from them after.
  ServerToBootstrapTabletPairMap server_to_remote_tablet_leader;
  for (const auto& [bootstrap_id, tablet_id] : remote_tablets_to_fetch_opids_for_) {
    auto ts_leader = VERIFY_RESULT(cdc_service_->GetLeaderTServer(tablet_id));
    const std::string& leader_server_id = ts_leader->permanent_uuid();

    // Add mapping from server to tablet leader.
    server_to_remote_tablet_leader[leader_server_id].push_back(
        std::make_pair(bootstrap_id, tablet_id));

    SCHECK(server_to_proxy_.contains(leader_server_id), TryAgain, "Tablets have moved.");
  }

  // Stores number of async rpc calls that have returned.
  CountDownLatch rpcs_done(server_to_remote_tablet_leader.size());
  // Store references to the rpc and response objects so they don't go out of scope.
  std::vector<std::shared_ptr<rpc::RpcController>> rpcs;
  std::unordered_map<std::string, std::shared_ptr<GetLatestEntryOpIdResponsePB>>
      get_op_id_responses_by_server;

  // Async per server, get the Latest OpID on each tablet leader.
  for (const auto& [server_id, bootstrap_tablet_pairs] : server_to_remote_tablet_leader) {
    auto rpc = std::make_shared<rpc::RpcController>();
    rpcs.push_back(rpc);

    // Add pointers to rpc and response objects to respective in memory data structures.
    GetLatestEntryOpIdRequestPB get_op_id_req;
    for (const auto& [_, tablet_id] : bootstrap_tablet_pairs) {
      get_op_id_req.add_tablet_ids(tablet_id);
    }
    auto get_op_id_resp = std::make_shared<GetLatestEntryOpIdResponsePB>();
    get_op_id_responses_by_server[server_id] = get_op_id_resp;

    auto proxy = server_to_proxy_[server_id];
    // Todo: GetLatestEntryOpId does not seem to enforce this deadline.
    rpc.get()->set_timeout(MonoDelta::FromMilliseconds(FLAGS_cdc_write_rpc_timeout_ms));

    proxy->GetLatestEntryOpIdAsync(
        get_op_id_req, get_op_id_resp.get(), rpc.get(), rpcs_done.CountDownCallback());
  }

  rpcs_done.Wait();

  // Parse responses and update producer_entries_modified and tablet_checkpoints_.
  std::string get_op_id_err_message;
  for (const auto& [server_id, resp] : get_op_id_responses_by_server) {
    const auto get_op_id_resp = resp.get();
    const auto& leader_tablets = server_to_remote_tablet_leader[server_id];

    // Note any errors, but continue processing all RPC results.
    if (get_op_id_resp->has_error()) {
      auto err_message = get_op_id_resp->error().status().message();
      LOG(WARNING) << "Error from " << server_id << ": " << err_message;
      if (get_op_id_err_message.empty()) {
        get_op_id_err_message = err_message;
      }
      continue;
    }

    // Record which tablets we retrieved an op id from & record in local cache.
    for (int i = 0; i < get_op_id_resp->op_ids_size(); i++) {
      const auto& bootstrap_id = leader_tablets.at(i).first;
      const auto& tablet_id = leader_tablets.at(i).second;
      TabletStreamInfo producer_tablet{bootstrap_id, tablet_id};
      OpId op_id = OpId::FromPB(get_op_id_resp->op_ids(i));

      // Add op_id for tablet.
      tablet_op_ids_[leader_tablets.at(i)] = op_id;

      // Update tablet state.
      cdc_service_->AddTabletCheckpoint(op_id, bootstrap_id, tablet_id);
    }

    if (!get_op_id_resp->has_bootstrap_time()) {
      bootstrap_time_ = HybridTime::kMax;
    } else {
      bootstrap_time_.MakeAtLeast(HybridTime(get_op_id_resp->bootstrap_time()));
    }
  }

  if (!get_op_id_err_message.empty()) {
    return STATUS(InternalError, get_op_id_err_message);
  }
  return Status::OK();
}

Status XClusterProducerBootstrap::VerifyTabletOpIds() {
  // Check that all tablets have a valid op id.
  for (const auto& [bootstrap_id_tablet_id_pair, opid] : tablet_op_ids_) {
    const auto& [bootstrap_id, tablet_id] = bootstrap_id_tablet_id_pair;
    LOG(INFO) << "XCluster checkpoint for T " << tablet_id << " S " << bootstrap_id
              << " is: " << opid;
    // Also check for not empty here, since 0.0 has different behaviour (start from beginning of
    // log cache), which we don't want for bootstrapping.
    SCHECK_FORMAT(
        opid.is_valid_not_empty(), InternalError, "Could not retrieve op id for tablet $0",
        tablet_id);
  }
  return Status::OK();
}

Status XClusterProducerBootstrap::SetLogRetentionForLocalTabletPeers() {
  for (const auto& bootstrap_id_tablet_id_pair : local_tablets_) {
    const auto& [bootstrap_id, tablet_id] = bootstrap_id_tablet_id_pair;
    auto tablet_peer = VERIFY_RESULT(cdc_service_context_->GetServingTablet(tablet_id));
    const auto& op_id = tablet_op_ids_[bootstrap_id_tablet_id_pair];

    // Can update directly, no need for a proxy.
    cdc_service_->AddTabletCheckpoint(op_id, bootstrap_id, tablet_id);
    RETURN_NOT_OK(tablet_peer->set_cdc_min_replicated_index(op_id.index));
    VERIFY_RESULT(tablet_peer->GetConsensus())->UpdateCDCConsumerOpId(op_id);
  }
  return Status::OK();
}

Status XClusterProducerBootstrap::SetLogRetentionForRemoteTabletPeers() {
  if (server_to_remote_tablets_.empty()) {
    return Status::OK();
  }

  CountDownLatch rpcs_done(server_to_remote_tablets_.size());
  // Store references to the rpc and response objects so they don't go out of scope.
  std::vector<std::shared_ptr<rpc::RpcController>> rpcs;
  std::vector<std::shared_ptr<UpdateCdcReplicatedIndexResponsePB>> update_index_responses;

  for (const auto& [server_id, bootstrap_tablet_pairs] : server_to_remote_tablets_) {
    UpdateCdcReplicatedIndexRequestPB update_index_req;
    auto update_index_resp = std::make_shared<UpdateCdcReplicatedIndexResponsePB>();
    auto rpc = std::make_shared<rpc::RpcController>();

    // Store pointers to response and rpc object.
    update_index_responses.push_back(update_index_resp);
    rpcs.push_back(rpc);

    for (const auto& bootstrap_id_tablet_id_pair : bootstrap_tablet_pairs) {
      update_index_req.add_tablet_ids(bootstrap_id_tablet_id_pair.second);
      update_index_req.add_replicated_indices(tablet_op_ids_[bootstrap_id_tablet_id_pair].index);
      update_index_req.add_replicated_terms(tablet_op_ids_[bootstrap_id_tablet_id_pair].term);
    }
    update_index_req.set_initial_retention_barrier(true);

    auto proxy = server_to_proxy_[server_id];
    // Todo: UpdateCdcReplicatedIndex does not seem to enforce this deadline.
    rpc.get()->set_timeout(MonoDelta::FromMilliseconds(FLAGS_cdc_write_rpc_timeout_ms));

    proxy->UpdateCdcReplicatedIndexAsync(
        update_index_req, update_index_resp.get(), rpc.get(), rpcs_done.CountDownCallback());
  }

  // Wait for all async calls to finish.
  rpcs_done.Wait();

  // Check all responses for errors.
  for (const auto& update_index_resp : update_index_responses) {
    if (update_index_resp->has_error()) {
      const std::string err_message = update_index_resp->error().status().message();
      LOG_WITH_FUNC(WARNING) << err_message;
      return STATUS(InternalError, err_message);
    }
  }
  return Status::OK();
}

Status XClusterProducerBootstrap::UpdateCdcStateTableWithCheckpoints() {
  // Create CDC state table update ops with all bootstrap id to tablet id pairs.
  std::vector<CDCStateTableEntry> entries_to_insert;
  for (const auto& [bootstrap_id_tablet_id_pair, op_id] : tablet_op_ids_) {
    CDCStateTableEntry entry(bootstrap_id_tablet_id_pair.second, bootstrap_id_tablet_id_pair.first);
    entry.checkpoint = op_id;
    entries_to_insert.emplace_back(std::move(entry));
  }
  RETURN_NOT_OK(cdc_state_table_->UpsertEntries(entries_to_insert));
  return Status::OK();
}

void XClusterProducerBootstrap::PrepareResponse() {
  if (req_.xrepl_stream_ids().empty()) {
    // Update response with bootstrap ids if it was not already provided.
    for (const auto& [bootstrap_id, _] : bootstrap_ids_and_tables_) {
      resp_->add_cdc_bootstrap_ids(bootstrap_id.ToString());
    }
  }

  if (!bootstrap_time_.is_special()) {
    resp_->set_bootstrap_time(bootstrap_time_.ToUint64());
  }
}

Result<bool> XClusterProducerBootstrap::IsBootstrapRequired() {
  // NOTE: There is a rare edge case where the data may be deleted between bootstrap time and now.
  // By itself it will cause the consumer to miss some data for a brief duration during the setup of
  // xCluster. We dont think this is going to be a common pattern, so we ignore it.

  // Check local tablets first. Exit early even if one tablet has data in it.
  for (const auto& tablet_id : local_leader_tablets_) {
    auto tablet_peer = cdc_service_context_->LookupTablet(tablet_id);

    SCHECK(
        tablet_peer && tablet_peer->IsLeaderAndReady(), LeaderNotReadyToServe,
        "Tablet leader is not ready $0", tablet_id);

    if (VERIFY_RESULT(IsDataPresent(tablet_peer))) {
      return true;
    }
  }

  for (const auto& [server_id, _] : server_to_remote_leader_tablets_) {
    SCHECK(server_to_proxy_.contains(server_id), TryAgain, "Tablets have moved.");
  }

  CountDownLatch rpcs_done(server_to_remote_leader_tablets_.size());

  std::vector<std::shared_ptr<rpc::RpcController>> rpcs;
  std::vector<std::shared_ptr<IsBootstrapRequiredResponsePB>> responses;

  for (const auto& [server_id, tablet_list] : server_to_remote_leader_tablets_) {
    IsBootstrapRequiredRequestPB req;
    auto resp = std::make_shared<IsBootstrapRequiredResponsePB>();
    auto rpc = std::make_shared<rpc::RpcController>();

    // Store pointers to response and rpc object.
    responses.push_back(resp);
    rpcs.push_back(rpc);

    req.mutable_tablet_ids()->Reserve(narrow_cast<int>(tablet_list.size()));
    for (const auto& tablet_id : tablet_list) {
      req.add_tablet_ids(tablet_id);
    }

    rpc->set_timeout(MonoDelta::FromMilliseconds(FLAGS_cdc_write_rpc_timeout_ms));

    server_to_proxy_[server_id]->IsBootstrapRequiredAsync(
        req, resp.get(), rpc.get(), rpcs_done.CountDownCallback());
  }

  rpcs_done.Wait();

  for (const auto& resp : responses) {
    if (resp->has_error()) {
      return StatusFromPB(resp->error().status());
    }
    if (resp->bootstrap_required()) {
      return true;
    }
  }
  return false;
}

}  // namespace cdc
}  // namespace yb
