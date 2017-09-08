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

#include <algorithm>
#include <gflags/gflags.h>

#include "yb/consensus/log_util.h"
#include "yb/consensus/quorum_util.h"
#include "yb/consensus/replica_state.h"
#include "yb/gutil/map-util.h"
#include "yb/gutil/strings/join.h"
#include "yb/gutil/strings/strcat.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/util/debug/trace_event.h"
#include "yb/util/flag_tags.h"
#include "yb/util/logging.h"
#include "yb/util/status.h"
#include "yb/util/trace.h"
#include "yb/util/tostring.h"
#include "yb/util/enums.h"

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

namespace {

const int kBitsPerPackedRole = 3;
static_assert(0 <= RaftPeerPB_Role_Role_MIN, "RaftPeerPB_Role_Role_MIN must be non-negative.");
static_assert(RaftPeerPB_Role_Role_MAX < (1 << kBitsPerPackedRole),
              "RaftPeerPB_Role_Role_MAX must fit in kBitsPerPackedRole bits.");

ReplicaState::PackedRoleAndTerm PackRoleAndTerm(RaftPeerPB::Role role, int64_t term) {
  // Ensure we've had no more than 2305843009213693952 terms in this tablet.
  CHECK_LT(term, 1ull << (8 * sizeof(ReplicaState::PackedRoleAndTerm) - kBitsPerPackedRole));
  return util::to_underlying(role) | (term << kBitsPerPackedRole);
}

int64_t UnpackTerm(ReplicaState::PackedRoleAndTerm role_and_term) {
  return role_and_term >> kBitsPerPackedRole;
}

RaftPeerPB::Role UnpackRole(ReplicaState::PackedRoleAndTerm role_and_term) {
  return static_cast<RaftPeerPB::Role>(role_and_term & ((1 << kBitsPerPackedRole) - 1));
}

} // anonymous namespace

using yb::util::to_underlying;

//////////////////////////////////////////////////
// ReplicaState
//////////////////////////////////////////////////

ReplicaState::ReplicaState(ConsensusOptions options, string peer_uuid,
                           gscoped_ptr<ConsensusMetadata> cmeta,
                           ReplicaTransactionFactory* txn_factory)
    : options_(std::move(options)),
      peer_uuid_(std::move(peer_uuid)),
      cmeta_(cmeta.Pass()),
      next_index_(0),
      txn_factory_(txn_factory),
      last_received_op_id_(MinimumOpId()),
      last_received_op_id_current_leader_(MinimumOpId()),
      last_committed_index_(MinimumOpId()),
      state_(kInitialized) {
  CHECK(cmeta_) << "ConsensusMeta passed as NULL";
  StoreRoleAndTerm(cmeta_->active_role(), cmeta_->current_term());
}

Status ReplicaState::StartUnlocked(const OpId& last_id_in_wal) {
  DCHECK(update_lock_.is_locked());

  // Our last persisted term can be higher than the last persisted operation
  // (i.e. if we called an election) but reverse should never happen.
  CHECK_LE(last_id_in_wal.term(), GetCurrentTermUnlocked()) << LogPrefixUnlocked()
      << "The last op in the WAL with id " << OpIdToString(last_id_in_wal)
      << " has a term (" << last_id_in_wal.term() << ") that is greater "
      << "than the latest recorded term, which is " << GetCurrentTermUnlocked();

  next_index_ = last_id_in_wal.index() + 1;

  last_received_op_id_.CopyFrom(last_id_in_wal);

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
  return update_lock_.is_locked();
}

Status ReplicaState::LockForRead(UniqueLock* lock) const {
  ThreadRestrictions::AssertWaitAllowed();
  UniqueLock l(update_lock_);
  lock->swap(l);
  return Status::OK();
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

  if (PREDICT_FALSE(GetActiveRoleUnlocked() != RaftPeerPB::LEADER)) {
    return STATUS(IllegalState, "Replica not LEADER");
  }
  lock->swap(l);
  return Status::OK();
}

