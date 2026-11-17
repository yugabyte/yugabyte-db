// Copyright (c) YugabyteDB, Inc.
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

#include <algorithm>
#include <future>
#include <unordered_set>

#include "yb/cdc/cdc_service.pb.h"

#include "yb/client/client.h"
#include "yb/client/snapshot_test_util.h"

#include "yb/common/ddl_mode-test-util.h"
#include "yb/common/hybrid_time.h"
#include "yb/common/opid.h"

#include "yb/consensus/consensus.messages.h"
#include "yb/consensus/raft_consensus.h"

#include "yb/integration-tests/mini_cluster.h"

#include "yb/master/catalog_manager.h"
#include "yb/master/master.h"
#include "yb/master/mini_master.h"
#include "yb/master/sys_catalog.h"
#include "yb/master/sys_catalog_pusher.h"

#include "yb/rpc/rpc_controller.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"

#include "yb/tserver/backup.pb.h"
#include "yb/tserver/local_catalog.proxy.h"
#include "yb/tserver/local_catalog_service.h"
#include "yb/tserver/mini_tablet_server.h"
#include "yb/tserver/tablet_server.h"

#include "yb/util/backoff_waiter.h"
#include "yb/util/mem_tracker.h"
#include "yb/util/metrics.h"
#include "yb/util/scope_exit.h"
#include "yb/util/test_macros.h"
#include "yb/util/test_thread_holder.h"

#include "yb/yql/pgwrapper/pg_mini_test_base.h"

using namespace std::literals;

DECLARE_bool(TEST_check_broadcast_address);
DECLARE_bool(TEST_local_catalog_answer_gap);
DECLARE_bool(TEST_local_catalog_answer_gap_behind);
DECLARE_bool(TEST_local_catalog_fail_pushes);
DECLARE_bool(TEST_local_catalog_push_fail_batch_build);
DECLARE_bool(TEST_local_catalog_record_pushes);
DECLARE_bool(enable_local_tserver_catalog);
DECLARE_bool(enable_ysql_operation_lease);
DECLARE_bool(ysql_enable_auto_analyze);
DECLARE_bool(ysql_yb_enable_invalidation_messages);
DECLARE_double(TEST_transaction_ignore_applying_probability);
DECLARE_int32(TEST_local_catalog_push_answer_delay_ms);
DECLARE_int32(TEST_local_catalog_push_delay_before_send_ms);
DECLARE_int32(TEST_local_catalog_push_delay_before_term_check_ms);
DECLARE_int32(TEST_local_catalog_push_empty_at_read);
DECLARE_int32(TEST_local_catalog_push_log_gone_at_read);
DECLARE_int32(TEST_local_catalog_push_reseed_answer_delay_ms);
DECLARE_int32(local_catalog_push_interval_ms);
DECLARE_int32(local_catalog_push_max_inflight_rpcs);
DECLARE_int32(log_min_seconds_to_retain);
DECLARE_int32(log_min_segments_to_retain);
DECLARE_int32(master_ts_rpc_timeout_ms);
DECLARE_int32(retryable_request_timeout_secs);
DECLARE_uint64(consensus_max_batch_size_bytes);
DECLARE_uint64(local_catalog_push_batch_max_bytes);
DECLARE_uint64(transaction_resend_applying_interval_usec);

METRIC_DECLARE_counter(local_catalog_push_bytes);
METRIC_DECLARE_counter(local_catalog_push_cycle_failures);
METRIC_DECLARE_counter(local_catalog_push_cycles);
METRIC_DECLARE_counter(local_catalog_push_cycles_skipped);
METRIC_DECLARE_counter(local_catalog_push_records);
METRIC_DECLARE_counter(local_catalog_push_reseeds);
METRIC_DECLARE_counter(local_catalog_push_rpc_failures);
METRIC_DECLARE_counter(local_catalog_push_rpcs);
METRIC_DECLARE_counter(local_catalog_push_stalls);
METRIC_DECLARE_event_stats(local_catalog_push_batch_bytes);
METRIC_DECLARE_event_stats(local_catalog_push_cycle_duration);
METRIC_DECLARE_event_stats(local_catalog_push_resolve_intents_wait);
METRIC_DECLARE_gauge_uint64(local_catalog_push_max_target_lag_ops);
METRIC_DECLARE_gauge_uint64(local_catalog_push_safe_time_lag_ms);
METRIC_DECLARE_gauge_uint64(local_catalog_push_targets);
METRIC_DECLARE_gauge_uint64(local_catalog_push_targets_caught_up);
METRIC_DECLARE_gauge_uint64(local_catalog_push_targets_without_copy);

namespace yb::pgwrapper {

using PushResponsePB = tserver::PushCatalogChangesResponsePB;

class PgLocalCatalogTest : public PgMiniTestBase {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_local_tserver_catalog) = LocalCatalogEnabled();
    // No timer cycle pushes during a test unless the test sets a shorter interval.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_local_catalog_push_interval_ms) = PushIntervalMs();
    PgMiniTestBase::SetUp();
  }

  virtual bool LocalCatalogEnabled() { return true; }
  virtual int32_t PushIntervalMs() { return 3600 * 1000; }

  size_t NumTabletServers() override { return 1; }

  Result<PushResponsePB> Push(
      const OpId& from_op_id, const OpId& to_op_id, HybridTime safe_time = HybridTime::kInvalid) {
    auto proxy = cluster_->GetTServerProxy<tserver::LocalCatalogServiceProxy>(0);
    tserver::PushCatalogChangesRequestPB req;
    from_op_id.ToPB(req.mutable_from_op_id());
    to_op_id.ToPB(req.mutable_to_op_id());
    if (safe_time.is_valid()) {
      req.set_safe_time(safe_time.ToUint64());
    }
    PushResponsePB resp;
    rpc::RpcController controller;
    controller.set_timeout(10s * kTimeMultiplier);
    RETURN_NOT_OK(proxy.PushCatalogChanges(req, &resp, &controller));
    return resp;
  }

  tserver::LocalCatalogServiceImpl& LocalCatalogService(size_t ts_idx = 0) {
    return *cluster_->mini_tablet_server(ts_idx)->server()->TEST_GetLocalCatalogService();
  }
};

// The test receiver records only a batch that starts at its position, acknowledges a batch it is
// already past, and answers every push with its position and safe time.
TEST_F(PgLocalCatalogTest, TestReceiverAppliesBatchesInOrder) {
  auto resp = ASSERT_RESULT(Push(OpId::Min(), OpId(1, 5)));
  ASSERT_EQ(resp.code(), PushResponsePB::NO_COPY);

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_local_catalog_record_pushes) = true;
  const auto safe_time1 = HybridTime::FromMicros(1000);
  const auto safe_time2 = HybridTime::FromMicros(2000);

  resp = ASSERT_RESULT(Push(OpId::Min(), OpId(1, 5), safe_time1));
  ASSERT_EQ(resp.code(), PushResponsePB::OK);
  ASSERT_EQ(OpId::FromPB(resp.applied_op_id()), OpId(1, 5));
  ASSERT_EQ(HybridTime(resp.safe_time()), safe_time1);

  resp = ASSERT_RESULT(Push(OpId(1, 7), OpId(1, 9)));
  ASSERT_EQ(resp.code(), PushResponsePB::GAP);
  ASSERT_EQ(OpId::FromPB(resp.applied_op_id()), OpId(1, 5));
  ASSERT_EQ(HybridTime(resp.safe_time()), safe_time1);

  resp = ASSERT_RESULT(Push(OpId::Min(), OpId(1, 3)));
  ASSERT_EQ(resp.code(), PushResponsePB::OK);
  ASSERT_EQ(OpId::FromPB(resp.applied_op_id()), OpId(1, 5));

  resp = ASSERT_RESULT(Push(OpId(1, 5), OpId(1, 9), safe_time2));
  ASSERT_EQ(resp.code(), PushResponsePB::OK);
  ASSERT_EQ(OpId::FromPB(resp.applied_op_id()), OpId(1, 9));
  ASSERT_EQ(HybridTime(resp.safe_time()), safe_time2);

  auto pushes = LocalCatalogService().TEST_TakeRecordedPushes();
  ASSERT_EQ(pushes.size(), 2);
  ASSERT_EQ(OpId::FromPB(pushes[0].to_op_id()), OpId(1, 5));
  ASSERT_EQ(OpId::FromPB(pushes[1].to_op_id()), OpId(1, 9));
}

using PushCycleResult = master::SysCatalogPusher::PushCycleResult;
using RecordPB = cdc::CDCRecordPB;

// Returns the commit time of the transaction that wrote a row containing text, if its apply record
// is among the records.
std::optional<HybridTime> FindCommit(
    const std::vector<RecordPB>& records, const std::string& text) {
  std::unordered_set<std::string> txn_ids;
  for (const auto& record : records) {
    if (record.operation() != RecordPB::WRITE || !record.has_transaction_state()) {
      continue;
    }
    for (const auto& change : record.changes()) {
      if (change.value().binary_value().find(text) != std::string::npos) {
        txn_ids.insert(record.transaction_state().transaction_id());
      }
    }
  }
  for (const auto& record : records) {
    if (record.operation() == RecordPB::APPLY &&
        record.transaction_state().has_commit_hybrid_time() &&
        txn_ids.contains(record.transaction_state().transaction_id())) {
      return HybridTime(record.transaction_state().commit_hybrid_time());
    }
  }
  return std::nullopt;
}

bool HasTransactionalRow(const std::vector<RecordPB>& records, const std::string& text) {
  for (const auto& record : records) {
    if (record.operation() != RecordPB::WRITE || !record.has_transaction_state()) {
      continue;
    }
    for (const auto& change : record.changes()) {
      if (change.value().binary_value().find(text) != std::string::npos) {
        return true;
      }
    }
  }
  return false;
}

bool HasNonTransactionalRow(const std::vector<RecordPB>& records, const std::string& text) {
  for (const auto& record : records) {
    if (record.operation() != RecordPB::WRITE || record.has_transaction_state()) {
      continue;
    }
    for (const auto& change : record.changes()) {
      if (change.value().binary_value().find(text) != std::string::npos) {
        return true;
      }
    }
  }
  return false;
}

std::vector<RecordPB> RecordsOf(const std::vector<tserver::PushCatalogChangesRequestPB>& pushes) {
  std::vector<RecordPB> records;
  for (const auto& push : pushes) {
    records.insert(records.end(), push.records().begin(), push.records().end());
  }
  return records;
}

