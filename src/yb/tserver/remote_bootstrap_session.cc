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
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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

#include "yb/tserver/remote_bootstrap_session.h"

#include "yb/ash/wait_state.h"

#include "yb/consensus/consensus.h"
#include "yb/consensus/log.h"
#include "yb/consensus/opid_util.h"

#include "yb/gutil/casts.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/gutil/type_traits.h"

#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet_bootstrap_if.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/tablet_snapshots.h"

#include "yb/tserver/remote_bootstrap_snapshots.h"

#include "yb/util/env_util.h"
#include "yb/util/fault_injection.h"
#include "yb/util/logging.h"
#include "yb/util/size_literals.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/stopwatch.h"
#include "yb/util/trace.h"

DECLARE_uint64(rpc_max_message_size);
DECLARE_int64(remote_bootstrap_rate_limit_bytes_per_sec);
DECLARE_bool(enable_flush_retryable_requests);

DEFINE_test_flag(int32, rbs_sleep_after_taking_metadata_ms, 0,
                 "Sleep after tablet metadata was taken during remote boostrap session init.");

DEFINE_RUNTIME_int32(rbs_init_max_number_of_retries, 5,
                     "Max number of retries during remote bootstrap session initialisation, "
                     "when metadata before and after checkpoint does not match. "
                     "0 - to disable retry logic.");

DEFINE_test_flag(bool, rbs_fail_checkpoint, false,
                 "Fail all attempts to checkpoint data when retries still exist.");


