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

#include "yb/common/schema.h"
#include "yb/common/ql_value.h"
#include "yb/common/wire_protocol.h"
#include "yb/consensus/leader_lease.h"
#include "yb/consensus/raft_consensus.h"

#include "yb/docdb/cql_operation.h"
#include "yb/docdb/doc_rowwise_iterator.h"
#include "yb/docdb/pgsql_operation.h"

#include "yb/gutil/bind.h"
#include "yb/gutil/casts.h"
#include "yb/gutil/stl_util.h"
#include "yb/gutil/stringprintf.h"
#include "yb/gutil/strings/escaping.h"
#include "yb/server/hybrid_clock.h"

#include "yb/tablet/tablet_bootstrap_if.h"
#include "yb/tablet/abstract_tablet.h"
#include "yb/tablet/metadata.pb.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metrics.h"

#include "yb/tablet/operations/change_metadata_operation.h"
#include "yb/tablet/operations/truncate_operation.h"
#include "yb/tablet/operations/update_txn_operation.h"
#include "yb/tablet/operations/write_operation.h"

#include "yb/tserver/remote_bootstrap_service.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/tserver/tserver_error.h"
#include "yb/tserver/tserver.pb.h"

#include "yb/util/crc.h"
#include "yb/util/debug/long_operation_tracker.h"
#include "yb/util/debug/trace_event.h"
#include "yb/util/faststring.h"
#include "yb/util/flag_tags.h"
#include "yb/util/math_util.h"
#include "yb/util/mem_tracker.h"
#include "yb/util/monotime.h"
#include "yb/util/random_util.h"
#include "yb/util/scope_exit.h"
#include "yb/util/size_literals.h"
#include "yb/util/status.h"
#include "yb/util/status_callback.h"
#include "yb/util/trace.h"
#include "yb/util/string_util.h"
#include "yb/consensus/consensus.pb.h"
#include "yb/tserver/service_util.h"

#include "yb/yql/pggate/util/pg_doc_data.h"

using namespace std::literals;  // NOLINT

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
DEFINE_test_flag(int32, scanner_inject_latency_on_each_batch_ms, 0,
                 "If set, the scanner will pause the specified number of milliesconds "
                 "before reading each batch of data on the tablet server.");

DECLARE_int32(memory_limit_warn_threshold_percentage);

DEFINE_int32(max_wait_for_safe_time_ms, 5000,
             "Maximum time in milliseconds to wait for the safe time to advance when trying to "
             "scan at the given hybrid_time.");

DEFINE_test_flag(bool, tserver_noop_read_write, false, "Respond NOOP to read/write.");

DEFINE_int32(max_stale_read_bound_time_ms, 0, "If we are allowed to read from followers, "
             "specify the maximum time a follower can be behind by using the last message received "
             "from the leader. If set to zero, a read can be served by a follower regardless of "
             "when was the last time it received a message from the leader.");
TAG_FLAG(max_stale_read_bound_time_ms, evolving);
TAG_FLAG(max_stale_read_bound_time_ms, runtime);

DEFINE_uint64(sst_files_soft_limit, 24,
              "When majority SST files number is greater that this limit, we will start rejecting "
              "part of write requests. The higher the number of SST files, the higher probability "
              "of rejection.");
TAG_FLAG(sst_files_soft_limit, runtime);

DEFINE_uint64(sst_files_hard_limit, 48,
              "When majority SST files number is greater that this limit, we will reject all write "
              "requests.");
TAG_FLAG(sst_files_hard_limit, runtime);

DEFINE_uint64(min_rejection_delay_ms, 100, ".");
TAG_FLAG(min_rejection_delay_ms, runtime);

DEFINE_uint64(max_rejection_delay_ms, 5000, ".");
TAG_FLAG(max_rejection_delay_ms, runtime);

DEFINE_test_flag(int32, TEST_write_rejection_percentage, 0,
                 "Reject specified percentage of writes.");

DEFINE_test_flag(bool, assert_reads_from_follower_rejected_because_of_staleness, false,
                 "If set, we verify that the consistency level is CONSISTENT_PREFIX, and that "
                 "a follower receives the request, but that it gets rejected because it's a stale "
                 "follower");

DEFINE_test_flag(bool, assert_reads_served_by_follower, false, "If set, we verify that the "
                 "consistency level is CONSISTENT_PREFIX, and that this server is not the leader "
                 "for the tablet");

DEFINE_test_flag(bool, simulate_time_out_failures, false, "If true, we will randomly mark replicas "
                 "as failed to simulate time out failures. The periodic refresh of the lookup "
                 "cache will eventually mark them as available");

DEFINE_test_flag(double, respond_write_failed_probability, 0.0,
                 "Probability to respond that write request is failed");

DEFINE_test_flag(bool, rpc_delete_tablet_fail, false, "Should delete tablet RPC fail.");

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
using tablet::ChangeMetadataOperationState;
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
                           shared_ptr<Consensus>* consensus) {
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

// overlimit - we have 2 bounds, value and random score.
// overlimit is calculated as:
// score + (value - lower_bound) / (upper_bound - lower_bound).
// And it will be >= 1.0 when this function is invoked.
template<class Resp>
bool RejectWrite(tablet::TabletPeer* tablet_peer, const std::string& message, double overlimit,
                 Resp* resp, rpc::RpcContext* context) {
  int64_t delay_ms = fit_bounds<int64_t>((overlimit - 1.0) * FLAGS_max_rejection_delay_ms,
                                         FLAGS_min_rejection_delay_ms,
                                         FLAGS_max_rejection_delay_ms);
  auto status = STATUS(ServiceUnavailable, message, TabletServerDelay(delay_ms * 1ms));
  YB_LOG_EVERY_N_SECS(WARNING, 1)
      << "T " << tablet_peer->tablet_id() << " P " << tablet_peer->permanent_uuid()
      << ": Rejecting Write request, " << status << THROTTLE_MSG;
  SetupErrorAndRespond(resp->mutable_error(), status,
                       TabletServerErrorPB::UNKNOWN_ERROR,
                       context);
  return false;
}

} // namespace

