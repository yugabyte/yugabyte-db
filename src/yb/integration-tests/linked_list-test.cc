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
// This is an integration test similar to TestLoadAndVerify in HBase.
// It creates a table and writes linked lists into it, where each row
// points to the previously written row. For example, a sequence of inserts
// may be:
//
//  rand_key   | link_to   |  insert_ts
//   12345          0           1
//   823          12345         2
//   9999          823          3
// (each insert links to the key of the previous insert)
//
// During insertion, a configurable number of parallel chains may be inserted.
// To verify, the table is scanned, and we ensure that every key is linked to
// either zero or one times, and no link_to refers to a missing key.

#include <functional>
#include <set>

#include "yb/util/logging.h"
#include <gtest/gtest.h>

#include "yb/client/client-test-util.h"
#include "yb/client/client.h"
#include "yb/client/schema.h"
#include "yb/client/session.h"
#include "yb/client/table.h"
#include "yb/client/table_creator.h"
#include "yb/client/tablet_server.h"
#include "yb/client/yb_op.h"

#include "yb/common/ql_value.h"

#include "yb/gutil/map-util.h"
#include "yb/gutil/stl_util.h"
#include "yb/gutil/strings/split.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/gutil/walltime.h"

#include "yb/integration-tests/external_mini_cluster.h"
#include "yb/integration-tests/ts_itest-base.h"

#include "yb/server/hybrid_clock.h"

#include "yb/util/blocking_queue.h"
#include "yb/util/curl_util.h"
#include "yb/util/hdr_histogram.h"
#include "yb/util/random.h"
#include "yb/util/status_log.h"
#include "yb/util/stopwatch.h"
#include "yb/util/test_util.h"
#include "yb/util/thread.h"
#include "yb/util/flags.h"

using namespace std::literals;

DEFINE_UNKNOWN_int32(seconds_to_run, 5, "Number of seconds for which to run the test");

DEFINE_UNKNOWN_int32(num_chains, 50, "Number of parallel chains to generate");
DEFINE_UNKNOWN_int32(num_tablets, 3, "Number of tablets over which to split the data");
DEFINE_UNKNOWN_bool(enable_mutation, true, "Enable periodic mutation of inserted rows");
DEFINE_UNKNOWN_int32(num_snapshots, 3,
    "Number of snapshots to verify across replicas and reboots.");

DEFINE_UNKNOWN_bool(stress_flush_compact, false,
            "Flush and compact way more aggressively to try to find bugs");
DEFINE_UNKNOWN_bool(stress_wal_gc, false,
            "Set WAL segment size small so that logs will be GCed during the test");
DECLARE_int32(replication_factor);
DECLARE_string(ts_flags);

namespace yb {

using yb::client::YBClient;
using yb::client::YBClientBuilder;
using yb::client::YBSchema;
using yb::client::YBTableName;
using std::shared_ptr;
using std::string;
using std::vector;
using std::pair;
using std::set;
using yb::itest::TServerDetails;
using yb::itest::MustBeCommitted;

using namespace std::placeholders;

namespace {

// Vector of snapshot hybrid_time, count pairs.
typedef std::vector<std::pair<HybridTime, int64_t>> SnapsAndCounts;

static const char* const kKeyColumnName = "rand_key";
static const char* const kLinkColumnName = "link_to";
static const char* const kInsertTsColumnName = "insert_ts";
static const char* const kUpdatedColumnName = "updated";
static const int64_t kNoParticularCountExpected = -1;

// Provides methods for writing data and reading it back in such a way that
// facilitates checking for data integrity.
class LinkedListTester {
 public:
  LinkedListTester(client::YBClient* client,
                   client::YBTableName table_name, uint8_t num_chains, int num_tablets,
                   int num_replicas, bool enable_mutation)
      : verify_projection_({kKeyColumnName, kLinkColumnName, kUpdatedColumnName}),
        table_name_(std::move(table_name)),
        num_chains_(num_chains),
        num_tablets_(num_tablets),
        num_replicas_(num_replicas),
        enable_mutation_(enable_mutation),
        latency_histogram_(1000000, 3),
        client_(client) {
    client::YBSchemaBuilder b;

    b.AddColumn(kKeyColumnName)->Type(INT64)->NotNull()->HashPrimaryKey();
    b.AddColumn(kLinkColumnName)->Type(INT64)->NotNull();
    b.AddColumn(kInsertTsColumnName)->Type(INT64)->NotNull();
    b.AddColumn(kUpdatedColumnName)->Type(BOOL)->NotNull();
    CHECK_OK(b.Build(&schema_));
  }

  // Create the table.
  Status CreateLinkedListTable();

  // Load the table with the linked list test pattern.
  //
  // Runs for the amount of time designated by 'run_for'.
  // Sets *written_count to the number of rows inserted.
  Status LoadLinkedList(
      const MonoDelta& run_for,
      int num_samples,
      int64_t* written_count);

  // Variant of VerifyLinkedListRemote that verifies at the specified snapshot hybrid_time.
  Status VerifyLinkedListAtSnapshotRemote(
      HybridTime snapshot_hybrid_time, const int64_t expected, const bool log_errors,
      const bool latest_at_leader, const std::function<Status(const std::string&)>& cb,
      int64_t* verified_count) {
    LOG(INFO) << __func__ << ": snapshot_hybrid_time=" << snapshot_hybrid_time
              << ", latest_at_leader=" << latest_at_leader
              << ", expected=" << expected
              << ", log_errors=" << log_errors;
    return VerifyLinkedListRemote(
        snapshot_hybrid_time,
        expected,
        log_errors,
        latest_at_leader,
        cb,
        verified_count);
  }

