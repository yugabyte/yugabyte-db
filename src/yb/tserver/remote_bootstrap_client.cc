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
#include "yb/consensus/consensus.h"
#include "yb/consensus/consensus_meta.h"
#include "yb/consensus/metadata.pb.h"
#include "yb/fs/fs_manager.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/gutil/strings/util.h"
#include "yb/gutil/walltime.h"
#include "yb/rpc/messenger.h"
#include "yb/rpc/rpc_controller.h"
#include "yb/tablet/tablet.pb.h"
#include "yb/tablet/tablet.h"
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
#include "yb/util/net/net_util.h"
#include "yb/util/net/rate_limiter.h"
#include "yb/util/scope_exit.h"
#include "yb/util/size_literals.h"

using namespace yb::size_literals;

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

DEFINE_int32(remote_bootstrap_max_chunk_size, 1_MB,
             "Maximum chunk size to be transferred at a time during remote bootstrap.");

DEFINE_test_flag(int32, simulate_long_remote_bootstrap_sec, 0,
                 "The remote bootstrap client will take at least this number of seconds to finish. "
                 "We use this for testing a scenario where a remote bootstrap takes longer than "
                 "follower_unavailable_considered_failed_sec seconds.");

// Deprecated because it's misspelled.  But if set, this flag takes precedence over
// remote_bootstrap_rate_limit_bytes_per_sec for compatibility.
DEFINE_int64(remote_boostrap_rate_limit_bytes_per_sec, 0,
             "DEPRECATED. Replaced by flag remote_bootstrap_rate_limit_bytes_per_sec.");
TAG_FLAG(remote_boostrap_rate_limit_bytes_per_sec, hidden);

DEFINE_int64(remote_bootstrap_rate_limit_bytes_per_sec, 256_MB,
             "Maximum transmission rate during a remote bootstrap. This is across all the remote "
             "bootstrap sessions for which this process is acting as a sender or receiver. So "
             "the total limit will be 2 * remote_bootstrap_rate_limit_bytes_per_sec because a "
             "tserver or master can act both as a sender and receiver at the same time.");

DEFINE_int32(bytes_remote_bootstrap_durable_write_mb, 8,
             "Explicitly call fsync after downloading the specified amount of data in MB "
             "during a remote bootstrap session. If 0 fsync() is not called.");

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
using rpc::Messenger;
using std::shared_ptr;
using std::string;
using std::vector;
using strings::Substitute;
using tablet::TabletDataState;
using tablet::TabletDataState_Name;
using tablet::RaftGroupMetadata;
using tablet::RaftGroupMetadataPtr;
using tablet::TabletStatusListener;
using tablet::RaftGroupReplicaSuperBlockPB;

constexpr int kBytesReservedForMessageHeaders = 16384;
std::atomic<int32_t> RemoteBootstrapClient::n_started_(0);

namespace {

// Decode the remote error into a human-readable Status object.
CHECKED_STATUS ExtractRemoteError(
    const rpc::ErrorStatusPB& remote_error, const Status& original_status) {
  if (!remote_error.HasExtension(RemoteBootstrapErrorPB::remote_bootstrap_error_ext)) {
    return original_status;
  }

  const RemoteBootstrapErrorPB& error =
      remote_error.GetExtension(RemoteBootstrapErrorPB::remote_bootstrap_error_ext);
  LOG(INFO) << "ExtractRemoteError: " << error.ShortDebugString();
  return StatusFromPB(error.status()).CloneAndPrepend(
      "Received error code " + RemoteBootstrapErrorPB::Code_Name(error.code()) +
          " from remote service");
}

// Enhance a RemoteError Status message with additional details from the remote.
CHECKED_STATUS UnwindRemoteError(const Status& status, const rpc::RpcController& controller) {
  if (!status.IsRemoteError()) {
    return status;
  }
  return ExtractRemoteError(*controller.error_response(), status);
}

} // namespace

