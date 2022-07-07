//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#include "yb/yql/pggate/pg_doc_op.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "yb/common/row_mark.h"

#include "yb/gutil/casts.h"
#include "yb/gutil/strings/escaping.h"

#include "yb/rpc/outbound_call.h"

#include "yb/util/random_util.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"

#include "yb/yql/pggate/pg_expr.h"
#include "yb/yql/pggate/pg_table.h"
#include "yb/yql/pggate/pg_tools.h"
#include "yb/yql/pggate/pggate_flags.h"
#include "yb/yql/pggate/util/pg_doc_data.h"

using std::lower_bound;
using std::list;
using std::vector;
using std::shared_ptr;
using std::make_shared;
using std::unique_ptr;
using std::move;

namespace yb {
namespace pggate {
namespace {

struct PgDocReadOpCachedHelper {
  PgTable dummy_table;
};

class PgDocReadOpCached : private PgDocReadOpCachedHelper, public PgDocOp {
 public:
  PgDocReadOpCached(const PgSession::ScopedRefPtr& pg_session, PrefetchedDataHolder data)
      : PgDocOp(pg_session, &dummy_table), data_(move(data)) {
  }

  Status GetResult(std::list<PgDocResult> *rowsets) override {
    if (data_) {
      for (const auto& d : *data_) {
        rowsets->emplace_back(d);
      }
      data_.reset();
    }
    return Status::OK();
  }

  Status ExecuteInit(const PgExecParameters* exec_params) override {
    return Status::OK();
  }

  Result<RequestSent> Execute(bool force_non_bufferable) override {
    return RequestSent::kTrue;
  }

  bool IsWrite() const override {
    return false;
  }

 protected:
  Status DoPopulateDmlByYbctidOps(const YbctidGenerator& generator) override {
    return Status::OK();
  }

  Result<bool> DoCreateRequests() override {
    return STATUS(InternalError, "DoCreateRequests is not defined for PgDocReadOpCached");
  }

  PgsqlOpPtr CloneFromTemplate() override {
    CHECK(false) << "CloneFromTemplate is not defined for PgDocReadOpCached";
    return nullptr;
  }

 private:
  Status CompleteProcessResponse() override {
    return STATUS(InternalError, "CompleteProcessResponse is not defined for PgDocReadOpCached");
  }

