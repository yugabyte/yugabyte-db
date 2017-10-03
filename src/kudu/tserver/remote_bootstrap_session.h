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
#ifndef KUDU_TSERVER_REMOTE_BOOTSTRAP_SESSION_H_
#define KUDU_TSERVER_REMOTE_BOOTSTRAP_SESSION_H_

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "kudu/consensus/log_anchor_registry.h"
#include "kudu/consensus/log_util.h"
#include "kudu/consensus/metadata.pb.h"
#include "kudu/consensus/opid_util.h"
#include "kudu/fs/block_id.h"
#include "kudu/fs/block_manager.h"
#include "kudu/gutil/macros.h"
#include "kudu/gutil/ref_counted.h"
#include "kudu/gutil/stl_util.h"
#include "kudu/tserver/remote_bootstrap.pb.h"
#include "kudu/util/env_util.h"
#include "kudu/util/locks.h"
#include "kudu/util/status.h"

namespace kudu {

class FsManager;

namespace tablet {
class TabletPeer;
} // namespace tablet

namespace tserver {

class TabletPeerLookupIf;

// Caches file size and holds a shared_ptr reference to a RandomAccessFile.
// Assumes that the file underlying the RandomAccessFile is immutable.
struct ImmutableRandomAccessFileInfo {
  std::shared_ptr<RandomAccessFile> readable;
  int64_t size;

  ImmutableRandomAccessFileInfo(std::shared_ptr<RandomAccessFile> readable,
                                int64_t size)
      : readable(std::move(readable)), size(size) {}

  Status ReadFully(uint64_t offset, int64_t size, Slice* data, uint8_t* scratch) const {
    return env_util::ReadFully(readable.get(), offset, size, data, scratch);
  }
};

// Caches block size and holds an exclusive reference to a ReadableBlock.
// Assumes that the block underlying the ReadableBlock is immutable.
struct ImmutableReadableBlockInfo {
  gscoped_ptr<fs::ReadableBlock> readable;
  int64_t size;

  ImmutableReadableBlockInfo(fs::ReadableBlock* readable,
                             int64_t size)
  : readable(readable),
    size(size) {
  }

  Status ReadFully(uint64_t offset, int64_t size, Slice* data, uint8_t* scratch) const {
    return readable->Read(offset, size, data, scratch);
  }
};

// A potential Learner must establish a RemoteBootstrapSession with the leader in order
// to fetch the needed superblock, blocks, and log segments.
// This class is refcounted to make it easy to remove it from the session map
// on expiration while it is in use by another thread.
class RemoteBootstrapSession : public RefCountedThreadSafe<RemoteBootstrapSession> {
 public:
  RemoteBootstrapSession(const scoped_refptr<tablet::TabletPeer>& tablet_peer,
                         std::string session_id, std::string requestor_uuid,
                         FsManager* fs_manager);

  // Initialize the session, including anchoring files (TODO) and fetching the
  // tablet superblock and list of WAL segments.
  Status Init();

  // Return ID of tablet corresponding to this session.
  const std::string& tablet_id() const;

  // Return UUID of the requestor that initiated this session.
  const std::string& requestor_uuid() const;

  // Open block for reading, if it's not already open, and read some of it.
  // If maxlen is 0, we use a system-selected length for the data piece.
  // *data is set to a std::string containing the data. Ownership of this object
  // is passed to the caller. A string is used because the RPC interface is
  // sending data serialized as protobuf and we want to minimize copying.
  // On error, Status is set to a non-OK value and error_code is filled in.
  //
  // This method is thread-safe.
  Status GetBlockPiece(const BlockId& block_id,
                       uint64_t offset, int64_t client_maxlen,
                       std::string* data, int64_t* block_file_size,
                       RemoteBootstrapErrorPB::Code* error_code);

  // Get a piece of a log segment.
  // The behavior and params are very similar to GetBlockPiece(), but this one
  // is only for sending WAL segment files.
  Status GetLogSegmentPiece(uint64_t segment_seqno,
                            uint64_t offset, int64_t client_maxlen,
                            std::string* data, int64_t* log_file_size,
                            RemoteBootstrapErrorPB::Code* error_code);

  const tablet::TabletSuperBlockPB& tablet_superblock() const { return tablet_superblock_; }

  const consensus::ConsensusStatePB& initial_committed_cstate() const {
    return initial_committed_cstate_;
  }

  const log::SegmentSequence& log_segments() const { return log_segments_; }

  // Check if a block is currently open.
  bool IsBlockOpenForTests(const BlockId& block_id) const;

 private:
  friend class RefCountedThreadSafe<RemoteBootstrapSession>;

  typedef std::unordered_map<BlockId, ImmutableReadableBlockInfo*, BlockIdHash> BlockMap;
  typedef std::unordered_map<uint64_t, ImmutableRandomAccessFileInfo*> LogMap;

  ~RemoteBootstrapSession();

  // Open the block and add it to the block map.
  Status OpenBlockUnlocked(const BlockId& block_id);

  // Look up cached block information.
  Status FindBlock(const BlockId& block_id,
                   ImmutableReadableBlockInfo** block_info,
                   RemoteBootstrapErrorPB::Code* error_code);

  // Snapshot the log segment's length and put it into segment map.
  Status OpenLogSegmentUnlocked(uint64_t segment_seqno);

  // Look up log segment in cache or log segment map.
  Status FindLogSegment(uint64_t segment_seqno,
                        ImmutableRandomAccessFileInfo** file_info,
                        RemoteBootstrapErrorPB::Code* error_code);

  // Unregister log anchor, if it's registered.
  Status UnregisterAnchorIfNeededUnlocked();

  scoped_refptr<tablet::TabletPeer> tablet_peer_;
  const std::string session_id_;
  const std::string requestor_uuid_;
  FsManager* const fs_manager_;

  mutable simple_spinlock session_lock_;

  BlockMap blocks_; // Protected by session_lock_.
  LogMap logs_;     // Protected by session_lock_.
  ValueDeleter blocks_deleter_;
  ValueDeleter logs_deleter_;

  tablet::TabletSuperBlockPB tablet_superblock_;

  consensus::ConsensusStatePB initial_committed_cstate_;

  // The sequence of log segments that will be sent in the course of this
  // session.
  log::SegmentSequence log_segments_;

  log::LogAnchor log_anchor_;

  DISALLOW_COPY_AND_ASSIGN(RemoteBootstrapSession);
};

} // namespace tserver
} // namespace kudu

#endif // KUDU_TSERVER_REMOTE_BOOTSTRAP_SESSION_H_
