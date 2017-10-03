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
#ifndef KUDU_CONSENSUS_LOCAL_CONSENSUS_H_
#define KUDU_CONSENSUS_LOCAL_CONSENSUS_H_

#include <boost/thread/locks.hpp>
#include <string>
#include <vector>

#include "kudu/consensus/consensus.h"
#include "kudu/consensus/consensus_meta.h"
#include "kudu/util/locks.h"

namespace kudu {

class FsManager;

namespace metadata {
class TabletServerPB;
}

namespace consensus {

// Local implementation of Consensus. This is mostly for testing purposes/
// using in single node configurations if/when applicable.
//
// NOTE: While this implementation has a lot less overhead running on a single
// node than a true consensus implementation in the same situation, this
// implementation will not be able to be reconfigured to accept more nodes
// while a true consensus implementation will.
//
// This class is not thread safe.
class LocalConsensus : public Consensus {
 public:
  explicit LocalConsensus(ConsensusOptions options,
                          gscoped_ptr<ConsensusMetadata> cmeta,
                          std::string peer_uuid,
                          const scoped_refptr<server::Clock>& clock,
                          ReplicaTransactionFactory* txn_factory,
                          log::Log* log);

  virtual Status Start(const ConsensusBootstrapInfo& info) OVERRIDE;

  virtual bool IsRunning() const OVERRIDE;

  virtual Status EmulateElection() OVERRIDE { return Status::OK(); }

  virtual Status StartElection(ElectionMode mode) OVERRIDE { return Status::OK(); }

  virtual Status Replicate(const scoped_refptr<ConsensusRound>& context) OVERRIDE;

  virtual RaftPeerPB::Role role() const OVERRIDE;

  virtual std::string peer_uuid() const OVERRIDE {
    return peer_uuid_;
  }

  virtual std::string tablet_id() const OVERRIDE {
    return options_.tablet_id;
  }

  virtual ConsensusStatePB ConsensusState(ConsensusConfigType type) const OVERRIDE;

  virtual RaftConfigPB CommittedConfig() const OVERRIDE;

  virtual void Shutdown() OVERRIDE;

  virtual void DumpStatusHtml(std::ostream& out) const OVERRIDE;

  //
  //  NOT IMPLEMENTED IN LOCAL CONSENSUS
  //
  virtual Status Update(const ConsensusRequestPB* request,
                        ConsensusResponsePB* response) OVERRIDE;

  virtual Status RequestVote(const VoteRequestPB* request,
                             VoteResponsePB* response) OVERRIDE;

 private:
  // Log prefix. Doesn't access any variables that require locking.
  std::string LogPrefix() const;

  // Resubmit the operations in 'replicates' to be applied immediately to
  // the tablet.
  //
  // This is used to re-apply operations which were found in the WAL at startup,
  // but did not have associated COMMIT records.
  Status ResubmitOrphanedReplicates(const std::vector<ReplicateMsg*> replicates);

  const std::string peer_uuid_;
  const ConsensusOptions options_;
  const gscoped_ptr<ConsensusMetadata> cmeta_;
  ReplicaTransactionFactory* const txn_factory_;
  log::Log* const log_;
  const scoped_refptr<server::Clock> clock_;

  // Protects 'state_' and 'next_op_id_index_'.
  mutable simple_spinlock lock_;

  State state_;
  int64 next_op_id_index_;

  DISALLOW_COPY_AND_ASSIGN(LocalConsensus);
};

} // namespace consensus
} // namespace kudu

#endif /* KUDU_CONSENSUS_LOCAL_CONSENSUS_H_ */
