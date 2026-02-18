//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#include "yb/yql/pggate/pg_doc_op.h"

#include <algorithm>

#include "yb/ash/wait_state.h"

#include "yb/client/yb_op.h"

#include "yb/common/row_mark.h"

#include "yb/dockv/doc_key.h"

#include "yb/gutil/casts.h"
#include "yb/gutil/strings/escaping.h"

#include "yb/rpc/outbound_call.h"

#include "yb/util/lw_function.h"
#include "yb/util/random_util.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"

#include "yb/yql/pggate/pg_table.h"
#include "yb/yql/pggate/pg_tools.h"
#include "yb/yql/pggate/pggate_flags.h"
#include "yb/yql/pggate/util/pg_doc_data.h"
#include "yb/yql/pggate/util/ybc-internal.h"
#include "yb/yql/pggate/util/ybc_util.h"
#include "yb/yql/pggate/ybc_pggate.h"

DECLARE_uint64(rpc_max_message_size);
DECLARE_double(max_buffer_size_to_rpc_limit_ratio);

namespace yb::pggate {
namespace {

struct PgDocReadOpCachedHelper {
  PgTable dummy_table;
};

class PgDocReadOpCached : private PgDocReadOpCachedHelper, public PgDocOp {
 public:
  PgDocReadOpCached(const PgSession::ScopedRefPtr& pg_session, PrefetchedDataHolder data)
      : PgDocOp(pg_session, &dummy_table) {
    std::list<DocResult> results;
    for (const auto& d : *data) {
      results.emplace_back(d);
    }
    result_stream_ = std::make_unique<CachedPgDocOpFetchStream>(std::move(results));
    VLOG(3) << "Created CachedPgDocOpFetchStream";
  }

  Status FetchMoreResults() override {
    return STATUS(IllegalState, "FetchMoreResults is not valid for PgDocReadOpCached");
  }

  Status ExecuteInit(const YbcPgExecParameters* exec_params) override {
    return Status::OK();
  }

  Result<RequestSent> Execute(ForceNonBufferable force_non_bufferable) override {
    return RequestSent::kTrue;
  }

  bool IsWrite() const override {
    return false;
  }

 protected:
  Status DoPopulateByYbctidOps(const YbctidGenerator& generator, KeepOrder keep_order) override {
    return Status::OK();
  }

  Status DoPopulateMergeStreamRequests(
      MergeSortKeysPtr merge_sort_keys, PgTable& bind,
      InPermutationGenerator&& merge_streams) override {
    return STATUS(InternalError,
                  "DoPopulateMergeStreamRequests is not defined for PgDocReadOpCached");
  }

  Result<bool> DoCreateRequests() override {
    return STATUS(InternalError, "DoCreateRequests is not defined for PgDocReadOpCached");
  }