namespace yb {
namespace tserver {

using std::shared_ptr;
using std::vector;
using std::string;

using consensus::MinimumOpId;
using strings::Substitute;
using tablet::RaftGroupMetadataPtr;
using tablet::TabletPeer;

const std::string kRetryableRequestsFileName = "retryable_requests";

RemoteBootstrapSession::RemoteBootstrapSession(
    const std::shared_ptr<TabletPeer>& tablet_peer, std::string session_id,
    std::string requestor_uuid, const std::atomic<int>* nsessions,
    const scoped_refptr<RemoteBootstrapAnchorClient>& rbs_anchor_client)
    : tablet_peer_(tablet_peer),
      session_id_(std::move(session_id)),
      requestor_uuid_(std::move(requestor_uuid)),
      nsessions_(nsessions),
      rbs_anchor_client_(rbs_anchor_client) {
  AddSource<RemoteBootstrapSnapshotsSource>();
}

RemoteBootstrapSession::~RemoteBootstrapSession() {
  WARN_NOT_OK(UnregisterRemotelogAnchor(),
              Format("$0Couldn't unregister remote log anchor session", LogPrefix()));

  // No lock taken in the destructor, should only be 1 thread with access now.
  CHECK_OK(UnregisterAnchorIfNeededUnlocked());
  RemoveCheckpointDir();
}

Status RemoteBootstrapSession::SetInitialCommittedState() {
  auto consensus = VERIFY_RESULT_PREPEND(
      tablet_peer_->GetConsensus(), "Unable to initialize remote bootstrap session");
  initial_committed_cstate_ = consensus->ConsensusState(consensus::CONSENSUS_CONFIG_COMMITTED);
  return Status::OK();
}

const std::string RemoteBootstrapSession::kCheckpointsDir = "checkpoints";

Status RemoteBootstrapSession::InitSnapshotTransferSession() {
  // Take locks to support re-initialization of the same session.
  std::lock_guard lock(mutex_);

  RETURN_NOT_OK(ReadSuperblockFromDisk());
  RETURN_NOT_OK(GetRunningTablet());
  RETURN_NOT_OK(InitSources());

  start_time_ = MonoTime::Now();

  return Status::OK();
}

Result<OpId> RemoteBootstrapSession::CreateSnapshot(int retry) {
  // Read the SuperBlock from disk.
  RETURN_NOT_OK(ReadSuperblockFromDisk());
  if (retry == 0 && FLAGS_TEST_rbs_sleep_after_taking_metadata_ms > 0) {
    LOG(INFO) << "TEST: Sleeping after taking tablet metadata";
    std::this_thread::sleep_for(
        std::chrono::milliseconds(FLAGS_TEST_rbs_sleep_after_taking_metadata_ms));
  }

  if (!tablet_peer_->log_available()) {
    return STATUS(IllegalState, "Tablet is not running (log is uninitialized)");
  }
  // Get the latest opid in the log at this point in time so we can re-anchor.
  auto last_logged_opid = tablet_peer_->GetLatestLogEntryOpId();

  auto tablet = VERIFY_RESULT(GetRunningTablet());

  MonoTime now = MonoTime::Now();
  auto* kv_store = tablet_superblock_.mutable_kv_store();
  const auto checkpoints_dir = JoinPathSegments(kv_store->rocksdb_dir(), kCheckpointsDir);

  auto session_checkpoint_dir = std::to_string(last_logged_opid.index) + "_" + now.ToString();
  checkpoint_dir_ = JoinPathSegments(checkpoints_dir, session_checkpoint_dir);

  // Clear any previous RocksDB files in the superblock. Each session should create a new list
  // based the checkpoint directory files.
  kv_store->clear_rocksdb_files();
  auto status = tablet->snapshots().CreateCheckpoint(checkpoint_dir_,
      tablet::CreateCheckpointIn::kSubDir,
      tablet::TabletSnapshots::UseTryLock::kTrue);
  if (status.ok()) {
    auto max_retries = FLAGS_rbs_init_max_number_of_retries;
    if (max_retries != 0) {
      tablet::RaftGroupReplicaSuperBlockPB new_superblock;
      RETURN_NOT_OK(ReadSuperblockFromDisk(&new_superblock));
      if (AsString(tablet_superblock_) != AsString(new_superblock)) {
        auto msg = "Metadata changed while creating checkpoint";
        status = retry >= max_retries ? STATUS(IllegalState, msg) : STATUS(TryAgain, msg);
        LOG(INFO) << status;
        return status;
      }
      if (PREDICT_FALSE(FLAGS_TEST_rbs_fail_checkpoint) && retry + 1 < max_retries) {
        LOG_AND_RETURN(WARNING, STATUS_FORMAT(TryAgain, "FLAGS_TEST_rbs_fail_checkpoint set"));
      }
    }

    *kv_store->mutable_rocksdb_files() = VERIFY_RESULT(tablet::ListFiles(checkpoint_dir_));
  } else if (!status.IsNotSupported()) {
    RETURN_NOT_OK(status);
  }

  return last_logged_opid;
}

void RemoteBootstrapSession::RemoveCheckpointDir() {
  // Delete checkpoint directory.
  if (checkpoint_dir_.empty()) {
    LOG(INFO) << "No checkpoint directory was created for this session";
    return;
  }
  auto s = env()->DeleteRecursively(checkpoint_dir_);
  if (!s.ok()) {
    LOG(WARNING) << Format("Unable to delete checkpoint directory $0: $1", checkpoint_dir_, s);
    return;
  }
  LOG(INFO) << "Successfully deleted checkpoint directory " << checkpoint_dir_;
}

Status RemoteBootstrapSession::InitBootstrapSession() {
  // Take locks to support re-initialization of the same session.
  std::lock_guard lock(mutex_);
  RETURN_NOT_OK(UnregisterAnchorIfNeededUnlocked());

  // Earliest WAL op index the source must retain -- the floor below which Log::GC() will not
  // delete. Compute it BEFORE registering our own log anchor, since GetEarliestRegisteredLogIndex()
  // inside GetEarliestNeededLogIndex() would otherwise pin the result to whatever we register at.
  //
  // GetEarliestNeededLogIndex() returns two floors: earliest_needed_log_index (what the tablet
  // itself needs) and log_index_needed_by_cdc (what the CDCSDK/xCluster (xrepl) retention barrier
  // needs). Log::GC() keeps segments at/above the xrepl barrier even when they sit below the
  // earliest-needed index. RBS must honor it too: once the destination is promoted to leader it
  // becomes the xrepl source and serves GetChanges by reading the WAL (not the RocksDB SSTs).
  // Shipping only segments at/above the earliest-needed index would drop ones below it that are
  // still at/above the xrepl barrier -- the WAL the new leader needs -- and GetChanges would then
  // fail with "logs ... have been garbage collected". So lower the floor to the same boundary GC
  // uses.
  // When no xrepl consumer constrains retention, log_index_needed_by_cdc is int64 max, so the min
  // is a no-op and RBS behaves exactly as before for non-xrepl tablets.
  const auto min_retain_log_index_info = VERIFY_RESULT(tablet_peer_->GetEarliestNeededLogIndex());
  const int64_t rbs_min_op_idx = std::min(
      min_retain_log_index_info.earliest_needed_log_index,
      min_retain_log_index_info.log_index_needed_by_cdc);

  // Prevent log GC from going below rbs_min_op_idx while we grab log segments and tablet
  // metadata. Pre-fix this code path did Register(MinimumOpId().index() = 0) here and later
  // UpdateRegistration'd to the first kept segment's min_replicate_index; because the pre-fix
  // path also kept every on-disk segment, that subsequent bump landed at the oldest on-disk
  // segment's minimum, effectively pinning GC at the bottom of the log for the duration of the
  // session. On a slow or repeatedly-retried RBS the same source would then accumulate more and
  // more redundant segments behind that anchor, forming a vicious cycle in which each new RBS
  // attempt had to ship strictly more data than the last. Registering at rbs_min_op_idx (the
  // earliest op index any consumer still needs) breaks that cycle: GC can continue reclaiming
  // everything strictly below this floor while the session is alive.
  string anchor_owner_token = Substitute("RemoteBootstrap-$0", session_id_);
  tablet_peer_->log_anchor_registry()->Register(
      rbs_min_op_idx, anchor_owner_token, &log_anchor_);

  OpId last_logged_opid;
  for (int retry = 0;; ++retry) {
    auto res = CreateSnapshot(retry);
    if (res.ok()) {
      last_logged_opid = *res;
      break;
    }
    RemoveCheckpointDir();
    if (!res.status().IsTryAgain()) {
      return res.status();
    }
  }

  // When the current peer is a follower and is serving rbs, make the leader anchor its log at
  // the last_logged_opid of the current peer. Since all data until that index will anyways be
  // served by this peer, we need not register the anchor at a preceeding index.
  remote_log_anchor_index_ = last_logged_opid.index;
  RETURN_NOT_OK(RegisterRemoteLogAnchorUnlocked());

  std::optional<OpId> min_synced_op_id;
  // Copy the retryable requests if it exists.
  if (FLAGS_enable_flush_retryable_requests) {
    Status s = tablet_peer_->FlushBootstrapState();
    if (s.ok() || s.IsAlreadyPresent()) {
      retryable_requests_filepath_ = JoinPathSegments(checkpoint_dir_, kRetryableRequestsFileName);
      auto copy_result = tablet_peer_->CopyBootstrapStateTo(*retryable_requests_filepath_);
      if (!copy_result.ok()) {
        LOG(WARNING) << "Copy retryable requests failed: " << s;
        retryable_requests_filepath_.reset();
      } else {
        min_synced_op_id = *copy_result;
      }
    } else {
      LOG(WARNING) << "Remote bootstrap session: flush retryable requests failed: " << s;
    }
  }

  RETURN_NOT_OK(InitSources());

  // It's possible that the wal segment is not synced and the retryable requests file
  // is newer than the data of wal file downloaded by remote peer. The remote peer will
  // reject newer ops that covered by retryable requests but not in the wal segment by
  // incorrectly detect them as duplicate.
  if (min_synced_op_id) {
    auto log_msg = Format("wait for OP($0) to be synced", *min_synced_op_id);
    LOG(INFO) << "Start to " << log_msg;
    auto wait_result = tablet_peer_->log()->WaitForSafeOpIdToApply(*min_synced_op_id);
    if (wait_result.empty()) {
      return STATUS_FORMAT(TimedOut, "Failed to $0", log_msg);
    }
  }

  // Get the current segments from the log, including the active segment. The Log doesn't add the
  // active segment to the log reader's list until a header has been written to it (but it will
  // not have a footer).
  RETURN_NOT_OK(tablet_peer_->log()->GetSegmentsSnapshot(&log_segments_));

  // Compute the contiguous prefix of segments that the destination does not need directly from
  // the local `log_segments_` snapshot, instead of re-querying Log::GetSegmentsToGC. Driving the
  // trim off the frozen snapshot we just took eliminates a race with the GC thread: between
  // GetSegmentsSnapshot and a second call back into Log, GC could physically reclaim a segment
  // below rbs_min_op_idx (allowed by our anchor, which only pins at rbs_min_op_idx and above).
  // The two calls would then disagree about which segments exist, num_to_skip would under-count,
  // log_segments_ would retain references to a segment whose file is gone, and the destination's
  // later FetchData would hit WAL_SEGMENT_NOT_FOUND -- the exact vicious-cycle symptom this fix
  // is meant to break.
  //
  // The predicate below mirrors the index-based portion of LogReader::GetSegmentPrefixNotIncluding
  // (stop at the active footer-less segment; stop at the first segment that still contains entries
  // at/above rbs_min_op_idx). rbs_min_op_idx already folds in the xrepl (CDCSDK/xCluster) retained
  // index (computed above), so segments the destination will still need for change capture once it
  // becomes leader are kept here. We do not replicate the other, purely source-side bumps that
  // Log::GetSegmentsToGC layers on (time-based and min-segments retention, in-flight log-copy
  // floor): each only ever makes GC retain *more* segments and covers ops no RBS destination needs,
  // so omitting them at worst ships a few extra segments -- never too few.
  size_t num_to_skip = 0;
  for (const auto& segment : log_segments_) {
    if (!segment->HasFooter()) {
      break;
    }
    if (segment->footer().max_replicate_index() >= rbs_min_op_idx) {
      break;
    }
    ++num_to_skip;
  }

  // When lazy superblock flush is enabled on this tablet (currently colocated tables only),
  // local bootstrap on the destination needs to replay at least
  // kMinSegmentsToReplayWithLazySuperblockFlush trailing WAL segments to pick up
  // committed-but-unflushed CHANGE_METADATA_OPs. GetEarliestNeededLogIndex already accounts
  // for the actual MinUnflushedChangeMetadataOpId, but bound num_to_skip defensively so the
  // destination always receives at least min(K, log_segments_.size()) trailing segments. The
  // min() handles the case where the source itself has fewer than K segments -- e.g. the brief
  // window during a segment rollover where the new active segment has been allocated and given
  // a footer-less old active, but the new active has not yet been registered with LogReader
  // via AppendEmptySegment -- by shipping everything we have rather than letting the snapshot
  // scan trim further. See the long comment around kMinSegmentsToReplayWithLazySuperblockFlush
  // in tablet_bootstrap.cc.
  if (tablet_peer_->tablet_metadata()->IsLazySuperblockFlushEnabled()) {
    const size_t min_segments_to_keep = std::min(
        tablet::kMinSegmentsToReplayWithLazySuperblockFlush, log_segments_.size());
    num_to_skip = std::min(num_to_skip, log_segments_.size() - min_segments_to_keep);
  }

  LOG_WITH_PREFIX(INFO)
      << "Computed WAL segments to ship: keeping=" << log_segments_.size() - num_to_skip
      << " (skipping " << num_to_skip << " of " << log_segments_.size()
      << " on-disk segments below retained op index " << rbs_min_op_idx
      << ", lazy_sb_flush="
      << tablet_peer_->tablet_metadata()->IsLazySuperblockFlushEnabled()
      << ", last_logged_opid=" << last_logged_opid << ")";

  while (!log_segments_.empty() && num_to_skip) {
    RETURN_NOT_OK(log_segments_.pop_front());
    num_to_skip--;
  }

  log_anchor_index_ = last_logged_opid.index;
  for (const auto& log_segment : log_segments_) {
    if (log_segment->HasFooter() && log_segment->footer().has_min_replicate_index()) {
      log_anchor_index_ = log_segment->footer().min_replicate_index();
      break;
    }
  }

  // Re-anchor on the highest OpId that was in the log right before we
  // snapshotted the log segments. This helps ensure that we don't end up in a
  // remote bootstrap loop due to a follower falling too far behind the
  // leader's log when remote bootstrap is slow. The remote controls when
  // this anchor is released by ending the remote bootstrap session.
  RETURN_NOT_OK(tablet_peer_->log_anchor_registry()->UpdateRegistration(
      log_anchor_index_, &log_anchor_));

  // Look up the committed consensus state.
  // We do this after snapshotting the log for YB table types to avoid a scenario where the latest
  // entry in the log has a term higher than the term stored in the consensus metadata, which
  // will result in a CHECK failure on RaftConsensus init.
  RETURN_NOT_OK(SetInitialCommittedState());

  start_time_ = MonoTime::Now();

  return Status::OK();
}

const std::string& RemoteBootstrapSession::tablet_id() const {
  return tablet_peer_->tablet_id();
}

const std::string& RemoteBootstrapSession::requestor_uuid() const {
  return requestor_uuid_;
}

namespace {

// Determine the length of the data chunk to return to the client.
int64_t DetermineReadLength(int64_t bytes_remaining, int64_t requested_len) {
  // Determine the size of the chunks we want to read.
  // Choose "system max" as a multiple of typical HDD block size (4K) with 4K to
  // spare for other stuff in the message, like headers, other protobufs, etc.
  const int32_t kSpareBytes = 4096;
  const int32_t kDiskSectorSize = 4096;
  auto system_max_chunk_size =
      ((FLAGS_rpc_max_message_size - kSpareBytes) / kDiskSectorSize) * kDiskSectorSize;
  CHECK_GT(system_max_chunk_size, 0) << "rpc_max_message_size is too low to transfer data: "
                                     << FLAGS_rpc_max_message_size;

  // The min of the {requested, system} maxes is the effective max.
  int64_t maxlen = requested_len > 0 ? std::min<int64_t>(requested_len, system_max_chunk_size)
                                     : system_max_chunk_size;
  return std::min(bytes_remaining, maxlen);
}

// Calculate the size of the data to return given a maximum client message
// length, the file itself, and the offset into the file to be read from.
Result<int64_t> GetResponseDataSize(GetDataPieceInfo* info) {
  // If requested offset is off the end of the data, bail.
  if (info->offset >= info->data_size) {
    info->error_code = RemoteBootstrapErrorPB::INVALID_REMOTE_BOOTSTRAP_REQUEST;
    return STATUS_FORMAT(InvalidArgument,
                         "Requested offset ($0) is beyond the data size ($1)",
                         info->offset, info->data_size);
  }

  auto result = DetermineReadLength(info->bytes_remaining(), info->client_maxlen);
  DCHECK_GT(result, 0);
  if (info->client_maxlen > 0) {
    DCHECK_LE(result, info->client_maxlen);
  }

  return result;
}

// Read a chunk of a file into a buffer.
// data_name provides a string for the block/log to be used in error messages.
Status ReadFileChunkToBuf(RandomAccessFile* file, const string& data_name, GetDataPieceInfo* info) {
  auto response_data_size = VERIFY_RESULT_PREPEND(
      GetResponseDataSize(info), Format("Error reading $0", data_name));

  Stopwatch chunk_timer(Stopwatch::THIS_THREAD);
  chunk_timer.start();

  // Writing into a std::string buffer is basically guaranteed to work on C++11,
  // however any modern compiler should be compatible with it.
  // Violates the API contract, but avoids excessive copies.
  info->data.resize(response_data_size);
  auto buf = reinterpret_cast<uint8_t*>(const_cast<char*>(info->data.data()));
  Slice slice;
  Status s = env_util::ReadFully(file, info->offset, response_data_size, &slice, buf);
  if (PREDICT_FALSE(!s.ok())) {
    s = s.CloneAndPrepend(Format("Unable to read existing file for $0", data_name));
    LOG(WARNING) << s;
    info->error_code = RemoteBootstrapErrorPB::IO_ERROR;
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

  return Status::OK();
}

} // namespace

Env* RemoteBootstrapSession::env() const {
  return tablet_peer_->tablet_metadata()->fs_manager()->env();
}

RemoteBootstrapSource* RemoteBootstrapSession::Source(DataIdPB::IdType id_type) const {
  size_t idx = id_type;
  return idx < sources_.size() ? sources_[idx].get() : nullptr;
}

Status RemoteBootstrapSession::ValidateDataId(const yb::tserver::DataIdPB& data_id) {
  const auto& source = Source(data_id.type());

  if (source) {
    return source->ValidateDataId(data_id);
  }

  switch (data_id.type()) {
    case DataIdPB::LOG_SEGMENT:
      if (PREDICT_FALSE(!data_id.wal_segment_seqno())) {
        return STATUS(InvalidArgument,
            "segment sequence number must be specified for type == LOG_SEGMENT",
            data_id.ShortDebugString());
      }
      return Status::OK();
    case DataIdPB::ROCKSDB_FILE:
      if (PREDICT_FALSE(data_id.file_name().empty())) {
        return STATUS(InvalidArgument,
            "file name must be specified for type == ROCKSDB_FILE",
            data_id.ShortDebugString());
      }
      return Status::OK();
    case DataIdPB::RETRYABLE_REQUESTS:
      return Status::OK();
    case DataIdPB::SNAPSHOT_FILE: FALLTHROUGH_INTENDED;
    case DataIdPB::UNKNOWN:
      return STATUS(InvalidArgument, "Type not supported", data_id.ShortDebugString());
  }
  LOG(FATAL) << "Invalid data id type: " << data_id.type();
}

Status RemoteBootstrapSession::GetDataPiece(const DataIdPB& data_id, GetDataPieceInfo* info) {
  SCOPED_WAIT_STATUS(RemoteBootstrap_ReadDataFromFile);
  const auto& source = sources_[data_id.type()];

  if (source) {
    // Fetching a snapshot file chunk.
    RETURN_NOT_OK_PREPEND(
        source->GetDataPiece(data_id, info),
        "Unable to get piece of snapshot file");
    return Status::OK();
  }


  switch (data_id.type()) {
    case DataIdPB::LOG_SEGMENT: {
      // Fetching a log segment chunk.
      RETURN_NOT_OK_PREPEND(GetLogSegmentPiece(data_id.wal_segment_seqno(), info),
                            "Unable to get piece of log segment");
      break;
    }
    case DataIdPB::ROCKSDB_FILE: {
      // Fetching a RocksDB file chunk.
      const string file_name = data_id.file_name();
      RETURN_NOT_OK_PREPEND(GetRocksDBFilePiece(data_id.file_name(), info),
                            "Unable to get piece of RocksDB file");
      break;
    }
    case DataIdPB::RETRYABLE_REQUESTS: {
      // Fetching the retryable requests file (may be abscent).
      RETURN_NOT_OK_PREPEND(GetRetryableRequestsFilePiece(info),
                            "Unable to get piece of retryable requests file");
      break;
    }
    default:
      info->error_code = RemoteBootstrapErrorPB::INVALID_REMOTE_BOOTSTRAP_REQUEST;
      return STATUS_SUBSTITUTE(InvalidArgument, "Invalid request type $0", data_id.type());
  }
  DCHECK(info->client_maxlen == 0 ||
         info->data.size() <= implicit_cast<size_t>(info->client_maxlen))
      << "client_maxlen: " << info->client_maxlen << ", data->size(): " << info->data.size();

  return Status::OK();
}

Status RemoteBootstrapSession::GetLogSegmentPiece(uint64_t segment_seqno, GetDataPieceInfo* info) {
  std::shared_ptr<RandomAccessFile> file;
  {
    std::lock_guard lock(mutex_);
    if (opened_log_segment_seqno_ != segment_seqno) {
      RETURN_NOT_OK(OpenLogSegment(segment_seqno, &info->error_code));
    }
    info->data_size = opened_log_segment_file_size_;
    file = opened_log_segment_file_;
  }
  RETURN_NOT_OK(ReadFileChunkToBuf(file.get(), Substitute("log segment $0", segment_seqno), info));

  // Note: We do not eagerly close log segment files, since we share ownership
  // of the LogSegment objects with the Log itself.

  return Status::OK();
}

Status RemoteBootstrapSession::GetRocksDBFilePiece(
    const std::string& file_name, GetDataPieceInfo* info) {
  return GetFilePiece(checkpoint_dir_, file_name, env(), info);
}

Status RemoteBootstrapSession::GetRetryableRequestsFilePiece(GetDataPieceInfo* info) {
  if (!retryable_requests_filepath_.has_value()) {
    return Status::OK();
  }
  return GetFilePiece(checkpoint_dir_, kRetryableRequestsFileName, env(), info);
}

Status RemoteBootstrapSession::GetFilePiece(
    const std::string& path, const std::string& file_name, Env* env, GetDataPieceInfo* info) {
  auto file_path = JoinPathSegments(path, file_name);
  if (!env->FileExists(file_path)) {
    info->error_code = RemoteBootstrapErrorPB::ROCKSDB_FILE_NOT_FOUND;
    return STATUS(NotFound, Substitute("Unable to find RocksDB file $0 in directory $1",
                                       file_name, path));
  }

  std::unique_ptr<RandomAccessFile> readable_file;

  RETURN_NOT_OK(env->NewRandomAccessFile(file_path, &readable_file));

  info->data_size = VERIFY_RESULT(readable_file->Size());
  auto inode = VERIFY_RESULT(readable_file->INode());
  VLOG(2) << "Reading RocksDB file. File path: " << file_path << ", file size: " << info->data_size
          << ", inode: " << inode;

  RETURN_NOT_OK(ReadFileChunkToBuf(
      readable_file.get(), Substitute("rocksdb file $0", file_name), info));

  return Status::OK();
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
    return STATUS(Corruption, "Found 0-length object");
  }

