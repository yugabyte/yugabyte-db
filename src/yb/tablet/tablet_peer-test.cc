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

#include "yb/util/logging.h"
#include <gtest/gtest.h>

#include "yb/common/hybrid_time.h"
#include "yb/common/schema_pbutil.h"
#include "yb/common/wire_protocol-test-util.h"

#include "yb/consensus/consensus.h"
#include "yb/consensus/consensus_fwd.h"
#include "yb/consensus/consensus_meta.h"
#include "yb/consensus/log.h"
#include "yb/consensus/log_anchor_registry.h"
#include "yb/consensus/log_reader.h"
#include "yb/consensus/log_util.h"
#include "yb/consensus/metadata.pb.h"
#include "yb/consensus/multi_raft_batcher.h"
#include "yb/consensus/opid_util.h"
#include "yb/consensus/state_change_context.h"

#include "yb/gutil/bind.h"
#include "yb/gutil/macros.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/proxy.h"

#include "yb/server/clock.h"
#include "yb/server/logical_clock.h"

#include "yb/tablet/tablet-test-util.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/write_query.h"

#include "yb/tserver/tserver.pb.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/debug-util.h"
#include "yb/util/metrics.h"
#include "yb/util/result.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status_log.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_thread_holder.h"
#include "yb/util/threadpool.h"

METRIC_DECLARE_entity(table);
METRIC_DECLARE_entity(tablet);

DECLARE_uint64(initial_log_segment_size_bytes);
DECLARE_int32(log_min_seconds_to_retain);
DECLARE_uint64(log_segment_size_bytes);
DECLARE_uint64(max_group_replicate_batch_size);
DECLARE_uint32(protobuf_message_total_bytes_limit);
DECLARE_uint64(rpc_max_message_size);
DECLARE_int32(retryable_request_timeout_secs);

DECLARE_bool(enable_flush_retryable_requests);
DECLARE_bool(quick_leader_election_on_create);
DECLARE_bool(TEST_pause_before_copying_bootstrap_state);
DECLARE_bool(TEST_pause_before_flushing_bootstrap_state);
DECLARE_bool(TEST_pause_before_submitting_flush_bootstrap_state);

namespace yb {
namespace tablet {

using namespace std::literals;
using consensus::ConsensusBootstrapInfo;
using consensus::ConsensusMetadata;
using consensus::RaftPeerPB;
using log::Log;
using log::LogOptions;
using std::shared_ptr;
using std::string;
using tserver::WriteRequestPB;
using tserver::WriteResponsePB;

static Schema GetTestSchema() {
  return Schema({ ColumnSchema("key", DataType::INT32, ColumnKind::HASH) });
}

class TabletPeerTest : public YBTabletTest {
 public:
  TabletPeerTest() : TabletPeerTest(GetTestSchema()) {}

  explicit TabletPeerTest(const Schema& schema)
    : YBTabletTest(schema, YQL_TABLE_TYPE),
      insert_counter_(0),
      delete_counter_(0) {
  }

