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

#include "yb/consensus/raft_consensus.h"

#include <algorithm>
#include <memory>
#include <mutex>

#include <boost/optional.hpp>

#include "yb/common/wire_protocol.h"

#include "yb/consensus/consensus.messages.h"
#include "yb/consensus/consensus_context.h"
#include "yb/consensus/consensus_peers.h"
#include "yb/consensus/consensus_round.h"
#include "yb/consensus/leader_election.h"
#include "yb/consensus/log.h"
#include "yb/consensus/opid_util.h"
#include "yb/consensus/peer_manager.h"
#include "yb/consensus/quorum_util.h"
#include "yb/consensus/replica_state.h"
#include "yb/consensus/state_change_context.h"

#include "yb/gutil/casts.h"
#include "yb/gutil/map-util.h"
#include "yb/gutil/stringprintf.h"

#include "yb/master/sys_catalog_constants.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/periodic.h"
#include "yb/rpc/rpc_controller.h"
#include "yb/rpc/strand.h"

#include "yb/server/clock.h"

#include "yb/util/callsite_profiling.h"
#include "yb/util/debug-util.h"
#include "yb/util/debug/long_operation_tracker.h"
#include "yb/util/debug/trace_event.h"
#include "yb/util/enums.h"
#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/memory/memory.h"
#include "yb/util/metrics.h"
#include "yb/util/net/dns_resolver.h"
#include "yb/util/random.h"
#include "yb/util/random_util.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/threadpool.h"
#include "yb/util/tostring.h"
#include "yb/util/trace.h"
#include "yb/util/tsan_util.h"
#include "yb/util/url-coding.h"

using namespace std::literals;
using namespace std::placeholders;

DEFINE_UNKNOWN_int32(raft_heartbeat_interval_ms, yb::NonTsanVsTsan(500, 1000),
             "The heartbeat interval for Raft replication. The leader produces heartbeats "
             "to followers at this interval. The followers expect a heartbeat at this interval "
             "and consider a leader to have failed if it misses several in a row.");
TAG_FLAG(raft_heartbeat_interval_ms, advanced);

DEFINE_UNKNOWN_double(leader_failure_max_missed_heartbeat_periods, 6.0,
              "Maximum heartbeat periods that the leader can fail to heartbeat in before we "
              "consider the leader to be failed. The total failure timeout in milliseconds is "
              "raft_heartbeat_interval_ms times leader_failure_max_missed_heartbeat_periods. "
              "The value passed to this flag may be fractional.");
TAG_FLAG(leader_failure_max_missed_heartbeat_periods, advanced);

DEFINE_UNKNOWN_int32(leader_failure_exp_backoff_max_delta_ms, 20 * 1000,
             "Maximum time to sleep in between leader election retries, in addition to the "
             "regular timeout. When leader election fails the interval in between retries "
             "increases exponentially, up to this value.");
TAG_FLAG(leader_failure_exp_backoff_max_delta_ms, experimental);

DEFINE_UNKNOWN_bool(enable_leader_failure_detection, true,
            "Whether to enable failure detection of tablet leaders. If enabled, attempts will be "
            "made to elect a follower as a new leader when the leader is detected to have failed.");
TAG_FLAG(enable_leader_failure_detection, unsafe);

DEFINE_test_flag(bool, do_not_start_election_test_only, false,
                 "Do not start election even if leader failure is detected. ");

DEFINE_UNKNOWN_bool(evict_failed_followers, true,
            "Whether to evict followers from the Raft config that have fallen "
            "too far behind the leader's log to catch up normally or have been "
            "unreachable by the leader for longer than "
            "follower_unavailable_considered_failed_sec");
TAG_FLAG(evict_failed_followers, advanced);

DEFINE_test_flag(bool, follower_reject_update_consensus_requests, false,
                 "Whether a follower will return an error for all UpdateConsensus() requests.");

DEFINE_test_flag(bool, follower_pause_update_consensus_requests, false,
                 "Whether a follower will pause all UpdateConsensus() requests.");

DEFINE_test_flag(int32, follower_reject_update_consensus_requests_seconds, 0,
                 "Whether a follower will return an error for all UpdateConsensus() requests for "
                 "the first TEST_follower_reject_update_consensus_requests_seconds seconds after "
                 "the Consensus objet is created.");

DEFINE_test_flag(bool, leader_skip_no_op, false,
                 "Whether a leader replicate NoOp to follower.");

DEFINE_test_flag(bool, follower_fail_all_prepare, false,
                 "Whether a follower will fail preparing all operations.");

DEFINE_UNKNOWN_int32(after_stepdown_delay_election_multiplier, 5,
             "After a peer steps down as a leader, the factor with which to multiply "
             "leader_failure_max_missed_heartbeat_periods to get the delay time before starting a "
             "new election.");
TAG_FLAG(after_stepdown_delay_election_multiplier, advanced);
TAG_FLAG(after_stepdown_delay_election_multiplier, hidden);

DECLARE_int32(memory_limit_warn_threshold_percentage);

DEFINE_test_flag(int32, inject_delay_leader_change_role_append_secs, 0,
                 "Amount of time to delay leader from sending replicate of change role.");

DEFINE_test_flag(double, return_error_on_change_config, 0.0,
                 "Fraction of the time when ChangeConfig will return an error.");

DEFINE_test_flag(bool, pause_before_replicate_batch, false,
                 "Whether to pause before doing DoReplicateBatch.");

DEFINE_test_flag(bool, request_vote_respond_leader_still_alive, false,
                 "Fake rejection to vote due to leader still alive");

METRIC_DEFINE_counter(tablet, follower_memory_pressure_rejections,
                      "Follower Memory Pressure Rejections",
                      yb::MetricUnit::kRequests,
                      "Number of RPC requests rejected due to "
                      "memory pressure while FOLLOWER.");
METRIC_DEFINE_gauge_int64(tablet, raft_term,
                          "Current Raft Consensus Term",
                          yb::MetricUnit::kUnits,
                          "Current Term of the Raft Consensus algorithm. This number increments "
                          "each time a leader election is started.");

METRIC_DEFINE_lag(tablet, follower_lag_ms,
                  "Follower lag from leader",
                  "The amount of time since the last UpdateConsensus request from the "
                  "leader.", {0, yb::AggregationFunction::kMax} /* optional_args */);

METRIC_DEFINE_gauge_int64(tablet, is_raft_leader,
                          "Is tablet raft leader",
                          yb::MetricUnit::kUnits,
                          "Keeps track whether tablet is raft leader"
                          "1 indicates that the tablet is raft leader");

METRIC_DEFINE_event_stats(
  table, dns_resolve_latency_during_update_raft_config,
  "yb.consensus.RaftConsensus.UpdateRaftConfig DNS Resolve",
  yb::MetricUnit::kMicroseconds,
  "Microseconds spent resolving DNS requests during RaftConsensus::UpdateRaftConfig");

DEFINE_UNKNOWN_int32(leader_lease_duration_ms, yb::consensus::kDefaultLeaderLeaseDurationMs,
             "Leader lease duration. A leader keeps establishing a new lease or extending the "
             "existing one with every UpdateConsensus. A new server is not allowed to serve as a "
             "leader (i.e. serve up-to-date read requests or acknowledge write requests) until a "
             "lease of this duration has definitely expired on the old leader's side.");

DEFINE_UNKNOWN_int32(ht_lease_duration_ms, 2000,
             "Hybrid time leader lease duration. A leader keeps establishing a new lease or "
             "extending the existing one with every UpdateConsensus. A new server is not allowed "
             "to add entries to RAFT log until a lease of the old leader is expired. 0 to disable."
             );

DEFINE_UNKNOWN_int32(min_leader_stepdown_retry_interval_ms,
             20 * 1000,
             "Minimum amount of time between successive attempts to perform the leader stepdown "
             "for the same combination of tablet and intended (target) leader. This is needed "
             "to avoid infinite leader stepdown loops when the current leader never has a chance "
             "to update the intended leader with its latest records.");

DEFINE_UNKNOWN_bool(use_preelection, true,
    "Whether to use pre election, before doing actual election.");

DEFINE_UNKNOWN_int32(temporary_disable_preelections_timeout_ms, 10 * 60 * 1000,
             "If some of nodes does not support preelections, then we disable them for this "
             "amount of time.");

DEFINE_test_flag(bool, pause_update_replica, false,
                 "Pause RaftConsensus::UpdateReplica processing before snoozing failure detector.");

DEFINE_test_flag(bool, pause_update_majority_replicated, false,
                 "Pause RaftConsensus::UpdateMajorityReplicated.");

DEFINE_test_flag(int32, log_change_config_every_n, 1,
                 "How often to log change config information. "
                 "Used to reduce the number of lines being printed for change config requests "
                 "when a test simulates a failure that would generate a log of these requests.");

DEFINE_UNKNOWN_bool(enable_lease_revocation, true, "Enables lease revocation mechanism");

DEFINE_UNKNOWN_bool(quick_leader_election_on_create, false,
            "Do we trigger quick leader elections on table creation.");
TAG_FLAG(quick_leader_election_on_create, advanced);
TAG_FLAG(quick_leader_election_on_create, hidden);

DEFINE_UNKNOWN_bool(
    stepdown_disable_graceful_transition, false,
    "During a leader stepdown, disable graceful leadership transfer "
    "to an up to date peer");

DEFINE_UNKNOWN_bool(
    raft_disallow_concurrent_outstanding_report_failure_tasks, true,
    "If true, only submit a new report failure task if there is not one outstanding.");
TAG_FLAG(raft_disallow_concurrent_outstanding_report_failure_tasks, advanced);
TAG_FLAG(raft_disallow_concurrent_outstanding_report_failure_tasks, hidden);

DEFINE_UNKNOWN_int64(protege_synchronization_timeout_ms, 1000,
             "Timeout to synchronize protege before performing step down. "
             "0 to disable synchronization.");

DEFINE_test_flag(bool, skip_election_when_fail_detected, false,
                 "Inside RaftConsensus::ReportFailureDetectedTask, skip normal election.");

DEFINE_test_flag(bool, pause_replica_start_before_triggering_pending_operations, false,
                 "Whether to pause before triggering pending operations in RaftConsensus::Start");

namespace yb {
namespace consensus {

using rpc::PeriodicTimer;
using std::shared_ptr;
using std::unique_ptr;
using std::weak_ptr;
using std::string;
using strings::Substitute;
using tserver::TabletServerErrorPB;

namespace {

const RaftPeerPB* FindPeer(const RaftConfigPB& active_config, const std::string& uuid) {
  for (const RaftPeerPB& peer : active_config.peers()) {
    if (peer.permanent_uuid() == uuid) {
      return &peer;
    }
  }

  return nullptr;
}

// Helper function to check if the op is a non-Operation op.
bool IsConsensusOnlyOperation(OperationType op_type) {
  return op_type == NO_OP || op_type == CHANGE_CONFIG_OP;
}

// Helper to check if the op is Change Config op.
bool IsChangeConfigOperation(OperationType op_type) {
  return op_type == CHANGE_CONFIG_OP;
}

class NonTrackedRoundCallback : public ConsensusRoundCallback {
 public:
  explicit NonTrackedRoundCallback(ConsensusRound* round, const StdStatusCallback& callback)
      : round_(DCHECK_NOTNULL(round)), callback_(callback) {
  }

  Status AddedToLeader(const OpId& op_id, const OpId& committed_op_id) override {
    auto& replicate_msg = *round_->replicate_msg();
    op_id.ToPB(replicate_msg.mutable_id());
    committed_op_id.ToPB(replicate_msg.mutable_committed_op_id());
    return Status::OK();
  }

  void ReplicationFinished(
      const Status& status, int64_t leader_term, OpIds* applied_op_ids) override {
    down_cast<RaftConsensus*>(round_->consensus())
        ->NonTrackedRoundReplicationFinished(round_, callback_, status);
  }

 private:
  ConsensusRound* round_;
  StdStatusCallback callback_;
};

} // namespace

std::unique_ptr<ConsensusRoundCallback> MakeNonTrackedRoundCallback(
    ConsensusRound* round, const StdStatusCallback& callback) {
  return std::make_unique<NonTrackedRoundCallback>(round, callback);
}

struct RaftConsensus::LeaderRequest {
  std::string leader_uuid;
  OpId preceding_op_id;
  OpId committed_op_id;
  ReplicateMsgs messages;
  // The positional index of the first message selected to be appended, in the
  // original leader's request message sequence.
  int64_t first_message_idx;

