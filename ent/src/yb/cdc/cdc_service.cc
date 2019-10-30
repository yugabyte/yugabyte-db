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

#include "yb/cdc/cdc_service.h"

#include <shared_mutex>
#include <chrono>
#include <memory>

#include <boost/algorithm/string.hpp>

#include "yb/cdc/cdc_producer.h"
#include "yb/cdc/cdc_service.proxy.h"
#include "yb/cdc/cdc_rpc.h"

#include "yb/common/entity_ids.h"
#include "yb/common/ql_expr.h"
#include "yb/common/ql_value.h"
#include "yb/common/wire_protocol.h"
#include "yb/consensus/raft_consensus.h"
#include "yb/client/table.h"
#include "yb/client/table_alterer.h"
#include "yb/client/table_handle.h"
#include "yb/client/session.h"
#include "yb/client/yb_table_name.h"
#include "yb/client/yb_op.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/tserver/service_util.h"
#include "yb/util/debug/trace_event.h"
#include "yb/util/flag_tags.h"
#include "yb/yql/cql/ql/util/statement_result.h"

DEFINE_int32(cdc_read_rpc_timeout_ms, 30 * 1000,
             "Timeout used for CDC read rpc calls.  Reads normally occur cross-cluster.");
TAG_FLAG(cdc_read_rpc_timeout_ms, advanced);

DEFINE_int32(cdc_write_rpc_timeout_ms, 30 * 1000,
             "Timeout used for CDC write rpc calls.  Writes normally occur intra-cluster.");
TAG_FLAG(cdc_write_rpc_timeout_ms, advanced);

DEFINE_int32(cdc_ybclient_reactor_threads, 50,
             "The number of reactor threads to be used for processing ybclient "
             "requests for CDC.");
TAG_FLAG(cdc_ybclient_reactor_threads, advanced);

DEFINE_int32(cdc_state_checkpoint_update_interval_ms, 15 * 1000,
             "Rate at which CDC state's checkpoint is updated.");

DECLARE_int32(cdc_checkpoint_opid_interval_ms);