  void SetUp() override {
    YBTabletTest::SetUp();

    ASSERT_OK(ThreadPoolBuilder("raft").Build(&raft_pool_));
    ASSERT_OK(ThreadPoolBuilder("prepare").Build(&tablet_prepare_pool_));
    ASSERT_OK(ThreadPoolBuilder("flush-bootstrap-state").Build(&flush_bootstrap_state_pool_));

    rpc::MessengerBuilder builder(CURRENT_TEST_NAME());
    messenger_ = ASSERT_RESULT(builder.Build());
    proxy_cache_ = std::make_unique<rpc::ProxyCache>(messenger_.get());

    table_metric_entity_ = METRIC_ENTITY_table.Instantiate(&metric_registry_, "test-table");
    tablet_metric_entity_ = METRIC_ENTITY_tablet.Instantiate(&metric_registry_, "test-tablet");

    RaftPeerPB config_peer;
    config_peer.set_permanent_uuid(tablet()->metadata()->fs_manager()->uuid());
    config_peer.set_member_type(consensus::PeerMemberType::VOTER);
    auto addr = config_peer.mutable_last_known_private_addr()->Add();
    addr->set_host("fake-host");
    addr->set_port(0);

    multi_raft_manager_ = std::make_unique<consensus::MultiRaftManager>(messenger_.get(),
                                                                        proxy_cache_.get(),
                                                                        config_peer.cloud_info());

    // "Bootstrap" and start the TabletPeer.
    tablet_peer_.reset(new TabletPeer(
        make_scoped_refptr(tablet()->metadata()), config_peer, clock(),
        tablet()->metadata()->fs_manager()->uuid(),
        Bind(
            &TabletPeerTest::TabletPeerStateChangedCallback,
            Unretained(this),
            tablet()->tablet_id()),
        &metric_registry_,
        nullptr, // tablet_splitter
        std::shared_future<client::YBClient*>()
    ));

    // Make TabletPeer use the same LogAnchorRegistry as the Tablet created by the harness.
    // TODO: Refactor TabletHarness to allow taking a LogAnchorRegistry, while also providing
    // RaftGroupMetadata for consumption by TabletPeer before Tablet is instantiated.
    tablet_peer_->log_anchor_registry_ = tablet()->log_anchor_registry_;

    consensus::RaftConfigPB config;
    config.add_peers()->CopyFrom(config_peer);
    config.set_opid_index(consensus::kInvalidOpIdIndex);

    std::unique_ptr<ConsensusMetadata> cmeta = ASSERT_RESULT(ConsensusMetadata::Create(
        tablet()->metadata()->fs_manager(), tablet()->tablet_id(),
        tablet()->metadata()->fs_manager()->uuid(), config, consensus::kMinimumTerm));

    ASSERT_OK(ThreadPoolBuilder("log")
                 .unlimited_threads()
                 .Build(&log_thread_pool_));
    scoped_refptr<Log> log;
    auto metadata = tablet()->metadata();
    log::NewSegmentAllocationCallback noop = {};
    auto new_segment_allocation_callback =
        metadata->IsLazySuperblockFlushEnabled()
            ? std::bind(&RaftGroupMetadata::Flush, metadata, OnlyIfDirty::kTrue)
            : noop;
    TabletPeerWeakPtr peer_weak_ptr(tablet_peer_);
    auto pre_log_rollover_callback = [peer_weak_ptr]() {
      auto peer = peer_weak_ptr.lock();
      if (peer) {
        Status s = peer->SubmitFlushBootstrapStateTask();
        LOG_IF(WARNING, !s.ok() && !s.IsNotSupported())
            <<  "Failed to submit retryable requests task: " << s.ToString();
      }
    };
    ASSERT_OK(Log::Open(LogOptions(), tablet()->tablet_id(), metadata->wal_dir(),
                        metadata->fs_manager()->uuid(), *tablet()->schema(),
                        metadata->schema_version(), table_metric_entity_.get(),
                        tablet_metric_entity_.get(), log_thread_pool_.get(), log_thread_pool_.get(),
                        log_thread_pool_.get(), &log,
                        pre_log_rollover_callback, new_segment_allocation_callback));

    auto bootstrap_state_manager = std::make_shared<TabletBootstrapStateManager>(
        tablet()->tablet_id(), metadata->fs_manager(), metadata->wal_dir());
    ASSERT_OK(bootstrap_state_manager->Init());

    consensus::RetryableRequests retryable_requests(
        MemTracker::FindOrCreateTracker(tablet()->tablet_id()),
        "");
    retryable_requests.SetServerClock(clock());
    retryable_requests.SetRequestTimeout(GetAtomicFlag(&FLAGS_retryable_request_timeout_secs));

    ASSERT_OK(tablet_peer_->SetBootstrapping());
    raft_notifications_pool_ = std::make_unique<rpc::ThreadPool>(rpc::ThreadPoolOptions {
      .name = "raft_notifications",
      .max_workers = rpc::ThreadPoolOptions::kUnlimitedWorkers
    });
    ASSERT_OK(tablet_peer_->InitTabletPeer(tablet(),
                                           nullptr /* server_mem_tracker */,
                                           messenger_.get(),
                                           proxy_cache_.get(),
                                           log,
                                           table_metric_entity_,
                                           tablet_metric_entity_,
                                           raft_pool_.get(),
                                           raft_notifications_pool_.get(),
                                           tablet_prepare_pool_.get(),
                                           &retryable_requests,
                                           bootstrap_state_manager,
                                           nullptr /* consensus_meta */,
                                           multi_raft_manager_.get(),
                                           flush_bootstrap_state_pool_.get()));
    tablet_peer_->EnableFlushBootstrapState();
  }

  Status StartPeer(const ConsensusBootstrapInfo& info) {
    RETURN_NOT_OK(tablet_peer_->Start(info));

    return LoggedWaitFor([&]() -> Result<bool> {
      if (FLAGS_quick_leader_election_on_create) {
        return tablet_peer_->LeaderStatus() == consensus::LeaderStatus::LEADER_AND_READY;
      }
      RETURN_NOT_OK(VERIFY_RESULT(tablet_peer_->GetConsensus())->EmulateElection());
      return true;
    }, MonoDelta::FromMilliseconds(500), "If quick leader elections enabled, wait for peer to be a "
                                         "leader, otherwise emulate.");
  }

