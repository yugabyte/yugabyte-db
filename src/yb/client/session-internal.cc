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

MAKE_ENUM_LIMITS(yb::client::YBSession::FlushMode,
                 yb::client::YBSession::AUTO_FLUSH_SYNC,
                 yb::client::YBSession::MANUAL_FLUSH);

namespace yb {

namespace client {

using internal::AsyncRpcMetrics;
using internal::Batcher;
using internal::ErrorCollector;

using std::shared_ptr;

YBSessionData::YBSessionData(shared_ptr<YBClient> client,
                             const YBTransactionPtr& transaction)
    : client_(std::move(client)),
      transaction_(transaction),
      error_collector_(new ErrorCollector()) {
  const auto metric_entity = client_->messenger()->metric_entity();
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

void YBSessionData::FlushAsync(boost::function<void(const Status&)> callback) {
  CHECK_NE(flush_mode_, YBSession::AUTO_FLUSH_BACKGROUND) << "TODO: handle flush background mode";

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

Status YBSessionData::Apply(std::shared_ptr<YBOperation> yb_op) {
  if (!batcher_) {
    batcher_.reset(new Batcher(client_.get(), error_collector_.get(), shared_from_this(),
                               transaction_));
    if (timeout_.Initialized()) {
      batcher_->SetTimeout(timeout_);
    }
  }
  Status s = batcher_->Add(yb_op);
  if (!PREDICT_FALSE(s.ok())) {
    error_collector_->AddError(yb_op, s);
    return s;
  }

  if (flush_mode_ == YBSession::AUTO_FLUSH_SYNC) {
    return Flush();
  }

  return Status::OK();
}

Status YBSessionData::Flush() {
  Synchronizer s;
  FlushAsync(s.AsStatusFunctor());
  return s.Wait();
}

Status YBSessionData::SetFlushMode(YBSession::FlushMode mode) {
  if (mode == YBSession::AUTO_FLUSH_BACKGROUND) {
    return STATUS(NotSupported, "AUTO_FLUSH_BACKGROUND has not been implemented in the"
        " c++ client (see KUDU-456).");
  }
  if (batcher_ && batcher_->HasPendingOperations()) {
    // TODO: there may be a more reasonable behavior here.
    return STATUS(IllegalState, "Cannot change flush mode when writes are buffered");
  }
  if (!tight_enum_test<YBSession::FlushMode>(mode)) {
    // Be paranoid in client code.
    return STATUS(InvalidArgument, "Bad flush mode");
  }

  flush_mode_ = mode;
  return Status::OK();
}

void YBSessionData::SetTimeout(MonoDelta timeout) {
  CHECK_GE(timeout, MonoDelta::kZero);
  timeout_ = timeout;
  if (batcher_) {
    batcher_->SetTimeout(timeout);
  }
}

int YBSessionData::CountBufferedOperations() const {
  CHECK_EQ(flush_mode_, YBSession::MANUAL_FLUSH);
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
