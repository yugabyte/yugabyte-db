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
#include <cmath>
#include <cstdlib>
#include <future>

#include "yb/util/flags.h"
#include "yb/util/logging.h"

#include "yb/client/callbacks.h"
#include "yb/client/client.h"
#include "yb/client/table.h"

#include "yb/gutil/strings/split.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/integration-tests/cluster_verifier.h"
#include "yb/integration-tests/load_generator.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_table_test_base.h"

#include "yb/master/mini_master.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"

#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

using namespace std::literals;

using std::string;

namespace yb {
namespace integration_tests {

class FlushUnderLoadTest : public YBTableTestBase {
 protected:

  bool use_external_mini_cluster() override { return false; }

  int num_tablets() override { return 1; }
  size_t num_tablet_servers() override { return 1; }
};

TEST_F(FlushUnderLoadTest, LoadTest) {
  std::atomic_bool stop_requested_flag(false);
  int rows = 5000;
  int start_key = 0;
  int writer_threads = 2;
  int reader_threads = 2;
  int value_size_bytes = 16;
  int max_write_errors = 0;
  int max_read_errors = 0;

  // Create two separate clients for reads and writes.
  auto write_client = CreateYBClient();
  auto read_client = CreateYBClient();
  yb::load_generator::YBSessionFactory write_session_factory(write_client.get(), &table_);
  yb::load_generator::YBSessionFactory read_session_factory(read_client.get(), &table_);

  yb::load_generator::MultiThreadedWriter writer(rows, start_key, writer_threads,
                                                 &write_session_factory, &stop_requested_flag,
                                                 value_size_bytes, max_write_errors);
  std::atomic<bool> pause_flag{false};
  writer.set_pause_flag(&pause_flag);
  yb::load_generator::MultiThreadedReader reader(rows, reader_threads, &read_session_factory,
                                                 writer.InsertionPoint(), writer.InsertedKeys(),
                                                 writer.FailedKeys(), &stop_requested_flag,
                                                 value_size_bytes, max_read_errors);

  writer.Start();
  // Having separate write requires adding in write client id to the reader.
  reader.set_client_id(write_session_factory.ClientId());
  reader.Start();

  while (writer.IsRunning()) {
    LOG(INFO) << "Pausing the workload";
    pause_flag = true;
    std::this_thread::sleep_for(500ms);
    LOG(INFO) << "Flushing all tablets";
    for (int i = 0; i < 3; ++i) {
      // In many cases there will be no records and we'll create an empty immutable memtable,
      // which is what we're trying to achieve.
      LOG(INFO) << "Switching memtables for all tablet servers";
      ASSERT_OK(mini_cluster_->SwitchMemtables());

      // Flush tablets, wait, etc.
      for (const auto& mini_ts : mini_cluster_->mini_tablet_servers()) {
        LOG(INFO) << "Flushing tablets for tablet server " << mini_ts->server()->permanent_uuid();
        ASSERT_OK(mini_ts->FlushTablets());
      }
      std::this_thread::sleep_for(500ms);
    }
    LOG(INFO) << "Resuming the workload";
    pause_flag = false;
    std::this_thread::sleep_for(1s);
  }
  writer.WaitForCompletion();
  LOG(INFO) << "Writing complete";

  // The reader will not stop on its own, so we stop it after a couple of seconds after the writer
  // stops.
  SleepFor(MonoDelta::FromSeconds(2));
  reader.Stop();
  reader.WaitForCompletion();
  LOG(INFO) << "Reading complete";

  ASSERT_EQ(0, writer.num_write_errors());
  ASSERT_EQ(0, reader.num_read_errors());
  ASSERT_GE(writer.num_writes(), rows);

  ClusterVerifier cluster_verifier(mini_cluster());
  ASSERT_NO_FATALS(cluster_verifier.CheckCluster());
  ASSERT_NO_FATALS(cluster_verifier.CheckRowCount(table_->name(), ClusterVerifier::EXACTLY, rows));
}

}  // namespace integration_tests
}  // namespace yb
