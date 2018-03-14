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

#include "yb/tserver/tablet_service.h"

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include <boost/scope_exit.hpp>

#include "yb/common/schema.h"
#include "yb/common/wire_protocol.h"
#include "yb/consensus/consensus.h"
#include "yb/consensus/leader_lease.h"

#include "yb/docdb/doc_operation.h"
#include "yb/docdb/doc_rowwise_iterator.h"

#include "yb/gutil/bind.h"
#include "yb/gutil/casts.h"
#include "yb/gutil/stl_util.h"
#include "yb/gutil/stringprintf.h"
#include "yb/gutil/strings/escaping.h"
#include "yb/server/hybrid_clock.h"
#include "yb/tablet/tablet_bootstrap_if.h"
#include "yb/tserver/remote_bootstrap_service.h"

#include "yb/tablet/abstract_tablet.h"
#include "yb/tablet/metadata.pb.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metrics.h"

#include "yb/tablet/operations/alter_schema_operation.h"
#include "yb/tablet/operations/truncate_operation.h"
#include "yb/tablet/operations/update_txn_operation.h"
#include "yb/tablet/operations/write_operation.h"

#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/tserver/tserver.pb.h"
#include "yb/util/crc.h"
#include "yb/util/debug/trace_event.h"
#include "yb/util/faststring.h"
#include "yb/util/flag_tags.h"
#include "yb/util/mem_tracker.h"
#include "yb/util/monotime.h"
#include "yb/util/size_literals.h"
#include "yb/util/status.h"
#include "yb/util/status_callback.h"
#include "yb/util/trace.h"
#include "yb/consensus/consensus.pb.h"
#include "yb/tserver/service_util.h"

using namespace std::literals;

DEFINE_int32(scanner_default_batch_size_bytes, 64 * 1024,
             "The default size for batches of scan results");
TAG_FLAG(scanner_default_batch_size_bytes, advanced);
TAG_FLAG(scanner_default_batch_size_bytes, runtime);

DEFINE_int32(scanner_max_batch_size_bytes, 8 * 1024 * 1024,
             "The maximum batch size that a client may request for "
             "scan results.");
TAG_FLAG(scanner_max_batch_size_bytes, advanced);
TAG_FLAG(scanner_max_batch_size_bytes, runtime);

DEFINE_int32(scanner_batch_size_rows, 100,
             "The number of rows to batch for servicing scan requests.");
TAG_FLAG(scanner_batch_size_rows, advanced);
TAG_FLAG(scanner_batch_size_rows, runtime);

DEFINE_bool(parallelize_read_ops, true,
             "Controls weather multiple (Redis) read ops that are present in a operation "
                 "should be executed in parallel.");
TAG_FLAG(parallelize_read_ops, advanced);
TAG_FLAG(parallelize_read_ops, runtime);

// Fault injection flags.
DEFINE_int32(scanner_inject_latency_on_each_batch_ms, 0,
             "If set, the scanner will pause the specified number of milliesconds "
             "before reading each batch of data on the tablet server. "
             "Used for tests.");
TAG_FLAG(scanner_inject_latency_on_each_batch_ms, unsafe);

DECLARE_int32(memory_limit_warn_threshold_percentage);

DEFINE_int32(max_wait_for_safe_time_ms, 5000,
             "Maximum time in milliseconds to wait for the safe time to advance when trying to "
             "scan at the given hybrid_time.");

DEFINE_bool(tserver_noop_read_write, false, "Respond NOOP to read/write.");
TAG_FLAG(tserver_noop_read_write, unsafe);
TAG_FLAG(tserver_noop_read_write, hidden);

DECLARE_uint64(max_clock_skew_usec);

namespace yb {
namespace tserver {

using consensus::ChangeConfigRequestPB;
using consensus::ChangeConfigResponsePB;
using consensus::CONSENSUS_CONFIG_ACTIVE;
using consensus::CONSENSUS_CONFIG_COMMITTED;
using consensus::Consensus;
using consensus::ConsensusConfigType;
using consensus::ConsensusRequestPB;
using consensus::ConsensusResponsePB;
using consensus::GetLastOpIdRequestPB;
using consensus::GetNodeInstanceRequestPB;
using consensus::GetNodeInstanceResponsePB;
using consensus::LeaderStepDownRequestPB;
using consensus::LeaderStepDownResponsePB;
using consensus::LeaderLeaseStatus;
using consensus::RunLeaderElectionRequestPB;
using consensus::RunLeaderElectionResponsePB;
using consensus::StartRemoteBootstrapRequestPB;
using consensus::StartRemoteBootstrapResponsePB;
using consensus::VoteRequestPB;
using consensus::VoteResponsePB;
using consensus::RaftPeerPB;

using std::unique_ptr;
using google::protobuf::RepeatedPtrField;
using rpc::RpcContext;
using std::shared_ptr;
using std::vector;
using std::string;
using strings::Substitute;
using tablet::AlterSchemaOperationState;
using tablet::Tablet;
using tablet::TabletPeer;
using tablet::TabletPeerPtr;
using tablet::TabletStatusPB;
using tablet::TruncateOperationState;
using tablet::OperationCompletionCallback;
using tablet::WriteOperationState;

namespace {

template<class RespClass>
bool GetConsensusOrRespond(const TabletPeerPtr& tablet_peer,
                           RespClass* resp,
                           rpc::RpcContext* context,
                           scoped_refptr<Consensus>* consensus) {
  *consensus = tablet_peer->shared_consensus();
  if (!*consensus) {
    Status s = STATUS(ServiceUnavailable, "Consensus unavailable. Tablet not running");
    SetupErrorAndRespond(resp->mutable_error(), s,
                         TabletServerErrorPB::TABLET_NOT_RUNNING, context);
    return false;
  }
  return true;
}

Status GetTabletRef(const TabletPeerPtr& tablet_peer,
                    shared_ptr<Tablet>* tablet,
                    TabletServerErrorPB::Code* error_code) {
  *DCHECK_NOTNULL(tablet) = tablet_peer->shared_tablet();
  if (PREDICT_FALSE(!*tablet)) {
    *error_code = TabletServerErrorPB::TABLET_NOT_RUNNING;
    return STATUS(IllegalState, "Tablet is not running");
  }
  return Status::OK();
}

} // namespace

// Prepares modification operation, checks limits, fetches tablet_peer and tablet etc.
template<class Req, class Resp>
bool TabletServiceImpl::PrepareModify(
    const Req& req,
    Resp* resp,
    rpc::RpcContext* context,
    tablet::TabletPeerPtr* tablet_peer,
    tablet::TabletPtr* tablet) {
  UpdateClock(req, server_->Clock());

  if (!LookupTabletPeerOrRespond(
      server_->tablet_manager(), req.tablet_id(), resp, context, tablet_peer)) {
    return false;
  }

  TabletServerErrorPB::Code error_code;
  Status s = GetTabletRef(*tablet_peer, tablet, &error_code);
  if (PREDICT_FALSE(!s.ok())) {
    SetupErrorAndRespond(resp->mutable_error(), s, error_code, context);
    return false;
  }

  TRACE("Found Tablet");
  // Check for memory pressure; don't bother doing any additional work if we've
  // exceeded the limit.
  double capacity_pct;
  if ((*tablet)->mem_tracker()->AnySoftLimitExceeded(&capacity_pct)) {
    (*tablet)->metrics()->leader_memory_pressure_rejections->Increment();
    string msg = StringPrintf(
        "Soft memory limit exceeded (at %.2f%% of capacity)",
        capacity_pct);
    if (capacity_pct >= FLAGS_memory_limit_warn_threshold_percentage) {
      YB_LOG_EVERY_N_SECS(WARNING, 1) << "Rejecting Write request: " << msg << THROTTLE_MSG;
    } else {
      YB_LOG_EVERY_N_SECS(INFO, 1) << "Rejecting Write request: " << msg << THROTTLE_MSG;
    }
    SetupErrorAndRespond(resp->mutable_error(), STATUS(ServiceUnavailable, msg),
                         TabletServerErrorPB::UNKNOWN_ERROR,
                         context);
    return false;
  }

  return true;
}

typedef ListTabletsResponsePB::StatusAndSchemaPB StatusAndSchemaPB;

void SetupErrorAndRespond(TabletServerErrorPB* error,
                          const Status& s,
                          TabletServerErrorPB::Code code,
                          rpc::RpcContext* context) {
  // Generic "service unavailable" errors will cause the client to retry later.
  if (code == TabletServerErrorPB::UNKNOWN_ERROR && s.IsServiceUnavailable()) {
    context->RespondRpcFailure(rpc::ErrorStatusPB::ERROR_SERVER_TOO_BUSY, s);
    return;
  }

  StatusToPB(s, error->mutable_status());
  error->set_code(code);
  // TODO: rename RespondSuccess() to just "Respond" or
  // "SendResponse" since we use it for application-level error
  // responses, and this just looks confusing!
  context->RespondSuccess();
}

class WriteOperationCompletionCallback : public OperationCompletionCallback {
 public:
  WriteOperationCompletionCallback(
      std::shared_ptr<rpc::RpcContext> context,
      WriteResponsePB* response,
      tablet::WriteOperationState* state,
      const server::ClockPtr& clock,
      bool trace = false)
      : context_(std::move(context)), response_(response), state_(state), clock_(clock),
        include_trace_(trace) {}

