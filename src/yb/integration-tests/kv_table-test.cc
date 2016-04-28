// Copyright (c) YugaByte, Inc.

#include <cmath>
#include <cstdlib>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <memory>
#include <signal.h>
#include <string>
#include <vector>
#include <future>

#include "yb/client/callbacks.h"
#include "yb/client/client.h"
#include "yb/client/scan_predicate.h"
#include "yb/client/scan_batch.h"
#include "yb/client/value.h"
#include "yb/client/client-test-util.h"
#include "yb/client/row_result.h"
#include "yb/client/write_op.h"
#include "yb/gutil/gscoped_ptr.h"
#include "yb/gutil/ref_counted.h"
#include "yb/gutil/strings/split.h"
#include "yb/gutil/strings/strcat.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/integration-tests/mini_cluster.h"
#include "yb/master/mini_master.h"
#include "yb/tablet/maintenance_manager.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metrics.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/util/async_util.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/errno.h"
#include "yb/util/stopwatch.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"
#include "yb/util/status.h"
#include "yb/util/subprocess.h"
#include "yb/util/thread.h"
#include "yb/util/random.h"
#include "yb/util/random_util.h"

using std::string;
using std::vector;

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
    }

    virtual void TearDown() OVERRIDE {
      if (cluster_) {
        cluster_->Shutdown();
      }
      YBTest::TearDown();
    }

   protected:
    void CreateTable() {
      gscoped_ptr<YBTableCreator> table_creator(client_->NewTableCreator());

      YBSchemaBuilder b;
      b.AddColumn("k")->Type(YBColumnSchema::BINARY)->NotNull()->PrimaryKey();
      b.AddColumn("v")->Type(YBColumnSchema::BINARY)->NotNull();
      CHECK_OK(b.Build(&schema_));

      ASSERT_OK(table_creator->table_name(kTableName)
        .table_type(YBTableType::KEY_VALUE_TABLE_TYPE)
        .num_replicas(1)
        .schema(&schema_)
        .Create());
      ASSERT_OK(client_->OpenTable(kTableName, &table_));
      ASSERT_EQ(YBTableType::KEY_VALUE_TABLE_TYPE, table_->table_type());
      session_ = NewSession();
    }

    void InitCluster() {
      // Start mini-cluster with 1 tserver, config client options
      cluster_.reset(new MiniCluster(env_.get(), MiniClusterOptions()));
      ASSERT_OK(cluster_->Start());
      YBClientBuilder builder;
      builder.add_master_server_addr(
        cluster_->mini_master()->bound_rpc_addr_str());
      builder.default_rpc_timeout(MonoDelta::FromSeconds(30));
      ASSERT_OK(builder.Build(&client_));
    }

    // Adds newly generated client's session and table pointers to arrays at id
    void CreateNewClient() {
      ASSERT_OK(client_->OpenTable(kTableName, &table_));

    }

    void ReadWriteKeysValues() {
      PutKeyValue("key123", "value123");
      PutKeyValue("key200", "value200");
      PutKeyValue("key300", "value300");

      {
        YBScanner scanner(table_.get());
        ConfigureScanner(&scanner);
        CHECK_OK(scanner.Open());

        vector<pair<string, string>> result_kvs;
        GetScanResults(&scanner, &result_kvs);

        ASSERT_EQ(3, result_kvs.size());
        ASSERT_EQ("key123", result_kvs.front().first);
        ASSERT_EQ("value123", result_kvs.front().second);
        ASSERT_EQ("key200", result_kvs[1].first);
        ASSERT_EQ("value200", result_kvs[1].second);
        ASSERT_EQ("key300", result_kvs[2].first);
        ASSERT_EQ("value300", result_kvs[2].second);
        scanner.Close();
      }

      {
        // Test a point query
        YBScanner scanner(table_.get());
        ConfigureScanner(&scanner);
        CHECK_OK(
          scanner.AddConjunctPredicate(
            table_->NewComparisonPredicate(
              "k", YBPredicate::EQUAL,
              YBValue::CopyString("key200"))));
        CHECK_OK(scanner.Open());
        vector<pair<string, string>> result_kvs;
        GetScanResults(&scanner, &result_kvs);
        ASSERT_EQ(1, result_kvs.size());
        ASSERT_EQ("key200", result_kvs.front().first);
        ASSERT_EQ("value200", result_kvs.front().second);
      }

      {
        vector<std::future<void>> futures;
        for (int thread_idx = 0; thread_idx < 16; ++thread_idx) {
          futures.push_back( std::async(std::launch::async, [this, thread_idx]() {
            auto session = NewSession();
            LOG(INFO) << "Starting writer thread " << thread_idx;
            for (int i = 1; i <= 5000; ++i) {
              PutKeyValue(session.get(),
                strings::Substitute("key$0", i), strings::Substitute("value$0", i));
              if (i % 1000 == 0) {
                LOG(INFO) << "Writer thread " << thread_idx << " has written " << i << " rows";
              }
            }
          }));
        }

        for (auto& fut : futures) {
          fut.wait();
        }
      }
    }

   private:
    shared_ptr<YBSession> NewSession() {
      shared_ptr<YBSession> session = client_->NewSession();
      session->SetTimeoutMillis(60000);
      CHECK_OK(session->SetFlushMode(YBSession::MANUAL_FLUSH));
      return session;
    }

    void PutKeyValue(YBSession* session, string key, string value) {
      gscoped_ptr<YBInsert> insert(table_->NewInsert());
      insert->mutable_row()->SetBinary("k", key);
      insert->mutable_row()->SetBinary("v", value);
      CHECK_OK(session->Apply(insert.release()));
      CHECK_OK(session->Flush());
    }

    void PutKeyValue(string key, string value) {
      PutKeyValue(session_.get(), std::move(key), std::move(value));
    }


    void ConfigureScanner(YBScanner* scanner) {
      scanner->SetSelection(YBClient::ReplicaSelection::LEADER_ONLY);
      CHECK_OK(scanner->SetProjectedColumns({ "k", "v" }));
    }

    void GetScanResults(YBScanner* scanner, vector<pair<string, string>>* result_kvs) {
      while (scanner->HasMoreRows()) {
        vector<YBScanBatch::RowPtr> rows;
        scanner->NextBatch(&rows);
        for (auto row : rows) {
          Slice returned_key, returned_value;
          CHECK_OK(row.GetBinary("k", &returned_key));
          CHECK_OK(row.GetBinary("v", &returned_value));
          result_kvs->emplace_back(make_pair(returned_key.ToString(), returned_value.ToString()));
        }
      }
    }

    static const char* const kTableName;
    static const int kSessionTimeoutMs = 60000;

    std::shared_ptr<MiniCluster> cluster_;
    shared_ptr<YBClient> client_;
    shared_ptr<YBTable> table_;
    YBSchema schema_;
    shared_ptr<YBSession> session_;
  };

  const char* const KVTableTest::kTableName = "kv-table-test-tbl";

  TEST_F(KVTableTest, SimpleKVTableTest) {
    NO_FATALS(InitCluster());
    NO_FATALS(CreateTable());
    NO_FATALS(CreateNewClient());
    NO_FATALS(ReadWriteKeysValues());
  }

} // namespace tablet
} // namespace yb
