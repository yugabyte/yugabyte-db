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

void PgDocOp::SetExecParams(const PgExecParameters& exec_params) {
  exec_params_ = exec_params;
}

Result<RequestSent> PgDocOp::Execute(bool force_non_bufferable) {
  // As of 09/25/2018, DocDB doesn't cache or keep any execution state for a statement, so we
  // have to call query execution every time.
  // - Normal SQL convention: Exec, Fetch, Fetch, ...
  // - Our SQL convention: Exec & Fetch, Exec & Fetch, ...
  // This refers to the sequence of operations between this layer and the underlying tablet
  // server / DocDB layer, not to the sequence of operations between the PostgreSQL layer and this
  // layer.
  Init();
  RETURN_NOT_OK(SendRequest(force_non_bufferable));
  return RequestSent(response_.InProgress());
}

void PgDocOp::Init() {
  result_cache_.clear();
  end_of_data_ = false;
}

Result<string> PgDocOp::GetResult() {
  // If the execution has error, return without reading any rows.
  RETURN_NOT_OK(exec_status_);

  if (result_cache_.empty() && !end_of_data_) {
    DCHECK(response_.InProgress());
    RETURN_NOT_OK(ProcessResponse(response_.GetStatus()));
    // In case ProcessResponse doesn't fail with an error
    // it should fill result_cache_ and/or set end_of_data_.
    DCHECK(!result_cache_.empty() || end_of_data_);
  }

  string result;
  if (!result_cache_.empty()) {
    result = std::move(result_cache_.front());
    result_cache_.pop_front();
    if (result_cache_.empty() && !end_of_data_) {
      RETURN_NOT_OK(SendRequest(true /* force_non_bufferable */));
    }
  }
  return result;
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

// End of PgDocOp base class.
//-------------------------------------------------------------------------------------------------

PgDocReadOp::PgDocReadOp(const PgSession::ScopedRefPtr& pg_session,
                         size_t num_hash_key_columns,
                         std::unique_ptr<client::YBPgsqlReadOp> read_op)
    : PgDocOp(pg_session),
      num_hash_key_columns_(num_hash_key_columns),
      template_op_(std::move(read_op)) {
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
  SetRequestPrefetchLimit();
  SetRowMark();

  DCHECK(!read_ops_.empty() || can_produce_more_ops_);
  if (can_produce_more_ops_) {
    InitializeNextOps(FLAGS_ysql_request_limit - read_ops_.size());
  }

  response_ = VERIFY_RESULT(
      pg_session_->RunAsync(read_ops_, PgObjectId(), &read_time_, force_non_bufferable));
  SCHECK(response_.InProgress(), IllegalState, "YSQL read operation should not be buffered");
  return Status::OK();
}

Status PgDocReadOp::ProcessResponseImpl(const Status& exec_status) {
  for (const auto& read_op : read_ops_) {
    RETURN_NOT_OK(pg_session_->HandleResponse(*read_op, PgObjectId()));
  }

  for (auto& read_op : read_ops_) {
    SCHECK(!read_op->rows_data().empty(),
           IllegalState,
           "Read operation should not return empty data");
    result_cache_.push_back(read_op->rows_data());
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
      return false;
    }
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
    result_cache_.push_back(write_op_->rows_data());
  }
  rows_affected_count_ = write_op_.get()->response().rows_affected_count();
  end_of_data_ = true;
  VLOG(1) << __PRETTY_FUNCTION__ << ": Received response for request " << this;
  return Status::OK();
}

//--------------------------------------------------------------------------------------------------

PgDocCompoundOp::PgDocCompoundOp(const PgSession::ScopedRefPtr& pg_session)
    : PgDocOp(pg_session) {
}

}  // namespace pggate
}  // namespace yb
