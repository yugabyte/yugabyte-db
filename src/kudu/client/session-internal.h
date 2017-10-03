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
#ifndef KUDU_CLIENT_SESSION_INTERNAL_H
#define KUDU_CLIENT_SESSION_INTERNAL_H

#include <unordered_set>

#include "kudu/client/client.h"
#include "kudu/util/locks.h"

namespace kudu {

namespace client {

namespace internal {
class Batcher;
class ErrorCollector;
} // internal

class KuduSession::Data {
 public:
  explicit Data(sp::shared_ptr<KuduClient> client);
  ~Data();

  void Init(const sp::shared_ptr<KuduSession>& session);

  // Swap in a new Batcher instance, returning the old one in '*old_batcher', unless it is
  // NULL.
  void NewBatcher(const sp::shared_ptr<KuduSession>& session,
                  scoped_refptr<internal::Batcher>* old_batcher);

  // Called by Batcher when a flush has finished.
  void FlushFinished(internal::Batcher* b);

  // Returns Status::IllegalState() if 'force' is false and there are still pending
  // operations. If 'force' is true batcher_ is aborted even if there are pending
  // operations.
  Status Close(bool force);

  // The client that this session is associated with.
  const sp::shared_ptr<KuduClient> client_;

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

  FlushMode flush_mode_;
  kudu::client::KuduSession::ExternalConsistencyMode external_consistency_mode_;

  // Timeout for the next batch.
  int timeout_ms_;

  DISALLOW_COPY_AND_ASSIGN(Data);
};

} // namespace client
} // namespace kudu

#endif
