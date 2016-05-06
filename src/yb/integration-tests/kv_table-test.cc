// Copyright (c) YugaByte, Inc.

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <future>
#include <gflags/gflags.h>
#include <glog/logging.h>

#include "yb/client/callbacks.h"
#include "yb/client/client-test-util.h"
#include "yb/gutil/ref_counted.h"
#include "yb/gutil/strings/split.h"
#include "yb/gutil/strings/strcat.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/integration-tests/load_generator.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/master/mini_master.h"
#include "yb/tablet/maintenance_manager.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metrics.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/util/random.h"
#include "yb/util/random_util.h"
#include "yb/util/stopwatch.h"
#include "yb/util/subprocess.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"

using std::string;
using std::vector;
using std::unique_ptr;

using yb::client::YBScanner;
using yb::client::YBScanBatch;
using yb::client::YBPredicate;
using yb::client::YBValue;

using yb::client::sp::shared_ptr;

namespace yb {
namespace tablet {

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
using strings::Substitute;

class KVTableTest : public YBTest {
 protected:
  KVTableTest() {
  }

  virtual void SetUp() OVERRIDE {
    YBTest::SetUp();
    // Start mini-cluster with 1 tserver, config client options
    cluster_.reset(new MiniCluster(env_.get(), MiniClusterOptions()));
    ASSERT_OK(cluster_->Start());

    YBClientBuilder builder;
    builder.add_master_server_addr(
      cluster_->mini_master()->bound_rpc_addr_str());
    builder.default_rpc_timeout(MonoDelta::FromSeconds(30));
    ASSERT_OK(builder.Build(&client_));

    unique_ptr<YBTableCreator> table_creator(client_->NewTableCreator());

    YBSchemaBuilder b;
    b.AddColumn("k")->Type(YBColumnSchema::BINARY)->NotNull()->PrimaryKey();
    b.AddColumn("v")->Type(YBColumnSchema::BINARY)->NotNull();
    ASSERT_OK(b.Build(&schema_));

    ASSERT_OK(table_creator->table_name(kTableName)
        .table_type(YBTableType::KEY_VALUE_TABLE_TYPE)
        .num_replicas(1)
        .schema(&schema_)
        .Create());
    ASSERT_OK(client_->OpenTable(kTableName, &table_));
    ASSERT_EQ(YBTableType::KEY_VALUE_TABLE_TYPE, table_->table_type());
    session_ = NewSession();
  }

  virtual void TearDown() OVERRIDE {
    if (cluster_) {
      cluster_->Shutdown();
    }
    YBTest::TearDown();
  }

 protected:
  shared_ptr<YBTable> table_;
  shared_ptr<YBClient> client_;

  void PutSampleKeysValues() {
    PutKeyValue("key123", "value123");
    PutKeyValue("key200", "value200");
    PutKeyValue("key300", "value300");    
  }

  void PutKeyValue(string key, string value) {
    PutKeyValue(session_.get(), std::move(key), std::move(value));
  }

  void ConfigureScanner(YBScanner* scanner) {
    scanner->SetSelection(YBClient::ReplicaSelection::LEADER_ONLY);
    ASSERT_OK(scanner->SetProjectedColumns({ "k", "v" }));
  }

  void GetScanResults(YBScanner* scanner, vector<pair<string, string>>* result_kvs) {
    while (scanner->HasMoreRows()) {
      vector<YBScanBatch::RowPtr> rows;
      scanner->NextBatch(&rows);
      for (auto row : rows) {
        Slice returned_key, returned_value;
        ASSERT_OK(row.GetBinary("k", &returned_key));
        ASSERT_OK(row.GetBinary("v", &returned_value));
        result_kvs->emplace_back(make_pair(returned_key.ToString(), returned_value.ToString()));
      }
    }
  }

 private:
  shared_ptr<YBSession> NewSession() {
    shared_ptr<YBSession> session = client_->NewSession();
    session->SetTimeoutMillis(kSessionTimeoutMs);
    CHECK_OK(session->SetFlushMode(YBSession::MANUAL_FLUSH));
    return session;
  }

  void PutKeyValue(YBSession* session, string key, string value) {
    unique_ptr<YBInsert> insert(table_->NewInsert());
    insert->mutable_row()->SetBinary("k", key);
    insert->mutable_row()->SetBinary("v", value);
    ASSERT_OK(session->Apply(insert.release()));
    ASSERT_OK(session->Flush());
  }

  static const char* const kTableName;
  static constexpr int kSessionTimeoutMs = 60000;

  std::shared_ptr<MiniCluster> cluster_;
  YBSchema schema_;
  shared_ptr<YBSession> session_;
};

const char* const KVTableTest::kTableName = "kv-table-test-tbl";

TEST_F(KVTableTest, SimpleKVTableTest) {
  NO_FATALS(PutSampleKeysValues());

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

TEST_F(KVTableTest, LoadTest) {
  std::atomic_bool stop_flag(false);
  int rows = 10000;
  int start_key = 0;
  int writer_threads = 4;
  int reader_threads = 4;
  int value_size_bytes = 16;
  int max_write_errors = 0;
  int max_read_errors = 0;
  yb::load_generator::MultiThreadedWriter writer(
      rows, start_key, writer_threads, client_.get(), table_.get(), &stop_flag, value_size_bytes,
      max_write_errors);
  yb::load_generator::MultiThreadedReader reader(
      rows, reader_threads, client_.get(), table_.get(), writer.InsertionPoint(),
      writer.InsertedKeys(), writer.FailedKeys(), &stop_flag, value_size_bytes, max_read_errors);

  writer.Start();
  reader.Start();
  writer.WaitForCompletion();
  LOG(INFO) << "Writing complete";

  // The reader will not stop on its own, so we stop it after a couple of seconds after the writer
  // stops. The stop flag could also already be set by a read/write error, and we don't wait
  // in that case.
  if (!stop_flag.load()) {
    SleepFor(MonoDelta::FromSeconds(2));
  }

  reader.Stop();
  reader.WaitForCompletion();
  LOG(INFO) << "Reading complete";

  ASSERT_EQ(0, writer.NumWriteErrors());
  ASSERT_EQ(0, reader.NumReadErrors());
  ASSERT_EQ(rows, writer.NumInsertedKeys());
}

} // namespace tablet
} // namespace yb