// The master leader pushes its system catalog to every tserver, whose test receivers record the
// batches. Cycles run only when the test runs them.
class PgLocalCatalogPushTest : public PgLocalCatalogTest {
 protected:
  void SetUp() override {
    ToggleDDLMode(UseLegacyDdlMode());
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_invalidation_messages) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_local_catalog_record_pushes) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_check_broadcast_address) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_auto_analyze) = false;
    PgLocalCatalogTest::SetUp();
    ASSERT_NE(LeaderMaster().catalog_manager_impl().sys_catalog_pusher(), nullptr);
  }

  // Legacy DDL mode turns object locking off.
  virtual bool UseLegacyDdlMode() { return false; }

  master::MiniMaster& LeaderMaster() { return *CHECK_RESULT(cluster_->GetLeaderMiniMaster()); }

  // The pusher and metrics of the given master, or of the leader master.
  master::SysCatalogPusher& Pusher(master::MiniMaster* master = nullptr) {
    return *(master ? *master : LeaderMaster()).catalog_manager_impl().sys_catalog_pusher();
  }

  template <class MetricType>
  MetricType& PushMetric(const MetricPrototype& prototype, master::MiniMaster* master = nullptr) {
    auto metric = (master ? *master : LeaderMaster()).master()->metric_entity_cluster()
        ->FindOrNull<MetricType>(prototype);
    CHECK(metric) << prototype.name();
    return *metric;
  }

  int64_t PushCounter(const CounterPrototype& prototype, master::MiniMaster* master = nullptr) {
    return PushMetric<Counter>(prototype, master).value();
  }

  template <template <class> class GaugeType = AtomicGauge>
  uint64_t PushGauge(
      const GaugePrototype<uint64_t>& prototype, master::MiniMaster* master = nullptr) {
    return PushMetric<GaugeType<uint64_t>>(prototype, master).value();
  }

  uint64_t PushStatsCount(
      const EventStatsPrototype& prototype, master::MiniMaster* master = nullptr) {
    return PushMetric<EventStats>(prototype, master).TotalCount();
  }

  MemTrackerPtr PushMemTracker() {
    return LeaderMaster().master()->mem_tracker()->FindChild("SysCatalogPusher");
  }

  // A timer cycle can hold a batch for a moment, but a batch left charged stays charged.
  Status WaitForMemoryReleased() {
    return WaitFor(
        [this] { return PushMemTracker()->consumption() == 0; }, 30s * kTimeMultiplier,
        "Batch memory released");
  }

  std::string TsUuid(size_t ts_idx) {
    return cluster_->mini_tablet_server(ts_idx)->server()->permanent_uuid();
  }

  Result<OpId> CommittedOpId() {
    auto consensus =
        VERIFY_RESULT(LeaderMaster().sys_catalog().tablet_peer()->GetRaftConsensus());
    return consensus->GetLastCommittedOpId();
  }

  // Runs push cycles until every copy is caught up, and returns the last cycle's result.
  Result<PushCycleResult> RunCycleUntilCaughtUp() {
    return RunCycleUntil([this](const PushCycleResult& result) {
      return result.caught_up_ts_uuids.size() == cluster_->num_tablet_servers();
    });
  }

  // Runs push cycles until one returns a result that satisfies the condition.
  Result<PushCycleResult> RunCycleUntil(std::function<bool(const PushCycleResult&)> condition) {
    std::optional<PushCycleResult> result;
    RETURN_NOT_OK(WaitFor(
        [this, &result, &condition]() -> Result<bool> {
          auto cycle = Pusher().TEST_RunCycle();
          if (!cycle.ok() && cycle.status().IsTryAgain()) {
            return false;
          }
          auto value = VERIFY_RESULT(std::move(cycle));
          if (!value) {
            return false;
          }
          result = std::move(*value);
          return condition(*result);
        },
        60s * kTimeMultiplier, "Push cycle condition"));
    return std::move(*result);
  }

  // Sets every copy at the current position, as if it had just been bootstrapped, pushes until
  // every copy is caught up, and returns the position the copies reached.
  Result<OpId> SeedCopies() {
    const auto position = VERIFY_RESULT(CommittedOpId());
    for (size_t i = 0; i != cluster_->num_tablet_servers(); ++i) {
      LocalCatalogService(i).TEST_SetPosition(position);
    }
    RETURN_NOT_OK(RunCycleUntilCaughtUp());
    for (size_t i = 0; i != cluster_->num_tablet_servers(); ++i) {
      LocalCatalogService(i).TEST_TakeRecordedPushes();
    }
    const auto state = Pusher().TEST_GetTargetState(TsUuid(0));
    SCHECK(state && state->position, IllegalState, "Seeded copy has no position");
    return *state->position;
  }

  // Sets the copies behind by a few DDLs, with every log entry pushed in a batch of its own.
  Result<OpId> FallBehindInSmallBatches() {
    const auto position = VERIFY_RESULT(SeedCopies());
    auto conn = VERIFY_RESULT(Connect());
    for (int i = 0; i != 3; ++i) {
      RETURN_NOT_OK(conn.ExecuteFormat("CREATE TABLE local_catalog_t$0 (k INT PRIMARY KEY)", i));
    }
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_local_catalog_push_batch_max_bytes) = 1;
    return position;
  }

  // Checks that the pushes run in order from the position to the end of the cycle, in more than
  // one batch, and that only the last carries the cycle's safe time.
  static void CheckBatchChain(
      const std::vector<tserver::PushCatalogChangesRequestPB>& pushes, const OpId& from,
      const PushCycleResult& result) {
    ASSERT_GT(pushes.size(), 1);
    auto position = from;
    for (size_t i = 0; i != pushes.size(); ++i) {
      const auto& push = pushes[i];
      SCOPED_TRACE(Format("push $0: $1", i, push.ShortDebugString()));
      ASSERT_FALSE(push.reseed_required());
      ASSERT_EQ(OpId::FromPB(push.from_op_id()), position);
      position = OpId::FromPB(push.to_op_id());
      ASSERT_EQ(push.has_safe_time(), i + 1 == pushes.size());
    }
    ASSERT_EQ(position.index, result.end_index);
    ASSERT_EQ(HybridTime(pushes.back().safe_time()), result.safe_time);
  }
};

// A copy far enough behind is caught up in one cycle by a chain of batches, each built from the
// position the copy answered the last one with.
TEST_F(PgLocalCatalogPushTest, LongCatchUpIsSplitIntoBatches) {
  const auto from = ASSERT_RESULT(FallBehindInSmallBatches());
  const auto rpcs = PushCounter(METRIC_local_catalog_push_rpcs);
  const auto batches = PushStatsCount(METRIC_local_catalog_push_batch_bytes);
  const auto stalls = PushCounter(METRIC_local_catalog_push_stalls);
  const auto reseeds = PushCounter(METRIC_local_catalog_push_reseeds);

  const auto result = ASSERT_RESULT(RunCycleUntilCaughtUp());
  const auto pushes = LocalCatalogService().TEST_TakeRecordedPushes();
  ASSERT_NO_FATALS(CheckBatchChain(pushes, from, result));
  const auto records = RecordsOf(pushes);
  for (int i = 0; i != 3; ++i) {
    const auto commit = FindCommit(records, Format("local_catalog_t$0", i));
    ASSERT_TRUE(commit);
    ASSERT_GE(result.safe_time, *commit);
  }

  ASSERT_EQ(PushCounter(METRIC_local_catalog_push_rpcs) - rpcs, std::ssize(pushes));
  ASSERT_EQ(PushStatsCount(METRIC_local_catalog_push_batch_bytes) - batches, pushes.size());
  ASSERT_EQ(PushCounter(METRIC_local_catalog_push_stalls), stalls);
  ASSERT_EQ(PushCounter(METRIC_local_catalog_push_reseeds), reseeds);
  ASSERT_OK(WaitForMemoryReleased());
}

// The cycle after a DDL commits pushes the DDL's rows and apply record, and certifies a safe time
// at or after the commit.
TEST_F(PgLocalCatalogPushTest, CycleAfterDdlPushesDdl) {
  ASSERT_OK(SeedCopies());
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE local_catalog_t (k INT PRIMARY KEY)"));

  const auto result = ASSERT_RESULT(RunCycleUntilCaughtUp());
  const auto pushes = LocalCatalogService().TEST_TakeRecordedPushes();
  ASSERT_TRUE(std::ranges::any_of(pushes, [&result](const auto& push) {
    return HybridTime(push.safe_time()) == result.safe_time;
  }));
  const auto commit = FindCommit(RecordsOf(pushes), "local_catalog_t");
  ASSERT_TRUE(commit);
  ASSERT_GE(result.safe_time, *commit);
}

// Every requested cycle certifies a new safe time.
TEST_F(PgLocalCatalogPushTest, RequestedCycleMovesSafeTime) {
  ASSERT_OK(SeedCopies());
  const auto cycles = PushCounter(METRIC_local_catalog_push_cycles);
  const auto rpcs = PushCounter(METRIC_local_catalog_push_rpcs);
  const auto durations = PushStatsCount(METRIC_local_catalog_push_cycle_duration);
  const auto first = ASSERT_RESULT(RunCycleUntilCaughtUp());
  const auto second = ASSERT_RESULT(RunCycleUntilCaughtUp());
  ASSERT_GT(second.safe_time, first.safe_time);
  ASSERT_GE(PushCounter(METRIC_local_catalog_push_cycles), cycles + 2);
  ASSERT_GE(PushCounter(METRIC_local_catalog_push_rpcs), rpcs + 2);
  ASSERT_GE(PushStatsCount(METRIC_local_catalog_push_cycle_duration), durations + 2);
}

// A requested cycle runs on the pusher thread without waiting for the push interval, and runs
// again when requested while the WAL has not moved and every copy is caught up.
TEST_F(PgLocalCatalogPushTest, RequestCycleRunsOnPusherThread) {
  ASSERT_OK(SeedCopies());
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE local_catalog_t (k INT PRIMARY KEY)"));

  std::vector<RecordPB> records;
  Pusher().RequestCycle();
  ASSERT_OK(WaitFor(
      [this, &records] {
        auto pushed = RecordsOf(LocalCatalogService().TEST_TakeRecordedPushes());
        records.insert(records.end(), pushed.begin(), pushed.end());
        return FindCommit(records, "local_catalog_t").has_value();
      },
      30s * kTimeMultiplier, "DDL pushed by a requested cycle"));

  // Waits until two cycles in a row end at the same index with every copy caught up, so that a
  // timed cycle would now be skipped.
  std::optional<int64_t> end_index;
  ASSERT_RESULT(RunCycleUntil([this, &end_index](const PushCycleResult& result) {
    const bool caught_up = result.caught_up_ts_uuids.size() == cluster_->num_tablet_servers();
    const bool quiet = caught_up && end_index == result.end_index;
    end_index = caught_up ? std::optional(result.end_index) : std::nullopt;
    return quiet;
  }));

  const auto cycles = PushCounter(METRIC_local_catalog_push_cycles);
  Pusher().RequestCycle();
  ASSERT_OK(WaitFor(
      [this, cycles] { return PushCounter(METRIC_local_catalog_push_cycles) > cycles; },
      30s * kTimeMultiplier, "Second requested cycle"));
  const auto state = Pusher().TEST_GetTargetState(TsUuid(0));
  ASSERT_TRUE(state && state->position);
  ASSERT_EQ(state->position->index, *end_index);
}

