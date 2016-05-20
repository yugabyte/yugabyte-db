// Copyright (c) YugaByte, Inc.

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <future>
#include <gflags/gflags.h>
#include <glog/logging.h>

#include "yb/client/callbacks.h"
#include "yb/client/client-test-util.h"
#include "yb/gutil/strings/split.h"
#include "yb/gutil/strings/strcat.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/master/mini_master.h"
#include "yb/tablet/maintenance_manager.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tserver/tablet_server.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

#include "yb/integration-tests/load_generator.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/kv_table_test_base.h"

using std::string;
using std::vector;
using std::unique_ptr;

using yb::client::YBScanner;
using yb::client::YBScanBatch;
using yb::client::YBPredicate;
using yb::client::YBValue;

using yb::client::sp::shared_ptr;

namespace yb {
namespace integration_tests {

using client::YBClient;
using client::YBClientBuilder;
using client::YBColumnSchema;
using client::YBInsert;
using client::YBRowResult;
using client::YBScanner;
using client::YBSchema;
using client::YBSchemaBuilder;
using client::YBSession;
using client::YBStatusMemberCallback;
using client::YBTable;
using client::YBTableCreator;
using client::YBTableType;
using strings::Split;

class KVTableTest : public KVTableTestBase {
 protected:

  virtual bool use_external_mini_cluster() OVERRIDE { return false; }

 protected:

  void PutSampleKeysValues() {
    PutKeyValue("key123", "value123");
    PutKeyValue("key200", "value200");
    PutKeyValue("key300", "value300");
  }

  void CheckSampleKeysValues() {
    YBScanner scanner(table_.get());
    ConfigureScanner(&scanner);
    ASSERT_OK(scanner.Open());

    vector<pair<string, string>> result_kvs;
    GetScanResults(&scanner, &result_kvs);

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
  NO_FATALS(PutSampleKeysValues());
  NO_FATALS(CheckSampleKeysValues());
}

TEST_F(KVTableTest, PointQuery) {
  NO_FATALS(PutSampleKeysValues());

  YBScanner scanner(table_.get());
  NO_FATALS(ConfigureScanner(&scanner));
  ASSERT_OK(
      scanner.AddConjunctPredicate(
          table_->NewComparisonPredicate(
              "k", YBPredicate::EQUAL, YBValue::CopyString("key200"))));
  ASSERT_OK(scanner.Open());
  vector<pair<string, string>> result_kvs;
  GetScanResults(&scanner, &result_kvs);
  ASSERT_EQ(1, result_kvs.size());
  ASSERT_EQ("key200", result_kvs.front().first);
  ASSERT_EQ("value200", result_kvs.front().second);
}

TEST_F(KVTableTest, Eng135MetricsTest) {
  for (int idx = 0; idx < 10; ++idx) {
    NO_FATALS(PutSampleKeysValues());
    NO_FATALS(CheckSampleKeysValues());
    NO_FATALS(DeleteTable());
    NO_FATALS(CreateTable());
    NO_FATALS(OpenTable());
  }
}

TEST_F(KVTableTest, LoadTest) {
  std::atomic_bool stop_flag(false);
  int rows = 5000;
  int start_key = 0;
  int writer_threads = 4;
  int reader_threads = 4;
  int value_size_bytes = 16;
  int max_write_errors = 0;
  int max_read_errors = 0;
  int retries_on_empty_read = 0;
  yb::load_generator::MultiThreadedWriter writer(
      rows, start_key, writer_threads, client_.get(), table_.get(), &stop_flag, value_size_bytes,
      max_write_errors);
  yb::load_generator::MultiThreadedReader reader(
      rows, reader_threads, client_.get(), table_.get(), writer.InsertionPoint(),
      writer.InsertedKeys(), writer.FailedKeys(), &stop_flag, value_size_bytes, max_read_errors,
      retries_on_empty_read);

  writer.Start();
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
}

TEST_F(KVTableTest, Restart) {
  NO_FATALS(PutSampleKeysValues());
  // Check we've written the data successfully.
  NO_FATALS(CheckSampleKeysValues());
  NO_FATALS(RestartCluster());

  LOG(INFO) << "Checking entries written before the cluster restart";
  NO_FATALS(CheckSampleKeysValues());
  LOG(INFO) << "Issuing additional write operations";
  NO_FATALS(PutSampleKeysValues());
  NO_FATALS(CheckSampleKeysValues());
}

} // namespace integration_tests
} // namespace yb
