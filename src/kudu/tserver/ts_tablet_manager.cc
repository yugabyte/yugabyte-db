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

#include "kudu/tserver/ts_tablet_manager.h"

#include <algorithm>
#include <boost/optional.hpp>
#include <boost/thread/locks.hpp>
#include <boost/thread/mutex.hpp>
#include <boost/thread/shared_mutex.hpp>
#include <glog/logging.h>
#include <memory>
#include <string>
#include <vector>

#include "kudu/common/wire_protocol.h"
#include "kudu/consensus/consensus_meta.h"
#include "kudu/consensus/log.h"
#include "kudu/consensus/metadata.pb.h"
#include "kudu/consensus/opid_util.h"
#include "kudu/consensus/quorum_util.h"
#include "kudu/fs/fs_manager.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/gutil/strings/util.h"
#include "kudu/master/master.pb.h"
#include "kudu/tablet/metadata.pb.h"
#include "kudu/tablet/tablet.h"
#include "kudu/tablet/tablet.pb.h"
#include "kudu/tablet/tablet_bootstrap.h"
#include "kudu/tablet/tablet_metadata.h"
#include "kudu/tablet/tablet_peer.h"
#include "kudu/tserver/heartbeater.h"
#include "kudu/tserver/remote_bootstrap_client.h"
#include "kudu/tserver/tablet_server.h"
#include "kudu/util/debug/trace_event.h"
#include "kudu/util/env.h"
#include "kudu/util/env_util.h"
#include "kudu/util/fault_injection.h"
#include "kudu/util/flag_tags.h"
#include "kudu/util/metrics.h"
#include "kudu/util/pb_util.h"
#include "kudu/util/stopwatch.h"
#include "kudu/util/trace.h"

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

