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

#include <memory>
#include <string>
#include <vector>

#include <boost/range/iterator_range.hpp>

#include "yb/client/callbacks.h"
#include "yb/client/client.h"
#include "yb/client/error.h"
#include "yb/client/schema.h"
#include "yb/client/session.h"
#include "yb/client/table_handle.h"
#include "yb/client/yb_op.h"
#include "yb/client/yb_table_name.h"

#include "yb/gutil/strings/strcat.h"

#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"

#include "yb/tserver/mini_tablet_server.h"

#include "yb/util/countdown_latch.h"
#include "yb/util/curl_util.h"
#include "yb/util/monotime.h"
#include "yb/util/status_log.h"
#include "yb/util/stopwatch.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"
#include "yb/util/thread.h"
#include "yb/util/flags.h"

using namespace std::literals;

DECLARE_int32(log_segment_size_mb);
DECLARE_int32(maintenance_manager_polling_interval_ms);
DEFINE_NON_RUNTIME_int32(mbs_for_flushes_and_rolls, 1,
             "How many MBs are needed to flush and roll");
DEFINE_NON_RUNTIME_int32(row_count, 2000,
             "How many rows will be used in this test for the base data");
DEFINE_NON_RUNTIME_int32(seconds_to_run, 4,
             "How long this test runs for, after inserting the base data, in seconds");

namespace yb {
namespace tablet {

using client::YBClient;
using client::YBSchema;
using client::YBSchemaBuilder;
using client::YBSession;
using client::YBTableName;
using std::shared_ptr;
using std::vector;
using std::string;

// This integration test tries to trigger all the update-related bits while also serving as a
// foundation for benchmarking. It first inserts 'row_count' rows and then starts two threads,
// one that continuously updates all the rows sequentially and one that scans them all, until
// it's been running for 'seconds_to_run'. It doesn't test for correctness, unless something
// FATALs.
class UpdateScanDeltaCompactionTest : public YBMiniClusterTestBase<MiniCluster> {
 protected:
  UpdateScanDeltaCompactionTest() {
    YBSchemaBuilder b;
    b.AddColumn("key")->Type(DataType::INT64)->NotNull()->HashPrimaryKey();
    b.AddColumn("string")->Type(DataType::STRING)->NotNull();
    b.AddColumn("int64")->Type(DataType::INT64)->NotNull();
    CHECK_OK(b.Build(&schema_));
  }

  void SetUp() override {
    HybridTime::TEST_SetPrettyToString(true);
    YBMiniClusterTestBase::SetUp();
  }

  void CreateTable() {
    ASSERT_NO_FATALS(InitCluster());
    ASSERT_OK(client_->CreateNamespaceIfNotExists(kTableName.namespace_name(),
                                                  kTableName.namespace_type()));
    ASSERT_OK(table_.Create(kTableName, CalcNumTablets(1), schema_, client_.get()));
  }

  void DoTearDown() override {
    if (cluster_) {
      cluster_->Shutdown();
    }
    YBMiniClusterTestBase::DoTearDown();
  }

  // Inserts row_count rows sequentially.
  void InsertBaseData();

  // Starts the update and scan threads then stops them after seconds_to_run.
  void RunThreads();

 private:
  static const YBTableName kTableName;

  void InitCluster() {
    // Start mini-cluster with 1 tserver.
    cluster_.reset(new MiniCluster(MiniClusterOptions()));
    ASSERT_OK(cluster_->Start());
    client_ = ASSERT_RESULT(cluster_->CreateClient());
  }

  shared_ptr<YBSession> CreateSession() {
    // Bumped this up from 5 sec to 30 sec in hope to fix the flakiness in this test.
    return client_->NewSession(30s);
  }

  // Continuously updates the existing data until 'stop_latch' drops to 0.
  void UpdateRows(CountDownLatch* stop_latch);

  // Continuously scans the data until 'stop_latch' drops to 0.
  void ScanRows(CountDownLatch* stop_latch) const;

  // Continuously fetch various web pages on the TS.
  void CurlWebPages(CountDownLatch* stop_latch) const;

  // Sets the passed values on the row.
  // TODO randomize the string column.
  void MakeRow(int64_t key, int64_t val, QLWriteRequestPB* req) const;

  // If 'key' is a multiple of kSessionBatchSize, it uses 'flush_future' to wait for the previous
  // batch to finish and then flushes the current one.
  Status WaitForLastBatchAndFlush(int64_t key,
                                  std::future<client::FlushStatus>* flush_future,
                                  shared_ptr<YBSession> session);

  YBSchema schema_;
  client::TableHandle table_;
  std::unique_ptr<YBClient> client_;
};

const YBTableName UpdateScanDeltaCompactionTest::kTableName(
    YQL_DATABASE_CQL, "my_keyspace", "update-scan-delta-compact-tbl");
const int kSessionBatchSize = 1000;

TEST_F(UpdateScanDeltaCompactionTest, TestAll) {
  OverrideFlagForSlowTests("seconds_to_run", "100");
  OverrideFlagForSlowTests("row_count", "1000000");
  OverrideFlagForSlowTests("mbs_for_flushes_and_rolls", "8");
  // Setting this high enough that we see the effects of flushes and compactions.
  OverrideFlagForSlowTests("maintenance_manager_polling_interval_ms", "2000");
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_log_segment_size_mb) = FLAGS_mbs_for_flushes_and_rolls;
  if (!AllowSlowTests()) {
    // Make it run more often since it's not a long test.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_maintenance_manager_polling_interval_ms) = 50;
  }

