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

#include <cmath>
#include <cstdlib>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <memory>
#include <signal.h>
#include <string>
#include <vector>

#include "kudu/client/callbacks.h"
#include "kudu/client/client.h"
#include "kudu/client/client-test-util.h"
#include "kudu/client/row_result.h"
#include "kudu/client/write_op.h"
#include "kudu/codegen/compilation_manager.h"
#include "kudu/gutil/gscoped_ptr.h"
#include "kudu/gutil/ref_counted.h"
#include "kudu/gutil/strings/split.h"
#include "kudu/gutil/strings/strcat.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/integration-tests/mini_cluster.h"
#include "kudu/master/mini_master.h"
#include "kudu/tablet/maintenance_manager.h"
#include "kudu/tablet/tablet.h"
#include "kudu/tablet/tablet_metrics.h"
#include "kudu/tablet/tablet_peer.h"
#include "kudu/tserver/mini_tablet_server.h"
#include "kudu/tserver/tablet_server.h"
#include "kudu/tserver/ts_tablet_manager.h"
#include "kudu/util/async_util.h"
#include "kudu/util/countdown_latch.h"
#include "kudu/util/errno.h"
#include "kudu/util/stopwatch.h"
#include "kudu/util/test_macros.h"
#include "kudu/util/test_util.h"
#include "kudu/util/status.h"
#include "kudu/util/subprocess.h"
#include "kudu/util/thread.h"
#include "kudu/util/random.h"
#include "kudu/util/random_util.h"

// Test size parameters
DEFINE_int32(concurrent_inserts, -1, "Number of inserting clients to launch");
DEFINE_int32(inserts_per_client, -1,
             "Number of rows inserted by each inserter client");
DEFINE_int32(rows_per_batch, -1, "Number of rows per client batch");

// Perf-related FLAGS_perf_stat
DEFINE_bool(perf_record_scan, false, "Call \"perf record --call-graph\" "
            "for the duration of the scan, disabled by default");
DEFINE_bool(perf_stat_scan, false, "Print \"perf stat\" results during"
            "scan to stdout, disabled by default");
DEFINE_bool(perf_fp_flag, false, "Only applicable with --perf_record_scan,"
            " provides argument \"fp\" to the --call-graph flag");
DECLARE_bool(enable_maintenance_manager);

using std::string;
using std::vector;

namespace kudu {
namespace tablet {

using client::KuduClient;
using client::KuduClientBuilder;
using client::KuduColumnSchema;
using client::KuduInsert;
using client::KuduRowResult;
using client::KuduScanner;
using client::KuduSchema;
using client::KuduSchemaBuilder;
using client::KuduSession;
using client::KuduStatusMemberCallback;
using client::KuduTable;
using client::KuduTableCreator;
using strings::Split;
using strings::Substitute;

class FullStackInsertScanTest : public KuduTest {
 protected:
  FullStackInsertScanTest()
    : // Set the default value depending on whether slow tests are allowed
    kNumInsertClients(DefaultFlag(FLAGS_concurrent_inserts, 3, 10)),
    kNumInsertsPerClient(DefaultFlag(FLAGS_inserts_per_client, 500, 50000)),
    kNumRows(kNumInsertClients * kNumInsertsPerClient),
    kFlushEveryN(DefaultFlag(FLAGS_rows_per_batch, 125, 5000)),
    random_(SeedRandom()),
    sessions_(kNumInsertClients),
    tables_(kNumInsertClients) {

    // schema has kNumIntCols contiguous columns of Int32 and Int64, in order.
    KuduSchemaBuilder b;
    b.AddColumn("key")->Type(KuduColumnSchema::INT64)->NotNull()->PrimaryKey();
    b.AddColumn("string_val")->Type(KuduColumnSchema::STRING)->NotNull();
    b.AddColumn("int32_val1")->Type(KuduColumnSchema::INT32)->NotNull();
    b.AddColumn("int32_val2")->Type(KuduColumnSchema::INT32)->NotNull();
    b.AddColumn("int32_val3")->Type(KuduColumnSchema::INT32)->NotNull();
    b.AddColumn("int32_val4")->Type(KuduColumnSchema::INT32)->NotNull();
    b.AddColumn("int64_val1")->Type(KuduColumnSchema::INT64)->NotNull();
    b.AddColumn("int64_val2")->Type(KuduColumnSchema::INT64)->NotNull();
    b.AddColumn("int64_val3")->Type(KuduColumnSchema::INT64)->NotNull();
    b.AddColumn("int64_val4")->Type(KuduColumnSchema::INT64)->NotNull();
    CHECK_OK(b.Build(&schema_));
  }

