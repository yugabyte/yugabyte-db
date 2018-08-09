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

#include "yb/client/session-internal.h"

#include <memory>
#include <mutex>

#include "yb/client/batcher.h"
#include "yb/client/callbacks.h"
#include "yb/client/error_collector.h"
#include "yb/client/yb_op.h"

#include "yb/rpc/messenger.h"

DEFINE_int32(client_read_write_timeout_ms, 60000, "Timeout for client read and write operations.");

namespace yb {

namespace client {

using internal::AsyncRpcMetrics;
using internal::Batcher;
using internal::ErrorCollector;

using std::shared_ptr;

YBSessionData::YBSessionData(shared_ptr<YBClient> client, const scoped_refptr<ClockBase>& clock)
    : client_(std::move(client)),
      read_point_(clock ? std::make_unique<ConsistentReadPoint>(clock) : nullptr),
      error_collector_(new ErrorCollector()),
      timeout_(MonoDelta::FromMilliseconds(FLAGS_client_read_write_timeout_ms)) {
  const auto metric_entity = client_->metric_entity();
  async_rpc_metrics_ = metric_entity ? std::make_shared<AsyncRpcMetrics>(metric_entity) : nullptr;
}

YBSessionData::~YBSessionData() {
}

void YBSessionData::SetTransaction(YBTransactionPtr transaction) {
  transaction_ = std::move(transaction);
  internal::BatcherPtr old_batcher;
  old_batcher.swap(batcher_);
  if (old_batcher) {
    LOG_IF(DFATAL, old_batcher->HasPendingOperations()) << "SetTransaction with non empty batcher";
    old_batcher->Abort(STATUS(Aborted, "Transaction changed"));
  }
}

void YBSessionData::FlushFinished(internal::BatcherPtr batcher) {
  std::lock_guard<simple_spinlock> l(lock_);
  CHECK_EQ(flushed_batchers_.erase(batcher), 1);
}

void YBSessionData::Abort() {
  if (batcher_ && batcher_->HasPendingOperations()) {
    batcher_->Abort(STATUS(Aborted, "Batch aborted"));
    batcher_.reset();
  }
}

void YBSessionData::SetReadPoint(const Restart restart) {
  DCHECK_NOTNULL(read_point_.get());
  if (restart && read_point_->IsRestartRequired()) {
    read_point_->Restart();
  } else {
    read_point_->SetCurrentReadTime();
  }
}

Status YBSessionData::Close(bool force) {
  if (batcher_) {
    if (batcher_->HasPendingOperations() && !force) {
      return STATUS(IllegalState, "Could not close. There are pending operations.");
    }
    batcher_->Abort(STATUS(Aborted, "Batch aborted"));
    batcher_.reset();
  }
  return Status::OK();
}

void YBSessionData::FlushAsync(StatusFunctor callback) {
  // Swap in a new batcher to start building the next batch.
  // Save off the old batcher.
  //
  // Send off any buffered data. Important to do this outside of the lock
  // since the callback may itself try to take the lock, in the case that
  // the batch fails "inline" on the same thread.

  internal::BatcherPtr old_batcher;
  old_batcher.swap(batcher_);
  if (old_batcher) {
    {
      std::lock_guard<simple_spinlock> l(lock_);
      flushed_batchers_.insert(old_batcher);
    }
    old_batcher->set_allow_local_calls_in_curr_thread(allow_local_calls_in_curr_thread_);
    old_batcher->FlushAsync(std::move(callback));
  } else {
    callback(Status::OK());
  }
}

bool YBSessionData::allow_local_calls_in_curr_thread() const {
  return allow_local_calls_in_curr_thread_;
}

void YBSessionData::set_allow_local_calls_in_curr_thread(bool flag) {
  allow_local_calls_in_curr_thread_ = flag;
}

internal::Batcher& YBSessionData::Batcher() {
  if (!batcher_) {
    batcher_.reset(new internal::Batcher(
        client_.get(), error_collector_.get(), shared_from_this(), transaction_,
        transaction_ ? &transaction_->read_point() : read_point_.get()));
    if (timeout_.Initialized()) {
      batcher_->SetTimeout(timeout_);
    }
  }
  return *batcher_;
}

Status YBSessionData::Apply(YBOperationPtr yb_op) {
  Status s = Batcher().Add(yb_op);
  if (!PREDICT_FALSE(s.ok())) {
    error_collector_->AddError(yb_op, s);
    return s;
  }

  return Status::OK();
}

Status YBSessionData::ApplyAndFlush(YBOperationPtr yb_op) {
  RETURN_NOT_OK(Apply(std::move(yb_op)));

  return Flush();
}

Status YBSessionData::Apply(const std::vector<YBOperationPtr>& ops) {
  auto& batcher = Batcher();
  for (const auto& op : ops) {
    Status s = batcher.Add(op);
    if (!PREDICT_FALSE(s.ok())) {
      error_collector_->AddError(op, s);
      return s;
    }
  }
  return Status::OK();
}

Status YBSessionData::ApplyAndFlush(
    const std::vector<YBOperationPtr>& ops, VerifyResponse verify_response) {
  RETURN_NOT_OK(Apply(ops));
  RETURN_NOT_OK(Flush());

  if (verify_response) {
    for (const auto& op : ops) {
      if (!op->succeeded()) {
        return STATUS_FORMAT(RuntimeError, "Operation failed: ", op);
      }
    }
  }

  return Status::OK();
}

Status YBSessionData::Flush() {
  Synchronizer s;
  FlushAsync(s.AsStatusFunctor());
  return s.Wait();
}

void YBSessionData::SetTimeout(MonoDelta timeout) {
  CHECK_GE(timeout, MonoDelta::kZero);
  timeout_ = timeout;
  if (batcher_) {
    batcher_->SetTimeout(timeout);
  }
}

int YBSessionData::CountBufferedOperations() const {
  return batcher_ ? batcher_->CountBufferedOperations() : 0;
}

bool YBSessionData::HasPendingOperations() const {
  if (batcher_ && batcher_->HasPendingOperations()) {
    return true;
  }
  std::lock_guard<simple_spinlock> l(lock_);
  for (const auto& b : flushed_batchers_) {
    if (b->HasPendingOperations()) {
      return true;
    }
  }
  return false;
}

int YBSessionData::CountPendingErrors() const {
  return error_collector_->CountErrors();
}

CollectedErrors YBSessionData::GetPendingErrors() {
  return error_collector_->GetErrors();
}

}  // namespace client
}  // namespace yb