RemoteBootstrapClient::RemoteBootstrapClient(std::string tablet_id,
                                             FsManager* fs_manager,
                                             string client_permanent_uuid)
    : tablet_id_(std::move(tablet_id)),
      fs_manager_(fs_manager),
      permanent_uuid_(std::move(client_permanent_uuid)),
      started_(false),
      downloaded_wal_(false),
      downloaded_blocks_(false),
      downloaded_rocksdb_files_(false),
      replace_tombstoned_tablet_(false),
      status_listener_(nullptr),
      session_idle_timeout_millis_(0),
      start_time_micros_(0),
      succeeded_(false),
      log_prefix_(Format("T $0 P$ 1: Remote bootstrap client: ", tablet_id_, permanent_uuid_)) {}

RemoteBootstrapClient::~RemoteBootstrapClient() {
  // Note: Ending the remote bootstrap session releases anchors on the remote.
  // This assumes that succeeded_ only gets set to true in Finish() just before calling
  // EndRemoteSession. If this didn't happen, then close the session here.
  if (!succeeded_) {
    LOG_WITH_PREFIX(INFO) << "Closing remote bootstrap session " << session_id_
                          << " in RemoteBootstrapClient destructor.";
    WARN_NOT_OK(EndRemoteSession(),
                LogPrefix() + "Unable to close remote bootstrap session " + session_id_);
  }
  if (started_) {
    auto old_count = n_started_.fetch_sub(1, std::memory_order_acq_rel);
    if (old_count < 1) {
      LOG_WITH_PREFIX(DFATAL) << "Invalid number of remote bootstrap sessions: " << old_count;
    }
  }
}