// A cycle that fails on the pusher thread is run again without waiting for the push interval.
TEST_F(PgLocalCatalogPushTest, FailedCycleIsRetried) {
  ASSERT_OK(SeedCopies());
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE local_catalog_t (k INT PRIMARY KEY)"));
  const auto failed_builds = Pusher().TEST_NumFailedBatchBuilds();

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_local_catalog_push_fail_batch_build) = true;
  auto restore = ScopeExit([] {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_local_catalog_push_fail_batch_build) = false;
  });
  Pusher().RequestCycle();
  ASSERT_OK(WaitFor(
      [this, failed_builds] { return Pusher().TEST_NumFailedBatchBuilds() > failed_builds; },
      30s * kTimeMultiplier, "Requested cycle fails"));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_local_catalog_push_fail_batch_build) = false;

  std::vector<RecordPB> records;
  ASSERT_OK(WaitFor(
      [this, &records] {
        auto pushed = RecordsOf(LocalCatalogService().TEST_TakeRecordedPushes());
        records.insert(records.end(), pushed.begin(), pushed.end());
        return FindCommit(records, "local_catalog_t").has_value();
      },
      30s * kTimeMultiplier, "DDL pushed by the retried cycle"));
}

// A copy that answers with a gap stalls, and the cycle leaves it.
TEST_F(PgLocalCatalogPushTest, GapAnswerIsStall) {
  const auto position = ASSERT_RESULT(SeedCopies());
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE local_catalog_t (k INT PRIMARY KEY)"));
  const auto stalls = PushCounter(METRIC_local_catalog_push_stalls);

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_local_catalog_answer_gap) = true;
  std::optional<PushCycleResult> result;
  ASSERT_OK(WaitFor(
      [this, &result]() -> Result<bool> {
        auto cycle = Pusher().TEST_RunCycle();
        if (!cycle.ok() && cycle.status().IsTryAgain()) {
          return false;
        }
        result = VERIFY_RESULT(std::move(cycle));
        return result.has_value();
      },
      30s * kTimeMultiplier, "Cycle run"));
  ASSERT_TRUE(result->caught_up_ts_uuids.empty());
  ASSERT_GT(PushCounter(METRIC_local_catalog_push_stalls), stalls);
  const auto state = Pusher().TEST_GetTargetState(TsUuid(0));
  ASSERT_TRUE(state && state->position);
  ASSERT_EQ(*state->position, position);
}

// A batch is charged to the pusher's memory tracker at its size while its push is in flight, and
// released once the push is answered.
TEST_F(PgLocalCatalogPushTest, BatchMemoryIsChargedWhileInFlight) {
  ASSERT_OK(SeedCopies());
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE local_catalog_t (k INT PRIMARY KEY)"));
  const auto mem_tracker = PushMemTracker();
  ASSERT_TRUE(mem_tracker);
  ASSERT_EQ(mem_tracker->consumption(), 0);

  TestThreadHolder thread_holder;
  // Declared after thread_holder so that a failed assertion clears the delay before the cycle is
  // joined.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_local_catalog_push_answer_delay_ms) =
      3000 * kTimeMultiplier;
  auto clear_delay = ScopeExit([] {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_local_catalog_push_answer_delay_ms) = 0;
  });
  auto promise = std::make_shared<std::promise<Result<std::optional<PushCycleResult>>>>();
  auto cycle = promise->get_future();
  thread_holder.AddThreadFunctor([this, promise] { promise->set_value(Pusher().TEST_RunCycle()); });

  int64_t charged = 0;
  ASSERT_OK(WaitFor(
      [&mem_tracker, &charged] {
        charged = mem_tracker->consumption();
        return charged > 0;
      },
      30s * kTimeMultiplier, "Batch charged"));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_local_catalog_push_answer_delay_ms) = 0;
  const auto result = ASSERT_RESULT(cycle.get());
  ASSERT_TRUE(result);
  thread_holder.JoinAll();

  // With a single target, only the first batch is held while its push is in flight.
  const auto pushes = LocalCatalogService().TEST_TakeRecordedPushes();
  ASSERT_FALSE(pushes.empty());
  ASSERT_EQ(charged, pushes.front().ByteSizeLong());
  ASSERT_EQ(mem_tracker->consumption(), 0);
}

// A copy whose position has a term other than the master's entry at that index is reseeded.
TEST_F(PgLocalCatalogPushTest, DivergedCopyIsReseeded) {
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE local_catalog_t (k INT PRIMARY KEY)"));
  const auto committed = ASSERT_RESULT(CommittedOpId());
  LocalCatalogService().TEST_SetPosition(OpId(committed.term + 1, committed.index - 1));
  const auto reseeds = PushCounter(METRIC_local_catalog_push_reseeds);
  const auto stalls = PushCounter(METRIC_local_catalog_push_stalls);

  ASSERT_OK(WaitFor(
      [this]() -> Result<bool> {
        auto cycle = Pusher().TEST_RunCycle();
        if (!cycle.ok() && !cycle.status().IsTryAgain()) {
          return cycle.status();
        }
        auto pushes = LocalCatalogService().TEST_TakeRecordedPushes();
        SCHECK(std::ranges::none_of(pushes, [](const auto& push) {
          return push.records_size() > 0;
        }), IllegalState, "Records pushed to a diverged copy");
        return std::ranges::any_of(pushes, [](const auto& push) {
          return push.reseed_required();
        });
      },
      30s * kTimeMultiplier, "Diverged copy reseeded"));
  ASSERT_GT(PushCounter(METRIC_local_catalog_push_reseeds), reseeds);
  ASSERT_EQ(PushCounter(METRIC_local_catalog_push_stalls), stalls);
}

// A DDL's rows are pushed while it is open. Once it commits, a cycle certifies a safe time at or
// after the commit only when the DDL's apply record is in the WAL, and pushes that record.
TEST_F(PgLocalCatalogPushTest, CycleWaitsForCommittedDdlApply) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_transaction_resend_applying_interval_usec) = 200000;
  ASSERT_OK(SeedCopies());
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("CREATE TABLE local_catalog_t (k INT PRIMARY KEY)"));
  const auto open_result = ASSERT_RESULT(RunCycleUntilCaughtUp());
  auto records = RecordsOf(LocalCatalogService().TEST_TakeRecordedPushes());
  ASSERT_TRUE(HasTransactionalRow(records, "local_catalog_t"));
  ASSERT_FALSE(FindCommit(records, "local_catalog_t"));

  using CycleResult = Result<std::optional<PushCycleResult>>;
  TestThreadHolder thread_holder;

  // The system catalog drops the coordinator's apply requests, so the DDL commits without its apply
  // record reaching the WAL. Declared after thread_holder so that a failed assertion releases the
  // apply record before the waiting cycle is joined.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_transaction_ignore_applying_probability) = 1;
  auto release_apply = ScopeExit([] {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_transaction_ignore_applying_probability) = 0;
  });
  ASSERT_OK(conn.Execute("COMMIT"));
  auto start_cycle = [this, &thread_holder] {
    auto promise = std::make_shared<std::promise<CycleResult>>();
    auto future = promise->get_future();
    thread_holder.AddThreadFunctor([this, promise] {
      promise->set_value(Pusher().TEST_RunCycle());
    });
    return future;
  };

  // A cycle that finishes while the apply record is held must certify a safe time before the
  // commit. Once the safe time passes the commit, a cycle waits for the apply record.
  std::vector<HybridTime> held_safe_times;
  auto timeouts = Pusher().TEST_NumResolveIntentsTimeouts();
  auto cycle = start_cycle();
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        if (Pusher().TEST_NumResolveIntentsTimeouts() > timeouts) {
          return true;
        }
        if (cycle.wait_for(0s) == std::future_status::timeout) {
          return false;
        }
        auto result = VERIFY_RESULT(cycle.get());
        SCHECK(result, IllegalState, "Master is not the leader");
        held_safe_times.push_back(result->safe_time);
        timeouts = Pusher().TEST_NumResolveIntentsTimeouts();
        cycle = start_cycle();
        return false;
      },
      30s * kTimeMultiplier, "A cycle waits for the apply record"));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_transaction_ignore_applying_probability) = 0;
  const auto result = ASSERT_RESULT(cycle.get());
  ASSERT_TRUE(result);
  ASSERT_EQ(result->caught_up_ts_uuids, std::vector{TsUuid(0)});
  auto applied_records = RecordsOf(LocalCatalogService().TEST_TakeRecordedPushes());
  records.insert(records.end(), applied_records.begin(), applied_records.end());
  const auto commit = FindCommit(records, "local_catalog_t");
  ASSERT_TRUE(commit);
  ASSERT_GE(result->safe_time, *commit);
  ASSERT_LT(open_result.safe_time, *commit);
  for (const auto& safe_time : held_safe_times) {
    ASSERT_LT(safe_time, *commit);
  }
  ASSERT_GE(
      PushMetric<EventStats>(METRIC_local_catalog_push_resolve_intents_wait).MaxValue(),
      ToMicroseconds(1s));
  thread_holder.JoinAll();
}

// A cycle whose deadline passes while a committed DDL's apply record is missing fails.
TEST_F(PgLocalCatalogPushTest, CycleFailsWithoutDdlApply) {
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_transaction_resend_applying_interval_usec) = 200000;
  ASSERT_OK(SeedCopies());
  const auto failures = PushCounter(METRIC_local_catalog_push_cycle_failures);
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("BEGIN"));
  ASSERT_OK(conn.Execute("CREATE TABLE local_catalog_t (k INT PRIMARY KEY)"));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_transaction_ignore_applying_probability) = 1;
  const auto saved_timeout = FLAGS_master_ts_rpc_timeout_ms;
  auto restore = ScopeExit([saved_timeout] {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_transaction_ignore_applying_probability) = 0;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_master_ts_rpc_timeout_ms) = saved_timeout;
  });
  ASSERT_OK(conn.Execute("COMMIT"));
  const auto timeouts = Pusher().TEST_NumResolveIntentsTimeouts();
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_master_ts_rpc_timeout_ms) = 3000 * kTimeMultiplier;
  ASSERT_OK(WaitFor(
      [this]() -> Result<bool> {
        auto cycle = Pusher().TEST_RunCycle();
        if (cycle.ok() || cycle.status().IsTryAgain()) {
          return false;
        }
        SCHECK(cycle.status().IsTimedOut(), IllegalState, cycle.status().ToString());
        return true;
      },
      60s * kTimeMultiplier, "A cycle times out"));
  ASSERT_GT(PushCounter(METRIC_local_catalog_push_cycle_failures), failures);
  ASSERT_GT(Pusher().TEST_NumResolveIntentsTimeouts(), timeouts);
}