Status ReplicaState::CheckActiveLeaderUnlocked(LeaderLeaseCheckMode lease_check_mode) const {
  const RaftPeerPB::Role role = GetActiveRoleUnlocked();
  if (role == RaftPeerPB::LEADER) {
    // We don't need to check the lease in some cases, e.g. when replicating the NoOp entry.
    if (lease_check_mode == LeaderLeaseCheckMode::DONT_NEED_LEASE)
      return Status::OK();

    MonoDelta remaining_old_leader_lease;
    const auto lease_status = GetLeaderLeaseStatusUnlocked(&remaining_old_leader_lease);
    switch (lease_status) {
      case LeaderLeaseStatus::HAS_LEASE:
        return Status::OK();
      case LeaderLeaseStatus::OLD_LEADER_MAY_HAVE_LEASE:
        // We return LeaderNotReady so that the client would retry on the same server.
        return STATUS(LeaderNotReadyToServe,
            Substitute("Previous leader's lease might still be active ($0 remaining).",
                       remaining_old_leader_lease.ToString()));
      case LeaderLeaseStatus::NO_MAJORITY_REPLICATED_LEASE:
        // We are returning a NotTheLeader as opposed to LeaderNotReady, because there is a chance
        // that we're a partitioned-away leader, and the client needs to do another leader lookup.
        return STATUS(LeaderHasNoLease, "This leader has not yet acquired a lease.");
    }
    FATAL_INVALID_ENUM_VALUE(LeaderLeaseStatus, lease_status);
  } else {
    ConsensusStatePB cstate = ConsensusStateUnlocked(CONSENSUS_CONFIG_ACTIVE);
    return STATUS(IllegalState, Substitute("Replica $0 is not leader of this config. Role: $1. "
                                           "Consensus state: $2",
                                           peer_uuid_,
                                           RaftPeerPB::Role_Name(role),
                                           cstate.ShortDebugString()));
  }
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
  if (!IsRaftConfigVoter(peer_uuid_, ConsensusStateUnlocked(CONSENSUS_CONFIG_ACTIVE).config())) {
    LOG_WITH_PREFIX_UNLOCKED(INFO) << "Allowing update even though not a member of the config";
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
  DCHECK(update_lock_.is_locked());
  CHECK_EQ(state_, kShuttingDown);
  state_ = kShutDown;
  return Status::OK();
}

ConsensusStatePB ReplicaState::ConsensusStateUnlocked(ConsensusConfigType type) const {
  return cmeta_->ToConsensusStatePB(type);
}

RaftPeerPB::Role ReplicaState::GetActiveRoleUnlocked() const {
  DCHECK(update_lock_.is_locked());
  return cmeta_->active_role();
}

bool ReplicaState::IsConfigChangePendingUnlocked() const {
  DCHECK(update_lock_.is_locked());
  return cmeta_->has_pending_config();
}

Status ReplicaState::CheckNoConfigChangePendingUnlocked() const {
  DCHECK(update_lock_.is_locked());
  if (IsConfigChangePendingUnlocked()) {
    return STATUS(IllegalState,
        Substitute("RaftConfig change currently pending. Only one is allowed at a time.\n"
                   "  Committed config: $0.\n  Pending config: $1",
                   GetCommittedConfigUnlocked().ShortDebugString(),
                   GetPendingConfigUnlocked().ShortDebugString()));
  }
  return Status::OK();
}

Status ReplicaState::SetPendingConfigUnlocked(const RaftConfigPB& new_config) {
  DCHECK(update_lock_.is_locked());
  RETURN_NOT_OK_PREPEND(VerifyRaftConfig(new_config, UNCOMMITTED_QUORUM),
                        "Invalid config to set as pending");
  CHECK(!cmeta_->has_pending_config())
      << "Attempt to set pending config while another is already pending! "
      << "Existing pending config: " << cmeta_->pending_config().ShortDebugString() << "; "
      << "Attempted new pending config: " << new_config.ShortDebugString();
  cmeta_->set_pending_config(new_config);
  return Status::OK();
}

Status ReplicaState::ClearPendingConfigUnlocked() {
  DCHECK(update_lock_.is_locked());
  if (!cmeta_->has_pending_config()) {
    LOG(WARNING) << "Attempt to clear a non-existent pending config."
                 << "Existing committed config: " << cmeta_->committed_config().ShortDebugString();
    return STATUS(IllegalState, "Attempt to clear a non-existent pending config.");
  }
  cmeta_->clear_pending_config();
  return Status::OK();
}

const RaftConfigPB& ReplicaState::GetPendingConfigUnlocked() const {
  DCHECK(update_lock_.is_locked());
  CHECK(IsConfigChangePendingUnlocked()) << "No pending config";
  return cmeta_->pending_config();
}

Status ReplicaState::SetCommittedConfigUnlocked(const RaftConfigPB& committed_config) {
  TRACE_EVENT0("consensus", "ReplicaState::SetCommittedConfigUnlocked");
  DCHECK(update_lock_.is_locked());
  DCHECK(committed_config.IsInitialized());
  RETURN_NOT_OK_PREPEND(VerifyRaftConfig(committed_config, COMMITTED_QUORUM),
                        "Invalid config to set as committed");

  // Compare committed with pending configuration, ensure they are the same.
  // Pending will not have an opid_index, so ignore that field.
  DCHECK(cmeta_->has_pending_config());
  RaftConfigPB config_no_opid = committed_config;
  config_no_opid.clear_opid_index();
  const RaftConfigPB& pending_config = GetPendingConfigUnlocked();
  // Quorums must be exactly equal, even w.r.t. peer ordering.
  CHECK_EQ(GetPendingConfigUnlocked().SerializeAsString(), config_no_opid.SerializeAsString())
      << Substitute("New committed config must equal pending config, but does not. "
                    "Pending config: $0, committed config: $1",
                    pending_config.ShortDebugString(), committed_config.ShortDebugString());

  cmeta_->set_committed_config(committed_config);
  cmeta_->clear_pending_config();
  CHECK_OK(cmeta_->Flush());
  return Status::OK();
}

const RaftConfigPB& ReplicaState::GetCommittedConfigUnlocked() const {
  DCHECK(update_lock_.is_locked());
  return cmeta_->committed_config();
}

const RaftConfigPB& ReplicaState::GetActiveConfigUnlocked() const {
  DCHECK(update_lock_.is_locked());
  return cmeta_->active_config();
}

bool ReplicaState::IsOpCommittedOrPending(const OpId& op_id, bool* term_mismatch) {
  DCHECK(update_lock_.is_locked());

  *term_mismatch = false;

  if (cmeta_ == nullptr) {
    LOG(FATAL) << "cmeta_ cannot be NULL";
  }

  int64_t committed_index = GetCommittedOpIdUnlocked().index();
  if (op_id.index() <= committed_index) {
    return true;
  }

  int64_t last_received_index = GetLastReceivedOpIdUnlocked().index();
  if (op_id.index() > last_received_index) {
    return false;
  }

  scoped_refptr<ConsensusRound> round = GetPendingOpByIndexOrNullUnlocked(op_id.index());
  if (round == nullptr) {
    LOG_WITH_PREFIX_UNLOCKED(ERROR) << "Consensus round not found for op id " << op_id << ": "
               << "committed_index=" << committed_index << ", "
               << "last_received_index=" << last_received_index << ", "
               << "tablet: " << options_.tablet_id << ", current state: "
               << ToStringUnlocked();
    DumpPendingTransactionsUnlocked();
    CHECK(false);
  }

  if (round->id().term() != op_id.term()) {
    *term_mismatch = true;
    return false;
  }
  return true;
}

Status ReplicaState::SetCurrentTermUnlocked(int64_t new_term) {
  TRACE_EVENT1("consensus", "ReplicaState::SetCurrentTermUnlocked",
               "term", new_term);
  DCHECK(update_lock_.is_locked());
  if (PREDICT_FALSE(new_term <= GetCurrentTermUnlocked())) {
    return STATUS(IllegalState,
        Substitute("Cannot change term to a term that is lower than or equal to the current one. "
                   "Current: $0, Proposed: $1", GetCurrentTermUnlocked(), new_term));
  }
  cmeta_->set_current_term(new_term);
  cmeta_->clear_voted_for();
  // OK to flush before clearing the leader, because the leader UUID is not part of
  // ConsensusMetadataPB.
  CHECK_OK(cmeta_->Flush());
  ClearLeaderUnlocked();
  // No need to call StoreRoleAndTerm here, because ClearLeaderUnlocked already calls it.
  last_received_op_id_current_leader_ = MinimumOpId();
  return Status::OK();
}

const int64_t ReplicaState::GetCurrentTermUnlocked() const {
  DCHECK(update_lock_.is_locked());
  return cmeta_->current_term();
}

void ReplicaState::SetLeaderUuidUnlocked(const std::string& uuid) {
  DCHECK(update_lock_.is_locked());
  cmeta_->set_leader_uuid(uuid);
  StoreRoleAndTerm(cmeta_->active_role(), cmeta_->current_term());
}

const string& ReplicaState::GetLeaderUuidUnlocked() const {
  DCHECK(update_lock_.is_locked());
  return cmeta_->leader_uuid();
}

const bool ReplicaState::HasVotedCurrentTermUnlocked() const {
  DCHECK(update_lock_.is_locked());
  return cmeta_->has_voted_for();
}

Status ReplicaState::SetVotedForCurrentTermUnlocked(const std::string& uuid) {
  TRACE_EVENT1("consensus", "ReplicaState::SetVotedForCurrentTermUnlocked",
               "uuid", uuid);
  DCHECK(update_lock_.is_locked());
  cmeta_->set_voted_for(uuid);
  CHECK_OK(cmeta_->Flush());
  return Status::OK();
}

const std::string& ReplicaState::GetVotedForCurrentTermUnlocked() const {
  DCHECK(update_lock_.is_locked());
  DCHECK(cmeta_->has_voted_for());
  return cmeta_->voted_for();
}

ReplicaTransactionFactory* ReplicaState::GetReplicaTransactionFactoryUnlocked() const {
  return txn_factory_;
}

const string& ReplicaState::GetPeerUuid() const {
  return peer_uuid_;
}

const ConsensusOptions& ReplicaState::GetOptions() const {
  return options_;
}

void ReplicaState::DumpPendingTransactionsUnlocked() {
  DCHECK(update_lock_.is_locked());
  LOG_WITH_PREFIX_UNLOCKED(INFO) << "Dumping " << pending_txns_.size()
                                 << " pending transactions.";
  for (const auto &txn : pending_txns_) {
    LOG_WITH_PREFIX_UNLOCKED(INFO) << txn.second->replicate_msg()->ShortDebugString();
  }
}

Status ReplicaState::CancelPendingTransactions() {
  {
    ThreadRestrictions::AssertWaitAllowed();
    UniqueLock lock(update_lock_);
    if (state_ != kShuttingDown) {
      return STATUS(IllegalState, "Can only wait for pending commits on kShuttingDown state.");
    }
    if (pending_txns_.empty()) {
      return Status::OK();
    }

    LOG_WITH_PREFIX_UNLOCKED(INFO) << "Trying to abort " << pending_txns_.size()
                                   << " pending transactions.";
    for (const auto& txn : pending_txns_) {
      const scoped_refptr<ConsensusRound>& round = txn.second;
      // We cancel only transactions whose applies have not yet been triggered.
      LOG_WITH_PREFIX_UNLOCKED(INFO) << "Aborting transaction as it isn't in flight: "
                                     << txn.second->replicate_msg()->ShortDebugString();
      round->NotifyReplicationFinished(STATUS(Aborted, "Transaction aborted"));
    }
  }
  return Status::OK();
}

Status ReplicaState::AbortOpsAfterUnlocked(int64_t new_preceding_idx) {
  DCHECK(update_lock_.is_locked());
  LOG_WITH_PREFIX_UNLOCKED(INFO) << "Aborting all transactions after (but not including): "
      << new_preceding_idx << ". Current State: " << ToStringUnlocked();

  DCHECK_GE(new_preceding_idx, 0);
  OpId new_preceding;

  auto iter = pending_txns_.lower_bound(new_preceding_idx);

  // Either the new preceding id is in the pendings set or it must be equal to the
  // committed index since we can't truncate already committed operations.
  if (iter != pending_txns_.end() && (*iter).first == new_preceding_idx) {
    new_preceding = (*iter).second->replicate_msg()->id();
    ++iter;
  } else {
    CHECK_EQ(new_preceding_idx, last_committed_index_.index());
    new_preceding = last_committed_index_;
  }

  // This is the same as UpdateLastReceivedOpIdUnlocked() but we do it
  // here to avoid the bounds check, since we're breaking monotonicity.
  last_received_op_id_ = new_preceding;
  last_received_op_id_current_leader_ = last_received_op_id_;
  next_index_ = new_preceding.index() + 1;

  for (; iter != pending_txns_.end();) {
    const scoped_refptr<ConsensusRound>& round = (*iter).second;
    LOG_WITH_PREFIX_UNLOCKED(INFO) << "Aborting uncommitted operation due to leader change: "
                                   << round->replicate_msg()->id();
    round->NotifyReplicationFinished(STATUS(Aborted, "Transaction aborted by new leader"));
    // Erase the entry from pendings.
    pending_txns_.erase(iter++);
  }

  return Status::OK();
}

Status ReplicaState::AddPendingOperation(const scoped_refptr<ConsensusRound>& round) {
  DCHECK(update_lock_.is_locked());
  if (PREDICT_FALSE(state_ != kRunning)) {
    // Special case when we're configuring and this is a config change, refuse
    // everything else.
    // TODO: Don't require a NO_OP to get to kRunning state
    if (round->replicate_msg()->op_type() != NO_OP) {
      return STATUS(IllegalState, "Cannot trigger prepare. Replica is not in kRunning state.");
    }
  }

  // Mark pending configuration.
  if (PREDICT_FALSE(round->replicate_msg()->op_type() == CHANGE_CONFIG_OP)) {
    DCHECK(round->replicate_msg()->change_config_record().has_old_config());
    DCHECK(round->replicate_msg()->change_config_record().old_config().has_opid_index());
    DCHECK(round->replicate_msg()->change_config_record().has_new_config());
    DCHECK(!round->replicate_msg()->change_config_record().new_config().has_opid_index());
    if (GetActiveRoleUnlocked() != RaftPeerPB::LEADER) {
      const RaftConfigPB& old_config = round->replicate_msg()->change_config_record().old_config();
      const RaftConfigPB& new_config = round->replicate_msg()->change_config_record().new_config();
      // The leader has to mark the configuration as pending before it gets here
      // because the active configuration affects the replication queue.
      // Do one last sanity check.
      Status s = CheckNoConfigChangePendingUnlocked();
      if (PREDICT_FALSE(!s.ok())) {
        s = s.CloneAndAppend(Substitute("\n  New config: $0", new_config.ShortDebugString()));
        LOG_WITH_PREFIX_UNLOCKED(INFO) << s.ToString();
        return s;
      }
      // Check if the pending Raft config has an OpId less than the committed
      // config. If so, this is a replay at startup in which the COMMIT
      // messages were delayed.
      const RaftConfigPB& committed_config = GetCommittedConfigUnlocked();
      if (round->replicate_msg()->id().index() > committed_config.opid_index()) {
        CHECK_OK(SetPendingConfigUnlocked(new_config));
      } else {
        LOG_WITH_PREFIX_UNLOCKED(INFO) << "Ignoring setting pending config change with OpId "
            << round->replicate_msg()->id() << " because the committed config has OpId index "
            << committed_config.opid_index() << ". The config change we are ignoring is: "
            << "Old config: { " << old_config.ShortDebugString() << " }. "
            << "New config: { " << new_config.ShortDebugString() << " }";
      }
    }
  }

  InsertOrDie(&pending_txns_, round->replicate_msg()->id().index(), round);
  return Status::OK();
}

scoped_refptr<ConsensusRound> ReplicaState::GetPendingOpByIndexOrNullUnlocked(int64_t index) {
  DCHECK(update_lock_.is_locked());
  return FindPtrOrNull(pending_txns_, index);
}

Status ReplicaState::UpdateMajorityReplicatedUnlocked(const OpId& majority_replicated,
                                                      OpId* committed_index,
                                                      bool* committed_index_changed) {
  DCHECK(update_lock_.is_locked());
  DCHECK(majority_replicated.IsInitialized());
  DCHECK(last_committed_index_.IsInitialized());
  if (PREDICT_FALSE(state_ == kShuttingDown || state_ == kShutDown)) {
    return STATUS(ServiceUnavailable, "Cannot trigger apply. Replica is shutting down.");
  }
  if (PREDICT_FALSE(state_ != kRunning)) {
    return STATUS(IllegalState, "Cannot trigger apply. Replica is not in kRunning state.");
  }

  // If the last committed operation was in the current term (the normal case)
  // then 'committed_index' is simply equal to majority replicated.
  if (last_committed_index_.term() == GetCurrentTermUnlocked()) {
    RETURN_NOT_OK(AdvanceCommittedIndexUnlocked(majority_replicated,
                                                committed_index_changed));
    committed_index->CopyFrom(last_committed_index_);
    return Status::OK();
  }

  // If the last committed operation is not in the current term (such as when
  // we change leaders) but 'majority_replicated' is then we can advance the
  // 'committed_index' too.
  if (majority_replicated.term() == GetCurrentTermUnlocked()) {
    OpId previous = last_committed_index_;
    RETURN_NOT_OK(AdvanceCommittedIndexUnlocked(majority_replicated,
                                                committed_index_changed));
    committed_index->CopyFrom(last_committed_index_);
    LOG_WITH_PREFIX_UNLOCKED(INFO) << "Advanced the committed_index across terms."
        << " Last committed operation was: " << previous.ShortDebugString()
        << " New committed index is: " << last_committed_index_.ShortDebugString();
    return Status::OK();
  }

  committed_index->CopyFrom(last_committed_index_);
  YB_LOG_EVERY_N_SECS(WARNING, 1) << LogPrefixUnlocked()
          << "Can't advance the committed index across term boundaries"
          << " until operations from the current term are replicated."
          << " Last committed operation was: " << last_committed_index_.ShortDebugString() << ","
          << " New majority replicated is: " << majority_replicated.ShortDebugString() << ","
          << " Current term is: " << GetCurrentTermUnlocked();

  return Status::OK();
}

void ReplicaState::SetLastCommittedIndexUnlocked(const OpId& committed_index) {
  DCHECK(update_lock_.is_locked());
  CHECK_GE(last_received_op_id_.index(), committed_index.index());
  last_committed_index_ = committed_index;
}

void ReplicaState::StoreRoleAndTerm(RaftPeerPB::Role role, int64_t term) {
  role_and_term_.store(PackRoleAndTerm(role, term), std::memory_order_release);
}

Status ReplicaState::InitCommittedIndexUnlocked(const OpId& committed_index) {
  if (!OpIdEquals(last_committed_index_, MinimumOpId())) {
    return STATUS_FORMAT(
        IllegalState,
        "Committed index already initialized to: $0, tried to set $1",
        last_committed_index_,
        committed_index);
  }

  if (!pending_txns_.empty() && committed_index.index() >= pending_txns_.begin()->first) {
    auto status = ApplyPendingOperationsUnlocked(pending_txns_.begin(), committed_index);
    if (!status.ok()) {
      return status;
    }
  }

  SetLastCommittedIndexUnlocked(committed_index);

  return Status::OK();
}

Status ReplicaState::CheckOperationExist(const OpId& committed_index,
                                         IndexToRoundMap::iterator* end_iter) {
  auto prev = pending_txns_.upper_bound(committed_index.index());
  if (prev == pending_txns_.begin()) {
    return STATUS_FORMAT(
        NotFound,
        "No pending entries before committed index: $0 => $1, stack: $2, pending: $3",
        last_committed_index_,
        committed_index,
        GetStackTrace(),
        pending_txns_);
  }
  *end_iter = prev;
  --prev;
  const auto prev_id = prev->second->id();
  if (!OpIdEquals(prev_id, committed_index)) {
    return STATUS_FORMAT(NotFound,
        "No pending entry with committed index: $0 => $1, stack: $2, pending: $3",
        last_committed_index_,
        committed_index,
        GetStackTrace(),
        pending_txns_);
  }
  return Status::OK();
}

Status ReplicaState::AdvanceCommittedIndexUnlocked(const OpId& committed_index,
                                                   bool *committed_index_changed) {
  DCHECK(update_lock_.is_locked());
  if (committed_index_changed) {
    *committed_index_changed = false;
  }
  // If we already committed up to (or past) 'id' return.
  // This can happen in the case that multiple UpdateConsensus() calls end
  // up in the RPC queue at the same time, and then might get interleaved out
  // of order.
  if (last_committed_index_.index() >= committed_index.index()) {
    VLOG_WITH_PREFIX_UNLOCKED(1)
        << "Already marked ops through " << last_committed_index_ << " as committed. "
        << "Now trying to mark " << committed_index << " which would be a no-op.";
    return Status::OK();
  }

  if (pending_txns_.empty()) {
    VLOG_WITH_PREFIX_UNLOCKED(1) << "No transactions to mark as committed up to: "
                                 << committed_index.ShortDebugString();
    return STATUS_SUBSTITUTE(
        NotFound,
        "No pending entries, requested to advance last committed OpId from $0 to $1, "
            "last received: $2",
        last_committed_index_.ShortDebugString(),
        committed_index.ShortDebugString(),
        last_received_op_id_.ShortDebugString());
  }

  // Start at the operation after the last committed one.
  auto iter = pending_txns_.upper_bound(last_committed_index_.index());
  CHECK(iter != pending_txns_.end());

  auto status = ApplyPendingOperationsUnlocked(iter, committed_index);
  if (!status.ok()) {
    return status;
  }

  if (committed_index_changed) {
    *committed_index_changed = true;
  }

  return Status::OK();
}

Status ReplicaState::ApplyPendingOperationsUnlocked(IndexToRoundMap::iterator iter,
                                                    const OpId &committed_index) {
  DCHECK(update_lock_.is_locked());
  VLOG_WITH_PREFIX_UNLOCKED(1) << "Last triggered apply was: "
      <<  last_committed_index_.ShortDebugString()
      << " Starting to apply from log index: " << (*iter).first;

  // Stop at the operation after the last one we must commit. This iterator by definition points to
  // the first entry greater than the committed index, so the entry preceding that must have the
  // OpId equal to commited_index.
  IndexToRoundMap::iterator end_iter;
  auto status = CheckOperationExist(committed_index, &end_iter);
  if (!status.ok()) {
    return status;
  }

  OpId prev_id = last_committed_index_;

  while (iter != end_iter) {
    scoped_refptr<ConsensusRound> round = (*iter).second; // Make a copy.
    DCHECK(round);
    const OpId& current_id = round->id();

    if (PREDICT_TRUE(!OpIdEquals(prev_id, MinimumOpId()))) {
      CHECK_OK(CheckOpInSequence(prev_id, current_id));
    }

    pending_txns_.erase(iter++);
    // Set committed configuration.
    if (PREDICT_FALSE(round->replicate_msg()->op_type() == CHANGE_CONFIG_OP)) {
      DCHECK(round->replicate_msg()->change_config_record().has_old_config());
      DCHECK(round->replicate_msg()->change_config_record().has_new_config());
      RaftConfigPB old_config = round->replicate_msg()->change_config_record().old_config();
      RaftConfigPB new_config = round->replicate_msg()->change_config_record().new_config();
      DCHECK(old_config.has_opid_index());
      DCHECK(!new_config.has_opid_index());

      if (PREDICT_FALSE(FLAGS_inject_delay_commit_pre_voter_to_voter_secs)) {
        bool is_transit_to_voter =
          CountVotersInTransition(old_config) > CountVotersInTransition(new_config);
        if (is_transit_to_voter) {
          LOG_WITH_PREFIX_UNLOCKED(INFO) << "Commit skipped "
              << " as inject_delay_commit_pre_voter_to_voter_secs flag is set to true.\n"
              << "  Old config: { " << old_config.ShortDebugString() << " }.\n"
              << "  New config: { " << new_config.ShortDebugString() << " }";
          SleepFor(MonoDelta::FromSeconds(FLAGS_inject_delay_commit_pre_voter_to_voter_secs));
        }
      }

      new_config.set_opid_index(current_id.index());
      // Check if the pending Raft config has an OpId less than the committed
      // config. If so, this is a replay at startup in which the COMMIT
      // messages were delayed.
      const RaftConfigPB& committed_config = GetCommittedConfigUnlocked();
      if (new_config.opid_index() > committed_config.opid_index()) {
        LOG_WITH_PREFIX_UNLOCKED(INFO) << "Committing config change with OpId "
            << current_id << ". Old config: { " << old_config.ShortDebugString() << " }. "
            << "New config: { " << new_config.ShortDebugString() << " }";
        CHECK_OK(SetCommittedConfigUnlocked(new_config));
      } else {
        LOG_WITH_PREFIX_UNLOCKED(INFO) << "Ignoring commit of config change with OpId "
            << current_id << " because the committed config has OpId index "
            << committed_config.opid_index() << ". The config change we are ignoring is: "
            << "Old config: { " << old_config.ShortDebugString() << " }. "
            << "New config: { " << new_config.ShortDebugString() << " }";
      }
    }

    prev_id.CopyFrom(round->id());
    round->NotifyReplicationFinished(Status::OK());
  }

  SetLastCommittedIndexUnlocked(committed_index);

  return Status::OK();
}

const OpId& ReplicaState::GetCommittedOpIdUnlocked() const {
  DCHECK(update_lock_.is_locked());
  return last_committed_index_;
}

bool ReplicaState::AreCommittedAndCurrentTermsSameUnlocked() const {
  int64_t term = GetCurrentTermUnlocked();
  const OpId &opid = GetCommittedOpIdUnlocked();
  if (opid.term() != term) {
    LOG(INFO) << "committed term=" << opid.term() << ", current term=" << term;
    return false;
  }
  return true;
}

void ReplicaState::UpdateLastReceivedOpIdUnlocked(const OpId& op_id) {
  DCHECK(update_lock_.is_locked());
  auto* trace = Trace::CurrentTrace();
  DCHECK_LE(OpIdCompare(last_received_op_id_, op_id), 0)
      << "Previously received OpId: " << last_received_op_id_.ShortDebugString()
      << ", updated OpId: " << op_id.ShortDebugString()
      << ", Trace:" << std::endl << (trace ? trace->DumpToString(true) : "No trace found");

  last_received_op_id_ = op_id;
  last_received_op_id_current_leader_ = last_received_op_id_;
  next_index_ = op_id.index() + 1;
}

const OpId& ReplicaState::GetLastReceivedOpIdUnlocked() const {
  DCHECK(update_lock_.is_locked());
  return last_received_op_id_;
}

const OpId& ReplicaState::GetLastReceivedOpIdCurLeaderUnlocked() const {
  DCHECK(update_lock_.is_locked());
  return last_received_op_id_current_leader_;
}

OpId ReplicaState::GetLastPendingTransactionOpIdUnlocked() const {
  DCHECK(update_lock_.is_locked());
  return pending_txns_.empty()
      ? MinimumOpId() : (--pending_txns_.end())->second->id();
}

void ReplicaState::NewIdUnlocked(OpId* id) {
  DCHECK(update_lock_.is_locked());
  id->set_term(GetCurrentTermUnlocked());
  id->set_index(next_index_++);
}

void ReplicaState::CancelPendingOperation(const OpId& id) {
  OpId previous = id;
  previous.set_index(previous.index() - 1);
  DCHECK(update_lock_.is_locked());
  CHECK_EQ(GetCurrentTermUnlocked(), id.term());
  CHECK_EQ(next_index_, id.index() + 1);
  next_index_ = id.index();

  // We don't use UpdateLastReceivedOpIdUnlocked because we're actually
  // updating it back to a lower value and we need to avoid the checks
  // that method has.

  // This is only ok if we do _not_ release the lock after calling
  // NewIdUnlocked() (which we don't in RaftConsensus::Replicate()).
  last_received_op_id_ = previous;
  scoped_refptr<ConsensusRound> round = EraseKeyReturnValuePtr(&pending_txns_, id.index());
  DCHECK(round);
}

string ReplicaState::LogPrefix() {
  ReplicaState::UniqueLock lock;
  CHECK_OK(LockForRead(&lock));
  return LogPrefixUnlocked();
}

string ReplicaState::LogPrefixUnlocked() const {
  DCHECK(update_lock_.is_locked());
  return Substitute("T $0 P $1 [term $2 $3]: ",
                    options_.tablet_id,
                    peer_uuid_,
                    GetCurrentTermUnlocked(),
                    RaftPeerPB::Role_Name(GetActiveRoleUnlocked()));
}

string ReplicaState::LogPrefixThreadSafe() const {
  return Substitute("T $0 P $1: ",
                    options_.tablet_id,
                    peer_uuid_);
}

ReplicaState::State ReplicaState::state() const {
  DCHECK(update_lock_.is_locked());
  return state_;
}

std::pair<RaftPeerPB::Role, int64_t> ReplicaState::GetRoleAndTerm() const {
  const auto packed_role_and_term = role_and_term_.load(std::memory_order_acquire);
  return std::make_pair(UnpackRole(packed_role_and_term), UnpackTerm(packed_role_and_term));
}

string ReplicaState::ToString() const {
  ThreadRestrictions::AssertWaitAllowed();
  ReplicaState::UniqueLock lock(update_lock_);
  return ToStringUnlocked();
}

string ReplicaState::ToStringUnlocked() const {
  DCHECK(update_lock_.is_locked());
  string ret;
  SubstituteAndAppend(&ret, "Replica: $0, State: $1, Role: $2\n",
                      peer_uuid_, state_,
                      RaftPeerPB::Role_Name(GetActiveRoleUnlocked()));

  SubstituteAndAppend(&ret, "Watermarks: {Received: $0 Committed: $1} Leader: $2\n",
                      last_received_op_id_.ShortDebugString(),
                      last_committed_index_.ShortDebugString(),
                      last_received_op_id_current_leader_.ShortDebugString());
  return ret;
}

Status ReplicaState::CheckOpInSequence(const OpId& previous, const OpId& current) {
  if (current.term() < previous.term()) {
    return STATUS(Corruption, Substitute("New operation's term is not >= than the previous "
        "op's term. Current: $0. Previous: $1", OpIdToString(current), OpIdToString(previous)));
  }

  if (current.index() != previous.index() + 1) {
    return STATUS(Corruption, Substitute("New operation's index does not follow the previous"
        " op's index. Current: $0. Previous: $1", OpIdToString(current), OpIdToString(previous)));
  }
  return Status::OK();
}

void ReplicaState::UpdateOldLeaderLeaseExpirationUnlocked(MonoDelta lease_duration) {
  old_leader_lease_expiration_.MakeAtLeast(MonoTime::FineNow() + lease_duration);
}

void ReplicaState::UpdateOldLeaderLeaseExpirationUnlocked(MonoTime lease_expiration) {
  old_leader_lease_expiration_.MakeAtLeast(lease_expiration);
}

LeaderLeaseStatus ReplicaState::GetLeaderLeaseStatusUnlocked(
    MonoDelta* remaining_old_leader_lease) const {
  DCHECK_EQ(GetActiveRoleUnlocked(), RaftPeerPB_Role_LEADER);

  if (remaining_old_leader_lease) {
    *remaining_old_leader_lease = MonoDelta();
  }

  if (!FLAGS_use_leader_leases) {
    return LeaderLeaseStatus::HAS_LEASE;
  }

  if (GetActiveConfigUnlocked().peers_size() == 1) {
    // It is OK that majority_replicated_lease_expiration_ might be undefined in this case, because
    // we are only reading it in this function (as of 08/09/2017).
    return LeaderLeaseStatus::HAS_LEASE;
  }

  // We will query the current time at most once in this function, and only if necessary.
  MonoTime now;

  if (old_leader_lease_expiration_) {
    const auto remaining_old_leader_lease_duration = RemainingOldLeaderLeaseDuration(&now);
    if (remaining_old_leader_lease_duration) {
      if (remaining_old_leader_lease) {
        *remaining_old_leader_lease = remaining_old_leader_lease_duration;
      }
      return LeaderLeaseStatus::OLD_LEADER_MAY_HAVE_LEASE;
    }
  }

  if (!majority_replicated_lease_expiration_) {
    return LeaderLeaseStatus::NO_MAJORITY_REPLICATED_LEASE;
  }

  if (!now) {
    now = MonoTime::FineNow();
  }

  if (now >= majority_replicated_lease_expiration_) {
    return LeaderLeaseStatus::NO_MAJORITY_REPLICATED_LEASE;
  } else {
    return LeaderLeaseStatus::HAS_LEASE;
  }
}

MonoDelta ReplicaState::RemainingOldLeaderLeaseDuration(MonoTime* now) const {
  MonoTime now_local;
  if (!now) {
    now = &now_local;
  }
  *now = MonoTime::FineNow();

  if (old_leader_lease_expiration_) {
    if (*now > old_leader_lease_expiration_) {
      // Reset the old leader lease expiration time so that we don't have to check it anymore.
      old_leader_lease_expiration_ = MonoTime::kMin;
      return MonoDelta();
    } else {
      return old_leader_lease_expiration_.GetDeltaSince(*now);
    }
  }
  return MonoDelta();
}

}  // namespace consensus
}  // namespace yb
