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

PgDocOp::PgDocOp(PgSession::ScopedRefPtr pg_session)
    : pg_session_(pg_session), exec_semaphore_(1) {
}

PgDocOp::~PgDocOp() {
  // Hold on to this object just in case there are requests in the queue while PostgreSQL client
  // cancels the operation.
  is_canceled_ = true;

  // If more data from server is expected, the object cannot be destructed until callback happens.
  WaitForData();
}

bool PgDocOp::EndOfResult() const {
  return !has_cached_data_ && end_of_data_;
}

Status PgDocOp::Execute() {
  // As of 09/25/2018, DocDB doesn't cache or keep any execution state for a statement, so we
  // have to call query execution every time.
  // - Normal SQL convention: Exec, Fetch, Fetch, ...
  // - Our SQL convention: Exec & Fetch, Exec & Fetch, ...
  if (!is_canceled_) {
    // Init the operation.
    Init();
    // Send the request to server.
    return SendRequest();
  } else {
    return Status::OK();
  }
}

void PgDocOp::Init() {
  // Block further execution.
  exec_semaphore_.Acquire();
  waiting_for_response_ = true;
  end_of_data_ = false;
}

Status PgDocOp::GetResult(string *result_set) {
  if (!is_canceled_) {
    // Wait for response from DocDB.
    WaitForData();

    // If the execution has error, return without reading any rows.
    if (!exec_status_.ok()) {
      return exec_status_;
    }

    // Read from cache.
    ReadFromCache(result_set);

    // Request more data if more execution is needed and cache is empty.
    if (!has_cached_data_ && !end_of_data_ && !waiting_for_response_) {
      exec_semaphore_.Acquire();
      RETURN_NOT_OK(SendRequest());
    }
  }
  return Status::OK();
}

void PgDocOp::WaitForData() {
  while (!has_cached_data_ && !end_of_data_) {
    exec_semaphore_.Acquire();
    exec_semaphore_.Release();
  }
}

void PgDocOp::WriteToCache(std::shared_ptr<client::YBPgsqlOp> yb_op) {
  cache_lock_.lock();
  if (!yb_op->rows_data().empty()) {
    result_cache_.push_back(yb_op->rows_data());
    has_cached_data_ = !result_cache_.empty();
  }
  cache_lock_.unlock();
}

void PgDocOp::ReadFromCache(string *result) {
  cache_lock_.lock();
  if (!result_cache_.empty()) {
    *result = result_cache_.front();
    result_cache_.pop_front();
    has_cached_data_ = !result_cache_.empty();
  }
  cache_lock_.unlock();
}

//--------------------------------------------------------------------------------------------------

PgDocReadOp::PgDocReadOp(PgSession::ScopedRefPtr pg_session, client::YBPgsqlReadOp *read_op)
    : PgDocOp(pg_session), read_op_(read_op) {
}

PgDocReadOp::~PgDocReadOp() {
}

void PgDocReadOp::Init() {
  PgDocOp::Init();

  PgsqlReadRequestPB *req = read_op_->mutable_request();
  req->set_limit(kPrefetchLimit);
  req->set_return_paging_state(true);
}

Status PgDocReadOp::SendRequest() {
  RETURN_NOT_OK(pg_session_->ApplyAsync(read_op_));
  pg_session_->FlushAsync([this](const Status& s) { PgDocReadOp::ReceiveResponse(s); });
  return Status::OK();
}

void PgDocReadOp::ReceiveResponse(Status exec_status) {
  exec_status_ = exec_status;
  int64_t row_count = 0;
  if (!is_canceled_ && exec_status.ok()) {
    // Save it to cache.
    WriteToCache(read_op_);

    // Setup request for the next batch of data.
    const PgsqlResponsePB& res = read_op_->response();
     if (res.has_paging_state()) {
      PgsqlReadRequestPB *req = read_op_->mutable_request();
      uint64 new_total_rows = res.paging_state().total_num_rows_read();
      uint64 last_total_rows = 0;
      if (req->mutable_paging_state()->has_total_num_rows_read()) {
        last_total_rows = req->mutable_paging_state()->total_num_rows_read();
      }

      row_count = new_total_rows - last_total_rows;
      *req->mutable_paging_state() = res.paging_state();
    } else {
      end_of_data_ = true;
    }
  } else {
    end_of_data_ = true;
  }

  if (!end_of_data_ && (!has_cached_data_ || row_count <= 0)) {
    // Send another request since the reponse for the last SendRequest doesn't get any data back.
    exec_status_ = SendRequest();
  } else {
    // Release the semaphore for the next send request.
    waiting_for_response_ = false;
    exec_semaphore_.Release();
  }
}

//--------------------------------------------------------------------------------------------------

PgDocWriteOp::PgDocWriteOp(PgSession::ScopedRefPtr pg_session, client::YBPgsqlWriteOp *write_op)
    : PgDocOp(pg_session), write_op_(write_op) {
}

PgDocWriteOp::~PgDocWriteOp() {
}

Status PgDocWriteOp::SendRequest() {
  RETURN_NOT_OK(pg_session_->ApplyAsync(write_op_));
  pg_session_->FlushAsync([this](const Status& s) { PgDocWriteOp::ReceiveResponse(s); });
  return Status::OK();
}

void PgDocWriteOp::ReceiveResponse(Status exec_status) {
  exec_status_ = exec_status;
  if (!is_canceled_ && exec_status.ok()) {
    // Save it to cache.
    WriteToCache(write_op_);
  }

  // Release the semaphore for the next execution.
  end_of_data_ = true;
  waiting_for_response_ = false;
  exec_semaphore_.Release();
}

//--------------------------------------------------------------------------------------------------

PgDocCompoundOp::PgDocCompoundOp(PgSession::ScopedRefPtr pg_session) : PgDocOp(pg_session) {
}

PgDocCompoundOp::~PgDocCompoundOp() {
}

}  // namespace pggate
}  // namespace yb