  void TabletPeerStateChangedCallback(
      const string& tablet_id,
      std::shared_ptr<consensus::StateChangeContext> context) {
    LOG(INFO) << "Tablet peer state changed for tablet " << tablet_id
              << ". Reason: " << context->ToString();
  }

  void TearDown() override {
    multi_raft_manager_->StartShutdown();
    messenger_->Shutdown();
    WARN_NOT_OK(
        tablet_peer_->Shutdown(
            ShouldAbortActiveTransactions::kFalse, DisableFlushOnShutdown::kFalse),
        "Tablet peer shutdown failed");
    multi_raft_manager_->CompleteShutdown();
    YBTabletTest::TearDown();
  }

 protected:
  // Generate monotonic sequence of key column integers.
  void GenerateSequentialInsertRequest(WriteRequestPB* write_req) {
    write_req->set_tablet_id(tablet()->tablet_id());
    AddTestRowInsert(insert_counter_++, write_req);
  }

  // Generate monotonic sequence of deletions, starting with 0.
  // Will assert if you try to delete more rows than you inserted.
  void GenerateSequentialDeleteRequest(WriteRequestPB* write_req) {
    CHECK_LT(delete_counter_, insert_counter_);
    write_req->set_tablet_id(tablet()->tablet_id());
    AddTestRowDelete(delete_counter_++, write_req);
  }

  void ExecuteWrite(TabletPeer* tablet_peer, const WriteRequestPB& req) {
    WriteResponsePB resp;
    auto query = std::make_unique<WriteQuery>(
        /* leader_term */ 1, CoarseTimePoint::max(), tablet_peer,
        ASSERT_RESULT(tablet_peer->shared_tablet_safe()), nullptr, &resp);
    query->set_client_request(req);

    CountDownLatch rpc_latch(1);
    query->set_callback(MakeLatchOperationCompletionCallback(&rpc_latch, &resp));

    tablet_peer->WriteAsync(std::move(query));
    rpc_latch.Wait();
    CHECK(!resp.has_error())
        << "\nResp:\n" << resp.DebugString() << "Req:\n" << req.DebugString();
  }

  template<class Callback>
  std::unique_ptr<WriteQuery> CreateQuery(TabletPeer* tablet_peer,
                                          const WriteRequestPB& req,
                                          WriteResponsePB* resp,
                                          const Callback& cb) {
    auto query = std::make_unique<WriteQuery>(
        /* leader_term */ 1, CoarseTimePoint::max(), tablet_peer,
        CHECK_RESULT(tablet_peer->shared_tablet_safe()), nullptr, resp);
    query->set_client_request(req);
    query->set_callback(cb);
    return query;
  }

  Status RollLog(TabletPeer* tablet_peer) {
    Synchronizer synchronizer;
    CHECK_OK(tablet_peer->log_->TEST_SubmitFuncToAppendToken([&synchronizer, tablet_peer] {
      synchronizer.StatusCB(tablet_peer->log_->AllocateSegmentAndRollOver());
    }));
    return synchronizer.Wait();
  }

  Status ExecuteWriteAndRollLog(TabletPeer* tablet_peer, const WriteRequestPB& req) {
    ExecuteWrite(tablet_peer, req);
    return RollLog(tablet_peer);
  }

  // Execute insert requests and roll log after each one.
  Status ExecuteInsertsAndRollLogs(int num_inserts) {
    for (int i = 0; i < num_inserts; i++) {
      WriteRequestPB req;
      GenerateSequentialInsertRequest(&req);
      RETURN_NOT_OK(ExecuteWriteAndRollLog(tablet_peer_.get(), req));
    }

    return Status::OK();
  }

  // Execute delete requests and roll log after each one.
  Status ExecuteDeletesAndRollLogs(int num_deletes) {
    for (int i = 0; i < num_deletes; i++) {
      WriteRequestPB req;
      GenerateSequentialDeleteRequest(&req);
      CHECK_OK(ExecuteWriteAndRollLog(tablet_peer_.get(), req));
    }

    return Status::OK();
  }

  // Assert that the Log GC() anchor is earlier than the latest OpId in the Log.
  void AssertLogAnchorEarlierThanLogLatest() {
    int64_t earliest_index = ASSERT_RESULT(tablet_peer_->GetEarliestNeededLogIndex());
    auto last_log_opid = tablet_peer_->log_->GetLatestEntryOpId();
    ASSERT_LE(earliest_index, last_log_opid.index)
      << "Expected valid log anchor, got earliest opid: " << earliest_index
      << " (expected any value earlier than last log id: " << last_log_opid << ")";
  }

