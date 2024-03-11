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
#pragma once

#include <stdint.h>

#include <atomic>
#include <optional>
#include <string>

#include "yb/common/common_types.pb.h"
#include "yb/common/entity_ids_types.h"
#include "yb/common/opid.h"

#include "yb/consensus/metadata.pb.h"

#include "yb/gutil/macros.h"

#include "yb/util/status_fwd.h"

namespace yb {

class FsManager;
class ServerRegistrationPB;

struct CloneSourceInfo {
  uint32_t seq_no;
  TabletId tablet_id;
};

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
  static Result<std::unique_ptr<ConsensusMetadata>> Create(FsManager* fs_manager,
                               const std::string& tablet_id,
                               const std::string& peer_uuid,
                               const RaftConfigPB& config,
                               int64_t current_term);

  // Load a ConsensusMetadata object from disk.
  // Returns Status::NotFound if the file could not be found. May return other
  // Status codes if unable to read the file.
  static Status Load(FsManager* fs_manager,
                             const std::string& tablet_id,
                             const std::string& peer_uuid,
                             std::unique_ptr<ConsensusMetadata>* cmeta);

  // Delete the ConsensusMetadata file associated with the given tablet from
  // disk.
  static Status DeleteOnDiskData(FsManager* fs_manager, const std::string& tablet_id);

  // Accessors for current term.
  int64_t current_term() const;
  void set_current_term(int64_t term);

  // Accessors for voted_for.
  bool has_voted_for() const;
  const std::string& voted_for() const;
  void clear_voted_for();
  void set_voted_for(const std::string& uuid);

  // Accessors for committed configuration.
  const RaftConfigPB& committed_config() const;
  void set_committed_config(const RaftConfigPB& config);

  // Accessors for split_parent_tablet_id.
  bool has_split_parent_tablet_id() const;
  const TabletId& split_parent_tablet_id() const;
  void set_split_parent_tablet_id(const TabletId& split_parent_tablet_id);

  // CloneSourceInfo contains info about the clone request that created this tablet.
  const std::optional<CloneSourceInfo> clone_source_info() const;
  void set_clone_source_info(uint32_t seq_no, const TabletId& tablet_id);

  // Returns whether a pending configuration is set.
  bool has_pending_config() const;

  // Returns the pending configuration if one is set. Otherwise, fires a DCHECK.
  const RaftConfigPB& pending_config() const;

  // Set & clear the pending configuration.
  void clear_pending_config();
  void set_pending_config(const RaftConfigPB& config, const OpId& config_op_id);
  Status set_pending_config_op_id(const OpId& config_op_id);

  OpId pending_config_op_id() { return pending_config_op_id_; }

  // If a pending configuration is set, return it.
  // Otherwise, return the committed configuration.
  const RaftConfigPB& active_config() const;

  // Accessors for setting the active leader.
  const std::string& leader_uuid() const;
  void set_leader_uuid(const std::string& uuid);
  void clear_leader_uuid();

  const TabletId& tablet_id() { return tablet_id_; }

  void set_tablet_id(const TabletId& tablet_id) { tablet_id_ = tablet_id; }

  // Returns the currently active role of the current node.
  PeerRole active_role() const;

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

  // The on-disk size of the consensus metadata, as of the last call to Load() or Flush().
  int64_t on_disk_size() const {
    return on_disk_size_.load(std::memory_order_acquire);
  }

  // A lock-free way to read role and term atomically.
  std::pair<PeerRole, int64_t> GetRoleAndTerm() const;

  // Used internally for storing the role + term combination atomically.
  using PackedRoleAndTerm = uint64;

  const ConsensusMetadataPB& GetConsensusMetadataPB() const {
    return pb_;
  }

 private:
  ConsensusMetadata(FsManager* fs_manager, std::string tablet_id,
                    std::string peer_uuid);

  std::string LogPrefix() const;

  // Updates the cached active role.
  void UpdateActiveRole();

  // Updates the cached on-disk size of the consensus metadata.
  Status UpdateOnDiskSize();

  void UpdateRoleAndTermCache();

  // Transient fields.
  // Constants:
  FsManager* const fs_manager_;
  std::string tablet_id_;
  const std::string peer_uuid_;
  // Mutable:
  std::string leader_uuid_; // Leader of the current term (term == pb_.current_term).
  bool has_pending_config_; // Indicates whether there is an as-yet uncommitted
                            // configuration change pending.
  // RaftConfig used by the peers when there is a pending config change operation.
  RaftConfigPB pending_config_;
  OpId pending_config_op_id_;

  // Cached role of the peer_uuid_ within the active configuration.
  PeerRole active_role_;

  // Durable fields.
  ConsensusMetadataPB pb_;

  // The on-disk size of the consensus metadata, as of the last call to Load() or Flush().
  std::atomic<uint64_t> on_disk_size_;

  // Active role and term. Stored as a separate atomic field for fast read-only access. This is
  // still only modified under the lock.
  std::atomic<PackedRoleAndTerm> role_and_term_cache_;

  DISALLOW_COPY_AND_ASSIGN(ConsensusMetadata);
};

const HostPortPB& DesiredHostPort(const RaftPeerPB& peer, const CloudInfoPB& from);
void TakeRegistration(ServerRegistrationPB* source, RaftPeerPB* dest);
void CopyRegistration(ServerRegistrationPB source, RaftPeerPB* dest);

} // namespace consensus
} // namespace yb
