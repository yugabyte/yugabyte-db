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

typedef LatchTransactionCompletionCallback<WriteResponsePB> LatchWriteCallback;

class TabletPeerTest : public YBTabletTest,
                       public ::testing::WithParamInterface<TableType> {
 public:
  TabletPeerTest()
    : YBTabletTest(GetTestSchema(), YQL_TABLE_TYPE),
      insert_counter_(0),
      delete_counter_(0) {
  }

  void SetUp() override {
    YBTabletTest::SetUp();

    table_type_ = YQL_TABLE_TYPE;

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
                                 nullptr /* client */,
                                 clock(),
                                 messenger_,
                                 log,
                                 metric_entity_));
  }

  Status StartPeer(const ConsensusBootstrapInfo& info) {
    RETURN_NOT_OK(tablet_peer_->Start(info));

    RETURN_NOT_OK(tablet_peer_->consensus()->EmulateElection());

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
    auto tx_state = std::make_unique<WriteTransactionState>(tablet_peer, &req, resp.get());

    CountDownLatch rpc_latch(1);
    tx_state->set_completion_callback(std::make_unique<LatchWriteCallback>(&rpc_latch, resp.get()));

    CHECK_OK(tablet_peer->SubmitWrite(std::move(tx_state)));
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
                          std::unique_ptr<WriteTransactionState> state)
      : WriteTransaction(std::move(state), consensus::LEADER),
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

// Ensure that Log::GC() doesn't delete logs with anchors.
TEST_P(TabletPeerTest, TestLogAnchorsAndGC) {
  FLAGS_log_min_seconds_to_retain = 0;
  ConsensusBootstrapInfo info;
  ASSERT_OK(StartPeer(info));

  Log* log = tablet_peer_->log();
  int32_t num_gced;

  log::SegmentSequence segments;
  ASSERT_OK(log->GetLogReader()->GetSegmentsSnapshot(&segments));

  ASSERT_EQ(1, segments.size());
  ASSERT_OK(ExecuteInsertsAndRollLogs(3));
  ASSERT_OK(log->GetLogReader()->GetSegmentsSnapshot(&segments));
  ASSERT_EQ(4, segments.size());

  AssertLogAnchorEarlierThanLogLatest();

  // Ensure nothing gets deleted.
  int64_t min_log_index = -1;
  ASSERT_OK(tablet_peer_->GetEarliestNeededLogIndex(&min_log_index));
  ASSERT_OK(log->GC(min_log_index, &num_gced));
  ASSERT_EQ(0, num_gced) << "earliest needed: " << min_log_index;

  // Flush RocksDB to ensure that we don't have OpId in anchors.
  ASSERT_OK(tablet_peer_->tablet()->Flush());

  // The first two segments should be deleted.
  // The last is anchored due to the commit in the last segment being the last
  // OpId in the log.
  int32_t earliest_needed = static_cast<int32_t>(tablet_peer_->tablet()->MaxPersistentOpId().index);
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

  log::SegmentSequence segments;
  ASSERT_OK(log->GetLogReader()->GetSegmentsSnapshot(&segments));

  ASSERT_EQ(1, segments.size());
  ASSERT_OK(ExecuteInsertsAndRollLogs(2));
  ASSERT_OK(log->GetLogReader()->GetSegmentsSnapshot(&segments));
  ASSERT_EQ(3, segments.size());

  // Flush RocksDB so the next mutation goes into a DMS.
  ASSERT_OK(tablet_peer_->tablet()->Flush());

  int32_t earliest_needed = static_cast<int32_t>(tablet_peer_->tablet()->MaxPersistentOpId().index);
  auto total_segments = log->GetLogReader()->num_segments();
  int64_t min_log_index = -1;
  ASSERT_OK(tablet_peer_->GetEarliestNeededLogIndex(&min_log_index));
  ASSERT_OK(log->GC(min_log_index, &num_gced));
  // We will only GC 1, and have 1 left because the earliest needed OpId falls
  // back to the latest OpId written to the Log if no anchors are set.
  ASSERT_EQ(earliest_needed, num_gced);
  ASSERT_OK(log->GetLogReader()->GetSegmentsSnapshot(&segments));
  ASSERT_EQ(total_segments - earliest_needed, segments.size());

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

  total_segments += 2;
  ASSERT_OK(log->GetLogReader()->GetSegmentsSnapshot(&segments));
  ASSERT_EQ(total_segments, segments.size());

  // Execute another couple inserts, but Flush it so it doesn't anchor.
  ASSERT_OK(ExecuteInsertsAndRollLogs(2));
  total_segments += 2;
  ASSERT_OK(log->GetLogReader()->GetSegmentsSnapshot(&segments));
  ASSERT_EQ(total_segments, segments.size());

  // Ensure the delta and last insert remain in the logs, anchored by the delta.
  // Note that this will allow GC of the 2nd insert done above.
  earliest_needed = tablet_peer_->tablet()->MaxPersistentOpId().index;
  ASSERT_OK(tablet_peer_->GetEarliestNeededLogIndex(&min_log_index));
  ASSERT_OK(log->GC(min_log_index, &num_gced));
  ASSERT_EQ(earliest_needed, num_gced);
  ASSERT_OK(log->GetLogReader()->GetSegmentsSnapshot(&segments));
  ASSERT_EQ(total_segments - earliest_needed, segments.size());

  // Flush DMS to release the anchor.
  ASSERT_OK(tablet_peer_->tablet()->FlushBiggestDMS());

  earliest_needed = static_cast<int32_t>(tablet_peer_->tablet()->MaxPersistentOpId().index);
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

  log::SegmentSequence segments;
  ASSERT_OK(log->GetLogReader()->GetSegmentsSnapshot(&segments));

  ASSERT_EQ(1, segments.size());
  ASSERT_OK(ExecuteInsertsAndRollLogs(4));
  ASSERT_OK(log->GetLogReader()->GetSegmentsSnapshot(&segments));
  ASSERT_EQ(5, segments.size());
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

INSTANTIATE_TEST_CASE_P(Rocks, TabletPeerTest, ::testing::Values(YQL_TABLE_TYPE));

} // namespace tablet
} // namespace yb