  // Variant of VerifyLinkedListRemote that verifies without specifying a snapshot hybrid_time.
  Status VerifyLinkedListNoSnapshotRemote(const int64_t expected,
                                          const bool log_errors,
                                          const bool latest_at_leader,
                                          int64_t* verified_count) {
    LOG(INFO) << __func__ << ": expected=" << expected
              << ", log_errors=" << log_errors
              << ", latest_at_leader=" << latest_at_leader;
    return VerifyLinkedListRemote(
        HybridTime::kInvalid, expected, log_errors, latest_at_leader,
        std::bind(&LinkedListTester::ReturnOk, this, _1), verified_count);
  }

  // Run the verify step on a table with RPCs. Calls the provided callback 'cb' once during
  // verification to test scanner fault tolerance.
  Status VerifyLinkedListRemote(
      HybridTime snapshot_hybrid_time, const int64_t expected, const bool log_errors,
      bool latest_at_leader, const std::function<Status(const std::string&)>& cb,
      int64_t* verified_count);

  // A variant of VerifyLinkedListRemote that is more robust towards ongoing
  // bootstrapping and replication.
  Status WaitAndVerify(const int seconds_to_run,
                       const int64_t expected,
                       const bool latest_at_leader) {
    LOG(INFO) << __func__ << ": seconds_to_run=" << seconds_to_run
              << ", expected=" << expected
              << ", latest_at_leader=" << latest_at_leader;
    return WaitAndVerify(seconds_to_run, expected, latest_at_leader,
        std::bind(&LinkedListTester::ReturnOk, this, _1));
  }

  // A variant of WaitAndVerify that also takes a callback to be run once during verification.
  Status WaitAndVerify(
      int seconds_to_run, int64_t expected, bool latest_at_leader,
      const std::function<Status(const std::string&)>& cb);

  // Generate a vector of ints which form the split keys.
  std::vector<int64_t> GenerateSplitInts();

  void DumpInsertHistogram(bool print_flags);

 protected:
  client::YBSchema schema_;
  const std::vector<std::string> verify_projection_;
  const client::YBTableName table_name_;
  const uint8_t num_chains_;
  const int num_tablets_;
  const int num_replicas_;
  const bool enable_mutation_;
  HdrHistogram latency_histogram_;
  client::YBClient* const client_;
  SnapsAndCounts sampled_hybrid_times_and_counts_;

 private:
  Status ReturnOk(const std::string& str) { return Status::OK(); }
};

} // namespace

class LinkedListTest : public tserver::TabletServerIntegrationTestBase {
 public:
  LinkedListTest() {}

  void SetUp() override {
    TabletServerIntegrationTestBase::SetUp();

    LOG(INFO) << "Linked List Test Configuration:";
    LOG(INFO) << "--------------";
    LOG(INFO) << FLAGS_num_chains << " chains";
    LOG(INFO) << FLAGS_num_tablets << " tablets";
    LOG(INFO) << "Mutations " << (FLAGS_enable_mutation ? "on" : "off");
    LOG(INFO) << "--------------";
  }

  void BuildAndStart() {
    vector<string> common_flags;

    common_flags.push_back("--skip_remove_old_recovery_dir");

    vector<string> ts_flags(common_flags);
    if (FLAGS_stress_wal_gc) {
      // Set the size of the WAL segments low so that some can be GC'd.
      ts_flags.push_back("--log_segment_size_mb=1");
    }

    CreateCluster("linked-list-cluster", ts_flags, common_flags);
    ResetClientAndTester();
    ASSERT_OK(tester_->CreateLinkedListTable());
    WaitForTSAndReplicas();
  }

  void ResetClientAndTester() {
    client_ = ASSERT_RESULT(cluster_->CreateClient());
    tester_.reset(new LinkedListTester(client_.get(),
                                       kTableName,
                                       FLAGS_num_chains,
                                       FLAGS_num_tablets,
                                       FLAGS_replication_factor,
                                       FLAGS_enable_mutation));
  }

  void RestartCluster() {
    CHECK(cluster_);
    cluster_->Shutdown(ExternalMiniCluster::TS_ONLY);
    CHECK_OK(cluster_->Restart());
    ResetClientAndTester();
  }

 protected:
  void AddExtraFlags(const string& flags_str, vector<string>* flags) {
    if (flags_str.empty()) {
      return;
    }
    vector<string> split_flags = strings::Split(flags_str, " ");
    for (const string& flag : split_flags) {
      flags->push_back(flag);
    }
  }

  std::unique_ptr<YBClient> client_;
  std::unique_ptr<LinkedListTester> tester_;
};

// Generates the linked list pattern.
// Since we can insert multiple chain in parallel, this encapsulates the
// state for each chain.
class LinkedListChainGenerator {
 public:
  // 'chain_idx' is a unique ID for this chain. Chains with different indexes
  // will always generate distinct sets of keys (thus avoiding the possibility of
  // a collision even in a longer run).
  explicit LinkedListChainGenerator(uint8_t chain_idx)
    : chain_idx_(chain_idx),
      rand_(chain_idx * 0xDEADBEEF),
      prev_key_(0) {
    CHECK_LT(chain_idx, 256);
  }

  ~LinkedListChainGenerator() {
  }

  // Generate a random 64-bit unsigned int.
  uint64_t Rand64() {
    return (implicit_cast<uint64_t>(rand_.Next()) << 32) | rand_.Next();
  }

