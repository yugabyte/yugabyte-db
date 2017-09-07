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

#include "yb/client/client.h"
#include "yb/util/locks.h"

namespace yb {

namespace client {

namespace internal {
class Batcher;
class ErrorCollector;
} // internal

// Single instance of YBSessionData is used by YBSession.
// But each Batcher stores weak_ptr to YBSessionData.
class YBSessionData : public std::enable_shared_from_this<YBSessionData> {
 public:
  explicit YBSessionData(std::shared_ptr<YBClient> client,
                         bool read_only,
                         const YBTransactionPtr& transaction);
  ~YBSessionData();

  YBSessionData(const YBSessionData&) = delete;
  void operator=(const YBSessionData&) = delete;

  void Init();

  bool is_read_only() { return read_only_; }

  CHECKED_STATUS Apply(std::shared_ptr<YBOperation> yb_op);

  void FlushAsync(YBStatusCallback* callback);

  CHECKED_STATUS Flush();

  // Called by Batcher when a flush has finished.
  void FlushFinished(internal::Batcher* b);

  // Returns Status::IllegalState() if 'force' is false and there are still pending
  // operations. If 'force' is true batcher_ is aborted even if there are pending
  // operations.
  CHECKED_STATUS Close(bool force);

  // Swap in a new Batcher instance, returning the old one in '*old_batcher', unless it is
  // NULL.
  scoped_refptr<internal::Batcher> NewBatcher();

  // The client that this session is associated with.
  const std::shared_ptr<YBClient> client_;

  // In case of read_only YBSessions, writes are not allowed. Otherwise, reads are not allowed.
  bool read_only_;

  YBTransactionPtr transaction_;

  // Lock protecting internal state.
  // Note that this lock should not be taken if the thread is already holding
  // a Batcher lock. This must be acquired first.
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
  std::unordered_set<internal::Batcher*> flushed_batchers_;

  YBSession::FlushMode flush_mode_ = YBSession::AUTO_FLUSH_SYNC;
  YBSession::ExternalConsistencyMode external_consistency_mode_ = YBSession::CLIENT_PROPAGATED;

  // Timeout for the next batch.
  int timeout_ms_ = -1;
};

}  // namespace client
}  // namespace yb

#endif // YB_CLIENT_SESSION_INTERNAL_H_
