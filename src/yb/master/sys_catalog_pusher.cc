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

#include "yb/master/sys_catalog_pusher.h"

#include <algorithm>
#include <deque>
#include <functional>
#include <map>
#include <utility>

#include "yb/cdc/cdc_error.h"
#include "yb/cdc/cdc_producer.h"

#include "yb/common/common_flags.h"
#include "yb/common/ysql_operation_lease.h"

#include "yb/consensus/consensus.h"
#include "yb/consensus/consensus.messages.h"
#include "yb/consensus/log_cache.h"
#include "yb/consensus/raft_consensus.h"

#include "yb/master/catalog_manager.h"
#include "yb/master/object_lock_info_manager.h"
#include "yb/master/ts_descriptor.h"

#include "yb/rpc/outbound_call.h"
#include "yb/rpc/rpc_controller.h"

#include "yb/server/clock.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/transaction_participant.h"

#include "yb/tserver/backup.pb.h"
#include "yb/tserver/local_catalog.proxy.h"

#include "yb/util/flag_validators.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/mem_tracker.h"
#include "yb/util/metrics.h"
#include "yb/util/monotime.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status_format.h"
#include "yb/util/thread.h"
#include "yb/util/unique_lock.h"

using namespace std::literals;

DEFINE_RUNTIME_int32(local_catalog_push_interval_ms, 60000,
    "How often the master leader pushes the system catalog changes to the tserver copies of the "
    "catalog. A push is skipped when the system catalog WAL has not moved and every copy was "
    "caught up by the last push.");
DEFINE_validator(local_catalog_push_interval_ms, FLAG_GT_VALUE_VALIDATOR(0));

DEFINE_RUNTIME_uint64(local_catalog_push_batch_max_bytes, 1_MB,
    "Approximate maximum size of the change records in one push to a tserver copy of the "
    "catalog.");
DEFINE_validator(local_catalog_push_batch_max_bytes, FLAG_GT_VALUE_VALIDATOR(0));

DEFINE_RUNTIME_int32(local_catalog_push_max_inflight_rpcs, 64,
    "Maximum number of push RPCs to the tserver copies of the catalog in flight at once. Further "
    "tservers wait for a free slot in arrival order.");
DEFINE_validator(local_catalog_push_max_inflight_rpcs, FLAG_GT_VALUE_VALIDATOR(0));

DEFINE_test_flag(int32, local_catalog_push_delay_before_term_check_ms, 0,
    "Delay each system catalog push cycle by this many milliseconds after it resolves intents and "
    "before it checks that the leader term has not changed.");

DEFINE_test_flag(int32, local_catalog_push_delay_before_send_ms, 0,
    "Delay each system catalog push cycle by this many milliseconds after it checks the leader "
    "term and before it sends to the tservers.");
DEFINE_test_flag(int32, local_catalog_push_log_gone_at_read, 0,
    "Have this read of the system catalog log in each push batch, counting from 1, find that the "
    "log no longer holds the entries after what the batch has read so far. 0 means no read does.");

DEFINE_test_flag(int32, local_catalog_push_empty_at_read, 0,
    "Have this read of the system catalog log in each push batch, counting from 1, return no "
    "entries, as when the log cache is over its memory limit. 0 means no read does.");

DEFINE_test_flag(bool, local_catalog_push_fail_batch_build, false,
    "Have every build of a system catalog push batch fail.");

METRIC_DECLARE_entity(cluster);

METRIC_DEFINE_gauge_uint64(cluster, local_catalog_push_safe_time_lag_ms,
    "Local catalog push safe time lag", yb::MetricUnit::kMilliseconds,
    "Time since the safe time of the last push cycle that caught up every tserver copy of the "
    "catalog. 0 when the last timed push cycle found nothing to push, or on a master that is not "
    "the leader.");
METRIC_DEFINE_gauge_uint64(cluster, local_catalog_push_max_target_lag_ops,
    "Local catalog push max copy lag", yb::MetricUnit::kEntries,
    "WAL entries between the end of the last push cycle and the tserver copy furthest behind it, "
    "among the copies that have answered with a position.");
METRIC_DEFINE_gauge_uint64(cluster, local_catalog_push_targets,
    "Local catalog push targets", yb::MetricUnit::kUnits,
    "Tservers holding a YSQL operation lease in the last push cycle.");
METRIC_DEFINE_gauge_uint64(cluster, local_catalog_push_targets_caught_up,
    "Local catalog push targets caught up", yb::MetricUnit::kUnits,
    "Tservers whose copy of the catalog reached the end and safe time of the last push cycle.");
METRIC_DEFINE_gauge_uint64(cluster, local_catalog_push_targets_without_copy,
    "Local catalog push targets without a copy", yb::MetricUnit::kUnits,
    "Tservers in the last push cycle that keep no copy of the catalog.");

METRIC_DEFINE_counter(cluster, local_catalog_push_cycles,
    "Local catalog push cycles", yb::MetricUnit::kOperations,
    "Push cycles that certified a safe time.");
METRIC_DEFINE_counter(cluster, local_catalog_push_cycles_skipped,
    "Local catalog push cycles skipped", yb::MetricUnit::kOperations,
    "Timed push cycles skipped because the system catalog WAL had not moved and every copy was "
    "caught up.");
METRIC_DEFINE_counter(cluster, local_catalog_push_cycle_failures,
    "Local catalog push cycle failures", yb::MetricUnit::kOperations,
    "Push cycles that failed.");
METRIC_DEFINE_counter(cluster, local_catalog_push_rpcs,
    "Local catalog push RPCs", yb::MetricUnit::kRequests,
    "Push RPCs sent to the tserver copies of the catalog.");
METRIC_DEFINE_counter(cluster, local_catalog_push_rpc_failures,
    "Local catalog push RPC failures", yb::MetricUnit::kRequests,
    "Pushes to a tserver that had no proxy or whose RPC failed.");
METRIC_DEFINE_counter(cluster, local_catalog_push_reseeds,
    "Local catalog push reseeds", yb::MetricUnit::kRequests,
    "Pushes telling a tserver copy of the catalog that it must be rebuilt.");