  PrefetchedDataHolder data_;
};

} // namespace

PgDocResult::PgDocResult(rpc::SidecarHolder data) : data_(std::move(data)) {
  PgDocData::LoadCache(data_.second, &row_count_, &row_iterator_);
}

PgDocResult::PgDocResult(rpc::SidecarHolder data, std::list<int64_t> row_orders)
    : data_(std::move(data)), row_orders_(std::move(row_orders)) {
  PgDocData::LoadCache(data_.second, &row_count_, &row_iterator_);
}

PgDocResult::~PgDocResult() = default;

int64_t PgDocResult::NextRowOrder() {
  return row_orders_.size() > 0 ? row_orders_.front() : -1;
}

Status PgDocResult::WritePgTuple(const std::vector<PgExpr*>& targets, PgTuple *pg_tuple,
                                 int64_t *row_order) {
  int attr_num = 0;
  for (const PgExpr *target : targets) {
    if (!target->is_colref() && !target->is_aggregate()) {
      return STATUS(InternalError,
                    "Unexpected expression, only column refs or aggregates supported here");
    }
    if (target->opcode() == PgColumnRef::Opcode::PG_EXPR_COLREF) {
      attr_num = static_cast<const PgColumnRef *>(target)->attr_num();
    } else {
      attr_num++;
    }

    PgWireDataHeader header = PgDocData::ReadDataHeader(&row_iterator_);
    target->TranslateData(&row_iterator_, header, attr_num - 1, pg_tuple);
  }

  if (row_orders_.size()) {
    *row_order = row_orders_.front();
    row_orders_.pop_front();
  } else {
    *row_order = -1;
  }
  return Status::OK();
}

Status PgDocResult::ProcessSystemColumns() {
  if (syscol_processed_) {
    return Status::OK();
  }
  syscol_processed_ = true;

  for (int i = 0; i < row_count_; i++) {
    PgWireDataHeader header = PgDocData::ReadDataHeader(&row_iterator_);
    SCHECK(!header.is_null(), InternalError, "System column ybctid cannot be NULL");

    int64_t data_size;
    size_t read_size = PgDocData::ReadNumber(&row_iterator_, &data_size);
    row_iterator_.remove_prefix(read_size);

    ybctids_.emplace_back(row_iterator_.data(), data_size);
    row_iterator_.remove_prefix(data_size);
  }
  return Status::OK();
}

Status PgDocResult::ProcessSparseSystemColumns(std::string *reservoir) {
  // Process block sampling result returned from DocDB.
  // Results come as (index, ybctid) tuples where index is the position in the reservoir of
  // predetermined size. DocDB returns ybctids with sequential indexes first, starting from 0 and
  // until reservoir is full. Then it returns ybctids with random indexes, so they replace previous
  // ybctids.
  for (int i = 0; i < row_count_; i++) {
    // Read index column
    PgWireDataHeader header = PgDocData::ReadDataHeader(&row_iterator_);
    SCHECK(!header.is_null(), InternalError, "Reservoir index cannot be NULL");
    int32_t index;
    size_t read_size = PgDocData::ReadNumber(&row_iterator_, &index);
    row_iterator_.remove_prefix(read_size);
    // Read ybctid column
    header = PgDocData::ReadDataHeader(&row_iterator_);
    SCHECK(!header.is_null(), InternalError, "System column ybctid cannot be NULL");
    int64_t data_size;
    read_size = PgDocData::ReadNumber(&row_iterator_, &data_size);
    row_iterator_.remove_prefix(read_size);

    // Copy ybctid data to the reservoir
    reservoir[index].assign(reinterpret_cast<const char *>(row_iterator_.data()), data_size);
    row_iterator_.remove_prefix(data_size);
  }
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

PgDocResponse::PgDocResponse(PerformFuture future, uint64_t in_txn_limit)
    : holder_(PerformInfo{.future = std::move(future), .in_txn_limit = in_txn_limit}) {}

PgDocResponse::PgDocResponse(ProviderPtr provider)
    : holder_(std::move(provider)) {}

bool PgDocResponse::Valid() const {
  return std::holds_alternative<PerformInfo>(holder_)
      ? std::get<PerformInfo>(holder_).future.Valid()
      : static_cast<bool>(std::get<ProviderPtr>(holder_));
}

Result<PgDocResponse::Data> PgDocResponse::Get() {
  if (std::holds_alternative<PerformInfo>(holder_)) {
    auto& info = std::get<PerformInfo>(holder_);
    return Data(VERIFY_RESULT(info.future.Get()), info.in_txn_limit);
  }
  // Detach provider pointer after first usage to make PgDocResponse::Valid return false.
  ProviderPtr provider;
  std::get<ProviderPtr>(holder_).swap(provider);
  return provider->Get();
}

//--------------------------------------------------------------------------------------------------

PgDocOp::PgDocOp(const PgSession::ScopedRefPtr& pg_session, PgTable* table, const Sender& sender)
    : pg_session_(pg_session), table_(*table), sender_(sender) {}

PgDocOp::~PgDocOp() {
  // Wait for result in case request was sent.
  // Operation can be part of transaction it is necessary to complete it before transaction commit.
  if (response_.Valid()) {
    WARN_NOT_OK(ResultToStatus(response_.Get()), "Operation completion failed");
  }
}

Status PgDocOp::ExecuteInit(const PgExecParameters *exec_params) {
  end_of_data_ = false;
  if (exec_params) {
    exec_params_ = *exec_params;
  }
  return Status::OK();
}

const PgExecParameters& PgDocOp::ExecParameters() const {
  return exec_params_;
}

Result<RequestSent> PgDocOp::Execute(bool force_non_bufferable) {
  // As of 09/25/2018, DocDB doesn't cache or keep any execution state for a statement, so we
  // have to call query execution every time.
  // - Normal SQL convention: Exec, Fetch, Fetch, ...
  // - Our SQL convention: Exec & Fetch, Exec & Fetch, ...
  // This refers to the sequence of operations between this layer and the underlying tablet
  // server / DocDB layer, not to the sequence of operations between the PostgreSQL layer and this
  // layer.
  RETURN_NOT_OK(SendRequest(force_non_bufferable));
  return RequestSent(response_.Valid());
}

Status PgDocOp::GetResult(list<PgDocResult> *rowsets) {
  // If the execution has error, return without reading any rows.
  RETURN_NOT_OK(exec_status_);

  if (!end_of_data_) {
    // Send request now in case prefetching was suppressed.
    if (suppress_next_result_prefetching_ && !response_.Valid()) {
      RETURN_NOT_OK(SendRequest(true /* force_non_bufferable */));
    }

    DCHECK(response_.Valid());
    auto result = response_.Get();
    auto rows = VERIFY_RESULT(ProcessResponse(result));
    // In case ProcessResponse doesn't fail with an error
    // it should return non empty rows and/or set end_of_data_.
    DCHECK(!rows.empty() || end_of_data_);
    rowsets->splice(rowsets->end(), rows);
    // Prefetch next portion of data if needed.
    if (!(end_of_data_ || suppress_next_result_prefetching_)) {
      RETURN_NOT_OK(SendRequest(true /* force_non_bufferable */));
    }
  }

  return Status::OK();
}

Result<int32_t> PgDocOp::GetRowsAffectedCount() const {
  RETURN_NOT_OK(exec_status_);
  DCHECK(end_of_data_);
  return rows_affected_count_;
}

Status PgDocOp::ClonePgsqlOps(size_t op_count) {
  // Allocate batch operator, one per partition.
  SCHECK(op_count > 0, InternalError, "Table must have at least one partition");
  if (pgsql_ops_.size() < op_count) {
    pgsql_ops_.resize(op_count);
    for (auto& op : pgsql_ops_) {
      op = CloneFromTemplate();

      // Initialize as inactive. Turn it on when setup argument for a specific partition.
      op->set_active(false);
    }

    // Set parallism_level_ to maximum possible of operators to be executed at one time.
    parallelism_level_ = pgsql_ops_.size();
  }

  return Status::OK();
}

void PgDocOp::MoveInactiveOpsOutside() {
  // Move inactive op to the end.
  const ssize_t total_op_count = pgsql_ops_.size();
  bool has_sorting_order = !batch_row_orders_.empty();
  ssize_t left_iter = 0;
  ssize_t right_iter = total_op_count - 1;
  while (true) {
    // Advance left iterator.
    while (left_iter < total_op_count && pgsql_ops_[left_iter]->is_active()) left_iter++;

    // Advance right iterator.
    while (right_iter >= 0 && !pgsql_ops_[right_iter]->is_active()) right_iter--;

    // Move inactive operator to the end by swapping the pointers.
    if (left_iter < right_iter) {
      std::swap(pgsql_ops_[left_iter], pgsql_ops_[right_iter]);
      if (has_sorting_order) {
        std::swap(batch_row_orders_[left_iter], batch_row_orders_[right_iter]);
      }
    } else {
      break;
    }
  }

  // Set active op count.
  active_op_count_ = left_iter;
}

Status PgDocOp::SendRequest(bool force_non_bufferable) {
  DCHECK(exec_status_.ok());
  DCHECK(!response_.Valid());
  exec_status_ = SendRequestImpl(force_non_bufferable);
  return exec_status_;
}

Status PgDocOp::SendRequestImpl(bool force_non_bufferable) {
  // Populate collected information into protobuf requests before sending to DocDB.
  RETURN_NOT_OK(CreateRequests());

  // Currently, send and receive individual request of a batch is not yet supported
  // - Among statements, only queries by BASE-YBCTIDs need to be sent and received in batches
  //   to honor the order of how the BASE-YBCTIDs are kept in the database.
  // - For other type of statements, it could be more efficient to send them individually.
  SCHECK(wait_for_batch_completion_, InternalError,
         "Only send and receive the whole batch is supported");

  // Send at most "parallelism_level_" number of requests at one time.
  size_t send_count = std::min(parallelism_level_, active_op_count_);
  response_ = VERIFY_RESULT(sender_(
      pg_session_.get(), pgsql_ops_.data(), send_count,
      *table_, GetInTxnLimit(), force_non_bufferable));
  return Status::OK();
}

Result<std::list<PgDocResult>> PgDocOp::ProcessResponse(
    const Result<PgDocResponse::Data>& response) {
  VLOG(1) << __PRETTY_FUNCTION__ << ": Received response for request " << this;
  // Check operation status.
  DCHECK(exec_status_.ok());
  auto result = ProcessResponseImpl(response);
  if (result.ok()) {
    return result;
  }
  exec_status_ = result.status();
  return exec_status_;
}

Result<std::list<PgDocResult>> PgDocOp::ProcessResponseImpl(
    const Result<PgDocResponse::Data>& response) {
  if (!response.ok()) {
    return response.status();
  }
  const auto& data = *response;
  auto result = VERIFY_RESULT(ProcessCallResponse(*data.response));
  GetInTxnLimit() = data.in_txn_limit;
  RETURN_NOT_OK(CompleteProcessResponse());
  return result;
}

Result<std::list<PgDocResult>> PgDocOp::ProcessCallResponse(const rpc::CallResponse& response) {
  // Process data coming from tablet server.
  std::list<PgDocResult> result;
  bool no_sorting_order = batch_row_orders_.empty();

  rows_affected_count_ = 0;
  // Check for errors reported by tablet server.
  for (size_t op_index = 0; op_index < active_op_count_; op_index++) {
    auto& pgsql_op = pgsql_ops_[op_index];
    auto* op_response = pgsql_op->response();
    if (!op_response) {
      continue;
    }
    // Get total number of rows that are operated on.
    rows_affected_count_ += op_response->rows_affected_count();

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

    // Get contents.
    if (!op_response->has_rows_data_sidecar()) {
      continue;
    }
    auto rows_data = VERIFY_RESULT(response.GetSidecarHolder(op_response->rows_data_sidecar()));
    if (no_sorting_order) {
      result.emplace_back(std::move(rows_data));
    } else {
      const auto& batch_orders = op_response->batch_orders();
      if (!batch_orders.empty()) {
        result.emplace_back(std::move(rows_data),
                            std::list<int64_t>(batch_orders.begin(), batch_orders.end()));
      } else {
        result.emplace_back(std::move(rows_data), std::move(batch_row_orders_[op_index]));
      }
    }
  }

  return result;
}

uint64_t& PgDocOp::GetInTxnLimit() {
  return exec_params_.statement_in_txn_limit ? *exec_params_.statement_in_txn_limit
                                             : in_txn_limit_;
}

Status PgDocOp::CreateRequests() {
  if (!request_population_completed_) {
    if (VERIFY_RESULT(DoCreateRequests())) {
      request_population_completed_ = true;
    }
  }
  return CompleteRequests();
}

Status PgDocOp::PopulateDmlByYbctidOps(const YbctidGenerator& generator) {
  RETURN_NOT_OK(DoPopulateDmlByYbctidOps(generator));
  request_population_completed_ = true;
  return CompleteRequests();
}

Status PgDocOp::CompleteRequests() {
  for (size_t i = 0; i != active_op_count_; ++i) {
    RETURN_NOT_OK(pgsql_ops_[i]->InitPartitionKey(*table_));
  }
  return Status::OK();
}

Result<PgDocResponse> PgDocOp::DefaultSender(
    PgSession* session, const PgsqlOpPtr* ops, size_t ops_count, const PgTableDesc& table,
    uint64_t in_txn_limit, bool force_non_bufferable) {
  auto result = VERIFY_RESULT(session->RunAsync(
      ops, ops_count, table, &in_txn_limit, force_non_bufferable));
  return PgDocResponse(std::move(result), in_txn_limit);
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

Status PgDocReadOp::ExecuteInit(const PgExecParameters *exec_params) {
  SCHECK(pgsql_ops_.empty(),
         IllegalState,
         "Exec params can't be changed for already created operations");
  RETURN_NOT_OK(PgDocOp::ExecuteInit(exec_params));

  read_op_->read_request().set_return_paging_state(true);
  // TODO(10696): This is probably the only place in pg_doc_op where pg_session is being
  // used as a source of truth. All other uses treat it as stateless. Refactor to move this
  // state elsewhere.
  if (pg_session_->ShouldUseFollowerReads()) {
    read_op_->set_read_from_followers();
  }
  SetRequestPrefetchLimit();
  SetBackfillSpec();
  SetRowMark();
  SetReadTimeForBackfill();
  return Status::OK();
}

Result<bool> PgDocReadOp::DoCreateRequests() {
  // All information from the SQL request has been collected and setup. This code populates
  // Protobuf requests before sending them to DocDB. For performance reasons, requests are
  // constructed differently for different statements.
  if (read_op_->read_request().has_sampling_state()) {
    VLOG(1) << __PRETTY_FUNCTION__ << ": Preparing sampling requests ";
    return PopulateSamplingOps();

  // Requests pushing down aggregates and/or filter expression tend to do more work on DocDB side,
  // so it takes longer to return responses, and Postgres side has generally less work to do.
  // Hence we optimize by sending multiple parallel requests to the nodes, allowing their
  // simultaneous processing.
  // Effect may be less than expected if the nodes are already heavily loaded and CPU consumption
  // is high, or selectivity of the filter is low.
  } else if (read_op_->read_request().is_aggregate() ||
             !read_op_->read_request().where_clauses().empty()) {
    return PopulateParallelSelectOps();

  } else if (!read_op_->read_request().partition_column_values().empty()) {
    // Optimization for multiple hash keys.
    // - SELECT * FROM sql_table WHERE hash_c1 IN (1, 2, 3) AND hash_c2 IN (4, 5, 6);
    // - Multiple requests for differrent hash permutations / keys.
    return PopulateNextHashPermutationOps();

  } else {
    // No optimization.
    if (exec_params_.partition_key != nullptr) {
      RETURN_NOT_OK(SetScanPartitionBoundary());
    }
    pgsql_ops_.emplace_back(read_op_);
    pgsql_ops_.back()->set_active(true);
    active_op_count_ = 1;
    return true;
  }
}

Status PgDocReadOp::DoPopulateDmlByYbctidOps(const YbctidGenerator& generator) {
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
  RETURN_NOT_OK(InitializeYbctidOperators());
  const auto& partition_keys = table_->GetPartitions();
  std::vector<std::string> lowest_ybctid{partition_keys.size(), std::string()};
  // Begin a batch of ybctids.
  end_of_data_ = false;
  // Assign ybctid values.
  while (true) {
    auto ybctid = generator();
    if (ybctid.empty()) {
      break;
    }
    // Find partition. The partition index is the boundary order minus 1.
    // - For hash partitioning, we use hashcode to find the right index.
    // - For range partitioning, we pass partition key to seek the index.
    const auto partition = VERIFY_RESULT(table_->FindPartitionIndex(ybctid));
    auto& lowest_ybctid_for_partition = lowest_ybctid[partition];
    if (lowest_ybctid_for_partition.empty() || ybctid < Slice(lowest_ybctid_for_partition)) {
      lowest_ybctid_for_partition = ybctid.ToBuffer();
    }
    // Append ybctid and its order to batch_arguments.
    // The "ybctid" values are returned in the same order as the row in the IndexTable. To keep
    // track of this order, each argument is assigned an order-number.
    auto* batch_arg = GetReadReq(partition).add_batch_arguments();
    batch_arg->set_order(batch_row_ordering_counter_);
    batch_arg->mutable_ybctid()->mutable_value()->dup_binary_value(ybctid);

    // Remember the order number for each request.
    batch_row_orders_[partition].push_back(batch_row_ordering_counter_);

    // Increment counter for the next row.
    ++batch_row_ordering_counter_;
  }
  // For each read request we must set "ybctid_column_value" for two reasons:
  // - "client::yb_op" uses it to set the hash_code.
  // - Rolling upgrade: Older server will read only "ybctid_column_value" as it doesn't know
  //   of ybctid-batching operation.
  // Note: lowest ybctid per partition must be used to correctly handle possible dynamic table
  // splitting.
  for (size_t partition = 0; partition < partition_keys.size(); ++partition) {
    const auto& ybctid = lowest_ybctid[partition];
    if (ybctid.empty()) {
      // No ybctids were assigned for current partition, ignore it.
      continue;
    }
    auto& read_req = GetReadReq(partition);
    pgsql_ops_[partition]->set_active(true);
    read_req.set_is_forward_scan(true);
    read_req.mutable_ybctid_column_value()->mutable_value()->dup_binary_value(ybctid);

    // For every read operation set partition boundary. In case a tablet is split between
    // preparing requests and executing them, DocDB will return a paging state for pggate to
    // continue till the end of current tablet is reached.
    std::string upper_bound;
    if (partition < partition_keys.size() - 1) {
      upper_bound = partition_keys[partition + 1];
    }
    RETURN_NOT_OK(table_->SetScanBoundary(&read_req,
                                          partition_keys[partition],
                                          /* lower_bound_is_inclusive */ true,
                                          upper_bound,
                                          /* upper_bound_is_inclusive */ false));
  }

  // Done creating request, but not all partition or operator has arguments (inactive).
  MoveInactiveOpsOutside();

  return Status::OK();
}

Status PgDocReadOp::InitializeYbctidOperators() {
  auto op_count = table_->GetPartitionCount();

  if (batch_row_orders_.size() == 0) {
    // First batch:
    // - Create operators.
    // - Allocate row orders for each tablet server.
    // - Protobuf fields in requests are not yet set so not needed to be cleared.
    RETURN_NOT_OK(ClonePgsqlOps(op_count));
    batch_row_orders_.resize(op_count);

    // To honor the indexing order of ybctid values, for each batch of ybctid-binds, select all rows
    // in the batch and then order them before returning result to Postgres layer.
    wait_for_batch_completion_ = true;

  } else {
    // Second and later batches: Reuse all state variables.
    // - Clear row orders for this batch to be set later.
    // - Clear protobuf fields ybctids and others before reusing them in this batch.
    RETURN_NOT_OK(ResetInactivePgsqlOps());
  }
  return Status::OK();
}

Result<bool> PgDocReadOp::PopulateNextHashPermutationOps() {
  RETURN_NOT_OK(InitializeHashPermutationStates());

  // Set the index at the start of inactive operators.
  auto op_count = pgsql_ops_.size();
  auto op_index = active_op_count_;

  // Fill inactive operators with new hash permutations.
  const size_t hash_column_count = table_->num_hash_key_columns();
  for (; op_index < op_count && next_permutation_idx_ < total_permutation_count_; ++op_index) {
    auto& read_req = GetReadReq(op_index);
    pgsql_ops_[op_index]->set_active(true);

    int pos = next_permutation_idx_++;
    auto it = read_req.mutable_partition_column_values()->end();
    std::advance(it, hash_column_count - read_req.mutable_partition_column_values()->size());
    for (int c_idx = narrow_cast<int>(hash_column_count); c_idx-- > 0;) {
      --it;
      int sel_idx = pos % partition_exprs_[c_idx].size();
      // TODO(LW_PERFORM)
      *it = *partition_exprs_[c_idx][sel_idx];
      pos /= partition_exprs_[c_idx].size();
    }
  }
  active_op_count_ = op_index;

  // Stop adding requests if we reach the total number of permutations.
  return next_permutation_idx_ >= total_permutation_count_;
}

// Collect hash expressions to prepare for generating permutations.
Status PgDocReadOp::InitializeHashPermutationStates() {
  // Return if state variables were initialized.
  if (!partition_exprs_.empty()) {
    // Reset the protobuf request before reusing the operators.
    return ResetInactivePgsqlOps();
  }

  // Initialize partition_exprs_.
  // Reorganize the input arguments from Postgres to prepre for permutation generation.
  const size_t hash_column_count = table_->num_hash_key_columns();
  partition_exprs_.resize(hash_column_count);
  size_t c_idx = 0;
  for (const auto& col_expr : read_op_->read_request().partition_column_values()) {
    if (col_expr.has_condition()) {
      auto it = ++col_expr.condition().operands().begin();
      for (const auto& expr : it->condition().operands()) {
        partition_exprs_[c_idx].push_back(&expr);
      }
    } else {
      partition_exprs_[c_idx].push_back(&col_expr);
    }
    ++c_idx;
  }

  // Calculate the total number of permutations to be generated.
  total_permutation_count_ = 1;
  for (auto& exprs : partition_exprs_) {
    total_permutation_count_ *= exprs.size();
  }

  // Create operators, one operation per partition, up to FLAGS_ysql_request_limit.
  //
  // TODO(neil) The control variable "ysql_request_limit" should be applied to ALL statements, but
  // at the moment, the number of operators never exceeds the number of tablets except for hash
  // permutation operation, so the work on this GFLAG can be done when it is necessary.
  int max_op_count = std::min(total_permutation_count_, FLAGS_ysql_request_limit);
  RETURN_NOT_OK(ClonePgsqlOps(max_op_count));

  // Clear the original partition expressions as it will be replaced with hash permutations.
  for (int op_index = 0; op_index < max_op_count; op_index++) {
    auto& read_request = GetReadReq(op_index);
    read_request.mutable_partition_column_values()->clear();
    for (size_t i = 0; i < hash_column_count; ++i) {
      read_request.add_partition_column_values();
    }
    pgsql_ops_[op_index]->set_active(false);
  }

  // Initialize counters.
  next_permutation_idx_ = 0;
  active_op_count_ = 0;

  return Status::OK();
}

Result<bool> PgDocReadOp::PopulateParallelSelectOps() {
  // Create batch operators, one per partition, to execute in parallel.
  // TODO(tsplit): what if table partition is changed during PgDocReadOp lifecycle before or after
  // the following line?
  RETURN_NOT_OK(ClonePgsqlOps(table_->GetPartitionCount()));
  // Set "pararallelism_level_" to control how many operators can be sent at one time.
  //
  // TODO(neil) The calculation for this control variable should be applied to ALL operators, but
  // the following calculation needs to be refined before it can be used for all statements.
  auto parallelism_level = FLAGS_ysql_select_parallelism;
  if (parallelism_level < 0) {
    int tserver_count = VERIFY_RESULT(pg_session_->TabletServerCount(true /* primary_only */));

    // Establish lower and upper bounds on parallelism.
    int kMinParSelParallelism = 1;
    int kMaxParSelParallelism = 16;
    parallelism_level_ =
      std::min(std::max(tserver_count * 2, kMinParSelParallelism), kMaxParSelParallelism);
  } else {
    parallelism_level_ = parallelism_level;
  }

  // Assign partitions to operators.
  const auto& partition_keys = table_->GetPartitions();
  SCHECK_EQ(partition_keys.size(), pgsql_ops_.size(), IllegalState,
            "Number of partitions and number of partition keys are not the same");

  for (size_t partition = 0; partition < partition_keys.size(); partition++) {
    // Construct a new YBPgsqlReadOp.
    pgsql_ops_[partition]->set_active(true);

    // Use partition index to setup the protobuf to identify the partition that this request
    // is for. Batcher will use this information to send the request to correct tablet server, and
    // server uses this information to operate on correct tablet.
    // - Range partition uses range partition key to identify partition.
    // - Hash partition uses "next_partition_key" and "max_hash_code" to identify partition.
    string upper_bound;
    if (partition < partition_keys.size() - 1) {
      upper_bound = partition_keys[partition + 1];
    }
    RETURN_NOT_OK(table_->SetScanBoundary(&GetReadReq(partition),
                                          partition_keys[partition],
                                          true /* lower_bound_is_inclusive */,
                                          upper_bound,
                                          false /* upper_bound_is_inclusive */));
  }
  active_op_count_ = partition_keys.size();

  return true;
}

Result<bool> PgDocReadOp::PopulateSamplingOps() {
  // Create one PgsqlOp per partition
  RETURN_NOT_OK(ClonePgsqlOps(table_->GetPartitionCount()));
  // Partitions are sampled sequentially, one at a time
  parallelism_level_ = 1;
  // Assign partitions to operators.
  const auto& partition_keys = table_->GetPartitions();
  SCHECK_EQ(partition_keys.size(), pgsql_ops_.size(), IllegalState,
            "Number of partitions and number of partition keys are not the same");

  // Bind requests to partitions
  for (size_t partition = 0; partition < partition_keys.size(); partition++) {
    // Construct a new YBPgsqlReadOp.
    pgsql_ops_[partition]->set_active(true);

    // Use partition index to setup the protobuf to identify the partition that this request
    // is for. Batcher will use this information to send the request to correct tablet server, and
    // server uses this information to operate on correct tablet.
    // - Range partition uses range partition key to identify partition.
    // - Hash partition uses "next_partition_key" and "max_hash_code" to identify partition.
    string upper_bound;
    if (partition < partition_keys.size() - 1) {
      upper_bound = partition_keys[partition + 1];
    }
    RETURN_NOT_OK(table_->SetScanBoundary(&GetReadReq(partition),
                                          partition_keys[partition],
                                          true /* lower_bound_is_inclusive */,
                                          upper_bound,
                                          false /* upper_bound_is_inclusive */));
  }
  active_op_count_ = partition_keys.size();
  VLOG(1) << "Number of partitions to sample: " << active_op_count_;
  // If we have big enough sample after processing some partitions we skip the rest.
  // By shuffling partitions we randomly select the partition(s) to sample.
  std::shuffle(pgsql_ops_.begin(), pgsql_ops_.end(), ThreadLocalRandom());

  return true;
}

Status PgDocReadOp::GetEstimatedRowCount(double *liverows, double *deadrows) {
  if (liverows != nullptr) {
    // Return estimated number of live tuples
    VLOG(1) << "Returning liverows " << sample_rows_;
    *liverows = sample_rows_;
  }
  if (deadrows != nullptr) {
    // TODO count dead tuples while sampling
    *deadrows = 0;
  }
  return Status::OK();
}

// When postgres requests to scan a specific partition, set the partition parameter accordingly.
Status PgDocReadOp::SetScanPartitionBoundary() {
  // Boundary to scan from a given key to the end of its associated tablet.
  // - Lower: The given partition key (inclusive).
  // - Upper: Beginning of next tablet (not inclusive).
  SCHECK(exec_params_.partition_key != nullptr, Uninitialized, "expected non-null partition_key");

  // Seek the tablet of the given key.
  // TODO(tsplit): what if table partition is changed during PgDocReadOp lifecycle before or after
  // the following line?
  const std::vector<std::string>& partition_keys = table_->GetPartitions();
  const auto& partition_key = std::find(
      partition_keys.begin(),
      partition_keys.end(),
      a2b_hex(exec_params_.partition_key));
  RSTATUS_DCHECK(
      partition_key != partition_keys.end(), InvalidArgument, "invalid partition key given");

  // Seek upper bound (Beginning of next tablet).
  string upper_bound;
  const auto& next_partition_key = std::next(partition_key, 1);
  if (next_partition_key != partition_keys.end()) {
    upper_bound = *next_partition_key;
  }
  RETURN_NOT_OK(table_->SetScanBoundary(
      &read_op_->read_request(), *partition_key, true /* lower_bound_is_inclusive */, upper_bound,
      false /* upper_bound_is_inclusive */));
  return Status::OK();
}

Status PgDocReadOp::CompleteProcessResponse() {
  // For each read_op, set up its request for the next batch of data or make it in-active.
  bool has_more_data = false;
  auto send_count = std::min(parallelism_level_, active_op_count_);

  for (size_t op_index = 0; op_index < send_count; op_index++) {
    auto& read_op = down_cast<PgsqlReadOp&>(*pgsql_ops_[op_index]);
    RETURN_NOT_OK(ReviewResponsePagingState(*table_, &read_op));

    // Check for completion.
    bool has_more_arg = false;
    auto& res = *read_op.response();
    auto& req = read_op.read_request();

    // Save the backfill_spec if tablet server wants to return it.
    if (res.is_backfill_batch_done()) {
      out_param_backfill_spec_ = res.backfill_spec().ToBuffer();
    } else if (PrepareNextRequest(&read_op)) {
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

    if (res.has_sampling_state()) {
      VLOG(1) << "Received sampling state:"
              << " samplerows: " << res.sampling_state().samplerows()
              << " rowstoskip: " << res.sampling_state().rowstoskip()
              << " rstate_w: " << res.sampling_state().rstate_w()
              << " rand_state: " << res.sampling_state().rand_state();
      if (has_more_arg) {
        // Copy sampling state from the response to the request, to properly continue to sample
        // the next block.
        req.ref_sampling_state(res.mutable_sampling_state());
        res.clear_sampling_state();
      } else {
        // Partition sampling is completed.
        // If samplerows is greater than or equal to targrows the sampling is complete. There are
        // enough rows selected to calculate stats and we can estimate total number of rows in the
        // table by extrapolating samplerows to the partitions that have not been scanned.
        // If samplerows is less than targrows next partition needs to be sampled. Next pgdoc_op
        // already has sampling state copied from the template_op_, only couple fields need to be
        // updated: numrows and samplerows. The targrows never changes, and in reservoir population
        // phase (before samplerows reaches targrows) 1. numrows and samplerows are always equal;
        // and 2. random numbers never generated, so random state remains the same.
        // That essentially means the only thing we need to collect from the partition's final
        // sampling state is the samplerows. We use that number to either estimate liverows, or to
        // update numrows and samplerows in next partition's sampling state.
        sample_rows_ = res.sampling_state().samplerows();
      }
    }

    if (has_more_arg) {
      has_more_data = true;
    } else {
      read_op.set_active(false);
    }
  }

  if (has_more_data || send_count < active_op_count_) {
    // Move inactive ops to the end of
    // to make room for new set of arguments.
    MoveInactiveOpsOutside();
    end_of_data_ = false;
  } else {
    // There should be no active op left in queue.
    active_op_count_ = 0;
    end_of_data_ = request_population_completed_;
  }

  if (active_op_count_ > 0 && read_op_->read_request().has_sampling_state()) {
    auto& read_op = down_cast<PgsqlReadOp&>(*pgsql_ops_[0]);
    auto *req = &read_op.read_request();
    if (!req->has_paging_state()) {
      // Current sampling op without paging state means that previous one was completed and moved
      // outside.
      auto sampling_state = req->mutable_sampling_state();
      if (sample_rows_ < sampling_state->targrows()) {
        // More sample rows are needed, update sampling state and let next partition be scanned
        VLOG(1) << "Continue sampling next partition from " << sample_rows_;
        sampling_state->set_numrows(static_cast<int32>(sample_rows_));
        sampling_state->set_samplerows(sample_rows_);
      } else {
        // Have enough of sample rows, estimate total table rows assuming they are evenly
        // distributed between partitions
        auto completed_ops = pgsql_ops_.size() - active_op_count_;
        sample_rows_ = floor((sample_rows_ / completed_ops) * pgsql_ops_.size() + 0.5);
        VLOG(1) << "Done sampling, prorated rowcount is " << sample_rows_;
        end_of_data_ = true;
      }
    }
  }

  return Status::OK();
}

void PgDocReadOp::SetRequestPrefetchLimit() {
  // Predict the maximum prefetch-limit using the associated gflags.
  auto& req = read_op_->read_request();
  auto predicted_limit = FLAGS_ysql_prefetch_limit;
  if (!req.is_forward_scan()) {
    // Backward scan is slower than forward scan, so predicted limit is a smaller number.
    predicted_limit = predicted_limit * FLAGS_ysql_backward_prefetch_scale_factor;
  }

  // System setting has to be at least 1 while user setting (LIMIT clause) can be anything that
  // is allowed by SQL semantics.
  if (predicted_limit < 1) {
    predicted_limit = 1;
  }

  // Use statement LIMIT(count + offset) if it is smaller than the predicted limit.
  auto limit = exec_params_.limit_count + exec_params_.limit_offset;
  suppress_next_result_prefetching_ = true;
  if (exec_params_.limit_use_default || limit > predicted_limit) {
    limit = predicted_limit;
    suppress_next_result_prefetching_ = false;
  }
  VLOG(3) << __func__
          << " exec_params_.limit_count=" << exec_params_.limit_count
          << " exec_params_.limit_offset=" << exec_params_.limit_offset
          << " exec_params_.limit_use_default=" << exec_params_.limit_use_default
          << " predicted_limit=" << predicted_limit
          << " limit=" << limit;
  req.set_limit(limit);
}

void PgDocReadOp::SetRowMark() {
  auto& req = read_op_->read_request();
  const auto row_mark_type = GetRowMarkType(&exec_params_);
  if (IsValidRowMarkType(row_mark_type)) {
    req.set_row_mark_type(row_mark_type);
    req.set_wait_policy(static_cast<yb::WaitPolicy>(exec_params_.wait_policy));
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

Status PgDocReadOp::ResetInactivePgsqlOps() {
  // Clear the existing ybctids.
  for (auto op_index = active_op_count_; op_index < pgsql_ops_.size(); op_index++) {
    auto& read_req = GetReadReq(op_index);
    read_req.clear_ybctid_column_value();
    read_req.mutable_batch_arguments()->clear();
    read_req.clear_hash_code();
    read_req.clear_max_hash_code();
    read_req.clear_paging_state();
    read_req.clear_lower_bound();
    read_req.clear_upper_bound();
  }

  // Clear row orders.
  if (batch_row_orders_.size() > 0) {
    for (auto op_index = active_op_count_; op_index < pgsql_ops_.size(); op_index++) {
      batch_row_orders_[op_index].clear();
    }
  }

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

LWPgsqlReadRequestPB& PgDocReadOp::GetReadReq(size_t op_index) {
  return down_cast<PgsqlReadOp&>(*pgsql_ops_[op_index]).read_request();
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
  active_op_count_ = 1;

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

}  // namespace pggate
}  // namespace yb