namespace yb {
namespace cdc {

using namespace std::literals;

using rpc::RpcContext;
using tserver::TSTabletManager;
using client::internal::RemoteTabletServer;

constexpr int kMaxDurationForTabletLookup = 50;
const client::YBTableName kCdcStateTableName(
    master::kSystemNamespaceName, master::kCdcStateTableName);

const auto kCdcStateCheckpointInterval = FLAGS_cdc_state_checkpoint_update_interval_ms * 1ms;
const auto kCheckpointOpIdInterval = FLAGS_cdc_checkpoint_opid_interval_ms * 1ms;

CDCServiceImpl::CDCServiceImpl(TSTabletManager* tablet_manager,
                               const scoped_refptr<MetricEntity>& metric_entity)
    : CDCServiceIf(metric_entity),
      tablet_manager_(tablet_manager) {
  const auto server = tablet_manager->server();
  async_client_init_.emplace(
      "cdc_client", FLAGS_cdc_ybclient_reactor_threads, FLAGS_cdc_read_rpc_timeout_ms / 1000,
      server->permanent_uuid(), &server->options(), server->metric_entity(), server->mem_tracker(),
      server->messenger());
  async_client_init_->Start();
}

namespace {
bool YsqlTableHasPrimaryKey(const client::YBSchema& schema) {
  for (const auto& col : schema.columns()) {
      if (col.order() == static_cast<int32_t>(PgSystemAttrNum::kYBRowId)) {
        // ybrowid column is added for tables that don't have user-specified primary key.
        return false;
    }
  }
  return true;
}

bool IsTabletPeerLeader(const std::shared_ptr<tablet::TabletPeer>& peer) {
  return peer->LeaderStatus() == consensus::LeaderStatus::LEADER_AND_READY;
}
} // namespace

template <class ReqType, class RespType>
bool CDCServiceImpl::CheckOnline(const ReqType* req, RespType* resp, rpc::RpcContext* rpc) {
  TRACE("Received RPC $0: $1", rpc->ToString(), req->DebugString());
  if (PREDICT_FALSE(!tablet_manager_)) {
    SetupErrorAndRespond(resp->mutable_error(),
                         STATUS(ServiceUnavailable, "Tablet Server is not running"),
                         CDCErrorPB::NOT_RUNNING,
                         rpc);
    return false;
  }
  return true;
}

void CDCServiceImpl::CreateCDCStream(const CreateCDCStreamRequestPB* req,
                                     CreateCDCStreamResponsePB* resp,
                                     RpcContext context) {
  if (!CheckOnline(req, resp, &context)) {
    return;
  }

  RPC_CHECK_AND_RETURN_ERROR(req->has_table_id(),
                             STATUS(InvalidArgument, "Table ID is required to create CDC stream"),
                             resp->mutable_error(),
                             CDCErrorPB::INVALID_REQUEST,
                             context);

  std::shared_ptr<client::YBTable> table;
  Status s = async_client_init_->client()->OpenTable(req->table_id(), &table);
  RPC_STATUS_RETURN_ERROR(s, resp->mutable_error(), CDCErrorPB::TABLE_NOT_FOUND, context);

  // We don't allow CDC on YEDIS and tables without a primary key.
  if (req->record_format() != CDCRecordFormat::WAL) {
    RPC_CHECK_NE_AND_RETURN_ERROR(table->table_type(), client::YBTableType::REDIS_TABLE_TYPE,
                                  STATUS(InvalidArgument, "Cannot setup CDC on YEDIS_TABLE"),
                                  resp->mutable_error(),
                                  CDCErrorPB::INVALID_REQUEST,
                                  context);

    // Check if YSQL table has a primary key. CQL tables always have a user specified primary key.
    RPC_CHECK_AND_RETURN_ERROR(
        table->table_type() != client::YBTableType::PGSQL_TABLE_TYPE ||
        YsqlTableHasPrimaryKey(table->schema()),
        STATUS(InvalidArgument, "Cannot setup CDC on table without primary key"),
        resp->mutable_error(),
        CDCErrorPB::INVALID_REQUEST,
        context);
  }

  std::unordered_map<std::string, std::string> options;
  options.reserve(2);
  options.emplace(kRecordType, CDCRecordType_Name(req->record_type()));
  options.emplace(kRecordFormat, CDCRecordFormat_Name(req->record_format()));

  auto result = async_client_init_->client()->CreateCDCStream(req->table_id(), options);
  RPC_CHECK_AND_RETURN_ERROR(result.ok(), result.status(), resp->mutable_error(),
                             CDCErrorPB::INTERNAL_ERROR, context);

  resp->set_stream_id(*result);

  // Add stream to cache.
  AddStreamMetadataToCache(*result, std::make_shared<StreamMetadata>(req->table_id(),
                                                                     req->record_type(),
                                                                     req->record_format()));
  context.RespondSuccess();
}

void CDCServiceImpl::DeleteCDCStream(const DeleteCDCStreamRequestPB* req,
                                     DeleteCDCStreamResponsePB* resp,
                                     RpcContext context) {
  if (!CheckOnline(req, resp, &context)) {
    return;
  }

  RPC_CHECK_AND_RETURN_ERROR(req->stream_id_size() > 0,
                             STATUS(InvalidArgument, "Stream ID is required to delete CDC stream"),
                             resp->mutable_error(),
                             CDCErrorPB::INVALID_REQUEST,
                             context);

  vector<CDCStreamId> streams(req->stream_id().begin(), req->stream_id().end());
  Status s = async_client_init_->client()->DeleteCDCStream(streams);
  RPC_STATUS_RETURN_ERROR(s, resp->mutable_error(), CDCErrorPB::INTERNAL_ERROR, context);

  context.RespondSuccess();
}

void CDCServiceImpl::ListTablets(const ListTabletsRequestPB* req,
                                 ListTabletsResponsePB* resp,
                                 RpcContext context) {
  if (!CheckOnline(req, resp, &context)) {
    return;
  }

  RPC_CHECK_AND_RETURN_ERROR(req->has_stream_id(),
                             STATUS(InvalidArgument, "Stream ID is required to list tablets"),
                             resp->mutable_error(),
                             CDCErrorPB::INVALID_REQUEST,
                             context);

  auto tablets = GetTablets(req->stream_id());
  RPC_CHECK_AND_RETURN_ERROR(tablets.ok(), tablets.status(), resp->mutable_error(),
                             CDCErrorPB::INTERNAL_ERROR, context);

  if (!req->local_only()) {
    resp->mutable_tablets()->Reserve(tablets->size());
  }

  for (const auto& tablet : *tablets) {
    // Filter local tablets if needed.
    if (req->local_only()) {
      bool is_local = false;
      for (const auto& replica :  tablet.replicas()) {
        if (replica.ts_info().permanent_uuid() == tablet_manager_->server()->permanent_uuid()) {
          is_local = true;
          break;
        }
      }

      if (!is_local) {
        continue;
      }
    }

    auto res = resp->add_tablets();
    res->set_tablet_id(tablet.tablet_id());
    res->mutable_tservers()->Reserve(tablet.replicas_size());
    for (const auto& replica : tablet.replicas()) {
      auto tserver =  res->add_tservers();
      tserver->mutable_broadcast_addresses()->CopyFrom(replica.ts_info().broadcast_addresses());
      if (tserver->broadcast_addresses_size() == 0) {
        LOG(WARNING) << "No public broadcast addresses found for "
                     << replica.ts_info().permanent_uuid() << ".  Using private addresses instead.";
        tserver->mutable_broadcast_addresses()->CopyFrom(replica.ts_info().private_rpc_addresses());
      }
    }
  }

  context.RespondSuccess();
}

Result<google::protobuf::RepeatedPtrField<master::TabletLocationsPB>> CDCServiceImpl::GetTablets(
    const CDCStreamId& stream_id) {
  auto stream_metadata = VERIFY_RESULT(GetStream(stream_id));
  client::YBTableName table_name;
  table_name.set_table_id(stream_metadata->table_id);
  google::protobuf::RepeatedPtrField<master::TabletLocationsPB> tablets;
  RETURN_NOT_OK(async_client_init_->client()->GetTablets(table_name, 0, &tablets));
  return tablets;
}

void CDCServiceImpl::GetChanges(const GetChangesRequestPB* req,
                                GetChangesResponsePB* resp,
                                RpcContext context) {
  if (!CheckOnline(req, resp, &context)) {
    return;
  }

  RPC_CHECK_AND_RETURN_ERROR(req->has_tablet_id(),
                             STATUS(InvalidArgument, "Tablet ID is required to get CDC changes"),
                             resp->mutable_error(),
                             CDCErrorPB::INVALID_REQUEST,
                             context);
  RPC_CHECK_AND_RETURN_ERROR(req->has_stream_id(),
                             STATUS(InvalidArgument, "Stream ID is required to get CDC changes"),
                             resp->mutable_error(),
                             CDCErrorPB::INVALID_REQUEST,
                             context);

  // Check that requested tablet_id is part of the CDC stream.
  Status s = CheckTabletValidForStream(req->stream_id(), req->tablet_id());
  RPC_STATUS_RETURN_ERROR(s, resp->mutable_error(), CDCErrorPB::INVALID_REQUEST, context);

  std::shared_ptr<tablet::TabletPeer> tablet_peer;
  s = tablet_manager_->GetTabletPeer(req->tablet_id(), &tablet_peer);

  // If we we can't serve this tablet...
  if (s.IsNotFound() || tablet_peer->LeaderStatus() != consensus::LeaderStatus::LEADER_AND_READY) {
    if (req->serve_as_proxy()) {
      // Forward GetChanges() to tablet leader. This commonly happens in Kubernetes setups.
      auto context_ptr = std::make_shared<RpcContext>(std::move(context));
      TabletLeaderGetChanges(req, resp, context_ptr, tablet_peer);
    // Otherwise, figure out the proper return code.
    } else if (s.IsNotFound()) {
      SetupErrorAndRespond(resp->mutable_error(), s, CDCErrorPB::TABLET_NOT_FOUND, &context);
    } else if (tablet_peer->LeaderStatus() == consensus::LeaderStatus::NOT_LEADER) {
      // TODO: we may be able to get some changes, even if we're not the leader.
      SetupErrorAndRespond(resp->mutable_error(),
          STATUS(NotFound, Format("Not leader for $0", req->tablet_id())),
          CDCErrorPB::TABLET_NOT_FOUND, &context);
    } else {
      SetupErrorAndRespond(resp->mutable_error(),
          STATUS(LeaderNotReadyToServe, "Not ready to serve"),
          CDCErrorPB::LEADER_NOT_READY, &context);
    }
    return;
  }

  ProducerTabletInfo producer_tablet = {"" /* UUID */, req->stream_id(), req->tablet_id()};
  auto session = async_client_init_->client()->NewSession();
  OpIdPB op_id;

  if (req->has_from_checkpoint()) {
    op_id = req->from_checkpoint().op_id();
  } else {
    auto result = GetLastCheckpoint(producer_tablet, session);
    RPC_CHECK_AND_RETURN_ERROR(result.ok(), result.status(), resp->mutable_error(),
                               CDCErrorPB::INTERNAL_ERROR, context);
    op_id = *result;
  }

  auto record = GetStream(req->stream_id());
  RPC_CHECK_AND_RETURN_ERROR(record.ok(), record.status(), resp->mutable_error(),
                             CDCErrorPB::INTERNAL_ERROR, context);

  CDCProducer cdc_producer;
  s = cdc_producer.GetChanges(req->stream_id(), req->tablet_id(), op_id, *record->get(),
                              tablet_peer, resp);
  RPC_STATUS_RETURN_ERROR(
      s,
      resp->mutable_error(),
      s.IsNotFound() ? CDCErrorPB::CHECKPOINT_TOO_OLD : CDCErrorPB::UNKNOWN_ERROR,
      context);

  s = UpdateCheckpoint(
      producer_tablet, resp->checkpoint().op_id(),
      req->has_from_checkpoint() ? req->from_checkpoint().op_id() : consensus::MinimumOpId(),
      session);
  RPC_STATUS_RETURN_ERROR(s, resp->mutable_error(), CDCErrorPB::INTERNAL_ERROR, context);

  tablet_peer->consensus()->UpdateCDCConsumerOpId(GetMinSentCheckpointForTablet(req->tablet_id()));

  context.RespondSuccess();
}

Result<RemoteTabletServer *> CDCServiceImpl::GetLeaderTServer(const TabletId& tablet_id) {
  std::promise<Result<client::internal::RemoteTabletPtr>> tablet_lookup_promise;
  auto future = tablet_lookup_promise.get_future();
  auto callback = [&tablet_lookup_promise](
      const Result<client::internal::RemoteTabletPtr>& result) {
    tablet_lookup_promise.set_value(result);
  };

  auto start = CoarseMonoClock::Now();
  async_client_init_->client()->LookupTabletById(
      tablet_id,
      CoarseMonoClock::Now() + MonoDelta::FromMilliseconds(FLAGS_cdc_read_rpc_timeout_ms),
      callback, client::UseCache::kTrue);
  future.wait();

  auto duration = CoarseMonoClock::Now() - start;
  if (duration > (kMaxDurationForTabletLookup * 1ms)) {
    LOG(WARNING) << "LookupTabletByKey took long time: " << duration << " ms";
  }

  auto result = VERIFY_RESULT(future.get());

  auto ts = result->LeaderTServer();
  if (ts == nullptr) {
    return STATUS(NotFound, "Tablet leader not found for tablet", tablet_id);
  }
  return ts;
}

std::shared_ptr<CDCServiceProxy> CDCServiceImpl::GetCDCServiceProxy(RemoteTabletServer* ts) {
  auto hostport = HostPortFromPB(DesiredHostPort(
      ts->public_rpc_hostports(), ts->private_rpc_hostports(), ts->cloud_info(),
      async_client_init_->client()->cloud_info()));
  DCHECK(!hostport.host().empty());

  {
    std::shared_lock<decltype(lock_)> l(lock_);
    auto it = cdc_service_map_.find(hostport);
    if (it != cdc_service_map_.end()) {
      return it->second;
    }
  }

  auto cdc_service = std::make_shared<CDCServiceProxy>(&async_client_init_->client()->proxy_cache(),
                                                       hostport);
  {
    std::lock_guard<decltype(lock_)> l(lock_);
    cdc_service_map_.emplace(hostport, cdc_service);
  }
  return cdc_service;
}

void CDCServiceImpl::TabletLeaderGetChanges(const GetChangesRequestPB* req,
                                            GetChangesResponsePB* resp,
                                            std::shared_ptr<RpcContext> context,
                                            std::shared_ptr<tablet::TabletPeer> peer) {
  auto rpc_handle = rpcs_.Prepare();
  RPC_CHECK_AND_RETURN_ERROR(rpc_handle != rpcs_.InvalidHandle(),
      STATUS(Aborted,
          Format("Could not create valid handle for GetChangesCDCRpc: tablet=$0, peer=$1",
                 req->tablet_id(),
                 peer->permanent_uuid())),
      resp->mutable_error(), CDCErrorPB::INTERNAL_ERROR, *context.get());

  GetChangesRequestPB new_req;
  new_req.CopyFrom(*req);
  new_req.set_serve_as_proxy(false);
  CoarseTimePoint deadline = context->GetClientDeadline();
  if (deadline == CoarseTimePoint::max()) { // Not specified by user.
    deadline = CoarseMonoClock::now() + async_client_init_->client()->default_rpc_timeout();
  }
  *rpc_handle = GetChangesCDCRpc(
      context->GetClientDeadline(),
      nullptr, /* RemoteTablet: will get this from 'new_req' */
      async_client_init_->client(),
      &new_req,
      [=] (Status status, GetChangesResponsePB&& new_resp) {
        auto retained = rpcs_.Unregister(rpc_handle);
        *resp = std::move(new_resp);
        RPC_STATUS_RETURN_ERROR(status, resp->mutable_error(), resp->error().code(),
                                *context.get());
        context->RespondSuccess();
      });
  (**rpc_handle).SendRpc();
}

void CDCServiceImpl::TabletLeaderGetCheckpoint(const GetCheckpointRequestPB* req,
                                               GetCheckpointResponsePB* resp,
                                               RpcContext* context,
                                               const std::shared_ptr<tablet::TabletPeer>& peer) {
  auto result = GetLeaderTServer(req->tablet_id());
  RPC_CHECK_AND_RETURN_ERROR(result.ok(), result.status(), resp->mutable_error(),
                             CDCErrorPB::TABLET_NOT_FOUND, *context);

  auto ts_leader = *result;
  // Check that tablet leader identified by master is not current tablet peer.
  // This can happen during tablet rebalance if master and tserver have different views of
  // leader. We need to avoid self-looping in this case.
  if (peer) {
    RPC_CHECK_NE_AND_RETURN_ERROR(ts_leader->permanent_uuid(), peer->permanent_uuid(),
                                  STATUS(IllegalState,
                                         Format("Tablet leader changed: leader=$0, peer=$1",
                                                ts_leader->permanent_uuid(),
                                                peer->permanent_uuid())),
                                  resp->mutable_error(), CDCErrorPB::NOT_LEADER, *context);
  }

  auto cdc_proxy = GetCDCServiceProxy(ts_leader);
  rpc::RpcController rpc;
  rpc.set_timeout(MonoDelta::FromMilliseconds(FLAGS_cdc_read_rpc_timeout_ms));
  // TODO(NIC): Change to GetCheckpointAsync like CDCPoller::DoPoll.
  cdc_proxy->GetCheckpoint(*req, resp, &rpc);
  RPC_STATUS_RETURN_ERROR(rpc.status(), resp->mutable_error(), CDCErrorPB::INTERNAL_ERROR,
                          *context);
  context->RespondSuccess();
}

void CDCServiceImpl::GetCheckpoint(const GetCheckpointRequestPB* req,
                                   GetCheckpointResponsePB* resp,
                                   RpcContext context) {
  if (!CheckOnline(req, resp, &context)) {
    return;
  }

  RPC_CHECK_AND_RETURN_ERROR(req->has_tablet_id(),
                             STATUS(InvalidArgument, "Tablet ID is required to get CDC checkpoint"),
                             resp->mutable_error(),
                             CDCErrorPB::INVALID_REQUEST,
                             context);
  RPC_CHECK_AND_RETURN_ERROR(req->has_stream_id(),
                             STATUS(InvalidArgument, "Stream ID is required to get CDC checkpoint"),
                             resp->mutable_error(),
                             CDCErrorPB::INVALID_REQUEST,
                             context);

  std::shared_ptr<tablet::TabletPeer> tablet_peer;
  Status s = tablet_manager_->GetTabletPeer(req->tablet_id(), &tablet_peer);

  if (s.IsNotFound() || !IsTabletPeerLeader(tablet_peer)) {
    // Forward GetChanges() to tablet leader. This happens often in Kubernetes setups.
    TabletLeaderGetCheckpoint(req, resp, &context, tablet_peer);
    return;
  }

  // Check that requested tablet_id is part of the CDC stream.
  s = CheckTabletValidForStream(req->stream_id(), req->tablet_id());
  RPC_STATUS_RETURN_ERROR(s, resp->mutable_error(), CDCErrorPB::INVALID_REQUEST, context);

  auto session = async_client_init_->client()->NewSession();
  ProducerTabletInfo producer_tablet = {"" /* UUID */, req->stream_id(), req->tablet_id()};

  auto result = GetLastCheckpoint(producer_tablet, session);
  RPC_CHECK_AND_RETURN_ERROR(result.ok(), result.status(), resp->mutable_error(),
                             CDCErrorPB::INTERNAL_ERROR, context);

  resp->mutable_checkpoint()->mutable_op_id()->CopyFrom(*result);
  context.RespondSuccess();
}

void CDCServiceImpl::Shutdown() {
  async_client_init_->Shutdown();
  rpcs_.Shutdown();
}

Result<OpIdPB> CDCServiceImpl::GetLastCheckpoint(
    const ProducerTabletInfo& producer_tablet,
    const std::shared_ptr<client::YBSession>& session) {
  {
    std::shared_lock<decltype(lock_)> l(lock_);
    auto it = tablet_checkpoints_.find(producer_tablet);
    if (it != tablet_checkpoints_.end()) {
      return it->second.cdc_state_checkpoint.op_id;
    }
  }

  client::TableHandle table;
  RETURN_NOT_OK(table.Open(kCdcStateTableName, async_client_init_->client()));

  const auto op = table.NewReadOp();
  auto* const req = op->mutable_request();
  DCHECK(!producer_tablet.stream_id.empty() && !producer_tablet.tablet_id.empty());
  QLAddStringHashValue(req, producer_tablet.stream_id);
  QLAddStringHashValue(req, producer_tablet.tablet_id);
  table.AddColumns({master::kCdcCheckpoint}, req);
  RETURN_NOT_OK(session->ApplyAndFlush(op));

  OpIdPB op_id;
  auto row_block = ql::RowsResult(op.get()).GetRowBlock();
  if (row_block->row_count() == 0) {
    op_id.set_term(0);
    op_id.set_index(0);
    return op_id;
  }

  DCHECK_EQ(row_block->row_count(), 1);
  DCHECK_EQ(row_block->row(0).column(0).type(), InternalType::kStringValue);

  std::vector<std::string> checkpoint;
  checkpoint.reserve(2);
  // Checkpoint is stored in the format "{term}.{index}".
  boost::split(checkpoint, row_block->row(0).column(0).string_value(), boost::is_any_of("."));
  DCHECK_EQ(checkpoint.size(), 2);
  op_id.set_term(boost::lexical_cast<int>(checkpoint[0]));
  op_id.set_index(boost::lexical_cast<int>(checkpoint[1]));
  return op_id;
}

Status CDCServiceImpl::UpdateCheckpoint(const ProducerTabletInfo& producer_tablet,
                                        const OpIdPB& sent_op_id,
                                        const OpIdPB& commit_op_id,
                                        const std::shared_ptr<client::YBSession>& session) {
  bool update_cdc_state = true;
  auto now = CoarseMonoClock::Now();
  TabletCheckpoint sent_checkpoint({sent_op_id, now});
  TabletCheckpoint commit_checkpoint({commit_op_id, now});

  {
    std::lock_guard<decltype(lock_)> l(lock_);
    auto it = tablet_checkpoints_.find(producer_tablet);
    if (it != tablet_checkpoints_.end()) {
      it->second.sent_checkpoint = sent_checkpoint;

      if (commit_op_id.index() > 0) {
        it->second.cdc_state_checkpoint.op_id = commit_op_id;
      }

      // Check if we need to update cdc_state table.
      if (now - it->second.cdc_state_checkpoint.last_update_time <= kCdcStateCheckpointInterval) {
        update_cdc_state = false;
      } else {
        it->second.cdc_state_checkpoint.last_update_time = now;
      }
    } else {
      tablet_checkpoints_[producer_tablet] = {commit_checkpoint, sent_checkpoint};
    }
  }

  if (update_cdc_state) {
    client::TableHandle table;
    RETURN_NOT_OK(table.Open(kCdcStateTableName, async_client_init_->client()));
    const auto op = table.NewUpdateOp();
    auto* const req = op->mutable_request();
    DCHECK(!producer_tablet.stream_id.empty() && !producer_tablet.tablet_id.empty());
    QLAddStringHashValue(req, producer_tablet.stream_id);
    QLAddStringHashValue(req, producer_tablet.tablet_id);
    table.AddStringColumnValue(req, master::kCdcCheckpoint,
                               Format("$0.$1", commit_op_id.term(), commit_op_id.index()));
    RETURN_NOT_OK(session->ApplyAndFlush(op));
  }

  return Status::OK();
}

OpIdPB CDCServiceImpl::GetMinSentCheckpointForTablet(const std::string& tablet_id) {
  OpIdPB min_op_id = consensus::MaximumOpId();
  auto now = CoarseMonoClock::Now();

  std::shared_lock<rw_spinlock> l(lock_);
  auto it = stream_tablets_.right.equal_range(tablet_id);
  if (it.first == it.second) {
    LOG(WARNING) << "Tablet ID not found in stream_tablets map: " << tablet_id;
    return min_op_id;
  }

  for (StreamTabletBiMap::right_const_iterator right = it.first; right != it.second; ++right) {
    ProducerTabletInfo producer_tablet{"" /*UUID*/, right->second, tablet_id};
    auto checkpoint = tablet_checkpoints_.find(producer_tablet);
    if (checkpoint == tablet_checkpoints_.end()) {
      LOG(WARNING) << "Sent Checkpoint not found for tablet_id " << tablet_id
                   << ", stream id " << right->second;
    } else {
      // We don't want to include streams that are not being actively polled.
      // So, if the stream has not been polled in the last x seconds,
      // then we ignore that stream while calculating min op ID.
      if (now - checkpoint->second.sent_checkpoint.last_update_time <= kCheckpointOpIdInterval &&
          checkpoint->second.sent_checkpoint.op_id.index() < min_op_id.index()) {
        min_op_id = checkpoint->second.sent_checkpoint.op_id;
      }
    }
  }
  return min_op_id;
}

Result<std::shared_ptr<StreamMetadata>> CDCServiceImpl::GetStream(const std::string& stream_id) {
  auto stream = GetStreamMetadataFromCache(stream_id);
  if (stream != nullptr) {
    return stream;
  }

  // Look up stream in sys catalog.
  TableId table_id;
  std::unordered_map<std::string, std::string> options;
  RETURN_NOT_OK(async_client_init_->client()->GetCDCStream(stream_id, &table_id, &options));

  auto stream_metadata = std::make_shared<StreamMetadata>();;
  stream_metadata->table_id = table_id;
  for (const auto& option : options) {
    if (option.first == kRecordType) {
      SCHECK(CDCRecordType_Parse(option.second, &stream_metadata->record_type),
             IllegalState, "CDC record type parsing error");
    } else if (option.first == kRecordFormat) {
      SCHECK(CDCRecordFormat_Parse(option.second, &stream_metadata->record_format),
             IllegalState, "CDC record format parsing error");
    } else {
      LOG(WARNING) << "Unsupported CDC option: " << option.first;
    }
  }

  AddStreamMetadataToCache(stream_id, stream_metadata);
  return stream_metadata;
}

void CDCServiceImpl::AddStreamMetadataToCache(const std::string& stream_id,
                                              const std::shared_ptr<StreamMetadata>& metadata) {
  std::lock_guard<decltype(lock_)> l(lock_);
  stream_metadata_.emplace(stream_id, metadata);
}

std::shared_ptr<StreamMetadata> CDCServiceImpl::GetStreamMetadataFromCache(
    const std::string& stream_id) {
  std::shared_lock<decltype(lock_)> l(lock_);
  auto it = stream_metadata_.find(stream_id);
  if (it != stream_metadata_.end()) {
    return it->second;
  } else {
    return nullptr;
  }
}

Status CDCServiceImpl::CheckTabletValidForStream(const std::string& stream_id,
                                                 const std::string& tablet_id) {
  {
    std::shared_lock<rw_spinlock> l(lock_);
    auto it = stream_tablets_.left.equal_range(stream_id);
    if (it.first != it.second) {
      // Found tablets.
      for (StreamTabletBiMap::left_const_iterator left = it.first; left != it.second; ++left) {
        if (left->second == tablet_id) {
          return Status::OK();
        }
      }

      // Did not find matching tablet ID.
      return STATUS(InvalidArgument,
                    Format("Tablet ID $0 is not part of stream ID $1", tablet_id, stream_id));
    }
  }

  auto tablets = VERIFY_RESULT(GetTablets(stream_id));
  bool found = false;
  {
    std::lock_guard<rw_spinlock> l(lock_);
    for (const auto &tablet : tablets) {
      stream_tablets_.insert(stream_tablet_value(stream_id, tablet.tablet_id()));
      if (tablet.tablet_id() == tablet_id) {
        found = true;
      }
    }
  }
  return found ? Status::OK() : STATUS(InvalidArgument,
      Format("Tablet ID $0 is not part of stream ID $1", tablet_id, stream_id));
}

}  // namespace cdc
}  // namespace yb