METRIC_DEFINE_counter(cluster, local_catalog_push_stalls,
    "Local catalog push stalls", yb::MetricUnit::kRequests,
    "Push answers in which the copy made no progress, other than answers to a reseed.");
METRIC_DEFINE_counter(cluster, local_catalog_push_bytes,
    "Local catalog push bytes", yb::MetricUnit::kBytes,
    "Bytes of push requests sent to the tserver copies of the catalog.");
METRIC_DEFINE_counter(cluster, local_catalog_push_records,
    "Local catalog push records", yb::MetricUnit::kEntries,
    "Change records sent to the tserver copies of the catalog.");

METRIC_DEFINE_event_stats(cluster, local_catalog_push_cycle_duration,
    "Local catalog push cycle duration", yb::MetricUnit::kMicroseconds,
    "Duration of a push cycle that certified a safe time.");
METRIC_DEFINE_event_stats(cluster, local_catalog_push_resolve_intents_wait,
    "Local catalog push resolve intents wait", yb::MetricUnit::kMicroseconds,
    "Time a push cycle waited for the catalog transactions committed at or before its safe time "
    "to be applied.");
METRIC_DEFINE_event_stats(cluster, local_catalog_push_batch_bytes,
    "Local catalog push batch size", yb::MetricUnit::kBytes,
    "Size of each push request built.");

namespace yb::master {

namespace {

using tserver::PushCatalogChangesResponsePB;

// A target that, this many times in one cycle, answers without progress is left for the next cycle.
constexpr int kMaxStallsPerCycle = 3;

// How often to check whether the committed index has reached a majority-replicated entry.
constexpr auto kCommitWaitInterval = 10ms;

// How often a cycle waiting for the safe time or for intents to resolve checks for shutdown.
constexpr auto kShutdownCheckInterval = 1s;

// How soon the pusher thread runs another cycle after one fails, when the push interval is longer.
constexpr std::chrono::milliseconds kFailedCycleRetryInterval = 1s;

Status ShutdownStatus() {
  return STATUS(ShutdownInProgress, "System catalog pusher is shutting down");
}

bool IsMethodMissing(const Status& status) {
  const auto rpc_error = rpc::RpcError(status);
  return rpc_error == rpc::ErrorStatusPB::ERROR_NO_SUCH_METHOD ||
         rpc_error == rpc::ErrorStatusPB::ERROR_NO_SUCH_SERVICE;
}

// The term to record for this master: the leader term, or, while this master is still the leader
// but without a lease for now, the term last recorded.
int64_t ObservedLeaderTerm(const consensus::Consensus& consensus, int64_t term, int64_t last_term) {
  if (term == OpId::kUnknownTerm && consensus.role() == PeerRole::LEADER) {
    return last_term;
  }
  return term;
}

// Whether this master is still the leader but cannot act as one for now, as while it has no leader
// lease.
bool LeaseLapsed(const consensus::Consensus& consensus, int64_t term) {
  return term == OpId::kUnknownTerm && consensus.role() == PeerRole::LEADER;
}

Status LeaseLapsedStatus() {
  return STATUS(TryAgain, "System catalog leader has no leader lease");
}

// Whether the entry restores the system catalog to an earlier time, as a point-in-time restore
// does. Applying it rewrites catalog rows that the log does not hold.
bool IsSysCatalogRestore(const consensus::LWReplicateMsg& msg) {
  return msg.op_type() == consensus::OperationType::SNAPSHOT_OP &&
         msg.snapshot_request().operation() ==
             tserver::TabletSnapshotOpRequestPB::RESTORE_SYS_CATALOG;
}

tserver::PushCatalogChangesRequestPB NewBatch(const OpId& from) {
  tserver::PushCatalogChangesRequestPB batch;
  from.ToPB(batch.mutable_from_op_id());
  from.ToPB(batch.mutable_to_op_id());
  return batch;
}

}  // namespace

class SysCatalogPusherMetrics {
 public:
  SysCatalogPusherMetrics(
      const scoped_refptr<MetricEntity>& entity, const MemTrackerPtr& parent_mem_tracker,
      server::Clock* clock)
      : max_target_lag_ops(METRIC_local_catalog_push_max_target_lag_ops.Instantiate(entity, 0)),
        targets(METRIC_local_catalog_push_targets.Instantiate(entity, 0)),
        targets_caught_up(METRIC_local_catalog_push_targets_caught_up.Instantiate(entity, 0)),
        targets_without_copy(
            METRIC_local_catalog_push_targets_without_copy.Instantiate(entity, 0)),
        cycles(METRIC_local_catalog_push_cycles.Instantiate(entity)),
        cycles_skipped(METRIC_local_catalog_push_cycles_skipped.Instantiate(entity)),
        cycle_failures(METRIC_local_catalog_push_cycle_failures.Instantiate(entity)),
        rpcs(METRIC_local_catalog_push_rpcs.Instantiate(entity)),
        rpc_failures(METRIC_local_catalog_push_rpc_failures.Instantiate(entity)),
        reseeds(METRIC_local_catalog_push_reseeds.Instantiate(entity)),
        stalls(METRIC_local_catalog_push_stalls.Instantiate(entity)),
        bytes(METRIC_local_catalog_push_bytes.Instantiate(entity)),
        records(METRIC_local_catalog_push_records.Instantiate(entity)),
        cycle_duration(METRIC_local_catalog_push_cycle_duration.Instantiate(entity)),
        resolve_intents_wait(METRIC_local_catalog_push_resolve_intents_wait.Instantiate(entity)),
        batch_bytes(METRIC_local_catalog_push_batch_bytes.Instantiate(entity)),
        mem_tracker(MemTracker::FindOrCreateTracker("SysCatalogPusher", parent_mem_tracker)),
        clock_(clock) {
    METRIC_local_catalog_push_safe_time_lag_ms.InstantiateFunctionGauge(
        entity, Bind(&SysCatalogPusherMetrics::SafeTimeLagMs, Unretained(this)))
        ->AutoDetach(&detacher_);
  }

  void LeaderChanged(bool is_leader) {
    caught_up_at_ = is_leader ? NowMicros() : 0;
    idle_ = false;
    max_target_lag_ops->set_value(0);
    targets->set_value(0);
    targets_caught_up->set_value(0);
    targets_without_copy->set_value(0);
  }