  ASSERT_NO_FATALS(CreateTable());
  ASSERT_NO_FATALS(InsertBaseData());
  ASSERT_NO_FATALS(RunThreads());
}

void UpdateScanDeltaCompactionTest::InsertBaseData() {
  shared_ptr<YBSession> session = CreateSession();
  std::promise<client::FlushStatus> promise;
  auto flush_future = promise.get_future();
  promise.set_value(client::FlushStatus());

  LOG_TIMING(INFO, "Insert") {
    for (int64_t key = 0; key < FLAGS_row_count; key++) {
      auto insert = table_.NewInsertOp();
      MakeRow(key, 0, insert->mutable_request());
      session->Apply(insert);
      ASSERT_OK(WaitForLastBatchAndFlush(key, &flush_future, session));
    }
    ASSERT_OK(WaitForLastBatchAndFlush(kSessionBatchSize, &flush_future, session));
    if (flush_future.valid()) {
      ASSERT_OK(flush_future.get().status);
    }
  }
}

void UpdateScanDeltaCompactionTest::RunThreads() {
  vector<scoped_refptr<Thread> > threads;

  CountDownLatch stop_latch(1);

  {
    scoped_refptr<Thread> t;
    ASSERT_OK(Thread::Create(CURRENT_TEST_NAME(),
                             StrCat(CURRENT_TEST_CASE_NAME(), "-update"),
                             &UpdateScanDeltaCompactionTest::UpdateRows, this,
                             &stop_latch, &t));
    threads.push_back(t);
  }

  {
    scoped_refptr<Thread> t;
    ASSERT_OK(Thread::Create(CURRENT_TEST_NAME(),
                             StrCat(CURRENT_TEST_CASE_NAME(), "-scan"),
                             &UpdateScanDeltaCompactionTest::ScanRows, this,
                             &stop_latch, &t));
    threads.push_back(t);
  }

  {
    scoped_refptr<Thread> t;
    ASSERT_OK(Thread::Create(CURRENT_TEST_NAME(),
                             StrCat(CURRENT_TEST_CASE_NAME(), "-curl"),
                             &UpdateScanDeltaCompactionTest::CurlWebPages, this,
                             &stop_latch, &t));
    threads.push_back(t);
  }

  SleepFor(MonoDelta::FromSeconds(FLAGS_seconds_to_run * 1.0));
  stop_latch.CountDown();

  for (const scoped_refptr<Thread>& thread : threads) {
    ASSERT_OK(ThreadJoiner(thread.get()).warn_every(500ms).Join());
  }
}

void UpdateScanDeltaCompactionTest::UpdateRows(CountDownLatch* stop_latch) {
  shared_ptr<YBSession> session = CreateSession();

  for (int64_t iteration = 1; stop_latch->count() > 0; iteration++) {
    LOG_TIMING(INFO, "Update") {
      std::promise<client::FlushStatus> promise;
      auto flush_future = promise.get_future();
      promise.set_value(client::FlushStatus());
      for (int64_t key = 0; key < FLAGS_row_count && stop_latch->count() > 0; key++) {
        auto update = table_.NewUpdateOp();
        MakeRow(key, iteration, update->mutable_request());
        session->Apply(update);
        CHECK_OK(WaitForLastBatchAndFlush(key, &flush_future, session));
      }
      CHECK_OK(WaitForLastBatchAndFlush(kSessionBatchSize, &flush_future, session));
      if (flush_future.valid()) {
        CHECK_OK(flush_future.get().status);
      }
    }
  }
}

void UpdateScanDeltaCompactionTest::ScanRows(CountDownLatch* stop_latch) const {
  while (stop_latch->count() > 0) {
    LOG_TIMING(INFO, "Scan") {
      boost::size(client::TableRange(table_));
    }
  }
}

void UpdateScanDeltaCompactionTest::CurlWebPages(CountDownLatch* stop_latch) const {
  vector<string> urls;
  string base_url = yb::ToString(cluster_->mini_tablet_server(0)->bound_http_addr());
  urls.push_back(base_url + "/scans");
  urls.push_back(base_url + "/transactions");

  EasyCurl curl;
  faststring dst;
  while (stop_latch->count() > 0) {
    for (const string& url : urls) {
      VLOG(1) << "Curling URL " << url;
      Status status = curl.FetchURL(url, &dst);
      if (status.ok()) {
        CHECK_GT(dst.length(), 0);
      }
    }
  }
}

void UpdateScanDeltaCompactionTest::MakeRow(int64_t key,
                                            int64_t val,
                                            QLWriteRequestPB* req) const {
  QLAddInt64HashValue(req, key);
  table_.AddStringColumnValue(req, "string", "TODO random string");
  table_.AddInt64ColumnValue(req, "int64", val);
}

Status UpdateScanDeltaCompactionTest::WaitForLastBatchAndFlush(
    int64_t key, std::future<client::FlushStatus>* flush_future, shared_ptr<YBSession> session) {
  if (key % kSessionBatchSize == 0) {
    RETURN_NOT_OK(flush_future->get().status);
    *flush_future = session->FlushFuture();
  }
  return Status::OK();
}

}  // namespace tablet
}  // namespace yb
