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

#include <cmath>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "yb/util/flags.h"
#include <glog/logging.h>

#include "yb/gutil/casts.h"

#include "yb/client/callbacks.h"
#include "yb/client/client-test-util.h"
#include "yb/client/client.h"
#include "yb/client/error.h"
#include "yb/client/schema.h"
#include "yb/client/session.h"
#include "yb/client/table_handle.h"
#include "yb/client/yb_op.h"
#include "yb/client/yb_table_name.h"

#include "yb/gutil/ref_counted.h"
#include "yb/gutil/strings/split.h"
#include "yb/gutil/strings/substitute.h"

#include "yb/integration-tests/mini_cluster.h"
#include "yb/integration-tests/yb_mini_cluster_test_base.h"

#include "yb/master/mini_master.h"

#include "yb/tablet/maintenance_manager.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"

#include "yb/util/async_util.h"
#include "yb/util/countdown_latch.h"
#include "yb/util/errno.h"
#include "yb/util/random.h"
#include "yb/util/random_util.h"
#include "yb/util/status.h"
#include "yb/util/status_log.h"
#include "yb/util/stopwatch.h"
#include "yb/util/subprocess.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_util.h"
#include "yb/util/thread.h"

using namespace std::literals;

// Test size parameters
DEFINE_NON_RUNTIME_int32(concurrent_inserts, -1, "Number of inserting clients to launch");
DEFINE_NON_RUNTIME_int32(inserts_per_client, -1,
             "Number of rows inserted by each inserter client");
DEFINE_NON_RUNTIME_int32(rows_per_batch, -1, "Number of rows per client batch");

// Perf-related FLAGS_perf_stat
DEFINE_NON_RUNTIME_bool(perf_record_scan, false, "Call \"perf record --call-graph\" "
            "for the duration of the scan, disabled by default");
DEFINE_NON_RUNTIME_bool(perf_stat_scan, false, "Print \"perf stat\" results during"
            "scan to stdout, disabled by default");
DEFINE_NON_RUNTIME_bool(perf_fp_flag, false, "Only applicable with --perf_record_scan,"
            " provides argument \"fp\" to the --call-graph flag");
DECLARE_bool(enable_maintenance_manager);

using std::string;
using std::vector;
using std::shared_ptr;

namespace yb {
namespace tablet {

using client::YBClient;
using client::YBClientBuilder;
using client::YBSchema;
using client::YBSchemaBuilder;
using client::YBSession;
using client::YBTableName;
using strings::Split;
using strings::Substitute;

namespace {

const auto kSessionTimeout = 60s;

}

class FullStackInsertScanTest : public YBMiniClusterTestBase<MiniCluster> {
 protected:
  FullStackInsertScanTest()
    : // Set the default value depending on whether slow tests are allowed
    kNumInsertClients(DefaultFlag(FLAGS_concurrent_inserts, 3, 10)),
    kNumInsertsPerClient(DefaultFlag(FLAGS_inserts_per_client, 500, 50000)),
    kNumRows(kNumInsertClients*kNumInsertsPerClient),
    kFlushEveryN(DefaultFlag(FLAGS_rows_per_batch, 125, 5000)),
    random_(SeedRandom()),
    sessions_(kNumInsertClients) {
    tables_.reserve(kNumInsertClients);
  }

  const int kNumInsertClients;
  const int kNumInsertsPerClient;
  const int kNumRows;

  void SetUp() override {
    YBMiniClusterTestBase::SetUp();
  }

  void CreateTable();

  void DoTearDown() override {
    client_.reset();
    if (cluster_) {
      cluster_->Shutdown();
    }
    YBMiniClusterTestBase::DoTearDown();
  }

  void DoConcurrentClientInserts();
  void DoTestScans();
  void FlushToDisk();

 private:
  int DefaultFlag(int flag, int fast, int slow) {
    if (flag != -1) return flag;
    if (AllowSlowTests()) return slow;
    return fast;
  }

