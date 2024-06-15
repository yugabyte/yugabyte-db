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
//

#pragma once

#include "yb/client/snapshot_test_util.h"
#include "yb/client/txn-test-base.h"

#include "yb/integration-tests/mini_cluster.h"

DECLARE_bool(enable_history_cutoff_propagation);
DECLARE_int32(history_cutoff_propagation_interval_ms);
DECLARE_uint64(snapshot_coordinator_poll_interval_ms);

namespace yb {
namespace client {

class SnapshotScheduleTest : public TransactionTestBase<MiniCluster> {
 public:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_history_cutoff_propagation) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_snapshot_coordinator_poll_interval_ms) = 250;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_history_cutoff_propagation_interval_ms) = 100;
    num_tablets_ = 1;
    TransactionTestBase<MiniCluster>::SetUp();
    snapshot_util_ = std::make_unique<SnapshotTestUtil>();
    snapshot_util_->SetProxy(&client_->proxy_cache());
    snapshot_util_->SetCluster(cluster_.get());
  }
  std::unique_ptr<SnapshotTestUtil> snapshot_util_;
};

} // namespace client
} // namespace yb