  // A cycle was not skipped, so the copies may lag until a cycle catches them all up.
  void NotSkipped() { idle_ = false; }

  // Every copy reached the given safe time.
  void CaughtUpAll(HybridTime safe_time) { caught_up_at_ = safe_time.GetPhysicalValueMicros(); }

  // A timed cycle found the WAL unchanged and every copy caught up, so the copies are current as
  // of now.
  void Skipped() {
    caught_up_at_ = NowMicros();
    idle_ = true;
    IncrementCounter(cycles_skipped);
  }

  const scoped_refptr<AtomicGauge<uint64_t>> max_target_lag_ops;
  const scoped_refptr<AtomicGauge<uint64_t>> targets;
  const scoped_refptr<AtomicGauge<uint64_t>> targets_caught_up;
  const scoped_refptr<AtomicGauge<uint64_t>> targets_without_copy;
  const CounterPtr cycles;
  const CounterPtr cycles_skipped;
  const CounterPtr cycle_failures;
  const CounterPtr rpcs;
  const CounterPtr rpc_failures;
  const CounterPtr reseeds;
  const CounterPtr stalls;
  const CounterPtr bytes;
  const CounterPtr records;
  const EventStatsPtr cycle_duration;
  const EventStatsPtr resolve_intents_wait;
  const EventStatsPtr batch_bytes;
  const MemTrackerPtr mem_tracker;

 private:
  MicrosTime NowMicros() const { return clock_->Now().GetPhysicalValueMicros(); }

  uint64_t SafeTimeLagMs() const {
    const MicrosTime since = caught_up_at_;
    if (since == 0 || idle_) {
      return 0;
    }
    const auto now = NowMicros();
    return now > since ? (now - since) / 1000 : 0;
  }

  server::Clock* const clock_;
  // 0 when this master is not the leader.
  std::atomic<MicrosTime> caught_up_at_{0};
  std::atomic<bool> idle_{false};
  std::shared_ptr<void> detacher_;
};

std::shared_ptr<SysCatalogPusherMetrics> CreateSysCatalogPusherMetrics(
    const scoped_refptr<MetricEntity>& metric_entity, const MemTrackerPtr& parent_mem_tracker,
    server::Clock* clock) {
  return std::make_shared<SysCatalogPusherMetrics>(metric_entity, parent_mem_tracker, clock);
}

struct SysCatalogPusher::Inbox {
  struct Answer {
    std::string ts_uuid;
    Status status;
    PushCatalogChangesResponsePB resp;
  };

  std::mutex mutex;
  std::condition_variable cv;
  std::deque<Answer> answers GUARDED_BY(mutex);
  bool closing GUARDED_BY(mutex) = false;

  void Add(Answer answer) {
    {
      std::lock_guard lock(mutex);
      // Answers to pushes still in flight at shutdown arrive after Close() and are not taken.
      if (closing) {
        return;
      }
      answers.push_back(std::move(answer));
    }
    cv.notify_all();
  }

  // Returns the next answer, or nullopt when shutting down.
  std::optional<Answer> Take() {
    UniqueLock lock(mutex);
    cv.wait(GetLockForCondition(lock), [this]() REQUIRES(mutex) {
      return closing || !answers.empty();
    });
    if (closing) {
      return std::nullopt;
    }
    auto answer = std::move(answers.front());
    answers.pop_front();
    return answer;
  }

  void Close() {
    {
      std::lock_guard lock(mutex);
      closing = true;
    }
    cv.notify_all();
  }
};

// A batch is charged to the memory tracker once, at its serialized size. The serialized copy that
// each push RPC holds while in flight is not charged.
struct SysCatalogPusher::Batch {
  tserver::PushCatalogChangesRequestPB request;
  size_t bytes;
  ScopedTrackedConsumption consumption;
};

struct SysCatalogPusher::CycleState {
  SysCatalogPusherMetrics* metrics;
  int64_t term;
  std::shared_ptr<tablet::TabletPeer> tablet_peer;
  std::shared_ptr<consensus::RaftConsensus> consensus;
  HybridTime safe_time;
  OpId end;
  std::vector<ObjectLockInfoManager::LiveLease> lease_holders;

  // The batches of this cycle by their from_op_id: a target at position P is sent the batch at P.
  // The slot at P lives while a target at P waits to be sent or waits for its answer. Its batch is
  // built when the first target at P is sent, and the targets after it reuse that batch.
  struct Slot {
    int users = 0;
    std::optional<Batch> batch;
  };
  std::map<OpId, Slot> slots;
  std::deque<Target*> waiting;
  int in_flight = 0;
  Status status;

  const Batch* AddBatch(const OpId& from, tserver::PushCatalogChangesRequestPB request) {
    const auto bytes = request.ByteSizeLong();
    IncrementStats(metrics->batch_bytes, bytes);
    return &slots[from].batch.emplace(Batch {
      .request = std::move(request),
      .bytes = bytes,
      .consumption = ScopedTrackedConsumption(metrics->mem_tracker, bytes),
    });
  }

  const Batch* FindBatch(const OpId& from) const {
    auto it = slots.find(from);
    return it != slots.end() && it->second.batch ? &*it->second.batch : nullptr;
  }

  static OpId From(const Target& target, const OpId& end) {
    return target.state.position.value_or(end);
  }

  void Enqueue(Target* target) {
    ++slots[From(*target, end)].users;
    waiting.push_back(target);
  }

  // Called once a target no longer waits for the batch from from.
  void Release(const OpId& from) {
    auto it = slots.find(from);
    DCHECK(it != slots.end()) << from;
    if (--it->second.users == 0) {
      slots.erase(it);
    }
  }
};

// Runs the pusher thread. Throws the Status of a failure to create the thread.
class SysCatalogPusher::RunThread {
 public:
  explicit RunThread(SysCatalogPusher* pusher) {
    auto status = Thread::Create(
        "catalog manager", "sys_catalog_pusher", &SysCatalogPusher::Run, pusher, &thread_);
    if (!status.ok()) {
      throw status;
    }
  }

