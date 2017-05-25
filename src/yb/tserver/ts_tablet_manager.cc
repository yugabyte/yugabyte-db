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

#include "yb/tserver/ts_tablet_manager.h"

#include <algorithm>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <boost/bind.hpp>
#include <boost/optional.hpp>
#include <boost/thread/shared_mutex.hpp>
#include <glog/logging.h>
#include "yb/common/wire_protocol.h"
#include "yb/consensus/consensus_meta.h"
#include "yb/consensus/log.h"
#include "yb/consensus/metadata.pb.h"
#include "yb/consensus/opid_util.h"
#include "yb/consensus/quorum_util.h"
#include "yb/fs/fs_manager.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/gutil/strings/util.h"
#include "yb/master/master.pb.h"
#include "yb/master/sys_catalog.h"
#include "yb/tablet/metadata.pb.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet.pb.h"
#include "yb/tablet/tablet_bootstrap.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tserver/heartbeater.h"
#include "yb/tserver/remote_bootstrap_client.h"
#include "yb/tserver/tablet_server.h"
#include "yb/util/debug/trace_event.h"
#include "yb/util/env.h"
#include "yb/util/env_util.h"
#include "yb/util/fault_injection.h"
#include "yb/util/flag_tags.h"
#include "yb/util/mem_tracker.h"
#include "yb/util/metrics.h"
#include "yb/util/pb_util.h"
#include "yb/util/stopwatch.h"
#include "yb/util/trace.h"

DEFINE_int32(num_tablets_to_open_simultaneously, 0,
             "Number of threads available to open tablets during startup. If this "
             "is set to 0 (the default), then the number of bootstrap threads will "
             "be set based on the number of data directories. If the data directories "
             "are on some very fast storage device such as SSD or a RAID array, it "
             "may make sense to manually tune this.");
TAG_FLAG(num_tablets_to_open_simultaneously, advanced);

DEFINE_int32(tablet_start_warn_threshold_ms, 500,
             "If a tablet takes more than this number of millis to start, issue "
             "a warning with a trace.");
TAG_FLAG(tablet_start_warn_threshold_ms, hidden);

DEFINE_double(fault_crash_after_blocks_deleted, 0.0,
              "Fraction of the time when the tablet will crash immediately "
              "after deleting the data blocks during tablet deletion. "
              "(For testing only!)");
TAG_FLAG(fault_crash_after_blocks_deleted, unsafe);

DEFINE_double(fault_crash_after_wal_deleted, 0.0,
              "Fraction of the time when the tablet will crash immediately "
              "after deleting the WAL segments during tablet deletion. "
              "(For testing only!)");
TAG_FLAG(fault_crash_after_wal_deleted, unsafe);

DEFINE_double(fault_crash_after_cmeta_deleted, 0.0,
              "Fraction of the time when the tablet will crash immediately "
              "after deleting the consensus metadata during tablet deletion. "
              "(For testing only!)");
TAG_FLAG(fault_crash_after_cmeta_deleted, unsafe);

DEFINE_double(fault_crash_after_rb_files_fetched, 0.0,
              "Fraction of the time when the tablet will crash immediately "
              "after fetching the files during a remote bootstrap but before "
              "marking the superblock as TABLET_DATA_READY. "
              "(For testing only!)");
TAG_FLAG(fault_crash_after_rb_files_fetched, unsafe);

DEFINE_int64(db_block_cache_size_bytes, -1,
             "Size of cross-tablet shared RocksDB block cache (in bytes). "
             "This defaults to -1 for system auto-generated default, which would use "
             "FLAGS_db_block_cache_ram_percentage to select a percentage of the total memory as "
             "the default size for the shared block cache. Value of -2 disables block cache.");

DEFINE_int32(db_block_cache_size_percentage, 50,
             "Default percentage of total available memory to use as block cache size, if not "
             "asking for a raw number, through FLAGS_db_block_cache_size_bytes.");

DEFINE_int32(sleep_after_tombstoning_tablet_secs, 0,
             "Whether we sleep in LogAndTombstone after calling DeleteTabletData "
             "(For testing only!)");
TAG_FLAG(sleep_after_tombstoning_tablet_secs, unsafe);
TAG_FLAG(sleep_after_tombstoning_tablet_secs, hidden);

