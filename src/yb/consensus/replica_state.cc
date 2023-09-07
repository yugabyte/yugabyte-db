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

#include "yb/consensus/replica_state.h"

#include <gflags/gflags.h>

#include "yb/consensus/consensus.h"
#include "yb/consensus/consensus_context.h"
#include "yb/consensus/consensus_round.h"
#include "yb/consensus/log_util.h"
#include "yb/consensus/quorum_util.h"

#include "yb/gutil/strings/substitute.h"

#include "yb/util/atomic.h"
#include "yb/util/debug/trace_event.h"
#include "yb/util/enums.h"
#include "yb/util/flag_tags.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/opid.h"
#include "yb/util/result.h"
#include "yb/util/status.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/thread_restrictions.h"
#include "yb/util/tostring.h"
#include "yb/util/trace.h"

using namespace std::literals;

DEFINE_int32(inject_delay_commit_pre_voter_to_voter_secs, 0,
             "Amount of time to delay commit of a PRE_VOTER to VOTER transition. To be used for "
             "unit testing purposes only.");
TAG_FLAG(inject_delay_commit_pre_voter_to_voter_secs, unsafe);
TAG_FLAG(inject_delay_commit_pre_voter_to_voter_secs, hidden);

namespace yb {
namespace consensus {

using std::string;
using strings::Substitute;
using strings::SubstituteAndAppend;

//////////////////////////////////////////////////
// ReplicaState
//////////////////////////////////////////////////

ReplicaState::ReplicaState(
    ConsensusOptions options, string peer_uuid, std::unique_ptr<ConsensusMetadata> cmeta,
    ConsensusContext* consensus_context, SafeOpIdWaiter* safe_op_id_waiter,
    RetryableRequests* retryable_requests,
    std::function<void(const OpIds&)> applied_ops_tracker)
    : options_(std::move(options)),
      peer_uuid_(std::move(peer_uuid)),
      cmeta_(std::move(cmeta)),
      context_(consensus_context),
      safe_op_id_waiter_(safe_op_id_waiter),
      applied_ops_tracker_(std::move(applied_ops_tracker)) {
  CHECK(cmeta_) << "ConsensusMeta passed as NULL";
  if (retryable_requests) {
    retryable_requests_ = std::move(*retryable_requests);
  }

  CHECK(IsAcceptableAtomicImpl(leader_state_cache_));

  // Actually we don't need this lock, but GetActiveRoleUnlocked checks that we are holding the
  // lock.
  auto lock = LockForRead();
  CoarseTimePoint now;
  RefreshLeaderStateCacheUnlocked(&now);
}

ReplicaState::~ReplicaState() {
}

Status ReplicaState::StartUnlocked(const OpIdPB& last_id_in_wal) {
  DCHECK(IsLocked());

  // Our last persisted term can be higher than the last persisted operation
  // (i.e. if we called an election) but reverse should never happen.
  CHECK_LE(last_id_in_wal.term(), GetCurrentTermUnlocked()) << LogPrefix()
      << "The last op in the WAL with id " << OpIdToString(last_id_in_wal)
      << " has a term (" << last_id_in_wal.term() << ") that is greater "
      << "than the latest recorded term, which is " << GetCurrentTermUnlocked();

  next_index_ = last_id_in_wal.index() + 1;

  last_received_op_id_ = yb::OpId::FromPB(last_id_in_wal);

  state_ = kRunning;
  return Status::OK();
}

Status ReplicaState::LockForStart(UniqueLock* lock) const {
  ThreadRestrictions::AssertWaitAllowed();
  UniqueLock l(update_lock_);
  CHECK_EQ(state_, kInitialized) << "Illegal state for Start()."
      << " Replica is not in kInitialized state";
  lock->swap(l);
  return Status::OK();
}

bool ReplicaState::IsLocked() const {
  std::unique_lock<std::mutex> lock(update_lock_, std::try_to_lock);
  return !lock.owns_lock();
}

ReplicaState::UniqueLock ReplicaState::LockForRead() const {
  ThreadRestrictions::AssertWaitAllowed();
  return UniqueLock(update_lock_);
}

Status ReplicaState::LockForReplicate(UniqueLock* lock, const ReplicateMsg& msg) const {
  DCHECK(!msg.has_id()) << "Should not have an ID yet: " << msg.ShortDebugString();
  CHECK(msg.has_op_type());  // TODO: better checking?
  return LockForReplicate(lock);
}

Status ReplicaState::LockForReplicate(UniqueLock* lock) const {
  ThreadRestrictions::AssertWaitAllowed();
  UniqueLock l(update_lock_);
  if (PREDICT_FALSE(state_ != kRunning)) {
    return STATUS(IllegalState, "Replica not in running state");
  }

  lock->swap(l);
  return Status::OK();
}

Status ReplicaState::CheckIsActiveLeaderAndHasLease() const {
  UniqueLock l(update_lock_);
  if (PREDICT_FALSE(state_ != kRunning)) {
    return STATUS(IllegalState, "Replica not in running state");
  }
  return CheckActiveLeaderUnlocked(LeaderLeaseCheckMode::NEED_LEASE);
}

Status ReplicaState::LockForMajorityReplicatedIndexUpdate(UniqueLock* lock) const {
  TRACE_EVENT0("consensus", "ReplicaState::LockForMajorityReplicatedIndexUpdate");
  ThreadRestrictions::AssertWaitAllowed();
  UniqueLock l(update_lock_);

  if (PREDICT_FALSE(state_ != kRunning)) {
    return STATUS(IllegalState, "Replica not in running state");
  }

  if (PREDICT_FALSE(GetActiveRoleUnlocked() != PeerRole::LEADER)) {
    return STATUS(IllegalState, "Replica not LEADER");
  }
  lock->swap(l);
  return Status::OK();
}

LeaderState ReplicaState::GetLeaderState(bool allow_stale) const {
  auto cache = leader_state_cache_.load(boost::memory_order_acquire);

  if (!allow_stale) {
    CoarseTimePoint now = CoarseMonoClock::Now();
    if (now >= cache.expire_at) {
      auto lock = LockForRead();
      return RefreshLeaderStateCacheUnlocked(&now);
    }
  }

  LeaderState result = {cache.status()};
  if (result.status == LeaderStatus::LEADER_AND_READY) {
    result.term = cache.extra_value();
  } else {
    if (result.status == LeaderStatus::LEADER_BUT_OLD_LEADER_MAY_HAVE_LEASE) {
      result.remaining_old_leader_lease = MonoDelta::FromMicroseconds(cache.extra_value());
    }
    result.MakeNotReadyLeader(result.status);
  }

  return result;
}

LeaderState ReplicaState::GetLeaderStateUnlocked(
    LeaderLeaseCheckMode lease_check_mode, CoarseTimePoint* now) const {
  LeaderState result;

  if (GetActiveRoleUnlocked() != PeerRole::LEADER) {
    return result.MakeNotReadyLeader(LeaderStatus::NOT_LEADER);
  }

  if (!leader_no_op_committed_) {
    // This will cause the client to retry on the same server (won't try to find the new leader).
    return result.MakeNotReadyLeader(LeaderStatus::LEADER_BUT_NO_OP_NOT_COMMITTED);
  }

  const auto lease_status = lease_check_mode != LeaderLeaseCheckMode::DONT_NEED_LEASE
      ? GetLeaderLeaseStatusUnlocked(&result.remaining_old_leader_lease, now)
      : LeaderLeaseStatus::HAS_LEASE;
  switch (lease_status) {
    case LeaderLeaseStatus::OLD_LEADER_MAY_HAVE_LEASE:
      // Will retry on the same server.
      VLOG(1) << "Old leader lease might still be active for "
              << result.remaining_old_leader_lease.ToString();
      return result.MakeNotReadyLeader(
          LeaderStatus::LEADER_BUT_OLD_LEADER_MAY_HAVE_LEASE);

    case LeaderLeaseStatus::NO_MAJORITY_REPLICATED_LEASE:
      // Will retry to look up the leader, because it might have changed.
      return result.MakeNotReadyLeader(
          LeaderStatus::LEADER_BUT_NO_MAJORITY_REPLICATED_LEASE);

    case LeaderLeaseStatus::HAS_LEASE:
      result.status = LeaderStatus::LEADER_AND_READY;
      result.term = GetCurrentTermUnlocked();
      return result;
  }

  FATAL_INVALID_ENUM_VALUE(LeaderLeaseStatus, lease_status);
}

Status ReplicaState::CheckActiveLeaderUnlocked(LeaderLeaseCheckMode lease_check_mode) const {
  auto state = GetLeaderStateUnlocked(lease_check_mode);
  if (state.status == LeaderStatus::NOT_LEADER) {
    ConsensusStatePB cstate = ConsensusStateUnlocked(CONSENSUS_CONFIG_ACTIVE);
    return STATUS_FORMAT(IllegalState,
                         "Replica $0 is not leader of this config. Role: $1. Consensus state: $2",
                         peer_uuid_, PeerRole_Name(GetActiveRoleUnlocked()), cstate);
  }

  return state.CreateStatus();
}

Status ReplicaState::LockForConfigChange(UniqueLock* lock) const {
  TRACE_EVENT0("consensus", "ReplicaState::LockForConfigChange");

  ThreadRestrictions::AssertWaitAllowed();
  UniqueLock l(update_lock_);
  // Can only change the config on running replicas.
  if (PREDICT_FALSE(state_ != kRunning)) {
    return STATUS(IllegalState, "Unable to lock ReplicaState for config change",
                                Substitute("State = $0", state_));
  }
  lock->swap(l);
  return Status::OK();
}

Status ReplicaState::LockForUpdate(UniqueLock* lock) const {
  TRACE_EVENT0("consensus", "ReplicaState::LockForUpdate");
  ThreadRestrictions::AssertWaitAllowed();
  UniqueLock l(update_lock_);
  if (PREDICT_FALSE(state_ != kRunning)) {
    return STATUS(IllegalState, "Replica not in running state");
  }
  lock->swap(l);
  return Status::OK();
}

Status ReplicaState::LockForShutdown(UniqueLock* lock) {
  TRACE_EVENT0("consensus", "ReplicaState::LockForShutdown");
  ThreadRestrictions::AssertWaitAllowed();
  UniqueLock l(update_lock_);
  if (state_ != kShuttingDown && state_ != kShutDown) {
    state_ = kShuttingDown;
  }
  lock->swap(l);
  return Status::OK();
}

Status ReplicaState::ShutdownUnlocked() {
  DCHECK(IsLocked());
  CHECK_EQ(state_, kShuttingDown);
  state_ = kShutDown;
  return Status::OK();
}

ConsensusStatePB ReplicaState::ConsensusStateUnlocked(ConsensusConfigType type) const {
  return cmeta_->ToConsensusStatePB(type);
}

PeerRole ReplicaState::GetActiveRoleUnlocked() const {
  DCHECK(IsLocked());
  return cmeta_->active_role();
}

bool ReplicaState::IsConfigChangePendingUnlocked() const {
  DCHECK(IsLocked());
  return cmeta_->has_pending_config();
}

Status ReplicaState::CheckNoConfigChangePendingUnlocked() const {
  DCHECK(IsLocked());
  if (IsConfigChangePendingUnlocked()) {
    return STATUS(IllegalState,
        Substitute("RaftConfig change currently pending. Only one is allowed at a time.\n"
                   "  Committed config: $0.\n  Pending config: $1",
                   GetCommittedConfigUnlocked().ShortDebugString(),
                   GetPendingConfigUnlocked().ShortDebugString()));
  }
  return Status::OK();
}

Status ReplicaState::SetPendingConfigUnlocked(
    const RaftConfigPB& new_config, const OpId& config_op_id) {
  DCHECK(IsLocked());
  RETURN_NOT_OK_PREPEND(VerifyRaftConfig(new_config, UNCOMMITTED_QUORUM),
                        "Invalid config to set as pending");
  if (!new_config.unsafe_config_change()) {
    CHECK(!cmeta_->has_pending_config())
        << "Attempt to set pending config while another is already pending! "
        << "Existing pending config: " << cmeta_->pending_config().ShortDebugString() << "; "
        << "Attempted new pending config: " << new_config.ShortDebugString();
  } else if (cmeta_->has_pending_config()) {
    LOG_WITH_PREFIX(INFO) << "Allowing unsafe config change even though there is a pending config! "
                          << "Existing pending config: "
                          << cmeta_->pending_config().ShortDebugString() << "; "
                          << "New pending config: " << new_config.ShortDebugString();
  }
  cmeta_->set_pending_config(new_config, config_op_id);
  CoarseTimePoint now;
  RefreshLeaderStateCacheUnlocked(&now);
  return Status::OK();
}

Status ReplicaState::SetPendingConfigOpIdUnlocked(const OpId& config_op_id) {
  DCHECK(IsLocked());
  return cmeta_->set_pending_config_op_id(config_op_id);
}

Status ReplicaState::ClearPendingConfigUnlocked() {
  DCHECK(IsLocked());
  if (!cmeta_->has_pending_config()) {
    LOG(WARNING) << "Attempt to clear a non-existent pending config."
                 << "Existing committed config: " << cmeta_->committed_config().ShortDebugString();
    return STATUS(IllegalState, "Attempt to clear a non-existent pending config.");
  }
  cmeta_->clear_pending_config();
  CoarseTimePoint now;
  RefreshLeaderStateCacheUnlocked(&now);
  return Status::OK();
}

const RaftConfigPB& ReplicaState::GetPendingConfigUnlocked() const {
  DCHECK(IsLocked());
  CHECK(IsConfigChangePendingUnlocked()) << "No pending config";
  return cmeta_->pending_config();
}

Status ReplicaState::SetCommittedConfigUnlocked(const RaftConfigPB& config_to_commit) {
  TRACE_EVENT0("consensus", "ReplicaState::SetCommittedConfigUnlocked");
  DCHECK(IsLocked());
  DCHECK(config_to_commit.IsInitialized());
  RETURN_NOT_OK_PREPEND(
      VerifyRaftConfig(config_to_commit, COMMITTED_QUORUM), "Invalid config to set as committed");

  // Only allow to have no pending config here when we want to do unsafe config change, see
  // https://github.com/yugabyte/yugabyte-db/commit/9305a202b59eb805f80492c1b7412b0fbfd1ce76.
  DCHECK(cmeta_->has_pending_config() || config_to_commit.unsafe_config_change());
  // Compare committed with pending configuration, ensure they are the same.
  // In the event of an unsafe config change triggered by an administrator,
  // it is possible that the config being committed may not match the pending config
  // because unsafe config change allows multiple pending configs to exist.
  // Therefore we only need to validate that 'config_to_commit' matches the pending config
  // if the pending config does not have its 'unsafe_config_change' flag set.
  if (!config_to_commit.unsafe_config_change()) {
    const RaftConfigPB& pending_config = GetPendingConfigUnlocked();
    // Pending will not have an opid_index, so ignore that field.
    RaftConfigPB config_no_opid = config_to_commit;
    config_no_opid.clear_opid_index();
    // Quorums must be exactly equal, even w.r.t. peer ordering.
    CHECK_EQ(GetPendingConfigUnlocked().SerializeAsString(), config_no_opid.SerializeAsString())
        << Substitute(
               "New committed config must equal pending config, but does not. "
               "Pending config: $0, committed config: $1",
               pending_config.ShortDebugString(), config_to_commit.ShortDebugString());
  }
  cmeta_->set_committed_config(config_to_commit);
  cmeta_->clear_pending_config();
  CoarseTimePoint now;
  RefreshLeaderStateCacheUnlocked(&now);
  CHECK_OK(cmeta_->Flush());
  return Status::OK();
}

const RaftConfigPB& ReplicaState::GetCommittedConfigUnlocked() const {
  DCHECK(IsLocked());
  return cmeta_->committed_config();
}

const RaftConfigPB& ReplicaState::GetActiveConfigUnlocked() const {
  DCHECK(IsLocked());
  return cmeta_->active_config();
}

bool ReplicaState::IsOpCommittedOrPending(const OpId& op_id, bool* term_mismatch) {
  DCHECK(IsLocked());

  *term_mismatch = false;

  if (cmeta_ == nullptr) {
    LOG(FATAL) << "cmeta_ cannot be NULL";
  }

  int64_t committed_index = GetCommittedOpIdUnlocked().index;
  if (op_id.index <= committed_index) {
    return true;
  }

  int64_t last_received_index = GetLastReceivedOpIdUnlocked().index;
  if (op_id.index > last_received_index) {
    return false;
  }

  scoped_refptr<ConsensusRound> round = GetPendingOpByIndexOrNullUnlocked(op_id.index);
  if (round == nullptr) {
    LOG_WITH_PREFIX(ERROR)
        << "Consensus round not found for op id " << op_id << ": "
        << "committed_index=" << committed_index << ", "
        << "last_received_index=" << last_received_index << ", "
        << "tablet: " << options_.tablet_id << ", current state: "
        << ToStringUnlocked();
    DumpPendingOperationsUnlocked();
    CHECK(false);
  }

  if (round->id().term != op_id.term) {
    *term_mismatch = true;
    return false;
  }
  return true;
}

Status ReplicaState::SetCurrentTermUnlocked(int64_t new_term) {
  TRACE_EVENT1("consensus", "ReplicaState::SetCurrentTermUnlocked",
               "term", new_term);
  DCHECK(IsLocked());
  if (PREDICT_FALSE(new_term <= GetCurrentTermUnlocked())) {
    return STATUS(IllegalState,
        Substitute("Cannot change term to a term that is lower than or equal to the current one. "
                   "Current: $0, Proposed: $1", GetCurrentTermUnlocked(), new_term));
  }
  cmeta_->set_current_term(new_term);
  cmeta_->clear_voted_for();
  // OK to flush before clearing the leader, because the leader UUID is not part of
  // ConsensusMetadataPB.
  RETURN_NOT_OK(cmeta_->Flush());
  ClearLeaderUnlocked();
  last_received_op_id_current_leader_ = yb::OpId();
  return Status::OK();
}

const int64_t ReplicaState::GetCurrentTermUnlocked() const {
  DCHECK(IsLocked());
  return cmeta_->current_term();
}

void ReplicaState::SetLeaderUuidUnlocked(const std::string& uuid) {
  DCHECK(IsLocked());
  cmeta_->set_leader_uuid(uuid);
  CoarseTimePoint now;
  RefreshLeaderStateCacheUnlocked(&now);
}

const string& ReplicaState::GetLeaderUuidUnlocked() const {
  DCHECK(IsLocked());
  return cmeta_->leader_uuid();
}

const bool ReplicaState::HasVotedCurrentTermUnlocked() const {
  DCHECK(IsLocked());
  return cmeta_->has_voted_for();
}

Status ReplicaState::SetVotedForCurrentTermUnlocked(const std::string& uuid) {
  TRACE_EVENT1("consensus", "ReplicaState::SetVotedForCurrentTermUnlocked",
               "uuid", uuid);
  DCHECK(IsLocked());
  cmeta_->set_voted_for(uuid);
  CHECK_OK(cmeta_->Flush());
  return Status::OK();
}

const std::string& ReplicaState::GetVotedForCurrentTermUnlocked() const {
  DCHECK(IsLocked());
  DCHECK(cmeta_->has_voted_for());
  return cmeta_->voted_for();
}

const string& ReplicaState::GetPeerUuid() const {
  return peer_uuid_;
}

const ConsensusOptions& ReplicaState::GetOptions() const {
  return options_;
}

void ReplicaState::DumpPendingOperationsUnlocked() {
  DCHECK(IsLocked());
  LOG_WITH_PREFIX(INFO) << "Dumping " << pending_operations_.size()
                                 << " pending operations.";
  for (const auto &round : pending_operations_) {
    LOG_WITH_PREFIX(INFO) << round->replicate_msg()->ShortDebugString();
  }
}

Status ReplicaState::CancelPendingOperations() {
  {
    ThreadRestrictions::AssertWaitAllowed();
    UniqueLock lock(update_lock_);
    if (state_ != kShuttingDown) {
      return STATUS(IllegalState, "Can only wait for pending commits on kShuttingDown state.");
    }
    if (pending_operations_.empty()) {
      return Status::OK();
    }

    LOG_WITH_PREFIX(INFO) << "Trying to abort " << pending_operations_.size()
                          << " pending operations because of shutdown.";
    auto abort_status = STATUS(Aborted, "Operation aborted");
    int i = 0;
    for (size_t idx = pending_operations_.size(); idx > 0; ) {
      --idx;
      // We cancel only operations whose applies have not yet been triggered.
      constexpr auto kLogAbortedOperationsNum = 10;
      if (++i <= kLogAbortedOperationsNum) {
        LOG_WITH_PREFIX(INFO) << "Aborting operation because of shutdown: "
                              << pending_operations_[idx]->replicate_msg()->ShortDebugString();
      }
      NotifyReplicationFinishedUnlocked(pending_operations_[idx], abort_status, OpId::kUnknownTerm,
                                        nullptr /* applied_op_ids */);
    }
  }
  return Status::OK();
}

struct PendingOperationsComparator {
  bool operator()(const ConsensusRoundPtr& lhs, int64_t rhs) const {
    return lhs->id().index < rhs;
  }

