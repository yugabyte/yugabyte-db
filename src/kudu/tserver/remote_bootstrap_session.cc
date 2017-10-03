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
#include "kudu/tserver/remote_bootstrap_session.h"

#include <algorithm>

#include "kudu/consensus/log.h"
#include "kudu/consensus/log_reader.h"
#include "kudu/fs/block_manager.h"
#include "kudu/gutil/map-util.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/gutil/type_traits.h"
#include "kudu/rpc/transfer.h"
#include "kudu/server/metadata.h"
#include "kudu/tablet/tablet_peer.h"
#include "kudu/util/stopwatch.h"
#include "kudu/util/trace.h"

namespace kudu {
namespace tserver {

using consensus::MinimumOpId;
using consensus::OpId;
using fs::ReadableBlock;
using log::LogAnchorRegistry;
using log::ReadableLogSegment;
using std::shared_ptr;
using strings::Substitute;
using tablet::ColumnDataPB;
using tablet::DeltaDataPB;
using tablet::RowSetDataPB;
using tablet::TabletMetadata;
using tablet::TabletPeer;
using tablet::TabletSuperBlockPB;

RemoteBootstrapSession::RemoteBootstrapSession(
    const scoped_refptr<TabletPeer>& tablet_peer, std::string session_id,
    std::string requestor_uuid, FsManager* fs_manager)
    : tablet_peer_(tablet_peer),
      session_id_(std::move(session_id)),
      requestor_uuid_(std::move(requestor_uuid)),
      fs_manager_(fs_manager),
      blocks_deleter_(&blocks_),
      logs_deleter_(&logs_) {}

RemoteBootstrapSession::~RemoteBootstrapSession() {
  // No lock taken in the destructor, should only be 1 thread with access now.
  CHECK_OK(UnregisterAnchorIfNeededUnlocked());
}

Status RemoteBootstrapSession::Init() {
  // Take locks to support re-initialization of the same session.
  boost::lock_guard<simple_spinlock> l(session_lock_);
  RETURN_NOT_OK(UnregisterAnchorIfNeededUnlocked());

  STLDeleteValues(&blocks_);
  STLDeleteValues(&logs_);
  blocks_.clear();
  logs_.clear();

  const string& tablet_id = tablet_peer_->tablet_id();

  // Prevent log GC while we grab log segments and Tablet metadata.
  string anchor_owner_token = Substitute("RemoteBootstrap-$0", session_id_);
  tablet_peer_->log_anchor_registry()->Register(
      MinimumOpId().index(), anchor_owner_token, &log_anchor_);

  // Read the SuperBlock from disk.
  const scoped_refptr<TabletMetadata>& metadata = tablet_peer_->tablet_metadata();
  RETURN_NOT_OK_PREPEND(metadata->ReadSuperBlockFromDisk(&tablet_superblock_),
                        Substitute("Unable to access superblock for tablet $0",
                                   tablet_id));

  // Anchor the data blocks by opening them and adding them to the cache.
  //
  // All subsequent requests should reuse the opened blocks.
  vector<BlockIdPB> data_blocks;
  TabletMetadata::CollectBlockIdPBs(tablet_superblock_, &data_blocks);
  for (const BlockIdPB& block_id : data_blocks) {
    LOG(INFO) << "Opening block " << block_id.DebugString();
    RETURN_NOT_OK(OpenBlockUnlocked(BlockId::FromPB(block_id)));
  }

  // Get the latest opid in the log at this point in time so we can re-anchor.
  OpId last_logged_opid;
  tablet_peer_->log()->GetLatestEntryOpId(&last_logged_opid);

  // Get the current segments from the log, including the active segment.
  // The Log doesn't add the active segment to the log reader's list until
  // a header has been written to it (but it will not have a footer).
  RETURN_NOT_OK(tablet_peer_->log()->GetLogReader()->GetSegmentsSnapshot(&log_segments_));
  for (const scoped_refptr<ReadableLogSegment>& segment : log_segments_) {
    RETURN_NOT_OK(OpenLogSegmentUnlocked(segment->header().sequence_number()));
  }
  LOG(INFO) << "Got snapshot of " << log_segments_.size() << " log segments";

  // Look up the committed consensus state.
  // We do this after snapshotting the log to avoid a scenario where the latest
  // entry in the log has a term higher than the term stored in the consensus
  // metadata, which will results in a CHECK failure on RaftConsensus init.
  scoped_refptr<consensus::Consensus> consensus = tablet_peer_->shared_consensus();
  if (!consensus) {
    tablet::TabletStatePB tablet_state = tablet_peer_->state();
    return Status::IllegalState(Substitute("Unable to initialize remote bootstrap session "
                                "for tablet $0. Consensus is not available. Tablet state: $1 ($2)",
                                tablet_id, tablet::TabletStatePB_Name(tablet_state), tablet_state));
  }
  initial_committed_cstate_ = consensus->ConsensusState(consensus::CONSENSUS_CONFIG_COMMITTED);

  // Re-anchor on the highest OpId that was in the log right before we
  // snapshotted the log segments. This helps ensure that we don't end up in a
  // remote bootstrap loop due to a follower falling too far behind the
  // leader's log when remote bootstrap is slow. The remote controls when
  // this anchor is released by ending the remote bootstrap session.
  RETURN_NOT_OK(tablet_peer_->log_anchor_registry()->UpdateRegistration(
      last_logged_opid.index(), anchor_owner_token, &log_anchor_));

  return Status::OK();
}

const std::string& RemoteBootstrapSession::tablet_id() const {
  return tablet_peer_->tablet_id();
}

const std::string& RemoteBootstrapSession::requestor_uuid() const {
  return requestor_uuid_;
}

// Determine the length of the data chunk to return to the client.
static int64_t DetermineReadLength(int64_t bytes_remaining, int64_t requested_len) {
  // Determine the size of the chunks we want to read.
  // Choose "system max" as a multiple of typical HDD block size (4K) with 4K to
  // spare for other stuff in the message, like headers, other protobufs, etc.
  const int32_t kSpareBytes = 4096;
  const int32_t kDiskSectorSize = 4096;
  int32_t system_max_chunk_size =
      ((FLAGS_rpc_max_message_size - kSpareBytes) / kDiskSectorSize) * kDiskSectorSize;
  CHECK_GT(system_max_chunk_size, 0) << "rpc_max_message_size is too low to transfer data: "
                                     << FLAGS_rpc_max_message_size;

  // The min of the {requested, system} maxes is the effective max.
  int64_t maxlen = (requested_len > 0) ? std::min<int64_t>(requested_len, system_max_chunk_size) :
                                        system_max_chunk_size;
  return std::min(bytes_remaining, maxlen);
}

// Calculate the size of the data to return given a maximum client message
// length, the file itself, and the offset into the file to be read from.
static Status GetResponseDataSize(int64_t total_size,
                                  uint64_t offset, int64_t client_maxlen,
                                  RemoteBootstrapErrorPB::Code* error_code, int64_t* data_size) {
  // If requested offset is off the end of the data, bail.
  if (offset >= total_size) {
    *error_code = RemoteBootstrapErrorPB::INVALID_REMOTE_BOOTSTRAP_REQUEST;
    return Status::InvalidArgument(
        Substitute("Requested offset ($0) is beyond the data size ($1)",
                   offset, total_size));
  }

  int64_t bytes_remaining = total_size - offset;

  *data_size = DetermineReadLength(bytes_remaining, client_maxlen);
  DCHECK_GT(*data_size, 0);
  if (client_maxlen > 0) {
    DCHECK_LE(*data_size, client_maxlen);
  }

  return Status::OK();
}

// Read a chunk of a file into a buffer.
// data_name provides a string for the block/log to be used in error messages.
template <class Info>
static Status ReadFileChunkToBuf(const Info* info,
                                 uint64_t offset, int64_t client_maxlen,
                                 const string& data_name,
                                 string* data, int64_t* file_size,
                                 RemoteBootstrapErrorPB::Code* error_code) {
  int64_t response_data_size = 0;
  RETURN_NOT_OK_PREPEND(GetResponseDataSize(info->size, offset, client_maxlen, error_code,
                                            &response_data_size),
                        Substitute("Error reading $0", data_name));

  Stopwatch chunk_timer(Stopwatch::THIS_THREAD);
  chunk_timer.start();

  // Writing into a std::string buffer is basically guaranteed to work on C++11,
  // however any modern compiler should be compatible with it.
  // Violates the API contract, but avoids excessive copies.
  data->resize(response_data_size);
  uint8_t* buf = reinterpret_cast<uint8_t*>(const_cast<char*>(data->data()));
  Slice slice;
  Status s = info->ReadFully(offset, response_data_size, &slice, buf);
  if (PREDICT_FALSE(!s.ok())) {
    s = s.CloneAndPrepend(
        Substitute("Unable to read existing file for $0", data_name));
    LOG(WARNING) << s.ToString();
    *error_code = RemoteBootstrapErrorPB::IO_ERROR;
    return s;
  }
  // Figure out if Slice points to buf or if Slice points to the mmap.
  // If it points to the mmap then copy into buf.
  if (slice.data() != buf) {
    memcpy(buf, slice.data(), slice.size());
  }
  chunk_timer.stop();
  TRACE("Remote bootstrap: $0: $1 total bytes read. Total time elapsed: $2",
        data_name, response_data_size, chunk_timer.elapsed().ToString());

  *file_size = info->size;
  return Status::OK();
}

Status RemoteBootstrapSession::GetBlockPiece(const BlockId& block_id,
                                             uint64_t offset, int64_t client_maxlen,
                                             string* data, int64_t* block_file_size,
                                             RemoteBootstrapErrorPB::Code* error_code) {
  ImmutableReadableBlockInfo* block_info;
  RETURN_NOT_OK(FindBlock(block_id, &block_info, error_code));

  RETURN_NOT_OK(ReadFileChunkToBuf(block_info, offset, client_maxlen,
                                   Substitute("block $0", block_id.ToString()),
                                   data, block_file_size, error_code));

  // Note: We do not eagerly close the block, as doing so may delete the
  // underlying data if this was its last reader and it had been previously
  // marked for deletion. This would be a problem for parallel readers in
  // the same session; they would not be able to find the block.

  return Status::OK();
}

Status RemoteBootstrapSession::GetLogSegmentPiece(uint64_t segment_seqno,
                                                  uint64_t offset, int64_t client_maxlen,
                                                  std::string* data, int64_t* block_file_size,
                                                  RemoteBootstrapErrorPB::Code* error_code) {
  ImmutableRandomAccessFileInfo* file_info;
  RETURN_NOT_OK(FindLogSegment(segment_seqno, &file_info, error_code));
  RETURN_NOT_OK(ReadFileChunkToBuf(file_info, offset, client_maxlen,
                                   Substitute("log segment $0", segment_seqno),
                                   data, block_file_size, error_code));

  // Note: We do not eagerly close log segment files, since we share ownership
  // of the LogSegment objects with the Log itself.

  return Status::OK();
}

bool RemoteBootstrapSession::IsBlockOpenForTests(const BlockId& block_id) const {
  boost::lock_guard<simple_spinlock> l(session_lock_);
  return ContainsKey(blocks_, block_id);
}

// Add a file to the cache and populate the given ImmutableRandomAcccessFileInfo
// object with the file ref and size.
template <class Collection, class Key, class Readable>
static Status AddImmutableFileToMap(Collection* const cache,
                                    const Key& key,
                                    const Readable& readable,
                                    uint64_t size) {
  // Sanity check for 0-length files.
  if (size == 0) {
    return Status::Corruption("Found 0-length object");
  }

  // Looks good, add it to the cache.
  typedef typename Collection::mapped_type InfoPtr;
  typedef typename base::remove_pointer<InfoPtr>::type Info;
  InsertOrDie(cache, key, new Info(readable, size));

  return Status::OK();
}

Status RemoteBootstrapSession::OpenBlockUnlocked(const BlockId& block_id) {
  DCHECK(session_lock_.is_locked());

  gscoped_ptr<ReadableBlock> block;
  Status s = fs_manager_->OpenBlock(block_id, &block);
  if (PREDICT_FALSE(!s.ok())) {
    LOG(WARNING) << "Unable to open requested (existing) block file: "
                 << block_id.ToString() << ": " << s.ToString();
    return s.CloneAndPrepend(Substitute("Unable to open block file for block $0",
                                        block_id.ToString()));
  }

  uint64_t size;
  s = block->Size(&size);
  if (PREDICT_FALSE(!s.ok())) {
    return s.CloneAndPrepend("Unable to get size of block");
  }

  s = AddImmutableFileToMap(&blocks_, block_id, block.get(), size);
  if (!s.ok()) {
    s = s.CloneAndPrepend(Substitute("Error accessing data for block $0", block_id.ToString()));
    LOG(DFATAL) << "Data block disappeared: " << s.ToString();
  } else {
    ignore_result(block.release());
  }
  return s;
}

Status RemoteBootstrapSession::FindBlock(const BlockId& block_id,
                                         ImmutableReadableBlockInfo** block_info,
                                         RemoteBootstrapErrorPB::Code* error_code) {
  Status s;
  boost::lock_guard<simple_spinlock> l(session_lock_);
  if (!FindCopy(blocks_, block_id, block_info)) {
    *error_code = RemoteBootstrapErrorPB::BLOCK_NOT_FOUND;
    s = Status::NotFound("Block not found", block_id.ToString());
  }
  return s;
}

Status RemoteBootstrapSession::OpenLogSegmentUnlocked(uint64_t segment_seqno) {
  DCHECK(session_lock_.is_locked());

  scoped_refptr<log::ReadableLogSegment> log_segment;
  int position = -1;
  if (!log_segments_.empty()) {
    position = segment_seqno - log_segments_[0]->header().sequence_number();
  }
  if (position < 0 || position >= log_segments_.size()) {
    return Status::NotFound(Substitute("Segment with sequence number $0 not found",
                                       segment_seqno));
  }
  log_segment = log_segments_[position];
  CHECK_EQ(log_segment->header().sequence_number(), segment_seqno);

  uint64_t size = log_segment->readable_up_to();
  Status s = AddImmutableFileToMap(&logs_, segment_seqno, log_segment->readable_file(), size);
  if (!s.ok()) {
    s = s.CloneAndPrepend(
            Substitute("Error accessing data for log segment with seqno $0",
                       segment_seqno));
    LOG(INFO) << s.ToString();
  }
  return s;
}

Status RemoteBootstrapSession::FindLogSegment(uint64_t segment_seqno,
                                              ImmutableRandomAccessFileInfo** file_info,
                                              RemoteBootstrapErrorPB::Code* error_code) {
  boost::lock_guard<simple_spinlock> l(session_lock_);
  if (!FindCopy(logs_, segment_seqno, file_info)) {
    *error_code = RemoteBootstrapErrorPB::WAL_SEGMENT_NOT_FOUND;
    return Status::NotFound(Substitute("Segment with sequence number $0 not found",
                                       segment_seqno));
  }
  return Status::OK();
}

Status RemoteBootstrapSession::UnregisterAnchorIfNeededUnlocked() {
  return tablet_peer_->log_anchor_registry()->UnregisterIfAnchored(&log_anchor_);
}

} // namespace tserver
} // namespace kudu
