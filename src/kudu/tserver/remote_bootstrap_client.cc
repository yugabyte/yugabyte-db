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

#include "kudu/tserver/remote_bootstrap_client.h"

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "kudu/common/wire_protocol.h"
#include "kudu/consensus/consensus_meta.h"
#include "kudu/consensus/metadata.pb.h"
#include "kudu/fs/block_id.h"
#include "kudu/fs/block_manager.h"
#include "kudu/fs/fs_manager.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/gutil/strings/util.h"
#include "kudu/gutil/walltime.h"
#include "kudu/rpc/messenger.h"
#include "kudu/rpc/transfer.h"
#include "kudu/tablet/tablet.pb.h"
#include "kudu/tablet/tablet_bootstrap.h"
#include "kudu/tablet/tablet_peer.h"
#include "kudu/tserver/remote_bootstrap.pb.h"
#include "kudu/tserver/remote_bootstrap.proxy.h"
#include "kudu/tserver/tablet_server.h"
#include "kudu/util/crc.h"
#include "kudu/util/env.h"
#include "kudu/util/env_util.h"
#include "kudu/util/flag_tags.h"
#include "kudu/util/logging.h"
#include "kudu/util/net/net_util.h"

DEFINE_int32(remote_bootstrap_begin_session_timeout_ms, 3000,
             "Tablet server RPC client timeout for BeginRemoteBootstrapSession calls. "
             "Also used for EndRemoteBootstrapSession calls.");
TAG_FLAG(remote_bootstrap_begin_session_timeout_ms, hidden);

DEFINE_bool(remote_bootstrap_save_downloaded_metadata, false,
            "Save copies of the downloaded remote bootstrap files for debugging purposes. "
            "Note: This is only intended for debugging and should not be normally used!");
TAG_FLAG(remote_bootstrap_save_downloaded_metadata, advanced);
TAG_FLAG(remote_bootstrap_save_downloaded_metadata, hidden);
TAG_FLAG(remote_bootstrap_save_downloaded_metadata, runtime);

// RETURN_NOT_OK_PREPEND() with a remote-error unwinding step.
#define RETURN_NOT_OK_UNWIND_PREPEND(status, controller, msg) \
  RETURN_NOT_OK_PREPEND(UnwindRemoteError(status, controller), msg)