  bool operator()(int64_t lhs, const ConsensusRoundPtr& rhs) const {
    return lhs < rhs->id().index;
  }
};

ReplicaState::PendingOperations::iterator ReplicaState::FindPendingOperation(int64_t index) {
  if (pending_operations_.empty()) {
    return pending_operations_.end();
  }

  size_t offset = index - pending_operations_.front()->id().index;
  // If index < pending_operations_.front()->id().index() then offset will be very big positive
  // number, so could check both bounds in one comparison.
  if (offset >= pending_operations_.size()) {
    return pending_operations_.end();
  }

  auto result = pending_operations_.begin() + offset;
  DCHECK_EQ((**result).id().index, index);
  return result;
}

Status ReplicaState::AbortOpsAfterUnlocked(int64_t new_preceding_idx) {
  DCHECK(IsLocked());
  LOG_WITH_PREFIX(INFO)
      << "Aborting all operations after (but not including): "
      << new_preceding_idx << ". Current State: " << ToStringUnlocked();

  DCHECK_GE(new_preceding_idx, 0);
  yb::OpId new_preceding;

  auto preceding_op_iter = FindPendingOperation(new_preceding_idx);

  // Either the new preceding id is in the pendings set or it must be equal to the
  // committed index since we can't truncate already committed operations.
  if (preceding_op_iter != pending_operations_.end()) {
    new_preceding = (**preceding_op_iter).id();
    ++preceding_op_iter;
  } else {
    CHECK_EQ(new_preceding_idx, last_committed_op_id_.index);
    new_preceding = last_committed_op_id_;
    if (!pending_operations_.empty() &&
        pending_operations_.front()->id().index > new_preceding_idx) {
      preceding_op_iter = pending_operations_.begin();
    }
  }

  // This is the same as UpdateLastReceivedOpIdUnlocked() but we do it
  // here to avoid the bounds check, since we're breaking monotonicity.
  last_received_op_id_ = new_preceding;
  last_received_op_id_current_leader_ = OpId();
  next_index_ = new_preceding.index + 1;

  auto abort_status = STATUS(Aborted, "Operation aborted by new leader");
  for (auto it = pending_operations_.end(); it != preceding_op_iter;) {
    const ConsensusRoundPtr& round = *--it;
    auto op_id = OpId::FromPB(round->replicate_msg()->id());
    LOG_WITH_PREFIX(INFO) << "Aborting uncommitted operation due to leader change: "
                          << op_id << ", committed: " << last_committed_op_id_;
    NotifyReplicationFinishedUnlocked(round, abort_status, OpId::kUnknownTerm,
                                      nullptr /* applied_op_ids */);
  }

  // Clear entries from pending operations.
  pending_operations_.erase(preceding_op_iter, pending_operations_.end());
  CheckPendingOperationsHead();

  return Status::OK();
}

Status ReplicaState::AddPendingOperation(const ConsensusRoundPtr& round, OperationMode mode) {
  DCHECK(IsLocked());

  auto op_type = round->replicate_msg()->op_type();
  if (PREDICT_FALSE(state_ != kRunning)) {
    // Special case when we're configuring and this is a config change, refuse
    // everything else.
    // TODO: Don't require a NO_OP to get to kRunning state
    if (op_type != NO_OP) {
      return STATUS(IllegalState, "Cannot trigger prepare. Replica is not in kRunning state.");
    }
  }

  if (mode == OperationMode::kLeader) {
    RETURN_NOT_OK(context_->CheckOperationAllowed(round->id(), op_type));
  }

  // Mark pending configuration.
  if (PREDICT_FALSE(op_type == CHANGE_CONFIG_OP)) {
    DCHECK(round->replicate_msg()->change_config_record().has_old_config());
    DCHECK(round->replicate_msg()->change_config_record().old_config().has_opid_index());
    DCHECK(round->replicate_msg()->change_config_record().has_new_config());
    DCHECK(!round->replicate_msg()->change_config_record().new_config().has_opid_index());
    if (mode == OperationMode::kFollower) {
      const RaftConfigPB& old_config = round->replicate_msg()->change_config_record().old_config();
      const RaftConfigPB& new_config = round->replicate_msg()->change_config_record().new_config();
      // The leader has to mark the configuration as pending before it gets here
      // because the active configuration affects the replication queue.
      // Do one last sanity check.
      Status s = CheckNoConfigChangePendingUnlocked();
      if (PREDICT_FALSE(!s.ok() && !new_config.unsafe_config_change())) {
        s = s.CloneAndAppend(Format("New config: $0", new_config));
        LOG_WITH_PREFIX(INFO) << s;
        return s;
      }
      // Check if the pending Raft config has an OpId less than the committed
      // config. If so, this is a replay at startup in which the COMMIT
      // messages were delayed.
      const RaftConfigPB& committed_config = GetCommittedConfigUnlocked();
      if (round->replicate_msg()->id().index() > committed_config.opid_index()) {
        CHECK_OK(SetPendingConfigUnlocked(new_config, round->id()));
      } else {
        LOG_WITH_PREFIX(INFO)
            << "Ignoring setting pending config change with OpId "
            << round->replicate_msg()->id() << " because the committed config has OpId index "
            << committed_config.opid_index() << ". The config change we are ignoring is: "
            << "Old config: { " << old_config.ShortDebugString() << " }. "
            << "New config: { " << new_config.ShortDebugString() << " }";
      }
    }
  } else if (op_type == WRITE_OP) {
    // Leader registers an operation with RetryableRequests even before assigning an op id.
    if (mode == OperationMode::kFollower) {
      auto result = retryable_requests_.Register(round, tablet::IsLeaderSide::kFalse);
      const auto error_msg = "Cannot register retryable request on follower";
      if (!result.ok()) {
        // This can happen if retryable requests have been cleaned up on leader before the follower,
        // see https://github.com/yugabyte/yugabyte-db/issues/11349.
        // Just run cleanup in this case and retry.
        VLOG_WITH_PREFIX(1) << error_msg << ": " << result.status()
                            << ". Cleaning retryable requests";
        auto min_op_id ATTRIBUTE_UNUSED = retryable_requests_.CleanExpiredReplicatedAndGetMinOpId();
        result = retryable_requests_.Register(round, tablet::IsLeaderSide::kFalse);
      }
      if (!result.ok()) {
        return result.status()
            .CloneAndReplaceCode(Status::kIllegalState)
            .CloneAndPrepend(error_msg);
      }
      if (!*result) {
        return STATUS(IllegalState, error_msg);
      }
    }
  } else if (op_type == SPLIT_OP) {
    const auto& split_request = round->replicate_msg()->split_request();
    SCHECK_EQ(
        split_request.tablet_id(), cmeta_->tablet_id(), InvalidArgument,
        "Received split op for a different tablet.");
    // TODO(tsplit): if we get failures past this point we can't undo the tablet state.
    // Might be need some tool to be able to remove SPLIT_OP from Raft log.
  }

  LOG_IF_WITH_PREFIX(
      DFATAL,
      !pending_operations_.empty() &&
          pending_operations_.back()->id().index + 1 != round->id().index)
      << "Adding operation with wrong index: " << AsString(round) << ", last op id: "
      << AsString(pending_operations_.back()->id()) << ", operations: "
      << AsString(pending_operations_);
  pending_operations_.push_back(round);
  CheckPendingOperationsHead();
  return Status::OK();
}

scoped_refptr<ConsensusRound> ReplicaState::GetPendingOpByIndexOrNullUnlocked(int64_t index) {
  DCHECK(IsLocked());
  auto it = FindPendingOperation(index);
  if (it == pending_operations_.end()) {
    return nullptr;
  }
  return *it;
}

Status ReplicaState::UpdateMajorityReplicatedUnlocked(
    const OpId& majority_replicated, OpId* committed_op_id,
    bool* committed_op_id_changed, OpId* last_applied_op_id) {
  DCHECK(IsLocked());
  if (PREDICT_FALSE(state_ == kShuttingDown || state_ == kShutDown)) {
    return STATUS(ServiceUnavailable, "Cannot trigger apply. Replica is shutting down.");
  }
  if (PREDICT_FALSE(state_ != kRunning)) {
    return STATUS(IllegalState, "Cannot trigger apply. Replica is not in kRunning state.");
  }

  // If the last committed operation was in the current term (the normal case)
  // then 'committed_op_id' is simply equal to majority replicated.
  if (last_committed_op_id_.term == GetCurrentTermUnlocked()) {
    *committed_op_id_changed = VERIFY_RESULT(AdvanceCommittedOpIdUnlocked(
        majority_replicated, CouldStop::kFalse));
    *committed_op_id = last_committed_op_id_;
    *last_applied_op_id = GetLastAppliedOpIdUnlocked();
    return Status::OK();
  }

  // If the last committed operation is not in the current term (such as when
  // we change leaders) but 'majority_replicated' is then we can advance the
  // 'committed_op_id' too.
  if (majority_replicated.term == GetCurrentTermUnlocked()) {
    auto previous = last_committed_op_id_;
    *committed_op_id_changed = VERIFY_RESULT(AdvanceCommittedOpIdUnlocked(
        majority_replicated, CouldStop::kFalse));
    *committed_op_id = last_committed_op_id_;
    *last_applied_op_id = GetLastAppliedOpIdUnlocked();
    LOG_WITH_PREFIX(INFO)
        << "Advanced the committed_op_id across terms."
        << " Last committed operation was: " << previous
        << " New committed index is: " << last_committed_op_id_;
    return Status::OK();
  }

  *committed_op_id = last_committed_op_id_;
  *last_applied_op_id = GetLastAppliedOpIdUnlocked();
  YB_LOG_EVERY_N_SECS(WARNING, 1) << LogPrefix()
          << "Can't advance the committed index across term boundaries"
          << " until operations from the current term are replicated."
          << " Last committed operation was: " << last_committed_op_id_ << ","
          << " New majority replicated is: " << majority_replicated << ","
          << " Current term is: " << GetCurrentTermUnlocked();

  return Status::OK();
}

void ReplicaState::SetLastCommittedIndexUnlocked(const yb::OpId& committed_op_id) {
  DCHECK(IsLocked());
  CHECK_GE(last_received_op_id_.index, committed_op_id.index);
  last_committed_op_id_ = committed_op_id;
  CheckPendingOperationsHead();
}

Status ReplicaState::InitCommittedOpIdUnlocked(const yb::OpId& committed_op_id) {
  if (!last_committed_op_id_.empty()) {
    return STATUS_FORMAT(
        IllegalState,
        "Committed index already initialized to: $0, tried to set $1",
        last_committed_op_id_,
        committed_op_id);
  }

  if (!pending_operations_.empty() &&
      committed_op_id.index >= pending_operations_.front()->id().index) {
    RETURN_NOT_OK(ApplyPendingOperationsUnlocked(committed_op_id, CouldStop::kFalse));
  }

  SetLastCommittedIndexUnlocked(committed_op_id);

  return Status::OK();
}

void ReplicaState::CheckPendingOperationsHead() const {
  if (pending_operations_.empty() || last_committed_op_id_.empty() ||
      pending_operations_.front()->id().index == last_committed_op_id_.index + 1) {
    return;
  }

  LOG_WITH_PREFIX(FATAL)
      << "The first pending operation's index is supposed to immediately follow the last committed "
      << "operation's index. Committed op id: " << last_committed_op_id_ << ", pending operations: "
      << AsString(pending_operations_);
}

Result<bool> ReplicaState::AdvanceCommittedOpIdUnlocked(
    const yb::OpId& committed_op_id, CouldStop could_stop) {
  DCHECK(IsLocked());
  // If we already committed up to (or past) 'id' return.
  // This can happen in the case that multiple UpdateConsensus() calls end
  // up in the RPC queue at the same time, and then might get interleaved out
  // of order.
  if (last_committed_op_id_.index >= committed_op_id.index) {
    VLOG_WITH_PREFIX(1)
        << "Already marked ops through " << last_committed_op_id_ << " as committed. "
        << "Now trying to mark " << committed_op_id << " which would be a no-op.";
    return false;
  }

  if (pending_operations_.empty()) {
    VLOG_WITH_PREFIX(1) << "No operations to mark as committed up to: "
                        << committed_op_id;
    return STATUS_FORMAT(
        NotFound,
        "No pending entries, requested to advance last committed OpId from $0 to $1, "
            "last received: $2",
        last_committed_op_id_, committed_op_id, last_received_op_id_);
  }

  CheckPendingOperationsHead();

  auto old_index = last_committed_op_id_.index;

  auto status = ApplyPendingOperationsUnlocked(committed_op_id, could_stop);
  if (!status.ok()) {
    return status;
  }

  return last_committed_op_id_.index != old_index;
}

Status ReplicaState::ApplyPendingOperationsUnlocked(
    const yb::OpId& committed_op_id, CouldStop could_stop) {
  DCHECK(IsLocked());
  VLOG_WITH_PREFIX(1) << "Last triggered apply was: " <<  last_committed_op_id_;

  // Stop at the operation after the last one we must commit. This iterator by definition points to
  // the first entry greater than the committed index, so the entry preceding that must have the
  // OpId equal to committed_op_id.

  auto prev_id = last_committed_op_id_;
  yb::OpId max_allowed_op_id;
  if (!safe_op_id_waiter_) {
    max_allowed_op_id.index = std::numeric_limits<int64_t>::max();
  }
  auto leader_term = GetLeaderStateUnlocked().term;

  OpIds applied_op_ids;
  applied_op_ids.reserve(committed_op_id.index - prev_id.index);

  Status status;

  while (!pending_operations_.empty()) {
    auto round = pending_operations_.front();
    auto current_id = round->id();

    if (PREDICT_TRUE(prev_id)) {
      CHECK_OK_PREPEND(CheckOpInSequence(prev_id, current_id), LogPrefix());
    }

    if (current_id.index > committed_op_id.index) {
      break;
    }

    auto type = round->replicate_msg()->op_type();

    // For write operations we block rocksdb flush, until appropriate records are written to the
    // log file. So we could apply them before adding to log.
    if (type == OperationType::WRITE_OP) {
      if (could_stop && !context_->ShouldApplyWrite()) {
        YB_LOG_EVERY_N_SECS(WARNING, 5) << LogPrefix()
            << "Stop apply pending operations, because of write delay required, last applied: "
            << prev_id << " of " << committed_op_id;
        break;
      }
    } else if (current_id.index > max_allowed_op_id.index ||
               current_id.term > max_allowed_op_id.term) {
      max_allowed_op_id = safe_op_id_waiter_->WaitForSafeOpIdToApply(current_id);
      // This situation should not happen. Prior to #4150 it could happen as follows.  Suppose
      // replica A was the leader of term 1 and added operations 1.100 and 1.101 to the WAL but
      // has not committed them yet.  Replica B decides that A is unavailable, starts and wins
      // term 2 election, and tries to replicate a no-op 2.100.  Replica A starts and wins term 3
      // election and then continues to replicate 1.100 and 1.101 and the new no-op 3.102.
      // Suppose an UpdateConsensus from replica A reaches replica B with a committed op id of
      // 3.102 (because perhaps some other replica has already received those entries).  Replica B
      // will abort 2.100 and try to apply all three operations. Suppose the last op id flushed to
      // the WAL on replica B is currently 2.100, and current_id is 1.101. Then
      // WaitForSafeOpIdToApply would return 2.100 immediately as 2.100 > 1.101 in terms of OpId
      // comparison, and we will throw an error here.
      //
      // However, after the #4150 fix we are resetting flushed op id using ResetLastSynchedOpId
      // when aborting operations during term changes, so WaitForSafeOpIdToApply would correctly
      // wait until 1.101 is written and return 1.101 or 3.102 in the above example.
      if (max_allowed_op_id.index < current_id.index || max_allowed_op_id.term < current_id.term) {
        status = STATUS_FORMAT(
            RuntimeError,
            "Bad max allowed op id ($0), term/index must be no less than that of current op id "
            "($1)",
            max_allowed_op_id, current_id);
        break;
      }
    }

    pending_operations_.pop_front();
    // Set committed configuration.
    if (PREDICT_FALSE(type == OperationType::CHANGE_CONFIG_OP)) {
      ApplyConfigChangeUnlocked(round);
    }

    prev_id = current_id;
    NotifyReplicationFinishedUnlocked(round, Status::OK(), leader_term, &applied_op_ids);
  }

  SetLastCommittedIndexUnlocked(prev_id);

  applied_ops_tracker_(applied_op_ids);

  return status;
}

void ReplicaState::ApplyConfigChangeUnlocked(const ConsensusRoundPtr& round) {
  DCHECK(round->replicate_msg()->change_config_record().has_old_config());
  DCHECK(round->replicate_msg()->change_config_record().has_new_config());
  RaftConfigPB old_config = round->replicate_msg()->change_config_record().old_config();
  RaftConfigPB new_config = round->replicate_msg()->change_config_record().new_config();
  DCHECK(old_config.has_opid_index());
  DCHECK(!new_config.has_opid_index());

  const OpId& current_id = round->id();

  if (PREDICT_FALSE(FLAGS_inject_delay_commit_pre_voter_to_voter_secs)) {
    bool is_transit_to_voter =
      CountVotersInTransition(old_config) > CountVotersInTransition(new_config);
    if (is_transit_to_voter) {
      LOG_WITH_PREFIX(INFO)
          << "Commit skipped as inject_delay_commit_pre_voter_to_voter_secs flag is set to true.\n"
          << "  Old config: { " << old_config.ShortDebugString() << " }.\n"
          << "  New config: { " << new_config.ShortDebugString() << " }";
      SleepFor(MonoDelta::FromSeconds(FLAGS_inject_delay_commit_pre_voter_to_voter_secs));
    }
  }

  new_config.set_opid_index(current_id.index);
  // Check if the pending Raft config has an OpId less than the committed
  // config. If so, this is a replay at startup in which the COMMIT
  // messages were delayed.
  const RaftConfigPB& committed_config = GetCommittedConfigUnlocked();
  if (new_config.opid_index() > committed_config.opid_index()) {
    LOG_WITH_PREFIX(INFO)
        << "Committing config change with OpId "
        << current_id << ". Old config: { " << old_config.ShortDebugString() << " }. "
        << "New config: { " << new_config.ShortDebugString() << " }";
    CHECK_OK(SetCommittedConfigUnlocked(new_config));
  } else {
    LOG_WITH_PREFIX(INFO)
        << "Ignoring commit of config change with OpId "
        << current_id << " because the committed config has OpId index "
        << committed_config.opid_index() << ". The config change we are ignoring is: "
        << "Old config: { " << old_config.ShortDebugString() << " }. "
        << "New config: { " << new_config.ShortDebugString() << " }";
  }
}

const yb::OpId& ReplicaState::GetCommittedOpIdUnlocked() const {
  DCHECK(IsLocked());
  return last_committed_op_id_;
}

RestartSafeCoarseMonoClock& ReplicaState::Clock() {
  return retryable_requests_.Clock();
}

RetryableRequestsCounts ReplicaState::TEST_CountRetryableRequests() {
  auto lock = LockForRead();
  return retryable_requests_.TEST_Counts();
}

bool ReplicaState::AreCommittedAndCurrentTermsSameUnlocked() const {
  int64_t term = GetCurrentTermUnlocked();
  const auto& opid = GetCommittedOpIdUnlocked();
  if (opid.term != term) {
    LOG(INFO) << "committed term=" << opid.term << ", current term=" << term;
    return false;
  }
  return true;
}

void ReplicaState::UpdateLastReceivedOpIdUnlocked(const OpIdPB& op_id) {
  DCHECK(IsLocked());
  auto* trace = Trace::CurrentTrace();
  DCHECK(last_received_op_id_.term <= op_id.term() && last_received_op_id_.index <= op_id.index())
      << LogPrefix() << ": "
      << "Previously received OpId: " << last_received_op_id_
      << ", updated OpId: " << op_id.ShortDebugString()
      << ", Trace:" << std::endl << (trace ? trace->DumpToString(true) : "No trace found");

  last_received_op_id_ = yb::OpId::FromPB(op_id);
  last_received_op_id_current_leader_ = last_received_op_id_;
  next_index_ = op_id.index() + 1;
}

void ReplicaState::UpdateLastReceivedOpIdFromCurrentLeaderIfEmptyUnlocked(const OpId& op_id) {
  if (last_received_op_id_current_leader_.empty()) {
    VLOG_WITH_PREFIX(0) << __func__ << " Updating last_received opid from "
      << last_received_op_id_current_leader_.ToString() << " to " << op_id.ToString();
    last_received_op_id_current_leader_ = op_id;
  }
}

const yb::OpId& ReplicaState::GetLastReceivedOpIdUnlocked() const {
  DCHECK(IsLocked());
  return last_received_op_id_;
}

const yb::OpId& ReplicaState::GetLastReceivedOpIdCurLeaderUnlocked() const {
  DCHECK(IsLocked());
  return last_received_op_id_current_leader_;
}

OpId ReplicaState::GetLastPendingOperationOpIdUnlocked() const {
  DCHECK(IsLocked());
  return pending_operations_.empty() ? OpId() : pending_operations_.back()->id();
}

OpId ReplicaState::NewIdUnlocked() {
  DCHECK(IsLocked());
  return OpId(GetCurrentTermUnlocked(), next_index_++);
}

void ReplicaState::CancelPendingOperation(const OpId& id, bool should_exist) {
  DCHECK(IsLocked());
  CHECK_EQ(GetCurrentTermUnlocked(), id.term);
  CHECK_EQ(next_index_, id.index + 1);
  next_index_ = id.index;

  // We don't use UpdateLastReceivedOpIdUnlocked because we're actually
  // updating it back to a lower value and we need to avoid the checks
  // that method has.

  // This is only ok if we do _not_ release the lock after calling
  // NewIdUnlocked() (which we don't in RaftConsensus::Replicate()).
  // The below is correct because we always have starting NoOp, that has the same term and was
  // replicated.
  last_received_op_id_ = OpId(id.term, id.index - 1);
  if (should_exist) {
    CHECK(!pending_operations_.empty() && pending_operations_.back()->id() == id)
        << "Pending operations should end with: " << id << ", but there are: "
        << AsString(pending_operations_);
    pending_operations_.pop_back();
  } else {
    // It could happen only in leader, while we add new operations, since we already have no op
    // in this term, that would not be cancelled, we could be sure that previous operation
    // has the same term.
    OpId expected_last_op_id(id.term, id.index - 1);
    CHECK(pending_operations_.empty() ||
          (pending_operations_.back()->id() == expected_last_op_id))
        << "Pending operations should end with: " << expected_last_op_id << ", but there are: "
        << AsString(pending_operations_);
  }
}

string ReplicaState::LogPrefix() const {
  auto role_and_term = cmeta_->GetRoleAndTerm();
  return Substitute("T $0 P $1 [term $2 $3]: ",
                    options_.tablet_id,
                    peer_uuid_,
                    role_and_term.second,
                    PeerRole_Name(role_and_term.first));
}

ReplicaState::State ReplicaState::state() const {
  DCHECK(IsLocked());
  return state_;
}

string ReplicaState::ToString() const {
  ThreadRestrictions::AssertWaitAllowed();
  ReplicaState::UniqueLock lock(update_lock_);
  return ToStringUnlocked();
}

string ReplicaState::ToStringUnlocked() const {
  DCHECK(IsLocked());
  return Format(
      "Replica: $0, State: $1, Role: $2, Watermarks: {Received: $3 Committed: $4} Leader: $5",
      peer_uuid_, state_, PeerRole_Name(GetActiveRoleUnlocked()),
      last_received_op_id_, last_committed_op_id_, last_received_op_id_current_leader_);
}

Status ReplicaState::CheckOpInSequence(const yb::OpId& previous, const yb::OpId& current) {
  if (current.term < previous.term) {
    return STATUS_FORMAT(
        Corruption,
        "New operation's term is not >= than the previous op's term. Current: $0. Previous: $1",
        current, previous);
  }

  if (current.index != previous.index + 1) {
    return STATUS_FORMAT(
        Corruption,
        "New operation's index does not follow the previous op's index. Current: $0. Previous: $1",
        current, previous);
  }
  return Status::OK();
}

void ReplicaState::UpdateOldLeaderLeaseExpirationOnNonLeaderUnlocked(
    const CoarseTimeLease& lease, const PhysicalComponentLease& ht_lease) {
  old_leader_lease_.TryUpdate(lease);
  old_leader_ht_lease_.TryUpdate(ht_lease);

  // Reset our lease, since we are non leader now. I.e. follower or candidate.
  auto existing_lease = majority_replicated_lease_expiration_;
  if (existing_lease != CoarseTimeLease::NoneValue()) {
    LOG_WITH_PREFIX(INFO)
        << "Reset our lease: " << MonoDelta(CoarseMonoClock::now() - existing_lease);
    majority_replicated_lease_expiration_ = CoarseTimeLease::NoneValue();
  }

  auto existing_ht_lease = majority_replicated_ht_lease_expiration_.load(std::memory_order_acquire);
  if (existing_ht_lease != PhysicalComponentLease::NoneValue()) {
    LOG_WITH_PREFIX(INFO) << "Reset our ht lease: " << HybridTime::FromMicros(existing_ht_lease);
    majority_replicated_ht_lease_expiration_.store(PhysicalComponentLease::NoneValue(),
                                                   std::memory_order_release);
    cond_.notify_all();
  }
}

template <class Policy>
LeaderLeaseStatus ReplicaState::GetLeaseStatusUnlocked(Policy policy) const {
  DCHECK_EQ(GetActiveRoleUnlocked(), PeerRole::LEADER);

  if (!policy.Enabled()) {
    return LeaderLeaseStatus::HAS_LEASE;
  }

  if (GetActiveConfigUnlocked().peers_size() == 1) {
    // It is OK that majority_replicated_lease_expiration_ might be undefined in this case, because
    // we are only reading it in this function (as of 08/09/2017).
    return LeaderLeaseStatus::HAS_LEASE;
  }

  if (!policy.OldLeaderLeaseExpired()) {
    return LeaderLeaseStatus::OLD_LEADER_MAY_HAVE_LEASE;
  }

  if (policy.MajorityReplicatedLeaseExpired()) {
    return LeaderLeaseStatus::NO_MAJORITY_REPLICATED_LEASE;
  }

  return LeaderLeaseStatus::HAS_LEASE;
}

// Policy that is used during leader lease calculation.
struct GetLeaderLeaseStatusPolicy {
  const ReplicaState* replica_state;
  MonoDelta* remaining_old_leader_lease;
  CoarseTimePoint* now;

