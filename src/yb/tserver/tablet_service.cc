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

#include "yb/common/iterator.h"
#include "yb/common/schema.h"
#include "yb/common/wire_protocol.h"
#include "yb/consensus/consensus.h"
#include "yb/consensus/leader_lease.h"
#include "yb/docdb/doc_operation.h"
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
#include "yb/tablet/tablet_metrics.h"

#include "yb/tablet/operations/alter_schema_operation.h"
#include "yb/tablet/operations/truncate_operation.h"
#include "yb/tablet/operations/update_txn_operation.h"
#include "yb/tablet/operations/write_operation.h"

#include "yb/tserver/scanners.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/tserver/tserver.pb.h"
#include "yb/util/crc.h"
#include "yb/util/debug/trace_event.h"
#include "yb/util/faststring.h"
#include "yb/util/flag_tags.h"
#include "yb/util/mem_tracker.h"
#include "yb/util/monotime.h"
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
using tablet::TabletStatusPB;
using tablet::TruncateOperationState;
using tablet::OperationCompletionCallback;
using tablet::WriteOperationState;

namespace {

template<class RespClass>
bool GetConsensusOrRespond(const scoped_refptr<TabletPeer>& tablet_peer,
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

Status GetTabletRef(const scoped_refptr<TabletPeer>& tablet_peer,
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

// Generic interface to handle scan results.
class ScanResultCollector {
 public:
  virtual ~ScanResultCollector() {}
  virtual void HandleRowBlock(const Schema* client_projection_schema,
                              const RowBlock& row_block) = 0;

  // Returns number of times HandleRowBlock() was called.
  virtual int BlocksProcessed() const = 0;

  // Returns number of bytes which will be returned in the response.
  virtual int64_t ResponseSize() const = 0;

  // Returns the last processed row's primary key.
  virtual const faststring& last_primary_key() const = 0;

  // Return the number of rows actually returned to the client.
  virtual int64_t NumRowsReturned() const = 0;
};

namespace {

// Given a RowBlock, set last_primary_key to the primary key of the last selected row
// in the RowBlock. If no row is selected, last_primary_key is not set.
void SetLastRow(const RowBlock& row_block, faststring* last_primary_key) {
  // Find the last selected row and save its encoded key.
  const SelectionVector* sel = row_block.selection_vector();
  if (sel->AnySelected()) {
    for (int i = sel->nrows() - 1; i >= 0; i--) {
      if (sel->IsRowSelected(i)) {
        RowBlockRow last_row = row_block.row(i);
        const Schema* schema = last_row.schema();
        schema->EncodeComparableKey(last_row, last_primary_key);
        break;
      }
    }
  }
}

}  // namespace

// Copies the scan result to the given row block PB and data buffers.
//
// This implementation is used in the common case where a client is running
// a scan and the data needs to be returned to the client.
//
// (This is in contrast to some other ScanResultCollector implementation that
// might do an aggregation or gather some other types of statistics via a
// server-side scan and thus never need to return the actual data.)
class ScanResultCopier : public ScanResultCollector {
 public:
  ScanResultCopier(RowwiseRowBlockPB* rowblock_pb, faststring* rows_data, faststring* indirect_data)
      : rowblock_pb_(DCHECK_NOTNULL(rowblock_pb)),
        rows_data_(DCHECK_NOTNULL(rows_data)),
        indirect_data_(DCHECK_NOTNULL(indirect_data)),
        blocks_processed_(0),
        num_rows_returned_(0) {
  }

  virtual void HandleRowBlock(const Schema* client_projection_schema,
                              const RowBlock& row_block) override {
    blocks_processed_++;
    num_rows_returned_ += row_block.selection_vector()->CountSelected();
    SerializeRowBlock(row_block, rowblock_pb_, client_projection_schema,
                      rows_data_, indirect_data_);
    SetLastRow(row_block, &last_primary_key_);
  }

  int BlocksProcessed() const override { return blocks_processed_; }

  // Returns number of bytes buffered to return.
  int64_t ResponseSize() const override {
    return rows_data_->size() + indirect_data_->size();
  }

  const faststring& last_primary_key() const override {
    return last_primary_key_;
  }

  int64_t NumRowsReturned() const override {
    return num_rows_returned_;
  }

 private:
  RowwiseRowBlockPB* const rowblock_pb_;
  faststring* const rows_data_;
  faststring* const indirect_data_;
  int blocks_processed_;
  int64_t num_rows_returned_;
  faststring last_primary_key_;

  DISALLOW_COPY_AND_ASSIGN(ScanResultCopier);
};

// Checksums the scan result.
class ScanResultChecksummer : public ScanResultCollector {
 public:
  ScanResultChecksummer()
      : crc_(crc::GetCrc32cInstance()),
        agg_checksum_(0),
        blocks_processed_(0) {
  }

  virtual void HandleRowBlock(const Schema* client_projection_schema,
                              const RowBlock& row_block) override {
    blocks_processed_++;
    if (!client_projection_schema) {
      client_projection_schema = &row_block.schema();
    }

    size_t nrows = row_block.nrows();
    for (size_t i = 0; i < nrows; i++) {
      if (!row_block.selection_vector()->IsRowSelected(i)) continue;
      uint32_t row_crc = CalcRowCrc32(*client_projection_schema, row_block.row(i));
      agg_checksum_ += row_crc;
    }
    // Find the last selected row and save its encoded key.
    SetLastRow(row_block, &encoded_last_row_);
  }

  int BlocksProcessed() const override { return blocks_processed_; }

  // Returns a constant -- we only return checksum based on a time budget.
  int64_t ResponseSize() const override { return sizeof(agg_checksum_); }

  const faststring& last_primary_key() const override { return encoded_last_row_; }

  int64_t NumRowsReturned() const override {
    return 0;
  }

  // Accessors for initializing / setting the checksum.
  void set_agg_checksum(uint64_t value) { agg_checksum_ = value; }
  uint64_t agg_checksum() const { return agg_checksum_; }

 private:
  // Calculates a CRC32C for the given row.
  uint32_t CalcRowCrc32(const Schema& projection, const RowBlockRow& row) {
    tmp_buf_.clear();

    for (size_t j = 0; j < projection.num_columns(); j++) {
      uint32_t col_index = static_cast<uint32_t>(j);  // For the CRC.
      tmp_buf_.append(&col_index, sizeof(col_index));
      ColumnBlockCell cell = row.cell(j);
      if (cell.is_nullable()) {
        uint8_t is_defined = cell.is_null() ? 0 : 1;
        tmp_buf_.append(&is_defined, sizeof(is_defined));
        if (!is_defined) continue;
      }
      if (cell.typeinfo()->physical_type() == BINARY) {
        const Slice* data = reinterpret_cast<const Slice *>(cell.ptr());
        tmp_buf_.append(data->data(), data->size());
      } else {
        tmp_buf_.append(cell.ptr(), cell.size());
      }
    }

    uint64_t row_crc = 0;
    crc_->Compute(tmp_buf_.data(), tmp_buf_.size(), &row_crc, nullptr);
    return static_cast<uint32_t>(row_crc);  // CRC32 only uses the lower 32 bits.
  }

  faststring tmp_buf_;
  crc::Crc* const crc_;
  uint64_t agg_checksum_;
  int blocks_processed_;
  faststring encoded_last_row_;

  DISALLOW_COPY_AND_ASSIGN(ScanResultChecksummer);
};

// Return the batch size to use for a given request, after clamping
// the user-requested request within the server-side allowable range.
// This is only a hint, really more of a threshold since returned bytes
// may exceed this limit, but hopefully only by a little bit.
static size_t GetMaxBatchSizeBytesHint(const ScanRequestPB* req) {
  if (!req->has_batch_size_bytes()) {
    return FLAGS_scanner_default_batch_size_bytes;
  }

  return std::min(req->batch_size_bytes(),
                  implicit_cast<uint32_t>(FLAGS_scanner_max_batch_size_bytes));
}

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

  scoped_refptr<TabletPeer> tablet_peer;
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

  scoped_refptr<tablet::TabletPeer> tablet_peer;
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
                                           shared_ptr<tablet::AbstractTablet>* tablet) {
  scoped_refptr<TabletPeer> tablet_peer;
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
  if (!read_time) {
    safe_ht_to_read = tablet->SafeHybridTimeToReadAt(require_lease);
    // If the read time is not specified, then it is non transactional read.
    // So we should restart it in server in case of failure.
    read_time.read = safe_ht_to_read;
    if (tablet->SchemaRef().table_properties().is_transactional()) {
      read_time.local_limit = server_->Clock()->Now().AddMicroseconds(FLAGS_max_clock_skew_usec);
      read_time.global_limit = read_time.local_limit;

      VLOG(1) << "Read time: " << read_time.ToString();
    } else {
      read_time.local_limit = read_time.read;
      read_time.global_limit = read_time.read;
    }
  } else {
    safe_ht_to_read = tablet->SafeHybridTimeToReadAt(
        require_lease, read_time.read, context.GetClientDeadline());
    if (!safe_ht_to_read.is_valid()) { // Timed out
      SetupErrorAndRespond(resp->mutable_error(), STATUS(TimedOut, ""),
                           TabletServerErrorPB::UNKNOWN_ERROR, &context);
      return;
    }
  }

  const auto& remote_address = context.remote_address();
  HostPortPB host_port_pb;
  host_port_pb.set_host(remote_address.address().to_string());
  host_port_pb.set_port(remote_address.port());

  for (;;) {
    resp->Clear();
    context.ResetRpcSidecars();
    auto result = DoRead(
        tablet.get(), req, read_time, safe_ht_to_read, &host_port_pb, resp, &context);
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

Result<ReadHybridTime> TabletServiceImpl::DoRead(tablet::AbstractTablet* tablet,
                                                 const ReadRequestPB* req,
                                                 ReadHybridTime read_time,
                                                 HybridTime safe_ht_to_read,
                                                 HostPortPB* host_port_pb,
                                                 ReadResponsePB* resp,
                                                 rpc::RpcContext* context) {
  tablet::RequireLease require_lease(req->consistency_level() == YBConsistencyLevel::STRONG);
  tablet::ScopedReadOperation read_tx(tablet, require_lease, read_time);
  switch (tablet->table_type()) {
    case TableType::REDIS_TABLE_TYPE: {
      for (const RedisReadRequestPB& redis_read_req : req->redis_batch()) {
        RedisResponsePB redis_response;
        RETURN_NOT_OK(tablet->HandleRedisReadRequest(
            read_tx.read_time(), redis_read_req, &redis_response));
        resp->add_redis_batch()->Swap(&redis_response);
      }

      // TODO(dtxn) implement read restart for Redis.
      return ReadHybridTime();
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
          VLOG(1) << "Restart read required at: " << result.restart_read_ht;
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
  scoped_refptr<TabletPeer> tablet_peer;
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
  scoped_refptr<TabletPeer> tablet_peer;
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
  scoped_refptr<TabletPeer> tablet_peer;
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
    scoped_refptr<TabletPeer> tablet_peer;
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
  scoped_refptr<TabletPeer> tablet_peer;
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
  RETURN_UNKNOWN_ERROR_IF_NOT_OK(s, resp, &context);
  context.RespondSuccess();
}

void TabletServiceImpl::ScannerKeepAlive(const ScannerKeepAliveRequestPB *req,
                                         ScannerKeepAliveResponsePB *resp,
                                         rpc::RpcContext context) {
  if(!req->has_scanner_id()) {
    context.RespondFailure(STATUS(InvalidArgument, "Scanner not specified"));
    return;
  }
  SharedScanner scanner;
  if (!server_->scanner_manager()->LookupScanner(req->scanner_id(), &scanner)) {
    resp->mutable_error()->set_code(TabletServerErrorPB::SCANNER_EXPIRED);
    StatusToPB(STATUS(NotFound, "Scanner not found", req->scanner_id()),
               resp->mutable_error()->mutable_status());
  } else {
    scanner->UpdateAccessTime();
  }
  context.RespondSuccess();
}

void TabletServiceImpl::NoOp(const NoOpRequestPB *req,
                             NoOpResponsePB *resp,
                             rpc::RpcContext context) {
  context.RespondSuccess();
}

void TabletServiceImpl::Scan(const ScanRequestPB* req,
                             ScanResponsePB* resp,
                             rpc::RpcContext context) {
  TRACE_EVENT0("tserver", "TabletServiceImpl::Scan");
  // Validate the request: user must pass a new_scan_request or
  // a scanner ID, but not both.
  if (PREDICT_FALSE(req->has_scanner_id() &&
                    req->has_new_scan_request())) {
    context.RespondFailure(STATUS(InvalidArgument,
                                  "Must not pass both a scanner_id and new_scan_request"));
    return;
  }

  size_t batch_size_bytes = GetMaxBatchSizeBytesHint(req);
  faststring rows_data(batch_size_bytes * 11 / 10);
  faststring indirect_data(batch_size_bytes * 11 / 10);
  RowwiseRowBlockPB data;
  ScanResultCopier collector(&data, &rows_data, &indirect_data);

  bool has_more_results = false;
  TabletServerErrorPB::Code error_code;
  if (req->has_new_scan_request()) {
    const NewScanRequestPB& scan_pb = req->new_scan_request();
    scoped_refptr<TabletPeer> tablet_peer;
    if (!LookupTabletPeerOrRespond(server_->tablet_manager(), scan_pb.tablet_id(), resp, &context,
                                   &tablet_peer)) {
      return;
    }
    string scanner_id;
    HybridTime scan_hybrid_time;
    Status s = HandleNewScanRequest(tablet_peer.get(), req, &context,
                                    &collector, &scanner_id, &scan_hybrid_time, &has_more_results,
                                    &error_code);
    if (PREDICT_FALSE(!s.ok())) {
      SetupErrorAndRespond(resp->mutable_error(), s, error_code, &context);
      return;
    }

    // Only set the scanner id if we have more results.
    if (has_more_results) {
      resp->set_scanner_id(scanner_id);
    }
    if (scan_hybrid_time.is_valid()) {
      resp->set_snap_hybrid_time(scan_hybrid_time.ToUint64());
    }
  } else if (req->has_scanner_id()) {
    Status s = HandleContinueScanRequest(req, &collector, &has_more_results, &error_code);
    if (PREDICT_FALSE(!s.ok())) {
      SetupErrorAndRespond(resp->mutable_error(), s, error_code, &context);
      return;
    }
  } else {
    context.RespondFailure(STATUS(InvalidArgument,
                                  "Must pass either a scanner_id or new_scan_request"));
    return;
  }
  resp->set_has_more_results(has_more_results);

  DVLOG(2) << "Blocks processed: " << collector.BlocksProcessed();
  if (collector.BlocksProcessed() > 0) {
    resp->mutable_data()->CopyFrom(data);

    // Add sidecar data to context and record the returned indices.
    int rows_idx;
    CHECK_OK(context.AddRpcSidecar(RefCntBuffer(rows_data), &rows_idx));
    resp->mutable_data()->set_rows_sidecar(rows_idx);

    // Add indirect data as a sidecar, if applicable.
    if (indirect_data.size() > 0) {
      int indirect_idx;
      CHECK_OK(context.AddRpcSidecar(RefCntBuffer(indirect_data), &indirect_idx));
      resp->mutable_data()->set_indirect_data_sidecar(indirect_idx);
    }

    // Set the last row found by the collector.
    // We could have an empty batch if all the remaining rows are filtered by the predicate,
    // in which case do not set the last row.
    const faststring& last = collector.last_primary_key();
    if (last.length() > 0) {
      resp->set_last_primary_key(last.ToString());
    }
  }

  context.RespondSuccess();
}

void TabletServiceImpl::ListTablets(const ListTabletsRequestPB* req,
                                    ListTabletsResponsePB* resp,
                                    rpc::RpcContext context) {
  std::vector<scoped_refptr<TabletPeer> > peers;
  server_->tablet_manager()->GetTabletPeers(&peers);
  RepeatedPtrField<StatusAndSchemaPB>* peer_status = resp->mutable_status_and_schema();
  for (const scoped_refptr<TabletPeer>& peer : peers) {
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
  std::vector<scoped_refptr<TabletPeer> > peers;
  server_->tablet_manager()->GetTabletPeers(&peers);
  for (const scoped_refptr<TabletPeer>& peer : peers) {
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

void TabletServiceImpl::Checksum(const ChecksumRequestPB* req,
                                 ChecksumResponsePB* resp,
                                 rpc::RpcContext context) {
  VLOG(1) << "Full request: " << req->DebugString();

  // Validate the request: user must pass a new_scan_request or
  // a scanner ID, but not both.
  if (PREDICT_FALSE(req->has_new_request() &&
                    req->has_continue_request())) {
    context.RespondFailure(STATUS(InvalidArgument,
                                  "Must not pass both a scanner_id and new_scan_request"));
    return;
  }

  // Convert ChecksumRequestPB to a ScanRequestPB.
  ScanRequestPB scan_req;
  if (req->has_call_seq_id()) scan_req.set_call_seq_id(req->call_seq_id());
  if (req->has_batch_size_bytes()) scan_req.set_batch_size_bytes(req->batch_size_bytes());
  if (req->has_close_scanner()) scan_req.set_close_scanner(req->close_scanner());

  ScanResultChecksummer collector;
  bool has_more = false;
  TabletServerErrorPB::Code error_code;
  if (req->has_new_request()) {
    scan_req.mutable_new_scan_request()->CopyFrom(req->new_request());
    const NewScanRequestPB& new_req = req->new_request();
    scoped_refptr<TabletPeer> tablet_peer;
    if (!LookupTabletPeerOrRespond(server_->tablet_manager(), new_req.tablet_id(), resp, &context,
                                   &tablet_peer)) {
      return;
    }

    string scanner_id;
    HybridTime snap_hybrid_time;
    Status s = HandleNewScanRequest(tablet_peer.get(), &scan_req, &context,
                                    &collector, &scanner_id, &snap_hybrid_time, &has_more,
                                    &error_code);
    if (PREDICT_FALSE(!s.ok())) {
      SetupErrorAndRespond(resp->mutable_error(), s, error_code, &context);
      return;
    }
    resp->set_scanner_id(scanner_id);
    if (snap_hybrid_time.is_valid()) {
      resp->set_snap_hybrid_time(snap_hybrid_time.ToUint64());
    }
  } else if (req->has_continue_request()) {
    const ContinueChecksumRequestPB& continue_req = req->continue_request();
    collector.set_agg_checksum(continue_req.previous_checksum());
    scan_req.set_scanner_id(continue_req.scanner_id());
    Status s = HandleContinueScanRequest(&scan_req, &collector, &has_more, &error_code);
    if (PREDICT_FALSE(!s.ok())) {
      SetupErrorAndRespond(resp->mutable_error(), s, error_code, &context);
      return;
    }
  } else {
    context.RespondFailure(STATUS(InvalidArgument,
                                  "Must pass either new_request or continue_request"));
    return;
  }

  resp->set_checksum(collector.agg_checksum());
  resp->set_has_more_results(has_more);

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

void TabletServiceImpl::Shutdown() {
}

// Extract a void* pointer suitable for use in a ColumnRangePredicate from the
// user-specified protobuf field.
// This validates that the pb_value has the correct length, copies the data into
// 'arena', and sets *result to point to it.
// Returns bad status if the user-specified value is the wrong length.
static Status ExtractPredicateValue(const ColumnSchema& schema,
                                    const string& pb_value,
                                    Arena* arena,
                                    const void** result) {
  // Copy the data from the protobuf into the Arena.
  uint8_t* data_copy = static_cast<uint8_t*>(arena->AllocateBytes(pb_value.size()));
  memcpy(data_copy, &pb_value[0], pb_value.size());

  // If the type is of variable length, then we need to return a pointer to a Slice
  // element pointing to the string. Otherwise, just verify that the provided
  // value was the right size.
  if (schema.type_info()->physical_type() == BINARY) {
    *result = arena->NewObject<Slice>(data_copy, pb_value.size());
  } else {
    // TODO: add test case for this invalid request
    size_t expected_size = schema.type_info()->size();
    if (pb_value.size() != expected_size) {
      return STATUS(InvalidArgument,
                    StringPrintf("Bad predicate on %s. Expected value size %zd, got %zd",
                                 schema.ToString().c_str(), expected_size, pb_value.size()));
    }
    *result = data_copy;
  }

  return Status::OK();
}

static Status DecodeEncodedKeyRange(const NewScanRequestPB& scan_pb,
                                    const Schema& tablet_schema,
                                    const SharedScanner& scanner,
                                    ScanSpec* spec) {
  gscoped_ptr<EncodedKey> start, stop;
  Inclusive start_inclusive = Inclusive::kTrue;
  if (scan_pb.has_start_primary_key()) {
    RETURN_NOT_OK_PREPEND(EncodedKey::DecodeEncodedString(
        tablet_schema, scanner->arena(),
        scan_pb.start_primary_key(), &start),
        "Invalid scan start key");
  }

  if (scan_pb.has_stop_primary_key()) {
    RETURN_NOT_OK_PREPEND(EncodedKey::DecodeEncodedString(
        tablet_schema, scanner->arena(),
        scan_pb.stop_primary_key(), &stop),
        "Invalid scan stop key");
  }

  if (scan_pb.order_mode() == ORDERED && scan_pb.has_last_primary_key()) {
    if (start) {
      return STATUS(InvalidArgument, "Cannot specify both a start key and a last key");
    }
    // Set the start key to the last key from a previous scan result.
    RETURN_NOT_OK_PREPEND(EncodedKey::DecodeEncodedString(tablet_schema, scanner->arena(),
                                                          scan_pb.last_primary_key(), &start),
                          "Failed to decode last primary key");
    start_inclusive = Inclusive::kFalse;
  }

  if (start) {
    spec->SetLowerBoundKey(start.get(), start_inclusive);
    scanner->autorelease_pool()->Add(start.release());
  }
  if (stop) {
    spec->SetExclusiveUpperBoundKey(stop.get());
    scanner->autorelease_pool()->Add(stop.release());
  }

  return Status::OK();
}

static Status SetupScanSpec(const NewScanRequestPB& scan_pb,
                            const Schema& tablet_schema,
                            const Schema& projection,
                            std::vector<ColumnSchema>* missing_cols,
                            gscoped_ptr<ScanSpec>* spec,
                            const SharedScanner& scanner) {
  gscoped_ptr<ScanSpec> ret(new ScanSpec);
  ret->set_cache_blocks(scan_pb.cache_blocks());

  unordered_set<string> missing_col_names;

  // First the column range predicates.
  for (const ColumnRangePredicatePB& pred_pb : scan_pb.range_predicates()) {
    if (!pred_pb.has_lower_bound() && !pred_pb.has_upper_bound()) {
      return STATUS(InvalidArgument,
                    string("Invalid predicate ") + pred_pb.ShortDebugString() +
                    ": has no lower or upper bound.");
    }
    ColumnSchema col(ColumnSchemaFromPB(pred_pb.column()));
    if (projection.find_column(col.name()) == -1 &&
        !ContainsKey(missing_col_names, col.name())) {
      missing_cols->push_back(col);
      InsertOrDie(&missing_col_names, col.name());
    }

    const void* lower_bound = nullptr;
    const void* upper_bound = nullptr;
    if (pred_pb.has_lower_bound()) {
      const void* val;
      RETURN_NOT_OK(ExtractPredicateValue(col, pred_pb.lower_bound(),
                                          scanner->arena(),
                                          &val));
      lower_bound = val;
    } else {
      lower_bound = nullptr;
    }
    if (pred_pb.has_upper_bound()) {
      const void* val;
      RETURN_NOT_OK(ExtractPredicateValue(col, pred_pb.upper_bound(),
                                          scanner->arena(),
                                          &val));
      upper_bound = val;
    } else {
      upper_bound = nullptr;
    }

    ColumnRangePredicate pred(col, lower_bound, upper_bound);
    if (VLOG_IS_ON(3)) {
      VLOG(3) << "Parsed predicate " << pred.ToString() << " from " << scan_pb.ShortDebugString();
    }
    ret->AddPredicate(pred);
  }

  // When doing an ordered scan, we need to include the key columns to be able to encode
  // the last row key for the scan response.
  if (scan_pb.order_mode() == yb::ORDERED &&
      projection.num_key_columns() != tablet_schema.num_key_columns()) {
    for (int i = 0; i < tablet_schema.num_key_columns(); i++) {
      const ColumnSchema &col = tablet_schema.column(i);
      if (projection.find_column(col.name()) == -1 &&
          !ContainsKey(missing_col_names, col.name())) {
        missing_cols->push_back(col);
        InsertOrDie(&missing_col_names, col.name());
      }
    }
  }
  // Then any encoded key range predicates.
  RETURN_NOT_OK(DecodeEncodedKeyRange(scan_pb, tablet_schema, scanner, ret.get()));

  spec->swap(ret);
  return Status::OK();
}

namespace {

Result<boost::optional<TransactionId>> GetTransactionId(const ScanRequestPB &req) {
  boost::optional<TransactionId> txn_id;
  if (req.has_transaction_id()) {
    Result<TransactionId> txn_id_res = FullyDecodeTransactionId(req.transaction_id());
    RETURN_NOT_OK(txn_id_res);
    txn_id = *txn_id_res;
  } else {
    txn_id = boost::none;
  }
  return txn_id;
}

} // namespace

// Start a new scan.
Status TabletServiceImpl::HandleNewScanRequest(TabletPeer* tablet_peer,
                                               const ScanRequestPB* req,
                                               const RpcContext* rpc_context,
                                               ScanResultCollector* result_collector,
                                               std::string* scanner_id,
                                               HybridTime* snap_hybrid_time,
                                               bool* has_more_results,
                                               TabletServerErrorPB::Code* error_code) {
  DCHECK(result_collector != nullptr);
  DCHECK(error_code != nullptr);
  DCHECK(req->has_new_scan_request());
  VLOG(1) << "New scan request for " << tablet_peer->tablet_id()
          << " leader_only: "  << req->leader_only();
  if (req->leader_only()) {
    RETURN_NOT_OK(CheckPeerIsLeaderAndReady(*tablet_peer, error_code));
  }

  const NewScanRequestPB& scan_pb = req->new_scan_request();
  TRACE_EVENT1("tserver", "TabletServiceImpl::HandleNewScanRequest",
               "tablet_id", scan_pb.tablet_id());

  const Schema& tablet_schema = tablet_peer->tablet_metadata()->schema();

  SharedScanner scanner;
  server_->scanner_manager()->NewScanner(tablet_peer,
                                         rpc_context->requestor_string(),
                                         &scanner);

  // If we early-exit out of this function, automatically unregister
  // the scanner.
  ScopedUnregisterScanner unreg_scanner(server_->scanner_manager(), scanner->id());

  // Create the user's requested projection.
  // TODO: add test cases for bad projections including 0 columns
  Schema projection;
  Status s = ColumnPBsToSchema(scan_pb.projected_columns(), &projection);
  if (PREDICT_FALSE(!s.ok())) {
    *error_code = TabletServerErrorPB::INVALID_SCHEMA;
    return s;
  }

  if (projection.has_column_ids()) {
    *error_code = TabletServerErrorPB::INVALID_SCHEMA;
    return STATUS(InvalidArgument, "User requests should not have Column IDs");
  }

  gscoped_ptr<ScanSpec> spec;

  // Missing columns will contain the columns that are not mentioned in the client
  // projection but are actually needed for the scan, such as columns referred to by
  // predicates or key columns (if this is an ORDERED scan).
  std::vector<ColumnSchema> missing_cols;
  s = SetupScanSpec(scan_pb, tablet_schema, projection, &missing_cols, &spec, scanner);
  if (PREDICT_FALSE(!s.ok())) {
    *error_code = TabletServerErrorPB::INVALID_SCAN_SPEC;
    return s;
  }

  // Set the query_id from the request.
  spec->set_query_id(reinterpret_cast<int64_t>(req));

  // Store the original projection.
  gscoped_ptr<Schema> orig_projection(new Schema(projection));
  scanner->set_client_projection_schema(orig_projection.Pass());

  // Build a new projection with the projection columns and the missing columns. Make
  // sure to set whether the column is a key column appropriately.
  SchemaBuilder projection_builder;
  std::vector<ColumnSchema> projection_columns = projection.columns();
  for (const ColumnSchema& col : missing_cols) {
    projection_columns.push_back(col);
  }
  for (const ColumnSchema& col : projection_columns) {
    CHECK_OK(projection_builder.AddColumn(col, tablet_schema.is_key_column(col.name())));
  }
  projection = projection_builder.BuildWithoutIds();

  gscoped_ptr<RowwiseIterator> iter;
  // Preset the error code for when creating the iterator on the tablet fails
  TabletServerErrorPB::Code tmp_error_code = TabletServerErrorPB::MISMATCHED_SCHEMA;

  shared_ptr<Tablet> tablet;
  RETURN_NOT_OK(GetTabletRef(tablet_peer, &tablet, error_code));
  {
    TRACE("Creating iterator");
    TRACE_EVENT0("tserver", "Create iterator");

    Result<boost::optional<TransactionId>> txn_id = GetTransactionId(*req);
    if (!txn_id.ok()) {
      s = txn_id.status();
    } else {
      s = tablet->NewRowIterator(projection, *txn_id, &iter);
    }
    TRACE("Iterator created");
  }

  if (PREDICT_TRUE(s.ok())) {
    TRACE_EVENT0("tserver", "iter->Init");
    s = iter->Init(spec.get());
  }

  TRACE("Iterator init: $0", s.ToString());

  if (PREDICT_FALSE(s.IsInvalidArgument())) {
    // An invalid projection returns InvalidArgument above.
    // TODO: would be nice if we threaded these more specific
    // error codes throughout YB.
    *error_code = tmp_error_code;
    return s;
  } else if (PREDICT_FALSE(!s.ok())) {
    LOG(WARNING) << "Error setting up scanner with request " << req->ShortDebugString();
    *error_code = TabletServerErrorPB::UNKNOWN_ERROR;
    return s;
  }

  *has_more_results = iter->HasNext();
  TRACE("has_more: $0", *has_more_results);
  if (!*has_more_results) {
    // If there are no more rows, we can short circuit some work and respond immediately.
    VLOG(1) << "No more rows, short-circuiting out without creating a server-side scanner.";
    return Status::OK();
  }

  scanner->Init(iter.Pass(), spec.Pass());
  unreg_scanner.Cancel();
  *scanner_id = scanner->id();

  VLOG(1) << "Started scanner " << scanner->id() << ": " << scanner->iter()->ToString();

  size_t batch_size_bytes = GetMaxBatchSizeBytesHint(req);
  if (batch_size_bytes > 0) {
    TRACE("Continuing scan request");
    // TODO: instead of copying the pb, instead split HandleContinueScanRequest
    // and call the second half directly
    ScanRequestPB continue_req(*req);
    continue_req.set_scanner_id(scanner->id());
    RETURN_NOT_OK(HandleContinueScanRequest(&continue_req, result_collector, has_more_results,
                                            error_code));
  } else {
    // Increment the scanner call sequence ID. HandleContinueScanRequest handles
    // this in the non-empty scan case.
    scanner->IncrementCallSeqId();
  }
  return Status::OK();
}

// Continue an existing scan request.
Status TabletServiceImpl::HandleContinueScanRequest(const ScanRequestPB* req,
                                                    ScanResultCollector* result_collector,
                                                    bool* has_more_results,
                                                    TabletServerErrorPB::Code* error_code) {
  DCHECK(req->has_scanner_id());
  TRACE_EVENT1("tserver", "TabletServiceImpl::HandleContinueScanRequest",
               "scanner_id", req->scanner_id());

  size_t batch_size_bytes = GetMaxBatchSizeBytesHint(req);

  // TODO: need some kind of concurrency control on these scanner objects
  // in case multiple RPCs hit the same scanner at the same time. Probably
  // just a trylock and fail the RPC if it contends.
  SharedScanner scanner;
  if (!server_->scanner_manager()->LookupScanner(req->scanner_id(), &scanner)) {
    if (batch_size_bytes == 0 && req->close_scanner()) {
      // A request to close a non-existent scanner.
      return Status::OK();
    } else {
      *error_code = TabletServerErrorPB::SCANNER_EXPIRED;
      return STATUS(NotFound, "Scanner not found");
    }
  }

  // If we early-exit out of this function, automatically unregister the scanner.
  ScopedUnregisterScanner unreg_scanner(server_->scanner_manager(), scanner->id());

  VLOG(2) << "Found existing scanner " << scanner->id() << " for request: "
          << req->ShortDebugString();
  TRACE("Found scanner $0", scanner->id());

  if (batch_size_bytes == 0 && req->close_scanner()) {
    *has_more_results = false;
    return Status::OK();
  }

  if (req->call_seq_id() != scanner->call_seq_id()) {
    *error_code = TabletServerErrorPB::INVALID_SCAN_CALL_SEQ_ID;
    return STATUS(InvalidArgument, "Invalid call sequence ID in scan request");
  }
  scanner->IncrementCallSeqId();
  scanner->UpdateAccessTime();

  RowwiseIterator* iter = scanner->iter();

  // TODO: could size the RowBlock based on the user's requested batch size?
  // If people had really large indirect objects, we would currently overshoot
  // their requested batch size by a lot.
  Arena arena(32 * 1024, 1 * 1024 * 1024);
  RowBlock block(scanner->iter()->schema(),
                 FLAGS_scanner_batch_size_rows, &arena);

  // TODO: in the future, use the client timeout to set a budget. For now,
  // just use a half second, which should be plenty to amortize call overhead.
  const auto kBudget = 500ms;
  auto deadline = CoarseMonoClock::Now() + kBudget;

  int64_t rows_scanned = 0;
  while (iter->HasNext()) {
    if (PREDICT_FALSE(FLAGS_scanner_inject_latency_on_each_batch_ms > 0)) {
      SleepFor(MonoDelta::FromMilliseconds(FLAGS_scanner_inject_latency_on_each_batch_ms));
    }

    Status s = iter->NextBlock(&block);
    if (PREDICT_FALSE(!s.ok())) {
      LOG(WARNING) << "Copying rows from internal iterator for request " << req->ShortDebugString();
      *error_code = TabletServerErrorPB::UNKNOWN_ERROR;
      return s;
    }

    if (PREDICT_TRUE(block.nrows() > 0)) {
      // Count the number of rows scanned, regardless of predicates or deletions.
      // The collector will separately count the number of rows actually returned to
      // the client.
      rows_scanned += block.nrows();
      result_collector->HandleRowBlock(scanner->client_projection_schema(), block);
    }

    int64_t response_size = result_collector->ResponseSize();

    if (VLOG_IS_ON(2)) {
      // This may be fairly expensive if row block size is small
      TRACE("Copied block (nrows=$0), new size=$1", block.nrows(), response_size);
    }

    // TODO: should check if RPC got cancelled, once we implement RPC cancellation.
    auto now = CoarseMonoClock::Now();
    if (PREDICT_FALSE(now >= deadline)) {
      TRACE("Deadline expired - responding early");
      break;
    }

    if (response_size >= batch_size_bytes) {
      break;
    }
  }

  // Update metrics based on this scan request.
  scoped_refptr<TabletPeer> tablet_peer = scanner->tablet_peer();
  shared_ptr<Tablet> tablet;
  RETURN_NOT_OK(GetTabletRef(tablet_peer, &tablet, error_code));

  // First, the number of rows/cells/bytes actually returned to the user.
  tablet->metrics()->scanner_rows_returned->IncrementBy(
      result_collector->NumRowsReturned());
  tablet->metrics()->scanner_cells_returned->IncrementBy(
      result_collector->NumRowsReturned() * scanner->client_projection_schema()->num_columns());
  tablet->metrics()->scanner_bytes_returned->IncrementBy(
      result_collector->ResponseSize());

  // Then the number of rows/cells/bytes actually processed. Here we have to dig
  // into the per-column iterator stats, sum them up, and then subtract out the
  // total that we already reported in a previous scan.
  vector<IteratorStats> stats_by_col;
  scanner->GetIteratorStats(&stats_by_col);
  IteratorStats total_stats;
  for (const IteratorStats& stats : stats_by_col) {
    total_stats.AddStats(stats);
  }
  IteratorStats delta_stats = total_stats;
  delta_stats.SubtractStats(scanner->already_reported_stats());
  scanner->set_already_reported_stats(total_stats);

  tablet->metrics()->scanner_rows_scanned->IncrementBy(
      rows_scanned);
  tablet->metrics()->scanner_cells_scanned_from_disk->IncrementBy(
      delta_stats.cells_read_from_disk);
  tablet->metrics()->scanner_bytes_scanned_from_disk->IncrementBy(
      delta_stats.bytes_read_from_disk);

  scanner->UpdateAccessTime();
  *has_more_results = !req->close_scanner() && iter->HasNext();
  if (*has_more_results) {
    unreg_scanner.Cancel();
  } else {
    VLOG(2) << "Scanner " << scanner->id() << " complete: removing...";
  }

  return Status::OK();
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