namespace kudu {
namespace tserver {

using consensus::ConsensusMetadata;
using consensus::ConsensusStatePB;
using consensus::OpId;
using consensus::RaftConfigPB;
using consensus::RaftPeerPB;
using env_util::CopyFile;
using fs::WritableBlock;
using rpc::Messenger;
using std::shared_ptr;
using std::string;
using std::vector;
using strings::Substitute;
using tablet::ColumnDataPB;
using tablet::DeltaDataPB;
using tablet::RowSetDataPB;
using tablet::TabletDataState;
using tablet::TabletDataState_Name;
using tablet::TabletMetadata;
using tablet::TabletStatusListener;
using tablet::TabletSuperBlockPB;

RemoteBootstrapClient::RemoteBootstrapClient(std::string tablet_id,
                                             FsManager* fs_manager,
                                             shared_ptr<Messenger> messenger,
                                             string client_permanent_uuid)
    : tablet_id_(std::move(tablet_id)),
      fs_manager_(fs_manager),
      messenger_(std::move(messenger)),
      permanent_uuid_(std::move(client_permanent_uuid)),
      started_(false),
      downloaded_wal_(false),
      downloaded_blocks_(false),
      replace_tombstoned_tablet_(false),
      status_listener_(nullptr),
      session_idle_timeout_millis_(0),
      start_time_micros_(0) {}

RemoteBootstrapClient::~RemoteBootstrapClient() {
  // Note: Ending the remote bootstrap session releases anchors on the remote.
  WARN_NOT_OK(EndRemoteSession(), "Unable to close remote bootstrap session");
}

Status RemoteBootstrapClient::SetTabletToReplace(const scoped_refptr<TabletMetadata>& meta,
                                                 int64_t caller_term) {
  CHECK_EQ(tablet_id_, meta->tablet_id());
  TabletDataState data_state = meta->tablet_data_state();
  if (data_state != tablet::TABLET_DATA_TOMBSTONED) {
    return Status::IllegalState(Substitute("Tablet $0 not in tombstoned state: $1 ($2)",
                                           tablet_id_,
                                           TabletDataState_Name(data_state),
                                           data_state));
  }

  replace_tombstoned_tablet_ = true;
  meta_ = meta;

  int64_t last_logged_term = meta->tombstone_last_logged_opid().term();
  if (last_logged_term > caller_term) {
    return Status::InvalidArgument(
        Substitute("Leader has term $0 but the last log entry written by the tombstoned replica "
                   "for tablet $1 has higher term $2. Refusing remote bootstrap from leader",
                   caller_term, tablet_id_, last_logged_term));
  }

  // Load the old consensus metadata, if it exists.
  gscoped_ptr<ConsensusMetadata> cmeta;
  Status s = ConsensusMetadata::Load(fs_manager_, tablet_id_,
                                     fs_manager_->uuid(), &cmeta);
  if (s.IsNotFound()) {
    // The consensus metadata was not written to disk, possibly due to a failed
    // remote bootstrap.
    return Status::OK();
  }
  RETURN_NOT_OK(s);
  cmeta_.swap(cmeta);
  return Status::OK();
}

Status RemoteBootstrapClient::Start(const string& bootstrap_peer_uuid,
                                    const HostPort& bootstrap_peer_addr,
                                    scoped_refptr<TabletMetadata>* meta) {
  CHECK(!started_);
  start_time_micros_ = GetCurrentTimeMicros();

  Sockaddr addr;
  RETURN_NOT_OK(SockaddrFromHostPort(bootstrap_peer_addr, &addr));
  if (addr.IsWildcard()) {
    return Status::InvalidArgument("Invalid wildcard address to remote bootstrap from",
                                   Substitute("$0 (resolved to $1)",
                                              bootstrap_peer_addr.host(), addr.host()));
  }
  LOG_WITH_PREFIX(INFO) << "Beginning remote bootstrap session"
                        << " from remote peer at address " << bootstrap_peer_addr.ToString();

  // Set up an RPC proxy for the RemoteBootstrapService.
  proxy_.reset(new RemoteBootstrapServiceProxy(messenger_, addr));

  BeginRemoteBootstrapSessionRequestPB req;
  req.set_requestor_uuid(permanent_uuid_);
  req.set_tablet_id(tablet_id_);

  rpc::RpcController controller;
  controller.set_timeout(MonoDelta::FromMilliseconds(
      FLAGS_remote_bootstrap_begin_session_timeout_ms));

  // Begin the remote bootstrap session with the remote peer.
  BeginRemoteBootstrapSessionResponsePB resp;
  RETURN_NOT_OK_UNWIND_PREPEND(proxy_->BeginRemoteBootstrapSession(req, &resp, &controller),
                               controller,
                               "Unable to begin remote bootstrap session");

  if (resp.superblock().tablet_data_state() != tablet::TABLET_DATA_READY) {
    Status s = Status::IllegalState("Remote peer (" + bootstrap_peer_uuid + ")" +
                                    " is currently remotely bootstrapping itself!",
                                    resp.superblock().ShortDebugString());
    LOG_WITH_PREFIX(WARNING) << s.ToString();
    return s;
  }

  session_id_ = resp.session_id();
  session_idle_timeout_millis_ = resp.session_idle_timeout_millis();
  superblock_.reset(resp.release_superblock());
  superblock_->set_tablet_data_state(tablet::TABLET_DATA_COPYING);
  wal_seqnos_.assign(resp.wal_segment_seqnos().begin(), resp.wal_segment_seqnos().end());
  remote_committed_cstate_.reset(resp.release_initial_committed_cstate());

  Schema schema;
  RETURN_NOT_OK_PREPEND(SchemaFromPB(superblock_->schema(), &schema),
                        "Cannot deserialize schema from remote superblock");

  if (replace_tombstoned_tablet_) {
    // Also validate the term of the bootstrap source peer, in case they are
    // different. This is a sanity check that protects us in case a bug or
    // misconfiguration causes us to attempt to bootstrap from an out-of-date
    // source peer, even after passing the term check from the caller in
    // SetTabletToReplace().
    int64_t last_logged_term = meta_->tombstone_last_logged_opid().term();
    if (last_logged_term > remote_committed_cstate_->current_term()) {
      return Status::InvalidArgument(
          Substitute("Tablet $0: Bootstrap source has term $1 but "
                     "tombstoned replica has last-logged opid with higher term $2. "
                      "Refusing remote bootstrap from source peer $3",
                      tablet_id_,
                      remote_committed_cstate_->current_term(),
                      last_logged_term,
                      bootstrap_peer_uuid));
    }

    // This will flush to disk, but we set the data state to COPYING above.
    RETURN_NOT_OK_PREPEND(meta_->ReplaceSuperBlock(*superblock_),
                          "Remote bootstrap unable to replace superblock on tablet " +
                          tablet_id_);
  } else {

    Partition partition;
    Partition::FromPB(superblock_->partition(), &partition);
    PartitionSchema partition_schema;
    RETURN_NOT_OK(PartitionSchema::FromPB(superblock_->partition_schema(),
                                          schema, &partition_schema));

    // Create the superblock on disk.
    RETURN_NOT_OK(TabletMetadata::CreateNew(fs_manager_, tablet_id_,
                                            superblock_->table_name(),
                                            schema,
                                            partition_schema,
                                            partition,
                                            tablet::TABLET_DATA_COPYING,
                                            &meta_));
  }

  started_ = true;
  if (meta) {
    *meta = meta_;
  }
  return Status::OK();
}

Status RemoteBootstrapClient::FetchAll(TabletStatusListener* status_listener) {
  CHECK(started_);
  status_listener_ = CHECK_NOTNULL(status_listener);

  // Download all the files (serially, for now, but in parallel in the future).
  RETURN_NOT_OK(DownloadBlocks());
  RETURN_NOT_OK(DownloadWALs());

  return Status::OK();
}

Status RemoteBootstrapClient::Finish() {
  CHECK(meta_);
  CHECK(started_);
  CHECK(downloaded_wal_);
  CHECK(downloaded_blocks_);

  RETURN_NOT_OK(WriteConsensusMetadata());

  // Replace tablet metadata superblock. This will set the tablet metadata state
  // to TABLET_DATA_READY, since we checked above that the response
  // superblock is in a valid state to bootstrap from.
  LOG_WITH_PREFIX(INFO) << "Remote bootstrap complete. Replacing tablet superblock.";
  UpdateStatusMessage("Replacing tablet superblock");
  new_superblock_->set_tablet_data_state(tablet::TABLET_DATA_READY);
  RETURN_NOT_OK(meta_->ReplaceSuperBlock(*new_superblock_));

  if (FLAGS_remote_bootstrap_save_downloaded_metadata) {
    string meta_path = fs_manager_->GetTabletMetadataPath(tablet_id_);
    string meta_copy_path = Substitute("$0.copy.$1.tmp", meta_path, start_time_micros_);
    RETURN_NOT_OK_PREPEND(CopyFile(Env::Default(), meta_path, meta_copy_path,
                                   WritableFileOptions()),
                          "Unable to make copy of tablet metadata");
  }

  return Status::OK();
}

// Decode the remote error into a human-readable Status object.
Status RemoteBootstrapClient::ExtractRemoteError(const rpc::ErrorStatusPB& remote_error) {
  if (PREDICT_TRUE(remote_error.HasExtension(RemoteBootstrapErrorPB::remote_bootstrap_error_ext))) {
    const RemoteBootstrapErrorPB& error =
        remote_error.GetExtension(RemoteBootstrapErrorPB::remote_bootstrap_error_ext);
    return StatusFromPB(error.status()).CloneAndPrepend("Received error code " +
              RemoteBootstrapErrorPB::Code_Name(error.code()) + " from remote service");
  } else {
    return Status::InvalidArgument("Unable to decode remote bootstrap RPC error message",
                                   remote_error.ShortDebugString());
  }
}

// Enhance a RemoteError Status message with additional details from the remote.
Status RemoteBootstrapClient::UnwindRemoteError(const Status& status,
                                                const rpc::RpcController& controller) {
  if (!status.IsRemoteError()) {
    return status;
  }
  Status extension_status = ExtractRemoteError(*controller.error_response());
  return status.CloneAndAppend(extension_status.ToString());
}

void RemoteBootstrapClient::UpdateStatusMessage(const string& message) {
  if (status_listener_ != nullptr) {
    status_listener_->StatusMessage("RemoteBootstrap: " + message);
  }
}

Status RemoteBootstrapClient::EndRemoteSession() {
  if (!started_) {
    return Status::OK();
  }

  rpc::RpcController controller;
  controller.set_timeout(MonoDelta::FromMilliseconds(
        FLAGS_remote_bootstrap_begin_session_timeout_ms));

  EndRemoteBootstrapSessionRequestPB req;
  req.set_session_id(session_id_);
  req.set_is_success(true);
  EndRemoteBootstrapSessionResponsePB resp;
  RETURN_NOT_OK_UNWIND_PREPEND(proxy_->EndRemoteBootstrapSession(req, &resp, &controller),
                               controller,
                               "Failure ending remote bootstrap session");

  return Status::OK();
}

Status RemoteBootstrapClient::DownloadWALs() {
  CHECK(started_);

  // Delete and recreate WAL dir if it already exists, to ensure stray files are
  // not kept from previous bootstraps and runs.
  string path = fs_manager_->GetTabletWalDir(tablet_id_);
  if (fs_manager_->env()->FileExists(path)) {
    RETURN_NOT_OK(fs_manager_->env()->DeleteRecursively(path));
  }
  RETURN_NOT_OK(fs_manager_->env()->CreateDir(path));
  RETURN_NOT_OK(fs_manager_->env()->SyncDir(DirName(path))); // fsync() parent dir.

  // Download the WAL segments.
  int num_segments = wal_seqnos_.size();
  LOG_WITH_PREFIX(INFO) << "Starting download of " << num_segments << " WAL segments...";
  uint64_t counter = 0;
  for (uint64_t seg_seqno : wal_seqnos_) {
    UpdateStatusMessage(Substitute("Downloading WAL segment with seq. number $0 ($1/$2)",
                                   seg_seqno, counter + 1, num_segments));
    RETURN_NOT_OK(DownloadWAL(seg_seqno));
    ++counter;
  }

  downloaded_wal_ = true;
  return Status::OK();
}

Status RemoteBootstrapClient::DownloadBlocks() {
  CHECK(started_);

  // Count up the total number of blocks to download.
  int num_blocks = 0;
  for (const RowSetDataPB& rowset : superblock_->rowsets()) {
    num_blocks += rowset.columns_size();
    num_blocks += rowset.redo_deltas_size();
    num_blocks += rowset.undo_deltas_size();
    if (rowset.has_bloom_block()) {
      num_blocks++;
    }
    if (rowset.has_adhoc_index_block()) {
      num_blocks++;
    }
  }

  // Download each block, writing the new block IDs into the new superblock
  // as each block downloads.
  gscoped_ptr<TabletSuperBlockPB> new_sb(new TabletSuperBlockPB());
  new_sb->CopyFrom(*superblock_);
  int block_count = 0;
  LOG_WITH_PREFIX(INFO) << "Starting download of " << num_blocks << " data blocks...";
  for (RowSetDataPB& rowset : *new_sb->mutable_rowsets()) {
    for (ColumnDataPB& col : *rowset.mutable_columns()) {
      RETURN_NOT_OK(DownloadAndRewriteBlock(col.mutable_block(),
                                            &block_count, num_blocks));
    }
    for (DeltaDataPB& redo : *rowset.mutable_redo_deltas()) {
      RETURN_NOT_OK(DownloadAndRewriteBlock(redo.mutable_block(),
                                            &block_count, num_blocks));
    }
    for (DeltaDataPB& undo : *rowset.mutable_undo_deltas()) {
      RETURN_NOT_OK(DownloadAndRewriteBlock(undo.mutable_block(),
                                            &block_count, num_blocks));
    }
    if (rowset.has_bloom_block()) {
      RETURN_NOT_OK(DownloadAndRewriteBlock(rowset.mutable_bloom_block(),
                                            &block_count, num_blocks));
    }
    if (rowset.has_adhoc_index_block()) {
      RETURN_NOT_OK(DownloadAndRewriteBlock(rowset.mutable_adhoc_index_block(),
                                            &block_count, num_blocks));
    }
  }

  // The orphaned physical block ids at the remote have no meaning to us.
  new_sb->clear_orphaned_blocks();
  new_superblock_.swap(new_sb);

  downloaded_blocks_ = true;

  return Status::OK();
}

Status RemoteBootstrapClient::DownloadWAL(uint64_t wal_segment_seqno) {
  VLOG_WITH_PREFIX(1) << "Downloading WAL segment with seqno " << wal_segment_seqno;
  DataIdPB data_id;
  data_id.set_type(DataIdPB::LOG_SEGMENT);
  data_id.set_wal_segment_seqno(wal_segment_seqno);
  string dest_path = fs_manager_->GetWalSegmentFileName(tablet_id_, wal_segment_seqno);

  WritableFileOptions opts;
  opts.sync_on_close = true;
  gscoped_ptr<WritableFile> writer;
  RETURN_NOT_OK_PREPEND(fs_manager_->env()->NewWritableFile(opts, dest_path, &writer),
                        "Unable to open file for writing");
  RETURN_NOT_OK_PREPEND(DownloadFile(data_id, writer.get()),
                        Substitute("Unable to download WAL segment with seq. number $0",
                                   wal_segment_seqno));
  return Status::OK();
}

Status RemoteBootstrapClient::WriteConsensusMetadata() {
  // If we didn't find a previous consensus meta file, create one.
  if (!cmeta_) {
    gscoped_ptr<ConsensusMetadata> cmeta;
    return ConsensusMetadata::Create(fs_manager_, tablet_id_, fs_manager_->uuid(),
                                     remote_committed_cstate_->config(),
                                     remote_committed_cstate_->current_term(),
                                     &cmeta);
  }

  // Otherwise, update the consensus metadata to reflect the config and term
  // sent by the remote bootstrap source.
  cmeta_->MergeCommittedConsensusStatePB(*remote_committed_cstate_);
  RETURN_NOT_OK(cmeta_->Flush());

  if (FLAGS_remote_bootstrap_save_downloaded_metadata) {
    string cmeta_path = fs_manager_->GetConsensusMetadataPath(tablet_id_);
    string cmeta_copy_path = Substitute("$0.copy.$1.tmp", cmeta_path, start_time_micros_);
    RETURN_NOT_OK_PREPEND(CopyFile(Env::Default(), cmeta_path, cmeta_copy_path,
                                   WritableFileOptions()),
                          "Unable to make copy of consensus metadata");
  }

  return Status::OK();
}

Status RemoteBootstrapClient::DownloadAndRewriteBlock(BlockIdPB* block_id,
                                                      int* block_count, int num_blocks) {
  BlockId old_block_id(BlockId::FromPB(*block_id));
  UpdateStatusMessage(Substitute("Downloading block $0 ($1/$2)",
                                 old_block_id.ToString(), *block_count,
                                 num_blocks));
  BlockId new_block_id;
  RETURN_NOT_OK_PREPEND(DownloadBlock(old_block_id, &new_block_id),
      "Unable to download block with id " + old_block_id.ToString());

  new_block_id.CopyToPB(block_id);
  (*block_count)++;
  return Status::OK();
}

Status RemoteBootstrapClient::DownloadBlock(const BlockId& old_block_id,
                                            BlockId* new_block_id) {
  VLOG_WITH_PREFIX(1) << "Downloading block with block_id " << old_block_id.ToString();

  gscoped_ptr<WritableBlock> block;
  RETURN_NOT_OK_PREPEND(fs_manager_->CreateNewBlock(&block),
                        "Unable to create new block");

  DataIdPB data_id;
  data_id.set_type(DataIdPB::BLOCK);
  old_block_id.CopyToPB(data_id.mutable_block_id());
  RETURN_NOT_OK_PREPEND(DownloadFile(data_id, block.get()),
                        Substitute("Unable to download block $0",
                                   old_block_id.ToString()));

  *new_block_id = block->id();
  RETURN_NOT_OK_PREPEND(block->Close(), "Unable to close block");
  return Status::OK();
}

template<class Appendable>
Status RemoteBootstrapClient::DownloadFile(const DataIdPB& data_id,
                                           Appendable* appendable) {
  uint64_t offset = 0;
  int32_t max_length = FLAGS_rpc_max_message_size - 1024; // Leave 1K for message headers.

  rpc::RpcController controller;
  controller.set_timeout(MonoDelta::FromMilliseconds(session_idle_timeout_millis_));
  FetchDataRequestPB req;

  bool done = false;
  while (!done) {
    controller.Reset();
    req.set_session_id(session_id_);
    req.mutable_data_id()->CopyFrom(data_id);
    req.set_offset(offset);
    req.set_max_length(max_length);

    FetchDataResponsePB resp;
    RETURN_NOT_OK_UNWIND_PREPEND(proxy_->FetchData(req, &resp, &controller),
                                controller,
                                "Unable to fetch data from remote");

    // Sanity-check for corruption.
    RETURN_NOT_OK_PREPEND(VerifyData(offset, resp.chunk()),
                          Substitute("Error validating data item $0", data_id.ShortDebugString()));

    // Write the data.
    RETURN_NOT_OK(appendable->Append(resp.chunk().data()));

    if (offset + resp.chunk().data().size() == resp.chunk().total_data_length()) {
      done = true;
    }
    offset += resp.chunk().data().size();
  }

  return Status::OK();
}

Status RemoteBootstrapClient::VerifyData(uint64_t offset, const DataChunkPB& chunk) {
  // Verify the offset is what we expected.
  if (offset != chunk.offset()) {
    return Status::InvalidArgument("Offset did not match what was asked for",
        Substitute("$0 vs $1", offset, chunk.offset()));
  }

  // Verify the checksum.
  uint32_t crc32 = crc::Crc32c(chunk.data().data(), chunk.data().length());
  if (PREDICT_FALSE(crc32 != chunk.crc32())) {
    return Status::Corruption(
        Substitute("CRC32 does not match at offset $0 size $1: $2 vs $3",
          offset, chunk.data().size(), crc32, chunk.crc32()));
  }
  return Status::OK();
}

string RemoteBootstrapClient::LogPrefix() {
  return Substitute("T $0 P $1: Remote bootstrap client: ", tablet_id_, permanent_uuid_);
}

} // namespace tserver
} // namespace kudu