  // We disable automatic log GC. Don't leak those changes.
  google::FlagSaver flag_saver_;

  int32_t insert_counter_;
  int32_t delete_counter_;
  MetricRegistry metric_registry_;
  scoped_refptr<MetricEntity> table_metric_entity_;
  scoped_refptr<MetricEntity> tablet_metric_entity_;
  std::unique_ptr<rpc::Messenger> messenger_;
  std::unique_ptr<rpc::ProxyCache> proxy_cache_;
  std::unique_ptr<ThreadPool> raft_pool_;
  std::unique_ptr<ThreadPool> tablet_prepare_pool_;
  std::unique_ptr<ThreadPool> log_thread_pool_;
  std::unique_ptr<ThreadPool> flush_bootstrap_state_pool_;
  std::shared_ptr<TabletPeer> tablet_peer_;
  std::unique_ptr<consensus::MultiRaftManager> multi_raft_manager_;
  std::unique_ptr<rpc::ThreadPool> raft_notifications_pool_;
};

// Ensure that Log::GC() doesn't delete logs with anchors.
TEST_F(TabletPeerTest, TestLogAnchorsAndGC) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_log_min_seconds_to_retain) = 0;
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

  ASSERT_NO_FATALS(AssertLogAnchorEarlierThanLogLatest());

  // Ensure nothing gets deleted.
  ASSERT_OK(tablet_peer_->tablet()->Flush(tablet::FlushMode::kSync));
  int64_t min_log_index = ASSERT_RESULT(tablet_peer_->GetEarliestNeededLogIndex());
  ASSERT_OK(log->GC(min_log_index, &num_gced));
  ASSERT_EQ(2, num_gced) << "Earliest needed: " << min_log_index;

  // Flush RocksDB to ensure that we don't have OpId in anchors.
  ASSERT_OK(tablet_peer_->tablet()->Flush(tablet::FlushMode::kSync));

  // The first two segments should be deleted.
  // The last is anchored due to the commit in the last segment being the last
  // OpId in the log.
  int32_t earliest_needed = 0;
  auto total_segments = log->GetLogReader()->num_segments();
  min_log_index = ASSERT_RESULT(tablet_peer_->GetEarliestNeededLogIndex());
  ASSERT_OK(log->GC(min_log_index, &num_gced));
  ASSERT_EQ(earliest_needed, num_gced) << "earliest needed: " << min_log_index;
  ASSERT_OK(log->GetLogReader()->GetSegmentsSnapshot(&segments));
  ASSERT_EQ(total_segments - earliest_needed, segments.size());
}