namespace kudu {
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
      continue;
    }
    metas.push_back(meta);
  }

  // Now submit the "Open" task for each.
  for (const scoped_refptr<TabletMetadata>& meta : metas) {
    scoped_refptr<TransitionInProgressDeleter> deleter;
    {
      boost::lock_guard<rw_spinlock> lock(lock_);
      CHECK_OK(StartTabletStateTransitionUnlocked(meta->tablet_id(), "opening tablet", &deleter));
    }

    scoped_refptr<TabletPeer> tablet_peer = CreateAndRegisterTabletPeer(meta, NEW_PEER);
    RETURN_NOT_OK(open_tablet_pool_->SubmitFunc(boost::bind(&TSTabletManager::OpenTablet,
                                                this, meta, deleter)));
  }

  {
    boost::lock_guard<rw_spinlock> lock(lock_);
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

Status TSTabletManager::CreateNewTablet(const string& table_id,
                                        const string& tablet_id,
                                        const Partition& partition,
                                        const string& table_name,
                                        const Schema& schema,
                                        const PartitionSchema& partition_schema,
                                        RaftConfigPB config,
                                        scoped_refptr<TabletPeer>* tablet_peer) {
  CHECK_EQ(state(), MANAGER_RUNNING);

  // If the consensus configuration is specified to use local consensus, verify that the peer
  // matches up with our local info.
  if (config.local()) {
    CHECK_EQ(1, config.peers_size());
    CHECK_EQ(server_->instance_pb().permanent_uuid(), config.peers(0).permanent_uuid());
  }

  // Set the initial opid_index for a RaftConfigPB to -1.
  config.set_opid_index(consensus::kInvalidOpIdIndex);

  scoped_refptr<TransitionInProgressDeleter> deleter;
  {
    // acquire the lock in exclusive mode as we'll add a entry to the
    // transition_in_progress_ set if the lookup fails.
    boost::lock_guard<rw_spinlock> lock(lock_);
    TRACE("Acquired tablet manager lock");

    // Sanity check that the tablet isn't already registered.
    scoped_refptr<TabletPeer> junk;
    if (LookupTabletUnlocked(tablet_id, &junk)) {
      return Status::AlreadyPresent("Tablet already registered", tablet_id);
    }

    // Sanity check that the tablet's creation isn't already in progress
    RETURN_NOT_OK(StartTabletStateTransitionUnlocked(tablet_id, "creating tablet", &deleter));
  }

  // Create the metadata.
  TRACE("Creating new metadata...");
  scoped_refptr<TabletMetadata> meta;
  RETURN_NOT_OK_PREPEND(
    TabletMetadata::CreateNew(fs_manager_,
                              tablet_id,
                              table_name,
                              schema,
                              partition_schema,
                              partition,
                              TABLET_DATA_READY,
                              &meta),
    "Couldn't create tablet metadata");

  // We must persist the consensus metadata to disk before starting a new
  // tablet's TabletPeer and Consensus implementation.
  gscoped_ptr<ConsensusMetadata> cmeta;
  RETURN_NOT_OK_PREPEND(ConsensusMetadata::Create(fs_manager_, tablet_id, fs_manager_->uuid(),
                                                  config, consensus::kMinimumTerm, &cmeta),
                        "Unable to create new ConsensusMeta for tablet " + tablet_id);
  scoped_refptr<TabletPeer> new_peer = CreateAndRegisterTabletPeer(meta, NEW_PEER);

  // We can run this synchronously since there is nothing to bootstrap.
  RETURN_NOT_OK(open_tablet_pool_->SubmitFunc(boost::bind(&TSTabletManager::OpenTablet,
                                              this, meta, deleter)));

  if (tablet_peer) {
    *tablet_peer = new_peer;
  }
  return Status::OK();
}

// If 'expr' fails, log a message, tombstone the given tablet, and return the
// error status.
#define TOMBSTONE_NOT_OK(expr, meta, msg) \
  do { \
    Status _s = (expr); \
    if (PREDICT_FALSE(!_s.ok())) { \
      LogAndTombstone((meta), (msg), _s); \
      return _s; \
    } \
  } while (0)

Status TSTabletManager::CheckLeaderTermNotLower(const string& tablet_id,
                                                int64_t leader_term,
                                                int64_t last_logged_term) {
  if (PREDICT_FALSE(leader_term < last_logged_term)) {
    Status s = Status::InvalidArgument(
        Substitute("Leader has replica of tablet $0 with term $1 "
                    "lower than last logged term $2 on local replica. Rejecting "
                    "remote bootstrap request",
                    tablet_id,
                    leader_term, last_logged_term));
    LOG(WARNING) << LogPrefix(tablet_id) << "Remote boostrap: " << s.ToString();
    return s;
  }
  return Status::OK();
}

Status TSTabletManager::StartRemoteBootstrap(const StartRemoteBootstrapRequestPB& req) {
  const string& tablet_id = req.tablet_id();
  const string& bootstrap_peer_uuid = req.bootstrap_peer_uuid();
  HostPort bootstrap_peer_addr;
  RETURN_NOT_OK(HostPortFromPB(req.bootstrap_peer_addr(), &bootstrap_peer_addr));
  int64_t leader_term = req.caller_term();

  const string kLogPrefix = LogPrefix(tablet_id);

  scoped_refptr<TabletPeer> old_tablet_peer;
  scoped_refptr<TabletMetadata> meta;
  bool replacing_tablet = false;
  scoped_refptr<TransitionInProgressDeleter> deleter;
  {
    boost::lock_guard<rw_spinlock> lock(lock_);
    if (LookupTabletUnlocked(tablet_id, &old_tablet_peer)) {
      meta = old_tablet_peer->tablet_metadata();
      replacing_tablet = true;
    }
    RETURN_NOT_OK(StartTabletStateTransitionUnlocked(tablet_id, "remote bootstrapping tablet",
                                                     &deleter));
  }

  if (replacing_tablet) {
    // Make sure the existing tablet peer is shut down and tombstoned.
    TabletDataState data_state = meta->tablet_data_state();
    switch (data_state) {
      case TABLET_DATA_COPYING:
        // This should not be possible due to the transition_in_progress_ "lock".
        LOG(FATAL) << LogPrefix(tablet_id) << " Remote bootstrap: "
                   << "Found tablet in TABLET_DATA_COPYING state during StartRemoteBootstrap()";
      case TABLET_DATA_TOMBSTONED: {
        int64_t last_logged_term = meta->tombstone_last_logged_opid().term();
        RETURN_NOT_OK(CheckLeaderTermNotLower(tablet_id, leader_term, last_logged_term));
        break;
      }
      case TABLET_DATA_READY: {
        Log* log = old_tablet_peer->log();
        if (!log) {
          return Status::IllegalState("Log unavailable. Tablet is not running", tablet_id);
        }
        OpId last_logged_opid;
        log->GetLatestEntryOpId(&last_logged_opid);
        int64_t last_logged_term = last_logged_opid.term();
        RETURN_NOT_OK(CheckLeaderTermNotLower(tablet_id, leader_term, last_logged_term));

        // Tombstone the tablet and store the last-logged OpId.
        old_tablet_peer->Shutdown();
        // TODO: Because we begin shutdown of the tablet after we check our
        // last-logged term against the leader's term, there may be operations
        // in flight and it may be possible for the same check in the remote
        // bootstrap client Start() method to fail. This will leave the replica in
        // a tombstoned state, and then the leader with the latest log entries
        // will simply remote boostrap this replica again. We could try to
        // check again after calling Shutdown(), and if the check fails, try to
        // reopen the tablet. For now, we live with the (unlikely) race.
        RETURN_NOT_OK_PREPEND(DeleteTabletData(meta, TABLET_DATA_TOMBSTONED, last_logged_opid),
                              Substitute("Unable to delete on-disk data from tablet $0",
                                         tablet_id));
        break;
      }
      default:
        return Status::IllegalState(
            Substitute("Found tablet in unsupported state for remote bootstrap. "
                        "Tablet: $0, tablet data state: $1",
                        tablet_id, TabletDataState_Name(data_state)));
    }
  }

  string init_msg = kLogPrefix + Substitute("Initiating remote bootstrap from Peer $0 ($1)",
                                            bootstrap_peer_uuid, bootstrap_peer_addr.ToString());
  LOG(INFO) << init_msg;
  TRACE(init_msg);

  gscoped_ptr<RemoteBootstrapClient> rb_client(
      new RemoteBootstrapClient(tablet_id, fs_manager_, server_->messenger(),
                                fs_manager_->uuid()));

  // Download and persist the remote superblock in TABLET_DATA_COPYING state.
  if (replacing_tablet) {
    RETURN_NOT_OK(rb_client->SetTabletToReplace(meta, leader_term));
  }
  RETURN_NOT_OK(rb_client->Start(bootstrap_peer_uuid, bootstrap_peer_addr, &meta));

  // From this point onward, the superblock is persisted in TABLET_DATA_COPYING
  // state, and we need to tombtone the tablet if additional steps prior to
  // getting to a TABLET_DATA_READY state fail.

  // Registering a non-initialized TabletPeer offers visibility through the Web UI.
  RegisterTabletPeerMode mode = replacing_tablet ? REPLACEMENT_PEER : NEW_PEER;
  scoped_refptr<TabletPeer> tablet_peer = CreateAndRegisterTabletPeer(meta, mode);
  string peer_str = bootstrap_peer_uuid + " (" + bootstrap_peer_addr.ToString() + ")";

  // Download all of the remote files.
  TOMBSTONE_NOT_OK(rb_client->FetchAll(tablet_peer->status_listener()), meta,
                   "Remote bootstrap: Unable to fetch data from remote peer " +
                   bootstrap_peer_uuid + " (" + bootstrap_peer_addr.ToString() + ")");

  MAYBE_FAULT(FLAGS_fault_crash_after_rb_files_fetched);

  // Write out the last files to make the new replica visible and update the
  // TabletDataState in the superblock to TABLET_DATA_READY.
  TOMBSTONE_NOT_OK(rb_client->Finish(), meta, "Remote bootstrap: Failure calling Finish()");

  // We run this asynchronously. We don't tombstone the tablet if this fails,
  // because if we were to fail to open the tablet, on next startup, it's in a
  // valid fully-copied state.
  RETURN_NOT_OK(open_tablet_pool_->SubmitFunc(boost::bind(&TSTabletManager::OpenTablet,
                                              this, meta, deleter)));
  return Status::OK();
}

// Create and register a new TabletPeer, given tablet metadata.
scoped_refptr<TabletPeer> TSTabletManager::CreateAndRegisterTabletPeer(
    const scoped_refptr<TabletMetadata>& meta, RegisterTabletPeerMode mode) {
  scoped_refptr<TabletPeer> tablet_peer(
      new TabletPeer(meta,
                     local_peer_pb_,
                     apply_pool_.get(),
                     Bind(&TSTabletManager::MarkTabletDirty, Unretained(this), meta->tablet_id())));
  RegisterTablet(meta->tablet_id(), tablet_peer, mode);
  return tablet_peer;
}

Status TSTabletManager::DeleteTablet(
    const string& tablet_id,
    TabletDataState delete_type,
    const boost::optional<int64_t>& cas_config_opid_index_less_or_equal,
    boost::optional<TabletServerErrorPB::Code>* error_code) {

  if (delete_type != TABLET_DATA_DELETED && delete_type != TABLET_DATA_TOMBSTONED) {
    return Status::InvalidArgument("DeleteTablet() requires an argument that is one of "
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
    boost::lock_guard<rw_spinlock> lock(lock_);
    TRACE("Acquired tablet manager lock");
    RETURN_NOT_OK(CheckRunningUnlocked(error_code));

    if (!LookupTabletUnlocked(tablet_id, &tablet_peer)) {
      *error_code = TabletServerErrorPB::TABLET_NOT_FOUND;
      return Status::NotFound("Tablet not found", tablet_id);
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
      return Status::IllegalState("Consensus not available. Tablet shutting down");
    }
    RaftConfigPB committed_config = consensus->CommittedConfig();
    if (committed_config.opid_index() > *cas_config_opid_index_less_or_equal) {
      *error_code = TabletServerErrorPB::CAS_FAILED;
      return Status::IllegalState(Substitute("Request specified cas_config_opid_index_less_or_equal"
                                             " of $0 but the committed config has opid_index of $1",
                                             *cas_config_opid_index_less_or_equal,
                                             committed_config.opid_index()));
    }
  }

  tablet_peer->Shutdown();

  boost::optional<OpId> opt_last_logged_opid;
  if (tablet_peer->log()) {
    OpId last_logged_opid;
    tablet_peer->log()->GetLatestEntryOpId(&last_logged_opid);
    opt_last_logged_opid = last_logged_opid;
  }

  Status s = DeleteTabletData(tablet_peer->tablet_metadata(), delete_type, opt_last_logged_opid);
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
    boost::lock_guard<rw_spinlock> lock(lock_);
    RETURN_NOT_OK(CheckRunningUnlocked(error_code));
    CHECK_EQ(1, tablet_map_.erase(tablet_id)) << tablet_id;
  }

  return Status::OK();
}

string TSTabletManager::LogPrefix(const string& tablet_id) const {
  return "T " + tablet_id + " P " + fs_manager_->uuid() + ": ";
}

Status TSTabletManager::CheckRunningUnlocked(
    boost::optional<TabletServerErrorPB::Code>* error_code) const {
  if (state_ == MANAGER_RUNNING) {
    return Status::OK();
  }
  *error_code = TabletServerErrorPB::TABLET_NOT_RUNNING;
  return Status::ServiceUnavailable(Substitute("Tablet Manager is not running: $0",
                                               TSTabletManagerStatePB_Name(state_)));
}

Status TSTabletManager::StartTabletStateTransitionUnlocked(
    const string& tablet_id,
    const string& reason,
    scoped_refptr<TransitionInProgressDeleter>* deleter) {
  DCHECK(lock_.is_write_locked());
  if (!InsertIfNotPresent(&transition_in_progress_, tablet_id, reason)) {
    return Status::IllegalState(
        Substitute("State transition of tablet $0 already in progress: $1",
                    tablet_id, transition_in_progress_[tablet_id]));
  }
  deleter->reset(new TransitionInProgressDeleter(&transition_in_progress_, &lock_, tablet_id));
  return Status::OK();
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

  LOG(INFO) << LogPrefix(tablet_id) << "Bootstrapping tablet";
  TRACE("Bootstrapping tablet");

  consensus::ConsensusBootstrapInfo bootstrap_info;
  Status s;
  LOG_TIMING_PREFIX(INFO, LogPrefix(tablet_id), "bootstrapping tablet") {
    // TODO: handle crash mid-creation of tablet? do we ever end up with a
    // partially created tablet here?
    tablet_peer->SetBootstrapping();
    s = BootstrapTablet(meta,
                        scoped_refptr<server::Clock>(server_->clock()),
                        server_->mem_tracker(),
                        metric_registry_,
                        tablet_peer->status_listener(),
                        &tablet,
                        &log,
                        tablet_peer->log_anchor_registry(),
                        &bootstrap_info);
    if (!s.ok()) {
      LOG(ERROR) << LogPrefix(tablet_id) << "Tablet failed to bootstrap: "
                 << s.ToString();
      tablet_peer->SetFailed(s);
      return;
    }
  }

  MonoTime start(MonoTime::Now(MonoTime::FINE));
  LOG_TIMING_PREFIX(INFO, LogPrefix(tablet_id), "starting tablet") {
    TRACE("Initializing tablet peer");
    s =  tablet_peer->Init(tablet,
                           scoped_refptr<server::Clock>(server_->clock()),
                           server_->messenger(),
                           log,
                           tablet->GetMetricEntity());

    if (!s.ok()) {
      LOG(ERROR) << LogPrefix(tablet_id) << "Tablet failed to init: "
                 << s.ToString();
      tablet_peer->SetFailed(s);
      return;
    }

    TRACE("Starting tablet peer");
    s = tablet_peer->Start(bootstrap_info);
    if (!s.ok()) {
      LOG(ERROR) << LogPrefix(tablet_id) << "Tablet failed to start: "
                 << s.ToString();
      tablet_peer->SetFailed(s);
      return;
    }

    tablet_peer->RegisterMaintenanceOps(server_->maintenance_manager());
  }

  int elapsed_ms = MonoTime::Now(MonoTime::FINE).GetDeltaSince(start).ToMilliseconds();
  if (elapsed_ms > FLAGS_tablet_start_warn_threshold_ms) {
    LOG(WARNING) << LogPrefix(tablet_id) << "Tablet startup took " << elapsed_ms << "ms";
    if (Trace::CurrentTrace()) {
      LOG(WARNING) << LogPrefix(tablet_id) << "Trace:" << std::endl
                   << Trace::CurrentTrace()->DumpToString(true);
    }
  }
}

void TSTabletManager::Shutdown() {
  {
    boost::lock_guard<rw_spinlock> lock(lock_);
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
    boost::lock_guard<rw_spinlock> l(lock_);
    // We don't expect anyone else to be modifying the map after we start the
    // shut down process.
    CHECK_EQ(tablet_map_.size(), peers_to_shutdown.size())
      << "Map contents changed during shutdown!";
    tablet_map_.clear();

    state_ = MANAGER_SHUTDOWN;
  }
}

void TSTabletManager::RegisterTablet(const std::string& tablet_id,
                                     const scoped_refptr<TabletPeer>& tablet_peer,
                                     RegisterTabletPeerMode mode) {
  boost::lock_guard<rw_spinlock> lock(lock_);
  // If we are replacing a tablet peer, we delete the existing one first.
  if (mode == REPLACEMENT_PEER && tablet_map_.erase(tablet_id) != 1) {
    LOG(FATAL) << "Unable to remove previous tablet peer " << tablet_id << ": not registered!";
  }
  if (!InsertIfNotPresent(&tablet_map_, tablet_id, tablet_peer)) {
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
    return Status::NotFound("Tablet not found", tablet_id);
  }
  TabletDataState data_state = (*tablet_peer)->tablet_metadata()->tablet_data_state();
  if (data_state != TABLET_DATA_READY) {
    return Status::IllegalState("Tablet data state not TABLET_DATA_READY: " +
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

void TSTabletManager::MarkTabletDirty(const std::string& tablet_id, const std::string& reason) {
  boost::lock_guard<rw_spinlock> lock(lock_);
  MarkDirtyUnlocked(tablet_id, reason);
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

void TSTabletManager::MarkDirtyUnlocked(const std::string& tablet_id, const std::string& reason) {
  TabletReportState* state = FindOrNull(dirty_tablets_, tablet_id);
  if (state != nullptr) {
    CHECK_GE(next_report_seq_, state->change_seq);
    state->change_seq = next_report_seq_;
  } else {
    TabletReportState state;
    state.change_seq = next_report_seq_;
    InsertOrDie(&dirty_tablets_, tablet_id, state);
  }
  VLOG(2) << LogPrefix(tablet_id) << "Marking dirty. Reason: " << reason
          << ". Will report this tablet to the Master in the next heartbeat "
          << "as part of report #" << next_report_seq_;
  server_->heartbeater()->TriggerASAP();
}

void TSTabletManager::InitLocalRaftPeerPB() {
  DCHECK_EQ(state(), MANAGER_INITIALIZING);
  local_peer_pb_.set_permanent_uuid(fs_manager_->uuid());
  Sockaddr addr = server_->first_rpc_address();
  HostPort hp;
  CHECK_OK(HostPortFromSockaddrReplaceWildcard(addr, &hp));
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
  boost::lock_guard<rw_spinlock> l(lock_);

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

  // Roll forward deletions, as needed.
  LOG(INFO) << LogPrefix(tablet_id) << "Tablet Manager startup: Rolling forward tablet deletion "
                                    << "of type " << TabletDataState_Name(data_state);
  // Passing no OpId will retain the last_logged_opid that was previously in the metadata.
  RETURN_NOT_OK(DeleteTabletData(meta, data_state, boost::none));

  // We only delete the actual superblock of a TABLET_DATA_DELETED tablet on startup.
  // TODO: Consider doing this after a fixed delay, instead of waiting for a restart.
  // See KUDU-941.
  if (data_state == TABLET_DATA_DELETED) {
    LOG(INFO) << LogPrefix(tablet_id) << "Deleting tablet superblock";
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

Status TSTabletManager::DeleteTabletData(const scoped_refptr<TabletMetadata>& meta,
                                         TabletDataState data_state,
                                         const boost::optional<OpId>& last_logged_opid) {
  const string& tablet_id = meta->tablet_id();
  LOG(INFO) << LogPrefix(tablet_id) << "Deleting tablet data with delete state "
            << TabletDataState_Name(data_state);
  CHECK(data_state == TABLET_DATA_DELETED ||
        data_state == TABLET_DATA_TOMBSTONED)
      << "Unexpected data_state to delete tablet " << meta->tablet_id() << ": "
      << TabletDataState_Name(data_state) << " (" << data_state << ")";

  // Note: Passing an unset 'last_logged_opid' will retain the last_logged_opid
  // that was previously in the metadata.
  RETURN_NOT_OK(meta->DeleteTabletData(data_state, last_logged_opid));
  LOG(INFO) << LogPrefix(tablet_id) << "Tablet deleted. Last logged OpId: "
            << meta->tombstone_last_logged_opid();
  MAYBE_FAULT(FLAGS_fault_crash_after_blocks_deleted);

  RETURN_NOT_OK(Log::DeleteOnDiskData(meta->fs_manager(), meta->tablet_id()));
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

void TSTabletManager::LogAndTombstone(const scoped_refptr<TabletMetadata>& meta,
                                      const std::string& msg,
                                      const Status& s) {
  const string& tablet_id = meta->tablet_id();
  const string kLogPrefix = "T " + tablet_id + " P " + fs_manager_->uuid() + ": ";
  LOG(WARNING) << kLogPrefix << msg << ": " << s.ToString();

  // Tombstone the tablet when remote bootstrap fails.
  LOG(INFO) << kLogPrefix << "Tombstoning tablet after failed remote bootstrap";
  Status delete_status = DeleteTabletData(meta, TABLET_DATA_TOMBSTONED, boost::optional<OpId>());
  if (PREDICT_FALSE(!delete_status.ok())) {
    // This failure should only either indicate a bug or an IO error.
    LOG(FATAL) << kLogPrefix << "Failed to tombstone tablet after remote bootstrap: "
               << delete_status.ToString();
  }
}

TransitionInProgressDeleter::TransitionInProgressDeleter(
    TransitionInProgressMap* map, rw_spinlock* lock, string entry)
    : in_progress_(map), lock_(lock), entry_(std::move(entry)) {}

TransitionInProgressDeleter::~TransitionInProgressDeleter() {
  boost::lock_guard<rw_spinlock> lock(*lock_);
  CHECK(in_progress_->erase(entry_));
}

} // namespace tserver
} // namespace kudu
