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

#include <glog/logging.h>
#include <gtest/gtest.h>

#include "yb/common/partial_row.h"
#include "yb/common/hybrid_time.h"
#include "yb/common/wire_protocol.h"
#include "yb/common/wire_protocol-test-util.h"
#include "yb/consensus/consensus_meta.h"
#include "yb/consensus/log.h"
#include "yb/consensus/log_reader.h"
#include "yb/consensus/log_util.h"
#include "yb/consensus/opid_util.h"
#include "yb/gutil/gscoped_ptr.h"
#include "yb/gutil/macros.h"
#include "yb/rpc/messenger.h"
#include "yb/server/clock.h"
#include "yb/server/logical_clock.h"
#include "yb/tablet/maintenance_manager.h"
#include "yb/tablet/transactions/transaction.h"
#include "yb/tablet/transactions/transaction_driver.h"
#include "yb/tablet/transactions/write_transaction.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/tablet_peer_mm_ops.h"
#include "yb/tablet/tablet-test-util.h"
#include "yb/tserver/tserver.pb.h"
#include "yb/util/metrics.h"
#include "yb/util/test_util.h"
#include "yb/util/test_macros.h"
#include "yb/util/threadpool.h"

METRIC_DECLARE_entity(tablet);

DECLARE_int32(log_min_seconds_to_retain);