  Status GenerateNextInsert(const client::TableHandle& table, client::YBSession* session) {
    // Encode the chain index in the lowest 8 bits so that different chains never
    // intersect.
    int64_t this_key = (Rand64() << 8) | chain_idx_;
    int64_t ts = GetCurrentTimeMicros();

    auto insert = table.NewInsertOp();
    auto req = insert->mutable_request();
    QLAddInt64HashValue(req, this_key);
    table.AddInt64ColumnValue(req, kInsertTsColumnName, ts);
    table.AddInt64ColumnValue(req, kLinkColumnName, prev_key_);
    session->Apply(insert);
    prev_key_ = this_key;
    return Status::OK();
  }

  int64_t prev_key() const {
    return prev_key_;
  }

 private:
  const uint8_t chain_idx_;

  // This is a linear congruential random number generator, so it won't repeat until
  // it has exhausted its period (which is quite large)
  Random rand_;

  // The previously output key.
  int64_t prev_key_;

  DISALLOW_COPY_AND_ASSIGN(LinkedListChainGenerator);
};

// A thread that updates the hybrid_times of rows whose keys are put in its BlockingQueue.
class ScopedRowUpdater {
 public:
  // Create and start a new ScopedUpdater. 'table' must remain valid for
  // the lifetime of this object.
  explicit ScopedRowUpdater(const client::TableHandle& table)
      : table_(table), to_update_(kint64max) {  // no limit
    EXPECT_OK(Thread::Create("linked_list-test", "updater",
                             &ScopedRowUpdater::RowUpdaterThread, this, &updater_));
  }

  ~ScopedRowUpdater() {
    to_update_.Shutdown();
    if (updater_) {
      updater_->Join();
    }
  }

  BlockingQueue<int64_t>* to_update() { return &to_update_; }

 private:
  void RowUpdaterThread() {
    std::shared_ptr<client::YBSession> session(table_.client()->NewSession());
    session->SetTimeout(15s);

    int64_t next_key;
    std::vector<client::YBqlOpPtr> ops;
    while (to_update_.BlockingGet(&next_key)) {
      auto update = table_.NewUpdateOp();
      auto req = update->mutable_request();
      QLAddInt64HashValue(req, next_key);
      table_.AddBoolColumnValue(req, kUpdatedColumnName, true);
      ops.push_back(update);
      session->Apply(update);
      if (ops.size() >= 50) {
        FlushSessionOrDie(session, ops);
        ops.clear();
      }
    }

    FlushSessionOrDie(session, ops);
  }

  const client::TableHandle& table_;
  BlockingQueue<int64_t> to_update_;
  scoped_refptr<Thread> updater_;
};

// A thread that periodically checks tablet and master web pages during the
// linked list test.
class PeriodicWebUIChecker {
 public:
  PeriodicWebUIChecker(const ExternalMiniCluster& cluster,
                       const std::string& tablet_id, MonoDelta period)
      : period_(std::move(period)), is_running_(true) {
    // List of master and ts web pages to fetch
    vector<std::string> master_pages, ts_pages;

    master_pages.push_back("/metrics");
    master_pages.push_back("/masters");
    master_pages.push_back("/tables");
    master_pages.push_back("/dump-entities");
    master_pages.push_back("/tablet-servers");

    ts_pages.push_back("/metrics");
    ts_pages.push_back("/tablets");
    ts_pages.push_back(strings::Substitute("/transactions?tablet_id=$0", tablet_id));

    // Generate list of urls for each master and tablet server
    for (size_t i = 0; i < cluster.num_masters(); i++) {
      for (std::string page : master_pages) {
        urls_.push_back(strings::Substitute(
            "http://$0$1",
            cluster.master(i)->bound_http_hostport().ToString(),
            page));
      }
    }
    for (size_t i = 0; i < cluster.num_tablet_servers(); i++) {
      for (std::string page : ts_pages) {
        urls_.push_back(strings::Substitute(
            "http://$0$1",
            cluster.tablet_server(i)->bound_http_hostport().ToString(),
            page));
      }
    }
    CHECK_OK(Thread::Create("linked_list-test", "checker",
                            &PeriodicWebUIChecker::CheckThread, this, &checker_));
  }

  ~PeriodicWebUIChecker() {
    LOG(INFO) << "Shutting down curl thread";
    is_running_.Store(false);
    if (checker_) {
      checker_->Join();
    }
  }

 private:
  void CheckThread() {
    EasyCurl curl;
    faststring dst;
    LOG(INFO) << "Curl thread will poll the following URLs every " << period_.ToMilliseconds()
        << " ms: ";
    for (std::string url : urls_) {
      LOG(INFO) << url;
    }
    for (int count = 0; is_running_.Load(); count++) {
      const std::string &url = urls_[count % urls_.size()];
      LOG(INFO) << "Curling URL " << url;
      const MonoTime start = MonoTime::Now();
      Status status = curl.FetchURL(url, &dst);
      if (status.ok()) {
        CHECK_GT(dst.length(), 0);
      }
      // Sleep until the next period
      const MonoTime end = MonoTime::Now();
      const MonoDelta elapsed = end.GetDeltaSince(start);
      const int64_t sleep_ns = period_.ToNanoseconds() - elapsed.ToNanoseconds();
      if (sleep_ns > 0) {
        SleepFor(MonoDelta::FromNanoseconds(sleep_ns));
      }
    }
  }

