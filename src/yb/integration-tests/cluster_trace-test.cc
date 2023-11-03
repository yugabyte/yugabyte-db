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

#include "yb/integration-tests/external_mini_cluster-itest-base.h"
#include "yb/integration-tests/test_workload.h"

namespace yb {

class TraceClusterTest : public ExternalMiniClusterITestBase {
 public:
  void SetTracingFlag(bool enabled, int sampling_freq, int print_freq, bool dump_all_traces) {
    for (int i = 0; i < 3; ++i) {
      ASSERT_OK(cluster_->SetFlag(
          cluster_->tablet_server(i), "enable_tracing", enabled ? "1" : "0"));
      ASSERT_OK(cluster_->SetFlag(
          cluster_->tablet_server(i), "collect_end_to_end_traces", "true"));
      ASSERT_OK(cluster_->SetFlag(
          cluster_->tablet_server(i), "sampled_trace_1_in_n", yb::ToString(sampling_freq)));
      ASSERT_OK(cluster_->SetFlag(
          cluster_->tablet_server(i), "print_trace_every", yb::ToString(print_freq)));
      ASSERT_OK(cluster_->SetFlag(
          cluster_->tablet_server(i), "rpc_dump_all_traces", (dump_all_traces ? "true" : "false")));
    }
    WaitForInserts(workload_->rows_inserted() + 1000);
  }

  void PrepareCluster() {
    ASSERT_NO_FATALS(StartCluster());
    workload_.reset(new TestWorkload(cluster_.get()));
    workload_->Setup();
    workload_->Start();
    // Wait for some data to come in.
    WaitForInserts(100);
  }

  void WaitForInserts(int64_t num_rows) {
    while (workload_->rows_inserted() < num_rows) {
      SleepFor(MonoDelta::FromMilliseconds(10));
    }
  }

  std::unique_ptr<TestWorkload> workload_;
};

TEST_F(TraceClusterTest, TestTracingUnderLoad) {
  // Start the cluster and then the workload.
  PrepareCluster();

  // Cycle through enable/disable a bunch of times.
  for (int i = 0; i < 10; ++i) {
    auto rows_inserted = workload_->rows_inserted();
    const bool enable_tracing = static_cast<bool>(i % 2);
    const int sampling_freq = (i % 5);
    const int print_freq = 1;
    const bool dump_all_traces = static_cast<bool>(i % 3);
    SetTracingFlag(enable_tracing, sampling_freq, print_freq, dump_all_traces);
    WaitForInserts(rows_inserted + 100);
  }

  // Stop the workload.
  workload_->StopAndJoin();
  cluster_->AssertNoCrashes();
}

}  // namespace yb