Status RemoteBootstrapClient::SetTabletToReplace(const RaftGroupMetadataPtr& meta,
                                                 int64_t caller_term) {
  CHECK_EQ(tablet_id_, meta->raft_group_id());
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
                                    rpc::ProxyCache* proxy_cache,
                                    const HostPort& bootstrap_peer_addr,
                                    RaftGroupMetadataPtr* meta,
                                    TSTabletManager* ts_manager) {
  CHECK(!started_);
  start_time_micros_ = GetCurrentTimeMicros();

  LOG_WITH_PREFIX(INFO) << "Beginning remote bootstrap session"
                        << " from remote peer at address " << bootstrap_peer_addr.ToString();

  // Set up an RPC proxy for the RemoteBootstrapService.
  proxy_.reset(new RemoteBootstrapServiceProxy(proxy_cache, bootstrap_peer_addr));

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
  RETURN_NOT_OK(MigrateSuperblock(resp.mutable_superblock()));

  auto* kv_store = resp.mutable_superblock()->mutable_kv_store();
  LOG_WITH_PREFIX(INFO) << "RocksDB files: " << yb::ToString(kv_store->rocksdb_files());
  LOG_WITH_PREFIX(INFO) << "Snapshot files: " << yb::ToString(kv_store->snapshot_files());
  if (first_wal_seqno_) {
    LOG_WITH_PREFIX(INFO) << "First WAL segment: " << first_wal_seqno_;
  } else {
    LOG_WITH_PREFIX(INFO) << "Log files: " << yb::ToString(resp.deprecated_wal_segment_seqnos());
  }

  const TableId table_id = resp.superblock().primary_table_id();
  const bool colocated = resp.superblock().colocated();
  const tablet::TableInfoPB* table_ptr = nullptr;
  for (auto& table_pb : kv_store->tables()) {
    if (table_pb.table_id() == table_id) {
      table_ptr = &table_pb;
      break;
    }
  }
  if (!table_ptr) {
    return STATUS(InvalidArgument, Format(
        "Tablet $0: Superblock's KV-store doesn't contain primary table $1", tablet_id_,
        table_id));
  }
  const auto& table = *table_ptr;

  session_id_ = resp.session_id();
  LOG_WITH_PREFIX(INFO) << "Began remote bootstrap session " << session_id_;

  session_idle_timeout_millis_ = resp.session_idle_timeout_millis();
  superblock_.reset(resp.release_superblock());

  // Clear fields rocksdb_dir and wal_dir so we get an error if we try to use them without setting
  // them to the right path.
  kv_store->clear_rocksdb_dir();
  superblock_->clear_wal_dir();

  superblock_->set_tablet_data_state(tablet::TABLET_DATA_COPYING);
  wal_seqnos_.assign(resp.deprecated_wal_segment_seqnos().begin(),
                     resp.deprecated_wal_segment_seqnos().end());
  if (resp.has_first_wal_segment_seqno()) {
    first_wal_seqno_ = resp.first_wal_segment_seqno();
  } else {
    first_wal_seqno_ = 0;
  }
  remote_committed_cstate_.reset(resp.release_initial_committed_cstate());

  Schema schema;
  RETURN_NOT_OK_PREPEND(SchemaFromPB(
      table.schema(), &schema), "Cannot deserialize schema from remote superblock");
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
    kv_store->set_rocksdb_dir(meta_->rocksdb_dir());

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
      // TODO: This will need to be fixed.
      ts_manager->RegisterDataAndWalDir(fs_manager_,
                                        table_id,
          meta_->raft_group_id(),
                                        meta_->table_type(),
                                        data_root_dir,
                                        wal_root_dir);
    }
  } else {
    Partition partition;
    Partition::FromPB(superblock_->partition(), &partition);
    PartitionSchema partition_schema;
    RETURN_NOT_OK(PartitionSchema::FromPB(table.partition_schema(), schema, &partition_schema));
    // Create the superblock on disk.
    if (ts_manager != nullptr && !colocated) {
      // TODO: GetAndRegisterDataAndWalDir is in charge of load balancing the disk storage.
      // Collocated tables are mostly for small clusters, single nodes. But we still need to make
      // sure that if the node has more than one data disk, we are balancing the storage correctly.
      ts_manager->GetAndRegisterDataAndWalDir(fs_manager_,
                                              table_id,
                                              tablet_id_,
                                              table.table_type(),
                                              &data_root_dir,
                                              &wal_root_dir);
    }
    Status create_status = RaftGroupMetadata::CreateNew(fs_manager_,
                                                     table_id,
                                                     tablet_id_,
                                                     table.table_name(),
                                                     table.table_type(),
                                                     schema,
                                                     IndexMap(table.indexes()),
                                                     partition_schema,
                                                     partition,
                                                     table.has_index_info() ?
                                                     boost::optional<IndexInfo>(
                                                         table.index_info()) :
                                                     boost::none,
                                                     table.schema_version(),
                                                     tablet::TABLET_DATA_COPYING,
                                                     &meta_,
                                                     data_root_dir,
                                                     wal_root_dir,
                                                     colocated);
    if (ts_manager != nullptr && !create_status.ok()) {
      ts_manager->UnregisterDataWalDir(table_id,
                                       tablet_id_,
                                       table.table_type(),
                                       data_root_dir,
                                       wal_root_dir);
    }
    RETURN_NOT_OK(create_status);

    vector<DeletedColumn> deleted_cols;
    for (const DeletedColumnPB& col_pb : table.deleted_cols()) {
      DeletedColumn col;
      RETURN_NOT_OK(DeletedColumn::FromPB(col_pb, &col));
      deleted_cols.push_back(col);
    }
    meta_->SetSchema(schema,
                     IndexMap(table.indexes()),
                     deleted_cols,
                     table.schema_version());

    // Replace rocksdb_dir in the received superblock with our rocksdb_dir.
    kv_store->set_rocksdb_dir(meta_->rocksdb_dir());

    // Replace wal_dir in the received superblock with our assigned wal_dir.
    superblock_->set_wal_dir(meta_->wal_dir());
  }

  started_ = true;
  auto old_count = n_started_.fetch_add(1, std::memory_order_acq_rel);
  if (old_count < 0) {
    LOG_WITH_PREFIX(DFATAL) << "Invalid number of remote bootstrap sessions: " << old_count;
    n_started_ = 0;
  }

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

  // We sleep here to simulate the transfer of very large files.
  if (PREDICT_FALSE(FLAGS_simulate_long_remote_bootstrap_sec > 0)) {
    LOG_WITH_PREFIX(INFO) << "Sleeping " << FLAGS_simulate_long_remote_bootstrap_sec
                          << " seconds to simulate the transfer of very large files";
    SleepFor(MonoDelta::FromSeconds(FLAGS_simulate_long_remote_bootstrap_sec));
  }
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
    string meta_path = fs_manager_->GetRaftGroupMetadataPath(tablet_id_);
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