 private:
  Status CompleteProcessResponse() override {
    return STATUS(InternalError, "CompleteProcessResponse is not defined for PgDocReadOpCached");
  }
};

[[nodiscard]] inline ash::WaitStateCode ResolveWaitEventCode(
    TableType table_type, IsForWritePgDoc is_write) {
  switch (table_type) {
    case TableType::SYSTEM:
      return is_write ? ash::WaitStateCode::kCatalogWrite
                      : ash::WaitStateCode::kCatalogRead;
    case TableType::INDEX:
      return is_write ? ash::WaitStateCode::kIndexWrite
                      : ash::WaitStateCode::kIndexRead;
    case TableType::USER:
      return is_write ? ash::WaitStateCode::kTableWrite
                      : ash::WaitStateCode::kTableRead;
  }
  FATAL_INVALID_ENUM_VALUE(TableType, table_type);
}

Status UpdateMetricOnGettingResponse(
    const LWFunction<Status()>& getter, const PgDocResponse::MetricInfo& info,
    PgDocMetrics& metrics) {
  // Update session stats instrumentation for write requests only. Writes are buffered and flushed
  // asynchronously, and thus it is not possible to correlate wait/execution times directly with
  // the request. We update instrumentation for writes exactly once, after successfully sending an
  // RPC request to the underlying storage layer.
  if (info.is_write && info.is_buffered == IsOpBuffered::kTrue) {
    // For buffered writes, we only track the write request without timing.
    // Timing will be tracked when the buffer is actually flushed.
    metrics.WriteRequest(info.table_type);
    return getter();
  }
  // For non-buffered writes and reads, track timing information
  uint64_t wait_time = 0;
  RETURN_NOT_OK(metrics.CallWithDuration(getter, &wait_time));
  if (info.is_write) {
    // Non-buffered write: track both write and flush
    metrics.WriteRequest(info.table_type);
    metrics.FlushRequest(wait_time);
  } else {
    // Update session stats instrumentation only for read requests. Reads are executed
    // synchronously with respect to Postgres query execution, and thus it is possible to
    // correlate wait/execution times directly with the request. We update instrumentation for
    // reads exactly once, upon receiving a success response from the underlying storage layer.
    metrics.ReadRequest(info.table_type, wait_time);
  }
  return Status::OK();
}

Status UpdateMetricOnRequestBuffering(
    const PgDocResponse::MetricInfo& info, PgDocMetrics& metrics) {
  DCHECK(info.is_write);
  return UpdateMetricOnGettingResponse(
      make_lw_function([]() -> Status { return Status::OK(); }), info, metrics);
}

Result<PgDocResponse::Data> GetResponse(
    PgDocResponse::FutureInfo& future_info, PgSession& session) {
  PgDocResponse::Data result;
  const auto& metric_info = future_info.metrics;

  RETURN_NOT_OK(UpdateMetricOnGettingResponse(make_lw_function(
      [&result, &future = future_info.future, &metric_info, &session] () -> Status {
        auto event_watcher = session.StartWaitEvent(
            ResolveWaitEventCode(metric_info.table_type, metric_info.is_write));
        result = VERIFY_RESULT(future.Get(session));
        return Status::OK();
      }),
      metric_info, session.metrics()));
  return result;
}

// Helper function to determine the type of relation that the given Pgsql operation is being
// performed on. This function classifies the operation into one of three buckets: system catalog,
// secondary index or user table requests.
[[nodiscard]] TableType ResolveRelationType(const PgsqlOp& op, const PgTableDesc& table) {
  if (table.schema().table_properties().is_ysql_catalog_table()) {
    // We don't distinguish between table reads and index reads for a catalog table.
    return TableType::SYSTEM;
  }

  // Check if we're making an index request.
  // Any request that lands on a secondary index table is treated as an index request, while
  // primary key lookups on the main table are treated as user table requests. No such distinction
  // is made for system catalog tables.
  // We make use of the following info to make this decision:
  // read_request().has_index_request() : is true for colocated secondary index requests.
  // table->isIndex() : is true for all secondary index writes and non-colocated secondary index
  //                    reads.
  return
      table.IsIndex() ||
      (op.is_read() && down_cast<const PgsqlReadOp&>(op).read_request().has_index_request())
          ? TableType::INDEX : TableType::USER;
}

// These values are set by  PgGate to optimize query to narrow the scanning range of a query.
// Returns false if new boundary makes request range empty.
bool ApplyPartitionBounds(
    LWPgsqlReadRequestPB& req,
    Slice partition_lower_bound, bool lower_bound_is_inclusive,
    Slice partition_upper_bound, bool upper_bound_is_inclusive,
    const Schema& schema) {
  auto lower_bound = partition_lower_bound;
  auto upper_bound = partition_upper_bound;
  dockv::KeyBytes lower_key_bytes_holder, upper_key_bytes_holder;

  if (schema.num_hash_key_columns()) {
    auto bound_builder =
        [&schema] (auto& holder, Slice bound, bool& is_inclusive, bool is_lower) {
            holder =  HashCodeToDocKeyBound(
                schema, dockv::PartitionSchema::DecodeMultiColumnHashValue(bound), is_inclusive,
                is_lower);
            is_inclusive = false;
            return holder.AsSlice();
        };

    if (!partition_lower_bound.empty()) {
      lower_bound = bound_builder(
          lower_key_bytes_holder, partition_lower_bound, lower_bound_is_inclusive,
          /* is_lower = */ true);
    }

    if (!partition_upper_bound.empty()) {
      upper_bound = bound_builder(
          upper_key_bytes_holder, partition_upper_bound, upper_bound_is_inclusive,
          /* is_lower = */ false);
    }
  }

  return ApplyBounds(
      req, lower_bound, lower_bound_is_inclusive, upper_bound, upper_bound_is_inclusive);
}

// Check if boundaries set on request define valid (not empty) range.
bool CheckScanBoundary(const LWPgsqlReadRequestPB& req) {
  const auto key_diff = req.has_lower_bound() && req.has_upper_bound()
      ? req.lower_bound().key().compare(req.upper_bound().key()) : -1;
  return key_diff < 0 ||
         (key_diff == 0 && req.lower_bound().is_inclusive() && req.upper_bound().is_inclusive());
}

bool IsActiveOp(const PgsqlOpPtr& op) {
  return DCHECK_NOTNULL(op)->is_active();
}

bool IsReadOp(const PgsqlOpPtr& op) {
  return DCHECK_NOTNULL(op)->is_read();
}

PgsqlReadOp& AsReadOp(const PgsqlOpPtr& op) {
  DCHECK(IsReadOp(op));
  return down_cast<PgsqlReadOp&>(*op);
}

LWPgsqlReadRequestPB& AsReadReq(const PgsqlOpPtr& op) {
  return AsReadOp(op).read_request();
}

} // namespace

//--------------------------------------------------------------------------------------------------

PgDocResponse::PgDocResponse(PerformFuture&& future, const MetricInfo& info)
    : holder_(std::in_place_type<FutureInfo>, std::move(future), info) {}

PgDocResponse::PgDocResponse(ProviderPtr provider)
    : holder_(std::move(provider)) {}

bool PgDocResponse::Valid() const {
  return std::holds_alternative<FutureInfo>(holder_)
      ? std::get<FutureInfo>(holder_).future.Valid()
      : static_cast<bool>(std::get<ProviderPtr>(holder_));
}

Result<PgDocResponse::Data> PgDocResponse::Get(PgSession& session) {
  DCHECK(Valid());
  if (std::holds_alternative<FutureInfo>(holder_)) {
    return GetResponse(std::get<FutureInfo>(holder_), session);
  }

  // Detach provider pointer after first usage to make PgDocResponse::Valid return false.
  ProviderPtr provider;
  std::get<ProviderPtr>(holder_).swap(provider);
  return provider->Get();
}

//--------------------------------------------------------------------------------------------------

PgDocOp::PgDocOp(const PgSession::ScopedRefPtr& pg_session, PgTable* table, const Sender& sender)
    : pg_session_(pg_session), table_(*table), sender_(sender) {}

Status PgDocOp::ExecuteInit(const YbcPgExecParameters *exec_params) {
  end_of_data_ = false;
  if (exec_params) {
    exec_params_ = *exec_params;
  }
  return Status::OK();
}

const YbcPgExecParameters& PgDocOp::ExecParameters() const {
  return exec_params_;
}

Result<RequestSent> PgDocOp::Execute(ForceNonBufferable force_non_bufferable) {
  // As of 09/25/2018, DocDB doesn't cache or keep any execution state for a statement, so we
  // have to call query execution every time.
  // - Normal SQL convention: Exec, Fetch, Fetch, ...
  // - Our SQL convention: Exec & Fetch, Exec & Fetch, ...
  // This refers to the sequence of operations between this layer and the underlying tablet
  // server / DocDB layer, not to the sequence of operations between the PostgreSQL layer and this
  // layer.
  if (end_of_data_) {
    return RequestSent(false);
  }
  RETURN_NOT_OK(SendRequest(force_non_bufferable));
  return RequestSent(response_.Valid());
}

Status PgDocOp::FetchMoreResults() {
  // If the execution has error, return without reading any rows.
  RETURN_NOT_OK(exec_status_);

  if (end_of_data_) {
    DCHECK(!HasActiveOps());
    return Status::OK();
  }

  // Send request now in case prefetching was suppressed.
  if (suppress_next_result_prefetching_ && !response_.Valid()) {
    RETURN_NOT_OK(SendRequest());
  }

  // This may block until results are available
  const auto result_data = VERIFY_RESULT(response_.Get(*pg_session_));
  RETURN_NOT_OK(ProcessResponse(result_data));

  // In case ProcessResponse doesn't fail with an error
  // it should return non empty rows, copy paging info from the responses into requests
  // and set end_of_data_.
  // Prefetch next portion of data if needed.
  if (!(end_of_data_ || suppress_next_result_prefetching_)) {
    RETURN_NOT_OK(SendRequest());
  }

  return Status::OK();
}

PgDocOpFetchStream& PgDocOp::ResultStream() {
  static CachedPgDocOpFetchStream dummy_stream({});
  return result_stream_ ? *result_stream_ : dummy_stream;
}

void PgDocOp::SetFetchedTargets(FetchedTargetsPtr targets) {
  targets_ = targets;
  if (result_stream_) {
    result_stream_->SetFetchedTargets(targets);
  }
}

Result<int32_t> PgDocOp::GetRowsAffectedCount() const {
  RETURN_NOT_OK(exec_status_);
  DCHECK(end_of_data_);
  return rows_affected_count_;
}

void PgDocOp::ResetResultStream() {
  if (result_stream_) {
    result_stream_->ResetOps();
  }
}

void PgDocOp::AddOpsToResultStream() {
  if (result_stream_) {
    result_stream_->ResetOps(pgsql_ops_);
  }
}

size_t PgDocOp::MoveInactiveOpsOutside() {
  return std::ranges::partition(pgsql_ops_, &IsActiveOp).begin() - pgsql_ops_.begin();
}

Status PgDocOp::SendRequest(ForceNonBufferable force_non_bufferable) {
  DCHECK(exec_status_.ok());
  DCHECK(!response_.Valid());
  exec_status_ = SendRequestImpl(force_non_bufferable);
  return exec_status_;
}

auto PgDocOp::ActiveOps() const {
  return pgsql_ops_ | std::views::take_while(&IsActiveOp);
}

bool PgDocOp::HasActiveOps() const {
  return !pgsql_ops_.empty() && pgsql_ops_.front()->is_active();
}

Status PgDocOp::SendRequestImpl(ForceNonBufferable force_non_bufferable) {
  // Populate collected information into protobuf requests before sending to DocDB.
  const auto active_op_count = VERIFY_RESULT(CreateRequests());

  if (!active_op_count) {
    return Status::OK();
  }

  // Send at most "parallelism_level_" number of requests at one time.
  const auto send_count = std::min(parallelism_level_, active_op_count);

  uint64_t max_size = FLAGS_rpc_max_message_size * FLAGS_max_buffer_size_to_rpc_limit_ratio
                      / send_count;

  for (auto& read_req : ActiveOps() |
                        std::views::filter(&IsReadOp) |
                        std::views::transform(&AsReadReq)) {
    const auto req_size_limit = read_req.size_limit();
    if (req_size_limit > 0) {
      VLOG(2) << "Capping read op at size limit: " << max_size
              << " (from " << req_size_limit << ")";
    }

    // Cap the size limit if the size limit is unset or exceeds the maximum size.
    if (req_size_limit > max_size || req_size_limit == 0) {
        read_req.set_size_limit(max_size);
    }
  }

  auto is_write = IsForWritePgDoc(IsWrite());
  auto table_type = ResolveRelationType(**pgsql_ops_.data(), *table_);

  // Count read ops.  Write ops should be the same as write requests, so no need to track them.
  if (!is_write) {
    pg_session_->metrics().ReadOp(table_type, send_count);
  }

  VLOG(1) << "Number of " << table_type << " operations to send: " << send_count;
  YbcReadPointHandle catalog_read_time_serial_no = YbcInvalidReadPointHandle;
  TxnReadPoint read_point_before_catalog_op{};
  if (!YBCIsLegacyModeForCatalogOps() &&
      table_->schema().table_properties().is_ysql_catalog_table()) {
    // TODO(#29284): Are catalog writes buffered in the legacy mode? Allow buffering of catalog
    // writes.
    force_non_bufferable = ForceNonBufferable::kTrue;
    read_point_before_catalog_op = pg_session_->GetCurrentReadPointState();
    // Switch to catalog snapshot's read time serial no.
    catalog_read_time_serial_no = pg_session_->GetCatalogSnapshotReadPoint(
        table_->pg_table_id().object_oid, true /* create_if_not_exists */);
    if (VLOG_IS_ON(2) || yb_debug_log_snapshot_mgmt) {
      LOG(INFO) << "Using catalog snapshot read time serial number: " << catalog_read_time_serial_no
                << " and saving read time serial no "
                << read_point_before_catalog_op.read_time_serial_no << " for txn no "
                << read_point_before_catalog_op.txn;
    }
    RSTATUS_DCHECK(
        catalog_read_time_serial_no != 0, IllegalState, "Catalog snapshot read time is 0");
    RETURN_NOT_OK(pg_session_->RestoreReadPoint(catalog_read_time_serial_no));
  }
  response_ = VERIFY_RESULT(sender_(
      pg_session_.get(), {pgsql_ops_.begin(), send_count}, *table_,
      {HybridTime::FromPB(GetInTxnLimitHt()), force_non_bufferable}, is_write));
  if (!result_stream_) {
    // Default PgDocOpFetchStream
    result_stream_ = std::make_unique<ParallelPgDocOpFetchStream>(
        [this]() { return this->FetchMoreResults(); }, pgsql_ops_);
    if (targets_) {
      result_stream_->SetFetchedTargets(targets_);
    }
  }

  if (!YBCIsLegacyModeForCatalogOps() &&
      table_->schema().table_properties().is_ysql_catalog_table()) {
    if (VLOG_IS_ON(2) || yb_debug_log_snapshot_mgmt) {
      LOG(INFO) << "Restoring read time serial no after catalog operation to "
                << read_point_before_catalog_op.read_time_serial_no << " for txn no "
                << read_point_before_catalog_op.txn;
    }
    RETURN_NOT_OK(pg_session_->RestoreReadPoint(read_point_before_catalog_op));
  }
  return Status::OK();
}

void PgDocOp::RecordRequestMetrics() {
  auto& metrics = pg_session_->metrics();
  for (const auto& op : ActiveOps()) {
    const auto* response = op->response();
    if (!(response && response->has_metrics())) {
      continue;
    }
    const auto& response_metrics = response->metrics();

    // Record the number of DocDB rows read.
    // Index Scans on secondary indexes in colocated tables need special handling: the target
    // table for such ops is the secondary index, however the scanned_table_rows field returns
    // the number of main table rows scanned scanned. Hence, if a request has both table and index
    // rows_scanned set, then we do not resolve the target table type. In all other cases, the
    // index_rows_scanned field is not populated and the table type can be resolved to figure out
    // the kind of rows scanned.
    if (!response_metrics.has_scanned_table_rows()) {
      continue;
    }

    if (table_->IsColocated() && response_metrics.has_scanned_index_rows()) {
      metrics.RecordStorageRowsRead(TableType::USER, response_metrics.scanned_table_rows());
      metrics.RecordStorageRowsRead(TableType::INDEX, response_metrics.scanned_index_rows());
    } else {
      metrics.RecordStorageRowsRead(
          ResolveRelationType(*op, *table_), response_metrics.scanned_table_rows());
    }
  }
}

Status PgDocOp::ProcessResponse(const Result<PgDocResponse::Data>& response) {
  VLOG(1) << __PRETTY_FUNCTION__ << ": Received response for request " << this;
  // Check operation status.
  DCHECK(exec_status_.ok());

  RecordRequestMetrics();

  exec_status_ = ProcessResponseImpl(response);
  return exec_status_;
}

Status PgDocOp::ProcessResponseImpl(const Result<PgDocResponse::Data>& response) {
  if (!response.ok()) {
    return response.status();
  }
  const auto& data = *response;
  RETURN_NOT_OK(ProcessCallResponse(*data.response));

  if (data.used_in_txn_limit) {
    GetInTxnLimitHt() = data.used_in_txn_limit.ToUint64();
  }
  return CompleteProcessResponse();
}

Status PgDocOp::ProcessCallResponse(const rpc::CallResponse& response) {
  // Process data coming from tablet server.

  rows_affected_count_ = 0;
  for (const auto& op : ActiveOps()) {
    auto* op_response = op->response();
    if (!op_response) {
      continue;
    }

    if (op_response->has_partition_list_version()) {
      table_->SetLatestKnownPartitionListVersion(op_response->partition_list_version());
    }

    rows_affected_count_ += op_response->rows_affected_count();
    if (!op_response->has_rows_data_sidecar()) {
      continue;
    }

    // A single batch of requests almost always is directed to fetch data from a single tablet.
    // However, when tablets split, data can be sharded/distributed across multiple tablets.
    // Due to automatic tablet splitting, there can exist scenarios where a pgsql_operation prepares
    // a request for a single tablet before the split occurs and then a tablet can be split to
    // multiple tablets. Hence, a single pgsql_op would potentially end up fetching data from
    // multiple tablets.
    //
    // For example consider a query select * from table where i=1 order by j;
    // where there exists a secondary index table_i_j_idx (i HASH, j ASC). Since, there is an
    // ordering constraint, the ybctids are ordered on j;
    //     ybctid[partition 1] contains (ybctid_1, ybctid_3, ybctid_4)
    //     ybctid[partition 2] contains (ybctid_2, ybctid_6)
    //     ybctid[partition 3] contains (ybctid_5, ybctid_7)
    //
    // say partition 1 splits into partition 1.1 and partition 1.2
    // ybctid[partition 1.1] contains (ybctid_1, ybctid_4) -> pgsql_op for partition 1
    // ybctid[partition 1.2] contains (ybctid_3) -> pgsql_op partition 1 with paging state
    //
    // In order to match the ordering constraints between the request and the responses, we
    // obtain the orders of requests executed in each partition and send it along with the responses
    // so that pg_gate can send responses to the postgres layer in the correct order.

    auto rows_data = VERIFY_RESULT(response.GetSidecarHolder(op_response->rows_data_sidecar()));

    // rows_data contains the row count, as well as the row data received from the docdb in Slice
    // format. Since EmplaceOpDocResult already takes the slice and extracts the number of rows
    // received, we are using that get the row count and increment the metric with the row count.
    auto rows_received = VERIFY_RESULT(
        ResultStream().EmplaceOpDocResult(op, std::move(rows_data), *op_response));

    DCHECK_NOTNULL(pg_session_)->metrics().RecordStorageRowsReceived(
        ResolveRelationType(*op, *table_), rows_received);
  }

  return Status::OK();
}

uint64_t& PgDocOp::GetInTxnLimitHt() {
  return !IsWrite() && exec_params_.stmt_in_txn_limit_ht_for_reads
      ? *exec_params_.stmt_in_txn_limit_ht_for_reads
      : in_txn_limit_ht_;
}

Result<size_t> PgDocOp::CreateRequests() {
  if (!request_population_completed_) {
    if (VERIFY_RESULT(DoCreateRequests())) {
      request_population_completed_ = true;
    }
  }
  return CompleteRequests();
}

Result<bool> PgDocOp::PopulateByYbctidOps(const YbctidGenerator& generator, KeepOrder keep_order) {
  RETURN_NOT_OK(DoPopulateByYbctidOps(generator, keep_order));
  request_population_completed_ = true;
  if (HasActiveOps()) {
    RETURN_NOT_OK(CompleteRequests());
    return true;
  }
  return false;
}

Result<bool> PgDocOp::PopulateMergeStreamRequests(
    MergeSortKeysPtr merge_sort_keys, PgTable& bind, InPermutationGenerator&& merge_streams) {
  RETURN_NOT_OK(DoPopulateMergeStreamRequests(merge_sort_keys, bind, std::move(merge_streams)));
  request_population_completed_ = true;
  if (HasActiveOps()) {
    RETURN_NOT_OK(CompleteRequests());
    return true;
  }
  return false;
}

Result<size_t> PgDocOp::CompleteRequests() {
  size_t count = 0;
  for (const auto& op : ActiveOps()) {
    if (op->is_read() && table_->num_hash_key_columns() > 0 && !yb_allow_dockey_bounds) {
      // With GHI#28219, lower_bound and upper_bound fields are dockeys in read requests of hash
      // partitioned tables. Since the AutoFlag is false, it is possible that some tservers may not
      // yet have this change yet. So fallback to the older protocol (where these fields are encoded
      // hash codes) to maintain backward compatibility.
      RETURN_NOT_OK(op->ConvertBoundsToHashCode());
    }
    RETURN_NOT_OK(op->InitPartitionKey(*table_));
    ++count;
  }
  return count;
}

Result<PgDocResponse> PgDocOp::DefaultSender(
    PgSession* session, std::span<const PgsqlOpPtr> ops, const PgTableDesc& table,
    const PgSession::RunOptions& options, IsForWritePgDoc is_write) {
  PgDocResponse::MetricInfo metrics{
    ResolveRelationType(*ops.front(), table), is_write,
    IsOpBuffered(!options.force_non_bufferable)};
  auto result = PgDocResponse{VERIFY_RESULT(session->RunAsync(ops, table, options)), metrics};
  if (!result.Valid()) {
    // session->RunAsync() calls PgSession::DoRunAsync() -> RunHelper::Flush().
    // RunHelper::Flush() returns PerformFuture() (empty constructor) when ops_info_.ops.Empty()
    // is true, meaning all operations were buffered. The empty PerformFuture makes
    // result.Valid() return false, indicating the operation was queued for later execution
    // rather than sent immediately. Buffered operations will have their flush requests
    // counted when the buffer is eventually flushed.
    // Operations flushing returns an invalid result.
    // when all operations are buffered and not performed immediately.
    // These ops will have their metrics counted when the buffer is flushed eventually.
    metrics.is_buffered = IsOpBuffered::kTrue;
    RETURN_NOT_OK(UpdateMetricOnRequestBuffering(metrics, session->metrics()));
  }
  return result;
}

//-------------------------------------------------------------------------------------------------

InExpressionWrapper InExpressionWrapper::Make(
    const PgTable& table, size_t idx, const LWPgsqlExpressionPB* expr) {
  if (expr->has_condition()) {
    const auto& front = expr->condition().operands().front();
    const auto& back = expr->condition().operands().back();
    std::vector<const LWPgsqlExpressionPB*> values;
    values.reserve(back.condition().operands().size());
    for (const auto& value : back.condition().operands()) {
      values.push_back(&value);
    }
    if (front.has_tuple()) {
      auto first_column_id = table->schema().first_column_id();
      std::vector<size_t> targets;
      targets.reserve(front.tuple().elems().size());
      for (const auto& lhs_elem : front.tuple().elems()) {
        targets.emplace_back(lhs_elem.column_id() - first_column_id);
      }
      return InExpressionWrapper(
          std::move(targets), std::move(values), &InExpressionWrapper::GetRowInValues);
    }
    return InExpressionWrapper({idx}, std::move(values), &InExpressionWrapper::GetInValue);
  }
  return InExpressionWrapper({idx}, {expr}, &InExpressionWrapper::GetSingleValue);
}

bool InExpressionWrapper::Next() {
  if (++pos_ == values_.size()) {
    pos_ = 0;
    return false;
  }
  return true;
}

void InExpressionWrapper::GetValues(std::vector<const LWQLValuePB*>* permutation) {
  (this->*GetValuesFn_)(permutation);
}

void InExpressionWrapper::GetSingleValue(std::vector<const LWQLValuePB*>* permutation) {
  (*permutation)[targets_[0]] = &values_[0]->value();
}

void InExpressionWrapper::GetInValue(std::vector<const LWQLValuePB*>* permutation) {
  (*permutation)[targets_[0]] = &values_[pos_]->value();
}

void InExpressionWrapper::GetRowInValues(std::vector<const LWQLValuePB*>* permutation) {
  auto val_it = values_[pos_]->value().tuple_value().elems().begin();
  for (const auto idx : targets_) {
    (*permutation)[idx] = &*val_it;
    ++val_it;
  }
}

InPermutationGenerator::InPermutationGenerator(
    PgTable& table,
    std::vector<InExpressionWrapper>&& source_exprs,
    std::vector<size_t>&& targets)
    : all_targets_(std::move(targets)),
      source_exprs_(std::move(source_exprs)),
      current_permutation_(table->num_key_columns(), nullptr) {}

size_t InPermutationGenerator::Size() {
  size_t result = 1;
  for (const auto& expr : source_exprs_) {
    result *= expr.Size();
  }
  return result;
}

const std::vector<const LWQLValuePB*>& InPermutationGenerator::NextPermutation() {
  DCHECK(!done_);
  for (auto& expr : source_exprs_) {
    expr.GetValues(&current_permutation_);
  }
  Next();
  return current_permutation_;
}

void InPermutationGenerator::Reset() {
  done_ = false;
  current_permutation_.assign(current_permutation_.size(), nullptr);
  for (auto& expr : source_exprs_) {
    expr.ResetPos();
  }
}

void InPermutationGenerator::Next() {
  DCHECK(!done_);
  for (auto& expr : source_exprs_) {
    if (expr.Next()) {
      return;
    }
  }
  done_ = true;
}

void InPermutationBuilder::AddExpression(size_t idx, const LWPgsqlExpressionPB* expr) {
  source_exprs_.push_back(InExpressionWrapper::Make(table_, idx, expr));
}

InPermutationGenerator InPermutationBuilder::Build() {
  std::vector<size_t> all_targets;
  for (const auto& expr : source_exprs_) {
    const auto& targets = expr.Targets();
    all_targets.insert(all_targets.end(), targets.begin(), targets.end());
  }
  std::sort(all_targets.begin(), all_targets.end());
  DCHECK(TargetsAreValid(all_targets));
  return InPermutationGenerator(table_, std::move(source_exprs_), std::move(all_targets));
}

bool InPermutationBuilder::TargetsAreValid(const std::vector<size_t>& all_targets) {
  // If there're more targets than keys, there are either duplicate targets, or non-key targets
  if (all_targets.size() > table_->num_key_columns()) {
    LOG(WARNING) << "Too many targets (" << all_targets.size() << ")";
    return false;
  }
  // All hash column must present
  for (size_t idx = 0; idx < table_->num_hash_key_columns(); ++idx) {
    if (all_targets[idx] != idx) {
      LOG(WARNING) << "Bad hash target: " << all_targets[idx] << " @ " << idx;
      return false;
    }
  }
  for (size_t idx = table_->num_hash_key_columns(); idx < all_targets.size(); ++idx) {
    // Targets must be keys
    if (all_targets[idx] >= table_->num_key_columns()) {
      LOG(WARNING) << "Target out of bounds: " << all_targets[idx] << " @ " << idx;
      return false;
    }
    // Targets must me sorted. Since all hash keys are already here as the first targets, this
    // also implicitly verifies that remaining targets are range keys.
    if (idx > 0 && all_targets[idx] <= all_targets[idx - 1]) {
      LOG(WARNING) << "Duplicate or out of order target: " << all_targets[idx]
                   << " after " << all_targets[idx - 1] << " @ " << idx;
      return false;
    }
  }
  return true;
}

//-------------------------------------------------------------------------------------------------

PgDocReadOp::PgDocReadOp(const PgSession::ScopedRefPtr& pg_session,
                         PgTable* table,
                         PgsqlReadOpPtr read_op)
    : PgDocOp(pg_session, table), read_op_(std::move(read_op)) {}

PgDocReadOp::PgDocReadOp(const PgSession::ScopedRefPtr& pg_session,
                         PgTable* table,
                         PgsqlReadOpPtr read_op,
                         const Sender& sender)
    : PgDocOp(pg_session, table, sender), read_op_(std::move(read_op)) {}

Status PgDocReadOp::ExecuteInit(const YbcPgExecParameters* exec_params) {
  RSTATUS_DCHECK(
      pgsql_ops_.empty() || !exec_params,
      IllegalState, "Exec params can't be changed for already created operations");
  RETURN_NOT_OK(PgDocOp::ExecuteInit(exec_params));
  if (!VERIFY_RESULT(SetScanBounds(table_, read_op_->read_request()))) {
    DCHECK(!HasActiveOps());
    end_of_data_ = true;
    return Status::OK();
  }

  read_op_->read_request().set_return_paging_state(true);
  RETURN_NOT_OK(SetRequestPrefetchLimit());
  SetBackfillSpec();
  SetRowMark();
  SetReadTimeForBackfill();
  return Status::OK();
}

Result<bool> PgDocReadOp::SetScanBounds(PgTable& table, LWPgsqlReadRequestPB& request) {
  if (table->schema().num_hash_key_columns() > 0) {
    // TODO: figure out if we can clarify bounds of a hash table scan
    return true;
  }
  VLOG_WITH_FUNC(4) << "Request: " << request.ShortDebugString();
  // A hacky way to handle merge sort case.
  // The range_column_values() may contain some IN conditions at this point to set up streams,
  // but client::GetRangePartitionBounds can't handle them.
  // So try to detect this case and skip bounds evaluation for now.
  // We are going to set the bounds for individual streams anyway.
  for (const auto& range_expr : request.range_column_values()) {
    if (range_expr.has_condition()) {
      VLOG_WITH_FUNC(4) << "Range expression has condition: " << range_expr.ShortDebugString();
      return true;
    }
  }
  std::vector<dockv::KeyEntryValue> lower_range_components, upper_range_components;
  RETURN_NOT_OK(client::GetRangePartitionBounds(
      table->schema(), request, &lower_range_components, &upper_range_components));
  if (lower_range_components.empty() && upper_range_components.empty()) {
    return true;
  }
  auto lower_bound = dockv::DocKey(
      table->schema(), std::move(lower_range_components)).Encode().ToStringBuffer();
  auto upper_bound = dockv::DocKey(
      table->schema(), std::move(upper_range_components)).Encode().ToStringBuffer();
  VLOG_WITH_FUNC(4) << "Lower bound: " << Slice(lower_bound).ToDebugHexString()
                    << ", upper bound: " << Slice(upper_bound).ToDebugHexString();
  return ApplyBounds(request, lower_bound, true, upper_bound, false);
}

bool CouldBeExecutedInParallel(const LWPgsqlReadRequestPB& req) {
  if (req.index_request().has_vector_idx_options()) {
    // Executed in parallel on PgClient
    return false;
  }
  // At this time ordered scan requires tablet scan order, so they have to be done sequentially
  return !req.has_is_forward_scan();
}

Result<bool> PgDocReadOp::DoCreateRequests() {
  // All information from the SQL request has been collected and setup. This code populates
  // Protobuf requests before sending them to DocDB. For performance reasons, requests are
  // constructed differently for different statements.
  const auto& req = read_op_->read_request();
  // Use partition column values to filter out partitions without possible matches.
  // If there is a query with conditions on multiple partition columns, like
  // - SELECT * FROM sql_table WHERE hash_c1 IN (1, 2, 3) AND hash_c2 IN (4, 5, 6);
  // the function creates multiple requests for possible hash permutations / keys.
  // The requests are also configured to run in parallel.
  if (!req.partition_column_values().empty()) {
    return PopulateNextHashPermutationOps();

  // There is no key values to filter out partitions, send requests to all of them.
  // Requests pushing down aggregates and/or filter expression tend to do more work on DocDB side,
  // so it takes longer to return responses, and Postgres side has generally less work to do.
  // Hence we optimize by sending multiple parallel requests to the tablets, allowing their
  // simultaneous processing.
  // Effect may be less than expected if the nodes are already heavily loaded and CPU consumption
  // is high, or selectivity of the filter is low.
  // Other concern than Postgres being a bottleneck, is an index scan over a range partitioned
  // table. Such scan is able to return rows ordered, and that order may be relied upon by upper
  // plan nodes, but parallel execution messes the order up. Here where we are preparing the
  // requests, we do not know if the query relies on the row order, we even may not be able to tell,
  // if we are doing sequential or primary key scan, hence we should not parallelize non-aggregate
  // scans on range partitioned tables.
  // TODO(GHI 13737): as explained above, explicitly indicate, if operation should return ordered
  // results.
  } else if (CouldBeExecutedInParallel(req)) {
    return PopulateParallelSelectOps();
  } else {
    // No optimization.
    // Target partition boundaries do not intersect with request boundaries, no results
    if (exec_params_.partition_key && !VERIFY_RESULT(SetScanPartitionBoundary())) {
      return false;
    }
    ClonePgsqlOps(1);
    auto& read_op = GetReadOp(0);
    read_op.set_active(true);
    if (req.has_ybctid_column_value()) {
      const Slice& ybctid = req.ybctid_column_value().value().binary_value();
      const size_t partition = VERIFY_RESULT(table_->FindPartitionIndex(ybctid));
      return SetLowerUpperBound(&read_op.read_request(), partition);
    }
    return true;
  }
}

Status PgDocReadOp::DoPopulateByYbctidOps(const YbctidGenerator& generator, KeepOrder keep_order) {
  // NOTE on a typical process.
  // 1- Statement:
  //    SELECT xxx FROM <table> WHERE ybctid IN (SELECT ybctid FROM INDEX);
  //
  // 2- Select 1024 ybctids (prefetch limit) from INDEX.
  //
  // 3- ONLY ONE TIME: Create a batch of operators, one per partition.
  //    * Each operator has a clone requests from template_op_.
  //    * We will reuse the created operators & requests for the future batches of 1024 ybctids.
  //    * Certain fields in the protobuf requests MUST BE RESET for each batches.
  //
  // 4- Assign the selected 1024 ybctids to the batch of operators.
  //
  // 5- Send requests to tablet servers to read data from <tab> associated with ybctid values.
  //
  // 6- Repeat step 2 thru 5 for the next batch of 1024 ybctids till done.
  end_of_data_ = false;
  InitializeYbctidOperators();
  VLOG(1) << "Row order " << (keep_order ? "is" : "is not") << " important";
  if (keep_order) {
    if (!result_stream_) {
      result_stream_ = std::make_unique<MergingPgDocOpFetchStream<DocResult>>(
          [this]() { return this->FetchMoreResults(); }, pgsql_ops_, 0);
      result_stream_->SetFetchedTargets(targets_);
      VLOG(3) << "Created MergingPgDocOpFetchStream";
    }
  }
  while (true) {
    const auto ybctid = generator.next();
    if (ybctid.empty()) {
      break;
    }
    // Find partition. The partition index is the boundary order minus 1.
    // - For hash partitioning, we use hashcode to find the right index.
    // - For range partitioning, we pass partition key to seek the index.
    const auto partition = VERIFY_RESULT(table_->FindPartitionIndex(ybctid));

    // Assign ybctids to operators.
    auto& read_op = GetReadOp(partition);
    auto& read_req = read_op.read_request();

    // Check bounds, if set.
    if (read_req.has_lower_bound()) {
      const auto& lower_bound = read_req.lower_bound();
      DCHECK(!dockv::PartitionSchema::IsValidHashPartitionKeyBound(lower_bound.key()));
      if (lower_bound.is_inclusive() ? ybctid < lower_bound.key() : ybctid <= lower_bound.key()) {
        continue;
      }
    }
    if (read_req.has_upper_bound()) {
      const auto& upper_bound = read_req.upper_bound();
      DCHECK(!dockv::PartitionSchema::IsValidHashPartitionKeyBound(upper_bound.key()));
      if (upper_bound.is_inclusive() ? ybctid > upper_bound.key() : ybctid >= upper_bound.key()) {
        continue;
      }
    }

    // Append ybctid and its order to batch_arguments.
    // The "ybctid" values are returned in the same order as the row in the IndexTable. To keep
    // track of this order, each argument is assigned an order-number.
    auto* batch_arg = read_req.add_batch_arguments();
    if (keep_order) {
      batch_arg->set_order(batch_row_ordering_counter_++);
    }
    auto* arg_value = batch_arg->mutable_ybctid()->mutable_value();
    arg_value->dup_binary_value(ybctid);

    // We must set "ybctid_column_value" in the request for the sake of rolling upgrades.
    // Servers before 2.15 will read only "ybctid_column_value" as they are not aware
    // of ybctid-batching.
    if (!read_req.has_ybctid_column_value() ||
        arg_value->binary_value() < read_req.ybctid_column_value().value().binary_value()) {
      read_req.mutable_ybctid_column_value()->mutable_value()
          ->ref_binary_value(arg_value->binary_value());
    }

    // For every read operation set partition boundary. In case a tablet is split between
    // preparing requests and executing them, DocDB will return a paging state for pggate to
    // continue till the end of current tablet is reached.
    if (VERIFY_RESULT(SetLowerUpperBound(&read_req, partition))) {
      read_op.set_active(true);
    }
  }

  // Done creating request, but not all partition or operator has arguments (inactive).
  MoveInactiveOpsOutside();

  return Status::OK();
}

void PgDocReadOp::InitializeYbctidOperators() {
  if (!pgsql_op_arena_) {
    // First batch, create arena for operations
    DCHECK(pgsql_ops_.empty());
    pgsql_op_arena_ = SharedThreadSafeArena();
  }
  ResetInactivePgsqlOps();
  // Make sure we have one operation per partition
  ClonePgsqlOps(table_->GetPartitionListSize());
}

Status PgDocReadOp::DoPopulateMergeStreamRequests(
    MergeSortKeysPtr merge_sort_keys, PgTable& bind, InPermutationGenerator&& merge_streams) {
  DCHECK(merge_sort_keys && !merge_sort_keys->empty());
  ClonePgsqlOps(merge_streams.Size());
  size_t active_op_count = 0;
  for (size_t idx = 0; idx < merge_streams.Size(); ++idx) {
    DCHECK(merge_streams.HasPermutation());
    auto& read_op = GetReadOp(idx);
    auto& read_req = read_op.read_request().has_index_request()
        ? *read_op.read_request().mutable_index_request()
        : read_op.read_request();
    read_req.mutable_range_column_values()->clear();
    if (VERIFY_RESULT(BindExprsRegular(bind, read_req, merge_streams.NextPermutation()))) {
      read_op.set_active(true);
      read_op.set_is_merge_stream(true);
      ++active_op_count;
    }
  }
  DCHECK(!merge_streams.HasPermutation());
  MoveInactiveOpsOutside();
  // We need a specialized result stream to merge results from multiple active operations.
  // If there is only one, default result stream is good; no active ops means no execution.
  if (active_op_count > 1) {
    DCHECK(!result_stream_);
    // It is not known here what columns are read, but need to reserve space in Datum/isnull arrays
    // for them. So reserve space for all regular attributes.
    auto nattrs = std::count_if(
        table_->schema().columns().cbegin(),
        table_->schema().columns().cend(),
        [](const ColumnSchema& c) { return c.order() > 0; });
    result_stream_ = std::make_unique<MergingPgDocOpFetchStream<PgDocResult>>(
        [this]() { return this->FetchMoreResults(); }, pgsql_ops_, nattrs);
    result_stream_->SetFetchedTargets(targets_);
    result_stream_->SetMergeSortKeys(merge_sort_keys);
  }
  return Status::OK();
}

Result<bool> PgDocReadOp::BindExprsRegular(
    PgTable& table, LWPgsqlReadRequestPB& read_req, const QLValuePBs& values) {
  if (table->num_hash_key_columns() > 0) {
    std::span hash_values(values.begin(), table->num_hash_key_columns());
    auto hash = VERIFY_RESULT(table->partition_schema().PgsqlHashColumnCompoundValue(hash_values));
    const auto lower_bound =
        HashCodeToDocKeyBound(table->schema(), hash, true /* is_inclusive */, true /* is_lower */);
    const auto upper_bound = HashCodeToDocKeyBound(
        table->schema(), hash, true /* is_inclusive */, false /* is_lower */);
    if (!ApplyBounds(
            read_req, lower_bound, false /* lower_bound_is_inclusive */, upper_bound,
            false /* upper_bound_is_inclusive */)) {
      return false;
    }
    read_req.mutable_partition_column_values()->clear();
    for (size_t index = 0; index < table->num_hash_key_columns(); ++index) {
      auto* partition_column_value = read_req.add_partition_column_values()->mutable_value();
      *partition_column_value = *values[index];
    }
  }

  for (size_t index = table->num_hash_key_columns(); index < table->num_key_columns(); ++index) {
    auto* elem = values[index];
    if (elem) {
      if (!read_req.has_condition_expr()) {
        read_req.mutable_condition_expr()->mutable_condition()->set_op(QL_OP_AND);
      }
      auto* op = read_req.mutable_condition_expr()->mutable_condition()->add_operands();
      auto* pgcond = op->mutable_condition();
      pgcond->set_op(QL_OP_EQUAL);
      pgcond->add_operands()->set_column_id(table.ColumnForIndex(index).id());
      *pgcond->add_operands()->mutable_value() = *elem;
    }
  }
  if (table->num_hash_key_columns() == 0) {
    return SetScanBounds(table, read_req);
  }
  return true;
}

Result<bool> PgDocReadOp::BindExprsToBatch(
    PartitionBatches& partition_batches, const QLValuePBs& values,
    const ReadOpProvider& read_op_provider) {
  std::span hash_values(values.begin(), table_->num_hash_key_columns());
  auto partition_key = VERIFY_RESULT(table_->partition_schema().EncodePgsqlHash(hash_values));
  auto partition = client::FindPartitionStartIndex(table_->GetPartitionList(), partition_key);
  DCHECK(partition < partition_batches.size());
  auto& partition_batch = partition_batches[partition];
  if (!partition_batch.first) {
    partition_batch.first = true;
    auto& read_op = VERIFY_RESULT_REF(read_op_provider());
    auto& read_req = read_op.read_request();
    if (VERIFY_RESULT(SetLowerUpperBound(&read_req, partition))) {
      read_op.set_active(true);
      partition_batch.second = InitHashPermutationBatch(read_req);
    }
  }
  if (!partition_batch.second) {
    return false;
  }
  auto* rhs_values_list = partition_batch.second->mutable_value()->mutable_list_value();
  // Add new tuple with the hash code and the provided key values
  auto* tup_elements = rhs_values_list->add_elems()->mutable_tuple_value();
  auto* new_elem = tup_elements->add_elems();
  new_elem->set_int32_value(table_->partition_schema().DecodeMultiColumnHashValue(partition_key));
  for (auto* elem : values) {
    if (elem) {
      new_elem = tup_elements->add_elems();
      *new_elem = *elem;
    }
  }

  return true;
}

bool PgDocReadOp::IsHashBatchingEnabled() {
  if (PREDICT_FALSE(!is_hash_batched_.has_value())) {
    is_hash_batched_ = pg_session_->IsHashBatchingEnabled() && hash_permutations_->Size() > 1;
  }
  return *is_hash_batched_;
}

bool PgDocReadOp::IsBatchFlushRequired() const {
  return exec_params_.work_mem > 0 &&
         pgsql_op_arena_ &&
         pgsql_op_arena_->UsedBytes() > (implicit_cast<size_t>(exec_params_.work_mem) * 1024);
}

// Collect hash expressions to prepare for generating permutations.
InPermutationGenerator PgDocReadOp::InitializeHashPermutationStates() {
  InPermutationBuilder builder(table_);
  size_t c_idx = 0;
  for (const auto& col_expr : read_op_->read_request().partition_column_values()) {
    if (col_expr.expr_case() != PgsqlExpressionPB::EXPR_NOT_SET) {
      builder.AddExpression(c_idx, &col_expr);
    }
    ++c_idx;
  }
  return builder.Build();
}

Result<bool> PgDocReadOp::PopulateNextHashPermutationOps() {
  // Create permutation state, if it does not exist.
  // We keep permutation state between batches, but need operations cleanup
  // Permutations and operations are reset between parallel ranges, in this case we need to clone
  // a new set of operations.
  if (!hash_permutations_) {
    DCHECK(pgsql_ops_.empty());
    hash_permutations_.emplace(InitializeHashPermutationStates());
    // Setup arena to control memory footprint
    if (!pgsql_op_arena_) {
      pgsql_op_arena_ = SharedThreadSafeArena();
    }
  } else {
    ResetInactivePgsqlOps();
  }

  // Continue with operations if any are still in flight
  if (HasActiveOps()) {
    return false;
  }

  // Setup operations
  const auto max_op_count = std::min(hash_permutations_->Size(),
                               IsHashBatchingEnabled() ?
                                   table_->GetPartitionListSize() :
                                   implicit_cast<size_t>(FLAGS_ysql_request_limit));
  ClonePgsqlOps(max_op_count);
  if (IsHashBatchingEnabled()) {
    PartitionBatches batches(table_->GetPartitionListSize(), {false, nullptr});
    const auto op_provider =
        [it = pgsql_ops_.begin(), end = pgsql_ops_.end()] () mutable -> Result<PgsqlReadOp&> {
          RSTATUS_DCHECK(it != end, IllegalState, "No more read ops available");
          return AsReadOp(*it++);
        };
    for (; hash_permutations_->HasPermutation(); ) {
      if (VERIFY_RESULT(BindExprsToBatch(
              batches, hash_permutations_->NextPermutation(), make_lw_function(op_provider))) &&
            IsBatchFlushRequired()) {
        break;
      }
    }
  } else {
    for (auto it = pgsql_ops_.begin();
         it != pgsql_ops_.end() && hash_permutations_->HasPermutation(); ++it) {
      if (VERIFY_RESULT(BindExprsRegular(
          table_, AsReadReq(*it), hash_permutations_->NextPermutation()))) {
        (**it).set_active(true);
      }
    }
  }

  MoveInactiveOpsOutside();
  return !hash_permutations_->HasPermutation();
}

LWPgsqlExpressionPB* PgDocReadOp::InitHashPermutationBatch(LWPgsqlReadRequestPB& read_req) {
  read_req.mutable_partition_column_values()->clear();
  if (!read_req.has_condition_expr()) {
    read_req.mutable_condition_expr()->mutable_condition()->set_op(QL_OP_AND);
  }
  auto& cond_bind_expr = *read_req.mutable_condition_expr()->mutable_condition()->add_operands();
  cond_bind_expr.mutable_condition()->set_op(QL_OP_IN);
  auto& add_targets = *cond_bind_expr.mutable_condition()->add_operands()->mutable_tuple();

  // ROW(YbHashCode, HashColumns, RangeColumns)
  auto& yb_hash_code = *add_targets.add_elems();
  yb_hash_code.set_column_id(kYbHashCodeColId);
  for (auto idx : hash_permutations_->Targets()) {
    add_targets.add_elems()->set_column_id(table_.ColumnForIndex(idx).id());
  }
  // RHS of IN condition
  return cond_bind_expr.mutable_condition()->add_operands();
}

Result<bool> PgDocReadOp::PopulateParallelSelectOps() {
  SCHECK(read_op_->read_request().partition_column_values().empty(),
         IllegalState,
         "Request with non empty partition_column_values can't be parallelized");
  // Create batch operators, one per partition, to execute in parallel.
  // TODO(tsplit): what if table partition is changed during PgDocReadOp lifecycle before or after
  // the following line?
  ClonePgsqlOps(table_->GetPartitionListSize());
  // Set "parallelism_level_" to control how many operators can be sent at one time.
  //
  // TODO(neil) The calculation for this control variable should be applied to ALL operators, but
  // the following calculation needs to be refined before it can be used for all statements.
  auto parallelism_level = FLAGS_ysql_select_parallelism;
  if (parallelism_level < 0) {
    auto tserver_count = VERIFY_RESULT(pg_session_->pg_client().TabletServerCount(
        true /* primary_only */));

    // Establish lower and upper bounds on parallelism.
    int kMinParSelParallelism = 1;
    int kMaxParSelParallelism = 16;
    parallelism_level_ =
      std::min(std::max(tserver_count * 2, kMinParSelParallelism), kMaxParSelParallelism);
  } else {
    parallelism_level_ = parallelism_level;
  }

  // Get table partitions
  const auto& partition_keys = table_->GetPartitionList();
  SCHECK_EQ(partition_keys.size(), pgsql_ops_.size(), IllegalState,
            "Number of partitions and number of partition keys are not the same");

  // Create operations, one per partition, and apply partition boundaries to ther requests.
  // Activate operation if partition boundaries intersect with boundaries that may be already set.
  // If boundaries do not intersect the operation won't produce any result.
  for (size_t partition = 0; partition < partition_keys.size(); ++partition) {
    if (VERIFY_RESULT(SetLowerUpperBound(&GetReadReq(partition), partition))) {
      pgsql_ops_[partition]->set_active(true);
    }
  }

  MoveInactiveOpsOutside();

  return true;
}

// When postgres requests to scan a specific partition, set the partition parameter accordingly.
Result<bool> PgDocReadOp::SetScanPartitionBoundary() {
  // Boundary to scan from a given key to the end of its associated tablet.
  // - Lower: The given partition key (inclusive).
  // - Upper: Beginning of next tablet (not inclusive).
  SCHECK(exec_params_.partition_key != nullptr, Uninitialized, "expected non-null partition_key");

  // Seek the tablet of the given key.
  // TODO(tsplit): what if table partition is changed during PgDocReadOp lifecycle before or after
  // the following line?
  const auto& partition_keys = table_->GetPartitionList();
  auto partition_key = std::find(
      partition_keys.begin(), partition_keys.end(), a2b_hex(exec_params_.partition_key));
  RSTATUS_DCHECK(
      partition_key != partition_keys.end(), InvalidArgument, "invalid partition key given");

  // Seek upper bound (Beginning of next tablet).
  std::string upper_bound;
  const auto& next_partition_key = std::next(partition_key, 1);
  if (next_partition_key != partition_keys.end()) {
    upper_bound = *next_partition_key;
  }
  return ApplyPartitionBounds(read_op_->read_request(),
                              *partition_key,
                              /* lower_bound_is_inclusive =*/true,
                              upper_bound,
                              /* upper_bound_is_inclusive =*/false,
                              table_->schema());
}

Status PgDocReadOp::CompleteProcessResponse() {
  for (auto& read_op : ActiveOps() |
                       std::views::take(parallelism_level_) |
                       std::views::transform(&AsReadOp)) {
    // Check for completion.
    bool has_more_arg = false;
    auto& res = *read_op.response();
    auto& req = read_op.read_request();

    // Save the backfill_spec if tablet server wants to return it.
    if (res.is_backfill_batch_done()) {
      out_param_backfill_spec_ = res.backfill_spec().ToBuffer();
    } else if (VERIFY_RESULT(PrepareNextRequest(*table_, &read_op))) {
      has_more_arg = true;
    }

    // Check for batch execution.
    if (res.batch_arg_count() < static_cast<int64_t>(req.batch_arguments().size())) {
      has_more_arg = true;

      // Delete the executed arguments from batch and keep those that haven't been executed.
      for (auto i = res.batch_arg_count(); i-- > 0;) {
        req.mutable_batch_arguments()->pop_front();
      }

      // Due to rolling upgrade reason, we must copy the first batch_arg to the scalar arg.
      FormulateRequestForRollingUpgrade(&req);
    }

    if (!has_more_arg) {
      read_op.set_active(false);
    }
  }

  MoveInactiveOpsOutside();
  end_of_data_ =  !HasActiveOps() && request_population_completed_;

  if (exec_params_.out_param && has_out_param_backfill_spec()) {
    YbcPgExecOutParamValue value;
    value.bfoutput = out_param_backfill_spec();
    YBCGetPgCallbacks()->WriteExecOutParam(exec_params_.out_param, &value);
  }

  return Status::OK();
}

Status PgDocReadOp::SetRequestPrefetchLimit() {
  // Predict the maximum prefetch-limit using the associated gflags.
  auto& req = read_op_->read_request();

  // Limits: 0 means 'unlimited'.
  uint64_t predicted_row_limit = exec_params_.yb_fetch_row_limit;
  uint64_t predicted_size_limit = exec_params_.yb_fetch_size_limit;

  // Use statement LIMIT(count + offset) if it is smaller than the predicted limit.
  auto row_limit = exec_params_.limit_count + exec_params_.limit_offset;
  suppress_next_result_prefetching_ = true;

  // If in any of these cases we determine that the default row/size based batch size is lower
  // than the actual requested LIMIT, we enable prefetching. If else, we try
  // to only get the required data in one RPC without prefetching.
  if (exec_params_.limit_use_default) {
    row_limit = predicted_row_limit;
    suppress_next_result_prefetching_ = false;
  } else if (predicted_row_limit > 0 && predicted_row_limit < row_limit) {
    row_limit = predicted_row_limit;
    suppress_next_result_prefetching_ = false;
  } else if (predicted_size_limit > 0) {
    // Try to estimate the total data size of a LIMIT'd query
    // Inaccurate in the presence of varlen targets but not possible to fix that without PG stats;
    size_t row_width = 0;
    for (const LWPgsqlExpressionPB& target : req.targets()) {
      // If target is a system column, we it's probably variable size
      // and we don't have the means to estimate its length.
      if (target.has_column_id()) {
        auto column_id = target.column_id();
        if (column_id < 0) {
            // System columns are usually variable length which we are
            // estimating with the size of a Binary DataType for now.
            row_width += GetTypeInfo(DataType::BINARY)->size;
            continue;
        }
        const ColumnSchema &col_schema =
          VERIFY_RESULT(table_->schema().column_by_id(ColumnId(column_id)));

        // This size is usually the computed sizeof() of the serialized datatype.
        // Its computation can be found in yb/common/types.h
        auto size = col_schema.type_info()->size;
        row_width += size;
      }
    }

    // Prefetch if we expect size limit to occur first, so there will
    // be multiple RPCs until row_limit is reached
    if (row_width > 0 && (predicted_size_limit / row_width) < row_limit) {
      suppress_next_result_prefetching_ = false;
    }
  }

  req.set_limit(row_limit);
  req.set_size_limit(predicted_size_limit);

  VLOG(3) << __func__
          << " exec_params_.limit_count=" << exec_params_.limit_count
          << " exec_params_.limit_offset=" << exec_params_.limit_offset
          << " exec_params_.limit_use_default=" << exec_params_.limit_use_default
          << " predicted_row_limit=" << predicted_row_limit
          << " row_limit=" << row_limit
          << (row_limit == 0 ? " (Unlimited)" : "")
          << " size_limit=" << predicted_size_limit
          << (predicted_size_limit == 0 ? " (Unlimited)" : "");
  return Status::OK();
}

void PgDocReadOp::SetRowMark() {
  auto& req = read_op_->read_request();
  const auto row_mark_type = GetRowMarkType(&exec_params_);
  if (IsValidRowMarkType(row_mark_type)) {
    req.set_row_mark_type(row_mark_type);
    req.set_wait_policy(static_cast<yb::WaitPolicy>(exec_params_.docdb_wait_policy));
  } else {
    req.clear_row_mark_type();
  }
}

void PgDocReadOp::SetBackfillSpec() {
  auto& req = read_op_->read_request();

  if (exec_params_.bfinstr) {
    req.dup_backfill_spec(Slice(exec_params_.bfinstr));
  } else {
    req.clear_backfill_spec();
  }
}

void PgDocReadOp::SetReadTimeForBackfill() {
  if (exec_params_.is_index_backfill) {
    read_op_->read_request().set_is_for_backfill(true);
    // TODO: Change to RSTATUS_DCHECK
    DCHECK(exec_params_.backfill_read_time);
    read_op_->set_read_time(ReadHybridTime::FromUint64(exec_params_.backfill_read_time));
  }
}

void PgDocReadOp::ResetInactivePgsqlOps() {
  if (pgsql_ops_.empty()) {
    return;
  }
  if (pgsql_op_arena_ && !HasActiveOps()) {
    // All past operations are done, can perform full reset to release memory
    VLOG_WITH_FUNC(3) << "do full reset";
    ResetResultStream();
    pgsql_ops_.clear();
    pgsql_op_arena_->Reset(ResetMode::kKeepLast);
    return;
  }
  // Clear the existing requests.
  for (auto& read_req : std::views::reverse(pgsql_ops_) |
                        std::views::take_while(std::not_fn(&IsActiveOp)) |
                        std::views::transform(&AsReadReq)) {
    read_req.clear_ybctid_column_value();
    read_req.mutable_batch_arguments()->clear();
    read_req.clear_hash_code();
    read_req.clear_max_hash_code();
    read_req.clear_paging_state();
    read_req.clear_lower_bound();
    read_req.clear_upper_bound();
  }
}

Status PgDocReadOp::ResetPgsqlOps() {
  SCHECK(!HasActiveOps(), IllegalState, "Can't reset operations when some of them are active");
  // Discard outstanding results, if any
  result_stream_ = nullptr;
  // Request cleanup for recycling like in ResetInactivePgsqlOps isn't sufficient here
  pgsql_ops_.clear();
  if (pgsql_op_arena_) {
    pgsql_op_arena_->Reset(ResetMode::kKeepLast);
  }
  if (hash_permutations_) {
    hash_permutations_->Reset();
  }
  request_population_completed_ = false;
  return Status::OK();
}

void PgDocReadOp::FormulateRequestForRollingUpgrade(LWPgsqlReadRequestPB *read_req) {
  if (read_req->batch_arguments().empty()) {
    return;
  }

  // Copy first batch-argument to scalar arg because older server does not support batch arguments.
  auto& batch_arg = read_req->mutable_batch_arguments()->front();

  if (batch_arg.has_ybctid()) {
    read_req->ref_ybctid_column_value(batch_arg.mutable_ybctid());
  }

  if (!batch_arg.partition_column_values().empty()) {
    read_req->set_hash_code(batch_arg.hash_code());
    read_req->set_max_hash_code(batch_arg.max_hash_code());
    read_req->ref_partition_column_values(batch_arg.mutable_partition_column_values());
  }
}

PgsqlReadOp& PgDocReadOp::GetReadOp(size_t op_index) {
  DCHECK(op_index < pgsql_ops_.size());
  return AsReadOp(pgsql_ops_[op_index]);
}

LWPgsqlReadRequestPB& PgDocReadOp::GetReadReq(size_t op_index) {
  return GetReadOp(op_index).read_request();
}

Result<bool> PgDocReadOp::SetLowerUpperBound(LWPgsqlReadRequestPB* request, size_t partition) {
  const auto& partition_keys = table_->GetPartitionList();
  const std::string default_upper_bound;
  const auto& upper_bound = (partition < partition_keys.size() - 1)
      ? partition_keys[partition + 1]
      : default_upper_bound;
  return ApplyPartitionBounds(*request,
                              partition_keys[partition],
                              /* lower_bound_is_inclusive =*/true,
                              upper_bound,
                              /* upper_bound_is_inclusive =*/false,
                              table_->schema());
}

void PgDocReadOp::ClonePgsqlOps(size_t op_count) {
  // Allocate batch operator, one per partition.
  DCHECK_GT(op_count, 0);
  DCHECK(!end_of_data_);
  pgsql_ops_.reserve(op_count);
  const auto& arena = pgsql_op_arena_ ? pgsql_op_arena_ : GetSharedArena(read_op_);
  while (pgsql_ops_.size() < op_count) {
    pgsql_ops_.push_back(read_op_->DeepCopy(arena));
    // Initialize as inactive. Turn it on when setup argument for a specific partition.
    pgsql_ops_.back()->set_active(false);
  }
  AddOpsToResultStream();
  // Set parallelism_level_ to maximum possible of operators to be executed at one time.
  parallelism_level_ = pgsql_ops_.size();
}

//--------------------------------------------------------------------------------------------------

PgDocWriteOp::PgDocWriteOp(const PgSession::ScopedRefPtr& pg_session,
                           PgTable* table,
                           PgsqlWriteOpPtr write_op)
    : PgDocOp(pg_session, table), write_op_(std::move(write_op)) {
}

Status PgDocWriteOp::CompleteProcessResponse() {
  end_of_data_ = true;
  return Status::OK();
}

Result<bool> PgDocWriteOp::DoCreateRequests() {
  // Setup a singular operator.
  pgsql_ops_.push_back(write_op_);
  pgsql_ops_.back()->set_active(true);
  AddOpsToResultStream();

  // Log non buffered request.
  VLOG_IF(1, response_.Valid()) << __PRETTY_FUNCTION__ << ": Sending request for " << this;
  return true;
}

void PgDocWriteOp::SetWriteTime(const HybridTime& write_time) {
  write_op_->SetWriteTime(write_time);
}

LWPgsqlWriteRequestPB& PgDocWriteOp::GetWriteOp(int op_index) {
  return down_cast<PgsqlWriteOp&>(*pgsql_ops_[op_index]).write_request();
}

PgDocOp::SharedPtr MakeDocReadOpWithData(
    const PgSession::ScopedRefPtr& pg_session, PrefetchedDataHolder data) {
  return std::make_shared<PgDocReadOpCached>(pg_session, std::move(data));
}

bool ApplyBounds(
    LWPgsqlReadRequestPB& req,
    Slice lower_bound, bool lower_bound_is_inclusive,
    Slice upper_bound, bool upper_bound_is_inclusive) {
  ApplyLowerBound(req, lower_bound, lower_bound_is_inclusive);
  ApplyUpperBound(req, upper_bound, upper_bound_is_inclusive);
  return CheckScanBoundary(req);
}

dockv::KeyBytes HashCodeToDocKeyBound(
    const Schema& schema, uint16_t hash, bool is_inclusive, bool is_lower) {
  if (!is_inclusive) {
    if (is_lower) {
      DCHECK(hash != UINT16_MAX) << Format("Invalid hash code bound '> $0'", UINT16_MAX);
      ++hash;
    } else {
      DCHECK(hash != 0) << "Invalid hash code bound '< 0'";
      --hash;
    }
  }

  using dockv::KeyEntryValues;
  using dockv::KeyEntryValue;
  using dockv::KeyEntryType;

  static const KeyEntryValues kLowest{KeyEntryValue{KeyEntryType::kLowest}};
  static const KeyEntryValues kHighest{KeyEntryValue{KeyEntryType::kHighest}};

  const auto& hash_range_components = is_lower ? kLowest : kHighest;

  return dockv::DocKey(schema, hash, hash_range_components, hash_range_components).Encode();
}

void ApplyLowerBound(LWPgsqlReadRequestPB& req, Slice lower_bound, bool is_inclusive) {
  if (lower_bound.empty()) {
    return;
  }

  // With GHI#28219, bounds are expected to be dockeys.
  DCHECK(!dockv::PartitionSchema::IsValidHashPartitionKeyBound(lower_bound));

  if (req.has_lower_bound()) {
    const auto key = req.lower_bound().key();
    // With GHI#28219, bounds are expected to be dockeys.
    DCHECK(!dockv::PartitionSchema::IsValidHashPartitionKeyBound(key));
    const auto diff = key.compare(lower_bound);
    if (diff > 0) {
      return;
    }

    if (!diff) {
      is_inclusive = is_inclusive & req.lower_bound().is_inclusive();
      req.mutable_lower_bound()->set_is_inclusive(is_inclusive);
      return;
    }
    // req->lower_bound() < lower_bound
  }
  req.mutable_lower_bound()->dup_key(lower_bound);
  req.mutable_lower_bound()->set_is_inclusive(is_inclusive);
}

void ApplyUpperBound(LWPgsqlReadRequestPB& req, Slice upper_bound, bool is_inclusive) {
  if (upper_bound.empty()) {
    return;
  }

  // With GHI#28219, bounds are expected to be dockeys.
  DCHECK(!dockv::PartitionSchema::IsValidHashPartitionKeyBound(upper_bound));

  if (req.has_upper_bound()) {
    const auto key = req.upper_bound().key();
    // With GHI#28219, bounds are expected to be dockeys.
    DCHECK(!dockv::PartitionSchema::IsValidHashPartitionKeyBound(key));
    const auto diff = key.compare(upper_bound);

    if (diff < 0) {
      return;
    }

    if (!diff) {
      is_inclusive = is_inclusive & req.upper_bound().is_inclusive();
      req.mutable_upper_bound()->set_is_inclusive(is_inclusive);
      return;
    }
    // req->upper_bound() > upper_bound
  }
  req.mutable_upper_bound()->dup_key(upper_bound);
  req.mutable_upper_bound()->set_is_inclusive(is_inclusive);
}

}  // namespace yb::pggate