// Ensure that Log::GC() doesn't delete logs when the DMS has an anchor.
TEST_F(TabletPeerTest, TestDMSAnchorPreventsLogGC) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_log_min_seconds_to_retain) = 0;
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
  ASSERT_OK(tablet_peer_->tablet()->Flush(tablet::FlushMode::kSync));

  int32_t earliest_needed = 1;
  auto total_segments = log->GetLogReader()->num_segments();
  int64_t min_log_index = ASSERT_RESULT(tablet_peer_->GetEarliestNeededLogIndex());
  ASSERT_OK(log->GC(min_log_index, &num_gced));
  // We will only GC 1, and have 1 left because the earliest needed OpId falls
  // back to the latest OpId written to the Log if no anchors are set.
  ASSERT_EQ(earliest_needed, num_gced);
  ASSERT_OK(log->GetLogReader()->GetSegmentsSnapshot(&segments));
  ASSERT_EQ(total_segments - earliest_needed, segments.size());

  auto id = log->GetLatestEntryOpId();
  LOG(INFO) << "Before: " << id;

  // We currently have no anchors and the last operation in the log is 0.3
  // Before the below was ExecuteDeletesAndRollLogs(1) but that was breaking
  // what I think is a wrong assertion.
  // I.e. since 0.4 is the last operation that we know is in memory 0.4 is the
  // last anchor we expect _and_ it's the last op in the log.
  // Only if we apply two operations is the last anchored operation and the
  // last operation in the log different.

  // Execute a mutation.
  ASSERT_OK(ExecuteDeletesAndRollLogs(2));
  ASSERT_NO_FATALS(AssertLogAnchorEarlierThanLogLatest());

  total_segments += 1;
  ASSERT_OK(log->GetLogReader()->GetSegmentsSnapshot(&segments));
  ASSERT_EQ(total_segments, segments.size());

  // Execute another couple inserts, but Flush it so it doesn't anchor.
  ASSERT_OK(ExecuteInsertsAndRollLogs(2));
  total_segments += 2;
  ASSERT_OK(log->GetLogReader()->GetSegmentsSnapshot(&segments));
  ASSERT_EQ(total_segments, segments.size());

  // Ensure the delta and last insert remain in the logs, anchored by the delta.
  // Note that this will allow GC of the 2nd insert done above.
  ASSERT_OK(tablet_peer_->tablet()->Flush(tablet::FlushMode::kSync));
  earliest_needed = 4;
  std::string details;
  min_log_index = ASSERT_RESULT(tablet_peer_->GetEarliestNeededLogIndex(&details));
  LOG(INFO) << details;
  ASSERT_OK(log->GC(min_log_index, &num_gced));
  ASSERT_EQ(earliest_needed, num_gced);
  ASSERT_OK(log->GetLogReader()->GetSegmentsSnapshot(&segments));
  ASSERT_EQ(total_segments - earliest_needed, segments.size());

  earliest_needed = 0;
  total_segments = log->GetLogReader()->num_segments();
  // We should only hang onto one segment due to no anchors.
  // The last log OpId is the commit in the last segment, so it only anchors
  // that segment, not the previous, because it's not the first OpId in the
  // segment.
  min_log_index = ASSERT_RESULT(tablet_peer_->GetEarliestNeededLogIndex());
  ASSERT_OK(log->GC(min_log_index, &num_gced));
  ASSERT_EQ(earliest_needed, num_gced);
  ASSERT_OK(log->GetLogReader()->GetSegmentsSnapshot(&segments));
  ASSERT_EQ(total_segments - earliest_needed, segments.size());
}

// Ensure that Log::GC() doesn't compact logs with OpIds of active transactions.
TEST_F(TabletPeerTest, TestActiveOperationPreventsLogGC) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_log_min_seconds_to_retain) = 0;
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

TEST_F(TabletPeerTest, TestGCEmptyLog) {
  ConsensusBootstrapInfo info;
  ASSERT_OK(tablet_peer_->Start(info));
  // We don't wait on consensus on purpose.
  ASSERT_OK(tablet_peer_->RunLogGC());
}

TEST_F(TabletPeerTest, TestAddTableUpdatesLastChangeMetadataOpId) {
  auto tablet = ASSERT_RESULT(tablet_peer_->shared_tablet_safe());
  TableInfoPB table_info;
  table_info.set_table_id("00004000000030008000000000004020");
  table_info.set_table_name("test");
  table_info.set_table_type(PGSQL_TABLE_TYPE);
  ColumnSchema col("a", DataType::UINT32, ColumnKind::RANGE_ASC_NULL_FIRST);
  ColumnId col_id(1);
  Schema schema({col}, {col_id});
  SchemaToPB(schema, table_info.mutable_schema());
  OpId op_id(100, 5);
  ASSERT_OK(tablet->AddTable(table_info, op_id));
  ASSERT_EQ(tablet->metadata()->TEST_LastAppliedChangeMetadataOperationOpId(), op_id);
}

TEST_F(TabletPeerTest, TestRollLogAfterTabletPeerShutdown) {
  ASSERT_OK(tablet_peer_->Shutdown(
      ShouldAbortActiveTransactions::kFalse, DisableFlushOnShutdown::kFalse));
  auto s = tablet_peer_->log()->AsyncAllocateSegmentAndRollover();
  ASSERT_NOK_STR_CONTAINS(s, "Invalid log state");
}

class TabletPeerProtofBufSizeLimitTest : public TabletPeerTest {
 public:
  TabletPeerProtofBufSizeLimitTest() : TabletPeerTest(GetSimpleTestSchema()) {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_protobuf_message_total_bytes_limit) = kProtobufSizeLimit;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_rpc_max_message_size) = kMaxRpcMsgSize;
    // Avoid unnecessary log segments rolling.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_initial_log_segment_size_bytes) = kProtobufSizeLimit * 2;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_log_segment_size_bytes) = kProtobufSizeLimit * 2;
  }

  static constexpr auto kProtobufSizeLimit = 10_MB;
  static constexpr auto kMaxRpcMsgSize = kProtobufSizeLimit / 2;
};

