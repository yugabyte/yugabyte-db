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

#include <array>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "yb/consensus/log_anchor_registry.h"
#include "yb/consensus/metadata.pb.h"

#include "yb/gutil/macros.h"
#include "yb/gutil/ref_counted.h"

#include "yb/tablet/tablet_fwd.h"
#include "yb/tserver/remote_bootstrap_anchor_client.h"
#include "yb/tserver/remote_bootstrap.pb.h"
#include "yb/tserver/remote_bootstrap.proxy.h"

#include "yb/util/status_fwd.h"
#include "yb/util/stopwatch.h"
#include "yb/util/locks.h"
#include "yb/util/net/rate_limiter.h"

namespace yb {

class Env;
class FsManager;
class RandomAccessFile;

namespace tablet {
class TabletPeer;
} // namespace tablet

namespace tserver {

class TabletPeerLookupIf;

struct GetDataPieceInfo {
  // Input
  uint64_t offset;
  int64_t client_maxlen;

  // Output
  std::string data;
  uint64_t data_size;
  RemoteBootstrapErrorPB::Code error_code;

  int64_t bytes_remaining() const {
    return data_size - offset;
  }
};

class RemoteBootstrapSource {
 public:
  virtual Status Init() = 0;
  virtual Status ValidateDataId(const DataIdPB& data_id) = 0;
  virtual Status GetDataPiece(const DataIdPB& data_id, GetDataPieceInfo* info) = 0;

  virtual ~RemoteBootstrapSource() = default;
};

// A potential Learner must establish a RemoteBootstrapSession with the leader in order
// to fetch the needed superblock, blocks, and log segments.
// This class is refcounted to make it easy to remove it from the session map
// on expiration while it is in use by another thread.
class RemoteBootstrapSession : public RefCountedThreadSafe<RemoteBootstrapSession> {
 public:
  RemoteBootstrapSession(const std::shared_ptr<tablet::TabletPeer>& tablet_peer,
                         std::string session_id, std::string requestor_uuid,
                         const std::atomic<int>* nsessions,
                         const scoped_refptr<RemoteBootstrapAnchorClient>&
                            rbs_anchor_client = nullptr);

  // Initialize the session, including anchoring files (TODO) and fetching the
  // tablet superblock and list of WAL segments.
  Status InitBootstrapSession();

  // Initialize the session: fetching superblock.
  Status InitSnapshotTransferSession();

  // Return ID of tablet corresponding to this session.
  const std::string& tablet_id() const;

  // Return UUID of the requestor that initiated this session.
  const std::string& requestor_uuid() const;

  // Return ID of session created.
  const std::string& session_id() const { return session_id_; }

  std::string LogPrefix() const {
    return Format("$0: ", session_id());
  }

  Status GetDataPiece(const DataIdPB& data_id, GetDataPieceInfo* info);

  Status ValidateDataId(const DataIdPB& data_id);

  MonoTime start_time() { return start_time_; }

  const tablet::RaftGroupReplicaSuperBlockPB& tablet_superblock() const {
    return tablet_superblock_; }

  const consensus::ConsensusStatePB& initial_committed_cstate() const {
    return initial_committed_cstate_;
  }

  const log::SegmentSequence& log_segments() const { return log_segments_; }

  void SetSuccess();

  bool Succeeded();

  bool ShouldChangeRole();

  // Change the peer's role to VOTER.
  Status ChangeRole();

  void InitRateLimiter();

  void EnsureRateLimiterIsInitialized();

  RateLimiter& rate_limiter() { return rate_limiter_; }

  Stopwatch& crc_compute_timer() { return crc_compute_timer_; }
  Stopwatch& data_read_timer() { return data_read_timer_; }

  static const std::string kCheckpointsDir;

  // Get a piece of a RocksDB file.
  // The behavior and params are very similar to GetLogSegmentPiece(), but this one
  // is only for sending rocksdb files.
  static Status GetFilePiece(
      const std::string& path, const std::string& file_name, Env* env, GetDataPieceInfo* info);

  // Refresh the Log Anchor Session with the leader when rbs_anchor_client != nullptr and
  // rbs_anchor_session_created_ is set.
  Status RefreshRemoteLogAnchorSessionAsync();

  bool has_retryable_requests_file() const {
    return retryable_requests_filepath_.has_value();
  }

 private:
  friend class RefCountedThreadSafe<RemoteBootstrapSession>;

  FRIEND_TEST(RemoteBootstrapRocksDBTest, TestCheckpointDirectory);
  FRIEND_TEST(RemoteBootstrapRocksDBTest, CheckSuperBlockHasRocksDBFields);
  FRIEND_TEST(RemoteBootstrapRocksDBTest, CheckSuperBlockHasSnapshotFields);
  FRIEND_TEST(RemoteBootstrapRocksDBTest, TestNonExistentRocksDBFile);