  void OperationCompleted() override {
    if (!status_.ok()) {
      SetupErrorAndRespond(get_error(), status_, code_, context_.get());
    } else {
      // Retrieve the rowblocks returned from the QL write operations and return them as RPC
      // sidecars. Populate the row schema also.
      for (const auto& ql_write_op : *state_->ql_write_ops()) {
        const auto& ql_write_req = ql_write_op->request();
        auto* ql_write_resp = ql_write_op->response();
        const QLRowBlock* rowblock = ql_write_op->rowblock();
        RETURN_UNKNOWN_ERROR_IF_NOT_OK(
            SchemaToColumnPBs(rowblock->schema(), ql_write_resp->mutable_column_schemas()),
            response_, context_.get());
        faststring rows_data;
        rowblock->Serialize(ql_write_req.client(), &rows_data);
        int rows_data_sidecar_idx = 0;
        RETURN_UNKNOWN_ERROR_IF_NOT_OK(
            context_->AddRpcSidecar(RefCntBuffer(rows_data), &rows_data_sidecar_idx),
            response_,
            context_.get());
        ql_write_resp->set_rows_data_sidecar(rows_data_sidecar_idx);
      }
      if (include_trace_ && Trace::CurrentTrace() != nullptr) {
        response_->set_trace_buffer(Trace::CurrentTrace()->DumpToString(true));
      }
      response_->set_propagated_hybrid_time(clock_->Now().ToUint64());
      context_->RespondSuccess();
    }
  }

 private:

  TabletServerErrorPB* get_error() {
    return response_->mutable_error();
  }

  const std::shared_ptr<rpc::RpcContext> context_;
  WriteResponsePB* const response_;
  tablet::WriteOperationState* const state_;
  server::ClockPtr clock_;
  const bool include_trace_;
};

// Checksums the scan result.
class ScanResultChecksummer {
 public:
  ScanResultChecksummer() {}

  void HandleRow(const Schema& schema, const QLTableRow& row) {
    QLValue value;
    buffer_.clear();
    for (uint32_t col_index = 0; col_index != schema.num_columns(); ++col_index) {
      auto status = row.GetValue(schema.column_id(col_index), &value);
      if (!status.ok()) {
        LOG(WARNING) << "Column " << schema.column_id(col_index)
                     << " not found in " << row.ToString();
        continue;
      }
      buffer_.append(pointer_cast<const char*>(&col_index), sizeof(col_index));
      if (schema.column(col_index).is_nullable()) {
        uint8_t defined = value.IsNull() ? 0 : 1;
        buffer_.append(pointer_cast<const char*>(&defined), sizeof(defined));
      }
      if (!value.IsNull()) {
        value.value().AppendToString(&buffer_);
      }
    }
    crc_->Compute(buffer_.c_str(), buffer_.size(), &agg_checksum_, nullptr);
  }

  // Accessors for initializing / setting the checksum.
  uint64_t agg_checksum() const { return agg_checksum_; }