namespace yb {
namespace tablet {

using consensus::CommitMsg;
using consensus::Consensus;
using consensus::ConsensusBootstrapInfo;
using consensus::ConsensusMetadata;
using consensus::MakeOpId;
using consensus::MinimumOpId;
using consensus::OpId;
using consensus::OpIdEquals;
using consensus::RaftPeerPB;
using consensus::WRITE_OP;
using docdb::KeyValueWriteBatchPB;
using log::Log;
using log::LogAnchorRegistry;
using log::LogOptions;
using rpc::Messenger;
using server::Clock;
using server::LogicalClock;
using std::shared_ptr;
using std::string;
using strings::Substitute;
using tserver::WriteRequestPB;
using tserver::WriteResponsePB;

static Schema GetTestSchema() {
  return Schema({ ColumnSchema("key", INT32) }, 1);
}

class TabletPeerTest : public YBTabletTest,
                       public ::testing::WithParamInterface<TableType> {
 public:
  TabletPeerTest()
    : YBTabletTest(GetTestSchema(), GetParam()),
      insert_counter_(0),
      delete_counter_(0) {
  }

  void SetUp() override {
    YBTabletTest::SetUp();

    table_type_ = GetParam();

    ASSERT_OK(ThreadPoolBuilder("apply").Build(&apply_pool_));

    rpc::MessengerBuilder builder(CURRENT_TEST_NAME());
    ASSERT_OK(builder.Build(&messenger_));

    metric_entity_ = METRIC_ENTITY_tablet.Instantiate(&metric_registry_, "test-tablet");

    RaftPeerPB config_peer;
    config_peer.set_permanent_uuid(tablet()->metadata()->fs_manager()->uuid());
    config_peer.set_member_type(RaftPeerPB::VOTER);
    config_peer.mutable_last_known_addr()->set_host("fake-host");
    config_peer.mutable_last_known_addr()->set_port(0);

    // "Bootstrap" and start the TabletPeer.
    tablet_peer_.reset(
      new TabletPeer(make_scoped_refptr(tablet()->metadata()),
                     config_peer,
                     apply_pool_.get(),
                     Bind(&TabletPeerTest::TabletPeerStateChangedCallback,
                          Unretained(this),
                          tablet()->tablet_id())));

    // Make TabletPeer use the same LogAnchorRegistry as the Tablet created by the harness.
    // TODO: Refactor TabletHarness to allow taking a LogAnchorRegistry, while also providing
    // TabletMetadata for consumption by TabletPeer before Tablet is instantiated.
    tablet_peer_->log_anchor_registry_ = tablet()->log_anchor_registry_;

    RaftConfigPB config;
    config.add_peers()->CopyFrom(config_peer);
    config.set_opid_index(consensus::kInvalidOpIdIndex);

    gscoped_ptr<ConsensusMetadata> cmeta;
    ASSERT_OK(ConsensusMetadata::Create(tablet()->metadata()->fs_manager(),
                                        tablet()->tablet_id(),
                                        tablet()->metadata()->fs_manager()->uuid(),
                                        config,
                                        consensus::kMinimumTerm, &cmeta));

    scoped_refptr<Log> log;
    ASSERT_OK(Log::Open(LogOptions(), fs_manager(), tablet()->tablet_id(),
                        tablet()->metadata()->wal_dir(), *tablet()->schema(),
                        tablet()->metadata()->schema_version(), metric_entity_.get(), &log));

    tablet_peer_->SetBootstrapping();
    ASSERT_OK(tablet_peer_->Init(tablet(),
                                 clock(),
                                 messenger_,
                                 log,
                                 metric_entity_));
  }

  Status StartPeer(const ConsensusBootstrapInfo& info) {
    RETURN_NOT_OK(tablet_peer_->Start(info));

    return Status::OK();
  }

  void TabletPeerStateChangedCallback(
      const string& tablet_id,
      std::shared_ptr<consensus::StateChangeContext> context) {
    LOG(INFO) << "Tablet peer state changed for tablet " << tablet_id
              << ". Reason: " << context->ToString();
  }

  void TearDown() override {
    tablet_peer_->Shutdown();
    apply_pool_->Shutdown();
    YBTabletTest::TearDown();
  }

 protected:
  // Generate monotonic sequence of key column integers.
  Status GenerateSequentialInsertRequest(WriteRequestPB* write_req) {
    Schema schema(GetTestSchema());
    write_req->set_tablet_id(tablet()->tablet_id());
    CHECK_OK(SchemaToPB(schema, write_req->mutable_schema()));

    YBPartialRow row(&schema);
    CHECK_OK(row.SetInt32("key", (insert_counter_++)));

    RowOperationsPBEncoder enc(write_req->mutable_row_operations());
    enc.Add(RowOperationsPB::INSERT, row);
    return Status::OK();
  }

  // Generate monotonic sequence of deletions, starting with 0.
  // Will assert if you try to delete more rows than you inserted.
  Status GenerateSequentialDeleteRequest(WriteRequestPB* write_req) {
    CHECK_LT(delete_counter_, insert_counter_);
    Schema schema(GetTestSchema());
    write_req->set_tablet_id(tablet()->tablet_id());
    CHECK_OK(SchemaToPB(schema, write_req->mutable_schema()));

    YBPartialRow row(&schema);
    CHECK_OK(row.SetInt32("key", delete_counter_++));

    RowOperationsPBEncoder enc(write_req->mutable_row_operations());
    enc.Add(RowOperationsPB::DELETE, row);
    return Status::OK();
  }

  Status ExecuteWriteAndRollLog(TabletPeer* tablet_peer, const WriteRequestPB& req) {
    gscoped_ptr<WriteResponsePB> resp(new WriteResponsePB());
    auto tx_state = new WriteTransactionState(tablet_peer, &req, resp.get());

    CountDownLatch rpc_latch(1);
    tx_state->set_completion_callback(gscoped_ptr<TransactionCompletionCallback>(
        new LatchTransactionCompletionCallback<WriteResponsePB>(&rpc_latch, resp.get())).Pass());

    CHECK_OK(tablet_peer->SubmitWrite(tx_state));
    rpc_latch.Wait();
    CHECK(!resp->has_error())
        << "\nReq:\n" << req.DebugString() << "Resp:\n" << resp->DebugString();

    // Roll the log after each write.
    // Usually the append thread does the roll and no additional sync is required. However in
    // this test the thread that is appending is not the same thread that is rolling the log
    // so we must make sure the Log's queue is flushed before we roll or we might have a race
    // between the appender thread and the thread executing the test.
    CHECK_OK(tablet_peer->log_->WaitUntilAllFlushed());
    CHECK_OK(tablet_peer->log_->AllocateSegmentAndRollOver());
    return Status::OK();
  }

  // Execute insert requests and roll log after each one.
  Status ExecuteInsertsAndRollLogs(int num_inserts) {
    for (int i = 0; i < num_inserts; i++) {
      gscoped_ptr<WriteRequestPB> req(new WriteRequestPB());
      RETURN_NOT_OK(GenerateSequentialInsertRequest(req.get()));
      RETURN_NOT_OK(ExecuteWriteAndRollLog(tablet_peer_.get(), *req));
    }

    return Status::OK();
  }

  // Execute delete requests and roll log after each one.
  Status ExecuteDeletesAndRollLogs(int num_deletes) {
    for (int i = 0; i < num_deletes; i++) {
      gscoped_ptr<WriteRequestPB> req(new WriteRequestPB());
      CHECK_OK(GenerateSequentialDeleteRequest(req.get()));
      CHECK_OK(ExecuteWriteAndRollLog(tablet_peer_.get(), *req));
    }

    return Status::OK();
  }

  void AssertNoLogAnchors() {
    if (table_type_ != KUDU_COLUMNAR_TABLE_TYPE) {
      return;
    }

    OpId last_log_opid;
    // Wait for the leader election opid to be appended.
    const MonoDelta timeout(MonoDelta::FromSeconds(10));
    const MonoTime start(MonoTime::Now(MonoTime::FINE));
    while (last_log_opid.index() < 1) {
      MonoTime now(MonoTime::Now(MonoTime::FINE));
      MonoDelta elapsed(now.GetDeltaSince(start));
      if (elapsed.MoreThan(timeout)) {
        FAIL() << "Leader election log entry not appended";
      }
      SleepFor(MonoDelta::FromMilliseconds(100));
      tablet_peer_->log_->GetLatestEntryOpId(&last_log_opid);
    }

    // Make sure that there are no registered anchors in the registry
    CHECK_EQ(0, tablet_peer_->log_anchor_registry()->GetAnchorCountForTests());
    int64_t earliest_index = -1;
    // And that there are no in-flight transactions (which are implicit
    // anchors) by comparing the TabletPeer's earliest needed OpId and the last
    // entry in the log; if they match there is nothing in flight.
    CHECK_OK(tablet_peer_->GetEarliestNeededLogIndex(&earliest_index));
    tablet_peer_->log_->GetLatestEntryOpId(&last_log_opid);
    CHECK_EQ(earliest_index, last_log_opid.index())
      << "Found unexpected anchor: " << earliest_index
      << " Last log entry: " << last_log_opid.ShortDebugString();
  }

  // Assert that the Log GC() anchor is earlier than the latest OpId in the Log.
  void AssertLogAnchorEarlierThanLogLatest() {
    int64_t earliest_index = -1;
    CHECK_OK(tablet_peer_->GetEarliestNeededLogIndex(&earliest_index));
    OpId last_log_opid;
    tablet_peer_->log_->GetLatestEntryOpId(&last_log_opid);
    CHECK_LT(earliest_index, last_log_opid.index())
      << "Expected valid log anchor, got earliest opid: " << earliest_index
      << " (expected any value earlier than last log id: " << last_log_opid.ShortDebugString()
      << ")";
  }

  // We disable automatic log GC. Don't leak those changes.
  google::FlagSaver flag_saver_;

  int32_t insert_counter_;
  int32_t delete_counter_;
  MetricRegistry metric_registry_;
  scoped_refptr<MetricEntity> metric_entity_;
  shared_ptr<Messenger> messenger_;
  scoped_refptr<TabletPeer> tablet_peer_;
  gscoped_ptr<ThreadPool> apply_pool_;
  TableType table_type_;
};

// A Transaction that waits on the apply_continue latch inside of Apply().
class DelayedApplyTransaction : public WriteTransaction {
 public:
  DelayedApplyTransaction(CountDownLatch* apply_started,
                          CountDownLatch* apply_continue,
                          WriteTransactionState* state)
      : WriteTransaction(state, consensus::LEADER),
        apply_started_(DCHECK_NOTNULL(apply_started)),
        apply_continue_(DCHECK_NOTNULL(apply_continue)) {
  }