  void InitCluster() {
    // Start mini-cluster with 1 tserver, config client options
    cluster_.reset(new MiniCluster(MiniClusterOptions()));
    ASSERT_OK(cluster_->Start());
    YBClientBuilder builder;
    builder.add_master_server_addr(
        cluster_->mini_master()->bound_rpc_addr_str());
    builder.default_rpc_timeout(MonoDelta::FromSeconds(30));
    client_ = ASSERT_RESULT(builder.Build());
  }

  // Adds newly generated client's session and table pointers to arrays at id
  void CreateNewClient(int id) {
    while (tables_.size() <= implicit_cast<size_t>(id)) {
      auto table = std::make_unique<client::TableHandle>();
      ASSERT_OK(table->Open(kTableName, client_.get()));
      tables_.push_back(std::move(table));
    }
    auto session = client_->NewSession(kSessionTimeout);
    sessions_[id] = session;
  }

  // Insert the rows that are associated with that ID.
  void InsertRows(CountDownLatch* start_latch, int id, uint32_t seed);

  // Run a scan from the reader_client_ with the projection schema schema
  // and LOG_TIMING message msg.
  void ScanProjection(const vector<string>& cols, const string& msg);

  vector<string> AllColumnNames() const;

  static const YBTableName kTableName;
  const int kFlushEveryN;

  Random random_;

  YBSchema schema_;
  std::unique_ptr<YBClient> client_;
  client::TableHandle reader_table_;
  // Concurrent client insertion test variables
  vector<std::shared_ptr<YBSession> > sessions_;
  std::vector<std::unique_ptr<client::TableHandle>> tables_;
};

namespace {

std::unique_ptr<Subprocess> MakePerfStat() {
  if (!FLAGS_perf_stat_scan) return std::unique_ptr<Subprocess>();
  // No output flag for perf-stat 2.x, just print to output
  string cmd = Substitute("perf stat --pid=$0", getpid());
  LOG(INFO) << "Calling: \"" << cmd << "\"";
  return std::unique_ptr<Subprocess>(new Subprocess("perf", Split(cmd, " ")));
}

std::unique_ptr<Subprocess> MakePerfRecord() {
  if (!FLAGS_perf_record_scan) return std::unique_ptr<Subprocess>();
  string cmd = Substitute("perf record --pid=$0 --call-graph", getpid());
  if (FLAGS_perf_fp_flag) cmd += " fp";
  LOG(INFO) << "Calling: \"" << cmd << "\"";
  return std::unique_ptr<Subprocess>(new Subprocess("perf", Split(cmd, " ")));
}

void InterruptNotNull(std::unique_ptr<Subprocess> sub) {
  if (!sub) return;
  ASSERT_OK(sub->Kill(SIGINT));
  int exit_status = 0;
  ASSERT_OK(sub->Wait(&exit_status));
  if (!exit_status) {
    LOG(WARNING) << "Subprocess returned " << exit_status
                 << ": " << ErrnoToString(exit_status);
  }
}

// If key is approximately at an even multiple of 1/10 of the way between
// start and end, then a % completion update is printed to LOG(INFO)
// Assumes that end - start + 1 fits into an int
void ReportTenthDone(int64_t key, int64_t start, int64_t end,
                     int id, int numids) {
  auto done = key - start + 1;
  auto total = end - start + 1;
  if (total < 10) return;
  if (done % (total / 10) == 0) {
    auto percent = done * 100 / total;
    LOG(INFO) << "Insertion thread " << id << " of "
              << numids << " is "<< percent << "% done.";
  }
}

void ReportAllDone(int id, int numids) {
  LOG(INFO) << "Insertion thread " << id << " of  "
            << numids << " is 100% done.";
}

} // anonymous namespace

const YBTableName FullStackInsertScanTest::kTableName(
    YQL_DATABASE_CQL, "my_keyspace", "full-stack-mrs-test-tbl");

TEST_F(FullStackInsertScanTest, MRSOnlyStressTest) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_maintenance_manager) = false;
  ASSERT_NO_FATALS(CreateTable());
  ASSERT_NO_FATALS(DoConcurrentClientInserts());
  ASSERT_NO_FATALS(DoTestScans());
}

