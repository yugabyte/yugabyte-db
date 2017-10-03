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

#include "kudu/tserver/tablet_service.h"

#include <algorithm>
#include <boost/optional.hpp>
#include <memory>
#include <string>
#include <vector>

#include "kudu/common/iterator.h"
#include "kudu/common/schema.h"
#include "kudu/common/wire_protocol.h"
#include "kudu/consensus/consensus.h"
#include "kudu/gutil/bind.h"
#include "kudu/gutil/casts.h"
#include "kudu/gutil/stl_util.h"
#include "kudu/gutil/stringprintf.h"
#include "kudu/gutil/strings/escaping.h"
#include "kudu/rpc/rpc_context.h"
#include "kudu/rpc/rpc_sidecar.h"
#include "kudu/server/hybrid_clock.h"
#include "kudu/tablet/tablet_bootstrap.h"
#include "kudu/tserver/remote_bootstrap_service.h"
#include "kudu/tablet/metadata.pb.h"
#include "kudu/tablet/tablet_peer.h"
#include "kudu/tablet/tablet_metrics.h"
#include "kudu/tablet/transactions/alter_schema_transaction.h"
#include "kudu/tablet/transactions/write_transaction.h"
#include "kudu/tserver/scanners.h"
#include "kudu/tserver/tablet_server.h"
#include "kudu/tserver/ts_tablet_manager.h"
#include "kudu/tserver/tserver.pb.h"
#include "kudu/util/crc.h"
#include "kudu/util/debug/trace_event.h"
#include "kudu/util/faststring.h"
#include "kudu/util/flag_tags.h"
#include "kudu/util/logging.h"
#include "kudu/util/mem_tracker.h"
#include "kudu/util/monotime.h"
#include "kudu/util/status.h"
#include "kudu/util/status_callback.h"
#include "kudu/util/trace.h"