// A copy whose answers keep changing without progress is left for the next cycle after its fourth
// such answer.
TEST_F(PgLocalCatalogPushTest, CopyFallingBackIsLeftForNextCycle) {
  ASSERT_OK(SeedCopies());
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE local_catalog_t (k INT PRIMARY KEY)"));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_local_catalog_answer_gap_behind) = true;
  const auto stalls = PushCounter(METRIC_local_catalog_push_stalls);
  const auto rpcs = PushCounter(METRIC_local_catalog_push_rpcs);

  const auto result = ASSERT_RESULT(RunCycleUntil([](const auto&) { return true; }));
  ASSERT_TRUE(result.caught_up_ts_uuids.empty());
  ASSERT_EQ(PushCounter(METRIC_local_catalog_push_stalls) - stalls, 4);
  ASSERT_EQ(PushCounter(METRIC_local_catalog_push_rpcs) - rpcs, 4);
}

// A copy is reseeded when the log no longer holds the entries after its position by the time the
// batch reads them.
TEST_F(PgLocalCatalogPushTest, LogGoneAtFirstReadIsReseeded) {
  const auto position = ASSERT_RESULT(SeedCopies());
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE local_catalog_t (k INT PRIMARY KEY)"));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_local_catalog_push_log_gone_at_read) = 1;
  const auto reseeds = PushCounter(METRIC_local_catalog_push_reseeds);

  const auto result = ASSERT_RESULT(RunCycleUntil([](const auto&) { return true; }));
  const auto pushes = LocalCatalogService().TEST_TakeRecordedPushes();
  ASSERT_FALSE(pushes.empty());
  ASSERT_TRUE(pushes.front().reseed_required());
  ASSERT_EQ(OpId::FromPB(pushes.front().from_op_id()), position);
  ASSERT_EQ(pushes.front().records_size(), 0);
  ASSERT_GT(PushCounter(METRIC_local_catalog_push_reseeds), reseeds);
  ASSERT_TRUE(result.caught_up_ts_uuids.empty());
}

// When the log no longer holds the entries after what a batch has read so far, the copy is sent
// what was read, without the cycle's safe time, and the next batch starts from there.
TEST_F(PgLocalCatalogPushTest, LogGoneMidBatchSendsWhatWasRead) {
  const auto from = ASSERT_RESULT(SeedCopies());
  auto conn = ASSERT_RESULT(Connect());
  for (int i = 0; i != 3; ++i) {
    ASSERT_OK(conn.ExecuteFormat("CREATE TABLE local_catalog_t$0 (k INT PRIMARY KEY)", i));
  }
  // Each read of the log returns one entry.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_consensus_max_batch_size_bytes) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_local_catalog_push_log_gone_at_read) = 2;
  const auto reseeds = PushCounter(METRIC_local_catalog_push_reseeds);

  const auto result = ASSERT_RESULT(RunCycleUntilCaughtUp());
  const auto pushes = LocalCatalogService().TEST_TakeRecordedPushes();
  ASSERT_NO_FATALS(CheckBatchChain(pushes, from, result));
  for (const auto& push : pushes) {
    ASSERT_EQ(push.to_op_id().index(), push.from_op_id().index() + 1) << push.ShortDebugString();
  }
  const auto records = RecordsOf(pushes);
  for (int i = 0; i != 3; ++i) {
    ASSERT_TRUE(FindCommit(records, Format("local_catalog_t$0", i)));
  }
  ASSERT_EQ(PushCounter(METRIC_local_catalog_push_reseeds), reseeds);
}

// When a read of the log returns nothing after a batch has read some entries, the copy is sent
// what was read, and the next batch starts from there.
TEST_F(PgLocalCatalogPushTest, EmptyReadMidBatchSendsWhatWasRead) {
  const auto from = ASSERT_RESULT(SeedCopies());
  auto conn = ASSERT_RESULT(Connect());
  for (int i = 0; i != 3; ++i) {
    ASSERT_OK(conn.ExecuteFormat("CREATE TABLE local_catalog_t$0 (k INT PRIMARY KEY)", i));
  }
  // Each read of the log returns one entry.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_consensus_max_batch_size_bytes) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_local_catalog_push_empty_at_read) = 2;
  const auto reseeds = PushCounter(METRIC_local_catalog_push_reseeds);
  const auto cycle_failures = PushCounter(METRIC_local_catalog_push_cycle_failures);

  const auto result = ASSERT_RESULT(RunCycleUntilCaughtUp());
  const auto pushes = LocalCatalogService().TEST_TakeRecordedPushes();
  ASSERT_NO_FATALS(CheckBatchChain(pushes, from, result));
  for (const auto& push : pushes) {
    ASSERT_EQ(push.to_op_id().index(), push.from_op_id().index() + 1) << push.ShortDebugString();
  }
  const auto records = RecordsOf(pushes);
  for (int i = 0; i != 3; ++i) {
    ASSERT_TRUE(FindCommit(records, Format("local_catalog_t$0", i)));
  }
  ASSERT_EQ(PushCounter(METRIC_local_catalog_push_reseeds), reseeds);
  ASSERT_EQ(PushCounter(METRIC_local_catalog_push_cycle_failures), cycle_failures);
}

// The system catalog log is kept only as long as nothing else needs it.
class PgLocalCatalogPushLogGcTest : public PgLocalCatalogPushTest {
 protected:
  void SetUp() override {
    // Applies to the system catalog tablet only when its master starts.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_retryable_request_timeout_secs) = 0;
    PgLocalCatalogPushTest::SetUp();
  }
};

// A copy whose position the log no longer holds is reseeded.
TEST_F(PgLocalCatalogPushLogGcTest, CopyBehindLogGcIsReseeded) {
  const auto position = ASSERT_RESULT(SeedCopies());
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_log_min_seconds_to_retain) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_log_min_segments_to_retain) = 1;
  auto tablet_peer = LeaderMaster().sys_catalog().tablet_peer();
  auto consensus = ASSERT_RESULT(tablet_peer->GetRaftConsensus());
  auto conn = ASSERT_RESULT(Connect());
  int table_idx = 0;
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        RETURN_NOT_OK(conn.ExecuteFormat(
            "CREATE TABLE local_catalog_t$0 (k INT PRIMARY KEY)", table_idx++));
        RETURN_NOT_OK(VERIFY_RESULT(tablet_peer->shared_tablet())->Flush(
            tablet::FlushMode::kSync, rocksdb::FlushReason::kTestOnly));
        RETURN_NOT_OK(tablet_peer->RunLogGC(/* rollover = */ true));
        auto op_id = consensus->LookupOpId(position.index);
        return !op_id.ok() && op_id.status().IsNotFound();
      },
      60s * kTimeMultiplier, "Log collected past the copy's position"));
  const auto reseeds = PushCounter(METRIC_local_catalog_push_reseeds);

  const auto result = ASSERT_RESULT(RunCycleUntil([](const auto&) { return true; }));
  const auto pushes = LocalCatalogService().TEST_TakeRecordedPushes();
  ASSERT_FALSE(pushes.empty());
  ASSERT_TRUE(pushes.front().reseed_required());
  ASSERT_EQ(OpId::FromPB(pushes.front().from_op_id()), position);
  ASSERT_GT(PushCounter(METRIC_local_catalog_push_reseeds), reseeds);
  ASSERT_TRUE(result.caught_up_ts_uuids.empty());
}

// A change to the system catalog tablet's metadata ends the batch it is in.
TEST_F(PgLocalCatalogPushTest, ChangeMetadataEndsBatch) {
  ASSERT_OK(SeedCopies());
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE DATABASE local_catalog_db"));

  ASSERT_RESULT(RunCycleUntilCaughtUp());
  const auto pushes = LocalCatalogService().TEST_TakeRecordedPushes();
  size_t ended_by_change_metadata = 0;
  for (size_t i = 0; i != pushes.size(); ++i) {
    const auto& records = pushes[i].records();
    for (int j = 0; j != records.size(); ++j) {
      if (records[j].operation() != RecordPB::CHANGE_METADATA) {
        continue;
      }
      ASSERT_EQ(j + 1, records.size()) << "Push " << i << ": " << pushes[i].ShortDebugString();
      if (i + 1 != pushes.size()) {
        ++ended_by_change_metadata;
      }
    }
  }
  ASSERT_GT(ended_by_change_metadata, 0);
}

