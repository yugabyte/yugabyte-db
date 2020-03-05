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
#include "yb/yql/pggate/pg_txn_manager.h"

#include <boost/algorithm/string.hpp>

#include "yb/client/table.h"

#include "yb/common/pgsql_error.h"
#include "yb/common/transaction_error.h"
#include "yb/util/yb_pg_errcodes.h"
#include "yb/docdb/doc_key.h"
#include "yb/yql/pggate/pggate_flags.h"
#include "yb/yql/pggate/ybc_pggate.h"

using std::lower_bound;
using std::list;
using std::vector;
using std::shared_ptr;
using std::make_shared;
using std::move;

namespace yb {
namespace pggate {

PgDocResult::PgDocResult(string&& data) : data_(move(data)) {
  PgDocData::LoadCache(data_, &row_count_, &row_iterator_);
}

PgDocResult::PgDocResult(string&& data, std::list<int64_t>&& row_orders)
    : data_(move(data)), row_orders_(move(row_orders)) {
  PgDocData::LoadCache(data_, &row_count_, &row_iterator_);
}

PgDocResult::~PgDocResult() {
}

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

//--------------------------------------------------------------------------------------------------

PgDocOp::PgDocOp(const PgSession::ScopedRefPtr& pg_session)
    : pg_session_(pg_session) {
  exec_params_.limit_count = FLAGS_ysql_prefetch_limit;
  exec_params_.limit_offset = 0;
  exec_params_.limit_use_default = true;
}

PgDocOp::~PgDocOp() {
  // Wait for result in case request was sent.
  // Operation can be part of transaction it is necessary to complete it before transaction commit.
  if (response_.InProgress()) {
    __attribute__((unused)) auto status = response_.GetStatus();
  }
}

void PgDocOp::Initialize(const PgExecParameters *exec_params) {
  result_cache_.clear();
  end_of_data_ = false;
  if (exec_params) {
    exec_params_ = *exec_params;
  }
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
  return RequestSent(response_.InProgress());
}

Status PgDocOp::GetResult(list<PgDocResult::SharedPtr> *rowsets) {
  // If the execution has error, return without reading any rows.
  RETURN_NOT_OK(exec_status_);

  if (result_cache_.empty() && !end_of_data_) {
    DCHECK(response_.InProgress());
    RETURN_NOT_OK(ProcessResponse(response_.GetStatus()));
    // In case ProcessResponse doesn't fail with an error
    // it should fill result_cache_ and/or set end_of_data_.
    DCHECK(!result_cache_.empty() || end_of_data_);
  }

  if (!result_cache_.empty()) {
    rowsets->splice(rowsets->end(), result_cache_);
    if (result_cache_.empty() && !end_of_data_) {
      RETURN_NOT_OK(SendRequest(true /* force_non_bufferable */));
    }
  }

  return Status::OK();
}

Result<int32_t> PgDocOp::GetRowsAffectedCount() const {
  RETURN_NOT_OK(exec_status_);
  DCHECK(end_of_data_);
  return GetRowsAffectedCountImpl();
}

Status PgDocOp::SendRequest(bool force_non_bufferable) {
  DCHECK(exec_status_.ok());
  DCHECK(!response_.InProgress());
  exec_status_ = SendRequestImpl(force_non_bufferable);
  return exec_status_;
}

Status PgDocOp::ProcessResponse(const Status& status) {
  DCHECK(exec_status_.ok());
  exec_status_ = status.ok() ? ProcessResponseImpl(status) : status;
  if (!exec_status_.ok()) {
    end_of_data_ = true;
  }
  return exec_status_;
}

//-------------------------------------------------------------------------------------------------

PgDocReadOp::PgDocReadOp(const PgSession::ScopedRefPtr& pg_session,
                         size_t num_hash_key_columns,
                         std::unique_ptr<client::YBPgsqlReadOp> read_op)
    : PgDocOp(pg_session),
      num_hash_key_columns_(num_hash_key_columns),
      template_op_(std::move(read_op)) {
}

void PgDocReadOp::Initialize(const PgExecParameters *exec_params) {
  PgDocOp::Initialize(exec_params);

  can_produce_more_ops_ = true;
  template_op_->mutable_request()->set_return_paging_state(true);
  SetRequestPrefetchLimit();
  SetRowMark();
}

Status PgDocReadOp::CreateBatchOps(int partition_count) {
  // Allocate batch operator, one per partition.
  SCHECK(partition_count > 0, InternalError, "Table must have at lease one partition");
  if (batch_ops_.size() < partition_count) {
    batch_ops_.resize(partition_count);
    for (int idx = 0; idx < partition_count; idx++) {
      batch_ops_[idx] = template_op_->DeepCopy();
    }

    batch_row_orders_.resize(partition_count);
    can_produce_more_ops_ = false;
  }

  // Initialize batch operators.
  // - Clear the existing ybctids and row orders.
  for (int partition = 0; partition < batch_ops_.size(); partition++) {
    batch_ops_[partition]->mutable_request()->clear_ybctid_column_value();
    batch_ops_[partition]->mutable_request()->clear_batch_arguments();

    batch_row_orders_[partition].clear();
  }

  return Status::OK();
}

Status PgDocReadOp::SetBatchArgYbctid(const vector<Slice> *ybctids,
                                      const PgTableDesc *table_desc) {
  // Begin the next batch of ybctids.
  end_of_data_ = false;

  // To honor the indexing order of ybctid values, for each batch of ybctid-binds, select all rows
  // in the batch and then order them before returning result to Postgres layer.
  //
  // NOTE: The buffering feature in RPC layer will be used to fulfill this requirement.
  wait_for_batch_completion_ = true;

  // Create batch operators, one per partition.
  RETURN_NOT_OK(CreateBatchOps(table_desc->GetPartitionCount()));

  // Assign ybctid values.
  for (const Slice& ybctid : *ybctids) {
    // Find the key.
    SCHECK(ybctid.size() > 0, InternalError, "Invalid ybctid value");
    uint16 hash_code = VERIFY_RESULT(docdb::DocKey::DecodeHash(ybctid));
    string key = PartitionSchema::EncodeMultiColumnHashValue(hash_code);

    // The partition index is the boundary index minus 1.
    int partition = table_desc->FindPartitionStartIndex(key);
    SCHECK(partition >= 0 || partition < table_desc->GetPartitionCount(), InternalError,
           "Ybctid value is not within partition boundary");

    // TODO(neil)
    // HACK: We must set "ybctid_column_value" for two reasons.
    // - "client::yb_op" uses it to set the hash_code.
    // - Rolling upgrade: Older server will read only "ybctid_column_value" as it doesn't know
    //   of ybctid-batching operation.
    if (!batch_ops_[partition]->mutable_request()->has_ybctid_column_value()) {
      batch_ops_[partition]->mutable_request()->mutable_ybctid_column_value()->mutable_value()
        ->set_binary_value(ybctid.data(), ybctid.size());

      // TODO(neil) We shouldn't need "reap_ops_" list. Just execute batch_ops_ directly.
      // Add the operator to "read_ops_" to be executed.
      read_ops_.push_back(batch_ops_[partition]);
    }

    // Insert ybctid and its order into batch.
    // The "ybctid" values are returned in the same order as the row in the IndexTable. To keep
    // track of this order, each argument is assigned an order-number.
    auto batch_arg = batch_ops_[partition]->mutable_request()->add_batch_arguments();
    batch_arg->set_order(batch_row_ordering_counter_);
    batch_arg->mutable_ybctid()->mutable_value()->set_binary_value(ybctid.data(), ybctid.size());

    // Remember the order number for each request.
    batch_row_orders_[partition].push_back(batch_row_ordering_counter_);

    // Increment counter for the next row.
    batch_row_ordering_counter_++;
  }

  return Status::OK();
}

void PgDocReadOp::SetRequestPrefetchLimit() {
  // Predict the maximum prefetch-limit using the associated gflags.
  PgsqlReadRequestPB *req = template_op_->mutable_request();
  int predicted_limit = FLAGS_ysql_prefetch_limit;
  if (!req->is_forward_scan()) {
    // Backward scan is slower than forward scan, so predicted limit is a smaller number.
    predicted_limit = predicted_limit * FLAGS_ysql_backward_prefetch_scale_factor;
  }

  // System setting has to be at least 1 while user setting (LIMIT clause) can be anything that
  // is allowed by SQL semantics.
  if (predicted_limit < 1) {
    predicted_limit = 1;
  }

  // Use statement LIMIT(count + offset) if it is smaller than the predicted limit.
  int64_t limit_count = exec_params_.limit_count + exec_params_.limit_offset;
  if (exec_params_.limit_use_default || limit_count > predicted_limit) {
    limit_count = predicted_limit;
  }
  req->set_limit(limit_count);
}

void PgDocReadOp::SetRowMark() {
  PgsqlReadRequestPB *const req = template_op_->mutable_request();

  if (exec_params_.rowmark < 0) {
    req->clear_row_mark_type();
  } else {
    req->set_row_mark_type(static_cast<yb::RowMarkType>(exec_params_.rowmark));
  }
}

void PgDocReadOp::InitializeNextOps(int num_ops) {
  if (num_ops <= 0) {
    return;
  }

  if (template_op_->request().partition_column_values_size() == 0) {
    // TODO(dmitry): Use template_op_ directly instead of copy in case of single partition expr.
    read_ops_.push_back(template_op_->DeepCopy());
    can_produce_more_ops_ = false;
  } else {
    if (partition_exprs_.empty()) {
      // Initialize partition_exprs_ on the first call.
      partition_exprs_.resize(num_hash_key_columns_);
      for (int c_idx = 0; c_idx < num_hash_key_columns_; ++c_idx) {
        const auto& col_expr = template_op_->request().partition_column_values(c_idx);
        if (col_expr.has_condition()) {
          for (const auto& expr : col_expr.condition().operands(1).condition().operands()) {
            partition_exprs_[c_idx].push_back(&expr);
          }
        } else {
          partition_exprs_[c_idx].push_back(&col_expr);
        }
      }
    }

    // Total number of unrolled operations.
    int total_op_count = 1;
    for (auto& exprs : partition_exprs_) {
      total_op_count *= exprs.size();
    }

    while (num_ops > 0 && next_op_idx_ < total_op_count) {
      // Construct a new YBPgsqlReadOp.
      auto read_op(template_op_->DeepCopy());
      read_op->mutable_request()->clear_partition_column_values();
      for (int i = 0; i < num_hash_key_columns_; ++i) {
        read_op->mutable_request()->add_partition_column_values();
      }

      // Fill in partition_column_values from currently selected permutation.
      int pos = next_op_idx_;
      for (int c_idx = num_hash_key_columns_ - 1; c_idx >= 0; --c_idx) {
        int sel_idx = pos % partition_exprs_[c_idx].size();
        read_op->mutable_request()->mutable_partition_column_values(c_idx)
               ->CopyFrom(*partition_exprs_[c_idx][sel_idx]);
        pos /= partition_exprs_[c_idx].size();
      }
      read_ops_.push_back(std::move(read_op));
      --num_ops;
      ++next_op_idx_;
    }

    if (next_op_idx_ == total_op_count) {
      can_produce_more_ops_ = false;
    }
  }
  DCHECK(!read_ops_.empty()) << "read_ops_ should not be empty after setting!";
}

Status PgDocReadOp::SendRequestImpl(bool force_non_bufferable) {
  DCHECK(!read_ops_.empty() || can_produce_more_ops_);
  if (can_produce_more_ops_) {
    InitializeNextOps(FLAGS_ysql_request_limit - read_ops_.size());
  }

  response_ =
      VERIFY_RESULT(pg_session_->RunAsync(read_ops_, PgObjectId(), &read_time_,
                                          force_non_bufferable || wait_for_batch_completion_));
  SCHECK(response_.InProgress(), IllegalState, "YSQL read operation should not be buffered");
  return Status::OK();
}

Status PgDocReadOp::ProcessResponseImpl(const Status& exec_status) {
  for (const auto& read_op : read_ops_) {
    RETURN_NOT_OK(pg_session_->HandleResponse(*read_op, PgObjectId()));
  }

  if (batch_row_orders_.size() == 0) {
    for (auto& read_op : read_ops_) {
      DCHECK(!read_op->rows_data().empty()) << "Read operation should not return empty data";
      result_cache_.push_back(make_shared<PgDocResult>(read_op->rows_data()));
    }
  } else {
    for (int partition = 0; partition < batch_ops_.size(); partition++) {
      if (batch_ops_[partition]->mutable_request()->has_ybctid_column_value()) {
        // Read the response as this request has been initialized and send to tablet server.
        result_cache_.push_back(make_shared<PgDocResult>(batch_ops_[partition]->rows_data(),
                                                         std::move(batch_row_orders_[partition])));
      }
    }
  }

  // For each read_op, set up its request for the next batch of data, or remove it from the list
  // if no data is left.
  read_ops_.erase(std::remove_if(read_ops_.begin(), read_ops_.end(), [](auto& read_op) {
    auto& res = *read_op->mutable_response();
    if (res.has_paging_state()) {
      PgsqlReadRequestPB *req = read_op->mutable_request();
      // Set up paging state for next request.
      // A query request can be nested, and paging state belong to the innermost query which is
      // the read operator that is operated first and feeds data to other queries.
      // Recursive Proto Message:
      //     PgsqlReadRequestPB { PgsqlReadRequestPB index_request; }
      PgsqlReadRequestPB *innermost_req = req;
      while (innermost_req->has_index_request()) {
        innermost_req = innermost_req->mutable_index_request();
      }
      *innermost_req->mutable_paging_state() = std::move(*res.mutable_paging_state());
      // Parse/Analysis/Rewrite catalog version has already been checked on the first request.
      // The docdb layer will check the target table's schema version is compatible.
      // This allows long-running queries to continue in the presence of other DDL statements
      // as long as they do not affect the table(s) being queried.
      req->clear_ysql_catalog_version();

      // Keep this read-op and resend for the next paging state.
      return false;

    } else if (read_op->request().batch_arguments_size() > 0 &&
               res.batch_arg_count() < read_op->request().batch_arguments_size()) {
      // === THIS CODE IS FOR ROLLING UPGRADE ONLY ===
      // Currently we batch only for ybctid argument, so the following will always be true.
      // - Each ybctid (i.e. each batch argument) yields exactly one rows.
      // - Current and later server will return ALL rows for all batch argument at once.
      // - OLDER server will return only one row for the very first argument.
      //   The following code is for connection with OLDER server.

      // During rolling-upgrade, old servers would process EXACTLY for ybctid-batching.
      DCHECK_EQ(res.batch_arg_count(), 1) << "Unexpected batch argument count";
      PgsqlReadRequestPB *req = read_op->mutable_request();

      // Update ybctid field for the next SendRequest.
      // Delete the first one from the batch and assign the second value to OLD ybctid column.
      req->mutable_batch_arguments()->DeleteSubrange(0, 1);
      *req->mutable_ybctid_column_value() = req->batch_arguments()[0].ybctid();

      // Keep the read-op and resend for the next batch.
      return false;
    }

    // Erase the read_op.
    return true;
  }), read_ops_.end());

  end_of_data_ = read_ops_.empty() && !can_produce_more_ops_;
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

PgDocWriteOp::PgDocWriteOp(const PgSession::ScopedRefPtr& pg_session,
                           const PgObjectId& relation_id,
                           std::unique_ptr<client::YBPgsqlWriteOp> write_op)
    : PgDocOp(pg_session), write_op_(std::move(write_op)), relation_id_(relation_id) {
}

Status PgDocWriteOp::SendRequestImpl(bool force_non_bufferable) {
  response_ = VERIFY_RESULT(
      pg_session_->RunAsync(write_op_, relation_id_, &read_time_, force_non_bufferable));
  // Log non buffered request.
  VLOG_IF(1, response_.InProgress()) << __PRETTY_FUNCTION__ << ": Sending request for " << this;
  return Status::OK();
}

Status PgDocWriteOp::ProcessResponseImpl(const Status& exec_status) {
  RETURN_NOT_OK(pg_session_->HandleResponse(*write_op_, relation_id_));
  if (PREDICT_FALSE(!write_op_->rows_data().empty())) {
    result_cache_.push_back(make_shared<PgDocResult>(write_op_->rows_data()));
  }
  rows_affected_count_ = write_op_.get()->response().rows_affected_count();
  end_of_data_ = true;
  VLOG(1) << __PRETTY_FUNCTION__ << ": Received response for request " << this;
  return Status::OK();
}

}  // namespace pggate
}  // namespace yb
