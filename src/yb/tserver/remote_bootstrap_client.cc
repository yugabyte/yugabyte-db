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

#include "yb/tserver/remote_bootstrap_client.h"

#include <gflags/gflags.h>
#include <glog/logging.h>

#include "yb/common/wire_protocol.h"
#include "yb/consensus/consensus_meta.h"
#include "yb/consensus/metadata.pb.h"
#include "yb/fs/block_id.h"
#include "yb/fs/block_manager.h"
#include "yb/fs/fs_manager.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/gutil/strings/util.h"
#include "yb/gutil/walltime.h"
#include "yb/rpc/messenger.h"
#include "yb/rpc/rpc_controller.h"
#include "yb/tablet/tablet.pb.h"
#include "yb/tablet/tablet_bootstrap_if.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tserver/remote_bootstrap.pb.h"
#include "yb/tserver/remote_bootstrap.proxy.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/util/crc.h"
#include "yb/util/env.h"
#include "yb/util/env_util.h"
#include "yb/util/fault_injection.h"
#include "yb/util/flag_tags.h"
#include "yb/util/logging.h"
#include "yb/util/net/net_util.h"

DEFINE_int32(remote_bootstrap_begin_session_timeout_ms, 3000,
             "Tablet server RPC client timeout for BeginRemoteBootstrapSession calls.");
TAG_FLAG(remote_bootstrap_begin_session_timeout_ms, hidden);

DEFINE_int32(remote_bootstrap_end_session_timeout_sec, 15,
             "Tablet server RPC client timeout for EndRemoteBootstrapSession calls. "
             "The timeout is usually a large value because we have to wait for the remote server "
             "to get a CHANGE_ROLE config change accepted.");
TAG_FLAG(remote_bootstrap_end_session_timeout_sec, hidden);

DEFINE_bool(remote_bootstrap_save_downloaded_metadata, false,
            "Save copies of the downloaded remote bootstrap files for debugging purposes. "
            "Note: This is only intended for debugging and should not be normally used!");
TAG_FLAG(remote_bootstrap_save_downloaded_metadata, advanced);
TAG_FLAG(remote_bootstrap_save_downloaded_metadata, hidden);
TAG_FLAG(remote_bootstrap_save_downloaded_metadata, runtime);

DEFINE_int32(committed_config_change_role_timeout_sec, 30,
             "Number of seconds to wait for the CHANGE_ROLE to be in the committed config before "
             "timing out. ");
TAG_FLAG(committed_config_change_role_timeout_sec, hidden);

DECLARE_int32(rpc_max_message_size);

DEFINE_test_flag(double, fault_crash_bootstrap_client_before_changing_role, 0.0,
                 "The remote bootstrap client will crash before closing the session with the "
                 "leader. Because the session won't be closed successfully, the leader won't issue "
                 "a ChangeConfig request to change this tserver role *(from PRE_VOTER or "
                 "PRE_OBSERVER to VOTER or OBSERVER respectively).");

// RETURN_NOT_OK_PREPEND() with a remote-error unwinding step.
#define RETURN_NOT_OK_UNWIND_PREPEND(status, controller, msg) \
  RETURN_NOT_OK_PREPEND(UnwindRemoteError(status, controller), msg)