template<class Resp>
bool TabletServiceImpl::CheckWriteThrottlingOrRespond(
    double score, tablet::TabletPeer* tablet_peer, Resp* resp, rpc::RpcContext* context) {
  // Check for memory pressure; don't bother doing any additional work if we've
  // exceeded the limit.
  auto tablet = tablet_peer->tablet();
  auto soft_limit_exceeded_result = tablet->mem_tracker()->AnySoftLimitExceeded(score);
  if (soft_limit_exceeded_result.exceeded) {
    tablet->metrics()->leader_memory_pressure_rejections->Increment();
    string msg = StringPrintf(
        "Soft memory limit exceeded (at %.2f%% of capacity), score: %.2f",
        soft_limit_exceeded_result.current_capacity_pct, score);
    if (soft_limit_exceeded_result.current_capacity_pct >=
            FLAGS_memory_limit_warn_threshold_percentage) {
      YB_LOG_EVERY_N_SECS(WARNING, 1) << "Rejecting Write request: " << msg << THROTTLE_MSG;
    } else {
      YB_LOG_EVERY_N_SECS(INFO, 1) << "Rejecting Write request: " << msg << THROTTLE_MSG;
    }
    SetupErrorAndRespond(resp->mutable_error(), STATUS(ServiceUnavailable, msg),
                         TabletServerErrorPB::UNKNOWN_ERROR,
                         context);
    return false;
  }

  const uint64_t num_sst_files = tablet_peer->raft_consensus()->MajorityNumSSTFiles();
  const auto sst_files_soft_limit = FLAGS_sst_files_soft_limit;
  const int64_t sst_files_used_delta = num_sst_files - sst_files_soft_limit;
  if (sst_files_used_delta >= 0) {
    const auto sst_files_hard_limit = FLAGS_sst_files_hard_limit;
    const auto sst_files_full_delta = sst_files_hard_limit - sst_files_soft_limit;
    if (sst_files_used_delta >= sst_files_full_delta * (1 - score)) {
      tablet->metrics()->majority_sst_files_rejections->Increment();
      auto message = Format("SST files limit exceeded $0 against ($1, $2), score: $3",
                            num_sst_files, sst_files_soft_limit, sst_files_hard_limit, score);
      auto overlimit = sst_files_full_delta > 0
          ? score + static_cast<double>(sst_files_used_delta) / sst_files_full_delta
          : 2.0;
      return RejectWrite(tablet_peer, message, overlimit, resp, context);
    }
  }

  if (FLAGS_TEST_write_rejection_percentage != 0 &&
      score >= 1.0 - FLAGS_TEST_write_rejection_percentage * 0.01) {
    auto status = Format("TEST: Write request rejected, desired percentage: $0, score: $1",
                         FLAGS_TEST_write_rejection_percentage, score);
    return RejectWrite(tablet_peer, status, score + FLAGS_TEST_write_rejection_percentage * 0.01,
                       resp, context);
  }

  return true;
}

typedef ListTabletsResponsePB::StatusAndSchemaPB StatusAndSchemaPB;

class WriteOperationCompletionCallback : public OperationCompletionCallback {
 public:
  WriteOperationCompletionCallback(
      tablet::TabletPeerPtr tablet_peer,
      std::shared_ptr<rpc::RpcContext> context,
      WriteResponsePB* response,
      tablet::WriteOperationState* state,
      const server::ClockPtr& clock,
      bool trace = false)
      : tablet_peer_(std::move(tablet_peer)), context_(std::move(context)), response_(response),
        state_(state), clock_(clock), include_trace_(trace) {}

  void OperationCompleted() override {
    // When we don't need to return any data, we could return success on duplicate request.
    if (status_.IsAlreadyPresent() &&
        state_->ql_write_ops()->empty() &&
        state_->pgsql_write_ops()->empty() &&
        state_->request()->redis_write_batch().empty()) {
      status_ = Status::OK();
    }

    if (!status_.ok()) {
      LOG(INFO) << "Write failed: " << status_;
      SetupErrorAndRespond(get_error(), status_, code_, context_.get());
      return;
    }

    // Retrieve the rowblocks returned from the QL write operations and return them as RPC
    // sidecars. Populate the row schema also.
    for (const auto& ql_write_op : *state_->ql_write_ops()) {
      const auto& ql_write_req = ql_write_op->request();
      auto* ql_write_resp = ql_write_op->response();
      const QLRowBlock* rowblock = ql_write_op->rowblock();
      SchemaToColumnPBs(rowblock->schema(), ql_write_resp->mutable_column_schemas());
      faststring rows_data;
      rowblock->Serialize(ql_write_req.client(), &rows_data);
      int rows_data_sidecar_idx = 0;
      RETURN_UNKNOWN_ERROR_IF_NOT_OK(
          context_->AddRpcSidecar(RefCntBuffer(rows_data), &rows_data_sidecar_idx),
          response_, context_.get());
      ql_write_resp->set_rows_data_sidecar(rows_data_sidecar_idx);
    }

    // Retrieve the resultset returned from the PGSQL write operations and return them as RPC
    // sidecars.
    for (const auto& pgsql_write_op : *state_->pgsql_write_ops()) {
      auto* pgsql_write_resp = pgsql_write_op->response();
      const PgsqlResultSet& resultset = pgsql_write_op->resultset();
      if (resultset.rsrow_count() > 0) {
        faststring rows_data;
        RETURN_UNKNOWN_ERROR_IF_NOT_OK(
            pggate::PgDocData::WriteTuples(resultset, &rows_data), response_, context_.get());
        int rows_data_sidecar_idx = 0;
        RETURN_UNKNOWN_ERROR_IF_NOT_OK(
            context_->AddRpcSidecar(RefCntBuffer(rows_data), &rows_data_sidecar_idx),
            response_, context_.get());
        pgsql_write_resp->set_rows_data_sidecar(rows_data_sidecar_idx);
      }
    }

    if (include_trace_ && Trace::CurrentTrace() != nullptr) {
      response_->set_trace_buffer(Trace::CurrentTrace()->DumpToString(true));
    }
    response_->set_propagated_hybrid_time(clock_->Now().ToUint64());
    context_->RespondSuccess();
  }

 private:

  TabletServerErrorPB* get_error() {
    return response_->mutable_error();
  }