  GetLeaderLeaseStatusPolicy(
      const ReplicaState* replica_state_, MonoDelta* remaining_old_leader_lease_,
      CoarseTimePoint* now_)
      : replica_state(replica_state_), remaining_old_leader_lease(remaining_old_leader_lease_),
        now(now_) {
    if (remaining_old_leader_lease) {
      *remaining_old_leader_lease = 0s;
    }
  }

  bool OldLeaderLeaseExpired() {
    const auto remaining_old_leader_lease_duration =
        replica_state->RemainingOldLeaderLeaseDuration(now);
    if (remaining_old_leader_lease_duration) {
      if (remaining_old_leader_lease) {
        *remaining_old_leader_lease = remaining_old_leader_lease_duration;
      }
      return false;
    }
    return true;
  }

  bool MajorityReplicatedLeaseExpired() {
    return replica_state->MajorityReplicatedLeaderLeaseExpired(now);
  }

  bool Enabled() {
    return true;
  }
};

bool ReplicaState::MajorityReplicatedLeaderLeaseExpired(CoarseTimePoint* now) const {
  if (majority_replicated_lease_expiration_ == CoarseTimePoint()) {
    return true;
  }

  if (*now == CoarseTimePoint()) {
    *now = CoarseMonoClock::Now();
  }

  return *now >= majority_replicated_lease_expiration_;
}

LeaderLeaseStatus ReplicaState::GetLeaderLeaseStatusUnlocked(
    MonoDelta* remaining_old_leader_lease, CoarseTimePoint* now) const {
  if (now == nullptr) {
    CoarseTimePoint local_now;
    return GetLeaseStatusUnlocked(GetLeaderLeaseStatusPolicy(
        this, remaining_old_leader_lease, &local_now));
  }
  return GetLeaseStatusUnlocked(GetLeaderLeaseStatusPolicy(this, remaining_old_leader_lease, now));
}

bool ReplicaState::MajorityReplicatedHybridTimeLeaseExpiredAt(MicrosTime hybrid_time) const {
  return hybrid_time >= majority_replicated_ht_lease_expiration_;
}

struct GetHybridTimeLeaseStatusAtPolicy {
  const ReplicaState* replica_state;
  MicrosTime micros_time;

