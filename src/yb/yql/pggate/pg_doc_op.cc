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

#include "yb/yql/pggate/pggate_flags.h"

// TODO: include a header for PgTxnManager specifically.
#include "yb/yql/pggate/pggate_if_cxx_decl.h"

#include "yb/common/pgsql_error.h"
#include "yb/common/transaction_error.h"
#include "yb/util/yb_pg_errcodes.h"
#include "yb/yql/pggate/ybc_pggate.h"

namespace yb {
namespace pggate {

PgDocOp::PgDocOp(PgSession::ScopedRefPtr pg_session)
    : pg_session_(std::move(pg_session)) {
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

Result<bool> PgDocOp::EndOfResult() const {
  RETURN_NOT_OK(exec_status_);
  return end_of_data_ && result_cache_.empty();
}

void PgDocOp::SetExecParams(const PgExecParameters *exec_params) {
  if (exec_params) {
    exec_params_ = *exec_params;
  }
}

Result<RequestSent> PgDocOp::Execute() {
  // As of 09/25/2018, DocDB doesn't cache or keep any execution state for a statement, so we
  // have to call query execution every time.
  // - Normal SQL convention: Exec, Fetch, Fetch, ...
  // - Our SQL convention: Exec & Fetch, Exec & Fetch, ...
  // This refers to the sequence of operations between this layer and the underlying tablet
  // server / DocDB layer, not to the sequence of operations between the PostgreSQL layer and this
  // layer.
  Init();

  RETURN_NOT_OK(SendRequest());

  return RequestSent(response_.InProgress());
}

void PgDocOp::Init() {
  result_cache_.clear();
  end_of_data_ = false;
}

Status PgDocOp::GetResult(string *result_set) {
  // Check request was sent.
  DCHECK(!result_cache_.empty() || end_of_data_ || response_.InProgress());

  // If the execution has error, return without reading any rows.
  RETURN_NOT_OK(exec_status_);

  // Wait for response from DocDB.
  if (result_cache_.empty() && !end_of_data_ && response_.InProgress()) {
    ProcessResponse(response_.GetStatus());
  }

  RETURN_NOT_OK(exec_status_);

  // Read from cache.
  ReadFromCache(result_set);
  // This will pre-fetch the next chunk of data if we've consumed all cached
  // rows.
  if (result_cache_.empty() && !end_of_data_) {
    return SendRequest();
  }

  return Status::OK();
}

void PgDocOp::WriteToCache(client::YBPgsqlOp* yb_op) {
  if (!yb_op->rows_data().empty()) {
    result_cache_.push_back(yb_op->rows_data());
  }
}

void PgDocOp::ReadFromCache(string *result) {
  if (!result_cache_.empty()) {
    *result = std::move(result_cache_.front());
    result_cache_.pop_front();
  }
}

void PgDocOp::HandleResponseStatus(client::YBPgsqlOp* op) {
  if (op->succeeded()) {
    return;
  }

  const auto& response = op->response();

  YBPgErrorCode pg_error_code = YBPgErrorCode::YB_PG_INTERNAL_ERROR;
  if (response.has_pg_error_code()) {
    pg_error_code = static_cast<YBPgErrorCode>(response.pg_error_code());
  }

  TransactionErrorCode txn_error_code = TransactionErrorCode::kNone;
  if (response.has_txn_error_code()) {
    txn_error_code = static_cast<TransactionErrorCode>(response.txn_error_code());
  }

  if (response.status() == PgsqlResponsePB::PGSQL_STATUS_DUPLICATE_KEY_ERROR) {
    // We're doing this to eventually replace the error message by one mentioning the index name.
    exec_status_ = STATUS(AlreadyPresent, op->response().error_message(), Slice(),
        PgsqlError(pg_error_code));
  } else {
    exec_status_ = STATUS(QLError, op->response().error_message(), Slice(),
        PgsqlError(pg_error_code));
  }

  exec_status_ = exec_status_.CloneAndAddErrorCode(TransactionError(txn_error_code));
}

// End of PgDocOp base class.
//-------------------------------------------------------------------------------------------------

PgDocReadOp::PgDocReadOp(
    PgSession::ScopedRefPtr pg_session,
    PgTableDesc::ScopedRefPtr table_desc)
    : PgDocOp(std::move(pg_session)),
      table_desc_(table_desc),
      template_op_(table_desc->NewPgsqlSelect()) {
}

void PgDocReadOp::Init() {
  PgDocOp::Init();

  template_op_->mutable_request()->set_return_paging_state(true);
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
    read_ops_.push_back(template_op_->DeepCopy());
    can_produce_more_ops_ = false;
  } else {
    int num_hash_cols = table_desc_->num_hash_key_columns();

    if (partition_exprs_.empty()) {
      // Initialize partition_exprs_ on the first call.
      partition_exprs_.resize(num_hash_cols);
      for (int c_idx = 0; c_idx < num_hash_cols; ++c_idx) {
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
      for (int i = 0; i < num_hash_cols; ++i) {
        read_op->mutable_request()->add_partition_column_values();
      }

      // Fill in partition_column_values from currently selected permutation.
      int pos = next_op_idx_;
      for (int c_idx = num_hash_cols - 1; c_idx >= 0; --c_idx) {
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

    DCHECK(!read_ops_.empty()) << "read_ops_ should not be empty after setting!";
  }
}

Status PgDocReadOp::SendRequest() {
  CHECK(!response_.InProgress());

  SetRequestPrefetchLimit();
  SetRowMark();

  CHECK(!read_ops_.empty() || can_produce_more_ops_);
  if (can_produce_more_ops_) {
    InitializeNextOps(FLAGS_ysql_request_limit - read_ops_.size());
  }

  response_ = VERIFY_RESULT(pg_session_->RunAsync(read_ops_, &read_time_));
  SCHECK(response_.InProgress(), IllegalState, "YSQL read operation should not be buffered");
  return Status::OK();
}

void PgDocReadOp::ProcessResponse(const Status& exec_status) {
  exec_status_ = exec_status;

  if (exec_status.ok()) {
    for (auto& read_op : read_ops_) {
      HandleResponseStatus(read_op.get());
    }
  }

  // exec_status_ could be changed by HandleResponseStatus.
  if (!exec_status_.ok()) {
    end_of_data_ = true;
    return;
  }

  // Save it to cache.
  for (auto& read_op : read_ops_) {
    WriteToCache(read_op.get());
  }

  // For each read_op, set up its request for the next batch of data, or remove it from the list
  // if no data is left.
  for (auto iter = read_ops_.begin(); iter != read_ops_.end(); /* NOOP */) {
    auto& read_op = *iter;
    const PgsqlResponsePB& res = read_op->response();
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
      *innermost_req->mutable_paging_state() = res.paging_state();
      // Parse/Analysis/Rewrite catalog version has already been checked on the first request.
      // The docdb layer will check the target table's schema version is compatible.
      // This allows long-running queries to continue in the presence of other DDL statements
      // as long as they do not affect the table(s) being queried.
      req->clear_ysql_catalog_version();
      ++iter;
    } else {
      iter = read_ops_.erase(iter);
    }
  }
  end_of_data_ = read_ops_.empty() && !can_produce_more_ops_;
}

//--------------------------------------------------------------------------------------------------

PgDocWriteOp::PgDocWriteOp(PgSession::ScopedRefPtr pg_session, client::YBPgsqlWriteOp *write_op)
    : PgDocOp(pg_session), write_op_(write_op) {
}

Status PgDocWriteOp::SendRequest() {
  CHECK(!response_.InProgress());

  // If the op is buffered, we should not flush now. Just return.
  response_ = VERIFY_RESULT(pg_session_->RunAsync(write_op_, &read_time_));
  // Log non buffered request.
  VLOG_IF(1, response_.InProgress()) << __PRETTY_FUNCTION__ << ": Sending request for " << this;
  return Status::OK();
}

void PgDocWriteOp::ProcessResponse(const Status& exec_status) {
  exec_status_ = exec_status;

  if (exec_status.ok()) {
    HandleResponseStatus(write_op_.get());
  }

  if (exec_status_.ok()) {
    // Save it to cache.
    WriteToCache(write_op_.get());
    // Save the number of rows affected by the write operation.
    rows_affected_count_ = write_op_.get()->response().rows_affected_count();
  }
  end_of_data_ = true;
  VLOG(1) << __PRETTY_FUNCTION__ << ": Received response for request " << this;
}

//--------------------------------------------------------------------------------------------------

PgDocCompoundOp::PgDocCompoundOp(PgSession::ScopedRefPtr pg_session)
    : PgDocOp(std::move(pg_session)) {
}

}  // namespace pggate
}  // namespace yb