TEST_F_EX(TabletPeerTest, MaxRaftBatchProtobufLimit, TabletPeerProtofBufSizeLimitTest) {
  constexpr auto kNumOps = 10;

  // Make sure batch of kNumOps operations is larger than kProtobufSizeLimit to test limit overflow.
  constexpr auto kValueSize = kProtobufSizeLimit / (kNumOps - 1);

  // Make sure we don't reach max_group_replicate_batch_size limit.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_max_group_replicate_batch_size) = kNumOps * 2;

  ConsensusBootstrapInfo info;
  ASSERT_OK(StartPeer(info));

  std::string value(kValueSize, 'X');

  std::vector<WriteRequestPB> requests(kNumOps);
  std::vector<WriteResponsePB> responses(kNumOps);
  std::vector<std::unique_ptr<WriteQuery>> queries;
  queries.reserve(kNumOps);
  CountDownLatch latch(kNumOps);

  auto* const tablet_peer = tablet_peer_.get();

  for (int i = 0; i < kNumOps; ++i) {
    auto* req = &requests[i];
    auto* resp = &responses[i];

    req->set_tablet_id(tablet()->tablet_id());
    AddTestRowInsert(i, i, value, req);
    auto query = CreateQuery(
        tablet_peer, *req, resp, MakeLatchOperationCompletionCallback(&latch, resp));
    queries.push_back(std::move(query));
  }

  for (auto& query : queries) {
    tablet_peer->WriteAsync(std::move(query));
  }
  latch.Wait();

  for (size_t i = 0; i < responses.size(); ++i) {
    const auto& resp = responses[i];
    ASSERT_FALSE(responses[i].has_error()) << "\n Response[" << i << "]:\n" << resp.DebugString();
  }

  ASSERT_OK(RollLog(tablet_peer_.get()));

  auto* log = tablet_peer_->log();

  log::SegmentSequence segments;
  ASSERT_OK(log->GetLogReader()->GetSegmentsSnapshot(&segments));

  for (auto& segment : segments) {
    auto entries = segment->ReadEntries();
    ASSERT_OK(entries.status);

    size_t current_batch_size = 0;
    int64_t current_batch_offset = 0;
    for (const auto& meta : entries.entry_metadata) {
      if (meta.offset == current_batch_offset) {
        ++current_batch_size;
      } else {
        current_batch_offset = meta.offset;
        current_batch_size = 1;
      }
      ASSERT_LE(current_batch_size, kProtobufSizeLimit);
    }
  }
}

TEST_F_EX(TabletPeerTest, SingleOpExceedsRpcMsgLimit, TabletPeerProtofBufSizeLimitTest) {
  // Make sure batch of kNumOps operations is larger than kMaxRpcMsgSize to test limit overflow.
  constexpr auto kValueSize = kMaxRpcMsgSize * 11 / 10;

  ConsensusBootstrapInfo info;
  ASSERT_OK(StartPeer(info));

  std::string value(kValueSize, 'X');

  WriteRequestPB req;
  WriteResponsePB resp;
  CountDownLatch latch(1);

  auto* const tablet_peer = tablet_peer_.get();

  req.set_tablet_id(tablet()->tablet_id());
  AddTestRowInsert(1, 1, value, &req);
  auto query = CreateQuery(
      tablet_peer, req, &resp, MakeLatchOperationCompletionCallback(&latch, &resp));

  tablet_peer->WriteAsync(std::move(query));
  latch.Wait();

  ASSERT_TRUE(resp.has_error()) << "\n Response:\n" << resp.DebugString();
}

class TabletBootstrapStateFlusherTest : public TabletPeerTest {
 protected:
  TabletBootstrapStateFlusherTest() : TabletPeerTest(GetSimpleTestSchema()) {}
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_flush_retryable_requests) = true;
    TabletPeerTest::SetUp();
    StartTabletPeer();
  }

  void StartTabletPeer() {
    ConsensusBootstrapInfo info;
    ASSERT_OK(StartPeer(info));
  }

  Status WaitForFlushState(TabletBootstrapFlushState state) {
    return WaitFor([&] {
      return tablet_peer_->TEST_TabletBootstrapStateFlusherState() == state;
    }, 10s, Format("Wait for flush state to be $0", state));
  }
};