  std::string OpsRangeString() const;
};

shared_ptr<RaftConsensus> RaftConsensus::Create(
    const ConsensusOptions& options,
    std::unique_ptr<ConsensusMetadata> cmeta,
    const RaftPeerPB& local_peer_pb,
    const scoped_refptr<MetricEntity>& table_metric_entity,
    const scoped_refptr<MetricEntity>& tablet_metric_entity,
    const scoped_refptr<server::Clock>& clock,
    ConsensusContext* consensus_context,
    rpc::Messenger* messenger,
    rpc::ProxyCache* proxy_cache,
    const scoped_refptr<log::Log>& log,
    const shared_ptr<MemTracker>& server_mem_tracker,
    const shared_ptr<MemTracker>& parent_mem_tracker,
    const Callback<void(std::shared_ptr<StateChangeContext> context)> mark_dirty_clbk,
    TableType table_type,
    ThreadPool* raft_pool,
    rpc::ThreadPool* raft_notifications_pool,
    RetryableRequests* retryable_requests,
    MultiRaftManager* multi_raft_manager) {

  auto rpc_factory = std::make_unique<RpcPeerProxyFactory>(
    messenger, proxy_cache, local_peer_pb.cloud_info());

  // The message queue that keeps track of which operations need to be replicated
  // where.
  auto queue = std::make_unique<PeerMessageQueue>(
      tablet_metric_entity,
      log,
      server_mem_tracker,
      parent_mem_tracker,
      local_peer_pb,
      options.tablet_id,
      clock,
      consensus_context,
      std::make_unique<rpc::Strand>(raft_notifications_pool));

  DCHECK(local_peer_pb.has_permanent_uuid());
  const string& peer_uuid = local_peer_pb.permanent_uuid();

  // A single Raft thread pool token is shared between RaftConsensus and
  // PeerManager. Because PeerManager is owned by RaftConsensus, it receives a
  // raw pointer to the token, to emphasize that RaftConsensus is responsible
  // for destroying the token.
  unique_ptr<ThreadPoolToken> raft_pool_concurrent_token(raft_pool->NewToken(
      ThreadPool::ExecutionMode::CONCURRENT));

  // A manager for the set of peers that actually send the operations both remotely
  // and to the local wal.
  auto peer_manager = std::make_unique<PeerManager>(
      options.tablet_id,
      peer_uuid,
      rpc_factory.get(),
      queue.get(),
      raft_pool_concurrent_token.get(),
      multi_raft_manager);

  return std::make_shared<RaftConsensus>(
      options,
      std::move(cmeta),
      std::move(rpc_factory),
      std::move(queue),
      std::move(peer_manager),
      std::move(raft_pool_concurrent_token),
      table_metric_entity,
      tablet_metric_entity,
      peer_uuid,
      clock,
      consensus_context,
      log,
      parent_mem_tracker,
      mark_dirty_clbk,
      table_type,
      retryable_requests);
}

RaftConsensus::RaftConsensus(
    const ConsensusOptions& options, std::unique_ptr<ConsensusMetadata> cmeta,
    std::unique_ptr<PeerProxyFactory> proxy_factory,
    std::unique_ptr<PeerMessageQueue> queue,
    std::unique_ptr<PeerManager> peer_manager,
    std::unique_ptr<ThreadPoolToken> raft_pool_concurrent_token,
    const scoped_refptr<MetricEntity>& table_metric_entity,
    const scoped_refptr<MetricEntity>& tablet_metric_entity,
    const std::string& peer_uuid, const scoped_refptr<server::Clock>& clock,
    ConsensusContext* consensus_context, const scoped_refptr<log::Log>& log,
    shared_ptr<MemTracker> parent_mem_tracker,
    Callback<void(std::shared_ptr<StateChangeContext> context)> mark_dirty_clbk,
    TableType table_type,
    RetryableRequests* retryable_requests)
    : raft_pool_concurrent_token_(std::move(raft_pool_concurrent_token)),
      log_(log),
      clock_(clock),
      peer_proxy_factory_(std::move(proxy_factory)),
      peer_manager_(std::move(peer_manager)),
      queue_(std::move(queue)),
      rng_(GetRandomSeed32()),
      withhold_votes_until_(MonoTime::Min()),
      step_down_check_tracker_(&peer_proxy_factory_->messenger()->scheduler()),
      mark_dirty_clbk_(std::move(mark_dirty_clbk)),
      shutdown_(false),
      follower_memory_pressure_rejections_(tablet_metric_entity->FindOrCreateMetric<Counter>(
          &METRIC_follower_memory_pressure_rejections)),
      term_metric_(tablet_metric_entity->FindOrCreateMetric<AtomicGauge<int64_t>>(
          &METRIC_raft_term, cmeta->current_term())),
      follower_last_update_time_ms_metric_(
          tablet_metric_entity->FindOrCreateMetric<AtomicMillisLag>(&METRIC_follower_lag_ms)),
      is_raft_leader_metric_(tablet_metric_entity->FindOrCreateMetric<AtomicGauge<int64_t>>(
          &METRIC_is_raft_leader, static_cast<int64_t>(0))),
      parent_mem_tracker_(std::move(parent_mem_tracker)),
      table_type_(table_type),
      update_raft_config_dns_latency_(
          METRIC_dns_resolve_latency_during_update_raft_config.Instantiate(table_metric_entity)),
      split_parent_tablet_id_(
          cmeta->has_split_parent_tablet_id() ? cmeta->split_parent_tablet_id() : ""),
      clone_source_info_(cmeta->clone_source_info()) {
  DCHECK_NOTNULL(log_.get());

  if (PREDICT_FALSE(FLAGS_TEST_follower_reject_update_consensus_requests_seconds > 0)) {
    withold_replica_updates_until_ = MonoTime::Now() +
        MonoDelta::FromSeconds(FLAGS_TEST_follower_reject_update_consensus_requests_seconds);
  }

  state_ = std::make_unique<ReplicaState>(
      options,
      peer_uuid,
      std::move(cmeta),
      DCHECK_NOTNULL(consensus_context),
      this,
      retryable_requests,
      std::bind(&PeerMessageQueue::TrackOperationsMemory, queue_.get(), _1));

  peer_manager_->SetConsensus(this);
}

RaftConsensus::~RaftConsensus() {
  Shutdown();
}

Status RaftConsensus::Start(const ConsensusBootstrapInfo& info) {
  RETURN_NOT_OK(ExecuteHook(PRE_START));

  // Capture a weak_ptr reference into the functor so it can safely handle
  // outliving the consensus instance.
  std::weak_ptr<RaftConsensus> w = shared_from_this();
  failure_detector_ = PeriodicTimer::Create(
      peer_proxy_factory_->messenger(),
      [w]() {
        if (auto consensus = w.lock()) {
          consensus->ReportFailureDetected();
        }
      },
      MinimumElectionTimeout());

  {
    if (table_type_ != TableType::TRANSACTION_STATUS_TABLE_TYPE) {
      TEST_PAUSE_IF_FLAG(TEST_pause_replica_start_before_triggering_pending_operations);
    }
    ReplicaState::UniqueLock lock;
    RETURN_NOT_OK(state_->LockForStart(&lock));
    state_->ClearLeaderUnlocked();

    RETURN_NOT_OK_PREPEND(state_->StartUnlocked(info.last_id),
                          "Unable to start RAFT ReplicaState");

    LOG_WITH_PREFIX(INFO) << "Replica starting. Triggering "
                          << info.orphaned_replicates.size()
                          << " pending operations. Active config: "
                          << state_->GetActiveConfigUnlocked().ShortDebugString();
    for (const auto& replicate : info.orphaned_replicates) {
      RETURN_NOT_OK(StartReplicaOperationUnlocked(replicate, HybridTime::kInvalid));
    }

    RETURN_NOT_OK(state_->InitCommittedOpIdUnlocked(yb::OpId::FromPB(info.last_committed_id)));

    queue_->Init(state_->GetLastReceivedOpIdUnlocked());
  }

  {
    ReplicaState::UniqueLock lock;
    RETURN_NOT_OK(state_->LockForConfigChange(&lock));

    // If this is the first term expire the FD immediately so that we have a fast first
    // election, otherwise we just let the timer expire normally.
    MonoDelta initial_delta = MonoDelta();
    if (state_->GetCurrentTermUnlocked() == 0) {
      // The failure detector is initialized to a low value to trigger an early election
      // (unless someone else requested a vote from us first, which resets the
      // election timer). We do it this way instead of immediately running an
      // election to get a higher likelihood of enough servers being available
      // when the first one attempts an election to avoid multiple election
      // cycles on startup, while keeping that "waiting period" random. If there is only one peer,
      // trigger an election right away.
      if (PREDICT_TRUE(FLAGS_enable_leader_failure_detection)) {
        LOG_WITH_PREFIX(INFO) << "Consensus starting up: Expiring fail detector timer "
                                 "to make a prompt election more likely";
        // Gating quick leader elections on table creation since prompter leader elections are
        // more likely to fail due to uninitialized peers or conflicting elections, which could
        // have unforseen consequences.
        if (FLAGS_quick_leader_election_on_create) {
          initial_delta = (state_->GetCommittedConfigUnlocked().peers_size() == 1) ?
              MonoDelta::kZero :
              MonoDelta::FromMilliseconds(rng_.Uniform(FLAGS_raft_heartbeat_interval_ms));
        }
      }
    }
    RETURN_NOT_OK(BecomeReplicaUnlocked(std::string(), initial_delta));
  }

  RETURN_NOT_OK(ExecuteHook(POST_START));

  // The context tracks that the current caller does not hold the lock for consensus state.
  // So mark dirty callback, e.g., consensus->ConsensusState() for master consensus callback of
  // SysCatalogStateChanged, can get the lock when needed.
  auto context = std::make_shared<StateChangeContext>(StateChangeReason::CONSENSUS_STARTED, false);
  // Report become visible to the Master.
  MarkDirty(context);

  return Status::OK();
}

bool RaftConsensus::IsRunning() const {
  auto lock = state_->LockForRead();
  return state_->state() == ReplicaState::kRunning;
}

Status RaftConsensus::EmulateElection() {
  ReplicaState::UniqueLock lock;
  RETURN_NOT_OK(state_->LockForConfigChange(&lock));

  LOG_WITH_PREFIX(INFO) << "Emulating election...";

  // Assume leadership of new term.
  RETURN_NOT_OK(IncrementTermUnlocked());
  SetLeaderUuidUnlocked(state_->GetPeerUuid());
  return BecomeLeaderUnlocked();
}

Status RaftConsensus::DoStartElection(const LeaderElectionData& data, PreElected preelected) {
  TRACE_EVENT2("consensus", "RaftConsensus::StartElection",
               "peer", peer_uuid(),
               "tablet", tablet_id());
  VLOG_WITH_PREFIX_AND_FUNC(1) << data.ToString();
  if (FLAGS_TEST_do_not_start_election_test_only) {
    LOG(INFO) << "Election start skipped as TEST_do_not_start_election_test_only flag "
                 "is set to true.";
    return Status::OK();
  }

  // If pre-elections disabled or we already won pre-election then start regular election,
  // otherwise pre-election is started.
  // Pre-elections could be disable via flag, or temporarily if some nodes do not support them.
  auto preelection = ANNOTATE_UNPROTECTED_READ(FLAGS_use_preelection) && !preelected &&
                     disable_pre_elections_until_ < CoarseMonoClock::now();
  const char* election_name = preelection ? "pre-election" : "election";

  LeaderElectionPtr election;
  {
    ReplicaState::UniqueLock lock;
    RETURN_NOT_OK(state_->LockForConfigChange(&lock));

    if (data.initial_election && state_->GetCurrentTermUnlocked() != 0) {
      LOG_WITH_PREFIX(INFO) << "Not starting initial " << election_name << " -- non zero term";
      return Status::OK();
    }

    PeerRole active_role = state_->GetActiveRoleUnlocked();
    if (active_role == PeerRole::LEADER) {
      LOG_WITH_PREFIX(INFO) << "Not starting " << election_name << " -- already leader";
      return Status::OK();
    }
    if (active_role == PeerRole::LEARNER || active_role == PeerRole::READ_REPLICA) {
      LOG_WITH_PREFIX(INFO) << "Not starting " << election_name << " -- role is " << active_role
                            << ", pending = " << state_->IsConfigChangePendingUnlocked()
                            << ", active_role=" << active_role;
      return Status::OK();
    }
    if (PREDICT_FALSE(active_role == PeerRole::NON_PARTICIPANT)) {
      VLOG_WITH_PREFIX(1) << "Not starting " << election_name << " -- non participant";
      // Avoid excessive election noise while in this state.
      SnoozeFailureDetector(DO_NOT_LOG);
      return STATUS_FORMAT(
          IllegalState,
          "Not starting $0: Node is currently a non-participant in the raft config: $1",
          election_name, state_->GetActiveConfigUnlocked());
    }

    // Default is to start the election now. But if we are starting a pending election, see if
    // there is an op id pending upon indeed and if it has been committed to the log. The op id
    // could have been cleared if the pending election has already been started or another peer
    // has jumped before we can start.
    bool start_now = true;
    if (data.pending_commit) {
      const auto required_id = !data.must_be_committed_opid
          ? state_->GetPendingElectionOpIdUnlocked() : data.must_be_committed_opid;
      const Status advance_committed_index_status = ResultToStatus(
          state_->AdvanceCommittedOpIdUnlocked(required_id, CouldStop::kFalse));
      if (!advance_committed_index_status.ok()) {
        LOG(WARNING) << "Starting an " << election_name << " but the latest committed OpId is not "
                        "present in this peer's log: "
                     << required_id << ". " << "Status: " << advance_committed_index_status;
      }
      start_now = required_id.index <= state_->GetCommittedOpIdUnlocked().index;
    }

    if (start_now) {
      if (state_->HasLeaderUnlocked()) {
        LOG_WITH_PREFIX(INFO) << "Fail or stepdown of leader " << state_->GetLeaderUuidUnlocked()
                              << " detected. Triggering leader " << election_name
                              << ", mode=" << data.mode;
      } else {
        LOG_WITH_PREFIX(INFO) << "Triggering leader " << election_name << ", mode=" << data.mode;
      }

      // Snooze to avoid the election timer firing again as much as possible.
      // We do not disable the election timer while running an election.
      MonoDelta timeout = LeaderElectionExpBackoffDeltaUnlocked();
      SnoozeFailureDetector(ALLOW_LOGGING, timeout);

      election = VERIFY_RESULT(CreateElectionUnlocked(data, timeout, PreElection(preelection)));
    } else if (data.pending_commit && !data.must_be_committed_opid.empty()) {
      // Queue up the pending op id if specified.
      state_->SetPendingElectionOpIdUnlocked(data.must_be_committed_opid);
      LOG_WITH_PREFIX(INFO)
          << "Leader " << election_name << " is pending upon log commitment of OpId "
          << data.must_be_committed_opid;
    } else {
      LOG_WITH_PREFIX(INFO) << "Ignore " << __func__ << " existing wait on op id";
    }
  }

  // Start the election outside the lock.
  if (election) {
    election->Run();
  }

  return Status::OK();
}

Result<LeaderElectionPtr> RaftConsensus::CreateElectionUnlocked(
    const LeaderElectionData& data, MonoDelta timeout, PreElection preelection) {
  int64_t new_term;
  if (preelection) {
    new_term = state_->GetCurrentTermUnlocked() + 1;
  } else {
    // Increment the term.
    RETURN_NOT_OK(IncrementTermUnlocked());
    new_term = state_->GetCurrentTermUnlocked();
  }

  const RaftConfigPB& active_config = state_->GetActiveConfigUnlocked();
  LOG_WITH_PREFIX(INFO) << "Starting " << (preelection ? "pre-" : "") << "election with config: "
                        << active_config.ShortDebugString();

  // Initialize the VoteCounter.
  auto num_voters = CountVoters(active_config);
  auto majority_size = MajoritySize(num_voters);

  // Vote for ourselves.
  if (!preelection) {
    // TODO: Consider using a separate Mutex for voting, which must sync to disk.
    RETURN_NOT_OK(state_->SetVotedForCurrentTermUnlocked(state_->GetPeerUuid()));
  }

  auto counter = std::make_unique<VoteCounter>(num_voters, majority_size);
  bool duplicate;
  RETURN_NOT_OK(counter->RegisterVote(state_->GetPeerUuid(), ElectionVote::kGranted, &duplicate));
  CHECK(!duplicate) << state_->LogPrefix()
                    << "Inexplicable duplicate self-vote for term "
                    << state_->GetCurrentTermUnlocked();

  VoteRequestPB request;
  request.set_ignore_live_leader(data.mode == ElectionMode::ELECT_EVEN_IF_LEADER_IS_ALIVE);
  request.set_candidate_uuid(state_->GetPeerUuid());
  request.set_candidate_term(new_term);
  request.set_tablet_id(state_->GetOptions().tablet_id);
  request.set_preelection(preelection);
  state_->GetLastReceivedOpIdUnlocked().ToPB(
      request.mutable_candidate_status()->mutable_last_received());

  LeaderElectionPtr result(new LeaderElection(
      active_config,
      peer_proxy_factory_.get(),
      request,
      std::move(counter),
      timeout,
      preelection,
      data.suppress_vote_request,
      std::bind(&RaftConsensus::ElectionCallback, shared_from_this(), data, _1)));

  if (!preelection) {
    // Clear the pending election op id so that we won't start the same pending election again.
    // Pre-election does not change state, so should not do it in this case.
    state_->ClearPendingElectionOpIdUnlocked();
  }

  return result;
}

Status RaftConsensus::WaitUntilLeaderForTests(const MonoDelta& timeout) {
  MonoTime deadline = MonoTime::Now();
  deadline.AddDelta(timeout);
  while (MonoTime::Now().ComesBefore(deadline)) {
    if (GetLeaderStatus() == LeaderStatus::LEADER_AND_READY) {
      return Status::OK();
    }
    SleepFor(MonoDelta::FromMilliseconds(10));
  }

  return STATUS(TimedOut, Substitute("Peer $0 is not leader of tablet $1 after $2. Role: $3",
                                     peer_uuid(), tablet_id(), timeout.ToString(), role()));
}

string RaftConsensus::ServersInTransitionMessage() {
  string err_msg;
  const RaftConfigPB& active_config = state_->GetActiveConfigUnlocked();
  const RaftConfigPB& committed_config = state_->GetCommittedConfigUnlocked();
  auto servers_in_transition = CountServersInTransition(active_config);
  auto committed_servers_in_transition = CountServersInTransition(committed_config);
  LOG(INFO) << Substitute("Active config has $0 and committed has $1 servers in transition.",
                          servers_in_transition, committed_servers_in_transition);
  if (servers_in_transition != 0 || committed_servers_in_transition != 0) {
    err_msg = Substitute("Leader not ready to step down as there are $0 active config peers"
                         " in transition, $1 in committed. Configs:\nactive=$2\ncommit=$3",
                         servers_in_transition, committed_servers_in_transition,
                         active_config.ShortDebugString(), committed_config.ShortDebugString());
    LOG(INFO) << err_msg;
  }
  return err_msg;
}

Status RaftConsensus::StartStepDownUnlocked(const RaftPeerPB& peer, bool graceful) {
  auto election_state = std::make_shared<RunLeaderElectionState>();
  election_state->proxy = peer_proxy_factory_->NewProxy(peer);
  election_state->req.set_originator_uuid(state_->GetPeerUuid());
  election_state->req.set_dest_uuid(peer.permanent_uuid());
  election_state->req.set_tablet_id(state_->GetOptions().tablet_id);
  election_state->rpc.set_invoke_callback_mode(rpc::InvokeCallbackMode::kThreadPoolHigh);
  election_state->proxy->RunLeaderElectionAsync(
      &election_state->req, &election_state->resp, &election_state->rpc,
      std::bind(&RaftConsensus::RunLeaderElectionResponseRpcCallback, shared_from(this),
                election_state));

  LOG_WITH_PREFIX(INFO) << "Transferring leadership to " << peer.permanent_uuid();

  return BecomeReplicaUnlocked(
      graceful ? std::string() : peer.permanent_uuid(), MonoDelta());
}

void RaftConsensus::CheckDelayedStepDown(const Status& status) {
  ReplicaState::UniqueLock lock;
  auto lock_status = state_->LockForConfigChange(&lock);
  if (!lock_status.ok()) {
    LOG_WITH_PREFIX(INFO) << "Failed to check delayed election: " << lock_status;
    return;
  }

  if (state_->GetCurrentTermUnlocked() != delayed_step_down_.term) {
    return;
  }

  const auto& config = state_->GetActiveConfigUnlocked();
  const auto* peer = FindPeer(config, delayed_step_down_.protege);
  if (peer) {
    LOG_WITH_PREFIX(INFO) << "Step down in favor on not synchronized protege: "
                          << delayed_step_down_.protege;
    WARN_NOT_OK(StartStepDownUnlocked(*peer, delayed_step_down_.graceful),
                "Start step down failed");
  } else {
    LOG_WITH_PREFIX(INFO) << "Failed to synchronize with protege " << delayed_step_down_.protege
                          << " and cannot find it in config: " << config.ShortDebugString();
    delayed_step_down_.term = OpId::kUnknownTerm;
  }
}

Status RaftConsensus::StepDown(const LeaderStepDownRequestPB* req, LeaderStepDownResponsePB* resp) {
  TRACE_EVENT0("consensus", "RaftConsensus::StepDown");
  ReplicaState::UniqueLock lock;
  RETURN_NOT_OK(state_->LockForConfigChange(&lock));

  // A sanity check that this request was routed to the correct RaftConsensus.
  const auto& tablet_id = req->tablet_id();
  if (tablet_id != this->tablet_id()) {
    resp->mutable_error()->set_code(TabletServerErrorPB::UNKNOWN_ERROR);
    const auto msg = Format(
        "Received a leader stepdown operation for wrong tablet id: $0, must be: $1",
        tablet_id, this->tablet_id());
    LOG_WITH_PREFIX(ERROR) << msg;
    StatusToPB(STATUS(IllegalState, msg), resp->mutable_error()->mutable_status());
    return Status::OK();
  }

  if (state_->GetActiveRoleUnlocked() != PeerRole::LEADER) {
    resp->mutable_error()->set_code(TabletServerErrorPB::NOT_THE_LEADER);
    StatusToPB(STATUS(IllegalState, "Not currently leader"),
               resp->mutable_error()->mutable_status());
    // We return OK so that the tablet service won't overwrite the error code.
    return Status::OK();
  }

  // The leader needs to be ready to perform a step down. There should be no PRE_VOTER in both
  // active and committed configs - ENG-557.
  const string err_msg = ServersInTransitionMessage();
  if (!err_msg.empty()) {
    resp->mutable_error()->set_code(TabletServerErrorPB::LEADER_NOT_READY_TO_STEP_DOWN);
    StatusToPB(STATUS(IllegalState, err_msg), resp->mutable_error()->mutable_status());
    return Status::OK();
  }

  std::string new_leader_uuid;
  // If a new leader is nominated, find it among peers to send RunLeaderElection request.
  // See https://ramcloud.stanford.edu/~ongaro/thesis.pdf, section 3.10 for this mechanism
  // to transfer the leadership.
  const bool forced = (req->has_force_step_down() && req->force_step_down());
  if (req->has_new_leader_uuid()) {
    new_leader_uuid = req->new_leader_uuid();
    if (!forced && !queue_->CanPeerBecomeLeader(new_leader_uuid)) {
      resp->mutable_error()->set_code(TabletServerErrorPB::LEADER_NOT_READY_TO_STEP_DOWN);
      StatusToPB(
          STATUS(IllegalState, "Suggested peer is not caught up yet"),
          resp->mutable_error()->mutable_status());
      // We return OK so that the tablet service won't overwrite the error code.
      return Status::OK();
    }
  }

  bool graceful_stepdown = false;
  if (new_leader_uuid.empty() && !FLAGS_stepdown_disable_graceful_transition &&
      !(req->has_disable_graceful_transition() && req->disable_graceful_transition())) {
    new_leader_uuid = queue_->GetUpToDatePeer();
    LOG_WITH_PREFIX(INFO) << "Selected up to date candidate protege leader [" << new_leader_uuid
                          << "]";
    graceful_stepdown = true;
  }

  const auto& local_peer_uuid = state_->GetPeerUuid();
  if (!new_leader_uuid.empty()) {
    const auto leadership_transfer_description =
        Format("tablet $0 from $1 to $2", tablet_id, local_peer_uuid, new_leader_uuid);
    if (!forced && new_leader_uuid == protege_leader_uuid_ && election_lost_by_protege_at_) {
      const MonoDelta time_since_election_loss_by_protege =
          MonoTime::Now() - election_lost_by_protege_at_;
      if (time_since_election_loss_by_protege.ToMilliseconds() <
              FLAGS_min_leader_stepdown_retry_interval_ms) {
        LOG_WITH_PREFIX(INFO) << "Unable to execute leadership transfer for "
                              << leadership_transfer_description
                              << " because the intended leader already lost an election only "
                              << ToString(time_since_election_loss_by_protege) << " ago (within "
                              << FLAGS_min_leader_stepdown_retry_interval_ms << " ms).";
        if (req->has_new_leader_uuid()) {
          LOG_WITH_PREFIX(INFO) << "Rejecting leader stepdown request for "
                                << leadership_transfer_description;
          resp->mutable_error()->set_code(TabletServerErrorPB::LEADER_NOT_READY_TO_STEP_DOWN);
          resp->set_time_since_election_failure_ms(
              time_since_election_loss_by_protege.ToMilliseconds());
          StatusToPB(
              STATUS(IllegalState, "Suggested peer lost an election recently"),
              resp->mutable_error()->mutable_status());
          // We return OK so that the tablet service won't overwrite the error code.
          return Status::OK();
        } else {
          // we were attempting a graceful transfer of our own choice
          // which is no longer possible
          new_leader_uuid.clear();
        }
      }
      election_lost_by_protege_at_ = MonoTime();
    }
  }

  if (!new_leader_uuid.empty()) {
    const auto* peer = FindPeer(state_->GetActiveConfigUnlocked(), new_leader_uuid);
    if (peer && peer->member_type() == PeerMemberType::VOTER) {
      auto timeout_ms = FLAGS_protege_synchronization_timeout_ms;
      if (timeout_ms != 0 &&
          queue_->PeerLastReceivedOpId(new_leader_uuid) < GetLatestOpIdFromLog()) {
        delayed_step_down_ = DelayedStepDown {
          .term = state_->GetCurrentTermUnlocked(),
          .protege = new_leader_uuid,
          .graceful = graceful_stepdown,
        };
        LOG_WITH_PREFIX(INFO) << "Delay step down: " << delayed_step_down_.ToString();
        step_down_check_tracker_.Schedule(
            std::bind(&RaftConsensus::CheckDelayedStepDown, this, _1),
            1ms * timeout_ms);
        return Status::OK();
      }

      return StartStepDownUnlocked(*peer, graceful_stepdown);
    }

    LOG_WITH_PREFIX(WARNING) << "New leader " << new_leader_uuid << " not found.";
    if (req->has_new_leader_uuid()) {
      resp->mutable_error()->set_code(TabletServerErrorPB::LEADER_NOT_READY_TO_STEP_DOWN);
      StatusToPB(
          STATUS(IllegalState, "New leader not found among peers"),
          resp->mutable_error()->mutable_status());
      // We return OK so that the tablet service won't overwrite the error code.
      return Status::OK();
    } else {
      // we were attempting a graceful transfer of our own choice
      // which is no longer possible
      new_leader_uuid.clear();
    }
  }

  if (graceful_stepdown) {
    new_leader_uuid.clear();
  }
  RETURN_NOT_OK(BecomeReplicaUnlocked(new_leader_uuid, MonoDelta()));

  return Status::OK();
}

Status RaftConsensus::ElectionLostByProtege(const std::string& election_lost_by_uuid) {
  if (election_lost_by_uuid.empty()) {
    return STATUS(InvalidArgument, "election_lost_by_uuid could not be empty");
  }

  auto start_election = false;
  {
    ReplicaState::UniqueLock lock;
    RETURN_NOT_OK(state_->LockForConfigChange(&lock));
    if (election_lost_by_uuid == protege_leader_uuid_) {
      LOG_WITH_PREFIX(INFO) << "Our protege " << election_lost_by_uuid
                            << ", lost election. Has leader: "
                            << state_->HasLeaderUnlocked();
      withhold_election_start_until_.store(MonoTime::Min(), std::memory_order_relaxed);
      election_lost_by_protege_at_ = MonoTime::Now();

      start_election = !state_->HasLeaderUnlocked();
    }
  }

  if (start_election) {
    return StartElection(LeaderElectionData{
        .mode = ElectionMode::NORMAL_ELECTION, .must_be_committed_opid = OpId()});
  }

  return Status::OK();
}

void RaftConsensus::WithholdElectionAfterStepDown(const std::string& protege_uuid) {
  DCHECK(state_->IsLocked());
  protege_leader_uuid_ = protege_uuid;
  auto timeout = MonoDelta::FromMilliseconds(
      FLAGS_leader_failure_max_missed_heartbeat_periods *
      FLAGS_raft_heartbeat_interval_ms);
  if (!protege_uuid.empty()) {
    // Actually we have 2 kinds of step downs.
    // 1) We step down in favor of some protege.
    // 2) We step down because term was advanced or just started.
    // In second case we should not withhold election for a long period of time.
    timeout *= FLAGS_after_stepdown_delay_election_multiplier;
  }
  auto deadline = MonoTime::Now() + timeout;
  VLOG(2) << "Withholding election for " << timeout;
  withhold_election_start_until_.store(deadline, std::memory_order_release);
  election_lost_by_protege_at_ = MonoTime();
}

void RaftConsensus::RunLeaderElectionResponseRpcCallback(
    shared_ptr<RunLeaderElectionState> election_state) {
  // Check for RPC errors.
  if (!election_state->rpc.status().ok()) {
    LOG_WITH_PREFIX(WARNING) << "RPC error from RunLeaderElection() call to peer "
                             << election_state->req.dest_uuid() << ": "
                             << election_state->rpc.status();
  // Check for tablet errors.
  } else if (election_state->resp.has_error()) {
    LOG_WITH_PREFIX(WARNING) << "Tablet error from RunLeaderElection() call to peer "
                             << election_state->req.dest_uuid() << ": "
                             << StatusFromPB(election_state->resp.error().status());
  }
}

void RaftConsensus::ReportFailureDetectedTask() {
  auto scope_exit = ScopeExit([this] {
    outstanding_report_failure_task_.store(false, std::memory_order_release);
  });

  MonoTime now;
  for (;;) {
    // Do not start election for an extended period of time if we were recently stepped down.
    auto old_value = withhold_election_start_until_.load(std::memory_order_acquire);

    if (old_value == MonoTime::Min()) {
      break;
    }

    if (!now.Initialized()) {
      now = MonoTime::Now();
    }

    if (now < old_value) {
      VLOG(1) << "Skipping election due to delayed timeout for " << (old_value - now);
      return;
    }

    // If we ever stepped down and then delayed election start did get scheduled, reset that we
    // are out of that extra delay mode.
    if (withhold_election_start_until_.compare_exchange_weak(
        old_value, MonoTime::Min(), std::memory_order_release)) {
      break;
    }
  }

  if (PREDICT_FALSE(FLAGS_TEST_skip_election_when_fail_detected)) {
    LOG_WITH_PREFIX(INFO) << "Skip normal election when failure detected due to "
                          << "FLAGS_TEST_skip_election_when_fail_detected";
    return;
  }

  // Start an election.
  LOG_WITH_PREFIX(INFO) << "ReportFailDetected: Starting NORMAL_ELECTION...";
  Status s = StartElection(LeaderElectionData{
      .mode = ElectionMode::NORMAL_ELECTION, .must_be_committed_opid = OpId()});
  if (PREDICT_FALSE(!s.ok())) {
    LOG_WITH_PREFIX(WARNING) << "Failed to trigger leader election: " << s.ToString();
  }
}

void RaftConsensus::ReportFailureDetected() {
  if (FLAGS_raft_disallow_concurrent_outstanding_report_failure_tasks &&
      outstanding_report_failure_task_.exchange(true, std::memory_order_acq_rel)) {
    VLOG(4)
        << "Returning from ReportFailureDetected as there is already an outstanding report task.";
  } else {
    // We're running on a timer thread; start an election on a different thread pool.
    auto s = raft_pool_concurrent_token_->SubmitFunc(
        std::bind(&RaftConsensus::ReportFailureDetectedTask, shared_from_this()));
    WARN_NOT_OK(s, "Failed to submit failure detected task");
    if (!s.ok()) {
      outstanding_report_failure_task_.store(false, std::memory_order_release);
    }
  }
}

Status RaftConsensus::BecomeLeaderUnlocked() {
  DCHECK(state_->IsLocked());
  TRACE_EVENT2("consensus", "RaftConsensus::BecomeLeaderUnlocked",
               "peer", peer_uuid(),
               "tablet", tablet_id());
  LOG_WITH_PREFIX(INFO) << "Becoming Leader. State: " << state_->ToStringUnlocked();

  // Disable FD while we are leader.
  DisableFailureDetector();

  // Don't vote for anyone if we're a leader.
  withhold_votes_until_.store(MonoTime::Max(), std::memory_order_release);

  queue_->RegisterObserver(this);

  // Refresh queue and peers before initiating NO_OP.
  RefreshConsensusQueueAndPeersUnlocked();

  // Initiate a NO_OP operation that is sent at the beginning of every term
  // change in raft.
  auto replicate = rpc::MakeSharedMessage<LWReplicateMsg>();
  replicate->set_op_type(NO_OP);
  replicate->mutable_noop_request(); // Define the no-op request field.
  LOG(INFO) << "Sending NO_OP at op " << state_->GetCommittedOpIdUnlocked();
  // This committed OpId is used for tablet bootstrap for RocksDB-backed tables.
  state_->GetCommittedOpIdUnlocked().ToPB(replicate->mutable_committed_op_id());

  // TODO: We should have no-ops (?) and config changes be COMMIT_WAIT
  // operations. See KUDU-798.
  // Note: This hybrid_time has no meaning from a serialization perspective
  // because this method is not executed on the TabletPeer's prepare thread.
  replicate->set_hybrid_time(clock_->Now().ToUint64());

  if (!PREDICT_FALSE(FLAGS_TEST_leader_skip_no_op)) {
    auto round = make_scoped_refptr<ConsensusRound>(this, replicate);
    round->SetCallback(MakeNonTrackedRoundCallback(round.get(),
        [this, term = state_->GetCurrentTermUnlocked()](const Status& status) {
      // Set 'Leader is ready to serve' flag only for committed NoOp operation
      // and only if the term is up-to-date.
      // It is guaranteed that successful notification is called only while holding replicate state
      // mutex.
      if (status.ok() && term == state_->GetCurrentTermUnlocked()) {
        state_->SetLeaderNoOpCommittedUnlocked(true);
      }
    }));
    RETURN_NOT_OK(AppendNewRoundToQueueUnlocked(round));
  }

  peer_manager_->SignalRequest(RequestTriggerMode::kNonEmptyOnly);

  // Set the timestamp to max uint64_t so that every time this metric is queried, the returned
  // lag is 0. We will need to restore the timestamp once this peer steps down.
  follower_last_update_time_ms_metric_->UpdateTimestampInMilliseconds(
      std::numeric_limits<int64_t>::max());
  is_raft_leader_metric_->set_value(1);

  // we don't care about this timestamp from leader because it doesn't accept Update requests.
  follower_last_update_received_time_ms_.store(0, std::memory_order_release);

  return Status::OK();
}

Status RaftConsensus::BecomeReplicaUnlocked(
    const std::string& new_leader_uuid, MonoDelta initial_fd_wait) {
  LOG_WITH_PREFIX(INFO)
      << "Becoming Follower/Learner. State: " << state_->ToStringUnlocked()
      << ", new leader: " << new_leader_uuid << ", initial_fd_wait: " << initial_fd_wait;

  if (state_->GetActiveRoleUnlocked() == PeerRole::LEADER) {
    WithholdElectionAfterStepDown(new_leader_uuid);
  }

  state_->ClearLeaderUnlocked();

  // FD should be running while we are a follower.
  EnableFailureDetector(initial_fd_wait);

  // Now that we're a replica, we can allow voting for other nodes.
  withhold_votes_until_.store(MonoTime::Min(), std::memory_order_release);

  const Status unregister_observer_status = queue_->UnRegisterObserver(this);
  if (!unregister_observer_status.IsNotFound()) {
    RETURN_NOT_OK(unregister_observer_status);
  }
  // Deregister ourselves from the queue. We don't care what get's replicated, since
  // we're stepping down.
  queue_->SetNonLeaderMode();

  peer_manager_->Close();

  // TODO: https://github.com/yugabyte/yugabyte-db/issues/5522. Add unit tests for this metric.
  // We update the follower lag metric timestamp here because it's possible that a leader
  // that step downs could get partitioned before it receives any replicate message. If we
  // don't update the timestamp here, and the above scenario happens, the metric will keep the
  // uint64_t max value, which would make the metric return a 0 lag every time it is queried,
  // even though that's not the case.
  follower_last_update_time_ms_metric_->UpdateTimestampInMilliseconds(
      clock_->Now().GetPhysicalValueMicros() / 1000);
  is_raft_leader_metric_->set_value(0);

  return Status::OK();
}

Status RaftConsensus::TEST_Replicate(const ConsensusRoundPtr& round) {
  return ReplicateBatch({ round });
}

Status RaftConsensus::ReplicateBatch(const ConsensusRounds& rounds) {
  size_t processed_rounds = 0;
  auto status = DoReplicateBatch(rounds, &processed_rounds);
  if (!status.ok()) {
    VLOG_WITH_PREFIX_AND_FUNC(1)
        << "Failed with status " << status << ", treating all " << rounds.size()
        << " operations as failed with that status";
    // Treat all the operations in the batch as failed.
    for (size_t i = rounds.size(); i != processed_rounds;) {
      rounds[--i]->NotifyReplicationFailed(status);
    }
  }
  return status;
}

Status RaftConsensus::DoReplicateBatch(const ConsensusRounds& rounds, size_t* processed_rounds) {
  RETURN_NOT_OK(ExecuteHook(PRE_REPLICATE));
  TEST_PAUSE_IF_FLAG(TEST_pause_before_replicate_batch);
  {
    ReplicaState::UniqueLock lock;
#ifndef NDEBUG
    for (const auto& round : rounds) {
      DCHECK(!round->replicate_msg()->has_id()) << "Should not have an OpId yet: "
                                                << round->replicate_msg()->ShortDebugString();
    }
#endif
    RETURN_NOT_OK(state_->LockForReplicate(&lock));
    auto current_term = state_->GetCurrentTermUnlocked();
    if (current_term == delayed_step_down_.term) {
      return STATUS(Aborted, "Rejecting because of planned step down");
    }

    for (const auto& round : rounds) {
      RETURN_NOT_OK(round->CheckBoundTerm(current_term));
    }
    RETURN_NOT_OK(AppendNewRoundsToQueueUnlocked(rounds, processed_rounds));
  }

  peer_manager_->SignalRequest(RequestTriggerMode::kNonEmptyOnly);
  RETURN_NOT_OK(ExecuteHook(POST_REPLICATE));
  return Status::OK();
}

Status RaftConsensus::AppendNewRoundToQueueUnlocked(const scoped_refptr<ConsensusRound>& round) {
  size_t processed_rounds = 0;
  return AppendNewRoundsToQueueUnlocked({ round }, &processed_rounds);
}

Status RaftConsensus::CheckLeasesUnlocked(const ConsensusRoundPtr& round) {
  auto op_type = round->replicate_msg()->op_type();
  // When we do not have a hybrid time leader lease we allow 2 operation types to be added to RAFT.
  // NO_OP - because even empty heartbeat messages could be used to obtain the lease.
  // CHANGE_CONFIG_OP - because we should be able to update consensus even w/o lease.
  // Both of them are safe, since they don't affect user reads or writes.
  if (IsConsensusOnlyOperation(op_type)) {
    return Status::OK();
  }

  auto lease_status = state_->GetHybridTimeLeaseStatusAtUnlocked(
      HybridTime(round->replicate_msg()->hybrid_time()).GetPhysicalValueMicros());
  static_assert(LeaderLeaseStatus_ARRAYSIZE == 3, "Please update logic below to adapt new state");
  if (lease_status == LeaderLeaseStatus::OLD_LEADER_MAY_HAVE_LEASE) {
    return STATUS_FORMAT(LeaderHasNoLease,
                         "Old leader may have hybrid time lease, while adding: $0",
                         OperationType_Name(op_type));
  }
  lease_status = state_->GetLeaderLeaseStatusUnlocked(nullptr);
  if (lease_status == LeaderLeaseStatus::OLD_LEADER_MAY_HAVE_LEASE) {
    return STATUS_FORMAT(LeaderHasNoLease,
                         "Old leader may have lease, while adding: $0",
                         OperationType_Name(op_type));
  }

  return Status::OK();
}

Status RaftConsensus::AppendNewRoundsToQueueUnlocked(
    const ConsensusRounds& rounds, size_t* processed_rounds) {
  SCHECK(!rounds.empty(), InvalidArgument, "Attempted to add zero rounds to the queue");

  auto role = state_->GetActiveRoleUnlocked();
  if (role != PeerRole::LEADER) {
    return STATUS_FORMAT(IllegalState, "Appending new rounds while not the leader but $0",
                         PeerRole_Name(role));
  }

  std::vector<ReplicateMsgPtr> replicate_msgs;
  replicate_msgs.reserve(rounds.size());

  auto status = DoAppendNewRoundsToQueueUnlocked(rounds, processed_rounds, &replicate_msgs);
  if (!status.ok()) {
    for (auto iter = replicate_msgs.rbegin(); iter != replicate_msgs.rend(); ++iter) {
      RollbackIdAndDeleteOpId(*iter, /* should_exists = */ true);
    }
    // In general we have 3 kinds of rounds in case of failure:
    // 1) Rounds that were rejected by retryable requests.
    //    We should not call ReplicationFinished for them, because it has been already called.
    // 2) Rounds that were registered with retryable requests.
    //    We should call state_->NotifyReplicationFinishedUnlocked for them.
    // 3) Rounds that were not registered with retryable requests.
    //    We should call ReplicationFinished directly for them. Could do it after releasing
    //    the lock. I.e. in ReplicateBatch.
    //
    // (3) is all rounds starting with index *processed_rounds and above.
    // For (1) we reset bound term, so could use it to distinguish between (1) and (2).
    for (size_t i = *processed_rounds; i != 0;) {
      --i;
      if (rounds[i]->bound_term() == OpId::kUnknownTerm) {
        // Already notified.
        continue;
      }
      state_->NotifyReplicationFinishedUnlocked(
          rounds[i], status, OpId::kUnknownTerm, /* applied_op_ids */ nullptr);
    }
  }
  return status;
}

Status RaftConsensus::DoAppendNewRoundsToQueueUnlocked(
    const ConsensusRounds& rounds, size_t* processed_rounds,
    std::vector<ReplicateMsgPtr>* replicate_msgs) {
  const OpId& committed_op_id = state_->GetCommittedOpIdUnlocked();

  for (const auto& round : rounds) {
    ++*processed_rounds;

    if (round->replicate_msg()->op_type() == OperationType::WRITE_OP) {
      DCHECK_EQ(state_->GetActiveRoleUnlocked(), PeerRole::LEADER);
      auto result = state_->RegisterRetryableRequest(
          round, tablet::IsLeaderSide(state_->GetActiveRoleUnlocked() == PeerRole::LEADER));
      if (!result.ok()) {
        round->NotifyReplicationFinished(
            result.status(), round->bound_term(), /* applied_op_ids = */ nullptr);
      }
      if (!result.ok() || !*result) {
        round->BindToTerm(OpId::kUnknownTerm); // Mark round as non replicating
        continue;
      }
    }

    OpId op_id = state_->NewIdUnlocked();

    // We use this callback to transform write operations by substituting the hybrid_time into
    // the write batch inside the write operation.
    //
    // TODO: we could allocate multiple HybridTimes in batch, only reading system clock once.
    RETURN_NOT_OK(round->NotifyAddedToLeader(op_id, committed_op_id));

    auto s = state_->AddPendingOperation(round, OperationMode::kLeader);
    if (!s.ok()) {
      RollbackIdAndDeleteOpId(round->replicate_msg(), false /* should_exists */);
      return s;
    }

    replicate_msgs->push_back(round->replicate_msg());
  }

  if (replicate_msgs->empty()) {
    return Status::OK();
  }

  // Could check lease just for the latest operation in batch, because it will have greatest
  // hybrid time, so requires most advanced lease.
  auto s = CheckLeasesUnlocked(rounds.back());

  if (s.ok()) {
    s = queue_->AppendOperations(*replicate_msgs, committed_op_id, state_->Clock().Now());
  }

  // Handle Status::ServiceUnavailable(), which means the queue is full.
  // TODO: what are we doing about other errors here? Should we also release OpIds in those cases?
  if (PREDICT_FALSE(!s.ok())) {
    LOG_WITH_PREFIX(WARNING) << "Could not append replicate request: " << s
                             << ", queue status: " << queue_->ToString();
    // TODO Possibly evict a dangling peer from the configuration here.
    // TODO count of number of ops failed due to consensus queue overflow.

    return s.CloneAndPrepend("Unable to append operations to consensus queue");
  }

  state_->UpdateLastReceivedOpIdUnlocked(OpId::FromPB(replicate_msgs->back()->id()));
  return Status::OK();
}

void RaftConsensus::MajorityReplicatedNumSSTFilesChanged(
    uint64_t majority_replicated_num_sst_files) {
  majority_num_sst_files_.store(majority_replicated_num_sst_files, std::memory_order_release);
}

void RaftConsensus::UpdateMajorityReplicated(
    const MajorityReplicatedData& majority_replicated_data, OpId* committed_op_id,
    OpId* last_applied_op_id) {
  TEST_PAUSE_IF_FLAG_WITH_LOG_PREFIX(TEST_pause_update_majority_replicated);
  ReplicaState::UniqueLock lock;
  Status s = state_->LockForMajorityReplicatedIndexUpdate(&lock);
  if (PREDICT_FALSE(!s.ok())) {
    LOG_WITH_PREFIX(WARNING)
        << "Unable to take state lock to update committed index: "
        << s.ToString();
    return;
  }

  EnumBitSet<SetMajorityReplicatedLeaseExpirationFlag> flags;
  if (GetAtomicFlag(&FLAGS_enable_lease_revocation)) {
    if (!state_->old_leader_lease().holder_uuid.empty() &&
        queue_->PeerAcceptedOurLease(state_->old_leader_lease().holder_uuid)) {
      flags.Set(SetMajorityReplicatedLeaseExpirationFlag::kResetOldLeaderLease);
    }

    if (!state_->old_leader_ht_lease().holder_uuid.empty() &&
        queue_->PeerAcceptedOurLease(state_->old_leader_ht_lease().holder_uuid)) {
      flags.Set(SetMajorityReplicatedLeaseExpirationFlag::kResetOldLeaderHtLease);
    }
  }

  s = state_->SetMajorityReplicatedLeaseExpirationUnlocked(majority_replicated_data, flags);
  if (s.ok()) {
    YB_PROFILE(leader_lease_wait_cond_.notify_all());
  } else {
    LOG(WARNING) << "Leader lease expiration was not set: " << s;
  }

  VLOG_WITH_PREFIX(1) << "Marking majority replicated up to "
      << majority_replicated_data.ToString();
  TRACE("Marking majority replicated up to $0", majority_replicated_data.op_id.ToString());
  bool committed_index_changed = false;
  s = state_->UpdateMajorityReplicatedUnlocked(
      majority_replicated_data.op_id, committed_op_id, &committed_index_changed,
      last_applied_op_id);
  if (s.ok() && state_->GetLeaderStateUnlocked().ok()) {
    s = state_->context()->MajorityReplicated();
  }
  if (PREDICT_FALSE(!s.ok())) {
    string msg = Format("Unable to mark committed up to $0: $1", majority_replicated_data.op_id, s);
    TRACE(msg);
    LOG_WITH_PREFIX(WARNING) << msg;
    return;
  }

  majority_num_sst_files_.store(majority_replicated_data.num_sst_files, std::memory_order_release);

  if (!majority_replicated_data.peer_got_all_ops.empty() &&
      delayed_step_down_.term == state_->GetCurrentTermUnlocked() &&
      majority_replicated_data.peer_got_all_ops == delayed_step_down_.protege) {
    LOG_WITH_PREFIX(INFO) << "Protege synchronized: " << delayed_step_down_.ToString();
    const auto* peer = FindPeer(state_->GetActiveConfigUnlocked(), delayed_step_down_.protege);
    if (peer) {
      WARN_NOT_OK(StartStepDownUnlocked(*peer, delayed_step_down_.graceful),
                  "Start step down failed");
    }
    delayed_step_down_.term = OpId::kUnknownTerm;
  }

  if (committed_index_changed &&
      state_->GetActiveRoleUnlocked() == PeerRole::LEADER) {
    // If all operations were just committed, and we don't have pending operations, then
    // we write an empty batch that contains committed index.
    // This affects only our local log, because followers have different logic in this scenario.
    if (*committed_op_id == state_->GetLastReceivedOpIdUnlocked()) {
      auto status = queue_->AppendOperations({}, *committed_op_id, state_->Clock().Now());
      LOG_IF_WITH_PREFIX(DFATAL, !status.ok() && !status.IsServiceUnavailable())
          << "Failed to append empty batch: " << status;
    }

    lock.unlock();
    // No need to hold the lock while calling SignalRequest.
    peer_manager_->SignalRequest(RequestTriggerMode::kNonEmptyOnly);
  }
}

void RaftConsensus::AppendEmptyBatchToLeaderLog() {
  auto lock = state_->LockForRead();
  auto committed_op_id = state_->GetCommittedOpIdUnlocked();
  if (committed_op_id == state_->GetLastReceivedOpIdUnlocked()) {
    auto status = queue_->AppendOperations({}, committed_op_id, state_->Clock().Now());
    LOG_IF_WITH_PREFIX(DFATAL, !status.ok()) << "Failed to append empty batch: " << status;
  }
}

void RaftConsensus::NotifyTermChange(int64_t term) {
  ReplicaState::UniqueLock lock;
  Status s = state_->LockForConfigChange(&lock);
  if (PREDICT_FALSE(!s.ok())) {
    LOG_WITH_PREFIX(WARNING) << "Unable to lock ReplicaState for config change"
                             << " when notified of new term " << term << ": " << s;
    return;
  }
  WARN_NOT_OK(HandleTermAdvanceUnlocked(term), "Couldn't advance consensus term.");
}

void RaftConsensus::NotifyFailedFollower(const string& uuid,
                                         int64_t term,
                                         const std::string& reason) {
  // Common info used in all of the log messages within this method.
  string fail_msg = Substitute("Processing failure of peer $0 in term $1 ($2): ",
                               uuid, term, reason);

  if (!FLAGS_evict_failed_followers) {
    LOG_WITH_PREFIX(INFO) << fail_msg << "Eviction of failed followers is disabled. Doing nothing.";
    return;
  }

  RaftConfigPB committed_config;
  {
    auto lock = state_->LockForRead();

    int64_t current_term = state_->GetCurrentTermUnlocked();
    if (current_term != term) {
      LOG_WITH_PREFIX(INFO) << fail_msg << "Notified about a follower failure in "
                            << "previous term " << term << ", but a leader election "
                            << "likely occurred since the failure was detected. "
                            << "Doing nothing.";
      return;
    }

    if (state_->IsConfigChangePendingUnlocked()) {
      LOG_WITH_PREFIX(INFO) << fail_msg << "There is already a config change operation "
                            << "in progress. Unable to evict follower until it completes. "
                            << "Doing nothing.";
      return;
    }
    committed_config = state_->GetCommittedConfigUnlocked();
  }

  // Run config change on thread pool after dropping ReplicaState lock.
  WARN_NOT_OK(raft_pool_concurrent_token_->SubmitFunc(
                  std::bind(&RaftConsensus::TryRemoveFollowerTask,
                            shared_from_this(), uuid, committed_config, reason)),
              state_->LogPrefix() + "Unable to start RemoteFollowerTask");
}

void RaftConsensus::TryRemoveFollowerTask(const string& uuid,
                                          const RaftConfigPB& committed_config,
                                          const std::string& reason) {
  ChangeConfigRequestPB req;
  req.set_tablet_id(tablet_id());
  req.mutable_server()->set_permanent_uuid(uuid);
  req.set_type(REMOVE_SERVER);
  req.set_cas_config_opid_index(committed_config.opid_index());
  LOG_WITH_PREFIX(INFO)
      << "Attempting to remove follower " << uuid << " from the Raft config at commit index "
      << committed_config.opid_index() << ". Reason: " << reason;
  boost::optional<TabletServerErrorPB::Code> error_code;
  WARN_NOT_OK(ChangeConfig(req, &DoNothingStatusCB, &error_code),
              state_->LogPrefix() + "Unable to remove follower " + uuid);
}

Status RaftConsensus::Update(
    const std::shared_ptr<LWConsensusRequestPB>& request_ptr,
    LWConsensusResponsePB* response, CoarseTimePoint deadline) {
  if (const auto& wait_state = yb::ash::WaitStateInfo::CurrentWaitState()) {
    wait_state->set_query_id(
        yb::to_underlying(yb::ash::FixedQueryId::kQueryIdForRaftUpdateConsensus));
  }
  follower_last_update_received_time_ms_.store(
      clock_->Now().GetPhysicalValueMillis(), std::memory_order_release);
  if (PREDICT_FALSE(FLAGS_TEST_follower_reject_update_consensus_requests)) {
    return STATUS(IllegalState, "Rejected: --TEST_follower_reject_update_consensus_requests "
                                "is set to true.");
  }

  TEST_PAUSE_IF_FLAG(TEST_follower_pause_update_consensus_requests);

  const auto& request = *request_ptr;
  auto reject_mode = reject_mode_.load(std::memory_order_acquire);
  if (reject_mode != RejectMode::kNone) {
    if (reject_mode == RejectMode::kAll ||
        (reject_mode == RejectMode::kNonEmpty && !request.ops().empty())) {
      auto result = STATUS_FORMAT(IllegalState, "Rejected because of reject mode: $0",
                                  ToString(reject_mode));
      LOG_WITH_PREFIX(INFO) << result;
      return result;
    }
    LOG_WITH_PREFIX(INFO) << "Accepted: " << request.ShortDebugString();
  }

  if (PREDICT_FALSE(FLAGS_TEST_follower_reject_update_consensus_requests_seconds > 0)) {
    if (MonoTime::Now() < withold_replica_updates_until_) {
      LOG(INFO) << "Rejecting Update for tablet: " << tablet_id()
                << " tserver uuid: " << peer_uuid();
      return STATUS_SUBSTITUTE(IllegalState,
          "Rejected: --TEST_follower_reject_update_consensus_requests_seconds is set to $0",
          FLAGS_TEST_follower_reject_update_consensus_requests_seconds);
    }
  }

  RETURN_NOT_OK(ExecuteHook(PRE_UPDATE));
  response->ref_responder_uuid(state_->GetPeerUuid());

  VLOG_WITH_PREFIX(2) << "Replica received request: " << request.ShortDebugString();

  UpdateReplicaResult result;
  {
    // see var declaration
    auto wait_start = CoarseMonoClock::now();
    auto wait_duration = deadline != CoarseTimePoint::max() ? deadline - wait_start
                                                            : CoarseDuration::max();
    auto lock = LockMutex(&update_mutex_, wait_duration);
    if (!lock.owns_lock()) {
      return STATUS_FORMAT(TimedOut, "Unable to lock update mutex for $0", wait_duration);
    }

    LongOperationTracker operation_tracker("UpdateReplica", 1s);
    result = VERIFY_RESULT(UpdateReplica(request_ptr, response));

    auto delay = TEST_delay_update_.load(std::memory_order_acquire);
    if (delay != MonoDelta::kZero) {
      std::this_thread::sleep_for(delay.ToSteadyDuration());
    }
  }

  // Release the lock while we wait for the log append to finish so that commits can go through.
  if (!result.wait_for_op_id.empty()) {
    RETURN_NOT_OK(WaitForWrites(result.current_term, result.wait_for_op_id));
  }

  if (PREDICT_FALSE(VLOG_IS_ON(2))) {
    VLOG_WITH_PREFIX(2) << "Replica updated. "
        << state_->ToString() << " Request: " << request.ShortDebugString();
  }

  // If an election pending on a specific op id and it has just been committed, start it now.
  // StartElection will ensure the pending election will be started just once only even if
  // UpdateReplica happens in multiple threads in parallel.
  if (result.start_election) {
    RETURN_NOT_OK(StartElection(LeaderElectionData{
        .mode = consensus::ElectionMode::ELECT_EVEN_IF_LEADER_IS_ALIVE,
        .pending_commit = true,
        .must_be_committed_opid = OpId()}));
  }

  RETURN_NOT_OK(ExecuteHook(POST_UPDATE));
  return Status::OK();
}

Status RaftConsensus::StartReplicaOperationUnlocked(
    const ReplicateMsgPtr& msg, HybridTime propagated_safe_time) {
  if (IsConsensusOnlyOperation(msg->op_type())) {
    return StartConsensusOnlyRoundUnlocked(msg);
  }

  if (PREDICT_FALSE(FLAGS_TEST_follower_fail_all_prepare)) {
    return STATUS(IllegalState, "Rejected: --TEST_follower_fail_all_prepare "
                                "is set to true.");
  }

  VLOG_WITH_PREFIX(1) << "Starting operation: " << msg->id().ShortDebugString();
  scoped_refptr<ConsensusRound> round(new ConsensusRound(this, msg));
  ConsensusRound* round_ptr = round.get();
  RETURN_NOT_OK(state_->context()->StartReplicaOperation(round, propagated_safe_time));
  auto result = state_->AddPendingOperation(round_ptr, OperationMode::kFollower);
  if (!result.ok()) {
    round_ptr->NotifyReplicationFinished(result, OpId::kUnknownTerm, /* applied_op_ids */ nullptr);
  }
  return result;
}

std::string RaftConsensus::LeaderRequest::OpsRangeString() const {
  std::string ret;
  ret.reserve(100);
  ret.push_back('[');
  if (!messages.empty()) {
    const auto& first_op = (*messages.begin())->id();
    const auto& last_op = (*messages.rbegin())->id();
    strings::SubstituteAndAppend(&ret, "$0.$1-$2.$3",
                                 first_op.term(), first_op.index(),
                                 last_op.term(), last_op.index());
  }
  ret.push_back(']');
  return ret;
}

Status RaftConsensus::DeduplicateLeaderRequestUnlocked(
    const std::shared_ptr<LWConsensusRequestPB>& request,
    LeaderRequest* deduplicated_req) {
  const auto& rpc_req = *request;
  const auto& last_committed = state_->GetCommittedOpIdUnlocked();

  // The leader's preceding id.
  deduplicated_req->preceding_op_id = OpId::FromPB(rpc_req.preceding_id());

  int64_t dedup_up_to_index = state_->GetLastReceivedOpIdUnlocked().index;

  deduplicated_req->first_message_idx = -1;

  // In this loop we discard duplicates and advance the leader's preceding id
  // accordingly.
  int idx = -1;
  for (const auto& leader_msg : rpc_req.ops()) {
    ++idx;
    auto id = OpId::FromPB(leader_msg.id());

    if (id.index <= last_committed.index) {
      VLOG_WITH_PREFIX(2) << "Skipping op id " << id << " (already committed)";
      deduplicated_req->preceding_op_id = id;
      continue;
    }

    if (id.index <= dedup_up_to_index) {
      // If the index is uncommitted and below our match index, then it must be in the
      // pendings set.
      scoped_refptr<ConsensusRound> round = state_->GetPendingOpByIndexOrNullUnlocked(id.index);
      if (!round) {
        // Could happen if we received outdated leader request. So should just reject it.
        return STATUS_FORMAT(IllegalState, "Round not found for index: $0", id.index);
      }

      // If the OpIds match, i.e. if they have the same term and id, then this is just
      // duplicate, we skip...
      if (OpId::FromPB(round->replicate_msg()->id()) == id) {
        VLOG_WITH_PREFIX(2) << "Skipping op id " << id << " (already replicated)";
        deduplicated_req->preceding_op_id = id;
        continue;
      }

      // ... otherwise we must adjust our match index, i.e. all messages from now on
      // are "new"
      dedup_up_to_index = id.index;
    }

    if (deduplicated_req->first_message_idx == -1) {
      deduplicated_req->first_message_idx = idx;
    }
    deduplicated_req->messages.emplace_back(request, const_cast<LWReplicateMsg*>(&leader_msg));
  }

  if (deduplicated_req->messages.size() != rpc_req.ops().size()) {
    LOG_WITH_PREFIX(INFO)
        << "Deduplicated request from leader. Original: " << OpId::FromPB(rpc_req.preceding_id())
        << "->" << OpsRangeString(rpc_req) << "   Dedup: " << deduplicated_req->preceding_op_id
        << "->" << deduplicated_req->OpsRangeString() << ", known committed: " << last_committed
        << ", received committed: " << OpId::FromPB(rpc_req.committed_op_id());
  }

  return Status::OK();
}

Status RaftConsensus::HandleLeaderRequestTermUnlocked(const LWConsensusRequestPB& request,
                                                      LWConsensusResponsePB* response) {
  // Do term checks first:
  if (request.caller_term() == state_->GetCurrentTermUnlocked()) {
    return Status::OK();
  }

  // If less, reject.
  if (request.caller_term() < state_->GetCurrentTermUnlocked()) {
    auto status = STATUS_FORMAT(
        IllegalState,
        "Rejecting Update request from peer $0 for earlier term $1. Current term is $2. Ops: $3",
        request.caller_uuid(), request.caller_term(), state_->GetCurrentTermUnlocked(),
        OpsRangeString(request));
    LOG_WITH_PREFIX(INFO) << status.message();
    FillConsensusResponseError(response, ConsensusErrorPB::INVALID_TERM, status);
    return Status::OK();
  }

  return HandleTermAdvanceUnlocked(request.caller_term());
}

Status RaftConsensus::EnforceLogMatchingPropertyMatchesUnlocked(const LeaderRequest& req,
                                                                LWConsensusResponsePB* response) {

  bool term_mismatch;
  if (state_->IsOpCommittedOrPending(req.preceding_op_id, &term_mismatch)) {
    return Status::OK();
  }

  string error_msg = Format(
    "Log matching property violated."
    " Preceding OpId in replica: $0. Preceding OpId from leader: $1. ($2 mismatch)",
    state_->GetLastReceivedOpIdUnlocked(), req.preceding_op_id, term_mismatch ? "term" : "index");

  FillConsensusResponseError(response,
                             ConsensusErrorPB::PRECEDING_ENTRY_DIDNT_MATCH,
                             STATUS(IllegalState, error_msg));

  LOG_WITH_PREFIX(INFO) << "Refusing update from remote peer "
                        << req.leader_uuid << ": " << error_msg;

  // If the terms mismatch we abort down to the index before the leader's preceding,
  // since we know that is the last opid that has a chance of not being overwritten.
  // Aborting preemptively here avoids us reporting a last received index that is
  // possibly higher than the leader's causing an avoidable cache miss on the leader's
  // queue.
  //
  // TODO: this isn't just an optimization! if we comment this out, we get
  // failures on raft_consensus-itest a couple percent of the time! Should investigate
  // why this is actually critical to do here, as opposed to just on requests that
  // append some ops.
  if (term_mismatch) {
    return state_->AbortOpsAfterUnlocked(req.preceding_op_id.index - 1);
  }

  return Status::OK();
}

Status RaftConsensus::CheckLeaderRequestOpIdSequence(
    const LeaderRequest& deduped_req, const LWConsensusRequestPB& request) {
  Status sequence_check_status;
  auto prev = deduped_req.preceding_op_id;
  for (const auto& message : deduped_req.messages) {
    auto current = OpId::FromPB(message->id());
    sequence_check_status = ReplicaState::CheckOpInSequence(prev, current);
    if (PREDICT_FALSE(!sequence_check_status.ok())) {
      LOG(ERROR) << "Leader request contained out-of-sequence messages. Status: "
          << sequence_check_status.ToString() << ". Leader Request: "
          << request.ShortDebugString();
      break;
    }
    prev = current;
  }

  return sequence_check_status;
}

Status RaftConsensus::CheckLeaderRequestUnlocked(
    const std::shared_ptr<LWConsensusRequestPB>& request,
    LWConsensusResponsePB* response, LeaderRequest* deduped_req) {
  RETURN_NOT_OK(DeduplicateLeaderRequestUnlocked(request, deduped_req));

  // This is an additional check for KUDU-639 that makes sure the message's index
  // and term are in the right sequence in the request, after we've deduplicated
  // them. We do this before we change any of the internal state.
  //
  // TODO move this to raft_consensus-state or whatever we transform that into.
  // We should be able to do this check for each append, but right now the way
  // we initialize raft_consensus-state is preventing us from doing so.
  RETURN_NOT_OK(CheckLeaderRequestOpIdSequence(*deduped_req, *request));

  RETURN_NOT_OK(HandleLeaderRequestTermUnlocked(*request, response));

  if (response->status().has_error()) {
    return Status::OK();
  }

  RETURN_NOT_OK(EnforceLogMatchingPropertyMatchesUnlocked(*deduped_req, response));

  if (response->status().has_error()) {
    return Status::OK();
  }

  // If the first of the messages to apply is not in our log, either it follows the last
  // received message or it replaces some in-flight.
  if (!deduped_req->messages.empty()) {
    auto first_id = yb::OpId::FromPB(deduped_req->messages[0]->id());
    bool term_mismatch;
    if (state_->IsOpCommittedOrPending(first_id, &term_mismatch)) {
      return STATUS_FORMAT(IllegalState,
                           "First deduped message $0 is committed or pending",
                           first_id);
    }

    // If the index is in our log but the terms are not the same abort down to the leader's
    // preceding id.
    if (term_mismatch) {
      // Since we are holding the lock ApplyPendingOperationsUnlocked would be invoked between
      // those two.
      RETURN_NOT_OK(state_->AbortOpsAfterUnlocked(deduped_req->preceding_op_id.index));
      RETURN_NOT_OK(log_->ResetLastSyncedEntryOpId(deduped_req->preceding_op_id));
    }
  }

  // If all of the above logic was successful then we can consider this to be
  // the effective leader of the configuration. If they are not currently marked as
  // the leader locally, mark them as leader now.
  const auto& caller_uuid = request->caller_uuid();
  if (PREDICT_FALSE(state_->HasLeaderUnlocked() &&
                    state_->GetLeaderUuidUnlocked() != caller_uuid)) {
    LOG_WITH_PREFIX(FATAL)
        << "Unexpected new leader in same term! "
        << "Existing leader UUID: " << state_->GetLeaderUuidUnlocked() << ", "
        << "new leader UUID: " << caller_uuid;
  }
  if (PREDICT_FALSE(!state_->HasLeaderUnlocked())) {
    SetLeaderUuidUnlocked(caller_uuid.ToBuffer());
  }

  return Status::OK();
}

Result<RaftConsensus::UpdateReplicaResult> RaftConsensus::UpdateReplica(
    const std::shared_ptr<LWConsensusRequestPB>& request_ptr,
    LWConsensusResponsePB* response) {
  TRACE_EVENT2("consensus", "RaftConsensus::UpdateReplica",
               "peer", peer_uuid(),
               "tablet", tablet_id());

  const auto& request = *request_ptr;
  if (request.has_propagated_hybrid_time()) {
    clock_->Update(HybridTime(request.propagated_hybrid_time()));
  }

  // The ordering of the following operations is crucial, read on for details.
  //
  // The main requirements explained in more detail below are:
  //
  //   1) We must enqueue the prepares before we write to our local log.
  //   2) If we were able to enqueue a prepare then we must be able to log it.
  //   3) If we fail to enqueue a prepare, we must not attempt to enqueue any
  //      later-indexed prepare or apply.
  //
  // See below for detailed rationale.
  //
  // The steps are:
  //
  // 0 - Dedup
  //
  // We make sure that we don't do anything on Replicate operations we've already received in a
  // previous call. This essentially makes this method idempotent.
  //
  // 1 - We mark as many pending operations as committed as we can.
  //
  // We may have some pending operations that, according to the leader, are now
  // committed. We Apply them early, because:
  // - Soon (step 2) we may reject the call due to excessive memory pressure. One
  //   way to relieve the pressure is by flushing the MRS, and applying these
  //   operations may unblock an in-flight Flush().
  // - The Apply and subsequent Prepares (step 2) can take place concurrently.
  //
  // 2 - We enqueue the Prepare of the operations.
  //
  // The actual prepares are enqueued in order but happen asynchronously so we don't
  // have decoding/acquiring locks on the critical path.
  //
  // We need to do this now for a number of reasons:
  // - Prepares, by themselves, are inconsequential, i.e. they do not mutate the
  //   state machine so, were we to crash afterwards, having the prepares in-flight
  //   won't hurt.
  // - Prepares depend on factors external to consensus (the operation drivers and
  //   the tablet peer) so if for some reason they cannot be enqueued we must know
  //   before we try write them to the WAL. Once enqueued, we assume that prepare will
  //   always succeed on a replica operation (because the leader already prepared them
  //   successfully, and thus we know they are valid).
  // - The prepares corresponding to every operation that was logged must be in-flight
  //   first. This because should we need to abort certain operations (say a new leader
  //   says they are not committed) we need to have those prepares in-flight so that
  //   the operations can be continued (in the abort path).
  // - Failure to enqueue prepares is OK, we can continue and let the leader know that
  //   we only went so far. The leader will re-send the remaining messages.
  // - Prepares represent new operations, and operations consume memory. Thus, if the
  //   overall memory pressure on the server is too high, we will reject the prepares.
  //
  // 3 - We enqueue the writes to the WAL.
  //
  // We enqueue writes to the WAL, but only the operations that were successfully
  // enqueued for prepare (for the reasons introduced above). This means that even
  // if a prepare fails to enqueue, if any of the previous prepares were successfully
  // submitted they must be written to the WAL.
  // If writing to the WAL fails, we're in an inconsistent state and we crash. In this
  // case, no one will ever know of the operations we previously prepared so those are
  // inconsequential.
  //
  // 4 - We mark the operations as committed.
  //
  // For each operation which has been committed by the leader, we update the
  // operation state to reflect that. If the logging has already succeeded for that
  // operation, this will trigger the Apply phase. Otherwise, Apply will be triggered
  // when the logging completes. In both cases the Apply phase executes asynchronously.
  // This must, of course, happen after the prepares have been triggered as the same batch
  // can both replicate/prepare and commit/apply an operation.
  //
  // Currently, if a prepare failed to enqueue we still trigger all applies for operations
  // with an id lower than it (if we have them). This is important now as the leader will
  // not re-send those commit messages. This will be moot when we move to the commit
  // commitIndex way of doing things as we can simply ignore the applies as we know
  // they will be triggered with the next successful batch.
  //
  // 5 - We wait for the writes to be durable.
  //
  // Before replying to the leader we wait for the writes to be durable. We then
  // just update the last replicated watermark and respond.
  //
  // TODO - These failure scenarios need to be exercised in an unit
  //        test. Moreover we need to add more fault injection spots (well that
  //        and actually use them) for each of these steps.
  TRACE("Updating replica for $0 ops", request.ops().size());

  // The deduplicated request.
  LeaderRequest deduped_req;

  ReplicaState::UniqueLock lock;
  RETURN_NOT_OK(state_->LockForUpdate(&lock));

  const auto old_leader = state_->GetLeaderUuidUnlocked();

  auto prev_committed_op_id = state_->GetCommittedOpIdUnlocked();

  request.caller_uuid().CopyToBuffer(&deduped_req.leader_uuid);

  RETURN_NOT_OK(CheckLeaderRequestUnlocked(request_ptr, response, &deduped_req));

  if (response->status().has_error()) {
    LOG_WITH_PREFIX(INFO)
        << "Returning from UpdateConsensus because of error: " << AsString(response->status());
    // We had an error, like an invalid term, we still fill the response.
    FillConsensusResponseOKUnlocked(response);
    return UpdateReplicaResult();
  }

  TEST_PAUSE_IF_FLAG(TEST_pause_update_replica);

  // Snooze the failure detector as soon as we decide to accept the message.
  // We are guaranteed to be acting as a FOLLOWER at this point by the above
  // sanity check.
  SnoozeFailureDetector(DO_NOT_LOG);

  auto now = MonoTime::Now();

  // Update the expiration time of the current leader's lease, so that when this follower becomes
  // a leader, it can wait out the time interval while the old leader might still be active.
  if (request.has_leader_lease_duration_ms()) {
    state_->UpdateOldLeaderLeaseExpirationOnNonLeaderUnlocked(
        CoarseTimeLease(deduped_req.leader_uuid,
                        CoarseMonoClock::now() + request.leader_lease_duration_ms() * 1ms),
        PhysicalComponentLease(deduped_req.leader_uuid, request.ht_lease_expiration()));
  }

  // Also prohibit voting for anyone for the minimum election timeout.
  withhold_votes_until_.store(now + MinimumElectionTimeout(), std::memory_order_release);

  // 1 - Early commit pending (and committed) operations
  RETURN_NOT_OK(EarlyCommitUnlocked(request, deduped_req));

  // 2 - Enqueue the prepares
  if (!VERIFY_RESULT(EnqueuePreparesUnlocked(request, &deduped_req, response))) {
    return UpdateReplicaResult();
  }

  if (deduped_req.committed_op_id.index < prev_committed_op_id.index) {
    deduped_req.committed_op_id = prev_committed_op_id;
  }

  // 3 - Enqueue the writes.
  auto last_from_leader = EnqueueWritesUnlocked(
      deduped_req, WriteEmpty(prev_committed_op_id != deduped_req.committed_op_id));

  // 4 - Mark operations as committed
  RETURN_NOT_OK(MarkOperationsAsCommittedUnlocked(request, deduped_req, last_from_leader));

  // Fill the response with the current state. We will not mutate anymore state until
  // we actually reply to the leader, we'll just wait for the messages to be durable.
  FillConsensusResponseOKUnlocked(response);

  UpdateReplicaResult result;

  // Check if there is an election pending and the op id pending upon has just been committed.
  const auto& pending_election_op_id = state_->GetPendingElectionOpIdUnlocked();
  result.start_election =
      !pending_election_op_id.empty() &&
      pending_election_op_id.index <= state_->GetCommittedOpIdUnlocked().index;

  if (!deduped_req.messages.empty()) {
    result.wait_for_op_id = state_->GetLastReceivedOpIdUnlocked();
  }
  result.current_term = state_->GetCurrentTermUnlocked();

  uint64_t update_time_ms = 0;
  if (request.has_propagated_hybrid_time()) {
    update_time_ms =  HybridTime::FromPB(
        request.propagated_hybrid_time()).GetPhysicalValueMicros() / 1000;
  } else if (!deduped_req.messages.empty()) {
    update_time_ms = HybridTime::FromPB(
        deduped_req.messages.back()->hybrid_time()).GetPhysicalValueMicros() / 1000;
  }
  follower_last_update_time_ms_metric_->UpdateTimestampInMilliseconds(
      (update_time_ms > 0 ? update_time_ms : clock_->Now().GetPhysicalValueMicros() / 1000));
  TRACE("UpdateReplica() finished");
  return result;
}

Status RaftConsensus::EarlyCommitUnlocked(const LWConsensusRequestPB& request,
                                          const LeaderRequest& deduped_req) {
  // What should we commit?
  // 1. As many pending operations as we can, except...
  // 2. ...if we commit beyond the preceding index, we'd regress KUDU-639
  //    ("Leader doesn't overwrite demoted follower's log properly"), and...
  // 3. ...the leader's committed index is always our upper bound.
  auto early_apply_up_to = state_->GetLastPendingOperationOpIdUnlocked();
  if (deduped_req.preceding_op_id.index < early_apply_up_to.index) {
    early_apply_up_to = deduped_req.preceding_op_id;
  }
  if (request.committed_op_id().index() < early_apply_up_to.index) {
    early_apply_up_to = yb::OpId::FromPB(request.committed_op_id());
  }

  VLOG_WITH_PREFIX(1) << "Early marking committed up to " << early_apply_up_to;
  TRACE("Early marking committed up to $0.$1", early_apply_up_to.term, early_apply_up_to.index);
  return ResultToStatus(state_->AdvanceCommittedOpIdUnlocked(early_apply_up_to, CouldStop::kTrue));
}

Result<bool> RaftConsensus::EnqueuePreparesUnlocked(const LWConsensusRequestPB& request,
                                                    LeaderRequest* deduped_req_ptr,
                                                    LWConsensusResponsePB* response) {
  LeaderRequest& deduped_req = *deduped_req_ptr;
  TRACE("Triggering prepare for $0 ops", deduped_req.messages.size());

  Status prepare_status;
  auto iter = deduped_req.messages.begin();

  if (PREDICT_TRUE(!deduped_req.messages.empty())) {
    // TODO Temporary until the leader explicitly propagates the safe hybrid_time.
    // TODO: what if there is a failure here because the updated time is too far in the future?
    clock_->Update(HybridTime(deduped_req.messages.back()->hybrid_time()));
  }

  HybridTime propagated_safe_time;
  if (request.has_propagated_safe_time()) {
    propagated_safe_time = HybridTime(request.propagated_safe_time());
    if (deduped_req.messages.empty()) {
      state_->context()->SetPropagatedSafeTime(propagated_safe_time);
    }
  }

  if (iter != deduped_req.messages.end()) {
    for (;;) {
      const ReplicateMsgPtr& msg = *iter;
      ++iter;
      bool last = iter == deduped_req.messages.end();
      prepare_status = StartReplicaOperationUnlocked(
          msg, last ? propagated_safe_time : HybridTime::kInvalid);
      if (PREDICT_FALSE(!prepare_status.ok())) {
        --iter;
        LOG_WITH_PREFIX(WARNING) << "StartReplicaOperationUnlocked failed: " << prepare_status;
        break;
      }
      if (last) {
        break;
      }
    }
  }

  // If we stopped before reaching the end we failed to prepare some message(s) and need
  // to perform cleanup, namely trimming deduped_req.messages to only contain the messages
  // that were actually prepared, and deleting the other ones since we've taken ownership
  // when we first deduped.
  bool incomplete = iter != deduped_req.messages.end();
  if (incomplete) {
    {
      const ReplicateMsgPtr msg = *iter;
      LOG_WITH_PREFIX(WARNING)
          << "Could not prepare operation for op: " << OpId::FromPB(msg->id()) << ". Suppressed "
          << (deduped_req.messages.end() - iter - 1) << " other warnings. Status for this op: "
          << prepare_status;
      deduped_req.messages.erase(iter, deduped_req.messages.end());
    }

    // If this is empty, it means we couldn't prepare a single de-duped message. There is nothing
    // else we can do. The leader will detect this and retry later.
    if (deduped_req.messages.empty()) {
      auto msg = Format("Rejecting Update request from peer $0 for term $1. "
                        "Could not prepare a single operation due to: $2",
                        request.caller_uuid(),
                        request.caller_term(),
                        prepare_status);
      LOG_WITH_PREFIX(INFO) << msg;
      FillConsensusResponseError(response, ConsensusErrorPB::CANNOT_PREPARE,
                                 STATUS(IllegalState, msg));
      FillConsensusResponseOKUnlocked(response);
      return false;
    }
  }

  deduped_req.committed_op_id = yb::OpId::FromPB(request.committed_op_id());
  if (!deduped_req.messages.empty()) {
    auto last_op_id = yb::OpId::FromPB(deduped_req.messages.back()->id());
    if (deduped_req.committed_op_id > last_op_id) {
      LOG_IF_WITH_PREFIX(DFATAL, !incomplete)
          << "Received committed op id: " << deduped_req.committed_op_id
          << ", past last known op id: " << last_op_id;

      // It is possible that we failed to prepare of of messages,
      // so limit committed op id to avoid having committed op id past last known op it.
      deduped_req.committed_op_id = last_op_id;
    }
  }

  return true;
}

yb::OpId RaftConsensus::EnqueueWritesUnlocked(const LeaderRequest& deduped_req,
                                              WriteEmpty write_empty) {
  // Now that we've triggered the prepares enqueue the operations to be written
  // to the WAL.
  if (PREDICT_TRUE(!deduped_req.messages.empty()) || write_empty) {
    // Trigger the log append asap, if fsync() is on this might take a while
    // and we can't reply until this is done.
    //
    // Since we've prepared, we need to be able to append (or we risk trying to apply
    // later something that wasn't logged). We crash if we can't.
    CHECK_OK(queue_->AppendOperations(
        deduped_req.messages, deduped_req.committed_op_id, state_->Clock().Now()));
  }

  return !deduped_req.messages.empty() ?
      OpId::FromPB(deduped_req.messages.back()->id()) : deduped_req.preceding_op_id;
}

Status RaftConsensus::WaitForWrites(int64_t term, const OpId& wait_for_op_id) {
  // 5 - We wait for the writes to be durable.

  // Note that this is safe because dist consensus now only supports a single outstanding
  // request at a time and this way we can allow commits to proceed while we wait.
  TRACE("Waiting on the replicates to finish logging");
  TRACE_EVENT0("consensus", "Wait for log");
  for (;;) {
    auto wait_result = log_->WaitForSafeOpIdToApply(
        wait_for_op_id, MonoDelta::FromMilliseconds(FLAGS_raft_heartbeat_interval_ms));
    // If just waiting for our log append to finish lets snooze the timer.
    // We don't want to fire leader election because we're waiting on our own log.
    if (!wait_result.empty()) {
      break;
    }
    int64_t new_term;
    {
      auto lock = state_->LockForRead();
      new_term = state_->GetCurrentTermUnlocked();
    }
    if (term != new_term) {
      return STATUS_FORMAT(IllegalState, "Term changed to $0 while waiting for writes in term $1",
                           new_term, term);
    }

    SnoozeFailureDetector(DO_NOT_LOG);

    const auto election_timeout_at = MonoTime::Now() + MinimumElectionTimeout();
    UpdateAtomicMax(&withhold_votes_until_, election_timeout_at);
  }
  TRACE("Finished waiting on the replicates to finish logging");

  return Status::OK();
}

Status RaftConsensus::MarkOperationsAsCommittedUnlocked(const LWConsensusRequestPB& request,
                                                        const LeaderRequest& deduped_req,
                                                        yb::OpId last_from_leader) {
  // Choose the last operation to be applied. This will either be 'committed_index', if
  // no prepare enqueuing failed, or the minimum between 'committed_index' and the id of
  // the last successfully enqueued prepare, if some prepare failed to enqueue.
  yb::OpId apply_up_to;
  if (last_from_leader.index < request.committed_op_id().index()) {
    // we should never apply anything later than what we received in this request
    apply_up_to = last_from_leader;

    LOG_WITH_PREFIX(INFO)
        << "Received commit index " << OpId::FromPB(request.committed_op_id())
        << " from the leader but only marked up to " << apply_up_to << " as committed.";
  } else {
    apply_up_to = OpId::FromPB(request.committed_op_id());
  }

  // We can now update the last received watermark.
  //
  // We do it here (and before we actually hear back from the wal whether things
  // are durable) so that, if we receive another, possible duplicate, message
  // that exercises this path we don't handle these messages twice.
  //
  // If any messages failed to be started locally, then we already have removed them
  // from 'deduped_req' at this point. So, we can simply update our last-received
  // watermark to the last message that remains in 'deduped_req'.
  //
  // It's possible that the leader didn't send us any new data -- it might be a completely
  // duplicate request. In that case, we only update LastReceivedOpIdFromCurrentLeader
  // Not updating this may result in the leader resending the same set of op-ids
  // and resulting in empty de-duped requests forever. See GH #15629.
  if (!deduped_req.messages.empty()) {
    OpId last_appended = OpId::FromPB(deduped_req.messages.back()->id());
    TRACE(Format("Updating last received op as $0", last_appended));
    state_->UpdateLastReceivedOpIdUnlocked(last_appended);
  } else {
    if (state_->GetLastReceivedOpIdUnlocked().index < deduped_req.preceding_op_id.index) {
      return STATUS_FORMAT(InvalidArgument,
                          "Bad preceding_opid: $0, last received: $1",
                          deduped_req.preceding_op_id,
                          state_->GetLastReceivedOpIdUnlocked());
    }
    state_->UpdateLastReceivedOpIdFromCurrentLeaderIfEmptyUnlocked(deduped_req.preceding_op_id);
  }

  VLOG_WITH_PREFIX(1) << "Marking committed up to " << apply_up_to;
  TRACE(Format("Marking committed up to $0", apply_up_to));
  return ResultToStatus(state_->AdvanceCommittedOpIdUnlocked(apply_up_to, CouldStop::kTrue));
}

void RaftConsensus::FillConsensusResponseOKUnlocked(LWConsensusResponsePB* response) {
  TRACE("Filling consensus response to leader.");
  response->set_responder_term(state_->GetCurrentTermUnlocked());
  state_->GetLastReceivedOpIdUnlocked().ToPB(response->mutable_status()->mutable_last_received());
  state_->GetLastReceivedOpIdCurLeaderUnlocked().ToPB(
      response->mutable_status()->mutable_last_received_current_leader());
  response->mutable_status()->set_last_committed_idx(state_->GetCommittedOpIdUnlocked().index);
  state_->GetLastAppliedOpIdUnlocked().ToPB(response->mutable_status()->mutable_last_applied());
}

void RaftConsensus::FillConsensusResponseError(LWConsensusResponsePB* response,
                                               ConsensusErrorPB::Code error_code,
                                               const Status& status) {
  auto* error = response->mutable_status()->mutable_error();
  error->set_code(error_code);
  StatusToPB(status, error->mutable_status());
}

Status RaftConsensus::RequestVote(const VoteRequestPB* request, VoteResponsePB* response) {
  TRACE_EVENT2("consensus", "RaftConsensus::RequestVote",
               "peer", peer_uuid(),
               "tablet", tablet_id());
  bool preelection = request->preelection();

  response->set_responder_uuid(state_->GetPeerUuid());
  response->set_preelection(preelection);

  // We must acquire the update lock in order to ensure that this vote action
  // takes place between requests.
  // Lock ordering: The update lock must be acquired before the ReplicaState lock.
  std::unique_lock<decltype(update_mutex_)> update_guard(update_mutex_, std::defer_lock);
  if (FLAGS_enable_leader_failure_detection) {
    auto try_lock_result [[maybe_unused]] = update_guard.try_lock();  // NOLINT
  } else {
    // If failure detection is not enabled, then we can't just reject the vote,
    // because there will be no automatic retry later. So, block for the lock.
    update_guard.lock();
  }
  if (!update_guard.owns_lock()) {
    // There is another vote or update concurrent with the vote. In that case, that
    // other request is likely to reset the timer, and we'll end up just voting
    // "NO" after waiting. To avoid starving RPC handlers and causing cascading
    // timeouts, just vote a quick NO.
    //
    // We still need to take the state lock in order to respond with term info, etc.
    ReplicaState::UniqueLock state_guard;
    RETURN_NOT_OK(state_->LockForConfigChange(&state_guard));
    return RequestVoteRespondIsBusy(request, response);
  }

  // Acquire the replica state lock so we can read / modify the consensus state.
  ReplicaState::UniqueLock state_guard;
  RETURN_NOT_OK(state_->LockForConfigChange(&state_guard));

  if (PREDICT_FALSE(FLAGS_TEST_request_vote_respond_leader_still_alive)) {
    return RequestVoteRespondLeaderIsAlive(request, response, "fake_peed_uuid");
  }

  // If the node is not in the configuration, allow the vote (this is required by Raft)
  // but log an informational message anyway.
  if (!IsRaftConfigMember(request->candidate_uuid(), state_->GetActiveConfigUnlocked())) {
    LOG_WITH_PREFIX(INFO) << "Handling vote request from an unknown peer "
                          << request->candidate_uuid();
  }

  // If we've heard recently from the leader, then we should ignore the request
  // (except if it is the leader itself requesting a vote -- something that might
  //  happen if the leader were to stepdown and call an election.). Otherwise,
  // it might be from a "disruptive" server. This could happen in a few cases:
  //
  // 1) Network partitions
  // If the leader can talk to a majority of the nodes, but is partitioned from a
  // bad node, the bad node's failure detector will trigger. If the bad node is
  // able to reach other nodes in the cluster, it will continuously trigger elections.
  //
  // 2) An abandoned node
  // It's possible that a node has fallen behind the log GC mark of the leader. In that
  // case, the leader will stop sending it requests. Eventually, the configuration
  // will change to eject the abandoned node, but until that point, we don't want the
  // abandoned follower to disturb the other nodes.
  //
  // See also https://ramcloud.stanford.edu/~ongaro/thesis.pdf
  // section 4.2.3.
  MonoTime now = MonoTime::Now();
  auto leader_uuid = state_->GetLeaderUuidUnlocked();
  if (request->candidate_uuid() != leader_uuid &&
      !request->ignore_live_leader() &&
      now < withhold_votes_until_.load(std::memory_order_acquire)) {
    return RequestVoteRespondLeaderIsAlive(request, response, leader_uuid);
  }

  // Candidate is running behind.
  if (request->candidate_term() < state_->GetCurrentTermUnlocked()) {
    return RequestVoteRespondInvalidTerm(request, response);
  }

  // We already voted this term.
  if (request->candidate_term() == state_->GetCurrentTermUnlocked() &&
      state_->HasVotedCurrentTermUnlocked()) {

    // Already voted for the same candidate in the current term.
    if (state_->GetVotedForCurrentTermUnlocked() == request->candidate_uuid()) {
      return RequestVoteRespondVoteAlreadyGranted(request, response);
    }

    // Voted for someone else in current term.
    return RequestVoteRespondAlreadyVotedForOther(request, response);
  }

  // The term advanced.
  if (request->candidate_term() > state_->GetCurrentTermUnlocked() && !preelection) {
    RETURN_NOT_OK_PREPEND(HandleTermAdvanceUnlocked(request->candidate_term()),
        Substitute("Could not step down in RequestVote. Current term: $0, candidate term: $1",
            state_->GetCurrentTermUnlocked(), request->candidate_term()));
  }

  // Candidate must have last-logged OpId at least as large as our own to get our vote.
  OpIdPB local_last_logged_opid;
  GetLatestOpIdFromLog().ToPB(&local_last_logged_opid);
  if (OpIdLessThan(request->candidate_status().last_received(), local_last_logged_opid)) {
    return RequestVoteRespondLastOpIdTooOld(local_last_logged_opid, request, response);
  }

  if (!preelection) {
    // Clear the pending election op id if any before granting the vote. If another peer jumps in
    // before we can catch up and start the election, let's not disrupt the quorum with another
    // election.
    state_->ClearPendingElectionOpIdUnlocked();
  }

  auto remaining_old_leader_lease = state_->RemainingOldLeaderLeaseDuration();

  if (remaining_old_leader_lease.Initialized()) {
    response->set_remaining_leader_lease_duration_ms(
        narrow_cast<int32_t>(remaining_old_leader_lease.ToMilliseconds()));
    response->set_leader_lease_uuid(state_->old_leader_lease().holder_uuid);
  } else {
    remaining_old_leader_lease = state_->RemainingMajorityReplicatedLeaderLeaseDuration();
    if (remaining_old_leader_lease.Initialized()) {
      response->set_remaining_leader_lease_duration_ms(
        narrow_cast<int32_t>(remaining_old_leader_lease.ToMilliseconds()));
      response->set_leader_lease_uuid(peer_uuid());
    }
  }

  const auto& old_leader_ht_lease = state_->old_leader_ht_lease();
  if (old_leader_ht_lease) {
    response->set_leader_ht_lease_expiration(old_leader_ht_lease.expiration);
    response->set_leader_ht_lease_uuid(old_leader_ht_lease.holder_uuid);
  } else {
    const auto ht_lease = VERIFY_RESULT(MajorityReplicatedHtLeaseExpiration(
        /* min_allowed = */ 0, /* deadline = */ CoarseTimePoint::max()));
    if (ht_lease) {
      response->set_leader_ht_lease_expiration(ht_lease);
      response->set_leader_ht_lease_uuid(peer_uuid());
    }
  }

  // Passed all our checks. Vote granted.
  if (preelection) {
    LOG_WITH_PREFIX(INFO) << "Pre-election. Granting vote for candidate "
                          << request->candidate_uuid() << " in term " << request->candidate_term();
    FillVoteResponseVoteGranted(*request, response);
    return Status::OK();
  }

  return RequestVoteRespondVoteGranted(request, response);
}

Status RaftConsensus::IsLeaderReadyForChangeConfigUnlocked(ChangeConfigType type,
                                                           const std::string& server_uuid) {
  const RaftConfigPB& active_config = state_->GetActiveConfigUnlocked();
  // Check that all the following requirements are met:
  // 1. We are required by Raft to reject config change operations until we have
  //    committed at least one operation in our current term as leader.
  //    See https://groups.google.com/forum/#!topic/raft-dev/t4xj6dJTP6E
  // 2. Ensure there is no other pending change config.
  if (!state_->AreCommittedAndCurrentTermsSameUnlocked() ||
      state_->IsConfigChangePendingUnlocked()) {
    return STATUS_FORMAT(IllegalState,
                         "Leader is not ready for Config Change, can try again. "
                         "Type: $0. Has opid: $1. Committed config: $2. "
                         "Pending config: $3. Current term: $4. Committed op id: $5.",
                         ChangeConfigType_Name(type),
                         active_config.has_opid_index(),
                         state_->GetCommittedConfigUnlocked().ShortDebugString(),
                         state_->IsConfigChangePendingUnlocked() ?
                             state_->GetPendingConfigUnlocked().ShortDebugString() : "",
                         state_->GetCurrentTermUnlocked(), state_->GetCommittedOpIdUnlocked());
  }
  // For sys catalog tablet, additionally ensure that there are no servers currently in transition.
  // If not, it could lead to data loss.
  // For instance, assuming that we allow processing of ChangeConfig requests for sys catalog amidst
  // transition (like with the other tablets), consider a full master move operation where we want
  // to go from  A,B,C -> D,E,F. Let's assume that D,E,F are added but get stuck in PRE_VOTER state.
  // The initial remove requests for B and C would succeed and we would end up in the state A,D,E,F.
  // Though the removal of A would throw an error, if the operator mistakenly assumes that D,E,F are
  // caught up and overrides the master server conf file, the new peers would form a quorum and kick
  // out A, thus leading to orphaned tablet cleanup resulting in data loss.
  //
  // We have this additional safeguard in place for sys catalog tablet since the config change ops
  // are driver by the user/admin, and the safeguard helps prevent us from going into the state
  // described above.
  //
  // We don't have similar restrictions for user tablets since the config change operations are run
  // by the load balancer. Additionally, we explicitly prevent raft from kicking out an unavailable
  // VOTER peer if it leads to < 2 VOTERS in the new config. Hence it is okay for the leader to
  // process new config change operations for user tablets even when a peer is in transition hoping
  // that it would expedite the process.
  //
  // TODO(#19453): A more optimized approach would be to allow changes amidst sys catalog transition
  // while ensuring we have >= rf/2 + 1 VOTERS in the new config.
  if (tablet_id() == master::kSysCatalogTabletId) {
      size_t servers_in_transition = 0;
      if (type == ADD_SERVER) {
        servers_in_transition = CountServersInTransition(active_config);
      } else if (type == REMOVE_SERVER) {
        // If we are trying to remove the server in transition, then servers_in_transition shouldn't
        // count it so we can proceed with the operation.
        servers_in_transition = CountServersInTransition(active_config, server_uuid);
      }
      if (servers_in_transition != 0) {
        return STATUS_FORMAT(IllegalState,
                             "Leader is not ready for Config Change, there is already one server "
                             "in transition. Try again once the transition completes or remove the "
                             "server in transition. Type: $0. Has opid: $1. Committed config: $2. "
                             "Pending config: $3. Current term: $4. Committed op id: $5. "
                             "Num peers in transit: $6",
                             ChangeConfigType_Name(type),
                             active_config.has_opid_index(),
                             state_->GetCommittedConfigUnlocked().ShortDebugString(),
                             state_->IsConfigChangePendingUnlocked() ?
                                 state_->GetPendingConfigUnlocked().ShortDebugString() : "",
                             state_->GetCurrentTermUnlocked(), state_->GetCommittedOpIdUnlocked(),
                             servers_in_transition);
      }
  }
  return Status::OK();
}

Status RaftConsensus::ChangeConfig(const ChangeConfigRequestPB& req,
                                   const StdStatusCallback& client_cb,
                                   boost::optional<TabletServerErrorPB::Code>* error_code) {
  if (PREDICT_FALSE(!req.has_type())) {
    return STATUS(InvalidArgument, "Must specify 'type' argument to ChangeConfig()",
                                   req.ShortDebugString());
  }
  if (PREDICT_FALSE(!req.has_server())) {
    *error_code = TabletServerErrorPB::INVALID_CONFIG;
    return STATUS(InvalidArgument, "Must specify 'server' argument to ChangeConfig()",
                                   req.ShortDebugString());
  }
  if (PREDICT_FALSE(req.tablet_id() != tablet_id())) {
    *error_code = TabletServerErrorPB::INVALID_CONFIG;
    return STATUS_SUBSTITUTE(
        InvalidArgument,
        "ChangeConfig request received for a different tablet at RaftConsensus serving tablet $0. "
        "ChangeConfigRequestPB: $1", tablet_id(), req.ShortDebugString());
  }
  YB_LOG_EVERY_N(INFO, FLAGS_TEST_log_change_config_every_n)
      << "Received ChangeConfig request " << req.ShortDebugString();
  ChangeConfigType type = req.type();
  bool use_hostport = req.has_use_host() && req.use_host();

  if (type != REMOVE_SERVER && use_hostport) {
    return STATUS_SUBSTITUTE(InvalidArgument, "Cannot set use_host for change config type $0, "
                             "only allowed with REMOVE_SERVER.", type);
  }

  if (PREDICT_FALSE(FLAGS_TEST_return_error_on_change_config != 0.0 && type == CHANGE_ROLE)) {
    DCHECK(FLAGS_TEST_return_error_on_change_config >= 0.0 &&
           FLAGS_TEST_return_error_on_change_config <= 1.0);
    if (clock_->Now().ToUint64() % 100 < 100 * FLAGS_TEST_return_error_on_change_config) {
      return STATUS(IllegalState, "Returning error for unit test");
    }
  }
  RaftPeerPB* new_peer = nullptr;
  const RaftPeerPB& server = req.server();
  if (!use_hostport && !server.has_permanent_uuid()) {
    return STATUS(InvalidArgument,
                  Substitute("server must have permanent_uuid or use_host specified: $0",
                             req.ShortDebugString()));
  }
  {
    ReplicaState::UniqueLock lock;
    RETURN_NOT_OK(state_->LockForConfigChange(&lock));
    Status s = state_->CheckActiveLeaderUnlocked(LeaderLeaseCheckMode::DONT_NEED_LEASE);
    if (!s.ok()) {
      *error_code = TabletServerErrorPB::NOT_THE_LEADER;
      return s;
    }

    const string& server_uuid = server.has_permanent_uuid() ? server.permanent_uuid() : "";
    s = IsLeaderReadyForChangeConfigUnlocked(type, server_uuid);
    if (!s.ok()) {
      YB_LOG_EVERY_N(INFO, FLAGS_TEST_log_change_config_every_n)
          << "Returning not ready for " << ChangeConfigType_Name(type)
          << " due to error " << s.ToString();
      *error_code = TabletServerErrorPB::LEADER_NOT_READY_CHANGE_CONFIG;
      return s;
    }

    const RaftConfigPB& committed_config = state_->GetCommittedConfigUnlocked();

    // Support atomic ChangeConfig requests.
    if (req.has_cas_config_opid_index()) {
      if (committed_config.opid_index() != req.cas_config_opid_index()) {
        *error_code = TabletServerErrorPB::CAS_FAILED;
        return STATUS(IllegalState, Substitute("Request specified cas_config_opid_index "
                                               "of $0 but the committed config has opid_index "
                                               "of $1",
                                               req.cas_config_opid_index(),
                                               committed_config.opid_index()));
      }
    }

    RaftConfigPB new_config = committed_config;
    new_config.clear_opid_index();
    switch (type) {
      case ADD_SERVER:
        // Ensure the server we are adding is not already a member of the configuration.
        if (IsRaftConfigMember(server_uuid, committed_config)) {
          *error_code = TabletServerErrorPB::ADD_CHANGE_CONFIG_ALREADY_PRESENT;
          return STATUS(IllegalState,
              Substitute("Server with UUID $0 is already a member of the config. RaftConfig: $1",
                        server_uuid, committed_config.ShortDebugString()));
        }
        if (!server.has_member_type()) {
          return STATUS(InvalidArgument,
                        Substitute("Server must have member_type specified. Request: $0",
                                   req.ShortDebugString()));
        }
        if (server.member_type() != PeerMemberType::PRE_VOTER &&
            server.member_type() != PeerMemberType::PRE_OBSERVER) {
          return STATUS(InvalidArgument,
              Substitute("Server with UUID $0 must be of member_type PRE_VOTER or PRE_OBSERVER. "
                         "member_type received: $1", server_uuid,
                         PeerMemberType_Name(server.member_type())));
        }
        if (server.last_known_private_addr().empty()) {
          return STATUS(InvalidArgument, "server must have last_known_addr specified",
                                         req.ShortDebugString());
        }
        new_peer = new_config.add_peers();
        *new_peer = server;
        break;

      case REMOVE_SERVER:
        if (use_hostport) {
          if (server.last_known_private_addr().empty()) {
            return STATUS(InvalidArgument, "Must have last_known_addr specified.",
                          req.ShortDebugString());
          }
          HostPort leader_hp;
          RETURN_NOT_OK(GetHostPortFromConfig(
              new_config, peer_uuid(), queue_->local_cloud_info(), &leader_hp));
          for (const auto& host_port : server.last_known_private_addr()) {
            if (leader_hp.port() == host_port.port() && leader_hp.host() == host_port.host()) {
              return STATUS(InvalidArgument, "Cannot remove live leader using hostport.",
                            req.ShortDebugString());
            }
          }
        }
        if (server_uuid == peer_uuid()) {
          *error_code = TabletServerErrorPB::LEADER_NEEDS_STEP_DOWN;
          return STATUS(InvalidArgument,
              Substitute("Cannot remove peer $0 from the config because it is the leader. "
                         "Force another leader to be elected to remove this server. "
                         "Active consensus state: $1", server_uuid,
                         state_->ConsensusStateUnlocked(CONSENSUS_CONFIG_ACTIVE)
                            .ShortDebugString()));
        }
        if (!RemoveFromRaftConfig(&new_config, req)) {
          *error_code = TabletServerErrorPB::REMOVE_CHANGE_CONFIG_NOT_PRESENT;
          return STATUS(NotFound,
              Substitute("Server with UUID $0 not a member of the config. RaftConfig: $1",
                        server_uuid, committed_config.ShortDebugString()));
        }
        break;

      case CHANGE_ROLE:
        if (server_uuid == peer_uuid()) {
          return STATUS(InvalidArgument,
              Substitute("Cannot change role of peer $0 because it is the leader. Force "
                         "another leader to be elected. Active consensus state: $1", server_uuid,
                         state_->ConsensusStateUnlocked(CONSENSUS_CONFIG_ACTIVE)
                             .ShortDebugString()));
        }
        VLOG(3) << "config before CHANGE_ROLE: " << new_config.DebugString();

        if (!GetMutableRaftConfigMember(&new_config, server_uuid, &new_peer).ok()) {
          return STATUS(NotFound,
            Substitute("Server with UUID $0 not a member of the config. RaftConfig: $1",
                       server_uuid, new_config.ShortDebugString()));
        }
        if (new_peer->member_type() != PeerMemberType::PRE_OBSERVER &&
            new_peer->member_type() != PeerMemberType::PRE_VOTER) {
          return STATUS(IllegalState, Substitute("Cannot change role of server with UUID $0 "
                                                 "because its member type is $1",
                                                 server_uuid, new_peer->member_type()));
        }
        if (new_peer->member_type() == PeerMemberType::PRE_OBSERVER) {
          new_peer->set_member_type(PeerMemberType::OBSERVER);
        } else {
          new_peer->set_member_type(PeerMemberType::VOTER);
        }

        VLOG(3) << "config after CHANGE_ROLE: " << new_config.DebugString();
        break;
      default:
        return STATUS(InvalidArgument, Substitute("Unsupported type $0",
                                                  ChangeConfigType_Name(type)));
    }

    auto cc_replicate = rpc::MakeSharedMessage<LWReplicateMsg>();
    cc_replicate->set_op_type(CHANGE_CONFIG_OP);
    auto* cc_req = cc_replicate->mutable_change_config_record();
    cc_req->ref_tablet_id(tablet_id());
    *cc_req->mutable_old_config() = committed_config;
    *cc_req->mutable_new_config() = new_config;
    // Note: This hybrid_time has no meaning from a serialization perspective
    // because this method is not executed on the TabletPeer's prepare thread.
    cc_replicate->set_hybrid_time(clock_->Now().ToUint64());
    state_->GetCommittedOpIdUnlocked().ToPB(cc_replicate->mutable_committed_op_id());

    auto context = std::make_shared<StateChangeContext>(
        StateChangeReason::LEADER_CONFIG_CHANGE_COMPLETE, *cc_req,
        type == REMOVE_SERVER ? server_uuid : "");

    RETURN_NOT_OK(
        ReplicateConfigChangeUnlocked(cc_replicate, new_config, type,
                                      std::bind(&RaftConsensus::MarkDirtyOnSuccess,
                                           this,
                                           std::move(context),
                                           std::move(client_cb), std::placeholders::_1)));
  }

  peer_manager_->SignalRequest(RequestTriggerMode::kNonEmptyOnly);

  return Status::OK();
}

Status RaftConsensus::UnsafeChangeConfig(
    const UnsafeChangeConfigRequestPB& req,
    boost::optional<tserver::TabletServerErrorPB::Code>* error_code) {
  if (PREDICT_FALSE(!req.has_new_config())) {
    *error_code = TabletServerErrorPB::INVALID_CONFIG;
    return STATUS(InvalidArgument, "Request must contain 'new_config' argument "
                  "to UnsafeChangeConfig()", yb::ToString(req));
  }
  if (PREDICT_FALSE(!req.has_caller_id())) {
    *error_code = TabletServerErrorPB::INVALID_CONFIG;
    return STATUS(InvalidArgument, "Must specify 'caller_id' argument to UnsafeChangeConfig()",
                  yb::ToString(req));
  }

  // Grab the committed config and current term on this node.
  int64_t current_term;
  RaftConfigPB committed_config;
  OpId last_committed_opid;
  OpId preceding_opid;
  string local_peer_uuid;
  {
    // Take the snapshot of the replica state and queue state so that
    // we can stick them in the consensus update request later.
    auto lock = state_->LockForRead();
    local_peer_uuid = state_->GetPeerUuid();
    current_term = state_->GetCurrentTermUnlocked();
    committed_config = state_->GetCommittedConfigUnlocked();
    if (state_->IsConfigChangePendingUnlocked()) {
      LOG_WITH_PREFIX(WARNING) << "Replica has a pending config, but the new config "
                               << "will be unsafely changed anyway. "
                               << "Currently pending config on the node: "
                               << yb::ToString(state_->GetPendingConfigUnlocked());
    }
    last_committed_opid = state_->GetCommittedOpIdUnlocked();
    preceding_opid = state_->GetLastAppliedOpIdUnlocked();
  }

  // Validate that passed replica uuids are part of the committed config
  // on this node.  This allows a manual recovery tool to only have to specify
  // the uuid of each replica in the new config without having to know the
  // addresses of each server (since we can get the address information from
  // the committed config). Additionally, only a subset of the committed config
  // is required for typical cluster repair scenarios.
  std::unordered_set<string> retained_peer_uuids;
  const RaftConfigPB& config = req.new_config();
  for (const RaftPeerPB& new_peer : config.peers()) {
    const string& peer_uuid = new_peer.permanent_uuid();
    retained_peer_uuids.insert(peer_uuid);
    if (!IsRaftConfigMember(peer_uuid, committed_config)) {
      *error_code = TabletServerErrorPB::INVALID_CONFIG;
      return STATUS(InvalidArgument, Substitute("Peer with uuid $0 is not in the committed  "
                                                "config on this replica, rejecting the  "
                                                "unsafe config change request for tablet $1. "
                                                "Committed config: $2",
                                                peer_uuid, req.tablet_id(),
                                                yb::ToString(committed_config)));
    }
  }

  RaftConfigPB new_config = committed_config;
  for (const auto& peer : committed_config.peers()) {
    const string& peer_uuid = peer.permanent_uuid();
    if (!ContainsKey(retained_peer_uuids, peer_uuid)) {
      ChangeConfigRequestPB change_config_request;
      change_config_request.set_tablet_id(tablet_id());
      change_config_request.mutable_server()->set_permanent_uuid(peer_uuid);
      change_config_request.set_type(REMOVE_SERVER);
      change_config_request.set_cas_config_opid_index(committed_config.opid_index());
      CHECK(RemoveFromRaftConfig(&new_config, change_config_request));
    }
  }
  // Check that local peer is part of the new config and is a VOTER.
  // Although it is valid for a local replica to not have itself
  // in the committed config, it is rare and a replica without itself
  // in the latest config is definitely not caught up with the latest leader's log.
  if (!IsRaftConfigVoter(local_peer_uuid, new_config)) {
    *error_code = TabletServerErrorPB::INVALID_CONFIG;
    return STATUS(InvalidArgument, Substitute("Local replica uuid $0 is not "
                                              "a VOTER in the new config, "
                                              "rejecting the unsafe config "
                                              "change request for tablet $1. "
                                              "Rejected config: $2" ,
                                              local_peer_uuid, req.tablet_id(),
                                              yb::ToString(new_config)));
  }
  new_config.set_unsafe_config_change(true);
  int64 replicate_opid_index = preceding_opid.index + 1;
  new_config.clear_opid_index();

  // Sanity check the new config.
  Status s = VerifyRaftConfig(new_config, UNCOMMITTED_QUORUM);
  if (!s.ok()) {
    *error_code = TabletServerErrorPB::INVALID_CONFIG;
    return STATUS(InvalidArgument, Substitute("The resulting new config for tablet $0  "
                                              "from passed parameters has failed raft "
                                              "config sanity check: $1",
                                              req.tablet_id(), s.ToString()));
  }

  // Prepare the consensus request as if the request is being generated
  // from a different leader.
  auto consensus_req = rpc::MakeSharedMessage<LWConsensusRequestPB>();
  consensus_req->ref_caller_uuid(req.caller_id());
  // Bumping up the term for the consensus request being generated.
  // This makes this request appear to come from a new leader that
  // the local replica doesn't know about yet. If the local replica
  // happens to be the leader, this will cause it to step down.
  const int64 new_term = current_term + 1;
  consensus_req->set_caller_term(new_term);
  preceding_opid.ToPB(consensus_req->mutable_preceding_id());
  last_committed_opid.ToPB(consensus_req->mutable_committed_op_id());

  // Prepare the replicate msg to be replicated.
  auto* replicate = consensus_req->add_ops();
  auto* cc_req = replicate->mutable_change_config_record();
  cc_req->ref_tablet_id(req.tablet_id());
  *cc_req->mutable_old_config() = committed_config;
  *cc_req->mutable_new_config() = new_config;
  auto* id = replicate->mutable_id();
  // Bumping up both the term and the opid_index from what's found in the log.
  id->set_term(new_term);
  id->set_index(replicate_opid_index);
  replicate->set_op_type(CHANGE_CONFIG_OP);
  replicate->set_hybrid_time(clock_->Now().ToUint64());
  last_committed_opid.ToPB(replicate->mutable_committed_op_id());

  VLOG_WITH_PREFIX(3) << "UnsafeChangeConfig: Generated consensus request: "
                      << yb::ToString(consensus_req);

  LOG_WITH_PREFIX(WARNING) << "PROCEEDING WITH UNSAFE CONFIG CHANGE ON THIS SERVER, "
                           << "COMMITTED CONFIG: " << yb::ToString(committed_config)
                           << "NEW CONFIG: " << yb::ToString(new_config);

  const auto deadline = CoarseMonoClock::Now() + 15s;  // TODO: fix me
  LWConsensusResponsePB consensus_resp(&consensus_req->arena());
  s = Update(consensus_req, &consensus_resp, deadline);
  if (!s.ok() || consensus_resp.has_error()) {
    *error_code = TabletServerErrorPB::UNKNOWN_ERROR;
  }
  if (s.ok() && consensus_resp.has_error()) {
    s = StatusFromPB(consensus_resp.error().status());
  }
  return s;
}

std::vector<FollowerCommunicationTime> RaftConsensus::GetFollowerCommunicationTimes() {
  return queue_->GetFollowerCommunicationTimes();
}

void RaftConsensus::Shutdown() {
  LOG_WITH_PREFIX(INFO) << "Shutdown.";

  // Avoid taking locks if already shut down so we don't violate
  // ThreadRestrictions assertions in the case where the RaftConsensus
  // destructor runs on the reactor thread due to an election callback being
  // the last outstanding reference.
  if (shutdown_.Load(kMemOrderAcquire)) return;

  CHECK_OK(ExecuteHook(PRE_SHUTDOWN));

  {
    ReplicaState::UniqueLock lock;
    // Transition to kShuttingDown state.
    CHECK_OK(state_->LockForShutdown(&lock));
    step_down_check_tracker_.StartShutdown();
  }
  step_down_check_tracker_.CompleteShutdown();

  // Close the peer manager.
  peer_manager_->Close();

  // We must close the queue after we close the peers.
  queue_->Close();

  CHECK_OK(state_->CancelPendingOperations());

  {
    ReplicaState::UniqueLock lock;
    CHECK_OK(state_->LockForShutdown(&lock));
    CHECK_EQ(ReplicaState::kShuttingDown, state_->state());
    CHECK_OK(state_->ShutdownUnlocked());
    LOG_WITH_PREFIX(INFO) << "Raft consensus is shut down!";
  }

  // Shut down things that might acquire locks during destruction.
  raft_pool_concurrent_token_->Shutdown();
  // We might not have run Start yet, so make sure we have a FD.
  if (failure_detector_) {
    DisableFailureDetector();
  }

  CHECK_OK(ExecuteHook(POST_SHUTDOWN));

  shutdown_.Store(true, kMemOrderRelease);
}

PeerRole RaftConsensus::GetActiveRole() const {
  auto lock = state_->LockForRead();
  return state_->GetActiveRoleUnlocked();
}

yb::OpId RaftConsensus::GetLatestOpIdFromLog() {
  return log_->GetLatestEntryOpId();
}

Status RaftConsensus::StartConsensusOnlyRoundUnlocked(const ReplicateMsgPtr& msg) {
  OperationType op_type = msg->op_type();
  if (!IsConsensusOnlyOperation(op_type)) {
    return STATUS_FORMAT(InvalidArgument,
                         "Expected a consensus-only op type, got $0: $1",
                         OperationType_Name(op_type),
                         *msg);
  }
  VLOG_WITH_PREFIX(1) << "Starting consensus round: "
                      << msg->id().ShortDebugString();
  scoped_refptr<ConsensusRound> round(new ConsensusRound(this, msg));
  std::shared_ptr<StateChangeContext> context = nullptr;

  // We are here for NO_OP or CHANGE_CONFIG_OP type ops. We need to set the change record for an
  // actual config change operation. The NO_OP does not update the config, as it is used for a new
  // leader election term change replicate message, which keeps the same config.
  if (IsChangeConfigOperation(op_type)) {
    context =
      std::make_shared<StateChangeContext>(StateChangeReason::FOLLOWER_CONFIG_CHANGE_COMPLETE,
                                           msg->change_config_record());
  } else {
    context = std::make_shared<StateChangeContext>(StateChangeReason::FOLLOWER_NO_OP_COMPLETE);
  }

  StdStatusCallback client_cb =
      std::bind(&RaftConsensus::MarkDirtyOnSuccess,
                this,
                context,
                &DoNothingStatusCB,
                std::placeholders::_1);
  round->SetCallback(MakeNonTrackedRoundCallback(round.get(), std::move(client_cb)));
  auto status = state_->AddPendingOperation(round, OperationMode::kFollower);
  if (!status.ok()) {
    round->NotifyReplicationFailed(status);
  }
  return status;
}

Status RaftConsensus::WaitForLeaderLeaseImprecise(CoarseTimePoint deadline) {
  CoarseTimePoint start = CoarseMonoClock::now();
  for (;;) {
    MonoDelta remaining_old_leader_lease;
    LeaderLeaseStatus leader_lease_status;
    ReplicaState::State state;
    {
      auto lock = state_->LockForRead();
      state = state_->state();
      if (state != ReplicaState::kRunning) {
        return STATUS_FORMAT(IllegalState, "Consensus is not running: $0", state);
      }
      if (state_->GetActiveRoleUnlocked() != PeerRole::LEADER) {
        return STATUS_FORMAT(IllegalState, "Not the leader: $0", state_->GetActiveRoleUnlocked());
      }
      leader_lease_status = state_->GetLeaderLeaseStatusUnlocked(&remaining_old_leader_lease);
    }
    if (leader_lease_status == LeaderLeaseStatus::HAS_LEASE) {
      return Status::OK();
    }
    CoarseTimePoint now = CoarseMonoClock::now();
    if (now > deadline) {
      return STATUS_FORMAT(
          TimedOut, "Waited for $0 to acquire a leader lease, state $1, lease status: $2",
          now - start, state, LeaderLeaseStatus_Name(leader_lease_status));
    }
    switch (leader_lease_status) {
      case LeaderLeaseStatus::HAS_LEASE:
        return Status::OK();
      case LeaderLeaseStatus::NO_MAJORITY_REPLICATED_LEASE:
        {
          std::unique_lock<decltype(leader_lease_wait_mtx_)> lock(leader_lease_wait_mtx_);
          // Because we're not taking the same lock (leader_lease_wait_mtx_) when we check the
          // leader lease status, there is a possibility of a race condition when we miss the
          // notification and by this point we already have a lease. Rather than re-taking the
          // ReplicaState lock and re-checking, here we simply block for up to 100ms in that case,
          // because this function is currently (08/14/2017) only used in a context when it is OK,
          // such as catalog manager initialization.
          leader_lease_wait_cond_.wait_for(
              lock, std::max<MonoDelta>(100ms, deadline - now).ToSteadyDuration());
        }
        continue;
      case LeaderLeaseStatus::OLD_LEADER_MAY_HAVE_LEASE: {
        auto wait_deadline = std::min({deadline, now + 100ms, now + remaining_old_leader_lease});
        std::this_thread::sleep_until(wait_deadline);
      } continue;
    }
    FATAL_INVALID_ENUM_VALUE(LeaderLeaseStatus, leader_lease_status);
  }
}

Status RaftConsensus::CheckIsActiveLeaderAndHasLease() const {
  return state_->CheckIsActiveLeaderAndHasLease();
}

Result<MicrosTime> RaftConsensus::MajorityReplicatedHtLeaseExpiration(
    MicrosTime min_allowed, CoarseTimePoint deadline) const {
  return state_->MajorityReplicatedHtLeaseExpiration(min_allowed, deadline);
}

std::string RaftConsensus::GetRequestVoteLogPrefix(const VoteRequestPB& request) const {
  return Format("$0 Leader $1election vote request",
                state_->LogPrefix(), request.preelection() ? "pre-" : "");
}

void RaftConsensus::FillVoteResponseVoteGranted(
    const VoteRequestPB& request, VoteResponsePB* response) {
  response->set_responder_term(request.candidate_term());
  response->set_vote_granted(true);
}

void RaftConsensus::FillVoteResponseVoteDenied(ConsensusErrorPB::Code error_code,
                                               VoteResponsePB* response) {
  response->set_responder_term(state_->GetCurrentTermUnlocked());
  response->set_vote_granted(false);
  response->mutable_consensus_error()->set_code(error_code);
}

void RaftConsensus::RequestVoteRespondVoteDenied(
    ConsensusErrorPB::Code error_code, const std::string& message_suffix,
    const VoteRequestPB& request, VoteResponsePB* response) {
  auto status = STATUS_FORMAT(
      InvalidArgument, "$0: Denying vote to candidate $1 $2",
      GetRequestVoteLogPrefix(request), request.candidate_uuid(), message_suffix);
  FillVoteResponseVoteDenied(error_code, response);
  LOG(INFO) << status.message().ToBuffer();
  StatusToPB(status, response->mutable_consensus_error()->mutable_status());
}

Status RaftConsensus::RequestVoteRespondInvalidTerm(const VoteRequestPB* request,
                                                    VoteResponsePB* response) {
  auto message_suffix = Format(
      "for earlier term $0. Current term is $1.",
      request->candidate_term(), state_->GetCurrentTermUnlocked());
  RequestVoteRespondVoteDenied(ConsensusErrorPB::INVALID_TERM, message_suffix, *request, response);
  return Status::OK();
}

Status RaftConsensus::RequestVoteRespondVoteAlreadyGranted(const VoteRequestPB* request,
                                                           VoteResponsePB* response) {
  FillVoteResponseVoteGranted(*request, response);
  LOG(INFO) << Substitute("$0: Already granted yes vote for candidate $1 in term $2. "
                          "Re-sending same reply.",
                          GetRequestVoteLogPrefix(*request),
                          request->candidate_uuid(),
                          request->candidate_term());
  return Status::OK();
}

Status RaftConsensus::RequestVoteRespondAlreadyVotedForOther(const VoteRequestPB* request,
                                                             VoteResponsePB* response) {
  auto message_suffix = Format(
      "in current term $0: Already voted for candidate $1 in this term.",
      state_->GetCurrentTermUnlocked(), state_->GetVotedForCurrentTermUnlocked());
  RequestVoteRespondVoteDenied(ConsensusErrorPB::ALREADY_VOTED, message_suffix, *request, response);
  return Status::OK();
}

Status RaftConsensus::RequestVoteRespondLastOpIdTooOld(const OpIdPB& local_last_logged_opid,
                                                       const VoteRequestPB* request,
                                                       VoteResponsePB* response) {
  auto message_suffix = Format(
      "for term $0 because replica has last-logged OpId of $1, which is greater than that of the "
          "candidate, which has last-logged OpId of $2.",
      request->candidate_term(), local_last_logged_opid,
      request->candidate_status().last_received());
  RequestVoteRespondVoteDenied(
      ConsensusErrorPB::LAST_OPID_TOO_OLD, message_suffix, *request, response);
  return Status::OK();
}

Status RaftConsensus::RequestVoteRespondLeaderIsAlive(const VoteRequestPB* request,
                                                      VoteResponsePB* response,
                                                      const std::string& leader_uuid) {
  FillVoteResponseVoteDenied(ConsensusErrorPB::LEADER_IS_ALIVE, response);
  std::string msg = Format(
      "$0: Denying vote to candidate $1 for term $2 because replica believes a valid leader to be "
      "alive (leader uuid: $3). Time left: $4",
      GetRequestVoteLogPrefix(*request), request->candidate_uuid(), request->candidate_term(),
      leader_uuid, withhold_votes_until_.load(std::memory_order_acquire) - MonoTime::Now());
  LOG(INFO) << msg;
  StatusToPB(STATUS(InvalidArgument, msg), response->mutable_consensus_error()->mutable_status());
  return Status::OK();
}

Status RaftConsensus::RequestVoteRespondIsBusy(const VoteRequestPB* request,
                                               VoteResponsePB* response) {
  FillVoteResponseVoteDenied(ConsensusErrorPB::CONSENSUS_BUSY, response);
  string msg = Substitute("$0: Denying vote to candidate $1 for term $2 because "
                          "replica is already servicing an update from a current leader "
                          "or another vote.",
                          GetRequestVoteLogPrefix(*request),
                          request->candidate_uuid(),
                          request->candidate_term());
  LOG(INFO) << msg;
  StatusToPB(STATUS(ServiceUnavailable, msg),
             response->mutable_consensus_error()->mutable_status());
  return Status::OK();
}

Status RaftConsensus::RequestVoteRespondVoteGranted(const VoteRequestPB* request,
                                                    VoteResponsePB* response) {
  // We know our vote will be "yes", so avoid triggering an election while we
  // persist our vote to disk. We use an exponential backoff to avoid too much
  // split-vote contention when nodes display high latencies.
  MonoDelta additional_backoff = LeaderElectionExpBackoffDeltaUnlocked();
  SnoozeFailureDetector(ALLOW_LOGGING, additional_backoff);

  // Persist our vote to disk.
  RETURN_NOT_OK(state_->SetVotedForCurrentTermUnlocked(request->candidate_uuid()));

  FillVoteResponseVoteGranted(*request, response);

  // Give peer time to become leader. Snooze one more time after persisting our
  // vote. When disk latency is high, this should help reduce churn.
  SnoozeFailureDetector(DO_NOT_LOG, additional_backoff);

  LOG(INFO) << Substitute("$0: Granting yes vote for candidate $1 in term $2.",
                          GetRequestVoteLogPrefix(*request),
                          request->candidate_uuid(),
                          state_->GetCurrentTermUnlocked());
  return Status::OK();
}

PeerRole RaftConsensus::GetRoleUnlocked() const {
  DCHECK(state_->IsLocked());
  return state_->GetActiveRoleUnlocked();
}

PeerRole RaftConsensus::role() const {
  auto lock = state_->LockForRead();
  return GetRoleUnlocked();
}

LeaderState RaftConsensus::GetLeaderState(bool allow_stale) const {
  return state_->GetLeaderState(allow_stale);
}

std::string RaftConsensus::LogPrefix() {
  return state_->LogPrefix();
}

void RaftConsensus::SetLeaderUuidUnlocked(const string& uuid) {
  failed_elections_since_stable_leader_.store(0, std::memory_order_release);
  state_->SetLeaderUuidUnlocked(uuid);
  auto context = std::make_shared<StateChangeContext>(StateChangeReason::NEW_LEADER_ELECTED, uuid);
  MarkDirty(context);
}

Status RaftConsensus::ReplicateConfigChangeUnlocked(const ReplicateMsgPtr& replicate_ref,
                                                    const RaftConfigPB& new_config,
                                                    ChangeConfigType type,
                                                    StdStatusCallback client_cb) {
  LOG_WITH_PREFIX(INFO) << "Setting replicate pending config " << new_config.ShortDebugString()
                        << ", type = " << ChangeConfigType_Name(type);

  // We will set pending config op id below once we have it.
  RETURN_NOT_OK(state_->SetPendingConfigUnlocked(new_config, OpId()));

  if (type == CHANGE_ROLE &&
      PREDICT_FALSE(FLAGS_TEST_inject_delay_leader_change_role_append_secs)) {
    LOG(INFO) << "Adding change role sleep for "
              << FLAGS_TEST_inject_delay_leader_change_role_append_secs << " secs.";
    SleepFor(MonoDelta::FromSeconds(FLAGS_TEST_inject_delay_leader_change_role_append_secs));
  }

  // Set as pending.
  RefreshConsensusQueueAndPeersUnlocked();

  auto round = make_scoped_refptr<ConsensusRound>(this, replicate_ref);
  round->SetCallback(MakeNonTrackedRoundCallback(round.get(), std::move(client_cb)));
  auto status = AppendNewRoundToQueueUnlocked(round);
  if (!status.ok()) {
    // We could just cancel pending config, because there could be only one pending config that
    // we've just set above and it corresponds to replicate_ref.
    auto clear_status = state_->ClearPendingConfigUnlocked();
    if (!clear_status.ok()) {
      LOG(WARNING) << "Could not clear pending config: " << clear_status;
    }
    return status;
  }

  RETURN_NOT_OK(state_->SetPendingConfigOpIdUnlocked(round->id()));

  return Status::OK();
}

void RaftConsensus::RefreshConsensusQueueAndPeersUnlocked() {
  DCHECK_EQ(PeerRole::LEADER, state_->GetActiveRoleUnlocked());
  const RaftConfigPB& active_config = state_->GetActiveConfigUnlocked();

  // Change the peers so that we're able to replicate messages remotely and
  // locally. Peer manager connections are updated using the active config. Connections to peers
  // that are not part of active_config are closed. New connections are created for those peers
  // that are present in active_config but have no connections. When the queue is in LEADER
  // mode, it checks that all registered peers are a part of the active config.
  peer_manager_->ClosePeersNotInConfig(active_config);
  queue_->SetLeaderMode(state_->GetCommittedOpIdUnlocked(),
                        state_->GetCurrentTermUnlocked(),
                        state_->GetLastAppliedOpIdUnlocked(),
                        active_config);

  ScopedDnsTracker dns_tracker(update_raft_config_dns_latency_.get());
  peer_manager_->UpdateRaftConfig(active_config);
}

const std::string& RaftConsensus::peer_uuid() const {
  return state_->GetPeerUuid();
}

const TabletId& RaftConsensus::tablet_id() const {
  return state_->GetOptions().tablet_id;
}

const TabletId& RaftConsensus::split_parent_tablet_id() const {
  return split_parent_tablet_id_;
}

const std::optional<CloneSourceInfo>& RaftConsensus::clone_source_info() const {
  return clone_source_info_;
}

LeaderLeaseStatus RaftConsensus::GetLeaderLeaseStatusIfLeader(MicrosTime* ht_lease_exp) const {
  auto lock = state_->LockForRead();
  LeaderLeaseStatus leader_lease_status = LeaderLeaseStatus::NO_MAJORITY_REPLICATED_LEASE;
  if (GetRoleUnlocked() == PeerRole::LEADER) {
    leader_lease_status = GetLeaderLeaseStatusUnlocked(ht_lease_exp);
  }
  return leader_lease_status;
}

LeaderLeaseStatus RaftConsensus::GetLeaderLeaseStatusUnlocked(MicrosTime* ht_lease_exp) const {
  auto leader_lease_status = state_->GetLeaderLeaseStatusUnlocked();
  if (leader_lease_status == LeaderLeaseStatus::HAS_LEASE && ht_lease_exp) {
    auto ht_lease = MajorityReplicatedHtLeaseExpiration(
        /* min_allowed = */ 0, /* deadline = */ CoarseTimePoint::max());
    if (!ht_lease.ok()) {
      LOG(WARNING) <<
          Format("Tablet $0 failed to get ht lease expiration $1", tablet_id(), ht_lease.status());
      return LeaderLeaseStatus::NO_MAJORITY_REPLICATED_LEASE;
    } else {
      *ht_lease_exp = *ht_lease;
    }
  }
  return leader_lease_status;
}

ConsensusStatePB RaftConsensus::ConsensusState(
    ConsensusConfigType type,
    LeaderLeaseStatus* leader_lease_status) const {
  auto lock = state_->LockForRead();
  return ConsensusStateUnlocked(type, leader_lease_status);
}

ConsensusStatePB RaftConsensus::ConsensusStateUnlocked(
    ConsensusConfigType type,
    LeaderLeaseStatus* leader_lease_status) const {
  CHECK(state_->IsLocked());
  if (leader_lease_status) {
    if (GetRoleUnlocked() == PeerRole::LEADER) {
      *leader_lease_status = GetLeaderLeaseStatusUnlocked(/* ht_lease_exp = */ nullptr);
    } else {
      // We'll still return a valid value if we're not a leader.
      *leader_lease_status = LeaderLeaseStatus::NO_MAJORITY_REPLICATED_LEASE;
    }
  }
  return state_->ConsensusStateUnlocked(type);
}

ConsensusStatePB RaftConsensus::GetConsensusStateFromCache() const {
  return state_->GetConsensusStateFromCache();
}

RaftConfigPB RaftConsensus::CommittedConfig() const {
  auto lock = state_->LockForRead();
  return state_->GetCommittedConfigUnlocked();
}

RaftConfigPB RaftConsensus::CommittedConfigUnlocked() const {
  return state_->GetCommittedConfigUnlocked();
}

void RaftConsensus::DumpStatusHtml(std::ostream& out) const {
  out << "<h1>Raft Consensus State</h1>" << std::endl;

  out << "<h2>State</h2>" << std::endl;
  out << "<pre>" << EscapeForHtmlToString(queue_->ToString()) << "</pre>" << std::endl;

  // Dump the queues on a leader.
  PeerRole role;
  std::string state_str;
  {
    auto lock = state_->LockForRead();
    role = state_->GetActiveRoleUnlocked();
    state_str = state_->ToStringUnlocked();
  }

  if (role == PeerRole::LEADER) {
    peer_manager_->DumpToHtml(out);
    out << "<hr/>" << std::endl;
    out << "<h2>Queue overview</h2>" << std::endl;
    out << "<pre>" << EscapeForHtmlToString(queue_->ToString()) << "</pre>" << std::endl;
    out << "<hr/>" << std::endl;
    out << "<h2>Queue details</h2>" << std::endl;
    queue_->DumpToHtml(out);
  } else if (role == PeerRole::FOLLOWER || role == PeerRole::READ_REPLICA) {
    out << "<h2>Replica State</h2>" << std::endl;
    out << "<ul>\n";
    out << "<li>State: { " << EscapeForHtmlToString(state_str) << " }</li>" << std::endl;
    const auto follower_last_update_received_time_ms =
        follower_last_update_received_time_ms_.load(std::memory_order_acquire);
    const auto last_update_received_time_lag = follower_last_update_received_time_ms > 0
        ? clock_->Now().GetPhysicalValueMillis() - follower_last_update_received_time_ms
        : 0;
    out << "<li>Last update received time: "
        << last_update_received_time_lag << "ms ago</li>" << std::endl;
    out << "</ul>\n";

    out << "<hr/>" << std::endl;
    out << "<h2>Raft Config</h2>" << std::endl;
    RaftConfigPB config = CommittedConfig();
    out << "<ul>\n";
    for (const RaftPeerPB& peer : config.peers()) {
      out << "<li>Peer:\n<ul>\n";
      out << Format("  <li>Host: $0</li>\n", peer.last_known_private_addr()[0].host());
      out << Format("  <li>UUID: $0</li>\n", peer.permanent_uuid());
      out << "</ul>\n";
    }
    out << "</ul>\n";
  }
}

ReplicaState* RaftConsensus::GetReplicaStateForTests() {
  return state_.get();
}

void RaftConsensus::ElectionCallback(const LeaderElectionData& data,
                                     const ElectionResult& result) {
  // The election callback runs on a reactor thread, so we need to defer to our
  // threadpool. If the threadpool is already shut down for some reason, it's OK --
  // we're OK with the callback never running.
  WARN_NOT_OK(raft_pool_concurrent_token_->SubmitFunc(
              std::bind(&RaftConsensus::DoElectionCallback, shared_from_this(), data, result)),
              state_->LogPrefix() + "Unable to run election callback");
}

void RaftConsensus::NotifyOriginatorAboutLostElection(const std::string& originator_uuid) {
  if (originator_uuid.empty()) {
    return;
  }

  ReplicaState::UniqueLock lock;
  Status s = state_->LockForConfigChange(&lock);
  if (PREDICT_FALSE(!s.ok())) {
    LOG_WITH_PREFIX(INFO) << "Unable to notify originator about lost election, lock failed: "
                          << s.ToString();
    return;
  }

  const auto& active_config = state_->GetActiveConfigUnlocked();
  const auto * peer = FindPeer(active_config, originator_uuid);
  if (!peer) {
    LOG_WITH_PREFIX(WARNING) << "Failed to find originators peer: " << originator_uuid
                             << ", config: " << active_config.ShortDebugString();
    return;
  }

  auto proxy = peer_proxy_factory_->NewProxy(*peer);
  LeaderElectionLostRequestPB req;
  req.set_dest_uuid(originator_uuid);
  req.set_election_lost_by_uuid(state_->GetPeerUuid());
  req.set_tablet_id(state_->GetOptions().tablet_id);
  auto resp = std::make_shared<LeaderElectionLostResponsePB>();
  auto rpc = std::make_shared<rpc::RpcController>();
  rpc->set_invoke_callback_mode(rpc::InvokeCallbackMode::kThreadPoolHigh);
  auto log_prefix = state_->LogPrefix();
  proxy->LeaderElectionLostAsync(&req, resp.get(), rpc.get(), [log_prefix, resp, rpc] {
    if (!rpc->status().ok()) {
      LOG(WARNING) << log_prefix << "Notify about lost election RPC failure: "
                   << rpc->status().ToString();
    } else if (resp->has_error()) {
      LOG(WARNING) << log_prefix << "Notify about lost election failed: "
                   << StatusFromPB(resp->error().status()).ToString();
    }
  });
}

void RaftConsensus::DoElectionCallback(const LeaderElectionData& data,
                                       const ElectionResult& result) {
  const char* election_name = result.preelection ? "Pre-election" : "election";
  const char* decision_name = result.decision == ElectionVote::kGranted ? "won" : "lost";
  // Snooze to avoid the election timer firing again as much as possible.
  {
    auto lock = state_->LockForRead();
    // We need to snooze when we win and when we lose:
    // - When we win because we're about to disable the timer and become leader.
    // - When we loose or otherwise we can fall into a cycle, where everyone keeps
    //   triggering elections but no election ever completes because by the time they
    //   finish another one is triggered already.
    // We ignore the status as we don't want to fail if we the timer is
    // disabled.
    SnoozeFailureDetector(ALLOW_LOGGING, LeaderElectionExpBackoffDeltaUnlocked());

    if (!result.preelections_not_supported_by_uuid.empty()) {
      disable_pre_elections_until_ =
          CoarseMonoClock::now() + FLAGS_temporary_disable_preelections_timeout_ms * 1ms;
      LOG_WITH_PREFIX(WARNING)
          << "Disable pre-elections until " << ToString(disable_pre_elections_until_)
          << ", because " << result.preelections_not_supported_by_uuid << " does not support them.";
    }
  }
  if (result.decision == ElectionVote::kDenied) {
    failed_elections_since_stable_leader_.fetch_add(1, std::memory_order_acq_rel);
    LOG_WITH_PREFIX(INFO) << "Leader " << election_name << " lost for term "
                          << result.election_term << ". Reason: "
                          << (!result.message.empty() ? result.message : "None given")
                          << ". Originator: " << data.originator_uuid;
    NotifyOriginatorAboutLostElection(data.originator_uuid);

    if (result.higher_term) {
      ReplicaState::UniqueLock lock;
      Status s = state_->LockForConfigChange(&lock);
      if (s.ok()) {
        s = HandleTermAdvanceUnlocked(*result.higher_term);
      }
      if (!s.ok()) {
        LOG_WITH_PREFIX(INFO) << "Unable to advance term as " << election_name << " result: " << s;
      }
    }

    return;
  }

  ReplicaState::UniqueLock lock;
  Status s = state_->LockForConfigChange(&lock);
  if (PREDICT_FALSE(!s.ok())) {
    LOG_WITH_PREFIX(INFO) << "Received " << election_name << " callback for term "
                          << result.election_term << " while not running: "
                          << s.ToString();
    return;
  }

  auto desired_term = state_->GetCurrentTermUnlocked() + (result.preelection ? 1 : 0);
  if (result.election_term != desired_term) {
    LOG_WITH_PREFIX(INFO)
        << "Leader " << election_name << " decision for defunct term "
        << result.election_term << ": " << decision_name;
    return;
  }

  const RaftConfigPB& active_config = state_->GetActiveConfigUnlocked();
  if (!IsRaftConfigVoter(state_->GetPeerUuid(), active_config)) {
    LOG_WITH_PREFIX(WARNING)
        << "Leader " << election_name << " decision while not in active config. "
        << "Result: Term " << result.election_term << ": " << decision_name
        << ". RaftConfig: " << active_config.ShortDebugString();
    return;
  }

  if (result.preelection) {
    LOG_WITH_PREFIX(INFO) << "Leader pre-election won for term " << result.election_term;
    lock.unlock();
    WARN_NOT_OK(DoStartElection(data, PreElected::kTrue), "Start election failed: ");
    return;
  }

  if (state_->GetActiveRoleUnlocked() == PeerRole::LEADER) {
    LOG_WITH_PREFIX(DFATAL)
        << "Leader " << election_name << " callback while already leader! Result: Term "
        << result.election_term << ": "
        << decision_name;
    return;
  }

  LOG_WITH_PREFIX(INFO) << "Leader " << election_name << " won for term " << result.election_term;

  // Apply lease updates that were possible received from voters.
  state_->UpdateOldLeaderLeaseExpirationOnNonLeaderUnlocked(
      result.old_leader_lease, result.old_leader_ht_lease);

  state_->SetLeaderNoOpCommittedUnlocked(false);
  // Convert role to LEADER.
  SetLeaderUuidUnlocked(state_->GetPeerUuid());

  // TODO: BecomeLeaderUnlocked() can fail due to state checks during shutdown.
  // It races with the above state check.
  // This could be a problem during tablet deletion.
  auto status = BecomeLeaderUnlocked();
  if (!status.ok()) {
    LOG_WITH_PREFIX(DFATAL) << "Failed to become leader: " << status.ToString();
  }
}

yb::OpId RaftConsensus::GetLastReceivedOpId() {
  auto lock = state_->LockForRead();
  return state_->GetLastReceivedOpIdUnlocked();
}

yb::OpId RaftConsensus::GetLastCommittedOpId() {
  auto lock = state_->LockForRead();
  return state_->GetCommittedOpIdUnlocked();
}

yb::OpId RaftConsensus::GetLastAppliedOpId() {
  auto lock = state_->LockForRead();
  return state_->GetLastAppliedOpIdUnlocked();
}

yb::OpId RaftConsensus::GetAllAppliedOpId() {
  return queue_->GetAllAppliedOpId();
}

void RaftConsensus::MarkDirty(std::shared_ptr<StateChangeContext> context) {
  LOG_WITH_PREFIX(INFO) << "Calling mark dirty synchronously for reason code " << context->reason;
  mark_dirty_clbk_.Run(context);
}

void RaftConsensus::MarkDirtyOnSuccess(std::shared_ptr<StateChangeContext> context,
                                       const StdStatusCallback& client_cb,
                                       const Status& status) {
  if (PREDICT_TRUE(status.ok())) {
    MarkDirty(context);
  }
  client_cb(status);
}

void RaftConsensus::NonTrackedRoundReplicationFinished(ConsensusRound* round,
                                                       const StdStatusCallback& client_cb,
                                                       const Status& status) {
  DCHECK(state_->IsLocked());
  OperationType op_type = round->replicate_msg()->op_type();
  string op_str = Format("$0 [$1]", OperationType_Name(op_type), round->id());
  if (!IsConsensusOnlyOperation(op_type)) {
    LOG_WITH_PREFIX(ERROR) << "Unexpected op type: " << op_str;
    return;
  }
  if (!status.ok()) {
    // TODO: Do something with the status on failure?
    LOG_WITH_PREFIX(INFO) << op_str << " replication failed: " << status << "\n" << GetStackTrace();

    // Clear out the pending state (ENG-590).
    if (IsChangeConfigOperation(op_type) && state_->GetPendingConfigOpIdUnlocked() == round->id()) {
      WARN_NOT_OK(state_->ClearPendingConfigUnlocked(), "Could not clear pending state");
    }
  } else if (IsChangeConfigOperation(op_type)) {
    // Notify the TabletPeer owner object.
    state_->context()->ChangeConfigReplicated(state_->GetCommittedConfigUnlocked());
  }

  client_cb(status);
}

void RaftConsensus::EnableFailureDetector(MonoDelta delta) {
  if (PREDICT_TRUE(FLAGS_enable_leader_failure_detection)) {
    failure_detector_->Start(delta);
  }
}

void RaftConsensus::DisableFailureDetector() {
  if (PREDICT_TRUE(FLAGS_enable_leader_failure_detection)) {
    failure_detector_->Stop();
  }
}

void RaftConsensus::SnoozeFailureDetector(AllowLogging allow_logging, MonoDelta delta) {
  if (PREDICT_TRUE(GetAtomicFlag(&FLAGS_enable_leader_failure_detection))) {
    if (allow_logging == ALLOW_LOGGING) {
      LOG_WITH_PREFIX(INFO) << Format("Snoozing leader timeout detection for $0",
                                      delta.Initialized() ? delta.ToString() : "election timeout");
    }

    if (!delta.Initialized()) {
      delta = MinimumElectionTimeout();
    }
    failure_detector_->Snooze(delta);
  }
}

MonoDelta RaftConsensus::MinimumElectionTimeout() const {
  int32_t failure_timeout = FLAGS_leader_failure_max_missed_heartbeat_periods *
      FLAGS_raft_heartbeat_interval_ms;

  return MonoDelta::FromMilliseconds(failure_timeout);
}

MonoDelta RaftConsensus::LeaderElectionExpBackoffDeltaUnlocked() {
  // Compute a backoff factor based on how many leader elections have
  // taken place since a stable leader was last seen.
  double backoff_factor = pow(
      1.1,
      failed_elections_since_stable_leader_.load(std::memory_order_acquire) + 1);
  double min_timeout = MinimumElectionTimeout().ToMilliseconds();
  double max_timeout = std::min<double>(
      min_timeout * backoff_factor,
      FLAGS_leader_failure_exp_backoff_max_delta_ms);
  if (max_timeout < min_timeout) {
    LOG(INFO) << "Resetting max_timeout from " <<  max_timeout << " to " << min_timeout
              << ", max_delta_flag=" << FLAGS_leader_failure_exp_backoff_max_delta_ms;
    max_timeout = min_timeout;
  }
  // Randomize the timeout between the minimum and the calculated value.
  // We do this after the above capping to the max. Otherwise, after a
  // churny period, we'd end up highly likely to backoff exactly the max
  // amount.
  double timeout = min_timeout + (max_timeout - min_timeout) * rng_.NextDoubleFraction();
  DCHECK_GE(timeout, min_timeout);

  return MonoDelta::FromMilliseconds(timeout);
}

Status RaftConsensus::IncrementTermUnlocked() {
  return HandleTermAdvanceUnlocked(state_->GetCurrentTermUnlocked() + 1);
}

Status RaftConsensus::HandleTermAdvanceUnlocked(ConsensusTerm new_term) {
  if (new_term <= state_->GetCurrentTermUnlocked()) {
    return STATUS(IllegalState, Substitute("Can't advance term to: $0 current term: $1 is higher.",
                                           new_term, state_->GetCurrentTermUnlocked()));
  }

  if (state_->GetActiveRoleUnlocked() == PeerRole::LEADER) {
    LOG_WITH_PREFIX(INFO) << "Stepping down as leader of term "
                          << state_->GetCurrentTermUnlocked()
                          << " since new term is " << new_term;

    RETURN_NOT_OK(BecomeReplicaUnlocked(std::string()));
  }

  LOG_WITH_PREFIX(INFO) << "Advancing to term " << new_term;
  RETURN_NOT_OK(state_->SetCurrentTermUnlocked(new_term));
  term_metric_->set_value(new_term);
  return Status::OK();
}

Result<ReadOpsResult> RaftConsensus::ReadReplicatedMessagesForCDC(
    const yb::OpId& from, int64_t* last_replicated_opid_index, const CoarseTimePoint deadline,
    const bool fetch_single_entry) {
  return queue_->ReadReplicatedMessagesForCDC(
      from, last_replicated_opid_index, deadline, fetch_single_entry);
}

Result<ReadOpsResult> RaftConsensus::ReadReplicatedMessagesForConsistentCDC(
    OpId from, uint64_t stream_safe_time, CoarseTimePoint deadline, bool fetch_single_entry,
    int64_t* last_replicated_opid_index) {
  return queue_->ReadReplicatedMessagesForConsistentCDC(
      from, stream_safe_time, deadline, fetch_single_entry, last_replicated_opid_index);
}

Result<ReadOpsResult> RaftConsensus::ReadReplicatedMessagesInSegmentForCDC(
    const OpId& from_op_id, CoarseTimePoint deadline, bool fetch_single_entry,
    int64_t* last_committed_index, HybridTime* consistent_stream_safe_time_footer,
    bool* read_entire_wal) {
  return queue_->ReadReplicatedMessagesInSegmentForCDC(
      from_op_id, deadline, fetch_single_entry, last_committed_index,
      consistent_stream_safe_time_footer, read_entire_wal);
}

void RaftConsensus::UpdateCDCConsumerOpId(const yb::OpId& op_id) {
  return queue_->UpdateCDCConsumerOpId(op_id);
}

void RaftConsensus::RollbackIdAndDeleteOpId(const ReplicateMsgPtr& replicate_msg,
                                            bool should_exists) {
  state_->CancelPendingOperation(OpId::FromPB(replicate_msg->id()), should_exists);
}

uint64_t RaftConsensus::OnDiskSize() const {
  return state_->OnDiskSize();
}

yb::OpId RaftConsensus::WaitForSafeOpIdToApply(const yb::OpId& op_id) {
  return log_->WaitForSafeOpIdToApply(op_id);
}

yb::OpId RaftConsensus::MinRetryableRequestOpId() {
  return state_->MinRetryableRequestOpId();
}

size_t RaftConsensus::LogCacheSize() {
  return queue_->LogCacheSize();
}

size_t RaftConsensus::EvictLogCache(size_t bytes_to_evict) {
  return queue_->EvictLogCache(bytes_to_evict);
}

Result<RetryableRequests> RaftConsensus::GetRetryableRequests() const {
  auto lock = state_->LockForRead();
  if(state_->state() != ReplicaState::kRunning) {
    return STATUS_FORMAT(IllegalState, "Replica is in $0 state", state_->state());
  }
  return state_->retryable_requests();
}

Result<std::unique_ptr<RetryableRequests>> RaftConsensus::TakeSnapshotOfRetryableRequests() {
  return state_->TakeSnapshotOfRetryableRequests();
}

OpId RaftConsensus::GetLastFlushedOpIdInRetryableRequests() {
  return state_->GetLastFlushedOpIdInRetryableRequests();
}

Status RaftConsensus::SetLastFlushedOpIdInRetryableRequests(const OpId& op_id) {
  return state_->SetLastFlushedOpIdInRetryableRequests(op_id);
}

int64_t RaftConsensus::follower_lag_ms() const {
  return follower_last_update_time_ms_metric_->lag_ms();
}

RetryableRequestsCounts RaftConsensus::TEST_CountRetryableRequests() {
  return state_->TEST_CountRetryableRequests();
}

int RaftConsensus::TEST_RetryableRequestTimeoutSecs() const {
  auto lock = state_->LockForRead();
  if(state_->state() != ReplicaState::kRunning) {
    return 0;
  }
  return state_->retryable_requests().request_timeout_secs();
}

void RaftConsensus::TrackOperationMemory(const yb::OpId& op_id) {
  queue_->TrackOperationsMemory({op_id});
}

int64_t RaftConsensus::TEST_LeaderTerm() const {
  auto lock = state_->LockForRead();
  return state_->GetCurrentTermUnlocked();
}

std::string RaftConsensus::DelayedStepDown::ToString() const {
  return YB_STRUCT_TO_STRING(term, protege, graceful);
}

Result<OpId> RaftConsensus::TEST_GetLastOpIdWithType(OpIdType opid_type, OperationType op_type) {
  return queue_->TEST_GetLastOpIdWithType(VERIFY_RESULT(GetLastOpId(opid_type)).index, op_type);
}

}  // namespace consensus
}  // namespace yb