// A copy behind a point-in-time restore of the catalog is pushed the entries before the restore and
// then reseeded, since the restored rows are not in the log.
TEST_F(PgLocalCatalogPushTest, CopyBehindCatalogRestoreIsReseeded) {
  const std::string db_name = "local_catalog_db";
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.ExecuteFormat("CREATE DATABASE $0", db_name));
  client::SnapshotTestUtil snapshot_util;
  snapshot_util.SetProxy(&client_->proxy_cache());
  snapshot_util.SetCluster(cluster_.get());
  const auto schedule_id = ASSERT_RESULT(snapshot_util.CreateSchedule(
      nullptr, YQL_DATABASE_PGSQL, db_name, client::WaitSnapshot::kTrue, 1s * kTimeMultiplier,
      60s * kTimeMultiplier));
  // A schedule restores only to times from its first snapshot on, and a snapshot's time can be
  // ahead of the clock when it completes.
  const auto first_snapshot = ASSERT_RESULT(snapshot_util.WaitScheduleSnapshot(schedule_id));
  const HybridTime first_snapshot_time(first_snapshot.entry().snapshot_hybrid_time());
  ASSERT_OK(WaitFor(
      [this, first_snapshot_time] { return LeaderMaster().Now() > first_snapshot_time; },
      10s * kTimeMultiplier, "Clock past the first snapshot"));
  const auto restore_at = LeaderMaster().Now();
  auto db_conn = ASSERT_RESULT(ConnectToDB(db_name));
  ASSERT_OK(db_conn.Execute("CREATE TABLE local_catalog_t (k INT PRIMARY KEY)"));
  ASSERT_RESULT(snapshot_util.WaitScheduleSnapshot(schedule_id, restore_at));
  ASSERT_OK(SeedCopies());
  // Leaves the copy entries to be pushed before the restore, in a database it does not restore.
  ASSERT_OK(conn.Execute("CREATE TABLE local_catalog_other (k INT PRIMARY KEY)"));
  const auto reseeds = PushCounter(METRIC_local_catalog_push_reseeds);

  ASSERT_OK(snapshot_util.RestoreSnapshotSchedule(schedule_id, restore_at));
  ASSERT_RESULT(RunCycleUntil([this, reseeds](const PushCycleResult&) {
    return PushCounter(METRIC_local_catalog_push_reseeds) > reseeds;
  }));

  const auto pushes = LocalCatalogService().TEST_TakeRecordedPushes();
  const auto reseed = std::ranges::find_if(
      pushes, [](const auto& push) { return push.reseed_required(); });
  ASSERT_NE(reseed, pushes.end());
  const auto reseed_from = OpId::FromPB(reseed->from_op_id());
  for (const auto& push : pushes) {
    if (!push.reseed_required()) {
      ASSERT_LE(push.to_op_id().index(), reseed_from.index) << push.ShortDebugString();
    }
  }
  ASSERT_TRUE(std::ranges::any_of(pushes, [&reseed_from](const auto& push) {
    return !push.reseed_required() && OpId::FromPB(push.to_op_id()) == reseed_from;
  }));

  auto consensus = ASSERT_RESULT(LeaderMaster().sys_catalog().tablet_peer()->GetRaftConsensus());
  const auto next = ASSERT_RESULT(consensus->ReadReplicatedMessagesForXCluster(
      reseed_from, CoarseMonoClock::Now() + 10s * kTimeMultiplier,
      /* fetch_single_entry = */ false));
  ASSERT_FALSE(next.result.messages.empty());
  const auto& restore = *next.result.messages.front();
  ASSERT_EQ(restore.id().index(), reseed_from.index + 1);
  ASSERT_EQ(restore.op_type(), consensus::OperationType::SNAPSHOT_OP);
  ASSERT_EQ(
      restore.snapshot_request().operation(),
      tserver::TabletSnapshotOpRequestPB::RESTORE_SYS_CATALOG);

  // No safe time at or after the restore reaches the copy.
  const HybridTime restore_time(restore.hybrid_time());
  for (const auto& push : pushes) {
    if (push.has_safe_time()) {
      ASSERT_LT(HybridTime(push.safe_time()), restore_time) << push.ShortDebugString();
    }
  }
  const auto state = Pusher().TEST_GetTargetState(TsUuid(0));
  ASSERT_TRUE(state && state->safe_time.is_valid());
  ASSERT_LT(state->safe_time, restore_time);
}

// A copy behind the others is pushed the records from its own position.
class PgLocalCatalogPushTwoCopiesTest : public PgLocalCatalogPushTest {
 protected:
  size_t NumTabletServers() override { return 2; }

  void SetUp() override {
    PgLocalCatalogPushTest::SetUp();
    for (size_t i = 0; i != cluster_->num_tablet_servers(); ++i) {
      if (i != kPgTsIndex) {
        ASSERT_OK(StartLease(i));
      }
    }
  }

  // Only the tserver running postgres takes a YSQL operation lease on its own.
  Status StartLease(size_t ts_idx) {
    return cluster_->mini_tablet_server(ts_idx)->server()->StartYSQLLeaseRefresher();
  }

  size_t OtherTsIndex() { return kPgTsIndex == 0 ? 1 : 0; }
};

TEST_F(PgLocalCatalogPushTwoCopiesTest, OlderCopyGetsRecordsFromItsPosition) {
  const auto old_position = ASSERT_RESULT(CommittedOpId());
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE local_catalog_t (k INT PRIMARY KEY)"));
  const auto new_position = ASSERT_RESULT(CommittedOpId());
  ASSERT_GT(new_position.index, old_position.index);

  LocalCatalogService(0).TEST_SetPosition(new_position);
  LocalCatalogService(1).TEST_SetPosition(old_position);
  const auto result = ASSERT_RESULT(RunCycleUntilCaughtUp());

  const auto new_pushes = LocalCatalogService(0).TEST_TakeRecordedPushes();
  ASSERT_FALSE(new_pushes.empty());
  ASSERT_EQ(OpId::FromPB(new_pushes.front().from_op_id()), new_position);
  // The DDL's rows are all before new_position; only its apply record can be after it.
  ASSERT_FALSE(HasTransactionalRow(RecordsOf(new_pushes), "local_catalog_t"));

  const auto old_pushes = LocalCatalogService(1).TEST_TakeRecordedPushes();
  ASSERT_FALSE(old_pushes.empty());
  ASSERT_EQ(OpId::FromPB(old_pushes.front().from_op_id()), old_position);
  ASSERT_TRUE(FindCommit(RecordsOf(old_pushes), "local_catalog_t"));
  ASSERT_GT(RecordsOf(old_pushes).size(), RecordsOf(new_pushes).size());

  const auto new_state = Pusher().TEST_GetTargetState(TsUuid(0));
  const auto old_state = Pusher().TEST_GetTargetState(TsUuid(1));
  ASSERT_TRUE(new_state && new_state->position);
  ASSERT_TRUE(old_state && old_state->position);
  ASSERT_EQ(*new_state->position, *old_state->position);
  ASSERT_GE(new_state->position->index, result.end_index);

  ASSERT_EQ(PushGauge(METRIC_local_catalog_push_targets), 2);
  ASSERT_EQ(PushGauge(METRIC_local_catalog_push_targets_caught_up), 2);
  ASSERT_EQ(PushGauge(METRIC_local_catalog_push_max_target_lag_ops), 0);
  ASSERT_GE(
      PushCounter(METRIC_local_catalog_push_records),
      RecordsOf(new_pushes).size() + RecordsOf(old_pushes).size());
  ASSERT_GT(PushCounter(METRIC_local_catalog_push_bytes), 0);
  ASSERT_GT(PushStatsCount(METRIC_local_catalog_push_batch_bytes), 0);
  const auto mem_tracker = PushMemTracker();
  ASSERT_TRUE(mem_tracker);
  ASSERT_GT(mem_tracker->peak_consumption(), 0);
  ASSERT_GE(
      mem_tracker->peak_consumption(),
      PushMetric<EventStats>(METRIC_local_catalog_push_batch_bytes).MaxValue());
  ASSERT_EQ(mem_tracker->consumption(), 0);
}

// Copies at the same position share each batch of a long catch up, and both catch up with one push
// in flight at a time.
TEST_F(PgLocalCatalogPushTwoCopiesTest, ManyBatchesWithOneRpcInFlight) {
  const auto from = ASSERT_RESULT(FallBehindInSmallBatches());
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_local_catalog_push_max_inflight_rpcs) = 1;
  const auto rpcs = PushCounter(METRIC_local_catalog_push_rpcs);
  const auto batches = PushStatsCount(METRIC_local_catalog_push_batch_bytes);

  const auto result = ASSERT_RESULT(RunCycleUntilCaughtUp());
  const auto pushes0 = LocalCatalogService(0).TEST_TakeRecordedPushes();
  const auto pushes1 = LocalCatalogService(1).TEST_TakeRecordedPushes();
  ASSERT_NO_FATALS(CheckBatchChain(pushes0, from, result));
  ASSERT_NO_FATALS(CheckBatchChain(pushes1, from, result));
  ASSERT_EQ(pushes0.size(), pushes1.size());
  for (size_t i = 0; i != pushes0.size(); ++i) {
    ASSERT_EQ(pushes0[i].ShortDebugString(), pushes1[i].ShortDebugString());
  }

  ASSERT_EQ(PushCounter(METRIC_local_catalog_push_rpcs) - rpcs, 2 * std::ssize(pushes0));
  ASSERT_EQ(PushStatsCount(METRIC_local_catalog_push_batch_bytes) - batches, pushes0.size());
  ASSERT_OK(WaitForMemoryReleased());
}

// A cycle whose batch cannot be built fails without sending to the copies, or building the same
// batch for the other copy.
TEST_F(PgLocalCatalogPushTwoCopiesTest, FailedBatchBuildEndsCycle) {
  ASSERT_OK(SeedCopies());
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE local_catalog_t (k INT PRIMARY KEY)"));
  const auto rpcs = PushCounter(METRIC_local_catalog_push_rpcs);
  const auto failed_builds = Pusher().TEST_NumFailedBatchBuilds();

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_local_catalog_push_fail_batch_build) = true;
  auto restore = ScopeExit([] {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_local_catalog_push_fail_batch_build) = false;
  });
  ASSERT_NOK(Pusher().TEST_RunCycle());

  ASSERT_EQ(Pusher().TEST_NumFailedBatchBuilds() - failed_builds, 1);
  ASSERT_EQ(PushCounter(METRIC_local_catalog_push_rpcs), rpcs);
}

// A push that fails for one copy does not stop the other copy from catching up in the same cycle.
TEST_F(PgLocalCatalogPushTwoCopiesTest, FailedPushLeavesOtherCopyCatchingUp) {
  ASSERT_OK(SeedCopies());
  const auto other = OtherTsIndex();
  LocalCatalogService(other).TEST_SetFailPushes(true);
  auto restore = ScopeExit([this, other] { LocalCatalogService(other).TEST_SetFailPushes(false); });
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE local_catalog_t (k INT PRIMARY KEY)"));
  const auto rpc_failures = PushCounter(METRIC_local_catalog_push_rpc_failures);

  const auto result = ASSERT_RESULT(Pusher().TEST_RunCycle());
  ASSERT_TRUE(result);
  ASSERT_EQ(result->caught_up_ts_uuids, std::vector{TsUuid(kPgTsIndex)});
  ASSERT_GT(PushCounter(METRIC_local_catalog_push_rpc_failures), rpc_failures);
  ASSERT_FALSE(LocalCatalogService(kPgTsIndex).TEST_TakeRecordedPushes().empty());
  ASSERT_TRUE(LocalCatalogService(other).TEST_TakeRecordedPushes().empty());
}

// A tserver that shuts down gives up its lease and is no longer pushed to, while the other copy
// is still caught up.
TEST_F(PgLocalCatalogPushTwoCopiesTest, TserverThatLeavesIsDropped) {
  ASSERT_OK(SeedCopies());
  const auto other = OtherTsIndex();
  const auto leaving_uuid = TsUuid(other);
  const std::vector<std::string> staying_uuids = {TsUuid(kPgTsIndex)};
  ASSERT_EQ(PushGauge(METRIC_local_catalog_push_targets), 2);
  cluster_->mini_tablet_server(other)->Shutdown();

  ASSERT_RESULT(RunCycleUntil([this, &staying_uuids, &leaving_uuid](const auto& result) {
    return result.caught_up_ts_uuids == staying_uuids &&
           !Pusher().TEST_GetTargetState(leaving_uuid);
  }));
  ASSERT_FALSE(Pusher().TEST_GetTargetState(leaving_uuid));
  ASSERT_EQ(PushGauge(METRIC_local_catalog_push_targets), 1);
  ASSERT_EQ(PushGauge(METRIC_local_catalog_push_targets_caught_up), 1);
}