 private:
  crc::Crc* const crc_ = crc::GetCrc32cInstance();
  uint64_t agg_checksum_ = 0;
  std::string buffer_;
};

TabletServiceImpl::TabletServiceImpl(TabletServerIf* server)
    : TabletServerServiceIf(server->MetricEnt()),
      server_(server) {
}

TabletServiceAdminImpl::TabletServiceAdminImpl(TabletServer* server)
    : TabletServerAdminServiceIf(server->MetricEnt()),
      server_(server) {
}

void TabletServiceAdminImpl::AlterSchema(const AlterSchemaRequestPB* req,
                                         AlterSchemaResponsePB* resp,
                                         rpc::RpcContext context) {
  if (!CheckUuidMatchOrRespond(server_->tablet_manager(), "AlterSchema", req, resp, &context)) {
    return;
  }
  DVLOG(3) << "Received Alter Schema RPC: " << req->DebugString();

  server::UpdateClock(*req, server_->Clock());

  TabletPeerPtr tablet_peer;
  if (!LookupTabletPeerOrRespond(server_->tablet_manager(), req->tablet_id(), resp, &context,
                                 &tablet_peer)) {
    return;
  }

  uint32_t schema_version = tablet_peer->tablet_metadata()->schema_version();

  // If the schema was already applied, respond as succeeded
  if (schema_version == req->schema_version()) {
    // Sanity check, to verify that the tablet should have the same schema
    // specified in the request.
    Schema req_schema;
    Status s = SchemaFromPB(req->schema(), &req_schema);
    if (!s.ok()) {
      SetupErrorAndRespond(resp->mutable_error(), s,
                           TabletServerErrorPB::INVALID_SCHEMA, &context);
      return;
    }

    Schema tablet_schema = tablet_peer->tablet_metadata()->schema();
    if (req_schema.Equals(tablet_schema)) {
      context.RespondSuccess();
      return;
    }

    schema_version = tablet_peer->tablet_metadata()->schema_version();
    if (schema_version == req->schema_version()) {
      LOG(ERROR) << "The current schema does not match the request schema."
                 << " version=" << schema_version
                 << " current-schema=" << tablet_schema.ToString()
                 << " request-schema=" << req_schema.ToString()
                 << " (corruption)";
      SetupErrorAndRespond(resp->mutable_error(),
                           STATUS(Corruption, "got a different schema for the same version number"),
                           TabletServerErrorPB::MISMATCHED_SCHEMA, &context);
      return;
    }
  }

  // If the current schema is newer than the one in the request reject the request.
  if (schema_version > req->schema_version()) {
    SetupErrorAndRespond(resp->mutable_error(),
                         STATUS(InvalidArgument, "Tablet has a newer schema"),
                         TabletServerErrorPB::TABLET_HAS_A_NEWER_SCHEMA, &context);
    return;
  }

  auto operation_state = std::make_unique<AlterSchemaOperationState>(
      tablet_peer->tablet(), tablet_peer->log(), req);

  operation_state->set_completion_callback(
      MakeRpcOperationCompletionCallback(std::move(context), resp, server_->Clock()));

  // Submit the alter schema op. The RPC will be responded to asynchronously.
  tablet_peer->Submit(std::make_unique<tablet::AlterSchemaOperation>(
      std::move(operation_state), consensus::LEADER));
}

void TabletServiceImpl::UpdateTransaction(const UpdateTransactionRequestPB* req,
                                          UpdateTransactionResponsePB* resp,
                                          rpc::RpcContext context) {
  TRACE("UpdateTransaction");

  tablet::TabletPeerPtr tablet_peer;
  tablet::TabletPtr tablet;
  if (!PrepareModify(*req, resp, &context, &tablet_peer, &tablet)) {
    return;
  }

  VLOG(1) << "UpdateTransaction: " << req->ShortDebugString();

  TabletServerErrorPB::Code error_code;
  auto status = CheckPeerIsLeader(*tablet_peer, &error_code);
  if (!status.ok()) {
    SetupErrorAndRespond(resp->mutable_error(), status, error_code, &context);
    return;
  }

  auto state = std::make_unique<tablet::UpdateTxnOperationState>(tablet_peer->tablet(),
                                                                 &req->state());
  state->set_completion_callback(MakeRpcOperationCompletionCallback(
      std::move(context), resp, server_->Clock()));

  tablet_peer->tablet()->transaction_coordinator()->Handle(std::move(state));
}

void TabletServiceImpl::GetTransactionStatus(const GetTransactionStatusRequestPB* req,
                                             GetTransactionStatusResponsePB* resp,
                                             rpc::RpcContext context) {
  TRACE("GetTransactionStatus");

  UpdateClock(*req, server_->Clock());

  tablet::TabletPeerPtr tablet_peer;
  if (!LookupTabletPeerOrRespond(server_->tablet_manager(),
                                 req->tablet_id(),
                                 resp,
                                 &context,
                                 &tablet_peer)) {
    return;
  }

  auto status = tablet_peer->tablet()->transaction_coordinator()->GetStatus(
      req->transaction_id(), resp);
  resp->set_propagated_hybrid_time(server_->Clock()->Now().ToUint64());
  if (status.ok()) {
    context.RespondSuccess();
  } else {
    SetupErrorAndRespond(
        resp->mutable_error(), status, TabletServerErrorPB::UNKNOWN_ERROR, &context);
  }
}

void TabletServiceImpl::AbortTransaction(const AbortTransactionRequestPB* req,
                                         AbortTransactionResponsePB* resp,
                                         rpc::RpcContext context) {
  TRACE("AbortTransaction");

  UpdateClock(*req, server_->Clock());

  tablet::TabletPeerPtr tablet_peer;
  if (!LookupTabletPeerOrRespond(server_->tablet_manager(),
                                 req->tablet_id(),
                                 resp,
                                 &context,
                                 &tablet_peer)) {
    return;
  }

  server::ClockPtr clock(server_->Clock());
  auto context_ptr = std::make_shared<rpc::RpcContext>(std::move(context));
  tablet_peer->tablet()->transaction_coordinator()->Abort(
      req->transaction_id(),
      [resp, context_ptr, clock](Result<TransactionStatusResult> result) {
        resp->set_propagated_hybrid_time(clock->Now().ToUint64());
        if (result.ok()) {
          resp->set_status(result->status);
          if (result->status_time.is_valid()) {
            resp->set_status_hybrid_time(result->status_time.ToUint64());
          }
          context_ptr->RespondSuccess();
        } else {
          SetupErrorAndRespond(resp->mutable_error(),
                               result.status(),
                               TabletServerErrorPB::UNKNOWN_ERROR,
                               context_ptr.get());
        }
      });
}

void TabletServiceImpl::Truncate(const TruncateRequestPB* req,
                                 TruncateResponsePB* resp,
                                 rpc::RpcContext context) {
  TRACE("Truncate");

  UpdateClock(*req, server_->Clock());

  TabletPeerPtr tablet_peer;
  if (!LookupTabletPeerOrRespond(server_->tablet_manager(),
                                 req->tablet_id(),
                                 resp, &context,
                                 &tablet_peer)) {
    return;
  }

  auto tx_state = std::make_unique<TruncateOperationState>(tablet_peer->tablet(), req);

  tx_state->set_completion_callback(
      MakeRpcOperationCompletionCallback(std::move(context), resp, server_->Clock()));

  // Submit the truncate tablet op. The RPC will be responded to asynchronously.
  tablet_peer->Submit(
      std::make_unique<tablet::TruncateOperation>(std::move(tx_state), consensus::LEADER));
}

void TabletServiceAdminImpl::CreateTablet(const CreateTabletRequestPB* req,
                                          CreateTabletResponsePB* resp,
                                          rpc::RpcContext context) {
  if (!CheckUuidMatchOrRespond(server_->tablet_manager(), "CreateTablet", req, resp, &context)) {
    return;
  }
  TRACE_EVENT1("tserver", "CreateTablet",
               "tablet_id", req->tablet_id());

  Schema schema;
  Status s = SchemaFromPB(req->schema(), &schema);
  DCHECK(schema.has_column_ids());
  if (!s.ok()) {
    SetupErrorAndRespond(resp->mutable_error(),
                         STATUS(InvalidArgument, "Invalid Schema."),
                         TabletServerErrorPB::INVALID_SCHEMA, &context);
    return;
  }

  PartitionSchema partition_schema;
  s = PartitionSchema::FromPB(req->partition_schema(), schema, &partition_schema);
  if (!s.ok()) {
    SetupErrorAndRespond(resp->mutable_error(),
                         STATUS(InvalidArgument, "Invalid PartitionSchema."),
                         TabletServerErrorPB::INVALID_SCHEMA, &context);
    return;
  }

  Partition partition;
  Partition::FromPB(req->partition(), &partition);

  LOG(INFO) << "Processing CreateTablet for tablet " << req->tablet_id()
            << " (table=" << req->table_name()
            << " [id=" << req->table_id() << "]), partition="
            << partition_schema.PartitionDebugString(partition, schema);
  VLOG(1) << "Full request: " << req->DebugString();

  s = server_->tablet_manager()->CreateNewTablet(req->table_id(), req->tablet_id(), partition,
      req->table_name(), req->table_type(), schema, partition_schema, req->config(), nullptr);
  if (PREDICT_FALSE(!s.ok())) {
    TabletServerErrorPB::Code code;
    if (s.IsAlreadyPresent()) {
      code = TabletServerErrorPB::TABLET_ALREADY_EXISTS;
    } else {
      code = TabletServerErrorPB::UNKNOWN_ERROR;
    }
    SetupErrorAndRespond(resp->mutable_error(), s, code, &context);
    return;
  }
  context.RespondSuccess();
}

void TabletServiceAdminImpl::DeleteTablet(const DeleteTabletRequestPB* req,
                                          DeleteTabletResponsePB* resp,
                                          rpc::RpcContext context) {
  if (!CheckUuidMatchOrRespond(server_->tablet_manager(), "DeleteTablet", req, resp, &context)) {
    return;
  }
  TRACE_EVENT2("tserver", "DeleteTablet",
               "tablet_id", req->tablet_id(),
               "reason", req->reason());

  tablet::TabletDataState delete_type = tablet::TABLET_DATA_UNKNOWN;
  if (req->has_delete_type()) {
    delete_type = req->delete_type();
  }
  LOG(INFO) << "Processing DeleteTablet for tablet " << req->tablet_id()
            << " with delete_type " << TabletDataState_Name(delete_type)
            << (req->has_reason() ? (" (" + req->reason() + ")") : "")
            << " from " << context.requestor_string();
  VLOG(1) << "Full request: " << req->DebugString();

  boost::optional<int64_t> cas_config_opid_index_less_or_equal;
  if (req->has_cas_config_opid_index_less_or_equal()) {
    cas_config_opid_index_less_or_equal = req->cas_config_opid_index_less_or_equal();
  }
  boost::optional<TabletServerErrorPB::Code> error_code;
  Status s = server_->tablet_manager()->DeleteTablet(req->tablet_id(),
                                                     delete_type,
                                                     cas_config_opid_index_less_or_equal,
                                                     &error_code);
  if (PREDICT_FALSE(!s.ok())) {
    HandleErrorResponse(resp, &context, s, error_code);
    return;
  }
  context.RespondSuccess();
}

// TODO(sagnik): Modify this to actually create a copartitioned table
void TabletServiceAdminImpl::CopartitionTable(const CopartitionTableRequestPB* req,
                                              CopartitionTableResponsePB* resp,
                                              rpc::RpcContext context) {
  context.RespondSuccess();
  LOG(INFO) << "tserver doesn't support co-partitioning yet";
}

void TabletServiceAdminImpl::FlushTablets(const FlushTabletsRequestPB* req,
                                          FlushTabletsResponsePB* resp,
                                          rpc::RpcContext context) {
  if (!CheckUuidMatchOrRespond(server_->tablet_manager(), "FlushTablets", req, resp, &context)) {
    return;
  }

  if (req->tablet_ids_size() == 0) {
    const Status s = STATUS(InvalidArgument, "No tablet ids");
    SetupErrorAndRespond(
        resp->mutable_error(), s, TabletServerErrorPB_Code_UNKNOWN_ERROR, &context);
    return;
  }

  server::UpdateClock(*req, server_->Clock());

  TRACE_EVENT1("tserver", "FlushTablets",
               "TS: ", req->dest_uuid());

  LOG(INFO) << "Processing FlushTablets from " << context.requestor_string();
  VLOG(1) << "Full request: " << req->DebugString();
  TabletPeers tablet_peers;

  for (const TabletId& id : req->tablet_ids()) {
    resp->set_failed_tablet_id(id);

    TabletPeerPtr tablet_peer;
    if (!LookupTabletPeerOrRespond(server_->tablet_manager(), id, resp, &context, &tablet_peer)) {
      return;
    }

    RETURN_UNKNOWN_ERROR_IF_NOT_OK(
        tablet_peer->tablet()->Flush(tablet::FlushMode::kAsync), resp, &context);

    tablet_peers.push_back(tablet_peer);
    resp->clear_failed_tablet_id();
  }

  // Wait for end of all flush operations.
  for (const TabletPeerPtr& tablet_peer : tablet_peers) {
    resp->set_failed_tablet_id(tablet_peer->tablet()->tablet_id());
    RETURN_UNKNOWN_ERROR_IF_NOT_OK(tablet_peer->tablet()->WaitForFlush(), resp, &context);
    resp->clear_failed_tablet_id();
  }

  context.RespondSuccess();
}

void TabletServiceImpl::Write(const WriteRequestPB* req,
                              WriteResponsePB* resp,
                              rpc::RpcContext context) {
  if (FLAGS_tserver_noop_read_write) {
    for (int i = 0; i < req->ql_write_batch_size(); ++i) {
      resp->add_ql_response_batch();
    }
    context.RespondSuccess();
    return;
  }
  TRACE("Start Write");
  TRACE_EVENT1("tserver", "TabletServiceImpl::Write",
               "tablet_id", req->tablet_id());
  DVLOG(3) << "Received Write RPC: " << req->DebugString();

  tablet::TabletPeerPtr tablet_peer;
  tablet::TabletPtr tablet;
  if (!PrepareModify(*req, resp, &context, &tablet_peer, &tablet)) {
    return;
  }

  if (req->has_write_batch() && req->write_batch().has_transaction()) {
    VLOG(1) << "Write with transaction: " << req->write_batch().transaction().ShortDebugString();
  }

  if (PREDICT_FALSE(req->has_write_batch() && !req->write_batch().kv_pairs().empty())) {
    Status s = STATUS(NotSupported, "Write Request contains write batch. This field should be "
        "used only for post-processed write requests during "
        "Raft replication.");
    SetupErrorAndRespond(resp->mutable_error(), s,
                         TabletServerErrorPB::INVALID_MUTATION,
                         &context);
    return;
  }

  bool has_operations = req->ql_write_batch_size() != 0 || req->redis_write_batch_size() != 0;
  if (!has_operations && tablet->table_type() != TableType::REDIS_TABLE_TYPE) {
    // An empty request. This is fine, can just exit early with ok status instead of working hard.
    // This doesn't need to go to Raft log.
    RpcOperationCompletionCallback<WriteResponsePB> callback(
        std::move(context), resp, server_->Clock());
    callback.OperationCompleted();
    return;
  }

  auto operation_state = std::make_unique<WriteOperationState>(tablet_peer->tablet(), req, resp);

  auto context_ptr = std::make_shared<RpcContext>(std::move(context));
  operation_state->set_completion_callback(
      std::make_unique<WriteOperationCompletionCallback>(
          context_ptr, resp, operation_state.get(), server_->Clock(), req->include_trace()));

  auto status = tablet_peer->SubmitWrite(std::move(operation_state));

  // Check that we could submit the write
  RETURN_UNKNOWN_ERROR_IF_NOT_OK(status, resp, context_ptr.get());
}

Status TabletServiceImpl::CheckPeerIsReady(const TabletPeer& tablet_peer,
                                           TabletServerErrorPB::Code* error_code) {
  scoped_refptr<consensus::Consensus> consensus = tablet_peer.shared_consensus();
  if (!consensus) {
    *error_code = TabletServerErrorPB::TABLET_NOT_RUNNING;
    return STATUS_SUBSTITUTE(IllegalState,
                             "Consensus not available for tablet $0.", tablet_peer.tablet_id());
  }

  Status s = tablet_peer.CheckRunning();
  if (!s.ok()) {
    *error_code = TabletServerErrorPB::TABLET_NOT_RUNNING;
    return s;
  }
  return Status::OK();
}

Status TabletServiceImpl::CheckPeerIsLeader(const TabletPeer& tablet_peer,
                                            TabletServerErrorPB::Code* error_code) {
  scoped_refptr<consensus::Consensus> consensus = tablet_peer.shared_consensus();
  const Consensus::LeaderStatus leader_status = consensus->leader_status();

  VLOG(1) << "Check for " << Format(
      "tablet $0 peer $1. Peer role is $2. Leader status is $3.",
      tablet_peer.tablet_id(), tablet_peer.permanent_uuid(),
      consensus->role(), static_cast<int>(leader_status));

  switch (leader_status) {
    case Consensus::LeaderStatus::NOT_LEADER:
      *error_code = TabletServerErrorPB::NOT_THE_LEADER;
      return STATUS(IllegalState, "Not the leader");

    case Consensus::LeaderStatus::LEADER_BUT_NOT_READY:
      *error_code = TabletServerErrorPB::LEADER_NOT_READY_TO_SERVE;
      return STATUS(ServiceUnavailable, "Leader is not ready");

    case Consensus::LeaderStatus::LEADER_AND_READY:
      return Status::OK();
  }
  FATAL_INVALID_ENUM_VALUE(consensus::Consensus::LeaderStatus, leader_status);
}

Status TabletServiceImpl::CheckPeerIsLeaderAndReady(const TabletPeer& tablet_peer,
                                                    TabletServerErrorPB::Code* error_code) {
  RETURN_NOT_OK(CheckPeerIsReady(tablet_peer, error_code));

  return CheckPeerIsLeader(tablet_peer, error_code);
}

bool TabletServiceImpl::GetTabletOrRespond(const ReadRequestPB* req,
                                           ReadResponsePB* resp,
                                           rpc::RpcContext* context,
                                           std::shared_ptr<tablet::AbstractTablet>* tablet) {
  return DoGetTabletOrRespond(req, resp, context, tablet);
}

template <class Req, class Resp>
bool TabletServiceImpl::DoGetTabletOrRespond(const Req* req, Resp* resp, rpc::RpcContext* context,
                                             std::shared_ptr<tablet::AbstractTablet>* tablet) {
  TabletPeerPtr tablet_peer;
  if (!LookupTabletPeerOrRespond(server_->tablet_manager(), req->tablet_id(), resp, context,
                                 &tablet_peer)) {
    return false;
  }

  TabletServerErrorPB::Code error_code;
  Status s = CheckPeerIsReady(*tablet_peer.get(), &error_code);
  if (PREDICT_FALSE(!s.ok())) {
    SetupErrorAndRespond(resp->mutable_error(), s, error_code, context);
    return false;
  }

  // Check for leader only in strong consistency level.
  if (req->consistency_level() == YBConsistencyLevel::STRONG) {
    s = CheckPeerIsLeader(*tablet_peer.get(), &error_code);
    if (PREDICT_FALSE(!s.ok())) {
      SetupErrorAndRespond(resp->mutable_error(), s, error_code, context);
      return false;
    }
  }

  shared_ptr<tablet::Tablet> ptr;
  s = GetTabletRef(tablet_peer, &ptr, &error_code);
  if (PREDICT_FALSE(!s.ok())) {
    SetupErrorAndRespond(resp->mutable_error(), s, error_code, context);
    return false;
  }
  *tablet = ptr;
  return true;
}

void TabletServiceImpl::Read(const ReadRequestPB* req,
                             ReadResponsePB* resp,
                             rpc::RpcContext context) {
  if (FLAGS_tserver_noop_read_write) {
    context.RespondSuccess();
    return;
  }
  TRACE("Start Read");
  TRACE_EVENT1("tserver", "TabletServiceImpl::Read",
               "tablet_id", req->tablet_id());
  DVLOG(3) << "Received Read RPC: " << req->DebugString();

  shared_ptr<tablet::AbstractTablet> tablet;
  if (!GetTabletOrRespond(req, resp, &context, &tablet)) {
    return;
  }

  // safe_ht_to_read is used only for read restart, so if read_time is valid, then we would respond
  // with "restart required".
  HybridTime safe_ht_to_read;
  auto read_time = ReadHybridTime::FromReadTimePB(*req);
  bool allow_retry = !read_time;
  tablet::RequireLease require_lease(req->consistency_level() == YBConsistencyLevel::STRONG);
  bool transactional = tablet->SchemaRef().table_properties().is_transactional();
  if (!read_time) {
    safe_ht_to_read = tablet->SafeTime(require_lease);
    // If the read time is not specified, then it is non transactional read.
    // So we should restart it in server in case of failure.
    read_time.read = safe_ht_to_read;
    if (transactional) {
      read_time.local_limit = server_->Clock()->Now().AddMicroseconds(FLAGS_max_clock_skew_usec);
      read_time.global_limit = read_time.local_limit;

      VLOG(1) << "Read time: " << read_time.ToString();
    } else {
      read_time.local_limit = read_time.read;
      read_time.global_limit = read_time.read;
    }
  } else {
    safe_ht_to_read = tablet->SafeTime(require_lease, read_time.read, context.GetClientDeadline());
    if (!safe_ht_to_read.is_valid()) { // Timed out
      SetupErrorAndRespond(resp->mutable_error(), STATUS(TimedOut, ""),
                           TabletServerErrorPB::UNKNOWN_ERROR, &context);
      return;
    }
  }

  if (transactional) {
    // Serial number is used for check whether this operation was initiated before
    // transaction status request. So we should initialize it as soon as possible.
    read_time.serial_no =
        down_cast<tablet::Tablet*>(tablet.get())->transaction_participant()->RegisterRequest();
  }

  const auto& remote_address = context.remote_address();
  HostPortPB host_port_pb;
  host_port_pb.set_host(remote_address.address().to_string());
  host_port_pb.set_port(remote_address.port());

  for (;;) {
    resp->Clear();
    context.ResetRpcSidecars();
    auto result = DoRead(tablet.get(), req, read_time, safe_ht_to_read, require_lease,
                         &host_port_pb, resp, &context);
    if (!result.ok()) {
      SetupErrorAndRespond(
          resp->mutable_error(), result.status(), TabletServerErrorPB::UNKNOWN_ERROR, &context);
      return;
    }
    read_time = *result;
    // If read was successful, then restart time is invalid. Finishing.
    if (!read_time) {
      break;
    }
    if (!allow_retry) {
      // The read time is specified, than we read as part of transaction. So we should restart
      // whole transaction. In this case we report restart time and abort reading.
      resp->Clear();
      auto restart_read_time = resp->mutable_restart_read_time();
      restart_read_time->set_read_ht(read_time.read.ToUint64());
      restart_read_time->set_local_limit_ht(read_time.local_limit.ToUint64());
      // Global limit is ignored by caller, so we don't set it.
      break;
    }
  }
  if (req->include_trace() && Trace::CurrentTrace() != nullptr) {
    resp->set_trace_buffer(Trace::CurrentTrace()->DumpToString(true));
  }
  RpcOperationCompletionCallback<ReadResponsePB> callback(
      std::move(context), resp, server_->Clock());
  callback.OperationCompleted();
  TRACE("Done Read");
}

void HandleRedisReadRequestAsync(
    tablet::AbstractTablet* tablet,
    const ReadHybridTime& read_time,
    const RedisReadRequestPB& redis_read_request,
    RedisResponsePB* response,
    const std::function<void(const Status& s)>& status_cb
) {
  status_cb(tablet->HandleRedisReadRequest(read_time, redis_read_request, response));
}

Result<ReadHybridTime> TabletServiceImpl::DoRead(tablet::AbstractTablet* tablet,
                                                 const ReadRequestPB* req,
                                                 ReadHybridTime read_time,
                                                 HybridTime safe_ht_to_read,
                                                 tablet::RequireLease require_lease,
                                                 HostPortPB* host_port_pb,
                                                 ReadResponsePB* resp,
                                                 rpc::RpcContext* context) {
  tablet::ScopedReadOperation read_tx(tablet, require_lease, read_time);
  switch (tablet->table_type()) {
    case TableType::REDIS_TABLE_TYPE: {
      size_t count = req->redis_batch_size();
      std::vector<Status> rets(count);
      CountDownLatch latch(count);
      for (int idx = 0; idx < count; idx++) {
        const RedisReadRequestPB& redis_read_req = req->redis_batch(idx);
        Status &failed_status_ = rets[idx];
        auto cb = [&latch, &failed_status_] (const Status &status) -> void {
          if (!status.ok())
            failed_status_ = status;
          latch.CountDown(1);
        };
        auto func = Bind(&HandleRedisReadRequestAsync,
                  Unretained(tablet),
                  read_tx.read_time(),
                  redis_read_req,
                  Unretained(resp->add_redis_batch()),
                  cb);

        Status s;
        bool run_async = FLAGS_parallelize_read_ops && (idx != count - 1);
        if (run_async) {
          s = server_->tablet_manager()->read_pool()->SubmitClosure(func);
        }

        if (!s.ok() || !run_async) {
          func.Run();
        }
      }
      latch.Wait();
      std::vector<Status> failed;
      for (auto& status : rets) {
        if (!status.ok()) {
          failed.push_back(status);
        }
      }
      if (failed.size() == 0) {
        // TODO(dtxn) implement read restart for Redis.
        return ReadHybridTime();
      } else if (failed.size() == 1) {
        return failed[0];
      } else {
        return STATUS(Combined, VectorToString(failed));
      }
    }
    case TableType::YQL_TABLE_TYPE: {
      ReadRequestPB* mutable_req = const_cast<ReadRequestPB*>(req);
      for (QLReadRequestPB& ql_read_req : *mutable_req->mutable_ql_batch()) {
        // Update the remote endpoint.
        ql_read_req.set_allocated_remote_endpoint(host_port_pb);
        BOOST_SCOPE_EXIT(&ql_read_req) {
          ql_read_req.release_remote_endpoint();
        } BOOST_SCOPE_EXIT_END;

        tablet::QLReadRequestResult result;
        TRACE("Start HandleQLReadRequest");
        RETURN_NOT_OK(tablet->HandleQLReadRequest(
            read_tx.read_time(), ql_read_req, req->transaction(), &result));
        TRACE("Done HandleQLReadRequest");
        if (result.restart_read_ht.is_valid()) {
          DCHECK_GT(result.restart_read_ht, read_time.read);
          VLOG(1) << "Restart read required at: " << result.restart_read_ht
                  << ", original: " << read_time;
          read_time.read = result.restart_read_ht;
          read_time.local_limit = safe_ht_to_read;
          return read_time;
        }
        int rows_data_sidecar_idx = 0;
        RETURN_NOT_OK(context->AddRpcSidecar(
            RefCntBuffer(result.rows_data), &rows_data_sidecar_idx));
        result.response.set_rows_data_sidecar(rows_data_sidecar_idx);
        resp->add_ql_batch()->Swap(&result.response);
      }
      return ReadHybridTime();
    }
  }
  FATAL_INVALID_ENUM_VALUE(TableType, tablet->table_type());
}

ConsensusServiceImpl::ConsensusServiceImpl(const scoped_refptr<MetricEntity>& metric_entity,
                                           TabletPeerLookupIf* tablet_manager)
    : ConsensusServiceIf(metric_entity),
      tablet_manager_(tablet_manager) {
}

ConsensusServiceImpl::~ConsensusServiceImpl() {
}

void ConsensusServiceImpl::UpdateConsensus(const ConsensusRequestPB* req,
                                           ConsensusResponsePB* resp,
                                           rpc::RpcContext context) {
  DVLOG(3) << "Received Consensus Update RPC: " << req->ShortDebugString();
  if (!CheckUuidMatchOrRespond(tablet_manager_, "UpdateConsensus", req, resp, &context)) {
    return;
  }
  TabletPeerPtr tablet_peer;
  if (!LookupTabletPeerOrRespond(tablet_manager_, req->tablet_id(), resp, &context, &tablet_peer)) {
    return;
  }

  // Submit the update directly to the TabletPeer's Consensus instance.
  scoped_refptr<Consensus> consensus;
  if (!GetConsensusOrRespond(tablet_peer, resp, &context, &consensus)) return;

  // Unfortunately, we have to use const_cast here, because the protobuf-generated interface only
  // gives us a const request, but we need to be able to move messages out of the request for
  // efficiency.
  Status s = consensus->Update(const_cast<ConsensusRequestPB*>(req), resp);
  if (PREDICT_FALSE(!s.ok())) {
    // Clear the response first, since a partially-filled response could
    // result in confusing a caller, or in having missing required fields
    // in embedded optional messages.
    resp->Clear();

    SetupErrorAndRespond(resp->mutable_error(), s,
                         TabletServerErrorPB::UNKNOWN_ERROR,
                         &context);
    return;
  }
  context.RespondSuccess();
}

void ConsensusServiceImpl::RequestConsensusVote(const VoteRequestPB* req,
                                                VoteResponsePB* resp,
                                                rpc::RpcContext context) {
  DVLOG(3) << "Received Consensus Request Vote RPC: " << req->DebugString();
  if (!CheckUuidMatchOrRespond(tablet_manager_, "RequestConsensusVote", req, resp, &context)) {
    return;
  }
  TabletPeerPtr tablet_peer;
  if (!LookupTabletPeerOrRespond(tablet_manager_, req->tablet_id(), resp, &context, &tablet_peer)) {
    return;
  }

  // Submit the vote request directly to the consensus instance.
  scoped_refptr<Consensus> consensus;
  if (!GetConsensusOrRespond(tablet_peer, resp, &context, &consensus)) return;
  Status s = consensus->RequestVote(req, resp);
  RETURN_UNKNOWN_ERROR_IF_NOT_OK(s, resp, &context);
  context.RespondSuccess();
}

void ConsensusServiceImpl::ChangeConfig(const ChangeConfigRequestPB* req,
                                        ChangeConfigResponsePB* resp,
                                        RpcContext context) {
  VLOG(1) << "Received ChangeConfig RPC: " << req->ShortDebugString();
  // If the destination uuid is empty string, it means the client was retrying after a leader
  // stepdown and did not have a chance to update the uuid inside the request.
  // TODO: Note that this can be removed once Java YBClient will reset change config's uuid
  // correctly after leader step down.
  if (req->dest_uuid() != "" &&
      !CheckUuidMatchOrRespond(tablet_manager_, "ChangeConfig", req, resp, &context)) {
    return;
  }
  TabletPeerPtr tablet_peer;
  if (!LookupTabletPeerOrRespond(tablet_manager_, req->tablet_id(), resp, &context,
                                 &tablet_peer)) {
    return;
  }

  scoped_refptr<Consensus> consensus;
  if (!GetConsensusOrRespond(tablet_peer, resp, &context, &consensus)) return;
  boost::optional<TabletServerErrorPB::Code> error_code;
  std::shared_ptr<RpcContext> context_ptr = std::make_shared<RpcContext>(std::move(context));
  Status s = consensus->ChangeConfig(*req, BindHandleResponse(resp, context_ptr), &error_code);
  VLOG(1) << "Sent ChangeConfig req " << req->ShortDebugString() << " to consensus layer.";
  if (PREDICT_FALSE(!s.ok())) {
    HandleErrorResponse(resp, context_ptr.get(), s, error_code);
    return;
  }
  // The success case is handled when the callback fires.
}

void ConsensusServiceImpl::GetNodeInstance(const GetNodeInstanceRequestPB* req,
                                           GetNodeInstanceResponsePB* resp,
                                           rpc::RpcContext context) {
  DVLOG(3) << "Received Get Node Instance RPC: " << req->DebugString();
  resp->mutable_node_instance()->CopyFrom(tablet_manager_->NodeInstance());
  context.RespondSuccess();
}

namespace {

class RpcScope {
 public:
  template<class Req, class Resp>
  RpcScope(TabletPeerLookupIf* tablet_manager,
           const char* method_name,
           const Req* req,
           Resp* resp,
           rpc::RpcContext* context)
      : context_(context) {
    if (!CheckUuidMatchOrRespond(tablet_manager, method_name, req, resp, context)) {
      return;
    }
    TabletPeerPtr tablet_peer;
    if (!LookupTabletPeerOrRespond(tablet_manager, req->tablet_id(), resp, context, &tablet_peer)) {
      return;
    }

    if (!GetConsensusOrRespond(tablet_peer, resp, context, &consensus_)) {
      return;
    }
    responded_ = false;
  }

