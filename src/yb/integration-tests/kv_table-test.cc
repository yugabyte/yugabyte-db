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
#include "yb/client/tablet_server.h"

#include "yb/gutil/strings/split.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/integration-tests/cluster_verifier.h"
#include "yb/integration-tests/load_generator.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_table_test_base.h"

#include "yb/master/mini_master.h"

#include "yb/tserver/mini_tablet_server.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/size_literals.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_thread_holder.h"

using namespace std::literals;

using std::string;
using std::vector;

DECLARE_int32(log_cache_size_limit_mb);
DECLARE_int32(global_log_cache_size_limit_mb);

namespace yb {
namespace integration_tests {

class KVTableTest : public YBTableTestBase {
 protected:

  bool use_external_mini_cluster() override { return false; }

 protected:

  void PutSampleKeysValues() {
    PutKeyValue("key123", "value123");
    PutKeyValue("key200", "value200");
    PutKeyValue("key300", "value300");
  }

  void CheckSampleKeysValues() {
    auto result_kvs = GetScanResults(client::TableRange(table_));

    ASSERT_EQ(3, result_kvs.size());
    ASSERT_EQ("key123", result_kvs.front().first);
    ASSERT_EQ("value123", result_kvs.front().second);
    ASSERT_EQ("key200", result_kvs[1].first);
    ASSERT_EQ("value200", result_kvs[1].second);
    ASSERT_EQ("key300", result_kvs[2].first);
    ASSERT_EQ("value300", result_kvs[2].second);
  }

};

TEST_F(KVTableTest, SimpleKVTableTest) {
  ASSERT_NO_FATALS(PutSampleKeysValues());
  ASSERT_NO_FATALS(CheckSampleKeysValues());
  ClusterVerifier cluster_verifier(mini_cluster());
  ASSERT_NO_FATALS(cluster_verifier.CheckCluster());
  ASSERT_NO_FATALS(cluster_verifier.CheckRowCount(table_->name(), ClusterVerifier::EXACTLY, 3));
}

TEST_F(KVTableTest, PointQuery) {
  ASSERT_NO_FATALS(PutSampleKeysValues());

  client::TableIteratorOptions options;
  options.filter = client::FilterEqual("key200"s, "k"s);
  auto result_kvs = GetScanResults(client::TableRange(table_, options));
  ASSERT_EQ(1, result_kvs.size());
  ASSERT_EQ("key200", result_kvs.front().first);
  ASSERT_EQ("value200", result_kvs.front().second);
}

TEST_F(KVTableTest, Eng135MetricsTest) {
  ClusterVerifier cluster_verifier(mini_cluster());
  for (int idx = 0; idx < 10; ++idx) {
    ASSERT_NO_FATALS(PutSampleKeysValues());
    ASSERT_NO_FATALS(CheckSampleKeysValues());
    ASSERT_NO_FATALS(DeleteTable());
    ASSERT_NO_FATALS(CreateTable());
    ASSERT_NO_FATALS(OpenTable());
    ASSERT_NO_FATALS(cluster_verifier.CheckCluster());
    ASSERT_NO_FATALS(cluster_verifier.CheckRowCount(table_->name(), ClusterVerifier::EXACTLY, 0));
  }
}

TEST_F(KVTableTest, LoadTest) {
  std::atomic_bool stop_requested_flag(false);
  int rows = 5000;
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
  ASSERT_GE(reader.num_reads(), rows);  // assuming reads are at least as fast as writes

  ClusterVerifier cluster_verifier(mini_cluster());
  ASSERT_NO_FATALS(cluster_verifier.CheckCluster());
  ASSERT_NO_FATALS(cluster_verifier.CheckRowCount(table_->name(), ClusterVerifier::EXACTLY, rows));
}

TEST_F(KVTableTest, Restart) {
  ASSERT_NO_FATALS(PutSampleKeysValues());
  // Check we've written the data successfully.
  ASSERT_NO_FATALS(CheckSampleKeysValues());
  ASSERT_NO_FATALS(RestartCluster());

  LOG(INFO) << "Checking entries written before the cluster restart";
  ASSERT_NO_FATALS(CheckSampleKeysValues());
  ClusterVerifier cluster_verifier(mini_cluster());
  ASSERT_NO_FATALS(cluster_verifier.CheckCluster());
  ASSERT_NO_FATALS(cluster_verifier.CheckRowCount(table_->name(), ClusterVerifier::EXACTLY, 3));

  LOG(INFO) << "Issuing additional write operations";
  ASSERT_NO_FATALS(PutSampleKeysValues());
  ASSERT_NO_FATALS(CheckSampleKeysValues());

  // Wait until all tablet servers come up.
  std::vector<client::YBTabletServer> tablet_servers;
  do {
    tablet_servers = ASSERT_RESULT(client_->ListTabletServers());
    if (tablet_servers.size() == num_tablet_servers()) {
      break;
    }
    SleepFor(MonoDelta::FromMilliseconds(100));
  } while (true);

  ASSERT_NO_FATALS(cluster_verifier.CheckCluster());
  ASSERT_NO_FATALS(cluster_verifier.CheckRowCount(table_->name(), ClusterVerifier::EXACTLY, 3));
}

class KVTableSingleTabletTest : public KVTableTest {
 public:
  int num_tablets() override {
    return 1;
  }

  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_global_log_cache_size_limit_mb) = 1;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_log_cache_size_limit_mb) = 1;
    KVTableTest::SetUp();
  }
};

// Write big values with small log cache.
// And restart one tserver.
//
// So we expect that some operations would be unloaded to disk and loaded back
// after this tservers joined raft group again.
//
// Also check that we track such operations.
TEST_F_EX(KVTableTest, YB_DISABLE_TEST_ON_MACOS(BigValues), KVTableSingleTabletTest) {
  std::atomic_bool stop_requested_flag(false);
  SetFlagOnExit set_flag_on_exit(&stop_requested_flag);
  int rows = 100;
  int start_key = 0;
  int writer_threads = 4;
  int value_size_bytes = 32_KB;
  int max_write_errors = 0;

  // Create two separate clients for read and writes.
  auto write_client = CreateYBClient();
  yb::load_generator::YBSessionFactory write_session_factory(write_client.get(), &table_);

  yb::load_generator::MultiThreadedWriter writer(rows, start_key, writer_threads,
                                                 &write_session_factory, &stop_requested_flag,
                                                 value_size_bytes, max_write_errors);

  writer.Start();
  mini_cluster_->mini_tablet_server(1)->Shutdown();
  auto start_writes = writer.num_writes();
  while (writer.num_writes() - start_writes < 50) {
    std::this_thread::sleep_for(100ms);
  }
  ASSERT_OK(mini_cluster_->mini_tablet_server(1)->Start(tserver::WaitTabletsBootstrapped::kFalse));

  ASSERT_OK(WaitFor([] {
    std::vector<MemTrackerData> trackers;
    trackers.clear();
    CollectMemTrackerData(MemTracker::GetRootTracker(), 0, &trackers);
    bool found = false;
    for (const auto& data : trackers) {
      if (data.tracker->id() == "OperationsFromDisk" && data.tracker->peak_consumption()) {
        LOG(INFO) << "Tracker: " << data.tracker->ToString() << ", peak consumption: "
                  << data.tracker->peak_consumption();
        found = true;
      }
    }
    return found;
  }, 15s, "Load operations from disk"));

  writer.WaitForCompletion();
}

}  // namespace integration_tests
}  // namespace yb