// A tserver that restarts takes a new lease with a copy that has nothing to build on, and is
// reseeded.
TEST_F(PgLocalCatalogPushTwoCopiesTest, RestartedTserverIsReseeded) {
  ASSERT_OK(SeedCopies());
  const auto other = OtherTsIndex();
  ASSERT_OK(cluster_->mini_tablet_server(other)->Restart());
  ASSERT_OK(cluster_->mini_tablet_server(other)->WaitStarted());
  ASSERT_OK(StartLease(other));
  const auto reseeds = PushCounter(METRIC_local_catalog_push_reseeds);

  ASSERT_OK(WaitFor(
      [this, other]() -> Result<bool> {
        auto cycle = Pusher().TEST_RunCycle();
        if (!cycle.ok() && !cycle.status().IsTryAgain()) {
          return cycle.status();
        }
        return std::ranges::any_of(
            LocalCatalogService(other).TEST_TakeRecordedPushes(),
            [](const auto& push) { return push.reseed_required(); });
      },
      60s * kTimeMultiplier, "Restarted copy reseeded"));
  ASSERT_GT(PushCounter(METRIC_local_catalog_push_reseeds), reseeds);
  ASSERT_EQ(PushGauge(METRIC_local_catalog_push_targets), 2);
}

// A tserver that joins and takes a lease becomes a target, and its copy is caught up from the
// position it answers with.
TEST_F(PgLocalCatalogPushTwoCopiesTest, JoiningTserverIsCaughtUp) {
  ASSERT_OK(SeedCopies());
  ASSERT_OK(cluster_->AddTabletServer());
  const auto joined = cluster_->num_tablet_servers() - 1;
  const auto position = ASSERT_RESULT(CommittedOpId());
  LocalCatalogService(joined).TEST_SetPosition(position);
  ASSERT_OK(StartLease(joined));

  const auto result = ASSERT_RESULT(RunCycleUntilCaughtUp());
  ASSERT_TRUE(std::ranges::count(result.caught_up_ts_uuids, TsUuid(joined)));
  ASSERT_EQ(PushGauge(METRIC_local_catalog_push_targets), 3);
  const auto state = Pusher().TEST_GetTargetState(TsUuid(joined));
  ASSERT_TRUE(state && state->position);
  ASSERT_GE(state->position->index, result.end_index);
  ASSERT_GE(state->safe_time, result.safe_time);
}

// A tserver started without the local catalog has no service to push to, and is counted as a target
// without a copy while the other copies are caught up.
TEST_F(PgLocalCatalogPushTwoCopiesTest, TserverWithoutLocalCatalogHasNoCopy) {
  ASSERT_OK(SeedCopies());
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_local_tserver_catalog) = false;
  ASSERT_OK(cluster_->AddTabletServer());
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_local_tserver_catalog) = true;
  const auto joined = cluster_->num_tablet_servers() - 1;
  const auto joined_uuid = TsUuid(joined);
  ASSERT_EQ(cluster_->mini_tablet_server(joined)->server()->TEST_GetLocalCatalogService(), nullptr);
  ASSERT_OK(StartLease(joined));
  const auto rpc_failures = PushCounter(METRIC_local_catalog_push_rpc_failures);

  auto expected = std::vector{TsUuid(0), TsUuid(1)};
  std::ranges::sort(expected);
  ASSERT_RESULT(RunCycleUntil([this, &expected](const auto& result) {
    auto caught_up = result.caught_up_ts_uuids;
    std::ranges::sort(caught_up);
    return caught_up == expected &&
           PushGauge(METRIC_local_catalog_push_targets_without_copy) == 1;
  }));
  const auto state = Pusher().TEST_GetTargetState(joined_uuid);
  ASSERT_TRUE(state);
  ASSERT_FALSE(state->position);
  ASSERT_EQ(PushGauge(METRIC_local_catalog_push_targets), 3);
  ASSERT_EQ(PushGauge(METRIC_local_catalog_push_targets_caught_up), 2);
  ASSERT_EQ(PushCounter(METRIC_local_catalog_push_rpc_failures), rpc_failures);

  // The tserver without the service is not pushed to again under the same lease, even once the WAL
  // moves.
  LocalCatalogService(0).TEST_TakeRecordedPushes();
  LocalCatalogService(1).TEST_TakeRecordedPushes();
  const auto rpcs = PushCounter(METRIC_local_catalog_push_rpcs);
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE local_catalog_t (k INT PRIMARY KEY)"));
  ASSERT_RESULT(RunCycleUntil([&expected](const auto& result) {
    auto caught_up = result.caught_up_ts_uuids;
    std::ranges::sort(caught_up);
    return caught_up == expected;
  }));
  const auto pushes = std::ssize(LocalCatalogService(0).TEST_TakeRecordedPushes()) +
                      std::ssize(LocalCatalogService(1).TEST_TakeRecordedPushes());
  ASSERT_GT(pushes, 0);
  ASSERT_EQ(PushCounter(METRIC_local_catalog_push_rpcs) - rpcs, pushes);
  ASSERT_EQ(PushGauge(METRIC_local_catalog_push_targets_without_copy), 1);
}

// With object locking off, the targets are the holders of the YSQL operation lease.
class PgLocalCatalogPushLegacyTest : public PgLocalCatalogPushTwoCopiesTest {
 protected:
  void SetUp() override {
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_ysql_operation_lease) = true;
    PgLocalCatalogPushTwoCopiesTest::SetUp();
  }

  bool UseLegacyDdlMode() override { return true; }
};

// Every lease holder is a target, and a DDL reaches every copy with a safe time at or after its
// commit.
TEST_F(PgLocalCatalogPushLegacyTest, DdlReachesEveryCopy) {
  ASSERT_OK(SeedCopies());
  ASSERT_EQ(PushGauge(METRIC_local_catalog_push_targets), 2);
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE local_catalog_t (k INT PRIMARY KEY)"));

  const auto result = ASSERT_RESULT(RunCycleUntilCaughtUp());
  for (size_t i = 0; i != cluster_->num_tablet_servers(); ++i) {
    SCOPED_TRACE(Format("tserver $0", i));
    const auto commit =
        FindCommit(RecordsOf(LocalCatalogService(i).TEST_TakeRecordedPushes()), "local_catalog_t");
    ASSERT_TRUE(commit);
    ASSERT_GE(result.safe_time, *commit);
  }
  ASSERT_EQ(PushGauge(METRIC_local_catalog_push_targets_caught_up), 2);
}

// Without object locking, the master does not push while the YSQL operation lease or invalidation
// messages are off.
TEST_F(PgLocalCatalogPushLegacyTest, GateOffPushesNothing) {
  ASSERT_OK(SeedCopies());
  const auto rpcs = PushCounter(METRIC_local_catalog_push_rpcs);

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_invalidation_messages) = false;
  ASSERT_FALSE(ASSERT_RESULT(Pusher().TEST_RunCycle()));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_invalidation_messages) = true;

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_ysql_operation_lease) = false;
  ASSERT_FALSE(ASSERT_RESULT(Pusher().TEST_RunCycle()));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_ysql_operation_lease) = true;

  ASSERT_EQ(PushCounter(METRIC_local_catalog_push_rpcs), rpcs);
  ASSERT_EQ(PushGauge(METRIC_local_catalog_push_targets), 0);
}

// After a master leader change, the new leader pushes to the copies from their positions, with a
// safe time past the old leader's, and the old leader reports no targets and no lag.
class PgLocalCatalogPushFailoverTest : public PgLocalCatalogPushTest {
 protected:
  size_t NumMasters() override { return 3; }

  // Waits for a master other than the given one to lead.
  Status WaitForOtherLeader(master::MiniMaster* old_leader) {
    const auto old_leader_uuid = old_leader->permanent_uuid();
    return WaitFor(
        [this, &old_leader_uuid]() -> Result<bool> {
          auto leader = cluster_->GetLeaderMiniMaster();
          return leader.ok() && (*leader)->permanent_uuid() != old_leader_uuid;
        },
        30s * kTimeMultiplier, "New master leader");
  }

  Status StepDownLeader(master::MiniMaster* old_leader) {
    VERIFY_RESULT(cluster_->StepDownMasterLeader());
    return WaitForOtherLeader(old_leader);
  }

  // Runs a cycle on the master's pusher on another thread.
  std::future<Result<std::optional<PushCycleResult>>> RunCycleAsync(master::MiniMaster* master) {
    return std::async(
        std::launch::async,
        [pusher = master->catalog_manager_impl().sys_catalog_pusher()] {
          return pusher->TEST_RunCycle();
        });
  }
};

TEST_F(PgLocalCatalogPushFailoverTest, NewLeaderPushesFromCopyPositions) {
  ASSERT_OK(SeedCopies());
  const auto before = ASSERT_RESULT(RunCycleUntilCaughtUp());
  auto* old_leader = &LeaderMaster();
  const auto position = Pusher().TEST_GetTargetState(TsUuid(0));
  ASSERT_TRUE(position && position->position);
  LocalCatalogService().TEST_TakeRecordedPushes();

  ASSERT_OK(StepDownLeader(old_leader));

  const auto after = ASSERT_RESULT(RunCycleUntilCaughtUp());
  ASSERT_GT(after.safe_time, before.safe_time);
  const auto pushes = LocalCatalogService().TEST_TakeRecordedPushes();
  ASSERT_TRUE(std::ranges::none_of(pushes, [](const auto& push) {
    return push.reseed_required();
  }));
  ASSERT_FALSE(pushes.empty());
  ASSERT_EQ(OpId::FromPB(pushes.front().from_op_id()), *position->position);

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE local_catalog_t (k INT PRIMARY KEY)"));
  ASSERT_RESULT(RunCycleUntilCaughtUp());
  ASSERT_TRUE(FindCommit(RecordsOf(LocalCatalogService().TEST_TakeRecordedPushes()),
                         "local_catalog_t"));

  // The old leader learns that it is not the leader on its next cycle.
  ASSERT_FALSE(ASSERT_RESULT(Pusher(old_leader).TEST_RunCycle()));
  ASSERT_EQ(PushGauge(METRIC_local_catalog_push_targets, old_leader), 0);
  ASSERT_EQ(
      PushGauge<FunctionGauge>(METRIC_local_catalog_push_safe_time_lag_ms, old_leader), 0);
}