DEFINE_int32(scanner_default_batch_size_bytes, 1024 * 1024,
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

namespace kudu {
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
using consensus::RunLeaderElectionRequestPB;
using consensus::RunLeaderElectionResponsePB;
using consensus::StartRemoteBootstrapRequestPB;
using consensus::StartRemoteBootstrapResponsePB;
using consensus::VoteRequestPB;
using consensus::VoteResponsePB;

using google::protobuf::RepeatedPtrField;
using rpc::RpcContext;
using std::shared_ptr;
using std::vector;
using strings::Substitute;
using tablet::AlterSchemaTransactionState;
using tablet::Tablet;
using tablet::TabletPeer;
using tablet::TabletStatusPB;
using tablet::TransactionCompletionCallback;
using tablet::WriteTransactionState;

namespace {

// Lookup the given tablet, ensuring that it both exists and is RUNNING.
// If it is not, responds to the RPC associated with 'context' after setting
// resp->mutable_error() to indicate the failure reason.
//
// Returns true if successful.
template<class RespClass>
bool LookupTabletPeerOrRespond(TabletPeerLookupIf* tablet_manager,
                               const string& tablet_id,
                               RespClass* resp,
                               rpc::RpcContext* context,
                               scoped_refptr<TabletPeer>* peer) {
  if (PREDICT_FALSE(!tablet_manager->GetTabletPeer(tablet_id, peer).ok())) {
    SetupErrorAndRespond(resp->mutable_error(),
                         Status::NotFound("Tablet not found"),
                         TabletServerErrorPB::TABLET_NOT_FOUND, context);
    return false;
  }

  // Check RUNNING state.
  tablet::TabletStatePB state = (*peer)->state();
  if (PREDICT_FALSE(state != tablet::RUNNING)) {
    Status s = Status::IllegalState("Tablet not RUNNING",
                                    tablet::TabletStatePB_Name(state));
    if (state == tablet::FAILED) {
      s = s.CloneAndAppend((*peer)->error().ToString());
    }
    SetupErrorAndRespond(resp->mutable_error(), s,
                         TabletServerErrorPB::TABLET_NOT_RUNNING, context);
    return false;
  }
  return true;
}

template<class ReqClass, class RespClass>
bool CheckUuidMatchOrRespond(TabletPeerLookupIf* tablet_manager,
                             const char* method_name,
                             const ReqClass* req,
                             RespClass* resp,
                             rpc::RpcContext* context) {
  const string& local_uuid = tablet_manager->NodeInstance().permanent_uuid();
  if (PREDICT_FALSE(!req->has_dest_uuid())) {
    // Maintain compat in release mode, but complain.
    string msg = Substitute("$0: Missing destination UUID in request from $1: $2",
                            method_name, context->requestor_string(), req->ShortDebugString());
#ifdef NDEBUG
    KLOG_EVERY_N(ERROR, 100) << msg;
#else
    LOG(DFATAL) << msg;
#endif
    return true;
  }
  if (PREDICT_FALSE(req->dest_uuid() != local_uuid)) {
    Status s = Status::InvalidArgument(Substitute("$0: Wrong destination UUID requested. "
                                                  "Local UUID: $1. Requested UUID: $2",
                                                  method_name, local_uuid, req->dest_uuid()));
    LOG(WARNING) << s.ToString() << ": from " << context->requestor_string()
                 << ": " << req->ShortDebugString();
    SetupErrorAndRespond(resp->mutable_error(), s,
                         TabletServerErrorPB::WRONG_SERVER_UUID, context);
    return false;
  }
  return true;
}

template<class RespClass>
bool GetConsensusOrRespond(const scoped_refptr<TabletPeer>& tablet_peer,
                           RespClass* resp,
                           rpc::RpcContext* context,
                           scoped_refptr<Consensus>* consensus) {
  *consensus = tablet_peer->shared_consensus();
  if (!*consensus) {
    Status s = Status::ServiceUnavailable("Consensus unavailable. Tablet not running");
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
    return Status::IllegalState("Tablet is not running");
  }
  return Status::OK();
}

template <class RespType>
void HandleUnknownError(const Status& s, RespType* resp, RpcContext* context) {
  resp->Clear();
  SetupErrorAndRespond(resp->mutable_error(), s,
                       TabletServerErrorPB::UNKNOWN_ERROR,
                       context);
}

template <class ReqType, class RespType>
void HandleResponse(const ReqType* req, RespType* resp,
                    RpcContext* context, const Status& s) {
  if (PREDICT_FALSE(!s.ok())) {
    HandleUnknownError(s, resp, context);
    return;
  }
  context->RespondSuccess();
}

template <class ReqType, class RespType>
static StatusCallback BindHandleResponse(const ReqType* req, RespType* resp, RpcContext* context) {
  return Bind(&HandleResponse<ReqType, RespType>, req, resp, context);
}

} // namespace

typedef ListTabletsResponsePB::StatusAndSchemaPB StatusAndSchemaPB;

static void SetupErrorAndRespond(TabletServerErrorPB* error,
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

template <class ReqType, class RespType>
void HandleErrorResponse(const ReqType* req, RespType* resp, RpcContext* context,
                         const boost::optional<TabletServerErrorPB::Code>& error_code,
                         const Status& s) {
  resp->Clear();
  if (error_code) {
    SetupErrorAndRespond(resp->mutable_error(), s, *error_code, context);
  } else {
    HandleUnknownError(s, resp, context);
  }
}

// A transaction completion callback that responds to the client when transactions
// complete and sets the client error if there is one to set.
template<class Response>
class RpcTransactionCompletionCallback : public TransactionCompletionCallback {
 public:
  RpcTransactionCompletionCallback(rpc::RpcContext* context,
                                   Response* response)
 : context_(context),
   response_(response) {}

  virtual void TransactionCompleted() OVERRIDE {
    if (!status_.ok()) {
      SetupErrorAndRespond(get_error(), status_, code_, context_);
    } else {
      context_->RespondSuccess();
    }
  };

 private:

  TabletServerErrorPB* get_error() {
    return response_->mutable_error();
  }

  rpc::RpcContext* context_;
  Response* response_;
  tablet::TransactionState* state_;
};

// Generic interface to handle scan results.
class ScanResultCollector {
 public:
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
                              const RowBlock& row_block) OVERRIDE {
    blocks_processed_++;
    num_rows_returned_ += row_block.selection_vector()->CountSelected();
    SerializeRowBlock(row_block, rowblock_pb_, client_projection_schema,
                      rows_data_, indirect_data_);
    SetLastRow(row_block, &last_primary_key_);
  }

  virtual int BlocksProcessed() const OVERRIDE { return blocks_processed_; }

  // Returns number of bytes buffered to return.
  virtual int64_t ResponseSize() const OVERRIDE {
    return rows_data_->size() + indirect_data_->size();
  }

  virtual const faststring& last_primary_key() const OVERRIDE {
    return last_primary_key_;
  }

  virtual int64_t NumRowsReturned() const OVERRIDE {
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
                              const RowBlock& row_block) OVERRIDE {
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

  virtual int BlocksProcessed() const OVERRIDE { return blocks_processed_; }

  // Returns a constant -- we only return checksum based on a time budget.
  virtual int64_t ResponseSize() const OVERRIDE { return sizeof(agg_checksum_); }

  virtual const faststring& last_primary_key() const OVERRIDE { return encoded_last_row_; }

  virtual int64_t NumRowsReturned() const OVERRIDE {
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
    return static_cast<uint32_t>(row_crc); // CRC32 only uses the lower 32 bits.
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

TabletServiceImpl::TabletServiceImpl(TabletServer* server)
  : TabletServerServiceIf(server->metric_entity()),
    server_(server) {
}

void TabletServiceImpl::Ping(const PingRequestPB* req,
                             PingResponsePB* resp,
                             rpc::RpcContext* context) {
  context->RespondSuccess();
}

TabletServiceAdminImpl::TabletServiceAdminImpl(TabletServer* server)
  : TabletServerAdminServiceIf(server->metric_entity()),
    server_(server) {
}

void TabletServiceAdminImpl::AlterSchema(const AlterSchemaRequestPB* req,
                                         AlterSchemaResponsePB* resp,
                                         rpc::RpcContext* context) {
  if (!CheckUuidMatchOrRespond(server_->tablet_manager(), "AlterSchema", req, resp, context)) {
    return;
  }
  DVLOG(3) << "Received Alter Schema RPC: " << req->DebugString();

  scoped_refptr<TabletPeer> tablet_peer;
  if (!LookupTabletPeerOrRespond(server_->tablet_manager(), req->tablet_id(), resp, context,
                                 &tablet_peer)) {
    return;
  }

  uint32_t schema_version = tablet_peer->tablet_metadata()->schema_version();

  // If the schema was already applied, respond as succeded
  if (schema_version == req->schema_version()) {
    // Sanity check, to verify that the tablet should have the same schema
    // specified in the request.
    Schema req_schema;
    Status s = SchemaFromPB(req->schema(), &req_schema);
    if (!s.ok()) {
      SetupErrorAndRespond(resp->mutable_error(), s,
                           TabletServerErrorPB::INVALID_SCHEMA, context);
      return;
    }

    Schema tablet_schema = tablet_peer->tablet_metadata()->schema();
    if (req_schema.Equals(tablet_schema)) {
      context->RespondSuccess();
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
                           Status::Corruption("got a different schema for the same version number"),
                           TabletServerErrorPB::MISMATCHED_SCHEMA, context);
      return;
    }
  }

  // If the current schema is newer than the one in the request reject the request.
  if (schema_version > req->schema_version()) {
    SetupErrorAndRespond(resp->mutable_error(),
                         Status::InvalidArgument("Tablet has a newer schema"),
                         TabletServerErrorPB::TABLET_HAS_A_NEWER_SCHEMA, context);
    return;
  }

  gscoped_ptr<AlterSchemaTransactionState> tx_state(
    new AlterSchemaTransactionState(tablet_peer.get(), req, resp));

  tx_state->set_completion_callback(gscoped_ptr<TransactionCompletionCallback>(
      new RpcTransactionCompletionCallback<AlterSchemaResponsePB>(context,
                                                                  resp)).Pass());

  // Submit the alter schema op. The RPC will be responded to asynchronously.
  Status s = tablet_peer->SubmitAlterSchema(tx_state.Pass());
  if (PREDICT_FALSE(!s.ok())) {
    SetupErrorAndRespond(resp->mutable_error(), s,
                         TabletServerErrorPB::UNKNOWN_ERROR,
                         context);
    return;
  }
}

void TabletServiceAdminImpl::CreateTablet(const CreateTabletRequestPB* req,
                                          CreateTabletResponsePB* resp,
                                          rpc::RpcContext* context) {
  if (!CheckUuidMatchOrRespond(server_->tablet_manager(), "CreateTablet", req, resp, context)) {
    return;
  }
  TRACE_EVENT1("tserver", "CreateTablet",
               "tablet_id", req->tablet_id());

  Schema schema;
  Status s = SchemaFromPB(req->schema(), &schema);
  DCHECK(schema.has_column_ids());
  if (!s.ok()) {
    SetupErrorAndRespond(resp->mutable_error(),
                         Status::InvalidArgument("Invalid Schema."),
                         TabletServerErrorPB::INVALID_SCHEMA, context);
    return;
  }

  PartitionSchema partition_schema;
  s = PartitionSchema::FromPB(req->partition_schema(), schema, &partition_schema);
  if (!s.ok()) {
    SetupErrorAndRespond(resp->mutable_error(),
                         Status::InvalidArgument("Invalid PartitionSchema."),
                         TabletServerErrorPB::INVALID_SCHEMA, context);
    return;
  }

  Partition partition;
  Partition::FromPB(req->partition(), &partition);

  LOG(INFO) << "Processing CreateTablet for tablet " << req->tablet_id()
            << " (table=" << req->table_name()
            << " [id=" << req->table_id() << "]), partition="
            << partition_schema.PartitionDebugString(partition, schema);
  VLOG(1) << "Full request: " << req->DebugString();

  s = server_->tablet_manager()->CreateNewTablet(req->table_id(),
                                                 req->tablet_id(),
                                                 partition,
                                                 req->table_name(),
                                                 schema,
                                                 partition_schema,
                                                 req->config(),
                                                 nullptr);
  if (PREDICT_FALSE(!s.ok())) {
    TabletServerErrorPB::Code code;
    if (s.IsAlreadyPresent()) {
      code = TabletServerErrorPB::TABLET_ALREADY_EXISTS;
    } else {
      code = TabletServerErrorPB::UNKNOWN_ERROR;
    }
    SetupErrorAndRespond(resp->mutable_error(), s, code, context);
    return;
  }
  context->RespondSuccess();
}

void TabletServiceAdminImpl::DeleteTablet(const DeleteTabletRequestPB* req,
                                          DeleteTabletResponsePB* resp,
                                          rpc::RpcContext* context) {
  if (!CheckUuidMatchOrRespond(server_->tablet_manager(), "DeleteTablet", req, resp, context)) {
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
            << " from " << context->requestor_string();
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
    HandleErrorResponse(req, resp, context, error_code, s);
    return;
  }
  context->RespondSuccess();
}

void TabletServiceImpl::Write(const WriteRequestPB* req,
                              WriteResponsePB* resp,
                              rpc::RpcContext* context) {
  TRACE_EVENT1("tserver", "TabletServiceImpl::Write",
               "tablet_id", req->tablet_id());
  DVLOG(3) << "Received Write RPC: " << req->DebugString();

  scoped_refptr<TabletPeer> tablet_peer;
  if (!LookupTabletPeerOrRespond(server_->tablet_manager(), req->tablet_id(), resp, context,
                                 &tablet_peer)) {
    return;
  }

  shared_ptr<Tablet> tablet;
  TabletServerErrorPB::Code error_code;
  Status s = GetTabletRef(tablet_peer, &tablet, &error_code);
  if (PREDICT_FALSE(!s.ok())) {
    SetupErrorAndRespond(resp->mutable_error(), s, error_code, context);
    return;
  }

  // Check for memory pressure; don't bother doing any additional work if we've
  // exceeded the limit.
  double capacity_pct;
  if (tablet->mem_tracker()->AnySoftLimitExceeded(&capacity_pct)) {
    tablet->metrics()->leader_memory_pressure_rejections->Increment();
    string msg = StringPrintf(
        "Soft memory limit exceeded (at %.2f%% of capacity)",
        capacity_pct);
    if (capacity_pct >= FLAGS_memory_limit_warn_threshold_percentage) {
      KLOG_EVERY_N_SECS(WARNING, 1) << "Rejecting Write request: " << msg << THROTTLE_MSG;
    } else {
      KLOG_EVERY_N_SECS(INFO, 1) << "Rejecting Write request: " << msg << THROTTLE_MSG;
    }
    SetupErrorAndRespond(resp->mutable_error(), Status::ServiceUnavailable(msg),
                         TabletServerErrorPB::UNKNOWN_ERROR,
                         context);
    return;
  }

  if (!server_->clock()->SupportsExternalConsistencyMode(req->external_consistency_mode())) {
    Status s = Status::NotSupported("The configured clock does not support the"
        " required consistency mode.");
    SetupErrorAndRespond(resp->mutable_error(), s,
                         TabletServerErrorPB::UNKNOWN_ERROR,
                         context);
    return;
  }

  auto tx_state = new WriteTransactionState(tablet_peer.get(), req, resp);

  // If the client sent us a timestamp, decode it and update the clock so that all future
  // timestamps are greater than the passed timestamp.
  if (req->has_propagated_timestamp()) {
    Timestamp ts(req->propagated_timestamp());
    s = server_->clock()->Update(ts);
  }
  if (PREDICT_FALSE(!s.ok())) {
    SetupErrorAndRespond(resp->mutable_error(), s,
                         TabletServerErrorPB::UNKNOWN_ERROR,
                         context);
    return;
  }

  tx_state->set_completion_callback(gscoped_ptr<TransactionCompletionCallback>(
      new RpcTransactionCompletionCallback<WriteResponsePB>(context,
                                                            resp)).Pass());

  // Submit the write. The RPC will be responded to asynchronously.
  s = tablet_peer->SubmitWrite(tx_state);

  // Check that we could submit the write
  if (PREDICT_FALSE(!s.ok())) {
    SetupErrorAndRespond(resp->mutable_error(), s,
                         TabletServerErrorPB::UNKNOWN_ERROR,
                         context);
  }
  return;
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
                                           rpc::RpcContext* context) {
  DVLOG(3) << "Received Consensus Update RPC: " << req->DebugString();
  if (!CheckUuidMatchOrRespond(tablet_manager_, "UpdateConsensus", req, resp, context)) {
    return;
  }
  scoped_refptr<TabletPeer> tablet_peer;
  if (!LookupTabletPeerOrRespond(tablet_manager_, req->tablet_id(), resp, context, &tablet_peer)) {
    return;
  }

  tablet_peer->permanent_uuid();

  // Submit the update directly to the TabletPeer's Consensus instance.
  scoped_refptr<Consensus> consensus;
  if (!GetConsensusOrRespond(tablet_peer, resp, context, &consensus)) return;
  Status s = consensus->Update(req, resp);
  if (PREDICT_FALSE(!s.ok())) {
    // Clear the response first, since a partially-filled response could
    // result in confusing a caller, or in having missing required fields
    // in embedded optional messages.
    resp->Clear();

    SetupErrorAndRespond(resp->mutable_error(), s,
                         TabletServerErrorPB::UNKNOWN_ERROR,
                         context);
    return;
  }
  context->RespondSuccess();
}

void ConsensusServiceImpl::RequestConsensusVote(const VoteRequestPB* req,
                                                VoteResponsePB* resp,
                                                rpc::RpcContext* context) {
  DVLOG(3) << "Received Consensus Request Vote RPC: " << req->DebugString();
  if (!CheckUuidMatchOrRespond(tablet_manager_, "RequestConsensusVote", req, resp, context)) {
    return;
  }
  scoped_refptr<TabletPeer> tablet_peer;
  if (!LookupTabletPeerOrRespond(tablet_manager_, req->tablet_id(), resp, context, &tablet_peer)) {
    return;
  }

  // Submit the vote request directly to the consensus instance.
  scoped_refptr<Consensus> consensus;
  if (!GetConsensusOrRespond(tablet_peer, resp, context, &consensus)) return;
  Status s = consensus->RequestVote(req, resp);
  if (PREDICT_FALSE(!s.ok())) {
    SetupErrorAndRespond(resp->mutable_error(), s,
                         TabletServerErrorPB::UNKNOWN_ERROR,
                         context);
    return;
  }
  context->RespondSuccess();
}

void ConsensusServiceImpl::ChangeConfig(const ChangeConfigRequestPB* req,
                                        ChangeConfigResponsePB* resp,
                                        RpcContext* context) {
  DVLOG(3) << "Received ChangeConfig RPC: " << req->DebugString();
  if (!CheckUuidMatchOrRespond(tablet_manager_, "ChangeConfig", req, resp, context)) {
    return;
  }
  scoped_refptr<TabletPeer> tablet_peer;
  if (!LookupTabletPeerOrRespond(tablet_manager_, req->tablet_id(), resp, context,
                                 &tablet_peer)) {
    return;
  }

  scoped_refptr<Consensus> consensus;
  if (!GetConsensusOrRespond(tablet_peer, resp, context, &consensus)) return;
  boost::optional<TabletServerErrorPB::Code> error_code;
  Status s = consensus->ChangeConfig(*req, BindHandleResponse(req, resp, context), &error_code);
  if (PREDICT_FALSE(!s.ok())) {
    HandleErrorResponse(req, resp, context, error_code, s);
    return;
  }
  // The success case is handled when the callback fires.
}

void ConsensusServiceImpl::GetNodeInstance(const GetNodeInstanceRequestPB* req,
                                           GetNodeInstanceResponsePB* resp,
                                           rpc::RpcContext* context) {
  DVLOG(3) << "Received Get Node Instance RPC: " << req->DebugString();
  resp->mutable_node_instance()->CopyFrom(tablet_manager_->NodeInstance());
  context->RespondSuccess();
}

void ConsensusServiceImpl::RunLeaderElection(const RunLeaderElectionRequestPB* req,
                                             RunLeaderElectionResponsePB* resp,
                                             rpc::RpcContext* context) {
  DVLOG(3) << "Received Run Leader Election RPC: " << req->DebugString();
  if (!CheckUuidMatchOrRespond(tablet_manager_, "RunLeaderElection", req, resp, context)) {
    return;
  }
  scoped_refptr<TabletPeer> tablet_peer;
  if (!LookupTabletPeerOrRespond(tablet_manager_, req->tablet_id(), resp, context, &tablet_peer)) {
    return;
  }

  scoped_refptr<Consensus> consensus;
  if (!GetConsensusOrRespond(tablet_peer, resp, context, &consensus)) return;
  Status s = consensus->StartElection(
      consensus::Consensus::ELECT_EVEN_IF_LEADER_IS_ALIVE);
  if (PREDICT_FALSE(!s.ok())) {
    SetupErrorAndRespond(resp->mutable_error(), s,
                         TabletServerErrorPB::UNKNOWN_ERROR,
                         context);
    return;
  }
  context->RespondSuccess();
}

void ConsensusServiceImpl::LeaderStepDown(const LeaderStepDownRequestPB* req,
                                          LeaderStepDownResponsePB* resp,
                                          RpcContext* context) {
  DVLOG(3) << "Received Leader stepdown RPC: " << req->DebugString();
  if (!CheckUuidMatchOrRespond(tablet_manager_, "LeaderStepDown", req, resp, context)) {
    return;
  }
  scoped_refptr<TabletPeer> tablet_peer;
  if (!LookupTabletPeerOrRespond(tablet_manager_, req->tablet_id(), resp, context, &tablet_peer)) {
    return;
  }

  scoped_refptr<Consensus> consensus;
  if (!GetConsensusOrRespond(tablet_peer, resp, context, &consensus)) return;
  Status s = consensus->StepDown(resp);
  if (PREDICT_FALSE(!s.ok())) {
    SetupErrorAndRespond(resp->mutable_error(), s,
                         TabletServerErrorPB::UNKNOWN_ERROR,
                         context);
    return;
  }
  context->RespondSuccess();
}

void ConsensusServiceImpl::GetLastOpId(const consensus::GetLastOpIdRequestPB *req,
                                       consensus::GetLastOpIdResponsePB *resp,
                                       rpc::RpcContext *context) {
  DVLOG(3) << "Received GetLastOpId RPC: " << req->DebugString();
  if (!CheckUuidMatchOrRespond(tablet_manager_, "GetLastOpId", req, resp, context)) {
    return;
  }
  scoped_refptr<TabletPeer> tablet_peer;
  if (!LookupTabletPeerOrRespond(tablet_manager_, req->tablet_id(), resp, context, &tablet_peer)) {
    return;
  }

  if (tablet_peer->state() != tablet::RUNNING) {
    SetupErrorAndRespond(resp->mutable_error(),
                         Status::ServiceUnavailable("Tablet Peer not in RUNNING state"),
                         TabletServerErrorPB::TABLET_NOT_RUNNING, context);
    return;
  }
  scoped_refptr<Consensus> consensus;
  if (!GetConsensusOrRespond(tablet_peer, resp, context, &consensus)) return;
  if (PREDICT_FALSE(req->opid_type() == consensus::UNKNOWN_OPID_TYPE)) {
    HandleUnknownError(Status::InvalidArgument("Invalid opid_type specified to GetLastOpId()"),
                       resp, context);
    return;
  }
  Status s = consensus->GetLastOpId(req->opid_type(), resp->mutable_opid());
  if (PREDICT_FALSE(!s.ok())) {
    SetupErrorAndRespond(resp->mutable_error(), s,
                         TabletServerErrorPB::UNKNOWN_ERROR,
                         context);
    return;
  }
  context->RespondSuccess();
}

void ConsensusServiceImpl::GetConsensusState(const consensus::GetConsensusStateRequestPB *req,
                                             consensus::GetConsensusStateResponsePB *resp,
                                             rpc::RpcContext *context) {
  DVLOG(3) << "Received GetConsensusState RPC: " << req->DebugString();
  if (!CheckUuidMatchOrRespond(tablet_manager_, "GetConsensusState", req, resp, context)) {
    return;
  }
  scoped_refptr<TabletPeer> tablet_peer;
  if (!LookupTabletPeerOrRespond(tablet_manager_, req->tablet_id(), resp, context, &tablet_peer)) {
    return;
  }

  scoped_refptr<Consensus> consensus;
  if (!GetConsensusOrRespond(tablet_peer, resp, context, &consensus)) return;
  ConsensusConfigType type = req->type();
  if (PREDICT_FALSE(type != CONSENSUS_CONFIG_ACTIVE && type != CONSENSUS_CONFIG_COMMITTED)) {
    HandleUnknownError(
        Status::InvalidArgument(Substitute("Unsupported ConsensusConfigType $0 ($1)",
                                           ConsensusConfigType_Name(type), type)),
        resp, context);
    return;
  }
  *resp->mutable_cstate() = consensus->ConsensusState(req->type());
  context->RespondSuccess();
}

void ConsensusServiceImpl::StartRemoteBootstrap(const StartRemoteBootstrapRequestPB* req,
                                                StartRemoteBootstrapResponsePB* resp,
                                                rpc::RpcContext* context) {
  if (!CheckUuidMatchOrRespond(tablet_manager_, "StartRemoteBootstrap", req, resp, context)) {
    return;
  }
  Status s = tablet_manager_->StartRemoteBootstrap(*req);
  if (!s.ok()) {
    SetupErrorAndRespond(resp->mutable_error(), s,
                         TabletServerErrorPB::UNKNOWN_ERROR,
                         context);
    return;
  }
  context->RespondSuccess();
}

void TabletServiceImpl::ScannerKeepAlive(const ScannerKeepAliveRequestPB *req,
                                         ScannerKeepAliveResponsePB *resp,
                                         rpc::RpcContext *context) {
  DCHECK(req->has_scanner_id());
  SharedScanner scanner;
  if (!server_->scanner_manager()->LookupScanner(req->scanner_id(), &scanner)) {
      resp->mutable_error()->set_code(TabletServerErrorPB::SCANNER_EXPIRED);
      StatusToPB(Status::NotFound("Scanner not found"),
                 resp->mutable_error()->mutable_status());
      return;
  }
  scanner->UpdateAccessTime();
  context->RespondSuccess();
}


void TabletServiceImpl::Scan(const ScanRequestPB* req,
                             ScanResponsePB* resp,
                             rpc::RpcContext* context) {
  TRACE_EVENT0("tserver", "TabletServiceImpl::Scan");
  // Validate the request: user must pass a new_scan_request or
  // a scanner ID, but not both.
  if (PREDICT_FALSE(req->has_scanner_id() &&
                    req->has_new_scan_request())) {
    context->RespondFailure(Status::InvalidArgument(
                            "Must not pass both a scanner_id and new_scan_request"));
    return;
  }

  size_t batch_size_bytes = GetMaxBatchSizeBytesHint(req);
  gscoped_ptr<faststring> rows_data(new faststring(batch_size_bytes * 11 / 10));
  gscoped_ptr<faststring> indirect_data(new faststring(batch_size_bytes * 11 / 10));
  RowwiseRowBlockPB data;
  ScanResultCopier collector(&data, rows_data.get(), indirect_data.get());

  bool has_more_results = false;
  TabletServerErrorPB::Code error_code;
  if (req->has_new_scan_request()) {
    const NewScanRequestPB& scan_pb = req->new_scan_request();
    scoped_refptr<TabletPeer> tablet_peer;
    if (!LookupTabletPeerOrRespond(server_->tablet_manager(), scan_pb.tablet_id(), resp, context,
                                   &tablet_peer)) {
      return;
    }
    string scanner_id;
    Timestamp scan_timestamp;
    Status s = HandleNewScanRequest(tablet_peer.get(), req, context,
                                    &collector, &scanner_id, &scan_timestamp, &has_more_results,
                                    &error_code);
    if (PREDICT_FALSE(!s.ok())) {
      SetupErrorAndRespond(resp->mutable_error(), s, error_code, context);
      return;
    }

    // Only set the scanner id if we have more results.
    if (has_more_results) {
      resp->set_scanner_id(scanner_id);
    }
    if (scan_timestamp != Timestamp::kInvalidTimestamp) {
      resp->set_snap_timestamp(scan_timestamp.ToUint64());
    }
  } else if (req->has_scanner_id()) {
    Status s = HandleContinueScanRequest(req, &collector, &has_more_results, &error_code);
    if (PREDICT_FALSE(!s.ok())) {
      SetupErrorAndRespond(resp->mutable_error(), s, error_code, context);
      return;
    }
  } else {
    context->RespondFailure(Status::InvalidArgument(
                              "Must pass either a scanner_id or new_scan_request"));
    return;
  }
  resp->set_has_more_results(has_more_results);

  DVLOG(2) << "Blocks processed: " << collector.BlocksProcessed();
  if (collector.BlocksProcessed() > 0) {
    resp->mutable_data()->CopyFrom(data);

    // Add sidecar data to context and record the returned indices.
    int rows_idx;
    CHECK_OK(context->AddRpcSidecar(make_gscoped_ptr(
        new rpc::RpcSidecar(rows_data.Pass())), &rows_idx));
    resp->mutable_data()->set_rows_sidecar(rows_idx);

    // Add indirect data as a sidecar, if applicable.
    if (indirect_data->size() > 0) {
      int indirect_idx;
      CHECK_OK(context->AddRpcSidecar(make_gscoped_ptr(
          new rpc::RpcSidecar(indirect_data.Pass())), &indirect_idx));
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

  context->RespondSuccess();
}

void TabletServiceImpl::ListTablets(const ListTabletsRequestPB* req,
                                    ListTabletsResponsePB* resp,
                                    rpc::RpcContext* context) {
  vector<scoped_refptr<TabletPeer> > peers;
  server_->tablet_manager()->GetTabletPeers(&peers);
  RepeatedPtrField<StatusAndSchemaPB>* peer_status = resp->mutable_status_and_schema();
  for (const scoped_refptr<TabletPeer>& peer : peers) {
    StatusAndSchemaPB* status = peer_status->Add();
    peer->GetTabletStatusPB(status->mutable_tablet_status());
    CHECK_OK(SchemaToPB(peer->status_listener()->schema(),
                        status->mutable_schema()));
    peer->tablet_metadata()->partition_schema().ToPB(status->mutable_partition_schema());
  }
  context->RespondSuccess();
}

void TabletServiceImpl::Checksum(const ChecksumRequestPB* req,
                                 ChecksumResponsePB* resp,
                                 rpc::RpcContext* context) {
  VLOG(1) << "Full request: " << req->DebugString();

  // Validate the request: user must pass a new_scan_request or
  // a scanner ID, but not both.
  if (PREDICT_FALSE(req->has_new_request() &&
                    req->has_continue_request())) {
    context->RespondFailure(Status::InvalidArgument(
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
    if (!LookupTabletPeerOrRespond(server_->tablet_manager(), new_req.tablet_id(), resp, context,
                                   &tablet_peer)) {
      return;
    }

    string scanner_id;
    Timestamp snap_timestamp;
    Status s = HandleNewScanRequest(tablet_peer.get(), &scan_req, context,
                                    &collector, &scanner_id, &snap_timestamp, &has_more,
                                    &error_code);
    if (PREDICT_FALSE(!s.ok())) {
      SetupErrorAndRespond(resp->mutable_error(), s, error_code, context);
      return;
    }
    resp->set_scanner_id(scanner_id);
    if (snap_timestamp != Timestamp::kInvalidTimestamp) {
      resp->set_snap_timestamp(snap_timestamp.ToUint64());
    }
  } else if (req->has_continue_request()) {
    const ContinueChecksumRequestPB& continue_req = req->continue_request();
    collector.set_agg_checksum(continue_req.previous_checksum());
    scan_req.set_scanner_id(continue_req.scanner_id());
    Status s = HandleContinueScanRequest(&scan_req, &collector, &has_more, &error_code);
    if (PREDICT_FALSE(!s.ok())) {
      SetupErrorAndRespond(resp->mutable_error(), s, error_code, context);
      return;
    }
  } else {
    context->RespondFailure(Status::InvalidArgument(
                            "Must pass either new_request or continue_request"));
    return;
  }

  resp->set_checksum(collector.agg_checksum());
  resp->set_has_more_results(has_more);

  context->RespondSuccess();
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
      return Status::InvalidArgument(
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
      return Status::InvalidArgument("Cannot specify both a start key and a last key");
    }
    // Set the start key to the last key from a previous scan result.
    RETURN_NOT_OK_PREPEND(EncodedKey::DecodeEncodedString(tablet_schema, scanner->arena(),
                                                          scan_pb.last_primary_key(), &start),
                          "Failed to decode last primary key");
    // Increment the start key, so we don't return the last row again.
    RETURN_NOT_OK_PREPEND(EncodedKey::IncrementEncodedKey(tablet_schema, &start, scanner->arena()),
                          "Failed to increment encoded last row key");
  }

  if (start) {
    spec->SetLowerBoundKey(start.get());
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
                            vector<ColumnSchema>* missing_cols,
                            gscoped_ptr<ScanSpec>* spec,
                            const SharedScanner& scanner) {
  gscoped_ptr<ScanSpec> ret(new ScanSpec);
  ret->set_cache_blocks(scan_pb.cache_blocks());

  unordered_set<string> missing_col_names;

  // First the column range predicates.
  for (const ColumnRangePredicatePB& pred_pb : scan_pb.range_predicates()) {
    if (!pred_pb.has_lower_bound() && !pred_pb.has_upper_bound()) {
      return Status::InvalidArgument(
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
  if (scan_pb.order_mode() == kudu::ORDERED &&
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

// Start a new scan.
Status TabletServiceImpl::HandleNewScanRequest(TabletPeer* tablet_peer,
                                               const ScanRequestPB* req,
                                               const RpcContext* rpc_context,
                                               ScanResultCollector* result_collector,
                                               std::string* scanner_id,
                                               Timestamp* snap_timestamp,
                                               bool* has_more_results,
                                               TabletServerErrorPB::Code* error_code) {
  DCHECK(result_collector != nullptr);
  DCHECK(error_code != nullptr);
  DCHECK(req->has_new_scan_request());
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
    return Status::InvalidArgument("User requests should not have Column IDs");
  }

  if (scan_pb.order_mode() == ORDERED) {
    // Ordered scans must be at a snapshot so that we perform a serializable read (which can be
    // resumed). Otherwise, this would be read committed isolation, which is not resumable.
    if (scan_pb.read_mode() != READ_AT_SNAPSHOT) {
      *error_code = TabletServerErrorPB::INVALID_SNAPSHOT;
          return Status::InvalidArgument("Cannot do an ordered scan that is not a snapshot read");
    }
  }

  gscoped_ptr<ScanSpec> spec(new ScanSpec);

  // Missing columns will contain the columns that are not mentioned in the client
  // projection but are actually needed for the scan, such as columns referred to by
  // predicates or key columns (if this is an ORDERED scan).
  vector<ColumnSchema> missing_cols;
  s = SetupScanSpec(scan_pb, tablet_schema, projection, &missing_cols, &spec, scanner);
  if (PREDICT_FALSE(!s.ok())) {
    *error_code = TabletServerErrorPB::INVALID_SCAN_SPEC;
    return s;
  }

  // Store the original projection.
  gscoped_ptr<Schema> orig_projection(new Schema(projection));
  scanner->set_client_projection_schema(orig_projection.Pass());

  // Build a new projection with the projection columns and the missing columns. Make
  // sure to set whether the column is a key column appropriately.
  SchemaBuilder projection_builder;
  vector<ColumnSchema> projection_columns = projection.columns();
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

    switch (scan_pb.read_mode()) {
      case UNKNOWN_READ_MODE: {
        *error_code = TabletServerErrorPB::INVALID_SCAN_SPEC;
        s = Status::NotSupported("Unknown read mode.");
        return s;
      }
      case READ_LATEST: {
        s = tablet->NewRowIterator(projection, &iter);
        break;
      }
      case READ_AT_SNAPSHOT: {
        s = HandleScanAtSnapshot(scan_pb, rpc_context, projection, tablet, &iter, snap_timestamp);
        if (!s.ok()) {
          tmp_error_code = TabletServerErrorPB::INVALID_SNAPSHOT;
        }
      }
      TRACE("Iterator created");
    }
  }

  if (PREDICT_TRUE(s.ok())) {
    TRACE_EVENT0("tserver", "iter->Init");
    s = iter->Init(spec.get());
  }

  TRACE("Iterator init: $0", s.ToString());

  if (PREDICT_FALSE(s.IsInvalidArgument())) {
    // An invalid projection returns InvalidArgument above.
    // TODO: would be nice if we threaded these more specific
    // error codes throughout Kudu.
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
      return Status::NotFound("Scanner not found");
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
    return Status::InvalidArgument("Invalid call sequence ID in scan request");
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
  int budget_ms = 500;
  MonoTime deadline = MonoTime::Now(MonoTime::COARSE);
  deadline.AddDelta(MonoDelta::FromMilliseconds(budget_ms));

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
    MonoTime now = MonoTime::Now(MonoTime::COARSE);
    if (PREDICT_FALSE(!now.ComesBefore(deadline))) {
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

Status TabletServiceImpl::HandleScanAtSnapshot(const NewScanRequestPB& scan_pb,
                                               const RpcContext* rpc_context,
                                               const Schema& projection,
                                               const shared_ptr<Tablet>& tablet,
                                               gscoped_ptr<RowwiseIterator>* iter,
                                               Timestamp* snap_timestamp) {

  // TODO check against the earliest boundary (i.e. how early can we go) right
  // now we're keeping all undos/redos forever!

  // If the client sent a timestamp update our clock with it.
  if (scan_pb.has_propagated_timestamp()) {
    Timestamp propagated_timestamp(scan_pb.propagated_timestamp());

    // Update the clock so that we never generate snapshots lower that
    // 'propagated_timestamp'. If 'propagated_timestamp' is lower than
    // 'now' this call has no effect. If 'propagated_timestamp' is too much
    // into the future this will fail and we abort.
    RETURN_NOT_OK(server_->clock()->Update(propagated_timestamp));
  }

  Timestamp tmp_snap_timestamp;

  // If the client provided no snapshot timestamp we take the current clock
  // time as the snapshot timestamp.
  if (!scan_pb.has_snap_timestamp()) {
    tmp_snap_timestamp = server_->clock()->Now();
  // ... else we use the client provided one, but make sure it is not too far
  // in the future as to be invalid.
  } else {
    tmp_snap_timestamp.FromUint64(scan_pb.snap_timestamp());
    Timestamp max_allowed_ts;
    Status s = server_->clock()->GetGlobalLatest(&max_allowed_ts);
    if (!s.ok()) {
      return Status::NotSupported("Snapshot scans not supported on this server",
                                  s.ToString());
    }
    if (tmp_snap_timestamp.CompareTo(max_allowed_ts) > 0) {
      return Status::InvalidArgument(
          Substitute("Snapshot time $0 in the future. Max allowed timestamp is $1",
                     server_->clock()->Stringify(tmp_snap_timestamp),
                     server_->clock()->Stringify(max_allowed_ts)));
    }
  }

  tablet::MvccSnapshot snap;

  // Wait for the in-flights in the snapshot to be finished.
  // We'll use the client-provided deadline, but not if it's more than 5 seconds from
  // now -- it's better to make the client retry than hold RPC threads busy.
  //
  // TODO(KUDU-1127): even this may not be sufficient -- perhaps we should check how long it
  // has been since the MVCC manager was able to advance its safe time. If it has been
  // a long time, it's likely that the majority of voters for this tablet are down
  // and some writes are "stuck" and therefore won't be committed.
  MonoTime client_deadline = rpc_context->GetClientDeadline();
  // Subtract a little bit from the client deadline so that it's more likely we actually
  // have time to send our response sent back before it times out.
  client_deadline.AddDelta(MonoDelta::FromMilliseconds(-10));

  MonoTime deadline = MonoTime::Now(MonoTime::FINE);
  deadline.AddDelta(MonoDelta::FromSeconds(5));
  if (client_deadline.ComesBefore(deadline)) {
    deadline = client_deadline;
  }

  TRACE("Waiting for operations in snapshot to commit");
  MonoTime before = MonoTime::Now(MonoTime::FINE);
  RETURN_NOT_OK_PREPEND(
      tablet->mvcc_manager()->WaitForCleanSnapshotAtTimestamp(
          tmp_snap_timestamp, &snap, deadline),
      "could not wait for desired snapshot timestamp to be consistent");

  uint64_t duration_usec = MonoTime::Now(MonoTime::FINE).GetDeltaSince(before).ToMicroseconds();
  tablet->metrics()->snapshot_read_inflight_wait_duration->Increment(duration_usec);
  TRACE("All operations in snapshot committed. Waited for $0 microseconds", duration_usec);

  tablet::Tablet::OrderMode order;
  switch (scan_pb.order_mode()) {
    case UNORDERED: order = tablet::Tablet::UNORDERED; break;
    case ORDERED: order = tablet::Tablet::ORDERED; break;
    default: LOG(FATAL) << "Unexpected order mode.";
  }
  RETURN_NOT_OK(tablet->NewRowIterator(projection, snap, order, iter));
  *snap_timestamp = tmp_snap_timestamp;
  return Status::OK();
}

} // namespace tserver
} // namespace kudu
