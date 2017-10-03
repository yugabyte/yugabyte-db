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

#include "kudu/consensus/local_consensus.h"

#include <boost/thread/locks.hpp>
#include <iostream>

#include "kudu/consensus/log.h"
#include "kudu/consensus/quorum_util.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/server/metadata.h"
#include "kudu/server/clock.h"
#include "kudu/util/debug/trace_event.h"
#include "kudu/util/logging.h"
#include "kudu/util/trace.h"

namespace kudu {
namespace consensus {

using base::subtle::Barrier_AtomicIncrement;
using log::Log;
using log::LogEntryBatch;
using strings::Substitute;

LocalConsensus::LocalConsensus(ConsensusOptions options,
                               gscoped_ptr<ConsensusMetadata> cmeta,
                               string peer_uuid,
                               const scoped_refptr<server::Clock>& clock,
                               ReplicaTransactionFactory* txn_factory, Log* log)
    : peer_uuid_(std::move(peer_uuid)),
      options_(std::move(options)),
      cmeta_(cmeta.Pass()),
      txn_factory_(DCHECK_NOTNULL(txn_factory)),
      log_(DCHECK_NOTNULL(log)),
      clock_(clock),
      state_(kInitializing),
      next_op_id_index_(-1) {
  CHECK(cmeta_) << "Passed ConsensusMetadata object is NULL";
}

Status LocalConsensus::Start(const ConsensusBootstrapInfo& info) {
  TRACE_EVENT0("consensus", "LocalConsensus::Start");

  CHECK_EQ(state_, kInitializing);
  LOG_WITH_PREFIX(INFO) << "Starting LocalConsensus...";

  {
    boost::lock_guard<simple_spinlock> lock(lock_);

    const RaftConfigPB& config = cmeta_->committed_config();
    CHECK(config.local()) << "Local consensus must be passed a local config";
    RETURN_NOT_OK_PREPEND(VerifyRaftConfig(config, COMMITTED_QUORUM),
                          "Invalid config found in LocalConsensus::Start()");

    next_op_id_index_ = info.last_id.index() + 1;

    CHECK(config.peers(0).has_permanent_uuid()) << config.ShortDebugString();
    cmeta_->set_leader_uuid(config.peers(0).permanent_uuid());

    RETURN_NOT_OK_PREPEND(ResubmitOrphanedReplicates(info.orphaned_replicates),
                          "Could not restart replicated operations");

    state_ = kRunning;
  }
  TRACE("Consensus started");
  return Status::OK();
}

Status LocalConsensus::ResubmitOrphanedReplicates(const std::vector<ReplicateMsg*> replicates) {
  for (ReplicateMsg* msg : replicates) {
    DCHECK_LT(msg->id().index(), next_op_id_index_)
      << "Orphaned replicate " << OpIdToString(msg->id())
      << " is newer than next op index " << next_op_id_index_;

    LOG_WITH_PREFIX(INFO) << "Resubmitting operation "
                          << OpIdToString(msg->id()) << " after restart";
    ReplicateRefPtr replicate_ptr = make_scoped_refptr_replicate(new ReplicateMsg(*msg));
    scoped_refptr<ConsensusRound> round(new ConsensusRound(this, replicate_ptr));
    RETURN_NOT_OK(txn_factory_->StartReplicaTransaction(round));
    round->NotifyReplicationFinished(Status::OK());
  }
  return Status::OK();
}

bool LocalConsensus::IsRunning() const {
  boost::lock_guard<simple_spinlock> lock(lock_);
  return state_ == kRunning;
}

Status LocalConsensus::Replicate(const scoped_refptr<ConsensusRound>& round) {
  TRACE_EVENT0("consensus", "LocalConsensus::Replicate");
  DCHECK_GE(state_, kConfiguring);

  ReplicateMsg* msg = round->replicate_msg();

  OpId* cur_op_id = DCHECK_NOTNULL(msg)->mutable_id();
  cur_op_id->set_term(0);

  // Pre-cache the ByteSize outside of the lock, since this is somewhat
  // expensive.
  ignore_result(msg->ByteSize());

  LogEntryBatch* reserved_entry_batch;
  {
    boost::lock_guard<simple_spinlock> lock(lock_);

    // create the new op id for the entry.
    cur_op_id->set_index(next_op_id_index_++);
    // Reserve the correct slot in the log for the replication operation.
    // It's important that we do this under the same lock as we generate
    // the op id, so that we log things in-order.
    gscoped_ptr<log::LogEntryBatchPB> entry_batch;
    log::CreateBatchFromAllocatedOperations({ round->replicate_scoped_refptr() }, &entry_batch);

    RETURN_NOT_OK(log_->Reserve(log::REPLICATE, entry_batch.Pass(),
                                &reserved_entry_batch));

    // Local consensus transactions are always committed so we
    // can just persist the configuration, if this is a change config.
    if (round->replicate_msg()->op_type() == CHANGE_CONFIG_OP) {
      RaftConfigPB new_config = round->replicate_msg()->change_config_record().new_config();
      DCHECK(!new_config.has_opid_index());
      new_config.set_opid_index(round->replicate_msg()->id().index());
      cmeta_->set_committed_config(new_config);
      CHECK_OK(cmeta_->Flush());
    }
  }
  // Serialize and mark the message as ready to be appended.
  // When the Log actually fsync()s this message to disk, 'repl_callback'
  // is triggered.
  RETURN_NOT_OK(log_->AsyncAppend(
      reserved_entry_batch,
      Bind(&ConsensusRound::NotifyReplicationFinished, round)));
  return Status::OK();
}

RaftPeerPB::Role LocalConsensus::role() const {
  return RaftPeerPB::LEADER;
}

Status LocalConsensus::Update(const ConsensusRequestPB* request,
                              ConsensusResponsePB* response) {
  return Status::NotSupported("LocalConsensus does not support Update() calls.");
}

Status LocalConsensus::RequestVote(const VoteRequestPB* request,
                                   VoteResponsePB* response) {
  return Status::NotSupported("LocalConsensus does not support RequestVote() calls.");
}

ConsensusStatePB LocalConsensus::ConsensusState(ConsensusConfigType type) const {
  boost::lock_guard<simple_spinlock> lock(lock_);
  return cmeta_->ToConsensusStatePB(type);
}

RaftConfigPB LocalConsensus::CommittedConfig() const {
  boost::lock_guard<simple_spinlock> lock(lock_);
  return cmeta_->committed_config();
}

void LocalConsensus::Shutdown() {
  VLOG_WITH_PREFIX(1) << "LocalConsensus Shutdown!";
}

void LocalConsensus::DumpStatusHtml(std::ostream& out) const {
  out << "<h1>Local Consensus Status</h1>\n";

  boost::lock_guard<simple_spinlock> lock(lock_);
  out << "next op: " << next_op_id_index_;
}

std::string LocalConsensus::LogPrefix() const {
  return Substitute("T $0 P $1: ", options_.tablet_id, peer_uuid_);
}

} // end namespace consensus
} // end namespace kudu
