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

using std::shared_ptr;

namespace yb {
namespace pggate {

PgDocOp::PgDocOp(PgSession::ScopedRefPtr pg_session, uint64_t* read_time)
    : pg_session_(std::move(pg_session)), read_time_(read_time),
      can_restart_(!pg_session_->HasAppliedOperations()) {
}

PgDocOp::~PgDocOp() {
  std::unique_lock<std::mutex> lock(mtx_);
  // Hold on to this object just in case there are requests in the queue while PostgreSQL client
  // cancels the operation.
  is_canceled_ = true;
  cv_.notify_all();

  while (waiting_for_response_) {
    cv_.wait(lock);
  }
}

Result<bool> PgDocOp::EndOfResult() const {
  std::lock_guard<std::mutex> lock(mtx_);
  RETURN_NOT_OK(exec_status_);
  return !has_cached_data_ && end_of_data_;
}

Result<RequestSent> PgDocOp::Execute() {
  if (is_canceled_) {
    return STATUS(IllegalState, "Operation canceled");
  }

  std::unique_lock<std::mutex> lock(mtx_);

  // As of 09/25/2018, DocDB doesn't cache or keep any execution state for a statement, so we
  // have to call query execution every time.
  // - Normal SQL convention: Exec, Fetch, Fetch, ...
  // - Our SQL convention: Exec & Fetch, Exec & Fetch, ...
  // This refers to the sequence of operations between this layer and the underlying tablet
  // server / DocDB layer, not to the sequence of operations between the PostgreSQL layer and this
  // layer.
  InitUnlocked(&lock);

  RETURN_NOT_OK(SendRequestUnlocked());

  return RequestSent(waiting_for_response_);
}

void PgDocOp::InitUnlocked(std::unique_lock<std::mutex>* lock) {
  CHECK(!is_canceled_);
  if (waiting_for_response_) {
    LOG(DFATAL) << __PRETTY_FUNCTION__
                << " is not supposed to be called while response is in flight";
    while (waiting_for_response_) {
      cv_.wait(*lock);
    }
    CHECK(!waiting_for_response_);
  }
  result_cache_.clear();
  end_of_data_ = false;
  has_cached_data_ = false;
}

Status PgDocOp::GetResult(string *result_set) {
  std::unique_lock<std::mutex> lock(mtx_);
  if (is_canceled_) {
    return STATUS(IllegalState, "Operation canceled");
  }

  // If the execution has error, return without reading any rows.
  RETURN_NOT_OK(exec_status_);

  RETURN_NOT_OK(SendRequestIfNeededUnlocked());

  // Wait for response from DocDB.
  while (!has_cached_data_ && !end_of_data_) {
    cv_.wait(lock);
  }

  RETURN_NOT_OK(exec_status_);

  // Read from cache.
  ReadFromCacheUnlocked(result_set);

  // This will pre-fetch the next chunk of data if we've consumed all cached
  // rows.
  RETURN_NOT_OK(SendRequestIfNeededUnlocked());

  return Status::OK();
}

void PgDocOp::WriteToCacheUnlocked(std::shared_ptr<client::YBPgsqlOp> yb_op) {
  if (!yb_op->rows_data().empty()) {
    result_cache_.push_back(yb_op->rows_data());
    has_cached_data_ = !result_cache_.empty();
  }
}

void PgDocOp::ReadFromCacheUnlocked(string *result) {
  if (!result_cache_.empty()) {
    *result = result_cache_.front();
    result_cache_.pop_front();
    has_cached_data_ = !result_cache_.empty();
  }
}

Status PgDocOp::SendRequestIfNeededUnlocked() {
  // Request more data if more execution is needed and cache is empty.
  if (!has_cached_data_ && !end_of_data_ && !waiting_for_response_) {
    return SendRequestUnlocked();
  }
  return Status::OK();
}

bool PgDocOp::CheckRestartUnlocked(client::YBPgsqlOp* op) {
  if (op->succeeded()) {
    return false;
  }

  if (op->response().status() == PgsqlResponsePB::PGSQL_STATUS_RESTART_REQUIRED_ERROR &&
      can_restart_) {
    exec_status_ = pg_session_->RestartTransaction();
    if (exec_status_.ok()) {
      exec_status_ = SendRequestUnlocked();
      if (exec_status_.ok()) {
        return true;
      }
    }
  } else {
    exec_status_ = STATUS(QLError, op->response().error_message());
  }

  return false;
}

//--------------------------------------------------------------------------------------------------

PgDocReadOp::PgDocReadOp(
    PgSession::ScopedRefPtr pg_session, uint64_t* read_time, client::YBPgsqlReadOp *read_op)
    : PgDocOp(pg_session, read_time), read_op_(read_op) {
}

PgDocReadOp::~PgDocReadOp() {
}

void PgDocReadOp::InitUnlocked(std::unique_lock<std::mutex>* lock) {
  PgDocOp::InitUnlocked(lock);

  PgsqlReadRequestPB *req = read_op_->mutable_request();
  req->set_limit(kPrefetchLimit);
  req->set_return_paging_state(true);
}

Status PgDocReadOp::SendRequestUnlocked() {
  CHECK(!waiting_for_response_);

  SCHECK_EQ(VERIFY_RESULT(pg_session_->PgApplyAsync(read_op_, read_time_)), OpBuffered::kFalse,
            IllegalState, "YSQL read operation should not be buffered");

  waiting_for_response_ = true;
  Status s = pg_session_->PgFlushAsync([this](const Status& s) {
                                         PgDocReadOp::ReceiveResponse(s);
                                       });
  if (!s.ok()) {
    waiting_for_response_ = false;
    return s;
  }
  return Status::OK();
}

void PgDocReadOp::ReceiveResponse(Status exec_status) {
  std::unique_lock<std::mutex> lock(mtx_);
  CHECK(waiting_for_response_);
  cv_.notify_all();
  waiting_for_response_ = false;
  exec_status_ = exec_status;

  if (exec_status.ok() && CheckRestartUnlocked(read_op_.get())) {
    return;
  }

  // exec_status_ could be changed by CheckRestartUnlocked
  if (!exec_status_.ok()) {
    end_of_data_ = true;
    return;
  }

  if (!is_canceled_) {
    // Save it to cache.
    WriteToCacheUnlocked(read_op_);

    // Setup request for the next batch of data.
    const PgsqlResponsePB& res = read_op_->response();
     if (res.has_paging_state()) {
       PgsqlReadRequestPB *req = read_op_->mutable_request();
       // Set up paging state for next request.
       *req->mutable_paging_state() = res.paging_state();
       // Parse/Analysis/Rewrite catalog version has already been checked on the first request.
       // The docdb layer will check the target table's schema version is compatible.
       // This allows long-running queries to continue in the presence of other DDL statements
       // as long as they do not affect the table(s) being queried.
       req->clear_ysql_catalog_version();
     } else {
      end_of_data_ = true;
    }
  } else {
    end_of_data_ = true;
  }
}

//--------------------------------------------------------------------------------------------------

PgDocWriteOp::PgDocWriteOp(PgSession::ScopedRefPtr pg_session, client::YBPgsqlWriteOp *write_op)
    : PgDocOp(pg_session, nullptr /* read_time */), write_op_(write_op) {
}

PgDocWriteOp::~PgDocWriteOp() {
}

Status PgDocWriteOp::SendRequestUnlocked() {
  CHECK(!waiting_for_response_);

  // If the op is buffered, we should not flush now. Just return.
  if (VERIFY_RESULT(pg_session_->PgApplyAsync(write_op_, read_time_)) == OpBuffered::kTrue) {
    return Status::OK();
  }

  waiting_for_response_ = true;
  Status s = pg_session_->PgFlushAsync([this](const Status& s) {
                                         PgDocWriteOp::ReceiveResponse(s);
                                       });
  if (!s.ok()) {
    waiting_for_response_ = false;
    return s;
  }
  VLOG(1) << __PRETTY_FUNCTION__ << ": Sending request for " << this;
  return Status::OK();
}

void PgDocWriteOp::ReceiveResponse(Status exec_status) {
  std::unique_lock<std::mutex> lock(mtx_);
  CHECK(waiting_for_response_);
  waiting_for_response_ = false;
  cv_.notify_all();
  exec_status_ = exec_status;

  if (exec_status.ok() && CheckRestartUnlocked(write_op_.get())) {
    return;
  }
  if (!is_canceled_ && exec_status_.ok()) {
    // Save it to cache.
    WriteToCacheUnlocked(write_op_);
  }
  end_of_data_ = true;
  VLOG(1) << __PRETTY_FUNCTION__ << ": Received response for request " << this;
}

//--------------------------------------------------------------------------------------------------

PgDocCompoundOp::PgDocCompoundOp(PgSession::ScopedRefPtr pg_session)
    : PgDocOp(pg_session, nullptr /* read_time */) {
}

PgDocCompoundOp::~PgDocCompoundOp() {
}

}  // namespace pggate
}  // namespace yb