namespace yb {
namespace tserver {

METRIC_DEFINE_histogram(server, op_apply_queue_length, "Operation Apply Queue Length",
                        MetricUnit::kTasks,
                        "Number of operations waiting to be applied to the tablet. "
                        "High queue lengths indicate that the server is unable to process "
                        "operations as fast as they are being written to the WAL.",
                        10000, 2);

METRIC_DEFINE_histogram(server, op_apply_queue_time, "Operation Apply Queue Time",
                        MetricUnit::kMicroseconds,
                        "Time that operations spent waiting in the apply queue before being "
                        "processed. High queue times indicate that the server is unable to "
                        "process operations as fast as they are being written to the WAL.",
                        10000000, 2);

METRIC_DEFINE_histogram(server, op_apply_run_time, "Operation Apply Run Time",
                        MetricUnit::kMicroseconds,
                        "Time that operations spent being applied to the tablet. "
                        "High values may indicate that the server is under-provisioned or "
                        "that operations consist of very large batches.",
                        10000000, 2);

using consensus::ConsensusMetadata;
using consensus::ConsensusStatePB;
using consensus::OpId;
using consensus::RaftConfigPB;
using consensus::RaftPeerPB;
using consensus::StartRemoteBootstrapRequestPB;
using log::Log;
using master::ReportedTabletPB;
using master::TabletReportPB;
using std::shared_ptr;
using std::string;
using std::vector;
using std::unordered_set;
using strings::Substitute;
using tablet::Tablet;
using tablet::TABLET_DATA_COPYING;
using tablet::TABLET_DATA_DELETED;
using tablet::TABLET_DATA_READY;
using tablet::TABLET_DATA_TOMBSTONED;
using tablet::TabletDataState;
using tablet::TabletMetadata;
using tablet::TabletPeer;
using tablet::TabletStatusListener;
using tablet::TabletStatusPB;
using tserver::RemoteBootstrapClient;

namespace {

constexpr int kDbCacheSizeUsePercentage = -1;
constexpr int kDbCacheSizeCacheDisabled = -2;

} // namespace

TSTabletManager::TSTabletManager(FsManager* fs_manager,
                                 TabletServer* server,
                                 MetricRegistry* metric_registry)
  : fs_manager_(fs_manager),
    server_(server),
    next_report_seq_(0),
    metric_registry_(metric_registry),
    state_(MANAGER_INITIALIZING) {

  CHECK_OK(ThreadPoolBuilder("apply").Build(&apply_pool_));
  apply_pool_->SetQueueLengthHistogram(
      METRIC_op_apply_queue_length.Instantiate(server_->metric_entity()));
  apply_pool_->SetQueueTimeMicrosHistogram(
      METRIC_op_apply_queue_time.Instantiate(server_->metric_entity()));
  apply_pool_->SetRunTimeMicrosHistogram(
      METRIC_op_apply_run_time.Instantiate(server_->metric_entity()));

  int64_t block_cache_size_bytes = FLAGS_db_block_cache_size_bytes;
  // Auto-compute size of block cache if asked to.
  if (FLAGS_db_block_cache_size_bytes == kDbCacheSizeUsePercentage) {
    int64_t total_ram;
    // Check some bounds.
    CHECK(FLAGS_db_block_cache_size_percentage > 0 && FLAGS_db_block_cache_size_percentage <= 100)
        << Substitute(
               "Flag tablet_block_cache_size_percentage must be between 0 and 100. Current value: "
               "$0",
               FLAGS_db_block_cache_size_percentage);
    CHECK_OK(Env::Default()->GetTotalRAMBytes(&total_ram));
    block_cache_size_bytes = total_ram * FLAGS_db_block_cache_size_percentage / 100;
  }
  if (FLAGS_db_block_cache_size_bytes != kDbCacheSizeCacheDisabled) {
    block_cache_ = rocksdb::NewLRUCache(block_cache_size_bytes);
  }
}

TSTabletManager::~TSTabletManager() {
}

Status TSTabletManager::Init() {
  CHECK_EQ(state(), MANAGER_INITIALIZING);

  // Start the threadpool we'll use to open tablets.
  // This has to be done in Init() instead of the constructor, since the
  // FsManager isn't initialized until this point.
  int max_bootstrap_threads = FLAGS_num_tablets_to_open_simultaneously;
  if (max_bootstrap_threads == 0) {
    // Default to the number of disks.
    max_bootstrap_threads = fs_manager_->GetDataRootDirs().size();
  }
  RETURN_NOT_OK(ThreadPoolBuilder("tablet-bootstrap")
                .set_max_threads(max_bootstrap_threads)
                .Build(&open_tablet_pool_));

  // Search for tablets in the metadata dir.
  vector<string> tablet_ids;
  RETURN_NOT_OK(fs_manager_->ListTabletIds(&tablet_ids));

  InitLocalRaftPeerPB();

  vector<scoped_refptr<TabletMetadata> > metas;

  // First, load all of the tablet metadata. We do this before we start
  // submitting the actual OpenTablet() tasks so that we don't have to compete
  // for disk resources, etc, with bootstrap processes and running tablets.
  for (const string& tablet_id : tablet_ids) {
    scoped_refptr<TabletMetadata> meta;
    RETURN_NOT_OK_PREPEND(OpenTabletMeta(tablet_id, &meta),
                          "Failed to open tablet metadata for tablet: " + tablet_id);
    if (PREDICT_FALSE(meta->tablet_data_state() != TABLET_DATA_READY)) {
      RETURN_NOT_OK(HandleNonReadyTabletOnStartup(meta));
      if (meta->tablet_data_state() == TABLET_DATA_TOMBSTONED) {
        RegisterDataAndWalDir(fs_manager_, meta->table_id(), meta->tablet_id(),
                      meta->table_type(), meta->data_root_dir(),
                      meta->wal_root_dir());
      }
      continue;
    }
    RegisterDataAndWalDir(fs_manager_, meta->table_id(), meta->tablet_id(),
                      meta->table_type(), meta->data_root_dir(),
                      meta->wal_root_dir());
    metas.push_back(meta);
  }

  // Now submit the "Open" task for each.
  for (const scoped_refptr<TabletMetadata>& meta : metas) {
    scoped_refptr<TransitionInProgressDeleter> deleter;
    {
      std::lock_guard<rw_spinlock> lock(lock_);
      CHECK_OK(StartTabletStateTransitionUnlocked(meta->tablet_id(), "opening tablet", &deleter));
    }

    scoped_refptr<TabletPeer> tablet_peer = CreateAndRegisterTabletPeer(meta, NEW_PEER);
    RETURN_NOT_OK(open_tablet_pool_->SubmitFunc(
        std::bind(&TSTabletManager::OpenTablet, this, meta, deleter)));
  }

  {
    std::lock_guard<rw_spinlock> lock(lock_);
    state_ = MANAGER_RUNNING;
  }

  return Status::OK();
}

Status TSTabletManager::WaitForAllBootstrapsToFinish() {
  CHECK_EQ(state(), MANAGER_RUNNING);

  open_tablet_pool_->Wait();

  Status s = Status::OK();

  boost::shared_lock<rw_spinlock> shared_lock(lock_);
  for (const TabletMap::value_type& entry : tablet_map_) {
    if (entry.second->state() == tablet::FAILED) {
      if (s.ok()) {
        s = entry.second->error();
      }
    }
  }

  return s;
}

Status TSTabletManager::CreateNewTablet(
    const string &table_id,
    const string &tablet_id,
    const Partition &partition,
    const string &table_name,
    TableType table_type,
    const Schema &schema,
    const PartitionSchema &partition_schema,
    RaftConfigPB config,
    scoped_refptr<TabletPeer> *tablet_peer) {
  CHECK_EQ(state(), MANAGER_RUNNING);

  for (int i = 0; i < config.peers_size(); ++i) {
    auto config_peer = config.mutable_peers(i);
    if (config_peer->has_member_type()) {
      return STATUS(IllegalState, Substitute("member_type shouldn't be set for config: { $0 }",
                                             config.ShortDebugString()));
    }
    config_peer->set_member_type(RaftPeerPB::VOTER);
  }

  CHECK(IsRaftConfigMember(server_->instance_pb().permanent_uuid(), config));

  // Set the initial opid_index for a RaftConfigPB to -1.
  config.set_opid_index(consensus::kInvalidOpIdIndex);

  scoped_refptr<TransitionInProgressDeleter> deleter;
  {
    // acquire the lock in exclusive mode as we'll add a entry to the
    // transition_in_progress_ set if the lookup fails.
    std::lock_guard<rw_spinlock> lock(lock_);
    TRACE("Acquired tablet manager lock");

    // Sanity check that the tablet isn't already registered.
    scoped_refptr<TabletPeer> junk;
    if (LookupTabletUnlocked(tablet_id, &junk)) {
      return STATUS(AlreadyPresent, "Tablet already registered", tablet_id);
    }

    // Sanity check that the tablet's creation isn't already in progress
    RETURN_NOT_OK(StartTabletStateTransitionUnlocked(tablet_id, "creating tablet", &deleter));
  }

  // Create the metadata.
  TRACE("Creating new metadata...");
  scoped_refptr<TabletMetadata> meta;
  string data_root_dir;
  string wal_root_dir;
  GetAndRegisterDataAndWalDir(fs_manager_, table_id, tablet_id, table_type,
                              &data_root_dir, &wal_root_dir);
  Status create_status = TabletMetadata::CreateNew(fs_manager_,
                                                   table_id,
                                                   tablet_id,
                                                   table_name,
                                                   table_type,
                                                   schema,
                                                   partition_schema,
                                                   partition,
                                                   TABLET_DATA_READY,
                                                   &meta,
                                                   data_root_dir,
                                                   wal_root_dir);
  if (!create_status.ok()) {
    UnregisterDataWalDir(table_id, tablet_id, table_type, data_root_dir, wal_root_dir);
  }
  RETURN_NOT_OK_PREPEND(create_status, "Couldn't create tablet metadata")
  LOG(INFO) << "Created tablet metadata for table: " << table_id << ", tablet: " << tablet_id;

  // We must persist the consensus metadata to disk before starting a new
  // tablet's TabletPeer and Consensus implementation.
  gscoped_ptr<ConsensusMetadata> cmeta;
  RETURN_NOT_OK_PREPEND(ConsensusMetadata::Create(fs_manager_, tablet_id, fs_manager_->uuid(),
                                                  config, consensus::kMinimumTerm, &cmeta),
                        "Unable to create new ConsensusMeta for tablet " + tablet_id);
  scoped_refptr<TabletPeer> new_peer = CreateAndRegisterTabletPeer(meta, NEW_PEER);

  // We can run this synchronously since there is nothing to bootstrap.
  RETURN_NOT_OK(
      open_tablet_pool_->SubmitFunc(std::bind(&TSTabletManager::OpenTablet, this, meta, deleter)));

  if (tablet_peer) {
    *tablet_peer = new_peer;
  }
  return Status::OK();
}

string LogPrefix(const string& tablet_id, const string& uuid) {
  return "T " + tablet_id + " P " + uuid + ": ";
}

Status CheckLeaderTermNotLower(
    const string& tablet_id,
    const string& uuid,
    int64_t leader_term,
    int64_t last_logged_term) {
  if (PREDICT_FALSE(leader_term < last_logged_term)) {
    Status s = STATUS(InvalidArgument,
        Substitute("Leader has replica of tablet $0 with term $1 lower than last "
                   "logged term $2 on local replica. Rejecting remote bootstrap request",
                   tablet_id, leader_term, last_logged_term));
    LOG(WARNING) << LogPrefix(tablet_id, uuid) << "Remote bootstrap: " << s.ToString();
    return s;
  }
  return Status::OK();
}

Status HandleReplacingStaleTablet(
    scoped_refptr<TabletMetadata> meta,
    scoped_refptr<tablet::TabletPeer> old_tablet_peer,
    const string& tablet_id,
    const string& uuid,
    const int64_t& leader_term) {
  TabletDataState data_state = meta->tablet_data_state();
  switch (data_state) {
    case TABLET_DATA_COPYING: {
      // This should not be possible due to the transition_in_progress_ "lock".
      LOG(FATAL) << LogPrefix(tablet_id, uuid) << " Remote bootstrap: "
                 << "Found tablet in TABLET_DATA_COPYING state during StartRemoteBootstrap()";
    }
    case TABLET_DATA_TOMBSTONED: {
      RETURN_NOT_OK(old_tablet_peer->CheckShutdownOrNotStarted());
      int64_t last_logged_term = meta->tombstone_last_logged_opid().term();
      RETURN_NOT_OK(CheckLeaderTermNotLower(tablet_id,
                                            uuid,
                                            leader_term,
                                            last_logged_term));
      break;
    }
    case TABLET_DATA_READY: {
      if (tablet_id == master::kSysCatalogTabletId) {
        LOG(FATAL) << LogPrefix(tablet_id, uuid) << " Remote bootstrap: "
                   << "Found tablet in TABLET_DATA_READY state during StartRemoteBootstrap()";
      }
      // There's a valid race here that can lead us to come here:
      // 1. Leader sends a second remote bootstrap request as a result of receiving a
      // TABLET_NOT_FOUND from this tserver while it was in the middle of a remote bootstrap.
      // 2. The remote bootstrap request arrives after the first one is finished, and it is able to
      // grab the mutex.
      // 3. This tserver finds that it already has the metadata for the tablet, and determines that
      // it needs to replace the tablet setting replacing_tablet to true.
      // In this case, the master can simply ignore this error.
      return STATUS(IllegalState, Substitute("Tablet $0 in TABLET_DATA_READY state", tablet_id));
    }
    default: {
      return STATUS(IllegalState,
          Substitute("Found tablet $0 in unexpected state $1 for remote bootstrap.",
                     tablet_id, TabletDataState_Name(data_state)));
    }
  }

  return Status::OK();
}

Status TSTabletManager::StartRemoteBootstrap(const StartRemoteBootstrapRequestPB& req) {
  const string& tablet_id = req.tablet_id();
  const string& bootstrap_peer_uuid = req.bootstrap_peer_uuid();
  HostPort bootstrap_peer_addr;
  RETURN_NOT_OK(HostPortFromPB(req.bootstrap_peer_addr(), &bootstrap_peer_addr));
  int64_t leader_term = req.caller_term();

  const string kLogPrefix = LogPrefix(tablet_id, fs_manager_->uuid());

  scoped_refptr<TabletPeer> old_tablet_peer;
  scoped_refptr<TabletMetadata> meta;
  bool replacing_tablet = false;
  scoped_refptr<TransitionInProgressDeleter> deleter;
  {
    std::lock_guard<rw_spinlock> lock(lock_);
    if (LookupTabletUnlocked(tablet_id, &old_tablet_peer)) {
      meta = old_tablet_peer->tablet_metadata();
      replacing_tablet = true;
    }
    RETURN_NOT_OK(StartTabletStateTransitionUnlocked(tablet_id,
        Substitute("remote bootstrapping tablet from peer $0", bootstrap_peer_uuid), &deleter));
  }

  if (replacing_tablet) {
    // Make sure the existing tablet peer is shut down and tombstoned.
    RETURN_NOT_OK(HandleReplacingStaleTablet(meta,
                                             old_tablet_peer,
                                             tablet_id,
                                             fs_manager_->uuid(),
                                             leader_term));
  }

  string init_msg = kLogPrefix + Substitute("Initiating remote bootstrap from Peer $0 ($1)",
                                            bootstrap_peer_uuid, bootstrap_peer_addr.ToString());
  LOG(INFO) << init_msg;
  TRACE(init_msg);

  gscoped_ptr<RemoteBootstrapClient> rb_client(
      new RemoteBootstrapClient(tablet_id, fs_manager_, server_->messenger(), fs_manager_->uuid()));

  // Download and persist the remote superblock in TABLET_DATA_COPYING state.
  if (replacing_tablet) {
    RETURN_NOT_OK(rb_client->SetTabletToReplace(meta, leader_term));
  }
  RETURN_NOT_OK(rb_client->Start(bootstrap_peer_uuid,
                                 bootstrap_peer_addr,
                                 &meta,
                                 this));

  // From this point onward, the superblock is persisted in TABLET_DATA_COPYING
  // state, and we need to tombstone the tablet if additional steps prior to
  // getting to a TABLET_DATA_READY state fail.

  // Registering a non-initialized TabletPeer offers visibility through the Web UI.
  RegisterTabletPeerMode mode = replacing_tablet ? REPLACEMENT_PEER : NEW_PEER;
  scoped_refptr<TabletPeer> tablet_peer = CreateAndRegisterTabletPeer(meta, mode);

  // Download all of the remote files.
  TOMBSTONE_NOT_OK(rb_client->FetchAll(tablet_peer->status_listener()),
                   meta,
                   fs_manager_->uuid(),
                   "Remote bootstrap: Unable to fetch data from remote peer " +
                       bootstrap_peer_uuid + " (" + bootstrap_peer_addr.ToString() + ")",
                   this);

  MAYBE_FAULT(FLAGS_fault_crash_after_rb_files_fetched);

  // Write out the last files to make the new replica visible and update the
  // TabletDataState in the superblock to TABLET_DATA_READY.
  // Finish() will call EndRemoteSession() and wait for the leader to successfully submit a
  // ChangeConfig request (to change this server's role from PRE_VOTER or PRE_OBSERVER to VOTER or
  // OBSERVER respectively). If the RPC times out, we will ignore the error (since the leader could
  // have successfully submitted the ChangeConfig request and failed to respond in time)
  // and check the committed config until we find that this server's role has changed, or until we
  // time out which will cause us to tombstone the tablet.
  TOMBSTONE_NOT_OK(rb_client->Finish(),
                   meta,
                   fs_manager_->uuid(),
                   "Remote bootstrap: Failure calling Finish()",
                   this);

  LOG(INFO) << kLogPrefix << "Remote bootstrap: Opening tablet";
  OpenTablet(meta, nullptr);

  // If OpenTablet fails, tablet_peer->error() will be set.
  SHUTDOWN_AND_TOMBSTONE_TABLET_PEER_NOT_OK(tablet_peer->error(),
                                            tablet_peer,
                                            meta,
                                            fs_manager_->uuid(),
                                            "Remote bootstrap: Failure calling OpenTablet()",
                                            this);

  SHUTDOWN_AND_TOMBSTONE_TABLET_PEER_NOT_OK(
      rb_client->VerifyRemoteBootstrapSucceeded(tablet_peer->shared_consensus()),
      tablet_peer,
      meta,
      fs_manager_->uuid(),
      "Remote bootstrap: Failure calling VerifyRemoteBootstrapSucceeded",
      this);

  LOG(INFO) << kLogPrefix << "Remote bootstrap for tablet ended successfully";

  return Status::OK();
}

// Create and register a new TabletPeer, given tablet metadata.
scoped_refptr<TabletPeer> TSTabletManager::CreateAndRegisterTabletPeer(
    const scoped_refptr<TabletMetadata>& meta, RegisterTabletPeerMode mode) {
  scoped_refptr<TabletPeer> tablet_peer(
      new TabletPeer(meta,
                     local_peer_pb_,
                     apply_pool_.get(),
                     Bind(&TSTabletManager::ApplyChange, Unretained(this), meta->tablet_id())));
  RegisterTablet(meta->tablet_id(), tablet_peer, mode);
  return tablet_peer;
}

Status TSTabletManager::DeleteTablet(
    const string& tablet_id,
    TabletDataState delete_type,
    const boost::optional<int64_t>& cas_config_opid_index_less_or_equal,
    boost::optional<TabletServerErrorPB::Code>* error_code) {

  if (delete_type != TABLET_DATA_DELETED && delete_type != TABLET_DATA_TOMBSTONED) {
    return STATUS(InvalidArgument, "DeleteTablet() requires an argument that is one of "
                                   "TABLET_DATA_DELETED or TABLET_DATA_TOMBSTONED",
                                   Substitute("Given: $0 ($1)",
                                              TabletDataState_Name(delete_type), delete_type));
  }

  TRACE("Deleting tablet $0", tablet_id);

  scoped_refptr<TabletPeer> tablet_peer;
  scoped_refptr<TransitionInProgressDeleter> deleter;
  {
    // Acquire the lock in exclusive mode as we'll add a entry to the
    // transition_in_progress_ map.
    std::lock_guard<rw_spinlock> lock(lock_);
    TRACE("Acquired tablet manager lock");
    RETURN_NOT_OK(CheckRunningUnlocked(error_code));

    if (!LookupTabletUnlocked(tablet_id, &tablet_peer)) {
      *error_code = TabletServerErrorPB::TABLET_NOT_FOUND;
      return STATUS(NotFound, "Tablet not found", tablet_id);
    }
    // Sanity check that the tablet's deletion isn't already in progress
    Status s = StartTabletStateTransitionUnlocked(tablet_id, "deleting tablet", &deleter);
    if (PREDICT_FALSE(!s.ok())) {
      *error_code = TabletServerErrorPB::TABLET_NOT_RUNNING;
      return s;
    }
  }

  // If the tablet is already deleted, the CAS check isn't possible because
  // consensus and therefore the log is not available.
  TabletDataState data_state = tablet_peer->tablet_metadata()->tablet_data_state();
  bool tablet_deleted = (data_state == TABLET_DATA_DELETED || data_state == TABLET_DATA_TOMBSTONED);

  // They specified an "atomic" delete. Check the committed config's opid_index.
  // TODO: There's actually a race here between the check and shutdown, but
  // it's tricky to fix. We could try checking again after the shutdown and
  // restarting the tablet if the local replica committed a higher config
  // change op during that time, or potentially something else more invasive.
  if (cas_config_opid_index_less_or_equal && !tablet_deleted) {
    scoped_refptr<consensus::Consensus> consensus = tablet_peer->shared_consensus();
    if (!consensus) {
      *error_code = TabletServerErrorPB::TABLET_NOT_RUNNING;
      return STATUS(IllegalState, "Consensus not available. Tablet shutting down");
    }
    RaftConfigPB committed_config = consensus->CommittedConfig();
    if (committed_config.opid_index() > *cas_config_opid_index_less_or_equal) {
      *error_code = TabletServerErrorPB::CAS_FAILED;
      return STATUS(IllegalState, Substitute("Request specified cas_config_opid_index_less_or_equal"
                                             " of $0 but the committed config has opid_index of $1",
                                             *cas_config_opid_index_less_or_equal,
                                             committed_config.opid_index()));
    }
  }

  scoped_refptr<TabletMetadata> meta = tablet_peer->tablet_metadata();
  tablet_peer->Shutdown();

  boost::optional<OpId> opt_last_logged_opid;
  if (tablet_peer->log()) {
    OpId last_logged_opid;
    tablet_peer->log()->GetLatestEntryOpId(&last_logged_opid);
    opt_last_logged_opid = last_logged_opid;
  }

  Status s = DeleteTabletData(meta,
                              delete_type,
                              fs_manager_->uuid(),
                              opt_last_logged_opid,
                              this);
  if (PREDICT_FALSE(!s.ok())) {
    s = s.CloneAndPrepend(Substitute("Unable to delete on-disk data from tablet $0",
                                     tablet_id));
    LOG(WARNING) << s.ToString();
    tablet_peer->SetFailed(s);
    return s;
  }

  tablet_peer->status_listener()->StatusMessage("Deleted tablet blocks from disk");

  // We only remove DELETED tablets from the tablet map.
  if (delete_type == TABLET_DATA_DELETED) {
    std::lock_guard<rw_spinlock> lock(lock_);
    RETURN_NOT_OK(CheckRunningUnlocked(error_code));
    CHECK_EQ(1, tablet_map_.erase(tablet_id)) << tablet_id;
    UnregisterDataWalDir(meta->table_id(),
                         tablet_id,
                         meta->table_type(),
                         meta->data_root_dir(),
                         meta->wal_root_dir());
}

  return Status::OK();
}

Status TSTabletManager::CheckRunningUnlocked(
    boost::optional<TabletServerErrorPB::Code>* error_code) const {
  if (state_ == MANAGER_RUNNING) {
    return Status::OK();
  }
  *error_code = TabletServerErrorPB::TABLET_NOT_RUNNING;
  return STATUS(ServiceUnavailable, Substitute("Tablet Manager is not running: $0",
                                               TSTabletManagerStatePB_Name(state_)));
}

Status TSTabletManager::StartTabletStateTransitionUnlocked(
    const string& tablet_id,
    const string& reason,
    scoped_refptr<TransitionInProgressDeleter>* deleter) {
  DCHECK(lock_.is_write_locked());
  if (!InsertIfNotPresent(&transition_in_progress_, tablet_id, reason)) {
    return STATUS(IllegalState,
        Substitute("State transition of tablet $0 already in progress: $1",
                    tablet_id, transition_in_progress_[tablet_id]));
  }
  deleter->reset(new TransitionInProgressDeleter(&transition_in_progress_, &lock_, tablet_id));
  return Status::OK();
}

bool TSTabletManager::IsTabletInTransition(const std::string& tablet_id) const {
  std::lock_guard<rw_spinlock> lock(lock_);
  return ContainsKey(transition_in_progress_, tablet_id);
}

Status TSTabletManager::OpenTabletMeta(const string& tablet_id,
                                       scoped_refptr<TabletMetadata>* metadata) {
  LOG(INFO) << "Loading metadata for tablet " << tablet_id;
  TRACE("Loading metadata...");
  scoped_refptr<TabletMetadata> meta;
  RETURN_NOT_OK_PREPEND(TabletMetadata::Load(fs_manager_, tablet_id, &meta),
                        strings::Substitute("Failed to load tablet metadata for tablet id $0",
                                            tablet_id));
  TRACE("Metadata loaded");
  metadata->swap(meta);
  return Status::OK();
}

void TSTabletManager::OpenTablet(const scoped_refptr<TabletMetadata>& meta,
                                 const scoped_refptr<TransitionInProgressDeleter>& deleter) {
  string tablet_id = meta->tablet_id();
  TRACE_EVENT1("tserver", "TSTabletManager::OpenTablet",
               "tablet_id", tablet_id);

  scoped_refptr<TabletPeer> tablet_peer;
  CHECK(LookupTablet(tablet_id, &tablet_peer))
      << "Tablet not registered prior to OpenTabletAsync call: " << tablet_id;

  shared_ptr<Tablet> tablet;
  scoped_refptr<Log> log;
  const string kLogPrefix = LogPrefix(tablet_id, fs_manager_->uuid());

  LOG(INFO) << kLogPrefix << "Bootstrapping tablet";
  TRACE("Bootstrapping tablet");

  consensus::ConsensusBootstrapInfo bootstrap_info;
  Status s;
  LOG_TIMING_PREFIX(INFO, kLogPrefix, "bootstrapping tablet") {
    // TODO: handle crash mid-creation of tablet? do we ever end up with a
    // partially created tablet here?
    tablet_peer->SetBootstrapping();
    s = BootstrapTablet(
        meta, scoped_refptr<server::Clock>(server_->clock()), server_->mem_tracker(),
        metric_registry_, tablet_peer->status_listener(), &tablet, &log,
        tablet_peer->log_anchor_registry(), &bootstrap_info, block_cache_);
    if (!s.ok()) {
      LOG(ERROR) << kLogPrefix << "Tablet failed to bootstrap: "
                 << s.ToString();
      tablet_peer->SetFailed(s);
      return;
    }
  }

  MonoTime start(MonoTime::Now(MonoTime::FINE));
  LOG_TIMING_PREFIX(INFO, kLogPrefix, "starting tablet") {
    TRACE("Initializing tablet peer");
    s =  tablet_peer->Init(tablet,
                           scoped_refptr<server::Clock>(server_->clock()),
                           server_->messenger(),
                           log,
                           tablet->GetMetricEntity());

    if (!s.ok()) {
      LOG(ERROR) << kLogPrefix << "Tablet failed to init: "
                 << s.ToString();
      tablet_peer->SetFailed(s);
      return;
    }

    TRACE("Starting tablet peer");
    s = tablet_peer->Start(bootstrap_info);
    if (!s.ok()) {
      LOG(ERROR) << kLogPrefix << "Tablet failed to start: "
                 << s.ToString();
      tablet_peer->SetFailed(s);
      return;
    }

    tablet_peer->RegisterMaintenanceOps(server_->maintenance_manager());
  }

  int elapsed_ms = MonoTime::Now(MonoTime::FINE).GetDeltaSince(start).ToMilliseconds();
  if (elapsed_ms > FLAGS_tablet_start_warn_threshold_ms) {
    LOG(WARNING) << kLogPrefix << "Tablet startup took " << elapsed_ms << "ms";
    if (Trace::CurrentTrace()) {
      LOG(WARNING) << kLogPrefix << "Trace:" << std::endl
                   << Trace::CurrentTrace()->DumpToString(true);
    }
  }
}

void TSTabletManager::Shutdown() {
  {
    std::lock_guard<rw_spinlock> lock(lock_);
    switch (state_) {
      case MANAGER_QUIESCING: {
        VLOG(1) << "Tablet manager shut down already in progress..";
        return;
      }
      case MANAGER_SHUTDOWN: {
        VLOG(1) << "Tablet manager has already been shut down.";
        return;
      }
      case MANAGER_INITIALIZING:
      case MANAGER_RUNNING: {
        LOG(INFO) << "Shutting down tablet manager...";
        state_ = MANAGER_QUIESCING;
        break;
      }
      default: {
        LOG(FATAL) << "Invalid state: " << TSTabletManagerStatePB_Name(state_);
      }
    }
  }

  // Shut down the bootstrap pool, so new tablets are registered after this point.
  open_tablet_pool_->Shutdown();

  // Take a snapshot of the peers list -- that way we don't have to hold
  // on to the lock while shutting them down, which might cause a lock
  // inversion. (see KUDU-308 for example).
  vector<scoped_refptr<TabletPeer> > peers_to_shutdown;
  GetTabletPeers(&peers_to_shutdown);

  for (const scoped_refptr<TabletPeer>& peer : peers_to_shutdown) {
    peer->Shutdown();
  }

  // Shut down the apply pool.
  apply_pool_->Shutdown();

  {
    std::lock_guard<rw_spinlock> l(lock_);
    // We don't expect anyone else to be modifying the map after we start the
    // shut down process.
    CHECK_EQ(tablet_map_.size(), peers_to_shutdown.size())
      << "Map contents changed during shutdown!";
    tablet_map_.clear();
    table_data_assignment_map_.clear();
    table_wal_assignment_map_.clear();

    state_ = MANAGER_SHUTDOWN;
  }
}

void TSTabletManager::RegisterTablet(const std::string& tablet_id,
                                     const scoped_refptr<TabletPeer>& tablet_peer,
                                     RegisterTabletPeerMode mode) {
  std::lock_guard<rw_spinlock> lock(lock_);
  // If we are replacing a tablet peer, we delete the existing one first.
  if (mode == REPLACEMENT_PEER && tablet_map_.erase(tablet_id) != 1) {
    LOG(FATAL) << "Unable to remove previous tablet peer " << tablet_id << ": not registered!";
  }
  if (!InsertIfNotPresent(&tablet_map_, tablet_id, tablet_peer)) {
    scoped_refptr<TabletMetadata> meta = tablet_peer->tablet_metadata();
    LOG(FATAL) << "Unable to register tablet peer " << tablet_id << ": already registered!";
  }

  LOG(INFO) << "Registered tablet " << tablet_id;
}

bool TSTabletManager::LookupTablet(const string& tablet_id,
                                   scoped_refptr<TabletPeer>* tablet_peer) const {
  boost::shared_lock<rw_spinlock> shared_lock(lock_);
  return LookupTabletUnlocked(tablet_id, tablet_peer);
}

bool TSTabletManager::LookupTabletUnlocked(const string& tablet_id,
                                           scoped_refptr<TabletPeer>* tablet_peer) const {
  const scoped_refptr<TabletPeer>* found = FindOrNull(tablet_map_, tablet_id);
  if (!found) {
    return false;
  }
  *tablet_peer = *found;
  return true;
}

Status TSTabletManager::GetTabletPeer(const string& tablet_id,
                                      scoped_refptr<tablet::TabletPeer>* tablet_peer) const {
  if (!LookupTablet(tablet_id, tablet_peer)) {
    return STATUS(NotFound, "Tablet not found", tablet_id);
  }
  TabletDataState data_state = (*tablet_peer)->tablet_metadata()->tablet_data_state();
  if (data_state != TABLET_DATA_READY) {
    return STATUS(IllegalState, "Tablet data state not TABLET_DATA_READY: " +
                                TabletDataState_Name(data_state),
                                tablet_id);
  }
  return Status::OK();
}

const NodeInstancePB& TSTabletManager::NodeInstance() const {
  return server_->instance_pb();
}

void TSTabletManager::GetTabletPeers(vector<scoped_refptr<TabletPeer> >* tablet_peers) const {
  boost::shared_lock<rw_spinlock> shared_lock(lock_);
  AppendValuesFromMap(tablet_map_, tablet_peers);
}

void TSTabletManager::ApplyChange(const string& tablet_id,
                                  shared_ptr<consensus::StateChangeContext> context) {
  WARN_NOT_OK(
      apply_pool_->SubmitFunc(
          std::bind(&TSTabletManager::MarkTabletDirty, this, tablet_id, context)),
      "Unable to run MarkDirty callback")
}

void TSTabletManager::MarkTabletDirty(const std::string& tablet_id,
                                      std::shared_ptr<consensus::StateChangeContext> context) {
  std::lock_guard<rw_spinlock> lock(lock_);
  MarkDirtyUnlocked(tablet_id, context);
}

int TSTabletManager::GetNumDirtyTabletsForTests() const {
  boost::shared_lock<rw_spinlock> lock(lock_);
  return dirty_tablets_.size();
}

int TSTabletManager::GetNumLiveTablets() const {
  int count = 0;
  boost::shared_lock<rw_spinlock> lock(lock_);
  for (const auto& entry : tablet_map_) {
    tablet::TabletStatePB state = entry.second->state();
    if (state == tablet::BOOTSTRAPPING ||
        state == tablet::RUNNING) {
      count++;
    }
  }
  return count;
}

void TSTabletManager::MarkDirtyUnlocked(const std::string& tablet_id,
                                        std::shared_ptr<consensus::StateChangeContext> context) {
  TabletReportState* state = FindOrNull(dirty_tablets_, tablet_id);
  if (state != nullptr) {
    CHECK_GE(next_report_seq_, state->change_seq);
    state->change_seq = next_report_seq_;
  } else {
    TabletReportState state;
    state.change_seq = next_report_seq_;
    InsertOrDie(&dirty_tablets_, tablet_id, state);
  }
  VLOG(2) << LogPrefix(tablet_id, fs_manager_->uuid())
          << "Marking dirty. Reason: " << context->ToString()
          << ". Will report this tablet to the Master in the next heartbeat "
          << "as part of report #" << next_report_seq_;
  server_->heartbeater()->TriggerASAP();
}

void TSTabletManager::InitLocalRaftPeerPB() {
  DCHECK_EQ(state(), MANAGER_INITIALIZING);
  local_peer_pb_.set_permanent_uuid(fs_manager_->uuid());
  auto addr = server_->first_rpc_address();
  HostPort hp;
  CHECK_OK(HostPortFromEndpointReplaceWildcard(addr, &hp));
  CHECK_OK(HostPortToPB(hp, local_peer_pb_.mutable_last_known_addr()));
}

void TSTabletManager::CreateReportedTabletPB(const string& tablet_id,
                                             const scoped_refptr<TabletPeer>& tablet_peer,
                                             ReportedTabletPB* reported_tablet) {
  reported_tablet->set_tablet_id(tablet_id);
  reported_tablet->set_state(tablet_peer->state());
  reported_tablet->set_tablet_data_state(tablet_peer->tablet_metadata()->tablet_data_state());
  if (tablet_peer->state() == tablet::FAILED) {
    AppStatusPB* error_status = reported_tablet->mutable_error();
    StatusToPB(tablet_peer->error(), error_status);
  }
  reported_tablet->set_schema_version(tablet_peer->tablet_metadata()->schema_version());

  // We cannot get consensus state information unless the TabletPeer is running.
  scoped_refptr<consensus::Consensus> consensus = tablet_peer->shared_consensus();
  if (consensus) {
    *reported_tablet->mutable_committed_consensus_state() =
        consensus->ConsensusState(consensus::CONSENSUS_CONFIG_COMMITTED);
  }
}

void TSTabletManager::GenerateIncrementalTabletReport(TabletReportPB* report) {
  boost::shared_lock<rw_spinlock> shared_lock(lock_);
  report->Clear();
  report->set_sequence_number(next_report_seq_++);
  report->set_is_incremental(true);
  for (const DirtyMap::value_type& dirty_entry : dirty_tablets_) {
    const string& tablet_id = dirty_entry.first;
    scoped_refptr<tablet::TabletPeer>* tablet_peer = FindOrNull(tablet_map_, tablet_id);
    if (tablet_peer) {
      // Dirty entry, report on it.
      CreateReportedTabletPB(tablet_id, *tablet_peer, report->add_updated_tablets());
    } else {
      // Removed.
      report->add_removed_tablet_ids(tablet_id);
    }
  }
}

void TSTabletManager::GenerateFullTabletReport(TabletReportPB* report) {
  boost::shared_lock<rw_spinlock> shared_lock(lock_);
  report->Clear();
  report->set_is_incremental(false);
  report->set_sequence_number(next_report_seq_++);
  for (const TabletMap::value_type& entry : tablet_map_) {
    CreateReportedTabletPB(entry.first, entry.second, report->add_updated_tablets());
  }
  dirty_tablets_.clear();
}

void TSTabletManager::MarkTabletReportAcknowledged(const TabletReportPB& report) {
  std::lock_guard<rw_spinlock> l(lock_);

  int32_t acked_seq = report.sequence_number();
  CHECK_LT(acked_seq, next_report_seq_);

  // Clear the "dirty" state for any tablets which have not changed since
  // this report.
  auto it = dirty_tablets_.begin();
  while (it != dirty_tablets_.end()) {
    const TabletReportState& state = it->second;
    if (state.change_seq <= acked_seq) {
      // This entry has not changed since this tablet report, we no longer need
      // to track it as dirty. If it becomes dirty again, it will be re-added
      // with a higher sequence number.
      it = dirty_tablets_.erase(it);
    } else {
      ++it;
    }
  }
}

Status TSTabletManager::HandleNonReadyTabletOnStartup(const scoped_refptr<TabletMetadata>& meta) {
  const string& tablet_id = meta->tablet_id();
  TabletDataState data_state = meta->tablet_data_state();
  CHECK(data_state == TABLET_DATA_DELETED ||
        data_state == TABLET_DATA_TOMBSTONED ||
        data_state == TABLET_DATA_COPYING)
      << "Unexpected TabletDataState in tablet " << tablet_id << ": "
      << TabletDataState_Name(data_state) << " (" << data_state << ")";

  if (data_state == TABLET_DATA_COPYING) {
    // We tombstone tablets that failed to remotely bootstrap.
    data_state = TABLET_DATA_TOMBSTONED;
  }

  const string kLogPrefix = LogPrefix(tablet_id, fs_manager_->uuid());

  // Roll forward deletions, as needed.
  LOG(INFO) << kLogPrefix << "Tablet Manager startup: Rolling forward tablet deletion "
            << "of type " << TabletDataState_Name(data_state);
  // Passing no OpId will retain the last_logged_opid that was previously in the metadata.
  RETURN_NOT_OK(DeleteTabletData(meta, data_state, fs_manager_->uuid(), boost::none));

  // We only delete the actual superblock of a TABLET_DATA_DELETED tablet on startup.
  // TODO: Consider doing this after a fixed delay, instead of waiting for a restart.
  // See KUDU-941.
  if (data_state == TABLET_DATA_DELETED) {
    LOG(INFO) << kLogPrefix << "Deleting tablet superblock";
    return meta->DeleteSuperBlock();
  }

  // Register TOMBSTONED tablets so that they get reported to the Master, which
  // allows us to permanently delete replica tombstones when a table gets
  // deleted.
  if (data_state == TABLET_DATA_TOMBSTONED) {
    CreateAndRegisterTabletPeer(meta, NEW_PEER);
  }

  return Status::OK();
}

void TSTabletManager::GetAndRegisterDataAndWalDir(FsManager* fs_manager,
                                                  const string& table_id,
                                                  const string& tablet_id,
                                                  const TableType table_type,
                                                  string* data_root_dir,
                                                  string* wal_root_dir) {
  // Skip sys catalog table and kudu table from modifying the map.
  if (table_id == master::kSysCatalogTableId || table_type == TableType::KUDU_COLUMNAR_TABLE_TYPE) {
    return;
  }
  MutexLock l(dir_assignment_lock_);
  LOG(INFO) << "Get and update data/wal directory assignment map for table: " << table_id;
  // Initialize the map if the directory mapping does not exist.
  auto data_root_dirs = fs_manager->GetDataRootDirs();
  CHECK(!data_root_dirs.empty()) << "No data root directories found";
  auto table_data_assignment_iter = table_data_assignment_map_.find(table_id);
  if (table_data_assignment_iter == table_data_assignment_map_.end()) {
    for (string data_root_iter : data_root_dirs) {
      unordered_set<string> tablet_id_set;
      table_data_assignment_map_[table_id][data_root_iter] = tablet_id_set;
    }
  }
  // Find the data directory with the least count of tablets for this table.
  table_data_assignment_iter = table_data_assignment_map_.find(table_id);
  auto data_assignment_value_map = table_data_assignment_iter->second;
  string min_dir;
  uint64_t min_dir_count = kuint64max;
  for (auto it = data_assignment_value_map.begin(); it != data_assignment_value_map.end(); ++it) {
    if (min_dir_count > it->second.size()) {
      min_dir = it->first;
      min_dir_count = it->second.size();
    }
  }
  *data_root_dir = min_dir;
  // Increment the count for min_dir.
  auto data_assignment_value_iter = table_data_assignment_map_[table_id].find(min_dir);
  data_assignment_value_iter->second.insert(tablet_id);

  // Find the wal directory with the least count of tablets for this table.
  min_dir = "";
  min_dir_count = kuint64max;
  auto wal_root_dirs = fs_manager->GetWalRootDirs();
  CHECK(!wal_root_dirs.empty()) << "No wal root directories found";
  auto table_wal_assignment_iter = table_wal_assignment_map_.find(table_id);
  if (table_wal_assignment_iter == table_wal_assignment_map_.end()) {
    for (string wal_root_iter : wal_root_dirs) {
      unordered_set<string> tablet_id_set;
      table_wal_assignment_map_[table_id][wal_root_iter] = tablet_id_set;
    }
  }
  table_wal_assignment_iter = table_wal_assignment_map_.find(table_id);
  auto wal_assignment_value_map = table_wal_assignment_iter->second;
  for (auto it = wal_assignment_value_map.begin(); it != wal_assignment_value_map.end(); ++it) {
    if (min_dir_count > it->second.size()) {
      min_dir = it->first;
      min_dir_count = it->second.size();
    }
  }
  *wal_root_dir = min_dir;
  auto wal_assignment_value_iter = table_wal_assignment_map_[table_id].find(min_dir);
  wal_assignment_value_iter->second.insert(tablet_id);
}

void TSTabletManager::RegisterDataAndWalDir(FsManager* fs_manager,
                                            const string& table_id,
                                            const string& tablet_id,
                                            const TableType table_type,
                                            const string& data_root_dir,
                                            const string& wal_root_dir) {
  // Skip sys catalog table and kudu table from modifying the map.
  if (table_id == master::kSysCatalogTableId || table_type == TableType::KUDU_COLUMNAR_TABLE_TYPE) {
    return;
  }
  MutexLock l(dir_assignment_lock_);
  LOG(INFO) << "Update data/wal directory assignment map for table: " << table_id;
  // Initialize the map if the directory mapping does not exist.
  auto data_root_dirs = fs_manager->GetDataRootDirs();
  CHECK(!data_root_dirs.empty()) << "No data root directories found";
  auto table_data_assignment_iter = table_data_assignment_map_.find(table_id);
  if (table_data_assignment_iter == table_data_assignment_map_.end()) {
    for (string data_root_iter : data_root_dirs) {
      unordered_set<string> tablet_id_set;
      table_data_assignment_map_[table_id][data_root_iter] = tablet_id_set;
    }
  }
  // Increment the count for data_root_dir.
  table_data_assignment_iter = table_data_assignment_map_.find(table_id);
  auto data_assignment_value_map = table_data_assignment_iter->second;
  auto data_assignment_value_iter = table_data_assignment_map_[table_id].find(data_root_dir);
  if (data_assignment_value_iter == table_data_assignment_map_[table_id].end()) {
    unordered_set<string> tablet_id_set;
    tablet_id_set.insert(tablet_id);
    table_data_assignment_map_[table_id][data_root_dir] = tablet_id_set;
  } else {
    data_assignment_value_iter->second.insert(tablet_id);
  }

  auto wal_root_dirs = fs_manager->GetWalRootDirs();
  CHECK(!wal_root_dirs.empty()) << "No wal root directories found";
  auto table_wal_assignment_iter = table_wal_assignment_map_.find(table_id);
  if (table_wal_assignment_iter == table_wal_assignment_map_.end()) {
    for (string wal_root_iter : wal_root_dirs) {
      unordered_set<string> tablet_id_set;
      table_wal_assignment_map_[table_id][wal_root_iter] = tablet_id_set;
    }
  }
  // Increment the count for wal_root_dir.
  table_wal_assignment_iter = table_wal_assignment_map_.find(table_id);
  auto wal_assignment_value_map = table_wal_assignment_iter->second;
  auto wal_assignment_value_iter = table_wal_assignment_map_[table_id].find(wal_root_dir);
  if (wal_assignment_value_iter == table_wal_assignment_map_[table_id].end()) {
    unordered_set<string> tablet_id_set;
    tablet_id_set.insert(tablet_id);
    table_wal_assignment_map_[table_id][wal_root_dir] = tablet_id_set;
  } else {
    wal_assignment_value_iter->second.insert(tablet_id);
  }
}

void TSTabletManager::UnregisterDataWalDir(const string& table_id,
                                           const string& tablet_id,
                                           const TableType table_type,
                                           const string& data_root_dir,
                                           const string& wal_root_dir) {
// Skip sys catalog table and kudu table from modifying the map.
  if (table_id == master::kSysCatalogTableId || table_type == TableType::KUDU_COLUMNAR_TABLE_TYPE) {
    return;
  }
  MutexLock l(dir_assignment_lock_);
  LOG(INFO) << "Unregister data/wal directory assignment map for table: " << table_id;
  auto table_data_assignment_iter = table_data_assignment_map_.find(table_id);
  DCHECK(table_data_assignment_iter != table_data_assignment_map_.end())
      << "Need to initialize table first";
  if (table_data_assignment_iter != table_data_assignment_map_.end()) {
    auto data_assignment_value_iter = table_data_assignment_map_[table_id].find(data_root_dir);
    DCHECK(data_assignment_value_iter != table_data_assignment_map_[table_id].end())
      << "No data directory index found for table: " << table_id;
    if (data_assignment_value_iter != table_data_assignment_map_[table_id].end()) {
      data_assignment_value_iter->second.erase(tablet_id);
    } else {
      LOG(WARNING) << "Tablet " << tablet_id << " not in the set for data directory "
                   << data_root_dir << "for table " << table_id;
    }
  }
  auto table_wal_assignment_iter = table_wal_assignment_map_.find(table_id);
  DCHECK(table_wal_assignment_iter != table_wal_assignment_map_.end())
      << "Need to initialize table first";
  if (table_wal_assignment_iter != table_wal_assignment_map_.end()) {
    auto wal_assignment_value_iter = table_wal_assignment_map_[table_id].find(wal_root_dir);
    DCHECK(wal_assignment_value_iter != table_wal_assignment_map_[table_id].end())
      << "No wal directory index found for table: " << table_id;
    if (wal_assignment_value_iter != table_wal_assignment_map_[table_id].end()) {
      wal_assignment_value_iter->second.erase(tablet_id);
    } else {
      LOG(WARNING) << "Tablet " << tablet_id << " not in the set for wal directory "
                   << wal_root_dir << "for table " << table_id;
    }
  }
}

Status DeleteTabletData(const scoped_refptr<TabletMetadata>& meta,
                        TabletDataState data_state,
                        const string& uuid,
                        const boost::optional<OpId>& last_logged_opid,
                        TSTabletManager* ts_manager) {
  const string& tablet_id = meta->tablet_id();
  const string kLogPrefix = LogPrefix(tablet_id, uuid);
  LOG(INFO) << kLogPrefix << "Deleting tablet data with delete state "
            << TabletDataState_Name(data_state);
  CHECK(data_state == TABLET_DATA_DELETED ||
        data_state == TABLET_DATA_TOMBSTONED)
      << "Unexpected data_state to delete tablet " << meta->tablet_id() << ": "
      << TabletDataState_Name(data_state) << " (" << data_state << ")";

  // Note: Passing an unset 'last_logged_opid' will retain the last_logged_opid
  // that was previously in the metadata.
  RETURN_NOT_OK(meta->DeleteTabletData(data_state, last_logged_opid));
  LOG(INFO) << kLogPrefix << "Tablet deleted. Last logged OpId: "
            << meta->tombstone_last_logged_opid();
  MAYBE_FAULT(FLAGS_fault_crash_after_blocks_deleted);

  RETURN_NOT_OK(Log::DeleteOnDiskData(meta->fs_manager(), meta->tablet_id(), meta->wal_dir()));
  MAYBE_FAULT(FLAGS_fault_crash_after_wal_deleted);

  // We do not delete the superblock or the consensus metadata when tombstoning
  // a tablet.
  if (data_state == TABLET_DATA_TOMBSTONED) {
    return Status::OK();
  }

  // Only TABLET_DATA_DELETED tablets get this far.
  RETURN_NOT_OK(ConsensusMetadata::DeleteOnDiskData(meta->fs_manager(), meta->tablet_id()));
  MAYBE_FAULT(FLAGS_fault_crash_after_cmeta_deleted);

  return Status::OK();
}

void LogAndTombstone(const scoped_refptr<TabletMetadata>& meta,
                     const std::string& msg,
                     const std::string& uuid,
                     const Status& s,
                     TSTabletManager* ts_manager) {
  const string& tablet_id = meta->tablet_id();
  const string kLogPrefix = LogPrefix(tablet_id, uuid);
  LOG(WARNING) << kLogPrefix << msg << ": " << s.ToString();

  // Tombstone the tablet when remote bootstrap fails.
  LOG(INFO) << kLogPrefix << "Tombstoning tablet after failed remote bootstrap";
  Status delete_status = DeleteTabletData(meta,
                                          TABLET_DATA_TOMBSTONED,
                                          uuid,
                                          boost::optional<OpId>(),
                                          ts_manager);

  if (PREDICT_FALSE(FLAGS_sleep_after_tombstoning_tablet_secs > 0)) {
    // We sleep here so that the test can verify that the state of the tablet is
    // TABLET_DATA_TOMBSTONED.
    LOG(INFO) << "Sleeping after remote bootstrap failed";
    SleepFor(MonoDelta::FromSeconds(FLAGS_sleep_after_tombstoning_tablet_secs));
  }

  if (PREDICT_FALSE(!delete_status.ok())) {
    // This failure should only either indicate a bug or an IO error.
    LOG(FATAL) << kLogPrefix << "Failed to tombstone tablet after remote bootstrap: "
               << delete_status.ToString();
  }

  // Remove the child tracker if present.
  if (ts_manager != nullptr) {
    shared_ptr<MemTracker> tracker;
    if (MemTracker::FindTracker(Substitute("tablet-$0", meta->tablet_id()), &tracker,
                                ts_manager->server()->mem_tracker())) {
      tracker->UnregisterFromParent();
    }
  }
}

TransitionInProgressDeleter::TransitionInProgressDeleter(
    TransitionInProgressMap* map, rw_spinlock* lock, string entry)
    : in_progress_(map), lock_(lock), entry_(std::move(entry)) {}

TransitionInProgressDeleter::~TransitionInProgressDeleter() {
  std::lock_guard<rw_spinlock> lock(*lock_);
  LOG(INFO) << "Deleting transition in progress " << in_progress_->at(entry_)
            << " for tablet " << entry_;
  CHECK(in_progress_->erase(entry_));
}

} // namespace tserver
} // namespace yb