  const MonoDelta period_;
  AtomicBool is_running_;
  scoped_refptr<Thread> checker_;
  vector<std::string> urls_;
};

// Helper class to hold results from a linked list scan and perform the
// verification step on the data.
class LinkedListVerifier {
 public:
  LinkedListVerifier(uint8_t num_chains, bool enable_mutation, int64_t expected,
                     std::vector<int64_t> split_key_ints);

  // Start the scan timer. The duration between starting the scan and verifying
  // the data is logged in the VerifyData() step, so this should be called
  // immediately before starting the table(t) scan.
  void StartScanTimer();

  // Register a new row result during the verify step.
  void RegisterResult(int64_t key, int64_t link, bool updated);

  // Run the common verify step once the scanned data is stored.
  Status VerifyData(int64_t* verified_count, bool log_errors);

 private:
  // Print a summary of the broken links to the log.
  void SummarizeBrokenLinks(const std::vector<int64_t>& broken_links);

  const uint8_t num_chains_;
  const int64_t expected_;
  const bool enable_mutation_;
  const std::vector<int64_t> split_key_ints_;
  std::vector<int64_t> seen_key_;
  std::vector<int64_t> seen_link_to_;
  int errors_;
  Stopwatch scan_timer_;
};

/////////////////////////////////////////////////////////////
// LinkedListTester
/////////////////////////////////////////////////////////////

std::vector<int64_t> LinkedListTester::GenerateSplitInts() {
  vector<int64_t> ret;
  ret.reserve(num_tablets_ - 1);
  int64_t increment = kint64max / num_tablets_;
  for (int64_t i = 1; i < num_tablets_; i++) {
    ret.push_back(i * increment);
  }
  return ret;
}

Status LinkedListTester::CreateLinkedListTable() {
  RETURN_NOT_OK(client_->CreateNamespaceIfNotExists(table_name_.namespace_name(),
                                                    table_name_.namespace_type()));

  std::unique_ptr<client::YBTableCreator> table_creator(client_->NewTableCreator());
  RETURN_NOT_OK_PREPEND(table_creator->table_name(table_name_)
                        .schema(&schema_)
                        .num_tablets(CalcNumTablets(num_replicas_))
                        .table_type(client::YBTableType::YQL_TABLE_TYPE)
                        .Create(),
                        "Failed to create table");
  return Status::OK();
}

Status LinkedListTester::LoadLinkedList(
    const MonoDelta& run_for,
    int num_samples,
    int64_t *written_count) {

  sampled_hybrid_times_and_counts_.clear();
  client::TableHandle table;
  RETURN_NOT_OK_PREPEND(table.Open(table_name_, client_),
                        "Could not open table " + table_name_.ToString());

  // Instantiate a hybrid clock so that we can collect hybrid_times since we're running the
  // tablet servers in an external mini cluster.
  // TODO when they become available (KUDU-420), use client-propagated hybrid_times
  // instead of reading from the clock directly. This will allow to run this test
  // against a "real" cluster and not force the client to be synchronized.
  scoped_refptr<server::Clock> ht_clock(new server::HybridClock());
  RETURN_NOT_OK(ht_clock->Init());

  auto start = CoarseMonoClock::Now();
  auto deadline = start + run_for.ToSteadyDuration();

  std::shared_ptr<client::YBSession> session = client_->NewSession();
  session->SetTimeout(30s);

  ScopedRowUpdater updater(table);
  std::vector<std::unique_ptr<LinkedListChainGenerator>> chains;
  for (uint8_t i = 0; i < num_chains_; i++) {
    chains.push_back(std::make_unique<LinkedListChainGenerator>(i));
  }

  auto sample_interval = run_for.ToSteadyDuration() / num_samples;
  auto next_sample = start + sample_interval;
  LOG(INFO) << "Running for: " << run_for.ToString();
  LOG(INFO) << "Sampling every " << ToMicroseconds(sample_interval) << " us";

  *written_count = 0;
  int iter = 0;
  while (true) {
    if (iter++ % 10000 == 0) {
      LOG(INFO) << "Written " << (*written_count) << " rows in chain";
      DumpInsertHistogram(false);
    }

    auto now = CoarseMonoClock::Now();
    if (deadline < now) {
      LOG(INFO) << "Finished inserting list. Added " << (*written_count) << " in chain";
      LOG(INFO) << "Last entries inserted had keys:";
      for (uint8_t i = 0; i < num_chains_; i++) {
        LOG(INFO) << i << ": " << chains[i]->prev_key();
      }
      return Status::OK();
    }
    // We cannot store snapshot on the last step, because his hybrid time could be in future,
    // related to max time that could be served by one alive server.
    // So we ensure that we always write some data after snapshot, to be sure that server would
    // have at least time of snapshot as read time for follower.
    if (next_sample < now) {
      HybridTime now = ht_clock->Now();
      sampled_hybrid_times_and_counts_.emplace_back(now, *written_count);
      next_sample += sample_interval;
      LOG(INFO) << "Sample at HT hybrid_time: " << now.ToString()
                << " Inserted count: " << *written_count;
    }
    for (const auto& chain : chains) {
      RETURN_NOT_OK_PREPEND(chain->GenerateNextInsert(table, session.get()),
                            "Unable to generate next insert into linked list chain");
    }

    MonoTime flush_start(MonoTime::Now());
    FlushSessionOrDie(session);
    MonoDelta elapsed = MonoTime::Now().GetDeltaSince(flush_start);
    latency_histogram_.Increment(elapsed.ToMicroseconds());

    (*written_count) += chains.size();

    if (enable_mutation_) {
      // Rows have been inserted; they're now safe to update.
      for (const auto& chain : chains) {
        updater.to_update()->Put(chain->prev_key());
      }
    }
  }
}

void LinkedListTester::DumpInsertHistogram(bool print_flags) {
  // We dump to cout instead of using glog so the output isn't prefixed with
  // line numbers. This makes it less ugly to copy-paste into JIRA, etc.
  using std::cout;
  using std::endl;

  const HdrHistogram* h = &latency_histogram_;

  cout << "------------------------------------------------------------" << endl;
  cout << "Histogram for latency of insert operations (microseconds)" << endl;
  if (print_flags) {
    cout << "Flags: " << google::CommandlineFlagsIntoString() << endl;
  }
  cout << "Note: each insert is a batch of " << num_chains_ << " rows." << endl;
  cout << "------------------------------------------------------------" << endl;
  cout << "Count: " << h->TotalCount() << endl;
  cout << "Mean: " << h->MeanValue() << endl;
  cout << "Percentiles:" << endl;
  cout << "   0%  (min) = " << h->MinValue() << endl;
  cout << "  25%        = " << h->ValueAtPercentile(25) << endl;
  cout << "  50%  (med) = " << h->ValueAtPercentile(50) << endl;
  cout << "  75%        = " << h->ValueAtPercentile(75) << endl;
  cout << "  95%        = " << h->ValueAtPercentile(95) << endl;
  cout << "  99%        = " << h->ValueAtPercentile(99) << endl;
  cout << "  99.9%      = " << h->ValueAtPercentile(99.9) << endl;
  cout << "  99.99%     = " << h->ValueAtPercentile(99.99) << endl;
  cout << "  100% (max) = " << h->MaxValue() << endl;
  if (h->MaxValue() >= h->highest_trackable_value()) {
    cout << "*NOTE: some values were greater than highest trackable value" << endl;
  }
}

// Verify that the given sorted vector does not contain any duplicate entries.
// If it does, *errors will be incremented once per duplicate and the given message
// will be logged.
static void VerifyNoDuplicateEntries(const std::vector<int64_t>& ints, int* errors,
                                     const string& message) {
  for (size_t i = 1; i < ints.size(); i++) {
    if (ints[i] == ints[i - 1]) {
      LOG(ERROR) << message << ": " << ints[i];
      (*errors)++;
    }
  }
}

Status LinkedListTester::VerifyLinkedListRemote(
    HybridTime snapshot_hybrid_time, const int64_t expected, bool log_errors,
    bool latest_at_leader, const std::function<Status(const std::string&)>& cb,
    int64_t* verified_count) {
  const bool is_latest = !snapshot_hybrid_time.is_valid();
  LOG(INFO) << __func__
            << ": snapshot_hybrid_time="
            << (is_latest ? "LATEST" : snapshot_hybrid_time.ToString())
            << ", expected=" << expected
            << ", log_errors=" << log_errors
            << ", latest_at_leader=" << latest_at_leader;
  client::TableHandle table;
  RETURN_NOT_OK(table.Open(table_name_, client_));

  string snapshot_str;
  if (is_latest) {
    snapshot_str = "LATEST";
  } else {
    snapshot_str = HybridTime(snapshot_hybrid_time).ToString();
  }

  client::TableIteratorOptions options;
  options.columns = verify_projection_;
  options.consistency = latest_at_leader ? YBConsistencyLevel::STRONG
                                         : YBConsistencyLevel::CONSISTENT_PREFIX;
  if (!is_latest) {
    options.read_time = ReadHybridTime::SingleTime(snapshot_hybrid_time);
  }

  LOG(INFO) << "Verifying Snapshot: " << snapshot_str << " Expected Rows: " << expected;
  LinkedListVerifier verifier(num_chains_, enable_mutation_, expected,
                              GenerateSplitInts());
  verifier.StartScanTimer();

  if (snapshot_hybrid_time.is_valid()) {
    const auto servers = VERIFY_RESULT(client_->ListTabletServers());
    const auto& down_ts = servers.front().uuid;
    LOG(INFO) << "Calling callback on tserver " << down_ts;
    RETURN_NOT_OK(cb(down_ts));
  }

  for (const auto& row : client::TableRange(table, options)) {
    int64_t key = row.column(0).int64_value();
    int64_t link = row.column(1).int64_value();
    // For non-snapshot reads we also verify that all rows were updated. We don't
    // for snapshot reads as updates are performed by their own thread. This means
    // that there is no guarantee that, for any snapshot hybrid_time that comes before
    // all writes are completed, all rows will be updated.
    bool updated = snapshot_hybrid_time.is_valid() ? enable_mutation_ : row.column(2).bool_value();

    verifier.RegisterResult(key, link, updated);
  }

  Status s = verifier.VerifyData(verified_count, log_errors);
  LOG(INFO) << "Snapshot: " << snapshot_str << " verified. Result: " << s.ToString();
  return s;
}

Status LinkedListTester::WaitAndVerify(
    int seconds_to_run, int64_t expected, bool latest_at_leader,
    const std::function<Status(const std::string&)>& cb) {
  std::list<pair<HybridTime, int64_t> > samples_as_list(sampled_hybrid_times_and_counts_.begin(),
                                                        sampled_hybrid_times_and_counts_.end());

  int64_t seen = 0;
  bool called = false;
  Stopwatch sw;
  sw.start();

  Status s;
  do {
    // We'll give the tablets 5 seconds to start up regardless of how long we
    // inserted for. There's some fixed cost startup time, especially when
    // replication is enabled.
    const int kBaseTimeToWaitSecs = 5;
    bool last_attempt = sw.elapsed().wall_seconds() > kBaseTimeToWaitSecs + seconds_to_run;
    s = Status::OK();
    auto iter = samples_as_list.begin();

    while (iter != samples_as_list.end()) {
      // Only call the callback once, on the first verify pass, since it may be destructive.
      if (iter == samples_as_list.begin() && !called) {
        s = VerifyLinkedListAtSnapshotRemote(
                iter->first, iter->second, last_attempt, latest_at_leader, cb, &seen);
        called = true;
      } else {
        s = VerifyLinkedListAtSnapshotRemote(
                iter->first, iter->second, last_attempt, latest_at_leader,
                std::bind(&LinkedListTester::ReturnOk, this, _1), &seen);
      }

      if (s.ok() && (*iter).second != seen) {
        // If we've seen less rows than we were expecting we should fail and not retry.
        //
        // The reasoning is the following:
        //
        // - We know that when we read this snapshot's hybrid_time the writes had completed, thus
        //   at hybrid_time '(*iter).first' any replica should have precisely '(*iter).second' rows.
        // - We also chose to perform a snapshot scan, which, when passed a hybrid_time, waits for
        //   that hybrid_time to become "clean", i.e. it makes sure that all transactions with lower
        //   hybrid_times have completed before it actually performs the scan.
        //
        // Together these conditions mean that if we don't get the expected rows back something
        // is wrong with the read path or with the write path and we should fail immediately.
        return STATUS(Corruption, strings::Substitute("Got wrong row count on snapshot. "
            "Expected: $0, Got:$1", (*iter).second, seen));
      }

      if (!s.ok()) break;
      // If the snapshot verification returned OK erase it so that we don't recheck
      // even if a later snapshot or the final verification failed.
      iter = samples_as_list.erase(iter);
    }
    if (s.ok()) {
      s = VerifyLinkedListNoSnapshotRemote(expected, last_attempt, latest_at_leader, &seen);
    }

    // TODO: when we enable hybridtime consistency for the scans,
    // then we should not allow !s.ok() here. But, with READ_LATEST
    // scans, we could have a lagging replica of one tablet, with an
    // up-to-date replica of another tablet, and end up with broken links
    // in the chain.

    if (!s.ok()) {
      LOG(INFO) << "Table not yet ready: " << seen << "/" << expected << " rows"
                << " (status: " << s.ToString() << ")";
      if (last_attempt) {
        // We'll give it an equal amount of time to re-load the data as it took
        // to write it in. Typically it completes much faster than that.
        return STATUS(TimedOut, "Timed out waiting for table to be accessible again",
                                s.ToString());
      }

      // Sleep and retry until timeout.
      SleepFor(MonoDelta::FromMilliseconds(20));
    }
  } while (!s.ok());

  LOG(INFO) << "Successfully verified " << expected << " rows";

  return Status::OK();
}

/////////////////////////////////////////////////////////////
// LinkedListVerifier
/////////////////////////////////////////////////////////////

LinkedListVerifier::LinkedListVerifier(uint8_t num_chains, bool enable_mutation,
                                       int64_t expected,
                                       std::vector<int64_t> split_key_ints)
    : num_chains_(num_chains),
      expected_(expected),
      enable_mutation_(enable_mutation),
      split_key_ints_(std::move(split_key_ints)),
      errors_(0) {
  if (expected != kNoParticularCountExpected) {
    DCHECK_GE(expected, 0);
    seen_key_.reserve(expected);
    seen_link_to_.reserve(expected);
  }
}

void LinkedListVerifier::StartScanTimer() {
  scan_timer_.start();
}

void LinkedListVerifier::RegisterResult(int64_t key, int64_t link, bool updated) {
  seen_key_.push_back(key);
  if (link != 0) {
    // Links to entry 0 don't count - the first inserts use this link
    seen_link_to_.push_back(link);
  }

  if (updated != enable_mutation_) {
    LOG(ERROR) << "Entry " << key << " was incorrectly "
      << (enable_mutation_ ? "not " : "") << "updated";
    errors_++;
  }
}

void LinkedListVerifier::SummarizeBrokenLinks(const std::vector<int64_t>& broken_links) {
  std::vector<int64_t> errors_by_tablet(split_key_ints_.size() + 1);

  int n_logged = 0;
  const int kMaxToLog = 100;

  for (int64_t broken : broken_links) {
    auto tablet = std::upper_bound(split_key_ints_.begin(),
                                  split_key_ints_.end(),
                                  broken) - split_key_ints_.begin();
    DCHECK_GE(tablet, 0);
    DCHECK_LT(tablet, errors_by_tablet.size());
    errors_by_tablet[tablet]++;

    if (n_logged < kMaxToLog) {
      LOG(ERROR) << "Entry " << broken << " was linked to but not present";
      n_logged++;
      if (n_logged == kMaxToLog) {
        LOG(ERROR) << "... no more broken links will be logged";
      }
    }
  }

  // Summarize the broken links by which tablet they fell into.
  if (!broken_links.empty()) {
    for (size_t tablet = 0; tablet < errors_by_tablet.size(); tablet++) {
      LOG(ERROR) << "Error count for tablet #" << tablet << ": " << errors_by_tablet[tablet];
    }
  }
}

Status LinkedListVerifier::VerifyData(int64_t* verified_count, bool log_errors) {
  *verified_count = seen_key_.size();
  LOG(INFO) << "Done collecting results (" << (*verified_count) << " rows in "
            << scan_timer_.elapsed().wall_millis() << "ms)";

  VLOG(1) << "Sorting results before verification of linked list structure...";
  std::sort(seen_key_.begin(), seen_key_.end());
  std::sort(seen_link_to_.begin(), seen_link_to_.end());
  VLOG(1) << "Done sorting";

  // Verify that no key was seen multiple times or linked to multiple times
  VerifyNoDuplicateEntries(seen_key_, &errors_, "Seen row key multiple times");
  VerifyNoDuplicateEntries(seen_link_to_, &errors_, "Seen link to row multiple times");
  // Verify that every key that was linked to was present
  std::vector<int64_t> broken_links = STLSetDifference(seen_link_to_, seen_key_);
  errors_ += broken_links.size();
  if (log_errors) {
    SummarizeBrokenLinks(broken_links);
  }

  // Verify that only the expected number of keys were seen but not linked to.
  // Only the last "batch" should have this characteristic.
  std::vector<int64_t> not_linked_to = STLSetDifference(seen_key_, seen_link_to_);
  if (not_linked_to.size() != num_chains_) {
    LOG_IF(ERROR, log_errors)
      << "Had " << not_linked_to.size() << " entries which were seen but not"
      << " linked to. Expected only " << num_chains_;
    errors_++;
  }

  if (errors_ > 0) {
    return STATUS(Corruption, "Had one or more errors during verification (see log)");
  }

  if (expected_ != *verified_count) {
    return STATUS(IllegalState, strings::Substitute(
        "Missing rows, but with no broken link in the chain. This means that "
        "a suffix of the inserted rows went missing. Expected=$0, seen=$1.",
        expected_, *verified_count));
  }

  return Status::OK();
}

TEST_F(LinkedListTest, TestLoadAndVerify) {
  OverrideFlagForSlowTests("seconds_to_run", "30");
  OverrideFlagForSlowTests("stress_flush_compact", "true");
  OverrideFlagForSlowTests("stress_wal_gc", "true");
  ASSERT_NO_FATALS(BuildAndStart());

  string tablet_id = tablet_replicas_.begin()->first;

  // In TSAN builds, we hit the web UIs more often, so we have a better chance
  // of seeing a thread error. We don't do this in normal builds since we
  // also use this test as a benchmark and it soaks up a lot of CPU.
#ifdef THREAD_SANITIZER
  const MonoDelta check_freq = MonoDelta::FromMilliseconds(10);
#else
  const MonoDelta check_freq = MonoDelta::FromSeconds(1);
#endif

  PeriodicWebUIChecker checker(*cluster_.get(), tablet_id,
                               check_freq);

  bool can_kill_ts = FLAGS_num_tablet_servers > 1 && FLAGS_replication_factor > 2;

  int64_t written = 0;
  ASSERT_OK(tester_->LoadLinkedList(MonoDelta::FromSeconds(FLAGS_seconds_to_run),
                                           FLAGS_num_snapshots,
                                           &written));

  // TODO: currently we don't use hybridtime on the C++ client, so it's possible when we
  // scan after writing we may not see all of our writes (we may scan a replica). So,
  // we use WaitAndVerify here instead of a plain Verify.
  ASSERT_OK(tester_->WaitAndVerify(FLAGS_seconds_to_run, written, /* latest_at_leader = */ true));
  ASSERT_OK(CheckTabletServersAreAlive(tablet_servers_.size()));

  LOG(INFO) << "Successfully verified " << written << " rows before killing any servers.";

  if (can_kill_ts) {
    // Restart a tserver during a scan to test scanner fault tolerance.
    WaitForTSAndReplicas();
    LOG(INFO) << "Will restart the tablet server during verification scan.";
    ASSERT_OK(tester_->WaitAndVerify(
        FLAGS_seconds_to_run, written, /* latest_at_leader = */ true,
        std::bind(&TabletServerIntegrationTestBase::RestartServerWithUUID, this, _1)));
    LOG(INFO) << "Done with tserver restart test.";
    ASSERT_OK(CheckTabletServersAreAlive(tablet_servers_.size()));

    // Kill a tserver during a scan to test scanner fault tolerance.
    // Note that the previously restarted node is likely still be bootstrapping, which makes this
    // even harder.
    LOG(INFO) << "Will kill the tablet server during verification scan.";
    ASSERT_OK(tester_->WaitAndVerify(
        FLAGS_seconds_to_run, written, /* latest_at_leader = */ true,
        std::bind(&TabletServerIntegrationTestBase::ShutdownServerWithUUID, this, _1)));
    LOG(INFO) << "Done with tserver kill test.";
    ASSERT_OK(CheckTabletServersAreAlive(tablet_servers_.size()-1));
    ASSERT_NO_FATALS(RestartCluster());
    // Again wait for cluster to finish bootstrapping.
    WaitForTSAndReplicas();

    // Check in-memory state with a downed TS. Scans may try other replicas.
    string tablet = (*tablet_replicas_.begin()).first;
    TServerDetails* leader;
    EXPECT_OK(GetLeaderReplicaWithRetries(tablet, &leader));
    LOG(INFO) << "Killing TS: " << leader->instance_id.permanent_uuid() << ", leader of tablet: "
        << tablet << " and verifying that we can still read all results";
    ASSERT_OK(ShutdownServerWithUUID(leader->uuid()));
    ASSERT_OK(tester_->WaitAndVerify(FLAGS_seconds_to_run, written, /* latest_at_leader = */ true));
    ASSERT_OK(CheckTabletServersAreAlive(tablet_servers_.size() - 1));
  }

  // Kill and restart the cluster, verify data remains.
  ASSERT_NO_FATALS(RestartCluster());

  LOG(INFO) << "Verifying rows after restarting entire cluster.";

  // We need to loop here because the tablet may spend some time in BOOTSTRAPPING state
  // initially after a restart. TODO: Scanner should support its own retries in this circumstance.
  // Remove this loop once client is more fleshed out.
  ASSERT_OK(tester_->WaitAndVerify(FLAGS_seconds_to_run, written, /* latest_at_leader = */ true));

  // In slow tests mode, we'll wait for a little bit to allow time for the tablet to
  // compact. This is a regression test for bugs where compaction post-bootstrap
  // could cause data loss.
  if (AllowSlowTests()) {
    SleepFor(MonoDelta::FromSeconds(10));
    ASSERT_OK(tester_->WaitAndVerify(FLAGS_seconds_to_run, written, /* latest_at_leader = */ true));
  }
  ASSERT_OK(CheckTabletServersAreAlive(tablet_servers_.size()));

  // Check post-replication state with a downed TS.
  if (can_kill_ts) {
    string tablet = (*tablet_replicas_.begin()).first;
    TServerDetails* leader;
    EXPECT_OK(GetLeaderReplicaWithRetries(tablet, &leader));
    LOG(INFO) << "Killing TS: " << leader->instance_id.permanent_uuid() << ", leader of tablet: "
        << tablet << " and verifying that we can still read all results";
    ASSERT_OK(ShutdownServerWithUUID(leader->uuid()));
    ASSERT_OK(tester_->WaitAndVerify(FLAGS_seconds_to_run, written, /* latest_at_leader = */ true));
    ASSERT_OK(CheckTabletServersAreAlive(tablet_servers_.size() - 1));
  }

  ASSERT_NO_FATALS(RestartCluster());

  // Sleep a little bit, so that the tablet is probably in bootstrapping state.
  SleepFor(MonoDelta::FromMilliseconds(100));

  // Restart while bootstrapping
  ASSERT_NO_FATALS(RestartCluster());

  ASSERT_OK(tester_->WaitAndVerify(FLAGS_seconds_to_run, written, /* latest_at_leader = */ true));
  ASSERT_OK(CheckTabletServersAreAlive(tablet_servers_.size()));

  ASSERT_ALL_REPLICAS_AGREE(written);

  // Dump the performance info at the very end, so it's easy to read. On a failed
  // test, we don't care about this stuff anyway.
  tester_->DumpInsertHistogram(true);
}

// This test loads the linked list while one of the servers is down.
// Once the loading is complete, the server is started back up and
// we wait for it to catch up. Then we shut down the other two servers
// and verify that the data is correct on the server which caught up.
TEST_F(LinkedListTest, TestLoadWhileOneServerDownAndVerify) {
  OverrideFlagForSlowTests("seconds_to_run", "30");

  if (!FLAGS_ts_flags.empty()) {
    FLAGS_ts_flags += " ";
  }

  FLAGS_ts_flags += "--log_cache_size_limit_mb=2";
  FLAGS_ts_flags += " --global_log_cache_size_limit_mb=4";

  FLAGS_num_tablet_servers = 3;
  FLAGS_num_tablets = 1;
  ASSERT_NO_FATALS(BuildAndStart());

  LOG(INFO) << "Load the data with one of the three servers down.";
  cluster_->tablet_server(0)->Shutdown();

  int64_t written = 0;
  ASSERT_OK(tester_->LoadLinkedList(MonoDelta::FromSeconds(FLAGS_seconds_to_run),
                                           FLAGS_num_snapshots,
                                           &written));
  LOG(INFO) << "Start back up the server that missed all of the data being loaded. It should be"
            << "able to stream the data back from the other server which is still up.";
  ASSERT_OK(cluster_->tablet_server(0)->Restart());

  const int kBaseTimeToWaitSecs = 5;
  LOG(INFO) << "We'll give the tablets " << kBaseTimeToWaitSecs << " seconds to start up "
            << "regardless of how long we inserted for. This prevents flakiness in TSAN builds "
            << "in particular.";
  const int kWaitTime = FLAGS_seconds_to_run + kBaseTimeToWaitSecs;

  set<TabletId> converged_tablets;
  for (const auto& tablet_replica_entry : tablet_replicas_) {
    const TabletId& tablet_id = tablet_replica_entry.first;
    if (converged_tablets.count(tablet_id)) {
      continue;
    }
    converged_tablets.insert(tablet_id);
    LOG(INFO) << "Waiting for replicas of tablet " << tablet_id << " to agree";
    ASSERT_OK(WaitForServersToAgree(
        MonoDelta::FromSeconds(kWaitTime),
        tablet_servers_,
        tablet_id,
        // TODO: not sure if this number makes sense. At best this is a lower bound on the Raft
        // index.
        written / FLAGS_num_chains,
        /* actual_index */ nullptr,
        // In addition to all replicas having all entries in the log, we require that followers
        // know that all entries are committed.
        MustBeCommitted::kTrue));
  }

  ASSERT_ALL_REPLICAS_AGREE(written);

  cluster_->tablet_server(1)->Shutdown();
  cluster_->tablet_server(2)->Shutdown();

  // We can't force reads to go to the leader, because with two out of the three servers down there
  // won't be a leader.
  ASSERT_OK(tester_->WaitAndVerify(FLAGS_seconds_to_run, written, /* latest_at_leader = */ false));
}

}  // namespace yb
