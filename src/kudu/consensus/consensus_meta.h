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
#ifndef KUDU_CONSENSUS_CONSENSUS_META_H_
#define KUDU_CONSENSUS_CONSENSUS_META_H_

#include <stdint.h>
#include <string>

#include "kudu/consensus/metadata.pb.h"
#include "kudu/gutil/gscoped_ptr.h"
#include "kudu/gutil/macros.h"
#include "kudu/util/status.h"

namespace kudu {

class FsManager;

namespace consensus {

// Provides methods to read, write, and persist consensus-related metadata.
// This partly corresponds to Raft Figure 2's "Persistent state on all servers".
//
// In addition to the persistent state, this class also provides access to some
// transient state. This includes the peer that this node considers to be the
// leader of the configuration, as well as the "pending" configuration, if any.
//
// Conceptually, a pending configuration is one that has been proposed via a config
// change operation (AddServer or RemoveServer from Chapter 4 of Diego Ongaro's
// Raft thesis) but has not yet been committed. According to the above spec,
// as soon as a server hears of a new cluster membership configuration, it must
// be adopted (even prior to be committed).
//
// The data structure difference between a committed configuration and a pending one
// is that opid_index (the index in the log of the committed config change
// operation) is always set in a committed configuration, while it is always unset in
// a pending configuration.
//
// Finally, this class exposes the concept of an "active" configuration, which means
// the pending configuration if a pending configuration is set, otherwise the committed
// configuration.
//
// This class is not thread-safe and requires external synchronization.
class ConsensusMetadata {
 public:
  // Create a ConsensusMetadata object with provided initial state.
  // Encoded PB is flushed to disk before returning.
  static Status Create(FsManager* fs_manager,
                       const std::string& tablet_id,
                       const std::string& peer_uuid,
                       const RaftConfigPB& config,
                       int64_t current_term,
                       gscoped_ptr<ConsensusMetadata>* cmeta);

  // Load a ConsensusMetadata object from disk.
  // Returns Status::NotFound if the file could not be found. May return other
  // Status codes if unable to read the file.
  static Status Load(FsManager* fs_manager,
                     const std::string& tablet_id,
                     const std::string& peer_uuid,
                     gscoped_ptr<ConsensusMetadata>* cmeta);

  // Delete the ConsensusMetadata file associated with the given tablet from
  // disk.
  static Status DeleteOnDiskData(FsManager* fs_manager, const std::string& tablet_id);

  // Accessors for current term.
  const int64_t current_term() const;
  void set_current_term(int64_t term);

  // Accessors for voted_for.
  bool has_voted_for() const;
  const std::string& voted_for() const;
  void clear_voted_for();
  void set_voted_for(const std::string& uuid);

  // Accessors for committed configuration.
  const RaftConfigPB& committed_config() const;
  void set_committed_config(const RaftConfigPB& config);

  // Returns whether a pending configuration is set.
  bool has_pending_config() const;

  // Returns the pending configuration if one is set. Otherwise, fires a DCHECK.
  const RaftConfigPB& pending_config() const;

  // Set & clear the pending configuration.
  void clear_pending_config();
  void set_pending_config(const RaftConfigPB& config);

  // If a pending configuration is set, return it.
  // Otherwise, return the committed configuration.
  const RaftConfigPB& active_config() const;

  // Accessors for setting the active leader.
  const std::string& leader_uuid() const;
  void set_leader_uuid(const std::string& uuid);

  // Returns the currently active role of the current node.
  RaftPeerPB::Role active_role() const;

  // Copy the stored state into a ConsensusStatePB object.
  // To get the active configuration, specify 'type' = ACTIVE.
  // Otherwise, 'type' = COMMITTED will return a version of the
  // ConsensusStatePB using only the committed configuration. In this case, if the
  // current leader is not a member of the committed configuration, then the
  // leader_uuid field of the returned ConsensusStatePB will be cleared.
  ConsensusStatePB ToConsensusStatePB(ConsensusConfigType type) const;

  // Merge the committed consensus state from the source node during remote
  // bootstrap.
  //
  // This method will clear any pending config change, replace the committed
  // consensus config with the one in 'committed_cstate', and clear the
  // currently tracked leader.
  //
  // It will also check whether the current term passed in 'committed_cstate'
  // is greater than the currently recorded one. If so, it will update the
  // local current term to match the passed one and it will clear the voting
  // record for this node. If the current term in 'committed_cstate' is less
  // than the locally recorded term, the locally recorded term and voting
  // record are not changed.
  void MergeCommittedConsensusStatePB(const ConsensusStatePB& committed_cstate);

  // Persist current state of the protobuf to disk.
  Status Flush();

 private:
  ConsensusMetadata(FsManager* fs_manager, std::string tablet_id,
                    std::string peer_uuid);

  std::string LogPrefix() const;

  // Updates the cached active role.
  void UpdateActiveRole();

  // Transient fields.
  // Constants:
  FsManager* const fs_manager_;
  const std::string tablet_id_;
  const std::string peer_uuid_;
  // Mutable:
  std::string leader_uuid_; // Leader of the current term (term == pb_.current_term).
  bool has_pending_config_; // Indicates whether there is an as-yet uncommitted
                            // configuration change pending.
  // RaftConfig used by the peers when there is a pending config change operation.
  RaftConfigPB pending_config_;

  // Cached role of the peer_uuid_ within the active configuration.
  RaftPeerPB::Role active_role_;

  // Durable fields.
  ConsensusMetadataPB pb_;

  DISALLOW_COPY_AND_ASSIGN(ConsensusMetadata);
};

} // namespace consensus
} // namespace kudu

#endif // KUDU_CONSENSUS_CONSENSUS_META_H_
