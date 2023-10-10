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

#include "yb/tserver/remote_bootstrap_session.h"

#include <boost/optional.hpp>
#include <glog/logging.h>

#include "yb/consensus/consensus.h"
#include "yb/consensus/log.h"
#include "yb/consensus/opid_util.h"

#include "yb/gutil/casts.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/gutil/type_traits.h"

#include "yb/tablet/tablet.h"
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

DEFINE_test_flag(double, fault_crash_leader_after_changing_role, 0.0,
                 "The leader will crash after successfully sending a ChangeConfig (CHANGE_ROLE "
                 "from PRE_VOTER or PRE_OBSERVER to VOTER or OBSERVER respectively) for the tablet "
                 "server it is remote bootstrapping, but before it sends a success response.");


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
      succeeded_(false),
      nsessions_(nsessions),
      rbs_anchor_client_(rbs_anchor_client) {
  AddSource<RemoteBootstrapSnapshotsSource>();
}

RemoteBootstrapSession::~RemoteBootstrapSession() {
  if (rbs_anchor_client_) {
    WARN_NOT_OK(
        rbs_anchor_client_->UnregisterLogAnchor(),
        Format("Couldn't delete log anchor session $0", session_id_));
  }

  // No lock taken in the destructor, should only be 1 thread with access now.
  CHECK_OK(UnregisterAnchorIfNeededUnlocked());

  // Delete checkpoint directory.
  if (!checkpoint_dir_.empty()) {
    auto s = env()->DeleteRecursively(checkpoint_dir_);
    if (!s.ok()) {
      LOG(WARNING) << "Unable to delete checkpoint directory " << checkpoint_dir_;
    } else {
      LOG(INFO) << "Successfully deleted checkpoint directory " << checkpoint_dir_;
    }
  } else {
    LOG(INFO) << "No checkpoint directory was created for this session";
  }

}

Status RemoteBootstrapSession::ChangeRole() {
  CHECK(Succeeded());
  CHECK(ShouldChangeRole());

  LOG(INFO) << "Attempting to ChangeRole for peer " << requestor_uuid_ << " in bootstrap session "
            << session_id_;
  auto status = rbs_anchor_client_ ? rbs_anchor_client_->ChangePeerRole()
                                   : tablet_peer_->ChangeRole(requestor_uuid_);
  if (status.ok()) {
    MAYBE_FAULT(FLAGS_TEST_fault_crash_leader_after_changing_role);
  }
  return status;
}

Status RemoteBootstrapSession::SetInitialCommittedState() {
  auto consensus = VERIFY_RESULT_PREPEND(
      tablet_peer_->GetConsensus(), "Unable to initialize remote bootstrap session");
  initial_committed_cstate_ = consensus->ConsensusState(consensus::CONSENSUS_CONFIG_COMMITTED);
  return Status::OK();
}

Result<google::protobuf::RepeatedPtrField<tablet::FilePB>> ListFiles(const std::string& dir) {
  std::vector<std::string> files;
  auto env = Env::Default();
  auto status = env->GetChildren(dir, ExcludeDots::kTrue, &files);
  if (!status.ok()) {
    return STATUS(IllegalState, Substitute("Unable to get RocksDB files in dir $0: $1", dir,
                                           status.ToString()));
  }

  google::protobuf::RepeatedPtrField<tablet::FilePB> result;
  result.Reserve(narrow_cast<int>(files.size()));
  for (const auto& file : files) {
    auto full_path = JoinPathSegments(dir, file);
    if (VERIFY_RESULT(env->IsDirectory(full_path))) {
      auto sub_files = VERIFY_RESULT(ListFiles(full_path));
      for (auto& subfile : sub_files) {
        subfile.set_name(JoinPathSegments(file, subfile.name()));
        *result.Add() = std::move(subfile);
      }
      continue;
    }
    auto file_pb = result.Add();
    file_pb->set_name(file);
    file_pb->set_size_bytes(VERIFY_RESULT(env->GetFileSize(full_path)));
    file_pb->set_inode(VERIFY_RESULT(env->GetFileINode(full_path)));
  }

  return result;
}

const std::string RemoteBootstrapSession::kCheckpointsDir = "checkpoints";

Status RemoteBootstrapSession::InitSnapshotTransferSession() {
  // Take locks to support re-initialization of the same session.
  std::lock_guard lock(mutex_);

  RETURN_NOT_OK(ReadSuperblockFromDisk());
  RETURN_NOT_OK(GetRunningTablet());
  RETURN_NOT_OK(InitSources());

  start_time_ = MonoTime::Now();
  should_try_change_role_ = false;

  return Status::OK();
}