  virtual ~RemoteBootstrapSession();

  template <class Source>
  void AddSource() {
    sources_[Source::id_type()] = std::make_unique<Source>(tablet_peer_, &tablet_superblock_);
  }

  Status ReadSuperblockFromDisk(tablet::RaftGroupReplicaSuperBlockPB* out = nullptr);

  Result<tablet::TabletPtr> GetRunningTablet();

  Status InitSources();

  // Snapshot the log segment's length and put it into segment map.
  Status OpenLogSegment(uint64_t segment_seqno, RemoteBootstrapErrorPB::Code* error_code)
      REQUIRES(mutex_);

  // Unregister log anchor, if it's registered.
  Status UnregisterAnchorIfNeededUnlocked();

  // Helper API to set initial_committed_cstate_.
  Status SetInitialCommittedState();

  // Get a piece of a log segment.
  // If maxlen is 0, we use a system-selected length for the data piece.
  // *data is set to a std::string containing the data. Ownership of this object
  // is passed to the caller. A string is used because the RPC interface is
  // sending data serialized as protobuf and we want to minimize copying.
  // On error, Status is set to a non-OK value and error_code is filled in.
  //
  // This method is thread-safe.
  Status GetLogSegmentPiece(uint64_t segment_seqno, GetDataPieceInfo* info);

  // Get a piece of a RocksDB checkpoint file.
  Status GetRocksDBFilePiece(const std::string& file_name, GetDataPieceInfo* info);

  // Get a piece of a retryable requests file.
  Status GetRetryableRequestsFilePiece(GetDataPieceInfo* info);

  Env* env() const;

  RemoteBootstrapSource* Source(DataIdPB::IdType id_type) const;

  Result<OpId> CreateSnapshot(int retry);

  // When a follower peer is serving as the rbs source, try registering a log anchor
  // at remote_log_anchor_index_ on the leader peer.
  Status RegisterRemoteLogAnchorUnlocked() REQUIRES(mutex_);
  // When a follower peer is serving as the rbs source, try updating the log anchor
  // to remote_log_anchor_index_ on the leader peer.
  Status UpdateRemoteLogAnchorUnlocked() REQUIRES(mutex_);
  Status UnregisterRemotelogAnchor();

  std::shared_ptr<tablet::TabletPeer> tablet_peer_;
  const std::string session_id_;
  const std::string requestor_uuid_;

  mutable std::mutex mutex_;

  std::shared_ptr<RandomAccessFile> opened_log_segment_file_ GUARDED_BY(mutex_);
  int64_t opened_log_segment_file_size_ GUARDED_BY(mutex_) = -1;
  uint64_t opened_log_segment_seqno_ GUARDED_BY(mutex_) = 0;
  bool opened_log_segment_active_ GUARDED_BY(mutex_) = false;

  tablet::RaftGroupReplicaSuperBlockPB tablet_superblock_;

  consensus::ConsensusStatePB initial_committed_cstate_;

  // The sequence of log segments that will be sent in the course of this
  // session.
  log::SegmentSequence log_segments_;

  log::LogAnchor log_anchor_;
  int64_t log_anchor_index_ GUARDED_BY(mutex_) = 0;
  // When a follower peer is serving as the rbs source, the field stores the index at which the
  // leader peer needs to anchor its log at.
  int64_t remote_log_anchor_index_ GUARDED_BY(mutex_) = 0;

  // We need to know whether this ended succesfully before changing the peer's member type from
  // PRE_VOTER to VOTER.
  bool succeeded_ GUARDED_BY(mutex_) = false;

  // Only RemoteBootstraps should try changing the peer's member type. Snapshot transfers should not
  // and should skip that step.
  bool should_try_change_role_ GUARDED_BY(mutex_) = true;

  // Directory where the checkpoint files are stored for this session (only for rocksdb).
  std::string checkpoint_dir_;

  std::optional<std::string> retryable_requests_filepath_;

  // Time when this session was initialized.
  MonoTime start_time_;

  // Stopwatch to capture the latency of different operations
  Stopwatch crc_compute_timer_;
  Stopwatch data_read_timer_;

  // Used to limit the transmission rate.
  RateLimiter rate_limiter_;

  // Pointer to the counter for of the number of sessions in RemoteBootstrapService. Used to
  // calculate the rate for the rate limiter.
  const std::atomic<int>* nsessions_;

  std::array<std::unique_ptr<RemoteBootstrapSource>, DataIdPB::IdType_ARRAYSIZE> sources_;

  scoped_refptr<RemoteBootstrapAnchorClient> rbs_anchor_client_;

  // Boolean that determines if we need to call KeepLogAnchorAlive on rbs_anchor_client_.
  bool rbs_anchor_session_created_ = false;

  DISALLOW_COPY_AND_ASSIGN(RemoteBootstrapSession);
};

} // namespace tserver
} // namespace yb