TEST_F(FullStackInsertScanTest, WithDiskStressTest) {
  ASSERT_NO_FATALS(CreateTable());
  ASSERT_NO_FATALS(DoConcurrentClientInserts());
  ASSERT_NO_FATALS(FlushToDisk());
  ASSERT_NO_FATALS(DoTestScans());
}

void FullStackInsertScanTest::DoConcurrentClientInserts() {
  vector<scoped_refptr<Thread> > threads(kNumInsertClients);
  CountDownLatch start_latch(kNumInsertClients + 1);
  for (int i = 0; i < kNumInsertClients; ++i) {
    ASSERT_NO_FATALS(CreateNewClient(i));
    ASSERT_OK(Thread::Create(CURRENT_TEST_NAME(),
                             StrCat(CURRENT_TEST_CASE_NAME(), "-id", i),
                             &FullStackInsertScanTest::InsertRows, this,
                             &start_latch, i, random_.Next(), &threads[i]));
    start_latch.CountDown();
  }
  LOG_TIMING(INFO,
             strings::Substitute("concurrent inserts ($0 rows, $1 threads)",
                                 kNumRows, kNumInsertClients)) {
    start_latch.CountDown();
    for (const scoped_refptr<Thread>& thread : threads) {
      ASSERT_OK(ThreadJoiner(thread.get()).warn_every(15s).Join());
    }
  }
}

namespace {

constexpr int kNumIntCols = 4;
constexpr int kRandomStrMinLength = 16;
constexpr int kRandomStrMaxLength = 31;

const std::vector<string>& StringColumnNames() {
  static std::vector<string> result = { "string_val" };
  return result;
}

const std::vector<string>& Int32ColumnNames() {
  static std::vector<string> result = {
      "int32_val1",
      "int32_val2",
      "int32_val3",
      "int32_val4" };
  return result;
}

const std::vector<string>& Int64ColumnNames() {
  static std::vector<string> result = {
      "int64_val1",
      "int64_val2",
      "int64_val3",
      "int64_val4" };
  return result;
}

// Fills in the fields for a row as defined by the Schema below
// name: (key,      string_val, int32_val$, int64_val$)
// type: (int64_t,  string,     int32_t x4, int64_t x4)
// The first int32 gets the id and the first int64 gets the thread
// id. The key is assigned to "key," and the other fields are random.
void RandomRow(Random* rng, QLWriteRequestPB* req, char* buf, int64_t key, int id,
               client::TableHandle* table) {
  QLAddInt64HashValue(req, key);
  int len = kRandomStrMinLength + rng->Uniform(kRandomStrMaxLength - kRandomStrMinLength + 1);
  RandomString(buf, len, rng);
  buf[len] = '\0';
  table->AddStringColumnValue(req, StringColumnNames()[0], buf);
  for (int i = 0; i < kNumIntCols; ++i) {
    table->AddInt32ColumnValue(req, Int32ColumnNames()[i], rng->Next32());
    table->AddInt64ColumnValue(req, Int64ColumnNames()[i], rng->Next64());
  }
}

} // namespace

void FullStackInsertScanTest::CreateTable() {
  ASSERT_GE(kNumInsertClients, 0);
  ASSERT_GE(kNumInsertsPerClient, 0);
  ASSERT_NO_FATALS(InitCluster());

  ASSERT_OK(client_->CreateNamespaceIfNotExists(kTableName.namespace_name(),
                                                kTableName.namespace_type()));

  YBSchemaBuilder b;
  b.AddColumn("key")->Type(DataType::INT64)->NotNull()->HashPrimaryKey();
  b.AddColumn("string_val")->Type(DataType::STRING)->NotNull();
  for (auto& col : Int32ColumnNames()) {
    b.AddColumn(col)->Type(DataType::INT32)->NotNull();
  }
  for (auto& col : Int64ColumnNames()) {
    b.AddColumn(col)->Type(DataType::INT64)->NotNull();
  }
  ASSERT_OK(reader_table_.Create(kTableName, CalcNumTablets(1), client_.get(), &b));
  schema_ = reader_table_.schema();
}