  const int kNumInsertClients;
  const int kNumInsertsPerClient;
  const int kNumRows;

  virtual void SetUp() OVERRIDE {
    KuduTest::SetUp();
  }

  void CreateTable() {
    ASSERT_GE(kNumInsertClients, 0);
    ASSERT_GE(kNumInsertsPerClient, 0);
    NO_FATALS(InitCluster());
    gscoped_ptr<KuduTableCreator> table_creator(client_->NewTableCreator());
    ASSERT_OK(table_creator->table_name(kTableName)
             .schema(&schema_)
             .num_replicas(1)
             .Create());
    ASSERT_OK(client_->OpenTable(kTableName, &reader_table_));
  }

  virtual void TearDown() OVERRIDE {
    if (cluster_) {
      cluster_->Shutdown();
    }
    KuduTest::TearDown();
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

  // Generate random row according to schema_.
  static void RandomRow(Random* rng, KuduPartialRow* row,
                        char* buf, int64_t key, int id);

  void InitCluster() {
    // Start mini-cluster with 1 tserver, config client options
    cluster_.reset(new MiniCluster(env_.get(), MiniClusterOptions()));
    ASSERT_OK(cluster_->Start());
    KuduClientBuilder builder;
    builder.add_master_server_addr(
        cluster_->mini_master()->bound_rpc_addr_str());
    builder.default_rpc_timeout(MonoDelta::FromSeconds(30));
    ASSERT_OK(builder.Build(&client_));
  }

  // Adds newly generated client's session and table pointers to arrays at id
  void CreateNewClient(int id) {
    ASSERT_OK(client_->OpenTable(kTableName, &tables_[id]));
    client::sp::shared_ptr<KuduSession> session = client_->NewSession();
    session->SetTimeoutMillis(kSessionTimeoutMs);
    ASSERT_OK(session->SetFlushMode(KuduSession::MANUAL_FLUSH));
    sessions_[id] = session;
  }

  // Insert the rows that are associated with that ID.
  void InsertRows(CountDownLatch* start_latch, int id, uint32_t seed);

  // Run a scan from the reader_client_ with the projection schema schema
  // and LOG_TIMING message msg.
  void ScanProjection(const vector<string>& cols, const string& msg);

  vector<string> AllColumnNames() const;
  vector<string> StringColumnNames() const;
  vector<string> Int32ColumnNames() const;
  vector<string> Int64ColumnNames() const;

  static const char* const kTableName;
  static const int kSessionTimeoutMs = 60000;
  static const int kRandomStrMinLength = 16;
  static const int kRandomStrMaxLength = 31;
  static const int kNumIntCols = 4;
  enum {
    kKeyCol,
    kStrCol,
    kInt32ColBase,
    kInt64ColBase = kInt32ColBase + kNumIntCols
  };
  const int kFlushEveryN;

  Random random_;

  KuduSchema schema_;
  std::shared_ptr<MiniCluster> cluster_;
  client::sp::shared_ptr<KuduClient> client_;
  client::sp::shared_ptr<KuduTable> reader_table_;
  // Concurrent client insertion test variables
  vector<client::sp::shared_ptr<KuduSession> > sessions_;
  vector<client::sp::shared_ptr<KuduTable> > tables_;
};

namespace {

gscoped_ptr<Subprocess> MakePerfStat() {
  if (!FLAGS_perf_stat_scan) return gscoped_ptr<Subprocess>();
  // No output flag for perf-stat 2.x, just print to output
  string cmd = Substitute("perf stat --pid=$0", getpid());
  LOG(INFO) << "Calling: \"" << cmd << "\"";
  return gscoped_ptr<Subprocess>(new Subprocess("perf", Split(cmd, " ")));
}

gscoped_ptr<Subprocess> MakePerfRecord() {
  if (!FLAGS_perf_record_scan) return gscoped_ptr<Subprocess>();
  string cmd = Substitute("perf record --pid=$0 --call-graph", getpid());
  if (FLAGS_perf_fp_flag) cmd += " fp";
  LOG(INFO) << "Calling: \"" << cmd << "\"";
  return gscoped_ptr<Subprocess>(new Subprocess("perf", Split(cmd, " ")));
}

void InterruptNotNull(gscoped_ptr<Subprocess> sub) {
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
  int done = key - start + 1;
  int total = end - start + 1;
  if (total < 10) return;
  if (done % (total / 10) == 0) {
    int percent = done * 100 / total;
    LOG(INFO) << "Insertion thread " << id << " of "
              << numids << " is "<< percent << "% done.";
  }
}

void ReportAllDone(int id, int numids) {
  LOG(INFO) << "Insertion thread " << id << " of  "
            << numids << " is 100% done.";
}

} // anonymous namespace

const char* const FullStackInsertScanTest::kTableName = "full-stack-mrs-test-tbl";

TEST_F(FullStackInsertScanTest, MRSOnlyStressTest) {
  FLAGS_enable_maintenance_manager = false;
  NO_FATALS(CreateTable());
  NO_FATALS(DoConcurrentClientInserts());
  NO_FATALS(DoTestScans());
}

TEST_F(FullStackInsertScanTest, WithDiskStressTest) {
  NO_FATALS(CreateTable());
  NO_FATALS(DoConcurrentClientInserts());
  NO_FATALS(FlushToDisk());
  NO_FATALS(DoTestScans());
}

void FullStackInsertScanTest::DoConcurrentClientInserts() {
  vector<scoped_refptr<Thread> > threads(kNumInsertClients);
  CountDownLatch start_latch(kNumInsertClients + 1);
  for (int i = 0; i < kNumInsertClients; ++i) {
    NO_FATALS(CreateNewClient(i));
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
      ASSERT_OK(ThreadJoiner(thread.get())
                .warn_every_ms(15000)
                .Join());
    }
  }
}

void FullStackInsertScanTest::DoTestScans() {
  LOG(INFO) << "Doing test scans on table of " << kNumRows << " rows.";

  gscoped_ptr<Subprocess> stat = MakePerfStat();
  gscoped_ptr<Subprocess> record = MakePerfRecord();
  if (stat) stat->Start();
  if (record) record->Start();

  NO_FATALS(ScanProjection(vector<string>(), "empty projection, 0 col"));
  NO_FATALS(ScanProjection({ "key" }, "key scan, 1 col"));
  NO_FATALS(ScanProjection(AllColumnNames(), "full schema scan, 10 col"));
  NO_FATALS(ScanProjection(StringColumnNames(), "String projection, 1 col"));
  NO_FATALS(ScanProjection(Int32ColumnNames(), "Int32 projection, 4 col"));
  NO_FATALS(ScanProjection(Int64ColumnNames(), "Int64 projection, 4 col"));

  NO_FATALS(InterruptNotNull(record.Pass()));
  NO_FATALS(InterruptNotNull(stat.Pass()));
}

void FullStackInsertScanTest::FlushToDisk() {
  for (int i = 0; i < cluster_->num_tablet_servers(); ++i) {
    tserver::TabletServer* ts = cluster_->mini_tablet_server(i)->server();
    ts->maintenance_manager()->Shutdown();
    tserver::TSTabletManager* tm = ts->tablet_manager();
    vector<scoped_refptr<TabletPeer> > peers;
    tm->GetTabletPeers(&peers);
    for (const scoped_refptr<TabletPeer>& peer : peers) {
      Tablet* tablet = peer->tablet();
      if (!tablet->MemRowSetEmpty()) {
        ASSERT_OK(tablet->Flush());
      }
      ASSERT_OK(tablet->Compact(Tablet::FORCE_COMPACT_ALL));
    }
  }
}

void FullStackInsertScanTest::InsertRows(CountDownLatch* start_latch, int id,
                                         uint32_t seed) {
  Random rng(seed + id);

  start_latch->Wait();
  // Retrieve id's session and table
  client::sp::shared_ptr<KuduSession> session = sessions_[id];
  client::sp::shared_ptr<KuduTable> table = tables_[id];
  // Identify start and end of keyrange id is responsible for
  int64_t start = kNumInsertsPerClient * id;
  int64_t end = start + kNumInsertsPerClient;
  // Printed id value is in the range 1..kNumInsertClients inclusive
  ++id;
  // Use synchronizer to keep 1 asynchronous batch flush maximum
  Synchronizer sync;
  KuduStatusMemberCallback<Synchronizer> cb(&sync, &Synchronizer::StatusCB);
  // Prime the synchronizer as if it was running a batch (for for-loop code)
  cb.Run(Status::OK());
  // Maintain buffer for random string generation
  char randstr[kRandomStrMaxLength + 1];
  // Insert in the id's key range
  for (int64_t key = start; key < end; ++key) {
    gscoped_ptr<KuduInsert> insert(table->NewInsert());
    RandomRow(&rng, insert->mutable_row(), randstr, key, id);
    CHECK_OK(session->Apply(insert.release()));

    // Report updates or flush every so often, using the synchronizer to always
    // start filling up the next batch while previous one is sent out.
    if (key % kFlushEveryN == 0) {
      Status s = sync.Wait();
      if (!s.ok()) {
        LogSessionErrorsAndDie(session, s);
      }
      sync.Reset();
      session->FlushAsync(&cb);
    }
    ReportTenthDone(key, start, end, id, kNumInsertClients);
  }
  ReportAllDone(id, kNumInsertClients);
  Status s = sync.Wait();
  if (!s.ok()) {
    LogSessionErrorsAndDie(session, s);
  }
  FlushSessionOrDie(session);
}

void FullStackInsertScanTest::ScanProjection(const vector<string>& cols,
                                             const string& msg) {
  {
    // Warmup codegen cache
    KuduScanner scanner(reader_table_.get());
    ASSERT_OK(scanner.SetProjectedColumns(cols));
    ASSERT_OK(scanner.Open());
    codegen::CompilationManager::GetSingleton()->Wait();
  }
  KuduScanner scanner(reader_table_.get());
  ASSERT_OK(scanner.SetProjectedColumns(cols));
  uint64_t nrows = 0;
  LOG_TIMING(INFO, msg) {
    ASSERT_OK(scanner.Open());
    vector<KuduRowResult> rows;
    while (scanner.HasMoreRows()) {
      ASSERT_OK(scanner.NextBatch(&rows));
      nrows += rows.size();
    }
  }
  ASSERT_EQ(nrows, kNumRows);
}

// Fills in the fields for a row as defined by the Schema below
// name: (key,      string_val, int32_val$, int64_val$)
// type: (int64_t,  string,     int32_t x4, int64_t x4)
// The first int32 gets the id and the first int64 gets the thread
// id. The key is assigned to "key," and the other fields are random.
void FullStackInsertScanTest::RandomRow(Random* rng, KuduPartialRow* row, char* buf,
                                        int64_t key, int id) {
  CHECK_OK(row->SetInt64(kKeyCol, key));
  int len = kRandomStrMinLength +
    rng->Uniform(kRandomStrMaxLength - kRandomStrMinLength + 1);
  RandomString(buf, len, rng);
  buf[len] = '\0';
  CHECK_OK(row->SetStringCopy(kStrCol, buf));
  CHECK_OK(row->SetInt32(kInt32ColBase, id));
  CHECK_OK(row->SetInt64(kInt64ColBase, Thread::current_thread()->tid()));
  for (int i = 1; i < kNumIntCols; ++i) {
    CHECK_OK(row->SetInt32(kInt32ColBase + i, rng->Next32()));
    CHECK_OK(row->SetInt64(kInt64ColBase + i, rng->Next64()));
  }
}

vector<string> FullStackInsertScanTest::AllColumnNames() const {
  vector<string> ret;
  for (int i = 0; i < schema_.num_columns(); i++) {
    ret.push_back(schema_.Column(i).name());
  }
  return ret;
}

vector<string> FullStackInsertScanTest::StringColumnNames() const {
  return { "string_val" };
}

vector<string> FullStackInsertScanTest::Int32ColumnNames() const {
  return { "int32_val1",
           "int32_val2",
           "int32_val3",
           "int32_val4" };
}

vector<string> FullStackInsertScanTest::Int64ColumnNames() const {
  return { "int64_val1",
           "int64_val2",
           "int64_val3",
           "int64_val4" };
}

} // namespace tablet
} // namespace kudu