Status RemoteBootstrapSession::InitBootstrapSession() {
  // Take locks to support re-initialization of the same session.
  std::lock_guard lock(mutex_);
  RETURN_NOT_OK(UnregisterAnchorIfNeededUnlocked());

  // Prevent log GC while we grab log segments and Tablet metadata.
  string anchor_owner_token = Substitute("RemoteBootstrap-$0", session_id_);
  tablet_peer_->log_anchor_registry()->Register(
      MinimumOpId().index(), anchor_owner_token, &log_anchor_);

  // Read the SuperBlock from disk.
  RETURN_NOT_OK(ReadSuperblockFromDisk());

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
  auto status = tablet->snapshots().CreateCheckpoint(checkpoint_dir_);
  if (status.ok()) {
    *kv_store->mutable_rocksdb_files() = VERIFY_RESULT(ListFiles(checkpoint_dir_));
  } else if (!status.IsNotSupported()) {
    RETURN_NOT_OK(status);
  }

  std::optional<OpId> min_synced_op_id;
  // Copy the retryable requests if it exists.
  if (GetAtomicFlag(&FLAGS_enable_flush_retryable_requests)) {
    Status s = tablet_peer_->FlushRetryableRequests();
    if (s.ok() || s.IsAlreadyPresent()) {
      retryable_requests_filepath_ = JoinPathSegments(checkpoint_dir_, kRetryableRequestsFileName);
      auto copy_result = tablet_peer_->CopyRetryableRequestsTo(*retryable_requests_filepath_);
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

  // Get the current segments from the log, including the active segment.
  // The Log doesn't add the active segment to the log reader's list until
  // a header has been written to it (but it will not have a footer).
  RETURN_NOT_OK(tablet_peer_->log()->GetSegmentsSnapshot(&log_segments_));
  log_anchor_index_ = last_logged_opid.index;
  for (const auto& log_segment : log_segments_) {
    if (log_segment->HasFooter() && log_segment->footer().has_min_replicate_index()) {
      log_anchor_index_ = log_segment->footer().min_replicate_index();
      break;
    }
  }

  if (rbs_anchor_client_) {
    RETURN_NOT_OK(rbs_anchor_client_->RegisterLogAnchor(
        tablet_peer_->tablet_id(), log_anchor_index_));
    rbs_anchor_session_created_ = true;
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

Status RemoteBootstrapSession::ReadSuperblockFromDisk() {
  const string& tablet_id = tablet_peer_->tablet_id();

  // Read the SuperBlock from disk.
  const RaftGroupMetadataPtr& metadata = tablet_peer_->tablet_metadata();
  RETURN_NOT_OK(metadata->Flush(tablet::OnlyIfDirty::kTrue));
  RETURN_NOT_OK_PREPEND(
      metadata->ReadSuperBlockFromDisk(&tablet_superblock_),
      Substitute("Unable to access superblock for tablet $0", tablet_id));

  return Status::OK();
}

Result<tablet::TabletPtr> RemoteBootstrapSession::GetRunningTablet() {
  auto tablet = tablet_peer_->shared_tablet();
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
  if (opened_log_segment_active_) {
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

    // Update log anchor on the tablet leader.
    if (rbs_anchor_client_) {
      RETURN_NOT_OK(rbs_anchor_client_->UpdateLogAnchorAsync(log_anchor_index_));
    }

    // Update log anchor, since we don't need older logs anymore.
    auto status = tablet_peer_->log_anchor_registry()->UpdateRegistration(
        log_anchor_index_, &log_anchor_);
    if (!status.ok()) {
      *error_code = RemoteBootstrapErrorPB::UNKNOWN_ERROR;
      return status;
    }
  }

  return Status::OK();
}

Status RemoteBootstrapSession::UnregisterAnchorIfNeededUnlocked() {
  return tablet_peer_->log_anchor_registry()->UnregisterIfAnchored(&log_anchor_);
}

void RemoteBootstrapSession::SetSuccess() {
  std::lock_guard lock(mutex_);
  succeeded_ = true;
}

bool RemoteBootstrapSession::Succeeded() {
  std::lock_guard lock(mutex_);
  return succeeded_;
}

bool RemoteBootstrapSession::ShouldChangeRole() {
  std::lock_guard lock(mutex_);
  return should_try_change_role_;
}

void RemoteBootstrapSession::EnsureRateLimiterIsInitialized() {
  if (!rate_limiter_.IsInitialized()) {
    InitRateLimiter();
  }
}

Status RemoteBootstrapSession::RefreshRemoteLogAnchorSessionAsync() {
  if (rbs_anchor_client_ && rbs_anchor_session_created_) {
    RETURN_NOT_OK(rbs_anchor_client_->KeepLogAnchorAliveAsync());
  }
  return Status::OK();
}

void RemoteBootstrapSession::InitRateLimiter() {
  if (FLAGS_remote_bootstrap_rate_limit_bytes_per_sec > 0 && nsessions_) {
    // Calling SetTargetRateUpdater will activate the rate limiter.
    rate_limiter_.SetTargetRateUpdater([this]() -> uint64_t {
      DCHECK_GT(FLAGS_remote_bootstrap_rate_limit_bytes_per_sec, 0);
      if (FLAGS_remote_bootstrap_rate_limit_bytes_per_sec <= 0) {
        YB_LOG_EVERY_N(ERROR, 1000)
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

} // namespace tserver
} // namespace yb