TEST_F(TabletBootstrapStateFlusherTest, RejectFlushOrSubmitIfFlushingOrSubmitted) {
  TestThreadHolder thread_holder;

  // If a flush is in progress, the next flush or submit should just return with
  // AlreadyPresent error.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_flushing_bootstrap_state) = true;
  thread_holder.AddThreadFunctor([&] {
    ASSERT_OK(tablet_peer_->FlushBootstrapState());
  });
  ASSERT_OK(WaitForFlushState(TabletBootstrapFlushState::kFlushing));
  Status s = tablet_peer_->FlushBootstrapState();
  ASSERT_TRUE(s.IsAlreadyPresent());
  s = tablet_peer_->SubmitFlushBootstrapStateTask();
  ASSERT_TRUE(s.IsAlreadyPresent());
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_flushing_bootstrap_state) = false;
  ASSERT_OK(WaitForFlushState(TabletBootstrapFlushState::kFlushIdle));

  // If a flush is submitted, the next flush or submit should just return with AlreadyPresent error.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_submitting_flush_bootstrap_state) = true;
  thread_holder.AddThreadFunctor([&] {
    ASSERT_OK(tablet_peer_->SubmitFlushBootstrapStateTask());
  });
  ASSERT_OK(WaitForFlushState(TabletBootstrapFlushState::kFlushSubmitted));
  s = tablet_peer_->FlushBootstrapState();
  ASSERT_TRUE(s.IsAlreadyPresent());
  s = tablet_peer_->SubmitFlushBootstrapStateTask();
  ASSERT_TRUE(s.IsAlreadyPresent());
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_submitting_flush_bootstrap_state) = false;
  ASSERT_OK(WaitForFlushState(TabletBootstrapFlushState::kFlushIdle));

  thread_holder.WaitAndStop(10s);
}

TEST_F(TabletBootstrapStateFlusherTest, WaitFlushDoneBeforeCopy) {
  TestThreadHolder thread_holder;

  std::atomic<int> finish_order{0};
  // If a flush is in progress, the next copy should wait for flush done.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_flushing_bootstrap_state) = true;
  thread_holder.AddThreadFunctor([&] {
    ASSERT_OK(tablet_peer_->FlushBootstrapState());
    auto order = finish_order.fetch_add(1);
    ASSERT_EQ(order, 0);
  });
  ASSERT_OK(WaitForFlushState(TabletBootstrapFlushState::kFlushing));
  thread_holder.AddThreadFunctor([&] {
    // There's no data to flush, so CopyBootstrapStateTo should do nothing.
    auto res = tablet_peer_->CopyBootstrapStateTo("fake_path");
    ASSERT_FALSE(res.ok());
    ASSERT_TRUE(res.status().IsNotFound());
    ASSERT_TRUE(res.status().message().Contains("Bootstrap state has not been flushed"));
    auto order = finish_order.fetch_add(1);
    ASSERT_EQ(order, 1);
  });
  SleepFor(1s);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_flushing_bootstrap_state) = false;
  ASSERT_OK(WaitForFlushState(TabletBootstrapFlushState::kFlushIdle));
  thread_holder.WaitAndStop(10s);

  finish_order.store(0);
  // If a submit is in progress, the next copy should wait for flush done.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_submitting_flush_bootstrap_state) = true;
  thread_holder.AddThreadFunctor([&] {
    ASSERT_OK(tablet_peer_->SubmitFlushBootstrapStateTask());
    auto order = finish_order.fetch_add(1);
    ASSERT_EQ(order, 0);
  });
  ASSERT_OK(WaitForFlushState(TabletBootstrapFlushState::kFlushSubmitted));
  thread_holder.AddThreadFunctor([&] {
    // There's no data to flush, so CopyBootstrapStateTo should do nothing.
    auto res = tablet_peer_->CopyBootstrapStateTo("fake_path");
    ASSERT_FALSE(res.ok());
    ASSERT_TRUE(res.status().IsNotFound());
    ASSERT_TRUE(res.status().message().Contains("Bootstrap state has not been flushed"));
    auto order = finish_order.fetch_add(1);
    ASSERT_EQ(order, 1);
  });
  SleepFor(1s);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_submitting_flush_bootstrap_state) = false;
  ASSERT_OK(WaitForFlushState(TabletBootstrapFlushState::kFlushIdle));

  thread_holder.WaitAndStop(10s);
}