  tablet::TabletPeerPtr tablet_peer_;
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

void TabletServiceAdminImpl::AlterSchema(const ChangeMetadataRequestPB* req,
                                         ChangeMetadataResponsePB* resp,
                                         rpc::RpcContext context) {
  if (!CheckUuidMatchOrRespond(server_->tablet_manager(), "ChangeMetadata", req, resp, &context)) {
    return;
  }
  DVLOG(3) << "Received Change Metadata RPC: " << req->DebugString();

  server::UpdateClock(*req, server_->Clock());

  auto tablet = LookupLeaderTabletOrRespond(
      server_->tablet_peer_lookup(), req->tablet_id(), resp, &context);
  if (!tablet) {
    return;
  }

  uint32_t schema_version = tablet.peer->tablet_metadata()->schema_version();

  // If the schema was already applied, respond as succeeded.
  if (!req->has_wal_retention_secs() && schema_version == req->schema_version()) {
    // Sanity check, to verify that the tablet should have the same schema
    // specified in the request.
    Schema req_schema;
    Status s = SchemaFromPB(req->schema(), &req_schema);
    if (!s.ok()) {
      SetupErrorAndRespond(resp->mutable_error(), s,
                           TabletServerErrorPB::INVALID_SCHEMA, &context);
      return;
    }

    Schema tablet_schema = tablet.peer->tablet_metadata()->schema();
    if (req_schema.Equals(tablet_schema)) {
      context.RespondSuccess();
      return;
    }

    schema_version = tablet.peer->tablet_metadata()->schema_version();
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

  auto operation_state = std::make_unique<ChangeMetadataOperationState>(
      tablet.peer->tablet(), tablet.peer->log(), req);

  operation_state->set_completion_callback(
      MakeRpcOperationCompletionCallback(std::move(context), resp, server_->Clock()));

  // Submit the alter schema op. The RPC will be responded to asynchronously.
  tablet.peer->Submit(std::make_unique<tablet::ChangeMetadataOperation>(
      std::move(operation_state)), tablet.leader_term);
}

#define VERIFY_RESULT_OR_RETURN(expr) \
  __extension__ ({ \
    auto&& __result = (expr); \
    if (!__result.ok()) { return; } \
    std::move(*__result); \
  })

void TabletServiceImpl::UpdateTransaction(const UpdateTransactionRequestPB* req,
                                          UpdateTransactionResponsePB* resp,
                                          rpc::RpcContext context) {
  TRACE("UpdateTransaction");

  VLOG(1) << "UpdateTransaction: " << req->ShortDebugString()
          << ", context: " << context.ToString();
  UpdateClock(*req, server_->Clock());

  LeaderTabletPeer tablet;
  if (req->state().status() != CLEANUP) {
    tablet = LookupLeaderTabletOrRespond(
        server_->tablet_peer_lookup(), req->tablet_id(), resp, &context);
  } else {
    tablet.peer = VERIFY_RESULT_OR_RETURN(LookupTabletPeerOrRespond(
        server_->tablet_peer_lookup(), req->tablet_id(), resp, &context));
    tablet.leader_term = OpId::kUnknownTerm;
  }
  if (!tablet) {
    return;
  }

  auto state = std::make_unique<tablet::UpdateTxnOperationState>(tablet.peer->tablet(),
                                                                 &req->state());
  state->set_completion_callback(MakeRpcOperationCompletionCallback(
      std::move(context), resp, server_->Clock()));

  if (req->state().status() == TransactionStatus::APPLYING ||
      req->state().status() == TransactionStatus::CLEANUP) {
    tablet.peer->tablet()->transaction_participant()->Handle(std::move(state), tablet.leader_term);
  } else {
    tablet.peer->tablet()->transaction_coordinator()->Handle(std::move(state), tablet.leader_term);
  }
}

void TabletServiceImpl::GetTransactionStatus(const GetTransactionStatusRequestPB* req,
                                             GetTransactionStatusResponsePB* resp,
                                             rpc::RpcContext context) {
  TRACE("GetTransactionStatus");

  UpdateClock(*req, server_->Clock());

  auto tablet_peer = VERIFY_RESULT_OR_RETURN(LookupTabletPeerOrRespond(
      server_->tablet_peer_lookup(), req->tablet_id(), resp, &context));

  auto status = CheckPeerIsLeaderAndReady(*tablet_peer);
  if (!status.ok()) {
    SetupErrorAndRespond(resp->mutable_error(), status, &context);
    return;
  }

  status = tablet_peer->tablet()->transaction_coordinator()->GetStatus(
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

  auto tablet = LookupLeaderTabletOrRespond(
      server_->tablet_peer_lookup(), req->tablet_id(), resp, &context);
  if (!tablet) {
    return;
  }

  server::ClockPtr clock(server_->Clock());
  auto context_ptr = std::make_shared<rpc::RpcContext>(std::move(context));
  tablet.peer->tablet()->transaction_coordinator()->Abort(
      req->transaction_id(),
      tablet.leader_term,
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

  auto tablet = LookupLeaderTabletOrRespond(
      server_->tablet_peer_lookup(), req->tablet_id(), resp, &context);
  if (!tablet) {
    return;
  }

  auto tx_state = std::make_unique<TruncateOperationState>(tablet.peer->tablet(), req);

  tx_state->set_completion_callback(
      MakeRpcOperationCompletionCallback(std::move(context), resp, server_->Clock()));

  // Submit the truncate tablet op. The RPC will be responded to asynchronously.
  tablet.peer->Submit(
      std::make_unique<tablet::TruncateOperation>(std::move(tx_state)), tablet.leader_term);
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
      req->table_name(), req->table_type(), schema, partition_schema,
      req->has_index_info() ? boost::optional<IndexInfo>(req->index_info()) : boost::none,
      req->config(), /* tablet_peer */ nullptr);
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
  if (PREDICT_FALSE(FLAGS_rpc_delete_tablet_fail)) {
    context.RespondFailure(STATUS(NetworkError, "Simulating network partition for test"));
    return;
  }

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
  LOG(INFO) << "T " << req->tablet_id() << " P " << server_->permanent_uuid()
            << ": Processing DeleteTablet with delete_type " << TabletDataState_Name(delete_type)
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
  VLOG(1) << "Full FlushTablets request: " << req->DebugString();
  TabletPeers tablet_peers;

  for (const TabletId& id : req->tablet_ids()) {
    resp->set_failed_tablet_id(id);

    auto tablet_peer = VERIFY_RESULT_OR_RETURN(LookupTabletPeerOrRespond(
        server_->tablet_peer_lookup(), id, resp, &context));

    auto tablet = tablet_peer->tablet();
    if (req->is_compaction()) {
      tablet->ForceRocksDBCompactInTest();
    } else {
      RETURN_UNKNOWN_ERROR_IF_NOT_OK(tablet->Flush(tablet::FlushMode::kAsync), resp, &context);
    }

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

void TabletServiceAdminImpl::CountIntents(
    const CountIntentsRequestPB* req,
    CountIntentsResponsePB* resp,
    rpc::RpcContext context) {
  auto tablet_peers = server_->tablet_manager()->GetTabletPeers();
  int64_t total_intents = 0;
  // TODO: do this in parallel.
  // TODO: per-tablet intent counts.
  for (const auto& peer : tablet_peers) {
    auto num_intents = peer->tablet()->CountIntents();
    if (!num_intents.ok()) {
      SetupErrorAndRespond(
          resp->mutable_error(), num_intents.status(), TabletServerErrorPB_Code_UNKNOWN_ERROR,
          &context);
      return;
    }
    total_intents += *num_intents;
  }
  resp->set_num_intents(total_intents);
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
  UpdateClock(*req, server_->Clock());

  auto tablet = LookupLeaderTabletOrRespond(
      server_->tablet_peer_lookup(), req->tablet_id(), resp, &context);
  if (!tablet ||
      !CheckWriteThrottlingOrRespond(
          req->rejection_score(), tablet.peer.get(), resp, &context)) {
    return;
  }

#if defined(DUMP_WRITE)
  if (req->has_write_batch() && req->write_batch().has_transaction()) {
    VLOG(1) << "Write with transaction: " << req->write_batch().transaction().ShortDebugString();
    if (req->pgsql_write_batch_size() != 0) {
      auto txn_id = CHECK_RESULT(FullyDecodeTransactionId(
          req->write_batch().transaction().transaction_id()));
      for (const auto& entry : req->pgsql_write_batch()) {
        if (entry.stmt_type() == PgsqlWriteRequestPB::PGSQL_UPDATE) {
          auto key = entry.column_new_values(0).expr().value().int32_value();
          LOG(INFO) << txn_id << " UPDATE: " << key << " = "
                    << entry.column_new_values(1).expr().value().string_value();
        } else if (entry.stmt_type() == PgsqlWriteRequestPB::PGSQL_INSERT) {
          docdb::DocKey doc_key;
          CHECK_OK(doc_key.FullyDecodeFrom(entry.ybctid_column_value().value().binary_value()));
          LOG(INFO) << txn_id << " INSERT: " << doc_key.hashed_group()[0].GetInt32() << " = "
                    << entry.column_values(0).expr().value().string_value();
        } else if (entry.stmt_type() == PgsqlWriteRequestPB::PGSQL_DELETE) {
          LOG(INFO) << txn_id << " DELETE: " << entry.ShortDebugString();
        }
      }
    }
  }
#endif

  if (PREDICT_FALSE(req->has_write_batch() && !req->has_external_hybrid_time() &&
      (!req->write_batch().write_pairs().empty() || !req->write_batch().read_pairs().empty()))) {
    Status s = STATUS(NotSupported, "Write Request contains write batch. This field should be "
        "used only for post-processed write requests during "
        "Raft replication.");
    SetupErrorAndRespond(resp->mutable_error(), s,
                         TabletServerErrorPB::INVALID_MUTATION,
                         &context);
    return;
  }

  bool has_operations = (req->ql_write_batch_size() != 0 ||
                         req->redis_write_batch_size() != 0 ||
                         req->pgsql_write_batch_size() != 0 ||
                         (req->write_batch().write_pairs_size() != 0 &&
                          req->has_external_hybrid_time()));
  if (!has_operations && tablet.peer->tablet()->table_type() != TableType::REDIS_TABLE_TYPE) {
    // An empty request. This is fine, can just exit early with ok status instead of working hard.
    // This doesn't need to go to Raft log.
    RpcOperationCompletionCallback<WriteResponsePB> callback(
        std::move(context), resp, server_->Clock());
    callback.OperationCompleted();
    return;
  }

  // For postgres requests check that the syscatalog version matches.
  if (tablet.peer->tablet()->table_type() == TableType::PGSQL_TABLE_TYPE) {
    for (const auto& pg_req : req->pgsql_write_batch()) {
      if (pg_req.has_ysql_catalog_version() &&
          pg_req.ysql_catalog_version() < server_->ysql_catalog_version()) {
        SetupErrorAndRespond(resp->mutable_error(),
            STATUS_SUBSTITUTE(QLError, "Catalog Version Mismatch: A DDL occurred while processing "
                                       "this query. Try Again."),
            TabletServerErrorPB::MISMATCHED_SCHEMA, &context);
        return;
      }
    }
  }

  auto operation_state = std::make_unique<WriteOperationState>(tablet.peer->tablet(), req, resp);

  auto context_ptr = std::make_shared<RpcContext>(std::move(context));
  if (RandomActWithProbability(GetAtomicFlag(&FLAGS_respond_write_failed_probability))) {
    operation_state->set_completion_callback(nullptr);
    SetupErrorAndRespond(resp->mutable_error(), STATUS(LeaderHasNoLease, "TEST: Random failure"),
                         TabletServerErrorPB::UNKNOWN_ERROR, context_ptr.get());
  } else {
    operation_state->set_completion_callback(
        std::make_unique<WriteOperationCompletionCallback>(
            tablet.peer, context_ptr, resp, operation_state.get(), server_->Clock(),
            req->include_trace()));
  }
  tablet.peer->WriteAsync(
      std::move(operation_state), tablet.leader_term, context_ptr->GetClientDeadline());
}

Status TabletServiceImpl::CheckPeerIsReady(const TabletPeer& tablet_peer) {
  shared_ptr<consensus::Consensus> consensus = tablet_peer.shared_consensus();
  if (!consensus) {
    return STATUS(
        IllegalState, Format("Consensus not available for tablet $0.", tablet_peer.tablet_id()),
        Slice(), TabletServerError(TabletServerErrorPB::TABLET_NOT_RUNNING));
  }

  Status s = tablet_peer.CheckRunning();
  if (!s.ok()) {
    return s.CloneAndAddErrorCode(TabletServerError(TabletServerErrorPB::TABLET_NOT_RUNNING));
  }
  return Status::OK();
}

Status TabletServiceImpl::CheckPeerIsLeader(const TabletPeer& tablet_peer) {
  RETURN_NOT_OK(LeaderTerm(tablet_peer));
  return Status::OK();
}

Status TabletServiceImpl::CheckPeerIsLeaderAndReady(const TabletPeer& tablet_peer) {
  RETURN_NOT_OK(CheckPeerIsReady(tablet_peer));

  return CheckPeerIsLeader(tablet_peer);
}

bool TabletServiceImpl::GetTabletOrRespond(
    const ReadRequestPB* req, ReadResponsePB* resp, rpc::RpcContext* context,
    std::shared_ptr<tablet::AbstractTablet>* tablet, TabletPeerPtr tablet_peer) {
  return DoGetTabletOrRespond(req, resp, context, tablet, tablet_peer);
}

template <class Req, class Resp>
bool TabletServiceImpl::DoGetTabletOrRespond(
    const Req* req, Resp* resp, rpc::RpcContext* context,
    std::shared_ptr<tablet::AbstractTablet>* tablet, TabletPeerPtr tablet_peer) {
  if (tablet_peer) {
    DCHECK_EQ(tablet_peer->tablet_id(), req->tablet_id());
  } else {
    auto tablet_peer_result = LookupTabletPeerOrRespond(
        server_->tablet_peer_lookup(), req->tablet_id(), resp, context);

    if (!tablet_peer_result.ok()) {
      return false;
    }

    tablet_peer = std::move(*tablet_peer_result);
  }

  Status s = CheckPeerIsReady(*tablet_peer);
  if (PREDICT_FALSE(!s.ok())) {
    SetupErrorAndRespond(resp->mutable_error(), s, context);
    return false;
  }

  // Check for leader only in strong consistency level.
  if (req->consistency_level() == YBConsistencyLevel::STRONG) {
    if (PREDICT_FALSE(FLAGS_assert_reads_served_by_follower) &&
        std::is_same<Req, ReadRequestPB>::value) {
      LOG(FATAL) << "--assert_reads_served_by_follower is true but consistency level is invalid: "
                 << "YBConsistencyLevel::STRONG";
    }
    if (PREDICT_FALSE(FLAGS_assert_reads_from_follower_rejected_because_of_staleness)) {
      LOG(FATAL) << "--assert_reads_from_follower_rejected_because_of_staleness is true but "
                    "consistency level is invalid: YBConsistencyLevel::STRONG";
    }

    s = CheckPeerIsLeader(*tablet_peer);
    if (PREDICT_FALSE(!s.ok())) {
      SetupErrorAndRespond(resp->mutable_error(), s, context);
      return false;
    }
  } else {
    s = CheckPeerIsLeader(*tablet_peer.get());

    // Peer is not the leader, so check that the time since it last heard from the leader is less
    // than FLAGS_max_stale_read_bound_time_ms.
    if (PREDICT_FALSE(!s.ok())) {
      if (FLAGS_max_stale_read_bound_time_ms > 0) {
        shared_ptr <consensus::Consensus> consensus = tablet_peer->shared_consensus();
        if (consensus->TimeSinceLastMessageFromLeader() != MonoTime::kUninitialized) {
          if (MonoTime::Now().GetDeltaSince(
              consensus->TimeSinceLastMessageFromLeader()).ToMilliseconds() >
              FLAGS_max_stale_read_bound_time_ms) {
            SetupErrorAndRespond(resp->mutable_error(), STATUS(IllegalState, "Stale follower"),
                                 TabletServerErrorPB::STALE_FOLLOWER, context);
            return false;
          } else if (PREDICT_FALSE(
              FLAGS_assert_reads_from_follower_rejected_because_of_staleness)) {
            LOG(FATAL) << "--assert_reads_from_follower_rejected_because_of_staleness is true, but "
                       << "peer " << tablet_peer->permanent_uuid()
                       << " for tablet: " << req->tablet_id()
                       << " is not stale. Time since last update from leader: "
                       << MonoTime::Now().GetDeltaSince(
                           consensus->TimeSinceLastMessageFromLeader()).ToMilliseconds();
          }
        } else {
          // If we haven't received a ping from the leader, we shouldn't serve read requests.
          SetupErrorAndRespond(
              resp->mutable_error(),
              STATUS(IllegalState, "Haven't received a ping from the leader"),
              TabletServerErrorPB::STALE_FOLLOWER, context);
          return false;
        }
      }
    } else {
      // We are here because we are the leader.
      if (PREDICT_FALSE(FLAGS_assert_reads_from_follower_rejected_because_of_staleness)) {
        LOG(FATAL) << "--assert_reads_from_follower_rejected_because_of_staleness is true but "
                   << " peer " << tablet_peer->permanent_uuid()
                   << " is the leader for tablet " << req->tablet_id();
      }
      if (PREDICT_FALSE(FLAGS_assert_reads_served_by_follower) &&
               std::is_same<Req, ReadRequestPB>::value) {
        LOG(FATAL) << "--assert_reads_served_by_follower is true but read is being served by "
                   << " peer " << tablet_peer->permanent_uuid()
                   << " which is the leader for tablet " << req->tablet_id();
      }
    }
  }

  shared_ptr<tablet::Tablet> ptr;
  TabletServerErrorPB::Code error_code;
  s = GetTabletRef(tablet_peer, &ptr, &error_code);
  if (PREDICT_FALSE(!s.ok())) {
    SetupErrorAndRespond(resp->mutable_error(), s, error_code, context);
    return false;
  }
  *tablet = ptr;
  return true;
}

struct ReadContext {
  const ReadRequestPB* req = nullptr;
  ReadResponsePB* resp = nullptr;
  rpc::RpcContext* context = nullptr;

  std::shared_ptr<tablet::AbstractTablet> tablet;

  ReadHybridTime read_time;
  HybridTime safe_ht_to_read;
  ReadHybridTime used_read_time;
  tablet::RequireLease require_lease = tablet::RequireLease::kFalse;
  HostPortPB* host_port_pb = nullptr;
  bool allow_retry = false;
  RequestScope request_scope;

  bool transactional() const {
    return tablet->SchemaRef().table_properties().is_transactional();
  }

  // Picks read based for specified read context.
  CHECKED_STATUS PickReadTime(server::Clock* clock) {
    if (!read_time) {
      safe_ht_to_read = tablet->SafeTime(require_lease);
      // If the read time is not specified, then it is a single-shard read.
      // So we should restart it in server in case of failure.
      read_time.read = safe_ht_to_read;
      if (transactional()) {
        read_time.local_limit = clock->MaxGlobalNow();
        read_time.global_limit = read_time.local_limit;

        VLOG(1) << "Read time: " << read_time.ToString();
      } else {
        read_time.local_limit = read_time.read;
        read_time.global_limit = read_time.read;
      }
    } else {
      safe_ht_to_read = tablet->SafeTime(
          require_lease, read_time.read, context->GetClientDeadline());
      if (!safe_ht_to_read.is_valid()) { // Timed out
        const char* error_message = "Timed out waiting for read time";
        TRACE(error_message);
        return STATUS(TimedOut, error_message);
      }
    }
    return Status::OK();
  }
};

// Used when we write intents during read, i.e. for serializable isolation.
// We cannot proceed with read from ReadOperationCompletionCallback, to avoid holding
// replica state lock for too long.
// So ThreadPool is used to proceed with read.
class ReadCompletionTask : public rpc::ThreadPoolTask {
 public:
  ReadCompletionTask(
      TabletServiceImpl* service,
      ReadContext&& read_context,
      std::shared_ptr<rpc::RpcContext> context)
      : service_(service), read_context_(std::move(read_context)), context_(std::move(context)) {
  }

  virtual ~ReadCompletionTask() = default;

 private:
  void Run() override {
    auto status = read_context_.PickReadTime(service_->server_->Clock());
    if (!status.ok()) {
      Done(status);
      return;
    }
    service_->CompleteRead(&read_context_);
  }

  void Done(const Status& status) override {
    if (!status.ok()) {
      SetupErrorAndRespond(
          read_context_.resp->mutable_error(), status, TabletServerErrorPB::UNKNOWN_ERROR,
          context_.get());
    }

    delete this;
  }

  TabletServiceImpl* service_;
  ReadContext read_context_;
  std::shared_ptr<rpc::RpcContext> context_;
};

class ReadOperationCompletionCallback : public OperationCompletionCallback {
 public:
  explicit ReadOperationCompletionCallback(
      TabletServiceImpl* service,
      tablet::TabletPeerPtr tablet_peer,
      ReadContext&& read_context,
      std::shared_ptr<rpc::RpcContext> context)
      : service_(service), tablet_peer_(std::move(tablet_peer)),
        read_context_(std::move(read_context)), context_(std::move(context)) {}

  void OperationCompleted() override {
    if (!status_.ok()) {
      SetupErrorAndRespond(read_context_.resp->mutable_error(), status_, code_,
                           context_.get());
      return;
    }

    tablet_peer_->Enqueue(new ReadCompletionTask(service_, std::move(read_context_), context_));
  }

 private:
  TabletServiceImpl* service_;
  tablet::TabletPeerPtr tablet_peer_;
  ReadContext read_context_;
  std::shared_ptr<rpc::RpcContext> context_;
};

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

  // Unfortunately, determining the isolation level is not as straightforward as it seems. All but
  // the first request to a given tablet by a particular transaction assume that the tablet already
  // has the transaction metadata, including the isolation level, and those requests expect us to
  // retrieve the isolation level from that metadata. Failure to do so was the cause of a
  // serialization anomaly tested by TestOneOrTwoAdmins
  // (https://github.com/YugaByte/yugabyte-db/issues/1572).

  LongOperationTracker long_operation_tracker("Read", 1s);

  bool serializable_isolation = false;
  TabletPeerPtr tablet_peer;
  if (req->has_transaction()) {
    IsolationLevel isolation_level;
    if (req->transaction().has_isolation()) {
      // This must be the first request to this tablet by this particular transaction.
      isolation_level = req->transaction().isolation();
    } else {
      tablet_peer = VERIFY_RESULT_OR_RETURN(LookupTabletPeerOrRespond(
          server_->tablet_peer_lookup(), req->tablet_id(), resp, &context));
      auto isolation_level_result = tablet_peer->tablet()->GetIsolationLevelFromPB(*req);
      if (!isolation_level_result.ok()) {
        SetupErrorAndRespond(
            resp->mutable_error(), isolation_level_result.status(),
            TabletServerErrorPB::UNKNOWN_ERROR, &context);
        return;
      }
      isolation_level = *isolation_level_result;
    }
    serializable_isolation = isolation_level == IsolationLevel::SERIALIZABLE_ISOLATION;
#if defined(DUMP_READ)
    if (req->pgsql_batch().size() > 0) {
      LOG(INFO) << CHECK_RESULT(FullyDecodeTransactionId(req->transaction().transaction_id()))
                << " READ: " << req->pgsql_batch(0).partition_column_values(0).value().int32_value()
                << ", " << isolation_level;
    }
#endif
  }

  bool has_row_lock = false;
  // For postgres requests check that the syscatalog version matches.
  if (!req->pgsql_batch().empty()) {
    for (const auto& pg_req : req->pgsql_batch()) {
      if (pg_req.has_ysql_catalog_version() &&
          pg_req.ysql_catalog_version() < server_->ysql_catalog_version()) {
        SetupErrorAndRespond(resp->mutable_error(),
            STATUS_SUBSTITUTE(QLError, "Catalog Version Mismatch: A DDL occurred while processing "
                                       "this query. Try Again."),
            TabletServerErrorPB::MISMATCHED_SCHEMA, &context);
        return;
      }
      if (pg_req.row_mark_type_size() > 0 &&
          (pg_req.row_mark_type(0) == RowMarkType::ROW_MARK_SHARE ||
           pg_req.row_mark_type(0) == RowMarkType::ROW_MARK_KEYSHARE)) {
        if (!req->has_transaction()) {
          SetupErrorAndRespond(resp->mutable_error(),
              STATUS(NotSupported,
                     "Read request with row mark types must be part of a transaction"),
              TabletServerErrorPB::OPERATION_NOT_SUPPORTED, &context);
        }
        has_row_lock = true;
      }
    }
  }

  LeaderTabletPeer leader_peer;
  ReadContext read_context = {req, resp, &context};

  if (serializable_isolation || has_row_lock) {
    // At this point we expect that we don't have pure read serializable transactions, and
    // always write read intents to detect conflicts with other writes.
    leader_peer = LookupLeaderTabletOrRespond(
        server_->tablet_peer_lookup(), req->tablet_id(), resp, &context, std::move(tablet_peer));
    // Serializable read adds intents, i.e. writes data.
    // We should check for memory pressure in this case.
    if (!leader_peer ||
        !CheckWriteThrottlingOrRespond(
            req->rejection_score(), leader_peer.peer.get(), resp, &context)) {
      return;
    }
    read_context.tablet = leader_peer.peer->shared_tablet();
  } else {
    if (!GetTabletOrRespond(req, resp, &context, &read_context.tablet, std::move(tablet_peer))) {
      return;
    }
    leader_peer.leader_term = yb::OpId::kUnknownTerm;
  }

  if (PREDICT_FALSE(FLAGS_simulate_time_out_failures) && RandomUniformInt(0, 10) < 3) {
    LOG(INFO) << "Marking request as timed out for test";
    SetupErrorAndRespond(resp->mutable_error(), STATUS(TimedOut, "timed out for test"),
        TabletServerErrorPB::UNKNOWN_ERROR, &context);
    return;
  }

  if (server_ && server_->Clock()) {
    server::UpdateClock(*req, server_->Clock());
  }

  // safe_ht_to_read is used only for read restart, so if read_time is valid, then we would respond
  // with "restart required".
  ReadHybridTime& read_time = read_context.read_time;

  read_time = ReadHybridTime::FromReadTimePB(*req);

  read_context.allow_retry = !read_time;
  read_context.require_lease = tablet::RequireLease(
      req->consistency_level() == YBConsistencyLevel::STRONG);
  // TODO: should check all the tables referenced by the requests to decide if it is transactional.
  const bool transactional = read_context.transactional();
  // Should not pick read time for serializable isolation, since it is picked after read intents
  // added. Also conflict resolution for serializable isolation should be done w/o read time
  // specified. So we use max hybrid time for conflict resolution in such case.
  // It was implemented as part of #655.
  if (!serializable_isolation) {
    auto status = read_context.PickReadTime(server_->Clock());
    if (!status.ok()) {
      SetupErrorAndRespond(
          resp->mutable_error(), status, TabletServerErrorPB::UNKNOWN_ERROR, &context);
      return;
    }
  }

  if (transactional) {
    // Serial number is used for check whether this operation was initiated before
    // transaction status request. So we should initialize it as soon as possible.
    read_context.request_scope = RequestScope(
        down_cast<Tablet*>(read_context.tablet.get())->transaction_participant());
    read_time.serial_no = read_context.request_scope.request_id();
  }

  const auto& remote_address = context.remote_address();
  HostPortPB host_port_pb;
  host_port_pb.set_host(remote_address.address().to_string());
  host_port_pb.set_port(remote_address.port());
  read_context.host_port_pb = &host_port_pb;

  if (serializable_isolation || has_row_lock) {
    WriteRequestPB write_req;
    *write_req.mutable_write_batch()->mutable_transaction() = req->transaction();
    if (has_row_lock) {
      // This will have to be modified once we support more row locks.
      // See https://github.com/yugabyte/yugabyte-db/issues/1199 and
      // https://github.com/yugabyte/yugabyte-db/issues/2496.
      write_req.mutable_write_batch()->add_row_mark_type(RowMarkType::ROW_MARK_SHARE);
      read_context.read_time.ToPB(write_req.mutable_read_time());
    }
    write_req.set_tablet_id(req->tablet_id());
    write_req.mutable_write_batch()->set_deprecated_may_have_metadata(true);
    // TODO(dtxn) write request id

    auto* write_batch = write_req.mutable_write_batch();
    auto status = leader_peer.peer->tablet()->CreateReadIntents(
        req->transaction(), req->ql_batch(), req->pgsql_batch(), write_batch);
    if (!status.ok()) {
      SetupErrorAndRespond(
          resp->mutable_error(), status, TabletServerErrorPB::UNKNOWN_ERROR, &context);
      return;
    }

    auto operation_state = std::make_unique<WriteOperationState>(
        leader_peer.peer->tablet(), &write_req, nullptr /* response */,
        docdb::OperationKind::kRead);

    auto context_ptr = std::make_shared<RpcContext>(std::move(context));
    read_context.context = context_ptr.get();
    operation_state->set_completion_callback(std::make_unique<ReadOperationCompletionCallback>(
        this, leader_peer.peer, std::move(read_context), context_ptr));
    leader_peer.peer->WriteAsync(
        std::move(operation_state), leader_peer.leader_term, context_ptr->GetClientDeadline());
    return;
  }

  CompleteRead(&read_context);
}

void TabletServiceImpl::CompleteRead(ReadContext* read_context) {
  for (;;) {
    read_context->resp->Clear();
    read_context->context->ResetRpcSidecars();
    VLOG(1) << "Read time: " << read_context->read_time
            << ", safe: " << read_context->safe_ht_to_read;
    auto result = DoRead(read_context);
    if (!result.ok()) {
      WARN_NOT_OK(result.status(), "DoRead");
      SetupErrorAndRespond(
          read_context->resp->mutable_error(), result.status(), TabletServerErrorPB::UNKNOWN_ERROR,
          read_context->context);
      return;
    }
    read_context->read_time = *result;
    // If read was successful, then restart time is invalid. Finishing.
    if (!read_context->read_time) {
      break;
    }
    if (!read_context->allow_retry) {
      // The read time is specified, than we read as part of transaction. So we should restart
      // whole transaction. In this case we report restart time and abort reading.
      read_context->resp->Clear();
      auto restart_read_time = read_context->resp->mutable_restart_read_time();
      restart_read_time->set_read_ht(read_context->read_time.read.ToUint64());
      restart_read_time->set_local_limit_ht(read_context->read_time.local_limit.ToUint64());
      // Global limit is ignored by caller, so we don't set it.
      down_cast<Tablet*>(read_context->tablet.get())->metrics()->restart_read_requests->Increment();
      break;
    }

    if (CoarseMonoClock::now() > read_context->context->GetClientDeadline()) {
      TRACE("Read timed out");
      SetupErrorAndRespond(read_context->resp->mutable_error(), STATUS(TimedOut, ""),
                           TabletServerErrorPB::UNKNOWN_ERROR, read_context->context);
      return;
    }
  }
  if (read_context->req->include_trace() && Trace::CurrentTrace() != nullptr) {
    read_context->resp->set_trace_buffer(Trace::CurrentTrace()->DumpToString(true));
  }

  // It was read as part of transaction, but read time was not specified.
  // I.e. allow_retry is true.
  // So we just picked a read time and we should tell it back to the caller.
  if (read_context->req->has_transaction() && read_context->allow_retry) {
    read_context->used_read_time.ToPB(read_context->resp->mutable_used_read_time());
  }

  // Useful when debugging transactions
#if defined(DUMP_READ)
  if (read_context->req->has_transaction() && read_context->req->pgsql_batch().size() == 1 &&
      read_context->req->pgsql_batch()[0].partition_column_values().size() == 1 &&
      read_context->resp->pgsql_batch().size() == 1 &&
      read_context->resp->pgsql_batch()[0].rows_data_sidecar() == 0) {
    auto txn_id = CHECK_RESULT(FullyDecodeTransactionId(
        read_context->req->transaction().transaction_id()));
    auto value_slice = read_context->context->RpcSidecar(0).as_slice();
    auto num = BigEndian::Load64(value_slice.data());
    std::string result;
    if (num == 0) {
      result = "<NONE>";
    } else if (num == 1) {
      auto len = BigEndian::Load64(value_slice.data() + 14) - 1;
      result = Slice(value_slice.data() + 22, len).ToBuffer();
    } else {
      result = value_slice.ToDebugHexString();
    }
    auto key = read_context->req->pgsql_batch(0).partition_column_values(0).value().int32_value();
    LOG(INFO) << txn_id << " READ DONE: " << key << " = " << result;
  }
#endif

  RpcOperationCompletionCallback<ReadResponsePB> callback(
      std::move(*read_context->context), read_context->resp, server_->Clock());
  callback.OperationCompleted();
  TRACE("Done Read");
}

void HandleRedisReadRequestAsync(
    tablet::AbstractTablet* tablet,
    CoarseTimePoint deadline,
    const ReadHybridTime& read_time,
    const RedisReadRequestPB& redis_read_request,
    RedisResponsePB* response,
    const std::function<void(const Status& s)>& status_cb
) {
  status_cb(tablet->HandleRedisReadRequest(deadline, read_time, redis_read_request, response));
}

Result<ReadHybridTime> TabletServiceImpl::DoRead(ReadContext* read_context) {
  auto read_tx = VERIFY_RESULT(
      tablet::ScopedReadOperation::Create(
          read_context->tablet.get(), read_context->require_lease, read_context->read_time));
  read_context->used_read_time = read_tx.read_time();
  if (!read_context->req->redis_batch().empty()) {
    // Assert the primary table is a redis table.
    DCHECK_EQ(read_context->tablet->table_type(), TableType::REDIS_TABLE_TYPE);
    size_t count = read_context->req->redis_batch_size();
    std::vector<Status> rets(count);
    CountDownLatch latch(count);
    for (int idx = 0; idx < count; idx++) {
      const RedisReadRequestPB& redis_read_req = read_context->req->redis_batch(idx);
      Status &failed_status_ = rets[idx];
      auto cb = [&latch, &failed_status_] (const Status &status) -> void {
                  if (!status.ok())
                    failed_status_ = status;
                  latch.CountDown(1);
                };
      auto func = Bind(
          &HandleRedisReadRequestAsync,
          Unretained(read_context->tablet.get()),
          read_context->context->GetClientDeadline(),
          read_tx.read_time(),
          redis_read_req,
          Unretained(read_context->resp->add_redis_batch()),
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

  if (!read_context->req->ql_batch().empty()) {
    // Assert the primary table is a YQL table.
    DCHECK_EQ(read_context->tablet->table_type(), TableType::YQL_TABLE_TYPE);
    ReadRequestPB* mutable_req = const_cast<ReadRequestPB*>(read_context->req);
    for (QLReadRequestPB& ql_read_req : *mutable_req->mutable_ql_batch()) {
      // Update the remote endpoint.
      ql_read_req.set_allocated_remote_endpoint(read_context->host_port_pb);
      ql_read_req.set_allocated_proxy_uuid(mutable_req->mutable_proxy_uuid());
      auto se = ScopeExit([&ql_read_req] {
        ql_read_req.release_remote_endpoint();
        ql_read_req.release_proxy_uuid();
      });

      tablet::QLReadRequestResult result;
      TRACE("Start HandleQLReadRequest");
      RETURN_NOT_OK(read_context->tablet->HandleQLReadRequest(
          read_context->context->GetClientDeadline(), read_tx.read_time(), ql_read_req,
          read_context->req->transaction(), &result));
      TRACE("Done HandleQLReadRequest");
      if (result.restart_read_ht.is_valid()) {
        DCHECK_GT(result.restart_read_ht, read_context->read_time.read);
        VLOG(1) << "Restart read required at: " << result.restart_read_ht
                << ", original: " << read_context->read_time;
        read_context->read_time.read = result.restart_read_ht;
        read_context->read_time.local_limit = read_context->safe_ht_to_read;
        return read_context->read_time;
      }
      int rows_data_sidecar_idx = 0;
      RETURN_NOT_OK(read_context->context->AddRpcSidecar(
          RefCntBuffer(result.rows_data), &rows_data_sidecar_idx));
      result.response.set_rows_data_sidecar(rows_data_sidecar_idx);
      read_context->resp->add_ql_batch()->Swap(&result.response);
    }
    return ReadHybridTime();
  }

  if (!read_context->req->pgsql_batch().empty()) {
    ReadRequestPB* mutable_req = const_cast<ReadRequestPB*>(read_context->req);
    for (PgsqlReadRequestPB& pgsql_read_req : *mutable_req->mutable_pgsql_batch()) {
      tablet::PgsqlReadRequestResult result;
      TRACE("Start HandlePgsqlReadRequest");
      RETURN_NOT_OK(read_context->tablet->HandlePgsqlReadRequest(
          read_context->context->GetClientDeadline(), read_tx.read_time(), pgsql_read_req,
          read_context->req->transaction(), &result));
      TRACE("Done HandlePgsqlReadRequest");
      if (result.restart_read_ht.is_valid()) {
        VLOG(1) << "Restart read required at: " << result.restart_read_ht;
        read_context->read_time.read = result.restart_read_ht;
        read_context->read_time.local_limit = read_context->safe_ht_to_read;
        return read_context->read_time;
      }
      int rows_data_sidecar_idx = 0;
      RETURN_NOT_OK(read_context->context->AddRpcSidecar(
          RefCntBuffer(result.rows_data), &rows_data_sidecar_idx));
      result.response.set_rows_data_sidecar(rows_data_sidecar_idx);
      read_context->resp->add_pgsql_batch()->Swap(&result.response);
    }
    return ReadHybridTime();
  }

  if (read_context->tablet->table_type() == TableType::TRANSACTION_STATUS_TABLE_TYPE) {
    return STATUS(NotSupported, "Transaction status table does not support read");
  }

  return ReadHybridTime();
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
  auto tablet_peer = VERIFY_RESULT_OR_RETURN(LookupTabletPeerOrRespond(
      tablet_manager_, req->tablet_id(), resp, &context));

  // Submit the update directly to the TabletPeer's Consensus instance.
  shared_ptr<Consensus> consensus;
  if (!GetConsensusOrRespond(tablet_peer, resp, &context, &consensus)) return;

  // Unfortunately, we have to use const_cast here, because the protobuf-generated interface only
  // gives us a const request, but we need to be able to move messages out of the request for
  // efficiency.
  Status s = consensus->Update(
      const_cast<ConsensusRequestPB*>(req), resp, context.GetClientDeadline());
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

  auto tablet = tablet_peer->shared_tablet();
  if (tablet) {
    resp->set_num_sst_files(tablet->GetCurrentVersionNumSSTFiles());
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
  auto tablet_peer = VERIFY_RESULT_OR_RETURN(LookupTabletPeerOrRespond(
      tablet_manager_, req->tablet_id(), resp, &context));

  // Submit the vote request directly to the consensus instance.
  shared_ptr<Consensus> consensus;
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
  auto tablet_peer = VERIFY_RESULT_OR_RETURN(LookupTabletPeerOrRespond(
      tablet_manager_, req->tablet_id(), resp, &context));

  shared_ptr<Consensus> consensus;
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
  auto status = tablet_manager_->GetRegistration(resp->mutable_registration());
  if (!status.ok()) {
    context.RespondFailure(status);
  } else {
    context.RespondSuccess();
  }
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
    auto tablet_peer = VERIFY_RESULT_OR_RETURN(LookupTabletPeerOrRespond(
        tablet_manager, req->tablet_id(), resp, context));

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
  shared_ptr<Consensus> consensus_;
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
      { consensus::ElectionMode::ELECT_EVEN_IF_LEADER_IS_ALIVE,
        req->has_committed_index(),
        req->committed_index(),
        req->has_originator_uuid() ? req->originator_uuid() : std::string(),
        consensus::TEST_SuppressVoteRequest(
          req->has_suppress_vote_request() && req->suppress_vote_request()) });
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
  auto tablet_peer = VERIFY_RESULT_OR_RETURN(LookupTabletPeerOrRespond(
      tablet_manager_, req->tablet_id(), resp, &context));

  if (tablet_peer->state() != tablet::RUNNING) {
    SetupErrorAndRespond(resp->mutable_error(),
                         STATUS(ServiceUnavailable, "Tablet Peer not in RUNNING state"),
                         TabletServerErrorPB::TABLET_NOT_RUNNING, &context);
    return;
  }
  std::shared_ptr<Consensus> consensus;
  if (!GetConsensusOrRespond(tablet_peer, resp, &context, &consensus)) return;
  if (PREDICT_FALSE(req->opid_type() == consensus::UNKNOWN_OPID_TYPE)) {
    HandleErrorResponse(resp, &context,
                        STATUS(InvalidArgument, "Invalid opid_type specified to GetLastOpId()"));
    return;
  }
  auto op_id = consensus->GetLastOpId(req->opid_type());
  // RETURN_UNKNOWN_ERROR_IF_NOT_OK does not support Result, so have to add extra check here.
  if (!op_id.ok()) {
    RETURN_UNKNOWN_ERROR_IF_NOT_OK(op_id.status(), resp, &context);
  }
  op_id->ToPB(resp->mutable_opid());
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
  if (!s.ok()) {
    // Using Status::AlreadyPresent for a remote bootstrap operation that is already in progress.
    if (s.IsAlreadyPresent()) {
      YB_LOG_EVERY_N_SECS(WARNING, 30) << "Start remote bootstrap failed: " << s;
      SetupErrorAndRespond(resp->mutable_error(), s, TabletServerErrorPB::ALREADY_IN_PROGRESS,
                           &context);
      return;
    } else {
      LOG(WARNING) << "Start remote bootstrap failed: " << s;
    }
  }

  RETURN_UNKNOWN_ERROR_IF_NOT_OK(s, resp, &context);
  context.RespondSuccess();
}

void TabletServiceImpl::NoOp(const NoOpRequestPB *req,
                             NoOpResponsePB *resp,
                             rpc::RpcContext context) {
  context.RespondSuccess();
}

void TabletServiceImpl::Publish(
    const PublishRequestPB* req, PublishResponsePB* resp, rpc::RpcContext context) {
  rpc::Publisher* publisher = server_->GetPublisher();
  resp->set_num_clients_forwarded_to(publisher ? (*publisher)(req->channel(), req->message()) : 0);
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
    SchemaToPB(peer->status_listener()->schema(), status->mutable_schema());
    peer->tablet_metadata()->partition_schema().ToPB(status->mutable_partition_schema());
  }
  context.RespondSuccess();
}

void TabletServiceImpl::GetMasterAddresses(const GetMasterAddressesRequestPB* req,
                                           GetMasterAddressesResponsePB* resp,
                                           rpc::RpcContext context) {
  resp->set_master_addresses(server::MasterAddressesToString(
      *server_->tablet_manager()->server()->options().GetMasterAddresses()));
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

    std::shared_ptr<consensus::Consensus> consensus = peer->shared_consensus();
    data_entry->set_is_leader(consensus && consensus->role() == consensus::RaftPeerPB::LEADER);
    data_entry->set_state(status.state());

    auto tablet = peer->shared_tablet();
    uint64_t num_sst_files = (tablet) ? tablet->GetCurrentVersionNumSSTFiles() : 0;
    data_entry->set_num_sst_files(num_sst_files);

    uint64_t num_log_segments = peer->GetNumLogSegments();
    data_entry->set_num_log_segments(num_log_segments);
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

  while (VERIFY_RESULT((**iter).HasNext())) {
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
  auto peer = VERIFY_RESULT_OR_RETURN(LookupTabletPeerOrRespond(
      server_->tablet_peer_lookup(), req->tablet_id(), resp, &context));

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
  const Status s = server_->GetTabletStatus(req, resp);
  if (!s.ok()) {
    SetupErrorAndRespond(resp->mutable_error(), s,
                         s.IsNotFound() ? TabletServerErrorPB::TABLET_NOT_FOUND
                                        : TabletServerErrorPB::UNKNOWN_ERROR,
                         &context);
    return;
  }
  context.RespondSuccess();
}

void TabletServiceImpl::IsTabletServerReady(const IsTabletServerReadyRequestPB* req,
                                            IsTabletServerReadyResponsePB* resp,
                                            rpc::RpcContext context) {
  Status s = server_->tablet_manager()->GetNumTabletsPendingBootstrap(resp);
  if (!s.ok()) {
    SetupErrorAndRespond(resp->mutable_error(), s, TabletServerErrorPB::UNKNOWN_ERROR,
                         &context);
    return;
  }
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
