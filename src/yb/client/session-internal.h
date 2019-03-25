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
#ifndef YB_CLIENT_SESSION_INTERNAL_H_
#define YB_CLIENT_SESSION_INTERNAL_H_

#include <unordered_set>

#include "yb/client/async_rpc.h"
#include "yb/common/consistent_read_point.h"
#include "yb/util/locks.h"

namespace yb {

namespace client {

namespace internal {
class Batcher;
class ErrorCollector;
} // internal

// Single instance of YBSessionData is used by YBSession.
// But each Batcher stores weak_ptr to YBSessionData.
//
// For description of public interface see YBSession comments.
class YBSessionData : public std::enable_shared_from_this<YBSessionData> {
 public:
  explicit YBSessionData(std::shared_ptr<YBClient> client,
                         const scoped_refptr<ClockBase>& clock = nullptr);
  ~YBSessionData();

  YBSessionData(const YBSessionData&) = delete;
  void operator=(const YBSessionData&) = delete;

  CHECKED_STATUS Apply(YBOperationPtr yb_op);
  CHECKED_STATUS Apply(const std::vector<YBOperationPtr>& ops);
  CHECKED_STATUS ApplyAndFlush(YBOperationPtr yb_op);
  CHECKED_STATUS ApplyAndFlush(
      const std::vector<YBOperationPtr>& ops, VerifyResponse verify_response);

  void FlushAsync(StatusFunctor callback);

  CHECKED_STATUS Flush();

  // Called by Batcher when a flush has finished.
  void FlushFinished(internal::BatcherPtr b);

  // Abort the unflushed or in-flight operations.
  void Abort();

  void SetReadPoint(Restart restart);

  // Changed transaction used by this session.
  void SetTransaction(YBTransactionPtr transaction);

  // Returns Status::IllegalState() if 'force' is false and there are still pending
  // operations. If 'force' is true batcher_ is aborted even if there are pending
  // operations.
  CHECKED_STATUS Close(bool force);

  void SetTimeout(MonoDelta timeout);
  bool HasPendingOperations() const;
  int CountBufferedOperations() const;

  int CountPendingErrors() const;
  CollectedErrors GetPendingErrors();

  YBClient* client() const {
    return client_.get();
  }

  const internal::AsyncRpcMetricsPtr& async_rpc_metrics() const {
    return async_rpc_metrics_;
  }

  void set_allow_local_calls_in_curr_thread(bool flag);
  bool allow_local_calls_in_curr_thread() const;

  void SetForceConsistentRead(ForceConsistentRead value);

  void SetInTxnLimit(HybridTime value);

 private:
  ConsistentReadPoint* read_point();

  internal::Batcher& Batcher();

  // The client that this session is associated with.
  const std::shared_ptr<YBClient> client_;

  std::unique_ptr<ConsistentReadPoint> read_point_;
  YBTransactionPtr transaction_;
  bool allow_local_calls_in_curr_thread_ = true;
  bool force_consistent_read_ = false;

  // Lock protecting flushed_batchers_.
  mutable simple_spinlock lock_;

  // Buffer for errors.
  scoped_refptr<internal::ErrorCollector> error_collector_;

  // The current batcher being prepared.
  scoped_refptr<internal::Batcher> batcher_;

  // Any batchers which have been flushed but not yet finished.
  //
  // Upon a batch finishing, it will call FlushFinished(), which removes the batcher from
  // this set. This set does not hold any reference count to the Batcher, since, while
  // the flush is active, the batcher manages its own refcount. The Batcher will always
  // call FlushFinished() before it destructs itself, so we're guaranteed that these
  // pointers stay valid.
  std::unordered_set<
      internal::BatcherPtr, ScopedRefPtrHashFunctor, ScopedRefPtrEqualsFunctor> flushed_batchers_;

  // Timeout for the next batch.
  MonoDelta timeout_;

  internal::AsyncRpcMetricsPtr async_rpc_metrics_;
};

}  // namespace client
}  // namespace yb

#endif // YB_CLIENT_SESSION_INTERNAL_H_
