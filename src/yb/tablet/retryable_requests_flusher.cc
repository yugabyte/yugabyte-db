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

#include "yb/tablet/retryable_requests_flusher.h"
#include "yb/consensus/raft_consensus.h"
#include "yb/util/scope_exit.h"

DEFINE_RUNTIME_AUTO_bool(enable_flush_retryable_requests, kLocalPersisted, false, true,
    "If enabled, will flush retryable requests structure to the disk when roll the log segment, "
    "which helps speedup bootstrap process");

using namespace std::literals;

namespace yb {
namespace tablet {

Status RetryableRequestsFlusher::FlushRetryableRequests() {
  // Should do flush exclusively. Also if there's already an active flush,
  // should be fine to skip this one.
  SCHECK_FORMAT(SetFlushing(),
                AlreadyPresent,
                "Tablet $0 has a flush task still in progress",
                tablet_id_);
  auto se = ScopeExit([this] {
    CHECK(this->UnsetFlushing());
  });
  return raft_consensus_->FlushRetryableRequests();
}

Status RetryableRequestsFlusher::SubmitFlushRetryableRequestsTask() {
  if (!flush_retryable_requests_pool_token_) {
    return Status::OK();
  }
  LOG(INFO) << "Tablet " << tablet_id_ << " is submitting flush retryable requests task...";
  return flush_retryable_requests_pool_token_->SubmitFunc(
      std::bind(&RetryableRequestsFlusher::FlushRetryableRequests, shared_from_this()));
}

// Copy retryable requests file to dest_path and return the last flushed op_id.
Result<OpId> RetryableRequestsFlusher::CopyRetryableRequestsTo(const std::string& dest_path) {
  auto se = ScopeExit([this] {
    CHECK(this->UnsetFlushing());
  });
  while(!SetFlushing()) {
    SleepFor(1ms);
  }
  RETURN_NOT_OK(raft_consensus_->CopyRetryableRequestsTo(dest_path));
  return raft_consensus_->GetLastFlushedOpIdInRetryableRequests();
}

bool RetryableRequestsFlusher::TEST_HasRetryableRequestsOnDisk() {
  auto se = ScopeExit([this] {
    CHECK(UnsetFlushing());
  });
  while(!SetFlushing()) {
    SleepFor(1ms);
  }
  return raft_consensus_->TEST_HasRetryableRequestsOnDisk();
}

} // namespace tablet
} // namespace yb
