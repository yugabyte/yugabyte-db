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

#include <memory>

#include <boost/algorithm/string.hpp>

#include "yb/cdc/cdc_producer.h"
#include "yb/common/entity_ids.h"
#include "yb/common/ql_expr.h"
#include "yb/common/wire_protocol.h"
#include "yb/consensus/raft_consensus.h"
#include "yb/client/table.h"
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

DEFINE_int32(cdc_rpc_timeout_ms, 30 * 1000,
             "Timeout used for CDC->{master,tserver} async rpc calls.");
TAG_FLAG(cdc_rpc_timeout_ms, advanced);

DEFINE_int32(cdc_ybclient_reactor_threads, 50,
             "The number of reactor threads to be used for processing ybclient "
             "requests for CDC.");
TAG_FLAG(cdc_ybclient_reactor_threads, advanced);

// TODO(Rahul): Remove this flag once the master handshake has been landed.
DEFINE_test_flag(bool, mock_get_changes_response_for_consumer_testing, false,
                 "Mock a successful response to consumer before stream id integration is set up.");

namespace yb {
namespace cdc {

using rpc::RpcContext;
using tserver::TSTabletManager;

CDCServiceImpl::CDCServiceImpl(TSTabletManager* tablet_manager,
                               const scoped_refptr<MetricEntity>& metric_entity)
    : CDCServiceIf(metric_entity),
      tablet_manager_(tablet_manager) {
  const auto server = tablet_manager->server();
  async_client_init_.emplace(
      "cdc_client", FLAGS_cdc_ybclient_reactor_threads, FLAGS_cdc_rpc_timeout_ms / 1000,
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

template <class RespType>
Result<std::shared_ptr<tablet::TabletPeer>> CDCServiceImpl::GetLeaderTabletPeer(
    const std::string& tablet_id,
    RespType* resp,
    rpc::RpcContext* rpc) {
  std::shared_ptr<tablet::TabletPeer> peer;
  Status status = tablet_manager_->GetTabletPeer(tablet_id, &peer);
  if (PREDICT_FALSE(!status.ok())) {
    CDCErrorPB::Code code = status.IsNotFound() ?
        CDCErrorPB::TABLET_NOT_FOUND : CDCErrorPB::TABLET_NOT_RUNNING;
    SetupErrorAndRespond(resp->mutable_error(), status, code, rpc);
    return status;
  }

  // Check RUNNING state.
  status = peer->CheckRunning();
  if (PREDICT_FALSE(!status.ok())) {
    Status s = STATUS(IllegalState, "Tablet not RUNNING");
    SetupErrorAndRespond(resp->mutable_error(), s, CDCErrorPB::TABLET_NOT_RUNNING, rpc);
    return s;
  }

  // Check if tablet peer is leader.
  consensus::LeaderStatus leader_status = peer->LeaderStatus();
  if (leader_status != consensus::LeaderStatus::LEADER_AND_READY) {
    // No records to read.
    if (leader_status == consensus::LeaderStatus::NOT_LEADER) {
      // TODO: Change this to provide new leader
    }
    Status s = STATUS(IllegalState, "Tablet Server is not leader", ToCString(leader_status));
    SetupErrorAndRespond(resp->mutable_error(), s, CDCErrorPB::NOT_LEADER, rpc);
    return s;
  }
  return peer;
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
  options.reserve(3);
  options.emplace(kRecordType, CDCRecordType_Name(req->record_type()));
  options.emplace(kRecordFormat, CDCRecordFormat_Name(req->record_format()));
  if (req->has_retention_sec()) {
    options.emplace(kRetentionSec, std::to_string(req->retention_sec()));
  }
  auto result = async_client_init_->client()->CreateCDCStream(req->table_id(), options);
  RPC_CHECK_AND_RETURN_ERROR(result.ok(), result.status(), resp->mutable_error(),
                             CDCErrorPB::INTERNAL_ERROR, context);

  resp->set_stream_id(*result);
  // TODO: Increase retention for WAL.

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

  RPC_CHECK_AND_RETURN_ERROR(req->has_stream_id(),
                             STATUS(InvalidArgument, "Stream ID is required to delete CDC stream"),
                             resp->mutable_error(),
                             CDCErrorPB::INVALID_REQUEST,
                             context);

  Status s = async_client_init_->client()->DeleteCDCStream(req->stream_id());
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
      tserver->mutable_private_rpc_addresses()->CopyFrom(replica.ts_info().private_rpc_addresses());
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

std::shared_ptr<std::unordered_set<std::string>> CDCServiceImpl::GetTabletIdsForStream(
    const CDCStreamId& stream_id) {
  {
    shared_lock<rw_spinlock> l(lock_);
    auto it = stream_tablets_.find(stream_id);
    if (it != stream_tablets_.end()) {
      return it->second;
    }
  }

  auto result = GetTablets(stream_id);
  if (!result.ok()) {
    return nullptr;
  }

  std::unordered_set<std::string> tablets;
  tablets.reserve(result->size());
  for (const auto& tablet : *result) {
    tablets.insert(tablet.tablet_id());
  }

  auto tablets_ptr = std::make_shared<std::unordered_set<std::string>>(std::move(tablets));
  {
    std::lock_guard<rw_spinlock> l(lock_);
    stream_tablets_.emplace(stream_id, tablets_ptr);
  }
  return tablets_ptr;
}

void CDCServiceImpl::GetChanges(const GetChangesRequestPB* req,
                                GetChangesResponsePB* resp,
                                RpcContext context) {
  if (FLAGS_mock_get_changes_response_for_consumer_testing) {
    *resp->mutable_checkpoint()->mutable_op_id() = consensus::MinimumOpId();
    context.RespondSuccess();
    return;
  }

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

  auto tablet_peer = GetLeaderTabletPeer(req->tablet_id(), resp, &context);
  if (!tablet_peer.ok()) {
    return;
  }

  auto session = async_client_init_->client()->NewSession();
  OpIdPB op_id;
  if (req->has_from_checkpoint()) {
    op_id = req->from_checkpoint().op_id();
  } else {
    auto result = GetLastCheckpoint(req->stream_id(), req->tablet_id(), session);
    RPC_CHECK_AND_RETURN_ERROR(result.ok(), result.status(), resp->mutable_error(),
                               CDCErrorPB::INTERNAL_ERROR, context);
    op_id = *result;
  }

  auto record = GetStream(req->stream_id());
  RPC_CHECK_AND_RETURN_ERROR(record.ok(), record.status(), resp->mutable_error(),
                             CDCErrorPB::INTERNAL_ERROR, context);

  CDCProducer cdc_producer;
  s = cdc_producer.GetChanges(req->stream_id(), req->tablet_id(), op_id, *record->get(),
                              *tablet_peer, resp);
  RPC_STATUS_RETURN_ERROR(
      s,
      resp->mutable_error(),
      s.IsNotFound() ? CDCErrorPB::CHECKPOINT_TOO_OLD : CDCErrorPB::UNKNOWN_ERROR,
      context);

  if (req->has_from_checkpoint()) {
    s = UpdateCheckpoint(req->stream_id(), req->tablet_id(), req->from_checkpoint().op_id(),
                         session);
    RPC_STATUS_RETURN_ERROR(s, resp->mutable_error(), CDCErrorPB::INTERNAL_ERROR, context);
  }

  context.RespondSuccess();
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

  // Check that requested tablet_id is part of the CDC stream.
  Status s = CheckTabletValidForStream(req->stream_id(), req->tablet_id());
  RPC_STATUS_RETURN_ERROR(s, resp->mutable_error(), CDCErrorPB::INVALID_REQUEST, context);

  auto session = async_client_init_->client()->NewSession();
  auto result = GetLastCheckpoint(req->stream_id(), req->tablet_id(), session);
  RPC_CHECK_AND_RETURN_ERROR(result.ok(), result.status(), resp->mutable_error(),
                             CDCErrorPB::INTERNAL_ERROR, context);

  resp->mutable_checkpoint()->mutable_op_id()->CopyFrom(*result);
  context.RespondSuccess();
}

void CDCServiceImpl::Shutdown() {
  async_client_init_->Shutdown();
}

Result<OpIdPB> CDCServiceImpl::GetLastCheckpoint(
    const std::string& stream_id,
    const std::string& tablet_id,
    const std::shared_ptr<client::YBSession>& session) {
  {
    boost::shared_lock<rw_spinlock> l(lock_);
    auto it = tablet_checkpoints_.find(tablet_id);
    if (it != tablet_checkpoints_.end()) {
      return it->second;
    }
  }

  // TODO: Cache table handle.
  client::YBTableName table_name(master::kSystemNamespaceName, master::kCdcStateTableName);
  client::TableHandle table;
  RETURN_NOT_OK(table.Open(table_name, async_client_init_->client()));

  const auto op = table.NewReadOp();
  auto* const req = op->mutable_request();
  QLAddStringHashValue(req, stream_id);
  QLAddStringHashValue(req, tablet_id);
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

Status CDCServiceImpl::UpdateCheckpoint(const std::string& stream_id,
                                        const std::string& tablet_id,
                                        const OpIdPB& op_id,
                                        const std::shared_ptr<client::YBSession>& session) {
  client::YBTableName table_name(master::kSystemNamespaceName, master::kCdcStateTableName);
  client::TableHandle table;
  RETURN_NOT_OK(table.Open(table_name, async_client_init_->client()));

  const auto op = table.NewUpdateOp();
  auto* const req = op->mutable_request();
  QLAddStringHashValue(req, stream_id);
  QLAddStringHashValue(req, tablet_id);
  table.AddStringColumnValue(req, master::kCdcCheckpoint,
                             Format("$0.$1", op_id.term(), op_id.index()));
  RETURN_NOT_OK(session->ApplyAndFlush(op));

  {
    std::lock_guard<rw_spinlock> l(lock_);
    tablet_checkpoints_.emplace(tablet_id, op_id);
  }
  return Status::OK();
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
  std::lock_guard<rw_spinlock> l(lock_);
  stream_metadata_.emplace(stream_id, metadata);
}

std::shared_ptr<StreamMetadata> CDCServiceImpl::GetStreamMetadataFromCache(
    const std::string& stream_id) {
  boost::shared_lock<rw_spinlock> l(lock_);
  auto it = stream_metadata_.find(stream_id);
  if (it != stream_metadata_.end()) {
    return it->second;
  } else {
    return nullptr;
  }
}

Status CDCServiceImpl::CheckTabletValidForStream(const std::string& stream_id,
                                                 const std::string& tablet_id) {
  auto tablets = GetTabletIdsForStream(stream_id);
  SCHECK_NE(tablets, nullptr, IllegalState, Format("No tablets found for stream $0", stream_id));
  SCHECK_EQ(tablets->count(tablet_id), 1, InvalidArgument,
            Format("Tablet ID $0 is not part of stream ID $1", tablet_id, stream_id));
  return Status::OK();
}

}  // namespace cdc
}  // namespace yb