TEST_F(TabletBootstrapStateFlusherTest, WaitCopyDoneBeforeFlushOrSubmit) {
  TestThreadHolder thread_holder;

  std::atomic<int> finish_order{0};
  // If a flush is in progress, the next copy should wait for flush done.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_copying_bootstrap_state) = true;
  thread_holder.AddThreadFunctor([&] {
    auto res = tablet_peer_->CopyBootstrapStateTo("fake_path");
    ASSERT_FALSE(res.ok());
    ASSERT_TRUE(res.status().IsNotFound());
    ASSERT_TRUE(res.status().message().Contains("Bootstrap state has not been flushed"));
    auto order = finish_order.fetch_add(1);
    ASSERT_EQ(order, 0);
  });
  ASSERT_OK(WaitForFlushState(TabletBootstrapFlushState::kReading));
  thread_holder.AddThreadFunctor([&] {
    ASSERT_OK(tablet_peer_->FlushBootstrapState());
    auto order = finish_order.fetch_add(1);
    ASSERT_EQ(order, 1);
  });
  SleepFor(1s);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_copying_bootstrap_state) = false;
  ASSERT_OK(WaitForFlushState(TabletBootstrapFlushState::kFlushIdle));
  thread_holder.WaitAndStop(10s);

  finish_order.store(0);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_copying_bootstrap_state) = true;
  thread_holder.AddThreadFunctor([&] {
    auto res = tablet_peer_->CopyBootstrapStateTo("fake_path");
    ASSERT_FALSE(res.ok());
    ASSERT_TRUE(res.status().IsNotFound());
    ASSERT_TRUE(res.status().message().Contains("Bootstrap state has not been flushed"));
    auto order = finish_order.fetch_add(1);
    ASSERT_EQ(order, 0);
  });
  ASSERT_OK(WaitForFlushState(TabletBootstrapFlushState::kReading));
  thread_holder.AddThreadFunctor([&] {
    ASSERT_OK(tablet_peer_->SubmitFlushBootstrapStateTask());
    auto order = finish_order.fetch_add(1);
    ASSERT_EQ(order, 1);
  });
  SleepFor(1s);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_copying_bootstrap_state) = false;
  ASSERT_OK(WaitForFlushState(TabletBootstrapFlushState::kFlushIdle));

  thread_holder.WaitAndStop(10s);
}

TEST_F(TabletBootstrapStateFlusherTest, WaitFlushIdleBeforeShutdown) {
  TestThreadHolder thread_holder;

  std::atomic<int> finish_order{0};
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_flushing_bootstrap_state) = true;
  thread_holder.AddThreadFunctor([&] {
    Status s = tablet_peer_->FlushBootstrapState();
    ASSERT_TRUE(s.ok() || s.IsIllegalState());
    auto order = finish_order.fetch_add(1);
    ASSERT_EQ(order, 0);
  });
  ASSERT_OK(WaitForFlushState(TabletBootstrapFlushState::kFlushing));
  thread_holder.AddThreadFunctor([&] {
    WARN_NOT_OK(
        tablet_peer_->Shutdown(
            ShouldAbortActiveTransactions::kFalse, DisableFlushOnShutdown::kFalse),
            "Tablet peer shutdown failed");
    auto order = finish_order.fetch_add(1);
    ASSERT_EQ(order, 1);
  });
  SleepFor(1s);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_flushing_bootstrap_state) = false;

  thread_holder.WaitAndStop(10s);
}

class TabletBootstrapStateFlusherWithLargeBatchTest : public TabletBootstrapStateFlusherTest {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_log_segment_size_bytes) = 5_KB;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_initial_log_segment_size_bytes) = 1_KB;
    TabletBootstrapStateFlusherTest::SetUp();
  }
};

TEST_F_EX(TabletBootstrapStateFlusherTest,
          TestLogRolloverWithSingleBatch,
          TabletBootstrapStateFlusherWithLargeBatchTest) {
  const int kNumOps = 100;
  std::string value(ANNOTATE_UNPROTECTED_READ(FLAGS_log_segment_size_bytes) + 1, 'X');

  std::vector<WriteRequestPB> requests(kNumOps);
  std::vector<WriteResponsePB> responses(kNumOps);
  std::vector<std::unique_ptr<WriteQuery>> queries;
  queries.reserve(kNumOps);
  CountDownLatch latch(kNumOps);

  auto* const tablet_peer = tablet_peer_.get();

  for (int i = 0; i < kNumOps; ++i) {
    auto* req = &requests[i];
    auto* resp = &responses[i];

    req->set_tablet_id(tablet()->tablet_id());
    AddTestRowInsert(i, i, value, req);
    auto query = CreateQuery(
        tablet_peer, *req, resp, MakeLatchOperationCompletionCallback(&latch, resp));
    queries.push_back(std::move(query));
  }

  // Pause at the first flush, and following submission or flush should be skipped.
  // Otherwise, it will cause deadlock, see https://github.com/yugabyte/yugabyte-db/issues/18946
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_flushing_bootstrap_state) = true;
  for (auto& query : queries) {
    tablet_peer->WriteAsync(std::move(query));
    SleepFor(10ms);
  }
  latch.Wait();
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_pause_before_flushing_bootstrap_state) = false;
}

} // namespace tablet
} // namespace yb