// A new leader pushes to the copies once it has loaded the catalog, without waiting for the push
// interval.
TEST_F(PgLocalCatalogPushFailoverTest, NewLeaderPushesWithoutWaiting) {
  ASSERT_OK(SeedCopies());
  const auto before = ASSERT_RESULT(RunCycleUntilCaughtUp());
  ASSERT_OK(StepDownLeader(&LeaderMaster()));

  ASSERT_OK(WaitFor(
      [this, &before] {
        const auto state = Pusher().TEST_GetTargetState(TsUuid(0));
        return state && state->safe_time.is_valid() && state->safe_time > before.safe_time;
      },
      30s * kTimeMultiplier, "New leader pushes"));
}

// A leader that loses its lease for a while is still the leader: its cycles push nothing and are
// retried without counting a failure, and its gauges keep the targets and the copies' lag.
TEST_F(PgLocalCatalogPushFailoverTest, LeaseLapseKeepsGauges) {
  ASSERT_OK(SeedCopies());
  auto* leader = &LeaderMaster();
  ASSERT_EQ(PushGauge(METRIC_local_catalog_push_targets, leader), 1);
  for (size_t i = 0; i != cluster_->num_masters(); ++i) {
    if (cluster_->mini_master(i) != leader) {
      cluster_->mini_master(i)->Shutdown();
    }
  }
  auto consensus = ASSERT_RESULT(leader->sys_catalog().tablet_peer()->GetRaftConsensus());
  ASSERT_OK(WaitFor(
      [&consensus] { return consensus->LeaderTerm() == OpId::kUnknownTerm; },
      30s * kTimeMultiplier, "Leader lease lapsed"));
  ASSERT_EQ(consensus->role(), PeerRole::LEADER);

  const auto failures = PushCounter(METRIC_local_catalog_push_cycle_failures, leader);
  const auto cycle = Pusher(leader).TEST_RunCycle();
  ASSERT_NOK(cycle);
  ASSERT_TRUE(cycle.status().IsTryAgain()) << cycle.status();
  ASSERT_EQ(PushCounter(METRIC_local_catalog_push_cycle_failures, leader), failures);
  ASSERT_EQ(PushGauge(METRIC_local_catalog_push_targets, leader), 1);
  ASSERT_GT(PushGauge<FunctionGauge>(METRIC_local_catalog_push_safe_time_lag_ms, leader), 0);
}

// A cycle that finds the leader changed after it resolved intents ends without pushing, as on a
// master that is not the leader, and is not a failure.
TEST_F(PgLocalCatalogPushFailoverTest, LeaderChangeBeforePushEndsCycle) {
  ASSERT_OK(SeedCopies());
  auto* old_leader = &LeaderMaster();
  const auto cycles = PushCounter(METRIC_local_catalog_push_cycles, old_leader);
  const auto failures = PushCounter(METRIC_local_catalog_push_cycle_failures, old_leader);
  const auto waits = PushStatsCount(METRIC_local_catalog_push_resolve_intents_wait, old_leader);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_local_catalog_push_delay_before_term_check_ms) =
      5000 * kTimeMultiplier;

  auto cycle = RunCycleAsync(old_leader);
  ASSERT_OK(WaitFor(
      [&] {
        return PushStatsCount(METRIC_local_catalog_push_resolve_intents_wait, old_leader) > waits;
      },
      30s * kTimeMultiplier, "Cycle resolved intents"));
  ASSERT_OK(StepDownLeader(old_leader));

  ASSERT_FALSE(ASSERT_RESULT(cycle.get()));
  ASSERT_EQ(PushCounter(METRIC_local_catalog_push_cycles, old_leader), cycles);
  ASSERT_EQ(PushCounter(METRIC_local_catalog_push_cycle_failures, old_leader), failures);
  ASSERT_EQ(PushGauge(METRIC_local_catalog_push_targets, old_leader), 0);
  ASSERT_TRUE(LocalCatalogService().TEST_TakeRecordedPushes().empty());
}

// A master that stops being the leader during a cycle does not reseed a copy, and the cycle ends
// as on a master that is not the leader, without counting a failure.
TEST_F(PgLocalCatalogPushFailoverTest, OldLeaderDoesNotReseed) {
  ASSERT_OK(SeedCopies());
  auto* old_leader = &LeaderMaster();
  const auto committed = ASSERT_RESULT(CommittedOpId());
  LocalCatalogService().TEST_SetPosition(OpId(committed.term + 1, committed.index - 1));
  const auto cycles = PushCounter(METRIC_local_catalog_push_cycles, old_leader);
  const auto failures = PushCounter(METRIC_local_catalog_push_cycle_failures, old_leader);
  const auto reseeds = PushCounter(METRIC_local_catalog_push_reseeds, old_leader);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_local_catalog_push_delay_before_send_ms) =
      5000 * kTimeMultiplier;

  auto cycle = RunCycleAsync(old_leader);
  ASSERT_OK(WaitFor(
      [&] { return PushCounter(METRIC_local_catalog_push_cycles, old_leader) > cycles; },
      30s * kTimeMultiplier, "Cycle passed the term check"));
  ASSERT_OK(StepDownLeader(old_leader));

  ASSERT_FALSE(ASSERT_RESULT(cycle.get()));
  ASSERT_EQ(PushCounter(METRIC_local_catalog_push_cycle_failures, old_leader), failures);
  ASSERT_EQ(PushCounter(METRIC_local_catalog_push_reseeds, old_leader), reseeds);
  ASSERT_EQ(PushGauge(METRIC_local_catalog_push_targets, old_leader), 0);
  ASSERT_TRUE(LocalCatalogService().TEST_TakeRecordedPushes().empty());
}

class PgLocalCatalogPushFailoverTwoCopiesTest : public PgLocalCatalogPushFailoverTest {
 protected:
  size_t NumTabletServers() override { return 2; }

  void SetUp() override {
    PgLocalCatalogPushFailoverTest::SetUp();
    for (size_t i = 0; i != cluster_->num_tablet_servers(); ++i) {
      if (i != kPgTsIndex) {
        // Only the tserver running postgres takes a YSQL operation lease on its own.
        ASSERT_OK(cluster_->mini_tablet_server(i)->server()->StartYSQLLeaseRefresher());
      }
    }
  }
};

// A reseed built for one copy is not sent to another copy at the same position once the master
// stops being the leader, and the cycle ends as on a master that is not the leader.
TEST_F(PgLocalCatalogPushFailoverTwoCopiesTest, OldLeaderDoesNotSendSharedReseed) {
  ASSERT_OK(SeedCopies());
  auto* old_leader = &LeaderMaster();
  const auto committed = ASSERT_RESULT(CommittedOpId());
  for (size_t i = 0; i != cluster_->num_tablet_servers(); ++i) {
    LocalCatalogService(i).TEST_SetPosition(OpId(committed.term + 1, committed.index - 1));
  }
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_local_catalog_push_max_inflight_rpcs) = 1;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_local_catalog_push_reseed_answer_delay_ms) =
      5000 * kTimeMultiplier;
  const auto failures = PushCounter(METRIC_local_catalog_push_cycle_failures, old_leader);
  const auto reseeds = PushCounter(METRIC_local_catalog_push_reseeds, old_leader);

  auto cycle = RunCycleAsync(old_leader);
  ASSERT_OK(WaitFor(
      [&] { return PushCounter(METRIC_local_catalog_push_reseeds, old_leader) > reseeds; },
      30s * kTimeMultiplier, "First reseed sent"));
  ASSERT_OK(StepDownLeader(old_leader));

  ASSERT_FALSE(ASSERT_RESULT(cycle.get()));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_local_catalog_push_reseed_answer_delay_ms) = 0;
  ASSERT_EQ(PushCounter(METRIC_local_catalog_push_reseeds, old_leader), reseeds + 1);
  ASSERT_EQ(PushCounter(METRIC_local_catalog_push_cycle_failures, old_leader), failures);
}

class PgLocalCatalogPushFailoverLogGcTest : public PgLocalCatalogPushFailoverTest {
 protected:
  void SetUp() override {
    // Applies to the system catalog tablet only when its master starts.
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_retryable_request_timeout_secs) = 0;
    PgLocalCatalogPushFailoverTest::SetUp();
  }
};

// A master that leads again pushes from where the copies are, not from what they answered in its
// earlier term, so a copy that another leader moved on is not reseeded once this master's log no
// longer holds the position it last knew.
TEST_F(PgLocalCatalogPushFailoverLogGcTest, ReturningLeaderDoesNotReseedFromOldPosition) {
  const auto old_position = ASSERT_RESULT(SeedCopies());
  auto* first_leader = &LeaderMaster();
  ASSERT_OK(StepDownLeader(first_leader));

  const auto saved_seconds = FLAGS_log_min_seconds_to_retain;
  const auto saved_segments = FLAGS_log_min_segments_to_retain;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_log_min_seconds_to_retain) = 0;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_log_min_segments_to_retain) = 1;
  auto tablet_peer = first_leader->sys_catalog().tablet_peer();
  auto consensus = ASSERT_RESULT(tablet_peer->GetRaftConsensus());
  auto conn = ASSERT_RESULT(Connect());
  int table_idx = 0;
  ASSERT_OK(WaitFor(
      [&]() -> Result<bool> {
        RETURN_NOT_OK(conn.ExecuteFormat(
            "CREATE TABLE local_catalog_t$0 (k INT PRIMARY KEY)", table_idx++));
        RETURN_NOT_OK(VERIFY_RESULT(tablet_peer->shared_tablet())->Flush(
            tablet::FlushMode::kSync, rocksdb::FlushReason::kTestOnly));
        RETURN_NOT_OK(tablet_peer->RunLogGC(/* rollover = */ true));
        auto op_id = consensus->LookupOpId(old_position.index);
        return !op_id.ok() && op_id.status().IsNotFound();
      },
      60s * kTimeMultiplier, "First leader's log collected past the copy's position"));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_log_min_seconds_to_retain) = saved_seconds;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_log_min_segments_to_retain) = saved_segments;

  // The other leader moves the copy past old_position.
  ASSERT_RESULT(RunCycleUntilCaughtUp());
  LocalCatalogService().TEST_TakeRecordedPushes();

  ASSERT_RESULT(cluster_->StepDownMasterLeader(first_leader->permanent_uuid()));
  ASSERT_OK(WaitFor(
      [this, first_leader]() -> Result<bool> {
        auto leader = cluster_->GetLeaderMiniMaster();
        return leader.ok() && *leader == first_leader;
      },
      30s * kTimeMultiplier, "First leader leads again"));
  const auto reseeds = PushCounter(METRIC_local_catalog_push_reseeds, first_leader);

  ASSERT_RESULT(RunCycleUntilCaughtUp());
  ASSERT_EQ(PushCounter(METRIC_local_catalog_push_reseeds, first_leader), reseeds);
  ASSERT_TRUE(std::ranges::none_of(LocalCatalogService().TEST_TakeRecordedPushes(),
                                   [](const auto& push) { return push.reseed_required(); }));
}