  ~RunThread() {
    Stop();
  }

  // Waits for the thread to exit. Every caller waits, including one that calls it while another
  // is waiting.
  void Stop() {
    std::lock_guard lock(mutex_);
    if (thread_) {
      CHECK_OK(ThreadJoiner(thread_.get()).Join());
      thread_.reset();
    }
  }

 private:
  std::mutex mutex_;
  scoped_refptr<Thread> thread_ GUARDED_BY(mutex_);
};

SysCatalogPusher::SysCatalogPusher(
    CatalogManager& catalog_manager, std::shared_ptr<SysCatalogPusherMetrics> metrics)
    : catalog_manager_(catalog_manager), metrics_(std::move(metrics)),
      inbox_(std::make_shared<Inbox>()) {}

SysCatalogPusher::~SysCatalogPusher() {
  CompleteShutdown();
}

Status SysCatalogPusher::Start() {
  DCHECK(!thread_);
  try {
    thread_ = std::make_unique<RunThread>(this);
  } catch (const Status& status) {
    return status;
  }
  return Status::OK();
}

void SysCatalogPusher::StartShutdown() {
  {
    std::lock_guard lock(wake_mutex_);
    closing_ = true;
  }
  wake_cv_.notify_all();
  inbox_->Close();
}

void SysCatalogPusher::CompleteShutdown() {
  StartShutdown();
  if (thread_) {
    thread_->Stop();
  }
  // Waits for a cycle that another caller of TEST_RunCycle is running.
  std::lock_guard lock(cycle_mutex_);
  // Until the master starts a new pusher, it reports as a master that is not the leader.
  NoteLeaderTerm(OpId::kUnknownTerm);
}

bool SysCatalogPusher::WaitUnlessClosing(std::chrono::milliseconds timeout) {
  UniqueLock lock(wake_mutex_);
  wake_cv_.wait_for(
      GetLockForCondition(lock), timeout, [this]() REQUIRES(wake_mutex_) { return closing_; });
  return !closing_;
}

void SysCatalogPusher::RequestCycle() {
  {
    std::lock_guard lock(wake_mutex_);
    cycle_requested_ = true;
  }
  wake_cv_.notify_all();
}

void SysCatalogPusher::Run() {
  bool last_cycle_failed = false;
  for (;;) {
    bool requested;
    {
      UniqueLock lock(wake_mutex_);
      const std::chrono::milliseconds interval = FLAGS_local_catalog_push_interval_ms * 1ms;
      wake_cv_.wait_for(
          GetLockForCondition(lock),
          last_cycle_failed ? std::min(interval, kFailedCycleRetryInterval) : interval,
          [this]() REQUIRES(wake_mutex_) { return closing_ || cycle_requested_; });
      if (closing_) {
        return;
      }
      // A request made from here on starts another cycle after this one.
      requested = std::exchange(cycle_requested_, false);
    }
    std::lock_guard lock(cycle_mutex_);
    auto result = DoRunCycle(/* skip_if_wal_unchanged = */ !requested);
    last_cycle_failed = !result.ok();
    if (!result.ok() && !result.status().IsShutdownInProgress() && !result.status().IsTryAgain()) {
      YB_LOG_EVERY_N_SECS(WARNING, 60) << "System catalog push cycle failed: " << result.status();
    }
  }
}

Result<std::optional<SysCatalogPusher::PushCycleResult>> SysCatalogPusher::TEST_RunCycle() {
  std::lock_guard lock(cycle_mutex_);
  return DoRunCycle(/* skip_if_wal_unchanged = */ false);
}

struct SysCatalogPusher::CycleLeader {
  std::shared_ptr<tablet::TabletPeer> tablet_peer;
  std::shared_ptr<consensus::RaftConsensus> consensus;
  int64_t term;
  tablet::TabletPtr tablet;
  tablet::TransactionParticipant* participant;
  std::vector<ObjectLockInfoManager::LiveLease> lease_holders;
  // The deadline for deciding whether to skip the cycle and for certifying it.
  CoarseTimePoint deadline;
};

struct SysCatalogPusher::Certificate {
  HybridTime safe_time;
  int64_t end_index;
};

Result<std::optional<SysCatalogPusher::PushCycleResult>> SysCatalogPusher::DoRunCycle(
    bool skip_if_wal_unchanged) {
  const auto cycle_start = CoarseMonoClock::Now();
  auto leader = VERIFY_RESULT(PrepareCycle());
  if (!leader) {
    return std::nullopt;
  }
  if (skip_if_wal_unchanged && VERIFY_RESULT(WalUnchangedSinceCaughtUp(*leader))) {
    metrics_->Skipped();
    return std::nullopt;
  }
  metrics_->NotSkipped();
  auto result = CertifyAndPush(std::move(*leader), cycle_start);
  if (result.ok() || result.status().IsShutdownInProgress() || result.status().IsTryAgain()) {
    return result;
  }
  // A cycle cut short by a leader change returns as it would on a master that is not the leader.
  if (LeaderTermChanged()) {
    return std::nullopt;
  }
  if (LeaderLeaseLapsed()) {
    return LeaseLapsedStatus();
  }
  IncrementCounter(metrics_->cycle_failures);
  return result;
}

void SysCatalogPusher::NoteLeaderTerm(int64_t term) {
  if (term != leader_term_) {
    // Another leader may have pushed to the copies since this master last heard from them.
    for (auto& [_, target] : targets_) {
      target.state = TargetState();
    }
    last_cycle_caught_up_all_ = false;
  }
  const bool was_leader = leader_term_ != OpId::kUnknownTerm;
  const bool is_leader = term != OpId::kUnknownTerm;
  leader_term_ = term;
  if (is_leader != was_leader) {
    metrics_->LeaderChanged(is_leader);
  }
}

bool SysCatalogPusher::LeaderTermChanged() {
  auto tablet_peer = catalog_manager_.tablet_peer();
  if (!tablet_peer) {
    return false;
  }
  auto consensus = tablet_peer->GetRaftConsensus();
  if (!consensus.ok()) {
    return false;
  }
  const auto term = ObservedLeaderTerm(**consensus, (*consensus)->LeaderTerm(), leader_term_);
  if (term == leader_term_) {
    return false;
  }
  NoteLeaderTerm(term);
  return true;
}

bool SysCatalogPusher::LeaderLeaseLapsed() {
  auto tablet_peer = catalog_manager_.tablet_peer();
  if (!tablet_peer) {
    return false;
  }
  auto consensus = tablet_peer->GetRaftConsensus();
  return consensus.ok() && LeaseLapsed(**consensus, (*consensus)->LeaderTerm());
}

Result<std::optional<SysCatalogPusher::CycleLeader>> SysCatalogPusher::PrepareCycle() {
  if (!IsYsqlLeaseEnabled() || !FLAGS_ysql_yb_enable_invalidation_messages) {
    NoteLeaderTerm(OpId::kUnknownTerm);
    return std::nullopt;
  }
  auto tablet_peer = catalog_manager_.tablet_peer();
  if (!tablet_peer) {
    NoteLeaderTerm(OpId::kUnknownTerm);
    return std::nullopt;
  }
  auto consensus = VERIFY_RESULT(tablet_peer->GetRaftConsensus());
  const auto term = consensus->LeaderTerm();
  NoteLeaderTerm(ObservedLeaderTerm(*consensus, term, leader_term_));
  if (LeaseLapsed(*consensus, term)) {
    return LeaseLapsedStatus();
  }
  if (term == OpId::kUnknownTerm) {
    return std::nullopt;
  }
  auto tablet = VERIFY_RESULT(tablet_peer->shared_tablet());
  auto* participant = tablet->transaction_participant();
  if (!participant) {
    return STATUS(IllegalState, "System catalog tablet has no transaction participant");
  }
  // Until the participant has loaded every transaction, resolving intents can miss one.
  if (!participant->AllTransactionsLoaded()) {
    return STATUS(TryAgain, "System catalog transactions are not loaded yet");
  }
  auto lease_holders = catalog_manager_.object_lock_info_manager()->GetLiveLeases();
  // The address of a replaced tserver now reaches the tserver that replaced it.
  std::erase_if(lease_holders, [](const auto& holder) { return holder.ts->IsReplaced(); });
  return CycleLeader {
    .tablet_peer = std::move(tablet_peer),
    .consensus = std::move(consensus),
    .term = term,
    .tablet = std::move(tablet),
    .participant = participant,
    .lease_holders = std::move(lease_holders),
    .deadline = CoarseMonoClock::Now() + FLAGS_master_ts_rpc_timeout_ms * 1ms,
  };
}

Result<bool> SysCatalogPusher::WalUnchangedSinceCaughtUp(const CycleLeader& leader) {
  // TODO: A target without a copy is pushed again only once the WAL moves or its lease changes,
  // so a copy that appears under the same lease waits for the next catalog write. Have the tserver
  // report that it now keeps a copy.
  if (!last_cycle_caught_up_all_ || !LeaseHoldersAreTargets(leader)) {
    return false;
  }
  const auto index = VERIFY_RESULT(leader.consensus->ReadReplicatedMessagesForXCluster(
      OpId::Max(), leader.deadline, /* fetch_single_entry = */ true)).majority_replicated_index;
  return index == last_end_index_;
}

Status SysCatalogPusher::RetryInSlices(
    CoarseTimePoint deadline, const char* what,
    const std::function<Result<bool>(CoarseTimePoint)>& attempt) {
  for (;;) {
    auto done = attempt(std::min(deadline, CoarseMonoClock::Now() + kShutdownCheckInterval));
    {
      std::lock_guard lock(wake_mutex_);
      if (closing_) {
        return ShutdownStatus();
      }
    }
    if (VERIFY_RESULT(std::move(done))) {
      return Status::OK();
    }
    if (CoarseMonoClock::Now() >= deadline) {
      return STATUS(TimedOut, what);
    }
  }
}

Result<std::optional<SysCatalogPusher::Certificate>> SysCatalogPusher::Certify(
    const CycleLeader& leader) {
  const auto deadline = leader.deadline;
  HybridTime safe_time;
  RETURN_NOT_OK(RetryInSlices(
      deadline, "No leader safe time for the system catalog",
      [&leader, &safe_time](CoarseTimePoint slice_deadline) -> Result<bool> {
        auto result =
            leader.tablet->SafeTime(tablet::RequireLease::kTrue, HybridTime::kMin, slice_deadline);
        if (!result.ok()) {
          if (result.status().IsTimedOut()) {
            return false;
          }
          return result.status();
        }
        if (result->is_special()) {
          return false;
        }
        safe_time = *result;
        return true;
      }));
  // Every catalog transaction that committed at or before safe_time has its apply record in the
  // WAL once this returns.
  const auto resolve_start = CoarseMonoClock::Now();
  RETURN_NOT_OK(RetryInSlices(
      deadline, "System catalog intents did not resolve",
      [this, &leader, safe_time](CoarseTimePoint slice_deadline) -> Result<bool> {
        auto status = leader.participant->ResolveIntents(
            safe_time, slice_deadline, tablet::WaitForApply::kFalse);
        if (status.IsTimedOut()) {
          ++TEST_resolve_intents_timeouts_;
          return false;
        }
        RETURN_NOT_OK(status);
        return true;
      }));
  IncrementStats(
      metrics_->resolve_intents_wait, ToMicroseconds(CoarseMonoClock::Now() - resolve_start));

  // Every entry at or before safe_time, including the apply records waited for above, is at or
  // before end_index. The committed index can still be behind end_index; BuildBatch waits for it.
  const auto end_index = VERIFY_RESULT(leader.consensus->ReadReplicatedMessagesForXCluster(
      OpId::Max(), deadline, /* fetch_single_entry = */ true)).majority_replicated_index;
  if (FLAGS_TEST_local_catalog_push_delay_before_term_check_ms > 0) {
    SleepFor(MonoDelta::FromMilliseconds(FLAGS_TEST_local_catalog_push_delay_before_term_check_ms));
  }
  if (const auto current_term = leader.consensus->LeaderTerm(); current_term != leader.term) {
    NoteLeaderTerm(ObservedLeaderTerm(*leader.consensus, current_term, leader_term_));
    if (LeaseLapsed(*leader.consensus, current_term)) {
      return LeaseLapsedStatus();
    }
    return std::nullopt;
  }
  return Certificate {
    .safe_time = safe_time,
    .end_index = end_index,
  };
}

Result<std::optional<SysCatalogPusher::PushCycleResult>> SysCatalogPusher::CertifyAndPush(
    CycleLeader leader, CoarseTimePoint cycle_start) {
  const auto certificate = VERIFY_RESULT(Certify(leader));
  if (!certificate) {
    return std::nullopt;
  }
  const auto safe_time = certificate->safe_time;
  const auto end_index = certificate->end_index;
  IncrementCounter(metrics_->cycles);
  auto record_duration = ScopeExit([this, cycle_start] {
    IncrementStats(metrics_->cycle_duration, ToMicroseconds(CoarseMonoClock::Now() - cycle_start));
  });
  if (FLAGS_TEST_local_catalog_push_delay_before_send_ms > 0) {
    SleepFor(MonoDelta::FromMilliseconds(FLAGS_TEST_local_catalog_push_delay_before_send_ms));
  }

  CycleState cycle;
  cycle.metrics = metrics_.get();
  cycle.term = leader.term;
  cycle.tablet_peer = std::move(leader.tablet_peer);
  cycle.consensus = std::move(leader.consensus);
  cycle.safe_time = safe_time;
  cycle.end = OpId(OpId::kUnknownTerm, end_index);
  cycle.lease_holders = std::move(leader.lease_holders);
  // end carries no term, so a target at an unknown position answers an empty batch at end with GAP
  // and its own position.
  last_cycle_caught_up_all_ = false;
  RETURN_NOT_OK(SendToTargets(&cycle));
  auto summary = SummarizeTargets(end_index, safe_time);
  UpdateTargetMetrics(summary);
  RETURN_NOT_OK(cycle.status);
  last_end_index_ = end_index;

  // A target that keeps no copy needs no further push until it gets a new lease or the WAL moves.
  last_cycle_caught_up_all_ =
      summary.caught_up_ts_uuids.size() + summary.without_copy == targets_.size();
  if (last_cycle_caught_up_all_) {
    metrics_->CaughtUpAll(safe_time);
  }
  return PushCycleResult {
    .safe_time = safe_time,
    .end_index = end_index,
    .caught_up_ts_uuids = std::move(summary.caught_up_ts_uuids),
  };
}

SysCatalogPusher::TargetSummary SysCatalogPusher::SummarizeTargets(
    int64_t end_index, HybridTime safe_time) {
  TargetSummary summary;
  for (const auto& [uuid, target] : targets_) {
    if (target.CaughtUp(end_index, safe_time)) {
      summary.caught_up_ts_uuids.push_back(uuid);
    } else if (target.no_copy) {
      ++summary.without_copy;
    } else if (target.state.position) {
      summary.max_lag = std::max(summary.max_lag, end_index - target.state.position->index);
    }
  }
  return summary;
}

void SysCatalogPusher::UpdateTargetMetrics(const TargetSummary& summary) {
  metrics_->max_target_lag_ops->set_value(summary.max_lag);
  metrics_->targets->set_value(targets_.size());
  metrics_->targets_caught_up->set_value(summary.caught_up_ts_uuids.size());
  metrics_->targets_without_copy->set_value(summary.without_copy);
}

bool SysCatalogPusher::LeaseHoldersAreTargets(const CycleLeader& leader) {
  if (leader.lease_holders.size() != targets_.size()) {
    return false;
  }
  for (const auto& holder : leader.lease_holders) {
    auto it = targets_.find(holder.ts->permanent_uuid());
    if (it == targets_.end() || it->second.lease_epoch != holder.lease_epoch) {
      return false;
    }
  }
  return true;
}

void SysCatalogPusher::RebuildTargets(CycleState* cycle) {
  // TODO: Right after a leader change the live lease set may not be rebuilt yet, so a lease holder
  // can be left out until the next cycle. Choose targets only once the set is rebuilt.
  decltype(targets_) targets;
  for (auto& holder : cycle->lease_holders) {
    const auto& uuid = holder.ts->permanent_uuid();
    auto node = targets_.extract(uuid);
    auto& target =
        (node ? targets.insert(std::move(node)).position : targets.try_emplace(uuid).first)->second;
    // A new lease may be held by a new process, whose copy is not the one the last answer was
    // about.
    if (target.lease_epoch != holder.lease_epoch) {
      target.state = TargetState();
      target.no_copy = false;
      target.no_service = false;
      target.lease_epoch = holder.lease_epoch;
    }
    target.done = false;
    target.stalls = 0;
    target.ts = std::move(holder.ts);
  }
  targets_ = std::move(targets);
}

Status SysCatalogPusher::SendToTargets(CycleState* cycle) {
  RebuildTargets(cycle);

  auto needs_push = [cycle](const Target& target) {
    return !target.done && !target.no_service &&
           !target.CaughtUp(cycle->end.index, cycle->safe_time);
  };

  for (auto& [_, target] : targets_) {
    if (needs_push(target)) {
      cycle->Enqueue(&target);
    }
  }

  const auto max_inflight = FLAGS_local_catalog_push_max_inflight_rpcs;
  // TODO: A cycle ends only when every target is caught up or has left it, so a slow or far-behind
  // target delays the next safe time for every copy, and one failed batch build stops the pushes
  // to the targets still queued. Replace the cycle loop with per-target scheduling: certify on its
  // own, keep one push in flight per target toward the latest certificate, and back off only the
  // target that fails.
  while (!cycle->waiting.empty() || cycle->in_flight > 0) {
    // After a failure the cycle fails, so no further target is sent; the answers in flight are
    // still taken.
    while (cycle->status.ok() && cycle->in_flight < max_inflight && !cycle->waiting.empty()) {
      auto* target = cycle->waiting.front();
      cycle->waiting.pop_front();
      Send(cycle, target);
    }
    if (cycle->in_flight == 0) {
      break;
    }

    auto answer = inbox_->Take();
    if (!answer) {
      return ShutdownStatus();
    }
    auto it = targets_.find(answer->ts_uuid);
    if (it == targets_.end()) {
      LOG(DFATAL) << "Push answer from " << answer->ts_uuid << ", which is not a target";
      --cycle->in_flight;
      continue;
    }
    auto& target = it->second;
    --cycle->in_flight;
    const auto sent_from = CycleState::From(target, cycle->end);
    const auto* sent = cycle->FindBatch(sent_from);
    const bool reseed = sent && sent->request.reseed_required();
    // A target queued again at the same position is sent the batch it was sent before.
    auto release = ScopeExit([cycle, &sent_from] { cycle->Release(sent_from); });

    if (!answer->status.ok()) {
      target.done = true;
      if (IsMethodMissing(answer->status)) {
        target.no_copy = true;
        target.no_service = true;
        target.state = TargetState();
      } else {
        IncrementCounter(metrics_->rpc_failures);
        YB_LOG_EVERY_N_SECS(WARNING, 60)
            << "System catalog push to " << answer->ts_uuid << " failed: " << answer->status;
      }
      continue;
    }

    const auto& resp = answer->resp;
    if (resp.code() == PushCatalogChangesResponsePB::NO_COPY) {
      target.state = TargetState();
      target.no_copy = true;
      target.done = true;
      continue;
    }
    // A code this master does not know parses as no code.
    if (!resp.has_code() || !resp.has_applied_op_id()) {
      YB_LOG_EVERY_N_SECS(WARNING, 60)
          << "System catalog push to " << answer->ts_uuid << " got an answer without a code or "
          << "position: " << resp.ShortDebugString();
      target.done = true;
      continue;
    }
    target.no_copy = false;

    const auto previous = target.state;
    target.state.position = OpId::FromPB(resp.applied_op_id());
    target.state.safe_time =
        resp.has_safe_time() ? HybridTime(resp.safe_time()) : HybridTime::kInvalid;
    // The copy progressed if:
    // - its position was not known before,
    // - its index moved forward, or
    // - it kept its position and gained a safe time or a newer one, as in the final answer to a
    //   cycle.
    const bool progressed =
        !previous.position || target.state.position->index > previous.position->index ||
        (*previous.position == *target.state.position && target.state.safe_time.is_valid() &&
         (!previous.safe_time.is_valid() || target.state.safe_time > previous.safe_time));
    // A copy answers a reseed without moving, which is not a stall.
    const bool stalled = !progressed && !reseed;
    if (stalled) {
      IncrementCounter(metrics_->stalls);
    }
    if (stalled && ++target.stalls > kMaxStallsPerCycle) {
      LOG(WARNING) << "System catalog push to " << answer->ts_uuid << " keeps answering at "
                   << *target.state.position << ", leaving it for the next cycle";
      target.done = true;
    }
    // An answer that changes nothing would repeat forever.
    if (previous.position == target.state.position &&
        previous.safe_time == target.state.safe_time) {
      VLOG(1) << "System catalog push to " << answer->ts_uuid << " got the same answer again at "
              << *target.state.position << ", leaving it for the next cycle";
      target.done = true;
    }
    // A copy told to reseed is replaced, so nothing more is pushed to it this cycle.
    if (reseed) {
      target.done = true;
    }
    if (needs_push(target)) {
      cycle->Enqueue(&target);
    } else {
      target.done = true;
    }
  }
  return Status::OK();
}

void SysCatalogPusher::Send(CycleState* cycle, Target* target) {
  // A target whose position is unknown is sent an empty batch at the end, and answers with its
  // position.
  const auto from = CycleState::From(*target, cycle->end);
  auto fail_cycle = [cycle, target, &from](Status status) {
    cycle->status = std::move(status);
    target->done = true;
    cycle->Release(from);
  };
  const auto* batch = cycle->FindBatch(from);
  if (!batch) {
    // TODO: Building a batch can wait up to master_ts_rpc_timeout_ms for the committed index, once
    // per distinct position, on this thread and without handling answers meanwhile.
    auto built = BuildBatch(cycle, from);
    if (!built.ok()) {
      if (!built.status().IsShutdownInProgress()) {
        YB_LOG_EVERY_N_SECS(WARNING, 60)
            << "Cannot push the system catalog from " << from << " to "
            << target->ts->permanent_uuid() << ": " << built.status();
        ++TEST_failed_batch_builds_;
      }
      fail_cycle(built.status());
      return;
    }
    batch = *built;
  }
  // A master that is no longer the leader, or that has lost its leader lease and so may already
  // have been replaced, can lack the entries that the new leader pushed, so it must not tell a copy
  // to throw its own away. Checked at every send of a reseed, including one built for an earlier
  // target.
  // TODO: The leader can still change between this check and the push. Carry the leader term in
  // the request so that a copy refuses a reseed from an older term.
  if (batch->request.reseed_required()) {
    const auto term = cycle->consensus->LeaderTerm();
    if (LeaseLapsed(*cycle->consensus, term)) {
      fail_cycle(LeaseLapsedStatus());
      return;
    }
    if (term != cycle->term) {
      fail_cycle(STATUS(IllegalState, "System catalog leader changed during the push cycle"));
      return;
    }
  }

  std::shared_ptr<tserver::LocalCatalogServiceProxy> proxy;
  auto status = target->ts->GetProxy(&proxy);
  if (!status.ok()) {
    IncrementCounter(metrics_->rpc_failures);
    LOG(WARNING) << "No proxy to " << target->ts->permanent_uuid() << ": " << status;
    target->done = true;
    cycle->Release(from);
    return;
  }

  struct Call {
    rpc::RpcController controller;
    PushCatalogChangesResponsePB resp;
  };
  auto call = std::make_shared<Call>();
  call->controller.set_timeout(FLAGS_master_ts_rpc_timeout_ms * 1ms);
  ++cycle->in_flight;
  IncrementCounter(metrics_->rpcs);
  IncrementCounterBy(metrics_->bytes, batch->bytes);
  IncrementCounterBy(metrics_->records, batch->request.records_size());
  if (batch->request.reseed_required()) {
    IncrementCounter(metrics_->reseeds);
  }
  // TODO: The batch is serialized again for every target sent it, and these copies are not charged
  // to the memory tracker. Serialize each batch once and send the bytes, and bound the bytes in
  // flight rather than only the number of RPCs, so a catch-up of large batches cannot hold
  // local_catalog_push_max_inflight_rpcs times local_catalog_push_batch_max_bytes at once.
  proxy->PushCatalogChangesAsync(
      batch->request, &call->resp, &call->controller,
      [inbox = inbox_, call, uuid = target->ts->permanent_uuid()]() mutable {
        inbox->Add(Inbox::Answer {
          .ts_uuid = std::move(uuid),
          .status = call->controller.status(),
          .resp = std::move(call->resp),
        });
      });
}

Result<const SysCatalogPusher::Batch*> SysCatalogPusher::BuildBatch(
    CycleState* cycle, const OpId& from) {
  if (FLAGS_TEST_local_catalog_push_fail_batch_build) {
    return STATUS(IllegalState, "Failing the batch build for a test");
  }
  const auto deadline = CoarseMonoClock::Now() + FLAGS_master_ts_rpc_timeout_ms * 1ms;
  auto reseed = [cycle, &from] {
    auto batch = NewBatch(from);
    batch.set_reseed_required(true);
    return cycle->AddBatch(from, std::move(batch));
  };

  // A copy's own position always has a term; only the master's end lacks one.
  if (from.term == OpId::kUnknownTerm && from.index < cycle->end.index) {
    return reseed();
  }
  // A copy whose entry at its position differs from this log's, or is not in it, has diverged
  // from it or is too far behind.
  if (from.index > 0 && from.term != OpId::kUnknownTerm) {
    auto op_id = cycle->consensus->LookupOpId(from.index);
    if (!op_id.ok()) {
      if (op_id.status().IsNotFound() || op_id.status().IsIncomplete()) {
        return reseed();
      }
      return op_id.status();
    }
    if (*op_id != from) {
      return reseed();
    }
  }

  if (from.index >= cycle->end.index) {
    auto batch = NewBatch(from);
    if (from.term != OpId::kUnknownTerm) {
      batch.set_safe_time(cycle->safe_time.ToUint64());
    }
    return cycle->AddBatch(from, std::move(batch));
  }
  // A copy always starts from a remote bootstrap at a real position, so a copy at index 0 has
  // nothing to build on.
  if (from.index == 0) {
    return reseed();
  }

  cdc::XClusterRecordsContext records_context {
    .tablet_peer = *cycle->tablet_peer,
    .db_oid_to_get_sequences_for = std::nullopt,
    .include_subtransactions = true,
    .update_on_split_op_func = {},
  };
  const auto max_bytes = FLAGS_local_catalog_push_batch_max_bytes;

  auto batch = NewBatch(from);
  size_t batch_bytes = 0;
  OpId cursor = from;
  // When the log no longer holds the entries after the cursor, the copy is sent what was read so
  // far, if anything.
  auto send_partial_or_reseed = [cycle, &from, &cursor, &batch, &reseed]() -> Result<const Batch*> {
    if (cursor == from) {
      return reseed();
    }
    return cycle->AddBatch(from, std::move(batch));
  };
  int reads = 0;
  int attempts = 0;
  while (true) {
    auto read_result = cycle->consensus->ReadReplicatedMessagesForXCluster(
        cursor, deadline, /* fetch_single_entry = */ false);
    if (!read_result.ok()) {
      if (cdc::CDCError(read_result.status()) == cdc::CDCErrorPB::CHECKPOINT_TOO_OLD) {
        return send_partial_or_reseed();
      }
      return read_result.status();
    }
    auto& messages = read_result->result.messages;
    if (++attempts == FLAGS_TEST_local_catalog_push_empty_at_read) {
      messages.clear();
    }
    if (messages.empty()) {
      // The committed index has not reached end yet, or the log cache is over its memory limit.
      // The copy is sent what was read so far rather than waiting with it.
      if (cursor != from) {
        return cycle->AddBatch(from, std::move(batch));
      }
      if (CoarseMonoClock::Now() >= deadline) {
        return STATUS_FORMAT(
            TimedOut, "No committed entries after $0 to read up to $1; the committed index may be "
            "behind or the log cache over its memory limit", cursor, cycle->end.index);
      }
      if (!WaitUnlessClosing(kCommitWaitInterval)) {
        return ShutdownStatus();
      }
      continue;
    }
    ++reads;
    // The log no longer holds the entry right after the cursor.
    if (messages.front()->id().index() != cursor.index + 1 ||
        reads == FLAGS_TEST_local_catalog_push_log_gone_at_read) {
      return send_partial_or_reseed();
    }

    // TODO: Each batch rereads the log from its start, and the conversion runs once per message.
    // Convert a read range once, passing PopulateXClusterRecords a span of it, and cut batches at
    // record boundaries.
    for (const auto& msg : messages) {
      // A copy cannot be caught up past a restore of the catalog, whose rows are not in the log.
      if (IsSysCatalogRestore(*msg)) {
        return send_partial_or_reseed();
      }
      const auto records_before = batch.records_size();
      auto converted = VERIFY_RESULT(cdc::PopulateXClusterRecords(
          records_context, consensus::ReplicateMsgs{msg}, batch.mutable_records()));
      for (int i = records_before; i < batch.records_size(); ++i) {
        batch_bytes += batch.records(i).ByteSizeLong();
      }
      cursor = OpId::FromPB(msg->id());
      cursor.ToPB(batch.mutable_to_op_id());

      const bool at_end = cursor.index == cycle->end.index;
      if (at_end || converted.stopped_at_change_metadata || batch_bytes >= max_bytes) {
        if (at_end) {
          batch.set_safe_time(cycle->safe_time.ToUint64());
        }
        return cycle->AddBatch(from, std::move(batch));
      }
    }
  }
}

std::optional<SysCatalogPusher::TargetState> SysCatalogPusher::TEST_GetTargetState(
    const std::string& ts_uuid) {
  std::lock_guard lock(cycle_mutex_);
  auto it = targets_.find(ts_uuid);
  if (it == targets_.end()) {
    return std::nullopt;
  }
  return it->second.state;
}

}  // namespace yb::master