  Status Apply(gscoped_ptr<CommitMsg>* commit_msg) override {
    apply_started_->CountDown();
    LOG(INFO) << "Delaying apply...";
    apply_continue_->Wait();
    LOG(INFO) << "Apply proceeding";
    return WriteTransaction::Apply(commit_msg);
  }

 private:
  CountDownLatch* apply_started_;
  CountDownLatch* apply_continue_;
  DISALLOW_COPY_AND_ASSIGN(DelayedApplyTransaction);
};

// Ensure that Log::GC() doesn't delete logs when the MRS has an anchor.
TEST_P(TabletPeerTest, TestMRSAnchorPreventsLogGC) {
  FLAGS_log_min_seconds_to_retain = 0;
  ConsensusBootstrapInfo info;
  ASSERT_OK(StartPeer(info));

  Log* log = tablet_peer_->log_.get();
  int32_t num_gced;

  AssertNoLogAnchors();

  log::SegmentSequence segments;
  ASSERT_OK(log->GetLogReader()->GetSegmentsSnapshot(&segments));

  ASSERT_EQ(1, segments.size());
  ASSERT_OK(ExecuteInsertsAndRollLogs(3));
  ASSERT_OK(log->GetLogReader()->GetSegmentsSnapshot(&segments));
  ASSERT_EQ(4, segments.size());

  AssertLogAnchorEarlierThanLogLatest();

  if (table_type_ == KUDU_COLUMNAR_TABLE_TYPE) {
    ASSERT_GT(tablet_peer_->log_anchor_registry()->GetAnchorCountForTests(), 0);
  }

  // Ensure nothing gets deleted.
  int64_t min_log_index = -1;
  ASSERT_OK(tablet_peer_->GetEarliestNeededLogIndex(&min_log_index));
  ASSERT_OK(log->GC(min_log_index, &num_gced));
  ASSERT_EQ(0, num_gced) << "earliest needed: " << min_log_index;

  // Flush MRS as needed to ensure that we don't have OpId anchors in the MRS.
  ASSERT_OK(tablet_peer_->tablet()->Flush());
  AssertNoLogAnchors();

  // The first two segments should be deleted.
  // The last is anchored due to the commit in the last segment being the last
  // OpId in the log.
  int32_t earliest_needed = table_type_ == KUDU_COLUMNAR_TABLE_TYPE ?
      2 : static_cast<int32_t>(tablet_peer_->tablet()->LargestFlushedSequenceNumber());
  auto total_segments = log->GetLogReader()->num_segments();
  ASSERT_OK(tablet_peer_->GetEarliestNeededLogIndex(&min_log_index));
  ASSERT_OK(log->GC(min_log_index, &num_gced));
  ASSERT_EQ(earliest_needed, num_gced) << "earliest needed: " << min_log_index;
  ASSERT_OK(log->GetLogReader()->GetSegmentsSnapshot(&segments));
  ASSERT_EQ(total_segments - earliest_needed, segments.size());
}

// Ensure that Log::GC() doesn't delete logs when the DMS has an anchor.
TEST_P(TabletPeerTest, TestDMSAnchorPreventsLogGC) {
  FLAGS_log_min_seconds_to_retain = 0;
  ConsensusBootstrapInfo info;
  ASSERT_OK(StartPeer(info));

  Log* log = tablet_peer_->log_.get();
  int32_t num_gced;

  AssertNoLogAnchors();

  log::SegmentSequence segments;
  ASSERT_OK(log->GetLogReader()->GetSegmentsSnapshot(&segments));

  ASSERT_EQ(1, segments.size());
  ASSERT_OK(ExecuteInsertsAndRollLogs(2));
  ASSERT_OK(log->GetLogReader()->GetSegmentsSnapshot(&segments));
  ASSERT_EQ(3, segments.size());

  // Flush MRS & GC log so the next mutation goes into a DMS.
  ASSERT_OK(tablet_peer_->tablet()->Flush());

  int32_t earliest_needed = table_type_ == KUDU_COLUMNAR_TABLE_TYPE ?
      1 : static_cast<int32_t>(tablet_peer_->tablet()->LargestFlushedSequenceNumber());
  auto total_segments = log->GetLogReader()->num_segments();
  int64_t min_log_index = -1;
  ASSERT_OK(tablet_peer_->GetEarliestNeededLogIndex(&min_log_index));
  ASSERT_OK(log->GC(min_log_index, &num_gced));
  // We will only GC 1, and have 1 left because the earliest needed OpId falls
  // back to the latest OpId written to the Log if no anchors are set.
  ASSERT_EQ(earliest_needed, num_gced);
  ASSERT_OK(log->GetLogReader()->GetSegmentsSnapshot(&segments));
  ASSERT_EQ(total_segments - earliest_needed, segments.size());
  AssertNoLogAnchors();

  OpId id;
  log->GetLatestEntryOpId(&id);
  LOG(INFO) << "Before: " << id.ShortDebugString();

  // We currently have no anchors and the last operation in the log is 0.3
  // Before the below was ExecuteDeletesAndRollLogs(1) but that was breaking
  // what I think is a wrong assertion.
  // I.e. since 0.4 is the last operation that we know is in memory 0.4 is the
  // last anchor we expect _and_ it's the last op in the log.
  // Only if we apply two operations is the last anchored operation and the
  // last operation in the log different.

  // Execute a mutation.
  ASSERT_OK(ExecuteDeletesAndRollLogs(2));
  AssertLogAnchorEarlierThanLogLatest();

  if (table_type_ == KUDU_COLUMNAR_TABLE_TYPE) {
    ASSERT_GT(tablet_peer_->log_anchor_registry()->GetAnchorCountForTests(), 0);
    total_segments = 4;
  } else {
    total_segments += 2;
  }
  ASSERT_OK(log->GetLogReader()->GetSegmentsSnapshot(&segments));
  ASSERT_EQ(total_segments, segments.size());

  // Execute another couple inserts, but Flush it so it doesn't anchor.
  ASSERT_OK(ExecuteInsertsAndRollLogs(2));
  total_segments += 2;
  ASSERT_OK(tablet_peer_->tablet()->Flush());
  ASSERT_OK(log->GetLogReader()->GetSegmentsSnapshot(&segments));
  ASSERT_EQ(total_segments, segments.size());

  // Ensure the delta and last insert remain in the logs, anchored by the delta.
  // Note that this will allow GC of the 2nd insert done above.
  earliest_needed = table_type_ == KUDU_COLUMNAR_TABLE_TYPE ?
      1 : tablet_peer_->tablet()->LargestFlushedSequenceNumber();
  ASSERT_OK(tablet_peer_->GetEarliestNeededLogIndex(&min_log_index));
  ASSERT_OK(log->GC(min_log_index, &num_gced));
  ASSERT_EQ(earliest_needed, num_gced);
  ASSERT_OK(log->GetLogReader()->GetSegmentsSnapshot(&segments));
  ASSERT_EQ(total_segments - earliest_needed, segments.size());

  // Flush DMS to release the anchor.
  ASSERT_OK(tablet_peer_->tablet()->FlushBiggestDMS());

  // Verify no anchors after Flush().
  AssertNoLogAnchors();

  earliest_needed = table_type_ == KUDU_COLUMNAR_TABLE_TYPE ?
      3 : static_cast<int32_t>(tablet_peer_->tablet()->LargestFlushedSequenceNumber());
  total_segments = log->GetLogReader()->num_segments();
  // We should only hang onto one segment due to no anchors.
  // The last log OpId is the commit in the last segment, so it only anchors
  // that segment, not the previous, because it's not the first OpId in the
  // segment.
  ASSERT_OK(tablet_peer_->GetEarliestNeededLogIndex(&min_log_index));
  ASSERT_OK(log->GC(min_log_index, &num_gced));
  ASSERT_EQ(earliest_needed, num_gced);
  ASSERT_OK(log->GetLogReader()->GetSegmentsSnapshot(&segments));
  ASSERT_EQ(total_segments - earliest_needed, segments.size());
}

// Ensure that Log::GC() doesn't compact logs with OpIds of active transactions.
TEST_P(TabletPeerTest, TestActiveTransactionPreventsLogGC) {
  FLAGS_log_min_seconds_to_retain = 0;
  ConsensusBootstrapInfo info;
  ASSERT_OK(StartPeer(info));

  Log* log = tablet_peer_->log_.get();
  int32_t num_gced;

  AssertNoLogAnchors();

  log::SegmentSequence segments;
  ASSERT_OK(log->GetLogReader()->GetSegmentsSnapshot(&segments));

  ASSERT_EQ(1, segments.size());
  ASSERT_OK(ExecuteInsertsAndRollLogs(4));
  ASSERT_OK(log->GetLogReader()->GetSegmentsSnapshot(&segments));
  ASSERT_EQ(5, segments.size());

  // For now end the test for non-KUDU tables.
  if (table_type_ != KUDU_COLUMNAR_TABLE_TYPE) {
    return;
  }

  // Flush MRS as needed to ensure that we don't have OpId anchors in the MRS.
  ASSERT_EQ(1, tablet_peer_->log_anchor_registry()->GetAnchorCountForTests());
  ASSERT_OK(tablet_peer_->tablet()->Flush());

  // Verify no anchors after Flush().
  AssertNoLogAnchors();

  // Now create a long-lived Transaction that hangs during Apply().
  // Allow other transactions to go through. Logs should be populated, but the
  // long-lived Transaction should prevent the log from being deleted since it
  // is in-flight.
  CountDownLatch rpc_latch(1);
  CountDownLatch apply_started(1);
  CountDownLatch apply_continue(1);
  gscoped_ptr<WriteRequestPB> req(new WriteRequestPB());
  gscoped_ptr<WriteResponsePB> resp(new WriteResponsePB());
  {
    // Long-running mutation.
    ASSERT_OK(GenerateSequentialDeleteRequest(req.get()));
    auto tx_state = new WriteTransactionState(tablet_peer_.get(), req.get(), resp.get());

    tx_state->set_completion_callback(gscoped_ptr<TransactionCompletionCallback>(
          new LatchTransactionCompletionCallback<WriteResponsePB>(&rpc_latch, resp.get())).Pass());

    gscoped_ptr<DelayedApplyTransaction> transaction(new DelayedApplyTransaction(&apply_started,
                                                                                 &apply_continue,
                                                                                 tx_state));

    scoped_refptr<TransactionDriver> driver;
    ASSERT_OK(tablet_peer_->NewLeaderTransactionDriver(transaction.PassAs<Transaction>(),
                                                       &driver));

    ASSERT_OK(driver->ExecuteAsync());
    apply_started.Wait();
    ASSERT_TRUE(driver->GetOpId().IsInitialized())
      << "By the time a transaction is applied, it should have an Opid";
    // The apply will hang until we CountDown() the continue latch.
    // Now, roll the log. Below, we execute a few more insertions with rolling.
    ASSERT_OK(log->AllocateSegmentAndRollOver());
  }

  ASSERT_EQ(1, tablet_peer_->txn_tracker_.GetNumPendingForTests());
  // The log anchor is currently equal to the latest OpId written to the Log
  // because we are delaying the Commit message with the CountDownLatch.

  // GC the first four segments created by the inserts.
  int64_t min_log_index = -1;
  ASSERT_OK(tablet_peer_->GetEarliestNeededLogIndex(&min_log_index));
  ASSERT_OK(log->GC(min_log_index, &num_gced));
  ASSERT_EQ(4, num_gced);
  ASSERT_OK(log->GetLogReader()->GetSegmentsSnapshot(&segments));
  ASSERT_EQ(2, segments.size());

  // We use mutations here, since an MRS Flush() quiesces the tablet, and we
  // want to ensure the only thing "anchoring" is the TransactionTracker.
  ASSERT_OK(ExecuteDeletesAndRollLogs(3));
  ASSERT_OK(log->GetLogReader()->GetSegmentsSnapshot(&segments));
  ASSERT_EQ(5, segments.size());
  if (table_type_ == KUDU_COLUMNAR_TABLE_TYPE) {
    ASSERT_EQ(1, tablet_peer_->log_anchor_registry()->GetAnchorCountForTests());
  }
  ASSERT_OK(tablet_peer_->tablet()->FlushBiggestDMS());
  ASSERT_EQ(0, tablet_peer_->log_anchor_registry()->GetAnchorCountForTests());
  ASSERT_EQ(1, tablet_peer_->txn_tracker_.GetNumPendingForTests());

  AssertLogAnchorEarlierThanLogLatest();

  // Try to GC(), nothing should be deleted due to the in-flight transaction.
  ASSERT_OK(tablet_peer_->GetEarliestNeededLogIndex(&min_log_index));
  ASSERT_OK(log->GC(min_log_index, &num_gced));
  ASSERT_EQ(0, num_gced);
  ASSERT_OK(log->GetLogReader()->GetSegmentsSnapshot(&segments));
  ASSERT_EQ(5, segments.size());

  // Now we release the transaction and wait for everything to complete.
  // We fully quiesce and flush, which should release all anchors.
  ASSERT_EQ(1, tablet_peer_->txn_tracker_.GetNumPendingForTests());
  apply_continue.CountDown();
  rpc_latch.Wait();
  tablet_peer_->txn_tracker_.WaitForAllToFinish();
  ASSERT_EQ(0, tablet_peer_->txn_tracker_.GetNumPendingForTests());
  ASSERT_OK(tablet_peer_->tablet()->FlushBiggestDMS());
  AssertNoLogAnchors();

  // All should be deleted except the two last segments.
  ASSERT_OK(tablet_peer_->GetEarliestNeededLogIndex(&min_log_index));
  ASSERT_OK(log->GC(min_log_index, &num_gced));
  ASSERT_EQ(3, num_gced);
  ASSERT_OK(log->GetLogReader()->GetSegmentsSnapshot(&segments));
  ASSERT_EQ(2, segments.size());
}

TEST_P(TabletPeerTest, TestGCEmptyLog) {
  ConsensusBootstrapInfo info;
  ASSERT_OK(tablet_peer_->Start(info));
  // We don't wait on consensus on purpose.
  ASSERT_OK(tablet_peer_->RunLogGC());
}

TEST_P(TabletPeerTest, TestFlushOpsPerfImprovements) {
  MaintenanceOpStats stats;

  // Just on the threshold and not enough time has passed for a time-based flush.
  stats.set_ram_anchored(64 * 1024 * 1024);
  FlushOpPerfImprovementPolicy::SetPerfImprovementForFlush(&stats, 1);
  ASSERT_EQ(0.0, stats.perf_improvement());
  stats.Clear();

  // Just on the threshold and enough time has passed, we'll have a low improvement.
  stats.set_ram_anchored(64 * 1024 * 1024);
  FlushOpPerfImprovementPolicy::SetPerfImprovementForFlush(&stats, 3 * 60 * 1000);
  ASSERT_GT(stats.perf_improvement(), 0.01);
  stats.Clear();

  // Way over the threshold, number is much higher than 1.
  stats.set_ram_anchored(128 * 1024 * 1024);
  FlushOpPerfImprovementPolicy::SetPerfImprovementForFlush(&stats, 1);
  ASSERT_LT(1.0, stats.perf_improvement());
  stats.Clear();

  // Below the threshold but have been there a long time, closing in to 1.0.
  stats.set_ram_anchored(30 * 1024 * 1024);
  FlushOpPerfImprovementPolicy::SetPerfImprovementForFlush(&stats, 60 * 50 * 1000);
  ASSERT_LT(0.7, stats.perf_improvement());
  ASSERT_GT(1.0, stats.perf_improvement());
  stats.Clear();
}

INSTANTIATE_TEST_CASE_P(KUDU, TabletPeerTest, ::testing::Values(KUDU_COLUMNAR_TABLE_TYPE));
INSTANTIATE_TEST_CASE_P(Rocks, TabletPeerTest, ::testing::Values(YQL_TABLE_TYPE));

} // namespace tablet
} // namespace yb