namespace yb {
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
      downloaded_rocksdb_files_(false),
      replace_tombstoned_tablet_(false),
      status_listener_(nullptr),
      session_idle_timeout_millis_(0),
      start_time_micros_(0),
      succeeded_(false) {}

RemoteBootstrapClient::~RemoteBootstrapClient() {
  // Note: Ending the remote bootstrap session releases anchors on the remote.
  // This assumes that succeeded_ only gets set to true in Finish() just before calling
  // EndRemoteSession. If this didn't happen, then close the session here.
  if (!succeeded_) {
    LOG_WITH_PREFIX(INFO) << "Closing remote bootstrap session " << session_id_
                          << " in RemoteBootstrapClient destructor.";
    WARN_NOT_OK(EndRemoteSession(), "Unable to close remote bootstrap session " + session_id_);
  }
}

Status RemoteBootstrapClient::SetTabletToReplace(const scoped_refptr<TabletMetadata>& meta,
                                                 int64_t caller_term) {
  CHECK_EQ(tablet_id_, meta->tablet_id());
  TabletDataState data_state = meta->tablet_data_state();
  if (data_state != tablet::TABLET_DATA_TOMBSTONED) {
    return STATUS(IllegalState, Substitute("Tablet $0 not in tombstoned state: $1 ($2)",
                                           tablet_id_,
                                           TabletDataState_Name(data_state),
                                           data_state));
  }

  replace_tombstoned_tablet_ = true;
  meta_ = meta;

  int64_t last_logged_term = meta->tombstone_last_logged_opid().term;
  if (last_logged_term > caller_term) {
    return STATUS(InvalidArgument,
        Substitute("Leader has term $0 but the last log entry written by the tombstoned replica "
                   "for tablet $1 has higher term $2. Refusing remote bootstrap from leader",
                   caller_term, tablet_id_, last_logged_term));
  }

  // Load the old consensus metadata, if it exists.
  std::unique_ptr<ConsensusMetadata> cmeta;
  Status s = ConsensusMetadata::Load(fs_manager_, tablet_id_,
                                     fs_manager_->uuid(), &cmeta);
  if (s.IsNotFound()) {
    // The consensus metadata was not written to disk, possibly due to a failed
    // remote bootstrap.
    return Status::OK();
  }
  RETURN_NOT_OK(s);
  cmeta_ = std::move(cmeta);
  return Status::OK();
}

Status RemoteBootstrapClient::Start(const string& bootstrap_peer_uuid,
                                    const HostPort& bootstrap_peer_addr,
                                    scoped_refptr<TabletMetadata>* meta,
                                    TSTabletManager* ts_manager) {
  CHECK(!started_);
  start_time_micros_ = GetCurrentTimeMicros();

  Endpoint addr;
  RETURN_NOT_OK(EndpointFromHostPort(bootstrap_peer_addr, &addr));
  if (addr.address().is_unspecified()) {
    return STATUS_SUBSTITUTE(
        InvalidArgument,
        "Invalid wildcard address to remote bootstrap from: $0 (resolved to $1)",
        bootstrap_peer_addr.host(),
        addr.address().to_string());
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
  auto status =
      UnwindRemoteError(proxy_->BeginRemoteBootstrapSession(req, &resp, &controller), controller);

  if (!status.ok()) {
    status = status.CloneAndPrepend("Unable to begin remote bootstrap session");
    LOG_WITH_PREFIX(WARNING) << status;
    return status;
  }

  if (resp.superblock().tablet_data_state() != tablet::TABLET_DATA_READY) {
    Status s = STATUS(IllegalState, "Remote peer (" + bootstrap_peer_uuid + ")" +
                                    " is currently remotely bootstrapping itself!",
                                    resp.superblock().ShortDebugString());
    LOG_WITH_PREFIX(WARNING) << s.ToString();
    return s;
  }

  LOG_WITH_PREFIX(INFO) << "Received superblock: " << resp.superblock().ShortDebugString();
  LOG_WITH_PREFIX(INFO) << "RocksDB files: " << yb::ToString(resp.superblock().rocksdb_files());
  LOG_WITH_PREFIX(INFO) << "Snapshot files: " << yb::ToString(resp.superblock().snapshot_files());

  session_id_ = resp.session_id();
  LOG_WITH_PREFIX(INFO) << "Began remote bootstrap session " << session_id_;

  session_idle_timeout_millis_ = resp.session_idle_timeout_millis();
  superblock_.reset(resp.release_superblock());

  // Clear fields rocksdb_dir and wal_dir so we get an error if we try to use them without setting
  // them to the right path.
  superblock_->clear_rocksdb_dir();
  superblock_->clear_wal_dir();

  superblock_->set_tablet_data_state(tablet::TABLET_DATA_COPYING);
  wal_seqnos_.assign(resp.wal_segment_seqnos().begin(), resp.wal_segment_seqnos().end());
  remote_committed_cstate_.reset(resp.release_initial_committed_cstate());

  Schema schema;
  RETURN_NOT_OK_PREPEND(SchemaFromPB(superblock_->schema(), &schema),
                        "Cannot deserialize schema from remote superblock");
  const string table_id = superblock_->table_id();
  string data_root_dir;
  string wal_root_dir;
  if (replace_tombstoned_tablet_) {
    // Also validate the term of the bootstrap source peer, in case they are
    // different. This is a sanity check that protects us in case a bug or
    // misconfiguration causes us to attempt to bootstrap from an out-of-date
    // source peer, even after passing the term check from the caller in
    // SetTabletToReplace().
    int64_t last_logged_term = meta_->tombstone_last_logged_opid().term;
    if (last_logged_term > remote_committed_cstate_->current_term()) {
      return STATUS(InvalidArgument,
          Substitute("Tablet $0: Bootstrap source has term $1 but "
                     "tombstoned replica has last-logged opid with higher term $2. "
                      "Refusing remote bootstrap from source peer $3",
                      tablet_id_,
                      remote_committed_cstate_->current_term(),
                      last_logged_term,
                      bootstrap_peer_uuid));
    }
    // Replace rocksdb_dir in the received superblock with our rocksdb_dir.
    superblock_->set_rocksdb_dir(meta_->rocksdb_dir());

    // Replace wal_dir in the received superblock with our assigned wal_dir.
    superblock_->set_wal_dir(meta_->wal_dir());

    // This will flush to disk, but we set the data state to COPYING above.
    RETURN_NOT_OK_PREPEND(meta_->ReplaceSuperBlock(*superblock_),
                          "Remote bootstrap unable to replace superblock on tablet " +
                          tablet_id_);
    // Update the directory assignment mapping.
    data_root_dir = meta_->data_root_dir();
    wal_root_dir = meta_->wal_root_dir();
    if (ts_manager != nullptr) {
      ts_manager->RegisterDataAndWalDir(fs_manager_,
                                        table_id,
                                        meta_->tablet_id(),
                                        meta_->table_type(),
                                        data_root_dir,
                                        wal_root_dir);
    }
  } else {
    Partition partition;
    Partition::FromPB(superblock_->partition(), &partition);
    PartitionSchema partition_schema;
    RETURN_NOT_OK(PartitionSchema::FromPB(superblock_->partition_schema(),
                                          schema, &partition_schema));
    // Create the superblock on disk.
    if (ts_manager != nullptr) {
      ts_manager->GetAndRegisterDataAndWalDir(fs_manager_,
                                              table_id,
                                              tablet_id_,
                                              superblock_->table_type(),
                                              &data_root_dir,
                                              &wal_root_dir);
    }
    Status create_status = TabletMetadata::CreateNew(fs_manager_,
                                                     table_id,
                                                     tablet_id_,
                                                     superblock_->table_name(),
                                                     superblock_->table_type(),
                                                     schema,
                                                     partition_schema,
                                                     partition,
                                                     tablet::TABLET_DATA_COPYING,
                                                     &meta_,
                                                     data_root_dir,
                                                     wal_root_dir);
    if (ts_manager != nullptr && !create_status.ok()) {
      ts_manager->UnregisterDataWalDir(table_id,
                                       tablet_id_,
                                       superblock_->table_type(),
                                       data_root_dir,
                                       wal_root_dir);
    }
    RETURN_NOT_OK(create_status);

    // Replace rocksdb_dir in the received superblock with our rocksdb_dir.
    superblock_->set_rocksdb_dir(meta_->rocksdb_dir());

    // Replace wal_dir in the received superblock with our assigned wal_dir.
    superblock_->set_wal_dir(meta_->wal_dir());
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

  VLOG_WITH_PREFIX(2) << "Fetching table_type: " << TableType_Name(meta_->table_type());
  RETURN_NOT_OK(DownloadRocksDBFiles());
  RETURN_NOT_OK(DownloadWALs());
  return Status::OK();
}

Status RemoteBootstrapClient::Finish() {
  CHECK(meta_);
  CHECK(started_);

  CHECK(downloaded_wal_);
  CHECK(downloaded_rocksdb_files_);

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

  succeeded_ = true;

  MAYBE_FAULT(FLAGS_fault_crash_bootstrap_client_before_changing_role);

  RETURN_NOT_OK_PREPEND(EndRemoteSession(), "Error closing remote bootstrap session " +
                        session_id_);
  return Status::OK();
}

Status RemoteBootstrapClient::VerifyRemoteBootstrapSucceeded(
    const scoped_refptr<consensus::Consensus>& shared_consensus) {

  if (!shared_consensus) {
    return STATUS(InvalidArgument, "Invalid consensus object");
  }

  auto start = MonoTime::Now();
  auto timeout = MonoDelta::FromSeconds(FLAGS_committed_config_change_role_timeout_sec);
  int backoff_ms = 1;
  const int kMaxBackoffMs = 256;
  RaftConfigPB committed_config;

  do {
    committed_config = shared_consensus->CommittedConfig();
    for (const auto &peer : committed_config.peers()) {
      if (peer.permanent_uuid() != fs_manager_->uuid()) {
        continue;
      }

      if (peer.member_type() == RaftPeerPB::VOTER || peer.member_type() == RaftPeerPB::OBSERVER) {
        return Status::OK();
      } else {
        SleepFor(MonoDelta::FromMilliseconds(backoff_ms));
        backoff_ms = min(backoff_ms << 1, kMaxBackoffMs);
        break;
      }
    }
  } while (MonoTime::Now().GetDeltaSince(start).LessThan(timeout));

  return STATUS(TimedOut,
                Substitute("Timed out waiting member type of peer $0 to change in the committed "
                           "config $1", fs_manager_->uuid(),
                           committed_config.ShortDebugString()));
}

// Decode the remote error into a human-readable Status object.
Status RemoteBootstrapClient::ExtractRemoteError(const rpc::ErrorStatusPB& remote_error) {
  if (PREDICT_TRUE(remote_error.HasExtension(RemoteBootstrapErrorPB::remote_bootstrap_error_ext))) {
    const RemoteBootstrapErrorPB& error =
        remote_error.GetExtension(RemoteBootstrapErrorPB::remote_bootstrap_error_ext);
    return StatusFromPB(error.status()).CloneAndPrepend("Received error code " +
              RemoteBootstrapErrorPB::Code_Name(error.code()) + " from remote service");
  } else {
    return STATUS(InvalidArgument, "Unable to decode remote bootstrap RPC error message",
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
  req.set_is_success(succeeded_);
  EndRemoteBootstrapSessionResponsePB resp;

  LOG_WITH_PREFIX(INFO) << "Ending remote bootstrap session " << session_id_;
  controller.set_timeout(MonoDelta::FromSeconds(FLAGS_remote_bootstrap_end_session_timeout_sec));
  auto status = proxy_->EndRemoteBootstrapSession(req, &resp, &controller);
  if (status.ok()) {
    LOG_WITH_PREFIX(INFO) << "Remote bootstrap session " << session_id_ << " ended successfully";
    return Status::OK();
  }

  if (status.IsTimedOut()) {
    // Ignore timeout errors since the server could have sent the ChangeConfig request and died
    // before replying. We need to check the config to verify that this server's role changed as
    // expected, in which case, the remote bootstrap was completed successfully.
    LOG_WITH_PREFIX(INFO) << "Remote bootstrap session " << session_id_ << " timed out";
    return Status::OK();
  }

  status = UnwindRemoteError(status, controller);
  return status.CloneAndPrepend(Substitute("Failure ending remote bootstrap session $0",
                                           session_id_));
}

Status RemoteBootstrapClient::DownloadWALs() {
  CHECK(started_);

  // Delete and recreate WAL dir if it already exists, to ensure stray files are
  // not kept from previous bootstraps and runs.
  const string& wal_dir = meta_->wal_dir();
  if (fs_manager_->env()->FileExists(wal_dir)) {
    RETURN_NOT_OK(fs_manager_->env()->DeleteRecursively(wal_dir));
  }
  auto wal_table_top_dir = DirName(wal_dir);
  RETURN_NOT_OK_PREPEND(fs_manager_->CreateDirIfMissing(wal_table_top_dir),
                        Substitute("Failed to create WAL table directory $0", wal_table_top_dir));

  // fsync() parent dir.
  RETURN_NOT_OK_PREPEND(fs_manager_->env()->SyncDir(DirName(wal_table_top_dir)),
                        Substitute("Failed to sync WAL root directory $0",
                                   DirName(wal_table_top_dir)));

  RETURN_NOT_OK_PREPEND(fs_manager_->env()->CreateDir(wal_dir),
                        Substitute("Failed to create WAL tablet directory $0", wal_dir));

  // fsync() parent dir.
  RETURN_NOT_OK_PREPEND(fs_manager_->env()->SyncDir(wal_table_top_dir),
                        Substitute("Failed to sync WAL table directory $0", wal_table_top_dir));

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

Status RemoteBootstrapClient::DownloadFile(
    const tablet::FilePB& file_pb, const std::string& dir, DataIdPB *data_id) {
  auto file_path = JoinPathSegments(dir, file_pb.name());
  if (file_pb.inode() != 0) {
    auto it = inode2file_.find(file_pb.inode());
    if (it != inode2file_.end()) {
      VLOG_WITH_PREFIX(2) << "File with the same inode already found: " << file_path
                          << " => " << it->second;
      auto link_status = fs_manager_->env()->LinkFile(it->second, file_path);
      if (link_status.ok()) {
        return Status::OK();
      }
      // TODO fallback to copy.
      LOG_WITH_PREFIX(ERROR) << "Failed to link file: " << file_path << " => " << it->second
                             << ": " << link_status;
    }
  }

  WritableFileOptions opts;
  opts.sync_on_close = true;
  gscoped_ptr<WritableFile> file;
  RETURN_NOT_OK(fs_manager_->env()->NewWritableFile(opts, file_path, &file));

  data_id->set_file_name(file_pb.name());
  RETURN_NOT_OK_PREPEND(DownloadFile(*data_id, file.get()),
                        Format("Unable to download $0 file $1",
                               DataIdPB::IdType_Name(data_id->type()), file_path));
  VLOG_WITH_PREFIX(2) << "Downloaded file " << file_path;

  if (file_pb.inode() != 0) {
    inode2file_.emplace(file_pb.inode(), file_path);
  }

  return Status::OK();
}

Status RemoteBootstrapClient::CreateTabletDirectories(const string& db_dir, FsManager* fs) {
  // Create the directory table-uuid first.
  RETURN_NOT_OK_PREPEND(fs->CreateDirIfMissing(DirName(db_dir)),
                        Substitute("Failed to create RocksDB table directory $0",
                                   DirName(db_dir)));

  RETURN_NOT_OK_PREPEND(fs->CreateDirIfMissing(db_dir),
                        Substitute("Failed to create RocksDB tablet directory $0",
                                   db_dir));
  return Status::OK();
}

Status RemoteBootstrapClient::DownloadRocksDBFiles() {
  gscoped_ptr<TabletSuperBlockPB> new_sb(new TabletSuperBlockPB());
  new_sb->CopyFrom(*superblock_);
  const auto& rocksdb_dir = meta_->rocksdb_dir();
  // Replace rocksdb_dir with our rocksdb_dir
  new_sb->set_rocksdb_dir(rocksdb_dir);

  RETURN_NOT_OK(CreateTabletDirectories(rocksdb_dir, meta_->fs_manager()));

  DataIdPB data_id;
  data_id.set_type(DataIdPB::ROCKSDB_FILE);
  for (auto const& file_pb : new_sb->rocksdb_files()) {
    RETURN_NOT_OK(DownloadFile(file_pb, rocksdb_dir, &data_id));
  }
  new_superblock_.swap(new_sb);
  downloaded_rocksdb_files_ = true;
  return Status::OK();
}

Status RemoteBootstrapClient::DownloadWAL(uint64_t wal_segment_seqno) {
  VLOG_WITH_PREFIX(1) << "Downloading WAL segment with seqno " << wal_segment_seqno;
  DataIdPB data_id;
  data_id.set_type(DataIdPB::LOG_SEGMENT);
  data_id.set_wal_segment_seqno(wal_segment_seqno);
  const string dest_path = fs_manager_->GetWalSegmentFileName(meta_->wal_dir(), wal_segment_seqno);

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
    std::unique_ptr<ConsensusMetadata> cmeta;
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
    return STATUS(InvalidArgument, "Offset did not match what was asked for",
        Substitute("$0 vs $1", offset, chunk.offset()));
  }

  // Verify the checksum.
  uint32_t crc32 = crc::Crc32c(chunk.data().data(), chunk.data().length());
  if (PREDICT_FALSE(crc32 != chunk.crc32())) {
    return STATUS(Corruption,
        Substitute("CRC32 does not match at offset $0 size $1: $2 vs $3",
          offset, chunk.data().size(), crc32, chunk.crc32()));
  }
  return Status::OK();
}

string RemoteBootstrapClient::LogPrefix() {
  return Substitute("T $0 P $1: Remote bootstrap client: ", tablet_id_, permanent_uuid_);
}

} // namespace tserver
} // namespace yb
