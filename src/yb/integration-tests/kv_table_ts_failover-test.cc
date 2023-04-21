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

#include <atomic>
#include <memory>
#include <vector>

#include "yb/client/client.h"
#include "yb/client/table.h"

#include "yb/integration-tests/cluster_verifier.h"
#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/load_generator.h"
#include "yb/integration-tests/yb_table_test_base.h"

#include "yb/util/test_util.h"
#include "yb/util/flags.h"

DEFINE_NON_RUNTIME_int32(test_num_iter,
             1,
             "Number of iterations for key-value table tablet server failover test");

namespace yb {

using integration_tests::YBTableTestBase;

class KVTableTsFailoverTest : public YBTableTestBase {
 public:
  bool use_external_mini_cluster() override { return true; }

  bool enable_ysql() override { return NonTsanVsTsan(true, false); }
};

TEST_F(KVTableTsFailoverTest, KillTabletServerUnderLoad) {
  for (int i = 1; i <= FLAGS_test_num_iter; ++i) {
    std::atomic_bool stop_requested_flag(false);
    int rows = 1000000;
    int start_key = 0;
    int writer_threads = 4;
    int reader_threads = 4;
    int value_size_bytes = 16;
    int max_write_errors = 0;
    int max_read_errors = 0;

    // Create two separate clients for read and writes.
    auto write_client = CreateYBClient();
    auto read_client = CreateYBClient();
    yb::load_generator::YBSessionFactory write_session_factory(write_client.get(), &table_);
    yb::load_generator::YBSessionFactory read_session_factory(read_client.get(), &table_);

    yb::load_generator::MultiThreadedWriter writer(rows, start_key, writer_threads,
                                                   &write_session_factory, &stop_requested_flag,
                                                   value_size_bytes, max_write_errors);
    yb::load_generator::MultiThreadedReader reader(rows, reader_threads, &read_session_factory,
                                                   writer.InsertionPoint(), writer.InsertedKeys(),
                                                   writer.FailedKeys(), &stop_requested_flag,
                                                   value_size_bytes, max_read_errors);

    writer.Start();
    // Having separate write requires adding in write client id to the reader.
    reader.set_client_id(write_session_factory.ClientId());
    reader.Start();

    for (int i = 0; i < 3; ++i) {
      SleepFor(MonoDelta::FromSeconds(5));
      LOG(INFO) << "Killing tablet server #" << i;
      external_mini_cluster()->tablet_server(i)->Shutdown();
      LOG(INFO) << "Re-starting tablet server #" << i;
      ASSERT_OK(external_mini_cluster()->tablet_server(i)->Restart());
      ASSERT_OK(external_mini_cluster()->WaitForTabletServerCount(3, MonoDelta::FromSeconds(20)));
    }

    stop_requested_flag.store(true);  // stop both reader and writer
    writer.WaitForCompletion();
    LOG(INFO) << "Writing complete";

    reader.WaitForCompletion();
    LOG(INFO) << "Reading complete";

    if (FLAGS_test_num_iter > 1) {
      LOG(INFO) << "Completed iteration " << i << " of the test";
    }

    ASSERT_NO_FATALS(writer.AssertSucceeded());
    ASSERT_NO_FATALS(reader.AssertSucceeded());

    // Assuming every thread has time to do at least 50 writes. Had to lower this from 100 after
    // enabling TSAN.
    LOG(INFO) << "Reads: " << reader.num_reads() << ", writes: " << writer.num_writes();
    ASSERT_GE(writer.num_writes(), writer_threads * 50);
    // Assuming at least 100 reads and 100 writes.
    ASSERT_GE(reader.num_reads(), 100);
    ASSERT_GE(writer.num_writes(), 100);

    ClusterVerifier cluster_verifier(external_mini_cluster());
    ASSERT_NO_FATALS(cluster_verifier.CheckCluster());
    ASSERT_NO_FATALS(cluster_verifier.CheckRowCount(table_->name(), ClusterVerifier::EXACTLY,
        writer.num_writes()));
  }
}

}  // namespace yb