  GetHybridTimeLeaseStatusAtPolicy(const ReplicaState* rs, MicrosTime ht)
      : replica_state(rs), micros_time(ht) {}

  bool OldLeaderLeaseExpired() {
    return micros_time > replica_state->old_leader_ht_lease().expiration;
  }

  bool MajorityReplicatedLeaseExpired() {
    return replica_state->MajorityReplicatedHybridTimeLeaseExpiredAt(micros_time);
  }

  bool Enabled() {
    return FLAGS_ht_lease_duration_ms != 0;
  }
};

LeaderLeaseStatus ReplicaState::GetHybridTimeLeaseStatusAtUnlocked(
    MicrosTime micros_time) const {
  return GetLeaseStatusUnlocked(GetHybridTimeLeaseStatusAtPolicy(this, micros_time));
}

MonoDelta ReplicaState::RemainingOldLeaderLeaseDuration(CoarseTimePoint* now) const {
  MonoDelta result;
  if (old_leader_lease_) {
    CoarseTimePoint now_local;
    if (!now) {
      now = &now_local;
    }
    *now = CoarseMonoClock::Now();

    if (*now > old_leader_lease_.expiration) {
      // Reset the old leader lease expiration time so that we don't have to check it anymore.
      old_leader_lease_.Reset();
    } else {
      result = old_leader_lease_.expiration - *now;
    }
  }

  return result;
}

MonoDelta ReplicaState::RemainingMajorityReplicatedLeaderLeaseDuration() const {
  MonoDelta result;
  if (majority_replicated_lease_expiration_ == CoarseTimeLease::NoneValue()) {
    return result;
  }
  CoarseTimePoint now_local = CoarseMonoClock::Now();
  if (now_local > majority_replicated_lease_expiration_) {
    // Reset the majority replicated leader lease expiration time so that we
    // don't have to check it anymore.
    LOG_WITH_PREFIX(INFO)
        << "Reset our lease: "
        << MonoDelta(CoarseMonoClock::now() - majority_replicated_lease_expiration_);
    majority_replicated_lease_expiration_ = CoarseTimeLease::NoneValue();
  } else {
    result = majority_replicated_lease_expiration_ - now_local;
  }
  return result;
}

Result<MicrosTime> ReplicaState::MajorityReplicatedHtLeaseExpiration(
    MicrosTime min_allowed, CoarseTimePoint deadline) const {
  if (FLAGS_ht_lease_duration_ms == 0) {
    return kMaxHybridTimePhysicalMicros;
  }

  auto result = majority_replicated_ht_lease_expiration_.load(std::memory_order_acquire);
  if (result >= min_allowed) { // Fast path
    return result;
  }

  if (result != PhysicalComponentLease::NoneValue()) {
    // Slow path
    UniqueLock l(update_lock_);
    auto predicate = [this, &result, min_allowed] {
      result = majority_replicated_ht_lease_expiration_.load(std::memory_order_acquire);
      return result >= min_allowed || result == PhysicalComponentLease::NoneValue();
    };
    if (deadline == CoarseTimePoint::max()) {
      cond_.wait(l, predicate);
    } else if (!cond_.wait_until(l, deadline, predicate)) {
      return STATUS_FORMAT(TimedOut, "Timed out waiting leader lease: $0", min_allowed);
    }
  }

  if (result == PhysicalComponentLease::NoneValue()) {
    static const Status kNotLeaderStatus = STATUS(IllegalState, "Not a leader");
    return kNotLeaderStatus;
  }

  return result;
}

Status ReplicaState::SetMajorityReplicatedLeaseExpirationUnlocked(
    const MajorityReplicatedData& majority_replicated_data,
    EnumBitSet<SetMajorityReplicatedLeaseExpirationFlag> flags) {
  if (PREDICT_FALSE(!leader_no_op_committed_ && GetActiveConfigUnlocked().peers_size() != 1)) {
    // Don't setting leader lease until NoOp at current term is committed.
    // If the older leader containing old, unreplicated operations is elected as leader again,
    // when replicating old operations to current node, might have a too low hybrid time.
    // See https://github.com/yugabyte/yugabyte-db/issues/14225 for more details.
    // Under leader only mode, this case shouldn't happen, it's safe to set the lease exp.
    return STATUS_FORMAT(IllegalState,
                         "NoOp of current term is not committed "
                         "(majority_replicated_lease_expiration_ = $0, "
                         "majority_replicated_ht_lease_expiration_ = $1)",
                         majority_replicated_lease_expiration_,
                         majority_replicated_ht_lease_expiration_.load(std::memory_order_acquire));
  }
  majority_replicated_lease_expiration_ = majority_replicated_data.leader_lease_expiration;
  majority_replicated_ht_lease_expiration_.store(majority_replicated_data.ht_lease_expiration,
                                                 std::memory_order_release);

  if (flags.Test(SetMajorityReplicatedLeaseExpirationFlag::kResetOldLeaderLease)) {
    LOG_WITH_PREFIX(INFO)
        << "Revoked old leader " << old_leader_lease_.holder_uuid << " lease: "
        << MonoDelta(old_leader_lease_.expiration - CoarseMonoClock::now());
    old_leader_lease_.Reset();
  }

  if (flags.Test(SetMajorityReplicatedLeaseExpirationFlag::kResetOldLeaderHtLease)) {
    LOG_WITH_PREFIX(INFO)
        << "Revoked old leader " << old_leader_ht_lease_.holder_uuid << " ht lease: "
        << HybridTime::FromMicros(old_leader_ht_lease_.expiration);
    old_leader_ht_lease_.Reset();
  }

  CoarseTimePoint now;
  RefreshLeaderStateCacheUnlocked(&now);
  cond_.notify_all();
  return Status::OK();
}

uint64_t ReplicaState::OnDiskSize() const {
  return cmeta_->on_disk_size();
}

Result<bool> ReplicaState::RegisterRetryableRequest(
    const ConsensusRoundPtr& round, tablet::IsLeaderSide is_leader_side) {
  CHECK(is_leader_side);
  return retryable_requests_.Register(round, tablet::IsLeaderSide::kTrue);
}

OpId ReplicaState::MinRetryableRequestOpId() {
  UniqueLock lock;
  auto status = LockForUpdate(&lock);
  if (!status.ok()) {
    return OpId::Min(); // return minimal op id, that prevents log from cleaning
  }
  return retryable_requests_.CleanExpiredReplicatedAndGetMinOpId();
}

void ReplicaState::NotifyReplicationFinishedUnlocked(
    const ConsensusRoundPtr& round, const Status& status, int64_t leader_term,
    OpIds* applied_op_ids) {
  round->NotifyReplicationFinished(status, leader_term, applied_op_ids);

  retryable_requests_.ReplicationFinished(*round->replicate_msg(), status, leader_term);
}

consensus::LeaderState ReplicaState::RefreshLeaderStateCacheUnlocked(CoarseTimePoint* now) const {
  auto result = GetLeaderStateUnlocked(LeaderLeaseCheckMode::NEED_LEASE, now);
  LeaderStateCache cache;
  if (result.status == LeaderStatus::LEADER_AND_READY) {
    cache.Set(result.status, result.term, majority_replicated_lease_expiration_);
  } else if (result.status == LeaderStatus::LEADER_BUT_OLD_LEADER_MAY_HAVE_LEASE) {
    cache.Set(result.status, result.remaining_old_leader_lease.ToMicroseconds(),
              *now + result.remaining_old_leader_lease);
  } else {
    cache.Set(result.status, 0 /* extra_value */, CoarseTimePoint::max());
  }

  leader_state_cache_.store(cache, boost::memory_order_release);

  return result;
}

void ReplicaState::SetLeaderNoOpCommittedUnlocked(bool value) {
  LOG_WITH_PREFIX(INFO)
      << __func__ << "(" << value << "), committed: " << GetCommittedOpIdUnlocked()
      << ", received: " << GetLastReceivedOpIdUnlocked();

  leader_no_op_committed_ = value;
  CoarseTimePoint now;
  RefreshLeaderStateCacheUnlocked(&now);
}

}  // namespace consensus
}  // namespace yb