void FullStackInsertScanTest::DoTestScans() {
  LOG(INFO) << "Doing test scans on table of " << kNumRows << " rows.";

  auto stat = MakePerfStat();
  auto record = MakePerfRecord();
  if (stat) {
    CHECK_OK(stat->Start());
  }
  if (record) {
    CHECK_OK(record->Start());
  }
  ASSERT_NO_FATALS(ScanProjection(vector<string>(), "empty projection, 0 col"));
  ASSERT_NO_FATALS(ScanProjection({ "key" }, "key scan, 1 col"));
  ASSERT_NO_FATALS(ScanProjection(AllColumnNames(), "full schema scan, 10 col"));
  ASSERT_NO_FATALS(ScanProjection(StringColumnNames(), "String projection, 1 col"));
  ASSERT_NO_FATALS(ScanProjection(Int32ColumnNames(), "Int32 projection, 4 col"));
  ASSERT_NO_FATALS(ScanProjection(Int64ColumnNames(), "Int64 projection, 4 col"));

  ASSERT_NO_FATALS(InterruptNotNull(std::move(record)));
  ASSERT_NO_FATALS(InterruptNotNull(std::move(stat)));
}

void FullStackInsertScanTest::FlushToDisk() {
  for (size_t i = 0; i < cluster_->num_tablet_servers(); ++i) {
    tserver::TabletServer* ts = cluster_->mini_tablet_server(i)->server();
    ts->maintenance_manager()->Shutdown();
    auto peers = ts->tablet_manager()->GetTabletPeers();
    for (const std::shared_ptr<TabletPeer>& peer : peers) {
      Tablet* tablet = peer->tablet();
      ASSERT_OK(tablet->Flush(tablet::FlushMode::kSync));
    }
  }
}

void FullStackInsertScanTest::InsertRows(CountDownLatch* start_latch, int id,
                                         uint32_t seed) {
  Random rng(seed + id);

  start_latch->Wait();
  // Retrieve id's session and table
  std::shared_ptr<YBSession> session = sessions_[id];
  client::TableHandle* table = tables_[id].get();
  // Identify start and end of keyrange id is responsible for
  int64_t start = kNumInsertsPerClient * id;
  int64_t end = start + kNumInsertsPerClient;
  // Printed id value is in the range 1..kNumInsertClients inclusive
  ++id;
  // Prime the future as if it was running a batch (for for-loop code)
  std::promise<client::FlushStatus> promise;
  auto flush_status_future = promise.get_future();
  promise.set_value(client::FlushStatus());
  // Maintain buffer for random string generation
  char randstr[kRandomStrMaxLength + 1];
  // Insert in the id's key range
  for (int64_t key = start; key < end; ++key) {
    auto op = table->NewWriteOp(QLWriteRequestPB::QL_STMT_INSERT);
    RandomRow(&rng, op->mutable_request(), randstr, key, id, table);
    session->Apply(op);

    // Report updates or flush every so often, using the synchronizer to always
    // start filling up the next batch while previous one is sent out.
    if (key % kFlushEveryN == 0 || key == end - 1) {
      auto flush_status = flush_status_future.get();
      if (!flush_status.status.ok()) {
        LogSessionErrorsAndDie(flush_status);
      }
      flush_status_future = session->FlushFuture();
    }
    ReportTenthDone(key, start, end, id, kNumInsertClients);
  }
  auto flush_status = flush_status_future.get();
  if (!flush_status.status.ok()) {
    LogSessionErrorsAndDie(flush_status);
  }
  ReportAllDone(id, kNumInsertClients);
  FlushSessionOrDie(session);
}

void FullStackInsertScanTest::ScanProjection(const vector<string>& cols,
                                             const string& msg) {
  client::TableIteratorOptions options;
  options.columns = cols;
  size_t nrows;
  LOG_TIMING(INFO, msg) {
    nrows = boost::size(client::TableRange(reader_table_));
  }
  ASSERT_EQ(nrows, kNumRows);
}

vector<string> FullStackInsertScanTest::AllColumnNames() const {
  vector<string> ret;
  for (size_t i = 0; i < schema_.num_columns(); i++) {
    ret.push_back(schema_.Column(i).name());
  }
  return ret;
}

}  // namespace tablet
}  // namespace yb
