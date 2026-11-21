// Copyright (c) YugabyteDB, Inc.
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

#pragma once

#include <atomic>
#include <mutex>
#include <vector>

#include "yb/common/hybrid_time.h"
#include "yb/common/opid.h"

#include "yb/gutil/thread_annotations.h"

#include "yb/tserver/local_catalog.service.h"

namespace yb::tserver {

// Receives the master leader's system catalog pushes. A tserver without a copy of the catalog
// answers NO_COPY. With TEST_local_catalog_record_pushes set it is a test receiver: it records
// every batch that starts at its position and moves its position to the end of the batch.
class LocalCatalogServiceImpl : public LocalCatalogServiceIf {
 public:
  explicit LocalCatalogServiceImpl(const scoped_refptr<MetricEntity>& metric_entity);

  void PushCatalogChanges(
      const PushCatalogChangesRequestPB* req, PushCatalogChangesResponsePB* resp,
      rpc::RpcContext context) override;

  // Sets the test receiver's position, as if its copy had been bootstrapped there, and clears its
  // safe time.
  void TEST_SetPosition(const OpId& position);

  // Returns the batches recorded since the previous call.
  std::vector<PushCatalogChangesRequestPB> TEST_TakeRecordedPushes();

  // The number of pushes answered, with success or failure.
  int64_t TEST_NumAnswered() const { return TEST_num_answered_; }

  // Fails this receiver's pushes, as TEST_local_catalog_fail_pushes does for every receiver.
  void TEST_SetFailPushes(bool fail) { TEST_fail_pushes_ = fail; }

 private:
  std::mutex mutex_;
  OpId position_ GUARDED_BY(mutex_);
  HybridTime safe_time_ GUARDED_BY(mutex_);
  std::vector<PushCatalogChangesRequestPB> recorded_pushes_ GUARDED_BY(mutex_);
  std::atomic<int64_t> TEST_num_answered_{0};
  std::atomic<bool> TEST_fail_pushes_{false};
};

}  // namespace yb::tserver