  // Looks good, add it to the cache.
  typedef typename Collection::mapped_type InfoPtr;
  typedef typename InfoPtr::element_type Info;
  CHECK(cache->emplace(key, std::make_unique<Info>(readable, size)).second);

  return Status::OK();
}

Status RemoteBootstrapSession::ReadSuperblockFromDisk(tablet::RaftGroupReplicaSuperBlockPB* out) {
  const string& tablet_id = tablet_peer_->tablet_id();

  // Read the SuperBlock from disk.
  const RaftGroupMetadataPtr& metadata = tablet_peer_->tablet_metadata();
  RETURN_NOT_OK(metadata->Flush(tablet::OnlyIfDirty::kTrue));
  RETURN_NOT_OK_PREPEND(
      metadata->ReadSuperBlockFromDisk(out ? out : &tablet_superblock_),
      Substitute("Unable to access superblock for tablet $0", tablet_id));

  return Status::OK();
}

Result<tablet::TabletPtr> RemoteBootstrapSession::GetRunningTablet() {
  auto tablet = tablet_peer_->shared_tablet_maybe_null();
  if (PREDICT_FALSE(!tablet)) {
    return STATUS(IllegalState, "Tablet is not running");
  }
  return tablet;
}

Status RemoteBootstrapSession::InitSources() {
  for (const auto& source : sources_) {
    if (source) {
      RETURN_NOT_OK(source->Init());
    }
  }
  return Status::OK();
}

Status RemoteBootstrapSession::OpenLogSegment(
    uint64_t segment_seqno, RemoteBootstrapErrorPB::Code* error_code) {
  auto active_seqno = tablet_peer_->log()->active_segment_sequence_number();
  auto log_segment_result = tablet_peer_->log()->GetSegmentBySequenceNumber(segment_seqno);
  // Usually active log segment is extended, while sent of the wire. So we cannot send next segment,
  // Otherwise entries at end of previously active log segment could be missing.
  if (opened_log_segment_active_ && segment_seqno != opened_log_segment_seqno_) {
    *error_code = RemoteBootstrapErrorPB::WAL_SEGMENT_NOT_FOUND;
    return STATUS_FORMAT(NotFound, "Already sent active log segment, don't send $0", segment_seqno);
  }
  if (!log_segment_result.ok()) {
    *error_code = RemoteBootstrapErrorPB::WAL_SEGMENT_NOT_FOUND;
    return STATUS_FORMAT(
        NotFound, "Log segment $0 not found: $1", segment_seqno, log_segment_result.status());
  }
  const log::ReadableLogSegmentPtr log_segment = *log_segment_result;
  opened_log_segment_file_size_ =
      log_segment->get_encryption_header_size() + log_segment->readable_to_offset();
  opened_log_segment_seqno_ = segment_seqno;
  opened_log_segment_file_ = log_segment->readable_file_checkpoint();
  opened_log_segment_active_ = active_seqno == segment_seqno;

  if (log_segment->HasFooter() &&
      log_segment->footer().min_replicate_index() > log_anchor_index_) {
    log_anchor_index_ = log_segment->footer().min_replicate_index();

    // Update log anchor, since we don't need older logs anymore.
    auto status = tablet_peer_->log_anchor_registry()->UpdateRegistration(
        log_anchor_index_, &log_anchor_);
    if (!status.ok()) {
      *error_code = RemoteBootstrapErrorPB::UNKNOWN_ERROR;
      return status;
    }
    // Update remote log anchor on the leader when the current peer serving rbs is a follower.
    if (log_anchor_index_ > remote_log_anchor_index_) {
      remote_log_anchor_index_ = log_anchor_index_;
      RETURN_NOT_OK(UpdateRemoteLogAnchorUnlocked());
    }
  }

  return Status::OK();
}

Status RemoteBootstrapSession::UnregisterAnchorIfNeededUnlocked() {
  return tablet_peer_->log_anchor_registry()->UnregisterIfAnchored(&log_anchor_);
}

void RemoteBootstrapSession::SetSuccess() {
  std::lock_guard lock(mutex_);
  succeeded_.store(true);
  // Can early clear the checkpoints dir since the session already succeeded (data sent to client),
  // and we don't need the snapshot anymore. In case of failure, the session is removed right away
  // which would anyways clear the checkpoints dir.
  RemoveCheckpointDir();
}

void RemoteBootstrapSession::EnsureRateLimiterIsInitialized() {
  if (!rate_limiter_.IsInitialized()) {
    InitRateLimiter();
  }
}

Status RemoteBootstrapSession::RefreshRemoteLogAnchorSessionAsync() {
  if (rbs_anchor_client_ && rbs_anchor_session_created_) {
    RETURN_NOT_OK(rbs_anchor_client_->KeepLogAnchorAliveAsync(Succeeded()));
  }
  return Status::OK();
}

void RemoteBootstrapSession::InitRateLimiter() {
  if (FLAGS_remote_bootstrap_rate_limit_bytes_per_sec > 0 && nsessions_) {
    // Calling SetTargetRateUpdater will activate the rate limiter.
    rate_limiter_.SetTargetRateUpdater([this]() -> uint64_t {
      DCHECK_GT(FLAGS_remote_bootstrap_rate_limit_bytes_per_sec, 0);
      if (FLAGS_remote_bootstrap_rate_limit_bytes_per_sec <= 0) {
        YB_LOG_EVERY_N(WARNING, 1000)
            << "Invalid value for remote_bootstrap_rate_limit_bytes_per_sec: "
            << FLAGS_remote_bootstrap_rate_limit_bytes_per_sec;
        // Since the rate limiter is initialized, it's expected that the value of
        // FLAGS_remote_bootstrap_rate_limit_bytes_per_sec is greater than 0. Since this is not the
        // case, we'll log an error, and set the rate to 50 MB/s.
        return 50_MB;
      }
      auto nsessions = nsessions_->load(std::memory_order_acquire);
      if (nsessions > 0) {
        return FLAGS_remote_bootstrap_rate_limit_bytes_per_sec / nsessions;
      } else {
        LOG(DFATAL) << "Invalid number of sessions: " << nsessions;
        return FLAGS_remote_bootstrap_rate_limit_bytes_per_sec;
      }
    });
  }
  rate_limiter_.Init();
}

Status RemoteBootstrapSession::RegisterRemoteLogAnchorUnlocked() {
  if (rbs_anchor_client_) {
    VLOG_WITH_PREFIX_AND_FUNC(4) << "index=" << remote_log_anchor_index_;
    RETURN_NOT_OK(rbs_anchor_client_->RegisterLogAnchor(
        tablet_peer_->tablet_id(), remote_log_anchor_index_, Succeeded()));
    rbs_anchor_session_created_ = true;
  }
  return Status::OK();
}

Status RemoteBootstrapSession::UpdateRemoteLogAnchorUnlocked() {
  if (rbs_anchor_client_) {
    VLOG_WITH_PREFIX_AND_FUNC(4) << "index=" << remote_log_anchor_index_;
    RETURN_NOT_OK(rbs_anchor_client_->UpdateLogAnchorAsync(remote_log_anchor_index_, Succeeded()));
  }
  return Status::OK();
}

Status RemoteBootstrapSession::UnregisterRemotelogAnchor() {
  if (rbs_anchor_client_) {
    VLOG_WITH_PREFIX_AND_FUNC(4);
    RETURN_NOT_OK(rbs_anchor_client_->UnregisterLogAnchor());
  }
  return Status::OK();
}

} // namespace tserver
} // namespace yb
