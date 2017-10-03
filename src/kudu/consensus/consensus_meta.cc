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
#include "kudu/consensus/consensus_meta.h"

#include "kudu/consensus/log_util.h"
#include "kudu/consensus/metadata.pb.h"
#include "kudu/consensus/opid_util.h"
#include "kudu/consensus/quorum_util.h"
#include "kudu/fs/fs_manager.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/util/logging.h"
#include "kudu/util/pb_util.h"
#include "kudu/util/stopwatch.h"

namespace kudu {
namespace consensus {

using std::string;
using strings::Substitute;

Status ConsensusMetadata::Create(FsManager* fs_manager,
                                 const string& tablet_id,
                                 const std::string& peer_uuid,
                                 const RaftConfigPB& config,
                                 int64_t current_term,
                                 gscoped_ptr<ConsensusMetadata>* cmeta_out) {
  gscoped_ptr<ConsensusMetadata> cmeta(new ConsensusMetadata(fs_manager, tablet_id, peer_uuid));
  cmeta->set_committed_config(config);
  cmeta->set_current_term(current_term);
  RETURN_NOT_OK(cmeta->Flush());
  cmeta_out->swap(cmeta);
  return Status::OK();
}

Status ConsensusMetadata::Load(FsManager* fs_manager,
                               const std::string& tablet_id,
                               const std::string& peer_uuid,
                               gscoped_ptr<ConsensusMetadata>* cmeta_out) {
  gscoped_ptr<ConsensusMetadata> cmeta(new ConsensusMetadata(fs_manager, tablet_id, peer_uuid));
  RETURN_NOT_OK(pb_util::ReadPBContainerFromPath(fs_manager->env(),
                                                 fs_manager->GetConsensusMetadataPath(tablet_id),
                                                 &cmeta->pb_));
  cmeta->UpdateActiveRole(); // Needs to happen here as we sidestep the accessor APIs.
  cmeta_out->swap(cmeta);
  return Status::OK();
}

Status ConsensusMetadata::DeleteOnDiskData(FsManager* fs_manager, const string& tablet_id) {
  string cmeta_path = fs_manager->GetConsensusMetadataPath(tablet_id);
  Env* env = fs_manager->env();
  if (!env->FileExists(cmeta_path)) {
    return Status::OK();
  }
  RETURN_NOT_OK_PREPEND(env->DeleteFile(cmeta_path),
                        "Unable to delete consensus metadata file for tablet " + tablet_id);
  return Status::OK();
}

const int64_t ConsensusMetadata::current_term() const {
  DCHECK(pb_.has_current_term());
  return pb_.current_term();
}

void ConsensusMetadata::set_current_term(int64_t term) {
  DCHECK_GE(term, kMinimumTerm);
  pb_.set_current_term(term);
}

bool ConsensusMetadata::has_voted_for() const {
  return pb_.has_voted_for();
}

const string& ConsensusMetadata::voted_for() const {
  DCHECK(pb_.has_voted_for());
  return pb_.voted_for();
}

void ConsensusMetadata::clear_voted_for() {
  pb_.clear_voted_for();
}

void ConsensusMetadata::set_voted_for(const string& uuid) {
  DCHECK(!uuid.empty());
  pb_.set_voted_for(uuid);
}

const RaftConfigPB& ConsensusMetadata::committed_config() const {
  DCHECK(pb_.has_committed_config());
  return pb_.committed_config();
}

void ConsensusMetadata::set_committed_config(const RaftConfigPB& config) {
  *pb_.mutable_committed_config() = config;
  if (!has_pending_config_) {
    UpdateActiveRole();
  }
}

bool ConsensusMetadata::has_pending_config() const {
  return has_pending_config_;
}

const RaftConfigPB& ConsensusMetadata::pending_config() const {
  DCHECK(has_pending_config_);
  return pending_config_;
}

void ConsensusMetadata::clear_pending_config() {
  has_pending_config_ = false;
  pending_config_.Clear();
  UpdateActiveRole();
}

void ConsensusMetadata::set_pending_config(const RaftConfigPB& config) {
  has_pending_config_ = true;
  pending_config_ = config;
  UpdateActiveRole();
}

const RaftConfigPB& ConsensusMetadata::active_config() const {
  if (has_pending_config_) {
    return pending_config();
  }
  return committed_config();
}

const string& ConsensusMetadata::leader_uuid() const {
  return leader_uuid_;
}

void ConsensusMetadata::set_leader_uuid(const string& uuid) {
  leader_uuid_ = uuid;
  UpdateActiveRole();
}

RaftPeerPB::Role ConsensusMetadata::active_role() const {
  return active_role_;
}

ConsensusStatePB ConsensusMetadata::ToConsensusStatePB(ConsensusConfigType type) const {
  CHECK(type == CONSENSUS_CONFIG_ACTIVE || type == CONSENSUS_CONFIG_COMMITTED)
      << "Unsupported ConsensusConfigType: " << ConsensusConfigType_Name(type) << ": " << type;
  ConsensusStatePB cstate;
  cstate.set_current_term(pb_.current_term());
  if (type == CONSENSUS_CONFIG_ACTIVE) {
    *cstate.mutable_config() = active_config();
    cstate.set_leader_uuid(leader_uuid_);
  } else {
    *cstate.mutable_config() = committed_config();
    // It's possible, though unlikely, that a new node from a pending configuration
    // could be elected leader. Do not indicate a leader in this case.
    if (PREDICT_TRUE(IsRaftConfigVoter(leader_uuid_, cstate.config()))) {
      cstate.set_leader_uuid(leader_uuid_);
    }
  }
  return cstate;
}

void ConsensusMetadata::MergeCommittedConsensusStatePB(const ConsensusStatePB& committed_cstate) {
  if (committed_cstate.current_term() > current_term()) {
    set_current_term(committed_cstate.current_term());
    clear_voted_for();
  }

  set_leader_uuid("");
  set_committed_config(committed_cstate.config());
  clear_pending_config();
}

Status ConsensusMetadata::Flush() {
  SCOPED_LOG_SLOW_EXECUTION_PREFIX(WARNING, 500, LogPrefix(), "flushing consensus metadata");
  // Sanity test to ensure we never write out a bad configuration.
  RETURN_NOT_OK_PREPEND(VerifyRaftConfig(pb_.committed_config(), COMMITTED_QUORUM),
                        "Invalid config in ConsensusMetadata, cannot flush to disk");

  // Create directories if needed.
  string dir = fs_manager_->GetConsensusMetadataDir();
  bool created_dir = false;
  RETURN_NOT_OK_PREPEND(fs_manager_->CreateDirIfMissing(dir, &created_dir),
                        "Unable to create consensus metadata root dir");
  // fsync() parent dir if we had to create the dir.
  if (PREDICT_FALSE(created_dir)) {
    string parent_dir = DirName(dir);
    RETURN_NOT_OK_PREPEND(Env::Default()->SyncDir(parent_dir),
                          "Unable to fsync consensus parent dir " + parent_dir);
  }

  string meta_file_path = fs_manager_->GetConsensusMetadataPath(tablet_id_);
  RETURN_NOT_OK_PREPEND(pb_util::WritePBContainerToPath(
      fs_manager_->env(), meta_file_path, pb_,
      pb_util::OVERWRITE,
      // We use FLAGS_log_force_fsync_all here because the consensus metadata is
      // essentially an extension of the primary durability mechanism of the
      // consensus subsystem: the WAL. Using the same flag ensures that the WAL
      // and the consensus metadata get the same durability guarantees.
      FLAGS_log_force_fsync_all ? pb_util::SYNC : pb_util::NO_SYNC),
          Substitute("Unable to write consensus meta file for tablet $0 to path $1",
                     tablet_id_, meta_file_path));
  return Status::OK();
}

ConsensusMetadata::ConsensusMetadata(FsManager* fs_manager,
                                     std::string tablet_id,
                                     std::string peer_uuid)
    : fs_manager_(CHECK_NOTNULL(fs_manager)),
      tablet_id_(std::move(tablet_id)),
      peer_uuid_(std::move(peer_uuid)),
      has_pending_config_(false) {}

std::string ConsensusMetadata::LogPrefix() const {
  return Substitute("T $0 P $1: ", tablet_id_, peer_uuid_);
}

void ConsensusMetadata::UpdateActiveRole() {
  ConsensusStatePB cstate = ToConsensusStatePB(CONSENSUS_CONFIG_ACTIVE);
  active_role_ = GetConsensusRole(peer_uuid_, cstate);
  VLOG_WITH_PREFIX(1) << "Updating active role to " << RaftPeerPB::Role_Name(active_role_)
                      << ". Consensus state: " << cstate.ShortDebugString();
}

} // namespace consensus
} // namespace kudu