Status RemoteBootstrapClient::VerifyChangeRoleSucceeded(
    const shared_ptr<consensus::Consensus>& shared_consensus) {

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
  controller.set_timeout(MonoDelta::FromSeconds(FLAGS_remote_bootstrap_end_session_timeout_sec));

  EndRemoteBootstrapSessionRequestPB req;
  req.set_session_id(session_id_);
  req.set_is_success(succeeded_);
  req.set_keep_session(succeeded_);
  EndRemoteBootstrapSessionResponsePB resp;

  LOG_WITH_PREFIX(INFO) << "Ending remote bootstrap session " << session_id_;
  auto status = proxy_->EndRemoteBootstrapSession(req, &resp, &controller);
  if (status.ok()) {
    remove_required_ = resp.session_kept();
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
  return status.CloneAndPrepend(Substitute("Failed to end remote bootstrap session $0",
                                           session_id_));
}

Status RemoteBootstrapClient::Remove() {
  if (!remove_required_) {
    return Status::OK();
  }

  rpc::RpcController controller;
  controller.set_timeout(MonoDelta::FromSeconds(FLAGS_remote_bootstrap_end_session_timeout_sec));

  RemoveSessionRequestPB req;
  req.set_session_id(session_id_);
  RemoveSessionResponsePB resp;

  LOG_WITH_PREFIX(INFO) << "Removing remote bootstrap session " << session_id_;
  const auto status = proxy_->RemoveSession(req, &resp, &controller);
  if (status.ok()) {
    LOG_WITH_PREFIX(INFO) << "Remote bootstrap session " << session_id_ << " removed successfully";
    return Status::OK();
  }

  return UnwindRemoteError(status, controller).CloneAndPrepend(
      Format("Failure removing remote bootstrap session $0", session_id_));
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
  uint64_t counter = 0;
  if (first_wal_seqno_) {
    LOG_WITH_PREFIX(INFO) << "Starting download of WAL segments starting from sequence number "
                          << first_wal_seqno_;
    for (;;) {
      uint64_t segment_seqno = first_wal_seqno_ + counter;
      UpdateStatusMessage(
          Format("Downloading WAL segment with seq. number $0 (#$1 in this session)",
                 segment_seqno, counter + 1));
      auto download_status = DownloadWAL(segment_seqno);
      if (!download_status.ok()) {
        std::string message_suffix;
        if (counter > 0) {
          message_suffix = Format(", downloaded segments in range: $0..$1",
                                      first_wal_seqno_, segment_seqno - 1);
        } else {
          message_suffix = ", no segments were downloaded";
        }
        if (download_status.IsNotFound()) {
          LOG_WITH_PREFIX(INFO) << "Stopped downloading WAL segments" << message_suffix;
          break;
        }
        LOG_WITH_PREFIX(WARNING) << "Downloading WAL segments failed: "
                                 << download_status << message_suffix;
        return download_status;
      }
      ++counter;
    }
  } else {
    int num_segments = wal_seqnos_.size();
    LOG_WITH_PREFIX(INFO) << "Starting download of " << num_segments << " WAL segments...";
    for (uint64_t seg_seqno : wal_seqnos_) {
      UpdateStatusMessage(Substitute("Downloading WAL segment with seq. number $0 ($1/$2)",
                                     seg_seqno, counter + 1, num_segments));
      RETURN_NOT_OK(DownloadWAL(seg_seqno));
      ++counter;
    }
  }

  if (FLAGS_bytes_remote_bootstrap_durable_write_mb != 0) {
    // Persist directory so that recently downloaded files are accessible.
    RETURN_NOT_OK_PREPEND(fs_manager_->env()->SyncDir(wal_table_top_dir),
                          Substitute("Failed to sync WAL table directory $0", wal_table_top_dir));
  }

  downloaded_wal_ = true;
  return Status::OK();
}

Status RemoteBootstrapClient::DownloadFile(
    const tablet::FilePB& file_pb, const std::string& dir, DataIdPB *data_id) {
  auto file_path = JoinPathSegments(dir, file_pb.name());
  RETURN_NOT_OK(fs_manager_->env()->CreateDirs(DirName(file_path)));

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
  std::unique_ptr<WritableFile> file;
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
  gscoped_ptr<RaftGroupReplicaSuperBlockPB> new_sb(new RaftGroupReplicaSuperBlockPB());
  new_sb->CopyFrom(*superblock_);
  const auto& rocksdb_dir = meta_->rocksdb_dir();
  // Replace rocksdb_dir with our rocksdb_dir
  new_sb->mutable_kv_store()->set_rocksdb_dir(rocksdb_dir);

  RETURN_NOT_OK(CreateTabletDirectories(rocksdb_dir, meta_->fs_manager()));

  DataIdPB data_id;
  data_id.set_type(DataIdPB::ROCKSDB_FILE);
  for (auto const& file_pb : new_sb->kv_store().rocksdb_files()) {
    auto start = MonoTime::Now();
    RETURN_NOT_OK(DownloadFile(file_pb, rocksdb_dir, &data_id));
    auto elapsed = MonoTime::Now().GetDeltaSince(start);
    LOG_WITH_PREFIX(INFO)
        << "Downloaded file " << file_pb.name() << " of size " << file_pb.size_bytes()
        << " in " << elapsed.ToSeconds() << " seconds";
  }

  // To avoid adding new file type to remote bootstrap we move intents as subdir of regular DB.
  auto intents_tmp_dir = JoinPathSegments(rocksdb_dir, tablet::kIntentsSubdir);
  if (fs_manager_->env()->FileExists(intents_tmp_dir)) {
    auto intents_dir = rocksdb_dir + tablet::kIntentsDBSuffix;
    LOG_WITH_PREFIX(INFO) << "Moving intents DB: " << intents_tmp_dir << " => " << intents_dir;
    RETURN_NOT_OK(fs_manager_->env()->RenameFile(intents_tmp_dir, intents_dir));
  }
  if (FLAGS_bytes_remote_bootstrap_durable_write_mb != 0) {
    // Persist directory so that recently downloaded files are accessible.
    RETURN_NOT_OK(fs_manager_->env()->SyncDir(rocksdb_dir));
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
  const auto temp_dest_path = dest_path + ".tmp";
  bool ok = false;
  auto se = ScopeExit([this, &temp_dest_path, &ok] {
    if (!ok) {
      WARN_NOT_OK(fs_manager_->env()->DeleteFile(temp_dest_path),
                  "Failed to delete temporary WAL segment");
    }
  });

  WritableFileOptions opts;
  opts.sync_on_close = true;
  std::unique_ptr<WritableFile> writer;
  RETURN_NOT_OK_PREPEND(fs_manager_->env()->NewWritableFile(opts, temp_dest_path, &writer),
                        "Unable to open file for writing");

  auto start = MonoTime::Now();
  RETURN_NOT_OK_PREPEND(DownloadFile(data_id, writer.get()),
                        Substitute("Unable to download WAL segment with seq. number $0",
                                   wal_segment_seqno));
  RETURN_NOT_OK(fs_manager_->env()->RenameFile(temp_dest_path, dest_path));
  auto elapsed = MonoTime::Now().GetDeltaSince(start);
  LOG_WITH_PREFIX(INFO) << "Downloaded WAL segment with seq. number " << wal_segment_seqno
                        << " of size " << writer->Size() << " in " << elapsed.ToSeconds()
                        << " seconds";
  ok = true;

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

template<class Appendable>
Status RemoteBootstrapClient::DownloadFile(const DataIdPB& data_id,
                                           Appendable* appendable) {
  // For periodic sync, indicates number of bytes which need to be sync'ed.
  size_t periodic_sync_unsynced_bytes = 0;
  uint64_t offset = 0;
  int32_t max_length = std::min(FLAGS_remote_bootstrap_max_chunk_size,
                                FLAGS_rpc_max_message_size - kBytesReservedForMessageHeaders);

  std::unique_ptr<RateLimiter> rate_limiter;

  if (FLAGS_remote_bootstrap_rate_limit_bytes_per_sec > 0) {
    static auto rate_updater = []() {
      if (n_started_.load(std::memory_order_acquire) < 1) {
        YB_LOG_EVERY_N(ERROR, 100) << "Invalid number of remote bootstrap sessions: " << n_started_;
        return static_cast<uint64_t>(FLAGS_remote_bootstrap_rate_limit_bytes_per_sec);
      }
      return static_cast<uint64_t>(FLAGS_remote_bootstrap_rate_limit_bytes_per_sec / n_started_);
    };

    rate_limiter = std::make_unique<RateLimiter>(rate_updater);
  } else {
    // Inactive RateLimiter.
    rate_limiter = std::make_unique<RateLimiter>();
  }

  rpc::RpcController controller;
  controller.set_timeout(MonoDelta::FromMilliseconds(session_idle_timeout_millis_));
  FetchDataRequestPB req;

  bool done = false;
  while (!done) {
    controller.Reset();
    req.set_session_id(session_id_);
    req.mutable_data_id()->CopyFrom(data_id);
    req.set_offset(offset);
    if (rate_limiter->active()) {
      auto max_size = rate_limiter->GetMaxSizeForNextTransmission();
      if (max_size > std::numeric_limits<decltype(max_length)>::max()) {
        max_size = std::numeric_limits<decltype(max_length)>::max();
      }
      max_length = std::min(max_length, decltype(max_length)(max_size));
    }
    req.set_max_length(max_length);

    FetchDataResponsePB resp;
    auto status = rate_limiter->SendOrReceiveData([this, &req, &resp, &controller]() {
      return proxy_->FetchData(req, &resp, &controller);
    }, [&resp]() { return resp.ByteSize(); });
    RETURN_NOT_OK_UNWIND_PREPEND(status, controller, "Unable to fetch data from remote");
    DCHECK_LE(resp.chunk().data().size(), max_length);

    // Sanity-check for corruption.
    RETURN_NOT_OK_PREPEND(VerifyData(offset, resp.chunk()),
                          Substitute("Error validating data item $0", data_id.ShortDebugString()));

    // Write the data.
    RETURN_NOT_OK(appendable->Append(resp.chunk().data()));
    VLOG_WITH_PREFIX(3)
        << "resp size: " << resp.ByteSize() << ", chunk size: " << resp.chunk().data().size();

    if (offset + resp.chunk().data().size() == resp.chunk().total_data_length()) {
      done = true;
    }
    offset += resp.chunk().data().size();
    if (FLAGS_bytes_remote_bootstrap_durable_write_mb != 0) {
      periodic_sync_unsynced_bytes += resp.chunk().data().size();
      if (periodic_sync_unsynced_bytes > FLAGS_bytes_remote_bootstrap_durable_write_mb * 1_MB) {
        RETURN_NOT_OK(appendable->Sync());
        periodic_sync_unsynced_bytes = 0;
      }
    }
  }

  VLOG_WITH_PREFIX(2) << "Transmission rate: " << rate_limiter->GetRate();

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

} // namespace tserver
} // namespace yb