// A pusher that shuts down with a push in flight stops without waiting for the answer, and then
// reports no targets and no lag. The next leader catches the copy up.
TEST_F(PgLocalCatalogPushFailoverTest, ShutdownWithPushInFlight) {
  ASSERT_OK(SeedCopies());
  auto* old_leader = &LeaderMaster();
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE local_catalog_t (k INT PRIMARY KEY)"));
  const int32_t delay_ms = 5000 * kTimeMultiplier;
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_local_catalog_push_answer_delay_ms) = delay_ms;
  const auto rpcs = PushCounter(METRIC_local_catalog_push_rpcs, old_leader);
  const auto answered = LocalCatalogService().TEST_NumAnswered();

  auto pusher = old_leader->catalog_manager_impl().sys_catalog_pusher();
  auto cycle = RunCycleAsync(old_leader);
  ASSERT_OK(WaitFor(
      [&] { return PushCounter(METRIC_local_catalog_push_rpcs, old_leader) > rpcs; },
      30s * kTimeMultiplier, "Push in flight"));
  const auto start = CoarseMonoClock::Now();
  pusher->CompleteShutdown();
  ASSERT_LT(CoarseMonoClock::Now() - start, delay_ms * 1ms);
  const auto result = cycle.get();
  ASSERT_NOK(result);
  ASSERT_TRUE(result.status().IsShutdownInProgress()) << result.status();
  ASSERT_EQ(PushMemTracker()->consumption(), 0);
  ASSERT_EQ(PushGauge(METRIC_local_catalog_push_targets, old_leader), 0);
  ASSERT_EQ(
      PushGauge<FunctionGauge>(METRIC_local_catalog_push_safe_time_lag_ms, old_leader), 0);

  // The answer arrives after the pusher has shut down.
  ASSERT_OK(WaitFor(
      [this, answered] { return LocalCatalogService().TEST_NumAnswered() > answered; },
      delay_ms * 2ms, "Push answered"));
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_local_catalog_push_answer_delay_ms) = 0;
  pusher.reset();
  ASSERT_OK(StepDownLeader(old_leader));
  ASSERT_RESULT(RunCycleUntilCaughtUp());
  ASSERT_TRUE(FindCommit(RecordsOf(LocalCatalogService().TEST_TakeRecordedPushes()),
                         "local_catalog_t"));
}

// Push cycles run on the timer.
class PgLocalCatalogPushTimerTest : public PgLocalCatalogPushTest {
 protected:
  int32_t PushIntervalMs() override { return 200; }
};

// A write by the master alone, outside any transaction, reaches the copy on the timer.
TEST_F(PgLocalCatalogPushTimerTest, MasterWriteArrivesOnTimer) {
  ASSERT_OK(SeedCopies());
  ASSERT_OK(client_->CreateNamespaceIfNotExists("local_catalog_ks", YQLDatabase::YQL_DATABASE_CQL));
  std::vector<RecordPB> records;
  ASSERT_OK(WaitFor(
      [this, &records] {
        auto pushed = RecordsOf(LocalCatalogService().TEST_TakeRecordedPushes());
        records.insert(records.end(), pushed.begin(), pushed.end());
        return HasNonTransactionalRow(records, "local_catalog_ks");
      },
      30s * kTimeMultiplier, "Namespace write pushed"));
}

// A copy that a cycle left behind is caught up on the timer although the WAL has not moved.
TEST_F(PgLocalCatalogPushTimerTest, TimerCatchesUpCopyLeftBehind) {
  const auto position = ASSERT_RESULT(SeedCopies());
  // The push fails, so the cycle leaves the copy without the cycle's safe time.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_local_catalog_fail_pushes) = true;
  const auto rpc_failures = PushCounter(METRIC_local_catalog_push_rpc_failures);
  const auto result = ASSERT_RESULT(Pusher().TEST_RunCycle());
  ASSERT_TRUE(result);
  ASSERT_TRUE(result->caught_up_ts_uuids.empty());
  ASSERT_GT(PushCounter(METRIC_local_catalog_push_rpc_failures), rpc_failures);
  ASSERT_EQ(PushGauge(METRIC_local_catalog_push_targets_caught_up), 0);
  ASSERT_OK(WaitForMemoryReleased());

  LocalCatalogService().TEST_TakeRecordedPushes();
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_local_catalog_fail_pushes) = false;
  ASSERT_OK(WaitFor(
      [this, &position] {
        return std::ranges::any_of(
            LocalCatalogService().TEST_TakeRecordedPushes(), [&position](const auto& push) {
              return OpId::FromPB(push.from_op_id()) == position && push.has_safe_time() &&
                     OpId::FromPB(push.to_op_id()) == position;
            });
      },
      30s * kTimeMultiplier, "Copy caught up on the timer"));
}

// A tserver that keeps no copy is pushed to on the timer only once the WAL moves.
TEST_F(PgLocalCatalogPushTimerTest, TimerSkipsTargetWithoutCopy) {
  ASSERT_OK(SeedCopies());
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_local_catalog_record_pushes) = false;
  ASSERT_RESULT(Pusher().TEST_RunCycle());
  auto state = Pusher().TEST_GetTargetState(TsUuid(0));
  ASSERT_TRUE(state);
  ASSERT_FALSE(state->position);

  // The copy appears without a new lease.
  const auto position = ASSERT_RESULT(CommittedOpId());
  LocalCatalogService().TEST_SetPosition(position);
  const auto skipped = PushCounter(METRIC_local_catalog_push_cycles_skipped);
  const auto rpcs = PushCounter(METRIC_local_catalog_push_rpcs);
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_local_catalog_record_pushes) = true;
  ASSERT_OK(WaitFor(
      [this, skipped] {
        return PushCounter(METRIC_local_catalog_push_cycles_skipped) >= skipped + 5;
      },
      30s * kTimeMultiplier, "Timer cycles skipped"));
  ASSERT_EQ(ASSERT_RESULT(CommittedOpId()), position) << "The WAL moved during the check";
  ASSERT_EQ(PushCounter(METRIC_local_catalog_push_rpcs), rpcs);
  ASSERT_TRUE(LocalCatalogService().TEST_TakeRecordedPushes().empty());
  ASSERT_EQ(PushGauge(METRIC_local_catalog_push_targets_without_copy), 1);
  ASSERT_EQ(PushGauge<FunctionGauge>(METRIC_local_catalog_push_safe_time_lag_ms), 0);

  ASSERT_OK(client_->CreateNamespaceIfNotExists("local_catalog_ks", YQLDatabase::YQL_DATABASE_CQL));
  ASSERT_OK(WaitFor(
      [this, &position] {
        return std::ranges::any_of(
            LocalCatalogService().TEST_TakeRecordedPushes(), [&position](const auto& push) {
              return OpId::FromPB(push.from_op_id()) == position && push.has_safe_time();
            });
      },
      30s * kTimeMultiplier, "Copy caught up once the WAL moved"));
}

// The lag gauges grow while a copy is left behind, and drop to 0 once it is caught up and the WAL
// is idle.
TEST_F(PgLocalCatalogPushTimerTest, LagGaugesTrackCopyLeftBehind) {
  ASSERT_OK(SeedCopies());
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_local_catalog_fail_pushes) = true;
  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE local_catalog_t (k INT PRIMARY KEY)"));
  const auto batches = PushStatsCount(METRIC_local_catalog_push_batch_bytes);
  ASSERT_RESULT(Pusher().TEST_RunCycle());
  ASSERT_GT(PushStatsCount(METRIC_local_catalog_push_batch_bytes), batches);
  ASSERT_OK(WaitForMemoryReleased());
  ASSERT_GT(PushGauge(METRIC_local_catalog_push_max_target_lag_ops), 0);
  ASSERT_OK(WaitFor(
      [this] {
        return PushGauge<FunctionGauge>(METRIC_local_catalog_push_safe_time_lag_ms) > 0;
      },
      30s * kTimeMultiplier, "Safe time lag grows"));

  ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_local_catalog_fail_pushes) = false;
  ASSERT_OK(WaitFor(
      [this] {
        return PushGauge(METRIC_local_catalog_push_max_target_lag_ops) == 0 &&
               PushGauge<FunctionGauge>(METRIC_local_catalog_push_safe_time_lag_ms) == 0;
      },
      30s * kTimeMultiplier, "Lag gauges drop to 0"));
}

// A master started without the local catalog has no pusher and no push metrics, and pushes
// nothing to a tserver that keeps a copy.
class PgLocalCatalogMasterOffTest : public PgLocalCatalogTest {
 protected:
  void SetUp() override {
    ToggleDDLMode(/* use_legacy = */ false);
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_yb_enable_invalidation_messages) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_local_catalog_record_pushes) = true;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_TEST_check_broadcast_address) = false;
    ANNOTATE_UNPROTECTED_WRITE(FLAGS_ysql_enable_auto_analyze) = false;
    PgLocalCatalogTest::SetUp();
  }

  bool LocalCatalogEnabled() override { return false; }
  int32_t PushIntervalMs() override { return 200; }
  size_t NumTabletServers() override { return 2; }
};

TEST_F(PgLocalCatalogMasterOffTest, MasterWithoutLocalCatalogPushesNothing) {
  auto* master = cluster_->mini_master();
  ASSERT_EQ(master->catalog_manager_impl().sys_catalog_pusher(), nullptr);
  ASSERT_FALSE(static_cast<bool>(master->master()->metric_entity_cluster()->FindOrNull<Counter>(
      METRIC_local_catalog_push_cycles)));
  ASSERT_FALSE(master->master()->mem_tracker()->FindChild("SysCatalogPusher"));

  // A tserver restarted with the local catalog keeps a copy and holds a lease.
  ANNOTATE_UNPROTECTED_WRITE(FLAGS_enable_local_tserver_catalog) = true;
  const size_t ts_idx = kPgTsIndex == 0 ? 1 : 0;
  auto* ts = cluster_->mini_tablet_server(ts_idx);
  ASSERT_OK(ts->Restart());
  ASSERT_OK(ts->WaitStarted());
  ASSERT_OK(ts->server()->StartYSQLLeaseRefresher());
  auto consensus = ASSERT_RESULT(master->sys_catalog().tablet_peer()->GetRaftConsensus());
  LocalCatalogService(ts_idx).TEST_SetPosition(consensus->GetLastCommittedOpId());

  auto conn = ASSERT_RESULT(Connect());
  ASSERT_OK(conn.Execute("CREATE TABLE local_catalog_t (k INT PRIMARY KEY)"));
  SleepFor(10 * PushIntervalMs() * 1ms * kTimeMultiplier);
  ASSERT_TRUE(LocalCatalogService(ts_idx).TEST_TakeRecordedPushes().empty());
}

}  // namespace yb::pgwrapper