  ~RpcScope() {
    if (!responded_) {
      context_->RespondSuccess();
    }
  }

  template<class Resp>
  void CheckStatus(const Status& status, Resp* resp) {
    if (!status.ok()) {
      LOG(INFO) << "Status failed: " << status.ToString();
      SetupErrorAndRespond(resp->mutable_error(), status,
                           TabletServerErrorPB::UNKNOWN_ERROR,
                           context_);
      responded_ = true;
    }
  }

  Consensus* operator->() {
    return consensus_.get();
  }

  explicit operator bool() const {
    return !responded_;
  }

 private:
  rpc::RpcContext* context_;
  bool responded_ = true;
  scoped_refptr<Consensus> consensus_;
};

} // namespace

void ConsensusServiceImpl::RunLeaderElection(const RunLeaderElectionRequestPB* req,
                                             RunLeaderElectionResponsePB* resp,
                                             rpc::RpcContext context) {
  DVLOG(3) << "Received Run Leader Election RPC: " << req->DebugString();
  RpcScope scope(tablet_manager_, "RunLeaderElection", req, resp, &context);
  if (!scope) {
    return;
  }
  Status s = scope->StartElection(
      consensus::Consensus::ELECT_EVEN_IF_LEADER_IS_ALIVE,
      req->has_committed_index(),
      req->committed_index(),
      req->has_originator_uuid() ? req->originator_uuid() : std::string(),
      consensus::TEST_SuppressVoteRequest(
          req->has_suppress_vote_request() && req->suppress_vote_request()));
  scope.CheckStatus(s, resp);
}

void ConsensusServiceImpl::LeaderElectionLost(const consensus::LeaderElectionLostRequestPB *req,
                                              consensus::LeaderElectionLostResponsePB *resp,
                                              ::yb::rpc::RpcContext context) {
  LOG(INFO) << "LeaderElectionLost, req: " << req->ShortDebugString();
  RpcScope scope(tablet_manager_, "LeaderElectionLost", req, resp, &context);
  if (!scope) {
    return;
  }
  auto status = scope->ElectionLostByProtege(req->election_lost_by_uuid());
  scope.CheckStatus(status, resp);
  LOG(INFO) << "LeaderElectionLost, outcome: " << (scope ? "success" : "failure") << "req: "
            << req->ShortDebugString();
}

void ConsensusServiceImpl::LeaderStepDown(const LeaderStepDownRequestPB* req,
                                          LeaderStepDownResponsePB* resp,
                                          RpcContext context) {
  LOG(INFO) << "Received Leader stepdown RPC: " << req->ShortDebugString();

  RpcScope scope(tablet_manager_, "LeaderStepDown", req, resp, &context);
  if (!scope) {
    return;
  }
  Status s = scope->StepDown(req, resp);
  LOG(INFO) << "Leader stepdown request " << req->ShortDebugString() << " success. Resp code="
            << TabletServerErrorPB::Code_Name(resp->error().code());
  scope.CheckStatus(s, resp);
}

void ConsensusServiceImpl::GetLastOpId(const consensus::GetLastOpIdRequestPB *req,
                                       consensus::GetLastOpIdResponsePB *resp,
                                       rpc::RpcContext context) {
  DVLOG(3) << "Received GetLastOpId RPC: " << req->DebugString();
  if (!CheckUuidMatchOrRespond(tablet_manager_, "GetLastOpId", req, resp, &context)) {
    return;
  }
  TabletPeerPtr tablet_peer;
  if (!LookupTabletPeerOrRespond(tablet_manager_, req->tablet_id(), resp, &context, &tablet_peer)) {
    return;
  }

  if (tablet_peer->state() != tablet::RUNNING) {
    SetupErrorAndRespond(resp->mutable_error(),
                         STATUS(ServiceUnavailable, "Tablet Peer not in RUNNING state"),
                         TabletServerErrorPB::TABLET_NOT_RUNNING, &context);
    return;
  }
  scoped_refptr<Consensus> consensus;
  if (!GetConsensusOrRespond(tablet_peer, resp, &context, &consensus)) return;
  if (PREDICT_FALSE(req->opid_type() == consensus::UNKNOWN_OPID_TYPE)) {
    HandleErrorResponse(resp, &context,
                        STATUS(InvalidArgument, "Invalid opid_type specified to GetLastOpId()"));
    return;
  }
  Status s = consensus->GetLastOpId(req->opid_type(), resp->mutable_opid());
  RETURN_UNKNOWN_ERROR_IF_NOT_OK(s, resp, &context);
  context.RespondSuccess();
}

void ConsensusServiceImpl::GetConsensusState(const consensus::GetConsensusStateRequestPB *req,
                                             consensus::GetConsensusStateResponsePB *resp,
                                             rpc::RpcContext context) {
  DVLOG(3) << "Received GetConsensusState RPC: " << req->DebugString();

  RpcScope scope(tablet_manager_, "GetConsensusState", req, resp, &context);
  if (!scope) {
    return;
  }
  ConsensusConfigType type = req->type();
  if (PREDICT_FALSE(type != CONSENSUS_CONFIG_ACTIVE && type != CONSENSUS_CONFIG_COMMITTED)) {
    HandleErrorResponse(resp, &context,
        STATUS(InvalidArgument, Substitute("Unsupported ConsensusConfigType $0 ($1)",
                                           ConsensusConfigType_Name(type), type)));
    return;
  }
  LeaderLeaseStatus leader_lease_status;
  *resp->mutable_cstate() = scope->ConsensusState(req->type(), &leader_lease_status);
  resp->set_leader_lease_status(leader_lease_status);
}

void ConsensusServiceImpl::StartRemoteBootstrap(const StartRemoteBootstrapRequestPB* req,
                                                StartRemoteBootstrapResponsePB* resp,
                                                rpc::RpcContext context) {
  if (!CheckUuidMatchOrRespond(tablet_manager_, "StartRemoteBootstrap", req, resp, &context)) {
    return;
  }
  Status s = tablet_manager_->StartRemoteBootstrap(*req);
  LOG_IF(WARNING, !s.ok()) << "Start remote bootstrap failed: " << s;
  RETURN_UNKNOWN_ERROR_IF_NOT_OK(s, resp, &context);
  context.RespondSuccess();
}

void TabletServiceImpl::NoOp(const NoOpRequestPB *req,
                             NoOpResponsePB *resp,
                             rpc::RpcContext context) {
  context.RespondSuccess();
}

void TabletServiceImpl::ListTablets(const ListTabletsRequestPB* req,
                                    ListTabletsResponsePB* resp,
                                    rpc::RpcContext context) {
  TabletPeers peers;
  server_->tablet_manager()->GetTabletPeers(&peers);
  RepeatedPtrField<StatusAndSchemaPB>* peer_status = resp->mutable_status_and_schema();
  for (const TabletPeerPtr& peer : peers) {
    StatusAndSchemaPB* status = peer_status->Add();
    peer->GetTabletStatusPB(status->mutable_tablet_status());
    CHECK_OK(SchemaToPB(peer->status_listener()->schema(),
                        status->mutable_schema()));
    peer->tablet_metadata()->partition_schema().ToPB(status->mutable_partition_schema());
  }
  context.RespondSuccess();
}

void TabletServiceImpl::GetLogLocation(
    const GetLogLocationRequestPB* req,
    GetLogLocationResponsePB* resp,
    rpc::RpcContext context) {
  resp->set_log_location(FLAGS_log_dir);
  context.RespondSuccess();
}

void TabletServiceImpl::ListTabletsForTabletServer(const ListTabletsForTabletServerRequestPB* req,
                                                   ListTabletsForTabletServerResponsePB* resp,
                                                   rpc::RpcContext context) {
  // Replicating logic from path-handlers.
  TabletPeers peers;
  server_->tablet_manager()->GetTabletPeers(&peers);
  for (const TabletPeerPtr& peer : peers) {
    TabletStatusPB status;
    peer->GetTabletStatusPB(&status);

    ListTabletsForTabletServerResponsePB::Entry* data_entry = resp->add_entries();
    data_entry->set_table_name(status.table_name());
    data_entry->set_tablet_id(status.tablet_id());

    scoped_refptr<consensus::Consensus> consensus = peer->shared_consensus();
    data_entry->set_is_leader(consensus && consensus->role() == consensus::RaftPeerPB::LEADER);
    data_entry->set_state(status.state());
  }

  context.RespondSuccess();
}

namespace {

Result<uint64_t> CalcChecksum(tablet::Tablet* tablet) {
  const Schema& schema = tablet->metadata()->schema();
  auto client_schema = schema.CopyWithoutColumnIds();
  auto iter = tablet->NewRowIterator(client_schema, boost::none);
  RETURN_NOT_OK(iter);

  QLTableRow value_map;
  ScanResultChecksummer collector;

  while ((**iter).HasNext()) {
    RETURN_NOT_OK((**iter).NextRow(&value_map));
    collector.HandleRow(schema, value_map);
  }

  return collector.agg_checksum();
}

} // namespace

void TabletServiceImpl::Checksum(const ChecksumRequestPB* req,
                                 ChecksumResponsePB* resp,
                                 rpc::RpcContext context) {
  VLOG(1) << "Full request: " << req->DebugString();

  std::shared_ptr<tablet::AbstractTablet> abstract_tablet;
  if (!DoGetTabletOrRespond(req, resp, &context, &abstract_tablet)) {
    return;
  }
  auto checksum = CalcChecksum(down_cast<tablet::Tablet*>(abstract_tablet.get()));
  if (!checksum.ok()) {
    SetupErrorAndRespond(resp->mutable_error(), checksum.status(),
                         TabletServerErrorPB::UNKNOWN_ERROR, &context);
    return;
  }

  resp->set_checksum(*checksum);

  context.RespondSuccess();
}

void TabletServiceImpl::ImportData(const ImportDataRequestPB* req,
                                   ImportDataResponsePB* resp,
                                   rpc::RpcContext context) {
  tablet::TabletPeerPtr peer;
  if (!LookupTabletPeerOrRespond(server_->tablet_manager(), req->tablet_id(), resp, &context,
                                 &peer)) {
    return;
  }
  auto status = peer->tablet()->ImportData(req->source_dir());
  if (!status.ok()) {
    SetupErrorAndRespond(resp->mutable_error(),
                         status,
                         TabletServerErrorPB::UNKNOWN_ERROR,
                         &context);
    return;
  }
  context.RespondSuccess();
}

void TabletServiceImpl::GetTabletStatus(const GetTabletStatusRequestPB* req,
                                        GetTabletStatusResponsePB* resp,
                                        rpc::RpcContext context) {
  VLOG(3) << "GetTabletStatus called for tablet " << req->tablet_id();
  tablet::TabletPeerPtr peer;
  if (!server_->tablet_manager()->LookupTablet(req->tablet_id(), &peer)) {
    TabletServerErrorPB::Code code = TabletServerErrorPB::TABLET_NOT_FOUND;
    auto status = STATUS(NotFound, "Tablet not found", req->tablet_id());
    SetupErrorAndRespond(resp->mutable_error(), status, code, &context);
    return;
  }
  peer->GetTabletStatusPB(resp->mutable_tablet_status());
  context.RespondSuccess();
}

void TabletServiceImpl::Shutdown() {
}

scoped_refptr<Histogram> TabletServer::GetMetricsHistogram(
    TabletServerServiceIf::RpcMetricIndexes metric) {
  // Returns the metric Histogram by holding a lock to make sure tablet_server_service_ remains
  // unchanged during the operation.
  std::lock_guard<simple_spinlock> l(lock_);
  if (tablet_server_service_) {
    return tablet_server_service_->GetMetric(metric).handler_latency;
  }
  return nullptr;
}


}  // namespace tserver
}  // namespace yb
