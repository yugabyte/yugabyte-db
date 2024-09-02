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

#include "yb/tserver/ts_tablet_manager.h"

#include <algorithm>
#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <boost/container/static_vector.hpp>
#include <boost/none.hpp>
#include <boost/optional/optional.hpp>

#include "yb/ash/wait_state.h"

#include "yb/cdc/cdc_service.h"

#include "yb/client/client.h"
#include "yb/client/meta_data_cache.h"
#include "yb/client/transaction_manager.h"

#include "yb/common/common.pb.h"
#include "yb/common/constants.h"
#include "yb/common/snapshot.h"
#include "yb/common/wire_protocol.h"

#include "yb/consensus/consensus.h"
#include "yb/consensus/consensus_meta.h"
#include "yb/consensus/consensus_util.h"
#include "yb/consensus/log.h"
#include "yb/consensus/log_anchor_registry.h"
#include "yb/consensus/metadata.pb.h"
#include "yb/consensus/multi_raft_batcher.h"
#include "yb/consensus/opid_util.h"
#include "yb/consensus/quorum_util.h"
#include "yb/consensus/raft_consensus.h"
#include "yb/consensus/retryable_requests.h"
#include "yb/consensus/state_change_context.h"

#include "yb/docdb/docdb_rocksdb_util.h"

#include "yb/fs/fs_manager.h"

#include "yb/gutil/bind.h"
#include "yb/gutil/callback.h"
#include "yb/gutil/stl_util.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/gutil/sysinfo.h"

#include "yb/master/master_heartbeat.pb.h"
#include "yb/master/sys_catalog.h"

#include "yb/qlexpr/index.h"

#include "yb/rocksdb/util/task_metrics.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/poller.h"

#include "yb/tablet/metadata.pb.h"
#include "yb/tablet/operations/clone_operation.h"
#include "yb/tablet/operations/split_operation.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet.pb.h"
#include "yb/tablet/tablet_bootstrap_if.h"
#include "yb/tablet/tablet_fwd.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_options.h"
#include "yb/tablet/tablet_peer.h"
#include "yb/tablet/tablet_snapshots.h"

#include "yb/tablet/tablet_types.pb.h"
#include "yb/tools/yb-admin_util.h"

#include "yb/tserver/full_compaction_manager.h"
#include "yb/tserver/heartbeater.h"
#include "yb/tserver/remote_bootstrap_client.h"
#include "yb/tserver/remote_bootstrap_session.h"
#include "yb/tserver/remote_snapshot_transfer_client.h"
#include "yb/tserver/tablet_limits.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/tablet_validator.h"
#include "yb/tserver/tserver.pb.h"

#include "yb/tserver/tserver_xcluster_context_if.h"
#include "yb/util/debug/long_operation_tracker.h"
#include "yb/util/debug/trace_event.h"
#include "yb/util/debug-util.h"
#include "yb/util/env.h"
#include "yb/util/fault_injection.h"
#include "yb/util/file_util.h"
#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/mem_tracker.h"
#include "yb/util/metrics.h"
#include "yb/util/monotime.h"
#include "yb/util/pb_util.h"
#include "yb/util/scope_exit.h"
#include "yb/util/shared_lock.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/stopwatch.h"
#include "yb/util/trace.h"

using namespace std::literals;
using namespace std::placeholders;

DEFINE_NON_RUNTIME_int32(num_tablets_to_open_simultaneously, 0,
             "Number of threads available to open tablets during startup. If this "
             "is set to 0 (the default), then the number of bootstrap threads will "
             "be set based on the number of data directories. If the data directories "
             "are on some very fast storage device such as SSD or a RAID array, it "
             "may make sense to manually tune this.");
TAG_FLAG(num_tablets_to_open_simultaneously, advanced);

DEFINE_NON_RUNTIME_int32(num_open_tablets_metadata_simultaneously, 0,
             "Number of threads available to open tablets' metadata during startup. If this "
             "is set to 0 (the default), then the number of open metadata threads will "
             "be set based on the number of CPUs");
TAG_FLAG(num_open_tablets_metadata_simultaneously, advanced);

DEFINE_UNKNOWN_int32(tablet_start_warn_threshold_ms, 500,
             "If a tablet takes more than this number of millis to start, issue "
             "a warning with a trace.");
TAG_FLAG(tablet_start_warn_threshold_ms, hidden);

DEFINE_NON_RUNTIME_int32(cleanup_split_tablets_interval_sec, 60,
             "Interval at which tablet manager tries to cleanup split tablets which are no longer "
             "needed. Setting this to 0 disables cleanup of split tablets.");

DEFINE_test_flag(double, fault_crash_after_blocks_deleted, 0.0,
                 "Fraction of the time when the tablet will crash immediately "
                 "after deleting the data blocks during tablet deletion.");

DEFINE_test_flag(double, fault_crash_after_wal_deleted, 0.0,
                 "Fraction of the time when the tablet will crash immediately "
                 "after deleting the WAL segments during tablet deletion.");

DEFINE_test_flag(double, fault_crash_after_cmeta_deleted, 0.0,
                 "Fraction of the time when the tablet will crash immediately "
                 "after deleting the consensus metadata during tablet deletion.");

DEFINE_test_flag(double, fault_crash_after_rb_files_fetched, 0.0,
                 "Fraction of the time when the tablet will crash immediately "
                 "after fetching the files during a remote bootstrap but before "
                 "marking the superblock as TABLET_DATA_READY.");

DEFINE_test_flag(double, fault_crash_in_split_after_log_copied, 0.0,
                 "Fraction of the time when the tablet will crash immediately after initiating a "
                 "Log::CopyTo from parent to child tablet, but before marking the child tablet as "
                 "TABLET_DATA_READY.");

DEFINE_test_flag(bool, simulate_already_present_in_remote_bootstrap, false,
                 "If true, return an AlreadyPresent error in remote bootstrap after starting the "
                 "remote bootstrap client.");

DEFINE_test_flag(bool, pause_before_remote_bootstrap, false,
                 "If true, pause after copying the superblock but before "
                 "RemoteBootstrapClient::Start.");

DEFINE_test_flag(bool, pause_after_set_bootstrapping, false,
                 "If true, pause after changing a tablet's state to BOOTSTRAPPING.");

DEFINE_test_flag(double, fault_crash_in_split_before_log_flushed, 0.0,
                 "Fraction of the time when the tablet will crash immediately before flushing a "
                 "parent tablet's kSplit operation.");

DEFINE_test_flag(uint64, crash_if_remote_bootstrap_sessions_greater_than, 0,
                 "If greater than zero, this process will crash if we detect more than the "
                 "specified number of remote bootstrap sessions.");

DEFINE_test_flag(uint64, crash_if_remote_bootstrap_sessions_per_table_greater_than, 0,
                 "If greater than zero, this process will crash if for any table we exceed the "
                 "specified number of remote bootstrap sessions");

DEFINE_test_flag(bool, crash_after_tablet_split_completed, false,
                 "Crash inside TSTabletManager::ApplyTabletSplit after tablet "
                 "being marked as TABLET_DATA_SPLIT_COMPLETED.");

DEFINE_test_flag(bool, crash_before_apply_tablet_split_op, false,
                 "Crash inside TSTabletManager::ApplyTabletSplit before doing anything.");

DEFINE_test_flag(bool, crash_before_source_tablet_mark_split_done, false,
                 "Crash inside TSTabletManager::ApplyTabletSplit before tablet "
                 "being marked TABLET_DATA_SPLIT_COMPLETED.");

DEFINE_test_flag(bool, force_single_tablet_failure, false,
                 "Force exactly one tablet to a failed state.");

DEFINE_test_flag(bool, fail_apply_clone_op, false,
                 "Whether to fail the RAFT apply of the clone op.");

DEFINE_test_flag(bool, expect_clone_apply_failure, false,
                 "Whether to simply log (instead of DFATAL) if the clone RAFT apply fails.");

DEFINE_test_flag(int32, apply_tablet_split_inject_delay_ms, 0,
                 "Inject delay into TSTabletManager::ApplyTabletSplit.");

DEFINE_test_flag(bool, pause_apply_tablet_split, false,
                 "Pause TSTabletManager::ApplyTabletSplit.");

DEFINE_test_flag(bool, skip_deleting_split_tablets, false,
                 "Skip deleting tablets which have been split.");

DEFINE_UNKNOWN_int32(verify_tablet_data_interval_sec, 0,
             "The tick interval time for the tablet data integrity verification background task. "
             "This defaults to 0, which means disable the background task.");

DEFINE_UNKNOWN_int32(cleanup_metrics_interval_sec, 60,
             "The tick interval time for the metrics cleanup background task. "
             "If set to 0, it disables the background task.");

DEFINE_UNKNOWN_int32(send_wait_for_report_interval_ms, 60000,
             "The tick interval time to trigger updating all transaction coordinators with wait-for"
             " relationships.");

DEFINE_UNKNOWN_bool(skip_tablet_data_verification, false,
            "Skip checking tablet data for corruption.");

DEFINE_UNKNOWN_int32(read_pool_max_threads, 128,
             "The maximum number of threads allowed for read_pool_. This pool is used "
             "to run multiple read operations, that are part of the same tablet rpc, "
             "in parallel.");

DEFINE_UNKNOWN_int32(read_pool_max_queue_size, 128,
             "The maximum number of tasks that can be held in the queue for read_pool_. This pool "
             "is used to run multiple read operations, that are part of the same tablet rpc, "
             "in parallel.");

DEPRECATE_FLAG(int32, post_split_trigger_compaction_pool_max_threads, "02_2024");
DEPRECATE_FLAG(int32, post_split_trigger_compaction_pool_max_queue_size, "02_2024");

DEFINE_NON_RUNTIME_int32(full_compaction_pool_max_threads, 1,
             "The maximum number of threads allowed for full_compaction_pool_. This "
             "pool is used to run full compactions on tablets, either on a shceduled basis "
              "or after they have been split and still contain irrelevant data from the tablet "
              "they were sourced from.");

DEFINE_NON_RUNTIME_int32(full_compaction_pool_max_queue_size, 500,
             "The maximum number of tasks that can be held in the pool for "
             "full_compaction_pool_. This pool is used to run full compactions on tablets "
             "on a scheduled basis or after they have been split and still contain irrelevant data "
             "from the tablet they were sourced from.");

DEPRECATE_FLAG(int32, scheduled_full_compaction_check_interval_min, "02_2024");

DEFINE_test_flag(int32, sleep_after_tombstoning_tablet_secs, 0,
                 "Whether we sleep in LogAndTombstone after calling DeleteTabletData.");

DEFINE_UNKNOWN_bool(enable_restart_transaction_status_tablets_first, true,
            "Set to true to prioritize bootstrapping transaction status tablets first.");

DEFINE_RUNTIME_int32(bg_superblock_flush_interval_secs, 60,
    "The interval at which tablet superblocks are flushed to disk (if dirty) by a background "
    "thread. Applicable only when lazily_flush_superblock is enabled. 0 indicates that the "
    "background task is fully disabled.");

DEFINE_RUNTIME_bool(enable_copy_retryable_requests_from_parent, true,
                    "Whether to copy retryable requests from parent tablet when opening"
                    "the child tablet");

DEFINE_UNKNOWN_int32(flush_bootstrap_state_pool_max_threads, -1,
                     "The maximum number of threads used to flush retryable requests");

DEFINE_test_flag(bool, disable_flush_on_shutdown, false,
                 "Whether to disable flushing memtable on shutdown.");

DEFINE_test_flag(bool, pause_delete_tablet, false,
                 "Make DeletTablet stuck.");

DEFINE_test_flag(bool, crash_before_clone_target_marked_ready, false,
                 "Whether to crash before marking the target tablet of a clone op as "
                 "TABLET_DATA_READY.");

DEFINE_test_flag(bool, crash_before_mark_clone_attempted, false,
                 "Whether to crash before marking a clone op as completed on the source tablet.");

DECLARE_bool(enable_wait_queues);
DECLARE_bool(disable_deadlock_detection);
DECLARE_bool(lazily_flush_superblock);

DECLARE_int32(retryable_request_timeout_secs);

DECLARE_string(rocksdb_compact_flush_rate_limit_sharing_mode);

namespace yb::tserver {

METRIC_DEFINE_event_stats(server, op_apply_queue_length, "Operation Apply Queue Length",
                        MetricUnit::kTasks,
                        "Number of operations waiting to be applied to the tablet. "
                        "High queue lengths indicate that the server is unable to process "
                        "operations as fast as they are being written to the WAL.");

METRIC_DEFINE_event_stats(server, op_apply_queue_time, "Operation Apply Queue Time",
                        MetricUnit::kMicroseconds,
                        "Time that operations spent waiting in the apply queue before being "
                        "processed. High queue times indicate that the server is unable to "
                        "process operations as fast as they are being written to the WAL.");

METRIC_DEFINE_event_stats(server, op_apply_run_time, "Operation Apply Run Time",
                        MetricUnit::kMicroseconds,
                        "Time that operations spent being applied to the tablet. "
                        "High values may indicate that the server is under-provisioned or "
                        "that operations consist of very large batches.");

METRIC_DEFINE_event_stats(server, op_read_queue_length, "Operation Read op Queue Length",
                        MetricUnit::kTasks,
                        "Number of operations waiting to be applied to the tablet. "
                            "High queue lengths indicate that the server is unable to process "
                            "operations as fast as they are being written to the WAL.");

METRIC_DEFINE_event_stats(server, op_read_queue_time, "Operation Read op Queue Time",
                        MetricUnit::kMicroseconds,
                        "Time that operations spent waiting in the read queue before being "
                            "processed. High queue times indicate that the server is unable to "
                            "process operations as fast as they are being written to the WAL.");

METRIC_DEFINE_event_stats(server, op_read_run_time, "Operation Read op Run Time",
                        MetricUnit::kMicroseconds,
                        "Time that operations spent being applied to the tablet. "
                            "High values may indicate that the server is under-provisioned or "
                            "that operations consist of very large batches.");

METRIC_DEFINE_event_stats(server, ts_bootstrap_time, "TServer Bootstrap Time",
                        MetricUnit::kMicroseconds,
                        "Time that the tablet server takes to bootstrap all of its tablets.");

METRIC_DEFINE_gauge_uint64(server, ts_open_metadata_time_us, "TServer Open Meta Time",
                        MetricUnit::kMicroseconds,
                        "Time that the tablet server takes to open all of its tablets' metadata.");

METRIC_DEFINE_gauge_uint64(server, ts_split_op_apply, "Split Apply",
                        MetricUnit::kOperations,
                        "Number of split operations successfully applied in Raft.");

METRIC_DEFINE_gauge_uint64(server, ts_post_split_compaction_added,
                        "Post-Split Compaction Submitted",
                        MetricUnit::kRequests,
                        "Number of post-split compaction requests submitted.");

METRIC_DEFINE_gauge_uint32(server, ts_live_tablet_peers,
    "Number of Live Tablet Peers", MetricUnit::kUnits,
    "Number of live tablet peers running on this TServer. Tablet peers are live if they are "
    "bootstrapping or running.");

METRIC_DEFINE_gauge_int64(server, ts_supportable_tablet_peers,
    "Number of Tablet Peers this TServer can support", MetricUnit::kUnits,
    "Number of tablet peers that this TServer can support based on available RAM and cores or -1 "
    "if no tablet limit in effect.");

THREAD_POOL_METRICS_DEFINE(server, admin_triggered_compaction_pool,
    "Thread pool for admin-triggered tablet compaction jobs.");

THREAD_POOL_METRICS_DEFINE(server, full_compaction_pool,
    "Thread pool for tserver-triggered full compaction jobs.");

THREAD_POOL_METRICS_DEFINE(
    server, waiting_txn_pool,
    "Thread pool for wait queue to resume waiting transactions and also for forwarding wait-for "
        "edges to the transaction coordinator/deadlock detector.");

ROCKSDB_PRIORITY_THREAD_POOL_METRICS_DEFINE(server);

using consensus::ConsensusMetadata;
using consensus::RaftConfigPB;
using consensus::StartRemoteBootstrapRequestPB;
using log::Log;
using master::ReportedTabletPB;
using master::TabletReportPB;
using master::TabletReportUpdatesPB;
using std::shared_ptr;
using std::string;
using std::unordered_set;
using std::vector;
using std::min;
using std::deque;
using strings::Substitute;
using tablet::BOOTSTRAPPING;
using tablet::NOT_STARTED;
using tablet::RaftGroupMetadata;
using tablet::RaftGroupMetadataPtr;
using tablet::RaftGroupStatePB;
using tablet::RUNNING;
using tablet::TABLET_DATA_COPYING;
using tablet::TABLET_DATA_DELETED;
using tablet::TABLET_DATA_INIT_STARTED;
using tablet::TABLET_DATA_READY;
using tablet::TABLET_DATA_SPLIT_COMPLETED;
using tablet::TABLET_DATA_TOMBSTONED;
using tablet::TabletDataState;
using tablet::TabletPeer;
using tablet::TabletPeerPtr;
using tablet::TabletPeerWeakPtr;
using yb::MonoDelta;
using yb::MonoTime;

constexpr int32_t kDefaultTserverBlockCacheSizePercentage = 50;
const std::string kDebugBootstrapString = "RemoteBootstrap";
const std::string kDebugSnapshotTransferString = "RemoteSnapshotTransfer";

// Jenkins builds complain if we use tools::SnapshotIdToString with `undefined reference`.
namespace {

std::string SnapshotIdToString(const std::string& snapshot_id) {
  auto uuid = TryFullyDecodeTxnSnapshotId(snapshot_id);
  return uuid.IsNil() ? snapshot_id : uuid.ToString();
}

} // namespace

void TSTabletManager::VerifyTabletData() {
  LOG_WITH_PREFIX(INFO) << "Beginning tablet data verification checks";
  for (const TabletPeerPtr& peer : GetTabletPeers()) {
    if (peer->state() == RUNNING) {
      if (PREDICT_FALSE(FLAGS_skip_tablet_data_verification)) {
        LOG_WITH_PREFIX(INFO)
            << Format("Skipped tablet data verification check on $0", peer->tablet_id());
      } else {
        auto tablet_result = peer->shared_tablet_safe();
        Status s;
        if (tablet_result.ok()) {
          s = (*tablet_result)->VerifyDataIntegrity();
        } else {
          s = tablet_result.status();
        }
        if (!s.ok()) {
          LOG(WARNING) << "Tablet data integrity verification failed on " << peer->tablet_id()
                       << ": " << s;
        }
      }
    }
  }
}

void TSTabletManager::EmitMetrics() {
  ts_live_tablet_peers_metric_->set_value(GetNumLiveTablets());
  ts_supportable_tablet_peers_metric_->set_value(GetNumSupportableTabletPeers());
}

void TSTabletManager::CleanupOldMetrics() {
  VLOG(2) << "Cleaning up old metrics";
  metric_registry_->RetireOldMetrics();
}

void TSTabletManager::PollWaitingTxnRegistry() {
  DCHECK_NOTNULL(waiting_txn_registry_)->SendWaitForGraph();
}

TSTabletManager::TSTabletManager(FsManager* fs_manager,
                                 TabletServer* server,
                                 MetricRegistry* metric_registry)
  : fs_manager_(fs_manager),
    server_(server),
    metric_registry_(metric_registry),
    state_(MANAGER_INITIALIZING) {
  ThreadPoolMetrics metrics = {
      METRIC_op_apply_queue_length.Instantiate(server_->metric_entity()),
      METRIC_op_apply_queue_time.Instantiate(server_->metric_entity()),
      METRIC_op_apply_run_time.Instantiate(server_->metric_entity())
  };
  CHECK_OK(ThreadPoolBuilder("apply")
               .set_metrics(std::move(metrics))
               .Build(&apply_pool_));

  // This pool is shared by all replicas hosted by this server.
  //
  // Some submitted tasks use blocking IO, so we configure no upper bound on
  // the maximum number of threads in each pool (otherwise the default value of
  // "number of CPUs" may cause blocking tasks to starve other "fast" tasks).
  // However, the effective upper bound is the number of replicas as each will
  // submit its own tasks via a dedicated token.
  CHECK_OK(ThreadPoolBuilder("consensus")
               .set_min_threads(1)
               .unlimited_threads()
               .Build(&raft_pool_));

  raft_notifications_pool_ = std::make_unique<rpc::ThreadPool>(rpc::ThreadPoolOptions {
    .name = "raft_notifications",
    .max_workers = rpc::ThreadPoolOptions::kUnlimitedWorkers
  });

  CHECK_OK(ThreadPoolBuilder("log-sync")
               .set_min_threads(1)
               .unlimited_threads()
               .Build(&log_sync_pool_));
  auto num_flush_threads = FLAGS_flush_bootstrap_state_pool_max_threads;
  if (num_flush_threads < 0) {
    num_flush_threads = base::NumCPUs();
    if (num_flush_threads < 2) {
      num_flush_threads = 2;
    }
  }
  CHECK_OK(ThreadPoolBuilder("flush-retryable-requests")
               .set_min_threads(1)
               .set_max_threads(num_flush_threads)
               .Build(&flush_bootstrap_state_pool_));
  CHECK_OK(ThreadPoolBuilder("prepare")
               .set_min_threads(1)
               .unlimited_threads()
               .Build(&tablet_prepare_pool_));
  CHECK_OK(ThreadPoolBuilder("append")
               .set_min_threads(1)
               .unlimited_threads()
               .set_idle_timeout(MonoDelta::FromMilliseconds(10000))
               .Build(&append_pool_));
  CHECK_OK(ThreadPoolBuilder("log-alloc")
               .set_min_threads(1)
               .unlimited_threads()
               .Build(&allocation_pool_));
  ThreadPoolMetrics read_metrics = {
      METRIC_op_read_queue_length.Instantiate(server_->metric_entity()),
      METRIC_op_read_queue_time.Instantiate(server_->metric_entity()),
      METRIC_op_read_run_time.Instantiate(server_->metric_entity())
  };
  CHECK_OK(ThreadPoolBuilder("read-parallel")
               .set_max_threads(FLAGS_read_pool_max_threads)
               .set_max_queue_size(FLAGS_read_pool_max_queue_size)
               .set_metrics(std::move(read_metrics))
               .Build(&read_pool_));
  CHECK_OK(ThreadPoolBuilder("admin-compaction")
               .set_max_threads(std::max(docdb::GetGlobalRocksDBPriorityThreadPoolSize(), 0))
               .set_metrics(THREAD_POOL_METRICS_INSTANCE(
                   server_->metric_entity(), admin_triggered_compaction_pool))
               .Build(&admin_triggered_compaction_pool_));
  CHECK_OK(ThreadPoolBuilder("full-compaction")
              .set_max_threads(FLAGS_full_compaction_pool_max_threads)
              .set_max_queue_size(FLAGS_full_compaction_pool_max_queue_size)
              .set_metrics(THREAD_POOL_METRICS_INSTANCE(
                  server_->metric_entity(), full_compaction_pool))
              .Build(&full_compaction_pool_));
  CHECK_OK(ThreadPoolBuilder("wait-queue")
              .set_min_threads(1)
              .unlimited_threads()
              .set_metrics(THREAD_POOL_METRICS_INSTANCE(
                  server_->metric_entity(), waiting_txn_pool))
              .Build(&waiting_txn_pool_));
  ts_split_op_apply_ = METRIC_ts_split_op_apply.Instantiate(server_->metric_entity(), 0);
  ts_post_split_compaction_added_ =
      METRIC_ts_post_split_compaction_added.Instantiate(server_->metric_entity(), 0);
  ts_live_tablet_peers_metric_ =
      METRIC_ts_live_tablet_peers.Instantiate(server_->metric_entity(), 0);
  ts_supportable_tablet_peers_metric_ = METRIC_ts_supportable_tablet_peers.Instantiate(
      server_->metric_entity(), GetNumSupportableTabletPeers());
  ts_open_metadata_time_us_ =
      METRIC_ts_open_metadata_time_us.Instantiate(server_->metric_entity(), 0);

  mem_manager_ = std::make_shared<TabletMemoryManager>(
      &tablet_options_,
      server_->mem_tracker(),
      kDefaultTserverBlockCacheSizePercentage,
      server_->metric_entity(),
      [this](){ return GetTabletPeers(); });

  full_compaction_manager_ = std::make_unique<FullCompactionManager>(this);

  tablet_options_.priority_thread_pool_metrics =
      std::make_shared<rocksdb::RocksDBPriorityThreadPoolMetrics>(
          ROCKSDB_PRIORITY_THREAD_POOL_METRICS_INSTANCE(server_->metric_entity()));
}

TSTabletManager::~TSTabletManager() {
}

Status TSTabletManager::Init() {
  CHECK_EQ(state(), MANAGER_INITIALIZING);

  tablet_options_.env = server_->GetEnv();
  tablet_options_.rocksdb_env = server_->GetRocksDBEnv();
  tablet_options_.listeners = server_->options().listeners;
  if (docdb::GetRocksDBRateLimiterSharingMode() == docdb::RateLimiterSharingMode::TSERVER) {
    tablet_options_.rate_limiter = docdb::CreateRocksDBRateLimiter();
  }

  // Start the threadpool we'll use to open tablets.
  // This has to be done in Init() instead of the constructor, since the
  // FsManager isn't initialized until this point.
  int max_bootstrap_threads = FLAGS_num_tablets_to_open_simultaneously;
  int num_cpus = base::NumCPUs();
  if (max_bootstrap_threads == 0) {
    if (num_cpus <= 2) {
      max_bootstrap_threads = 2;
    } else {
      max_bootstrap_threads = min(
          num_cpus - 1, narrow_cast<int>(fs_manager_->GetDataRootDirs().size()) * 8);
    }
    LOG_WITH_PREFIX(INFO) <<  "max_bootstrap_threads=" << max_bootstrap_threads;
  }
  ThreadPoolMetrics bootstrap_metrics = {
          nullptr,
          nullptr,
          METRIC_ts_bootstrap_time.Instantiate(server_->metric_entity())
  };
  RETURN_NOT_OK(ThreadPoolBuilder("tablet-bootstrap")
                .set_max_threads(max_bootstrap_threads)
                .set_metrics(std::move(bootstrap_metrics))
                .Build(&open_tablet_pool_));

  CleanupCheckpoints();

  // Search for tablets in the metadata dir.
  vector<string> tablet_ids = VERIFY_RESULT(fs_manager_->ListTabletIds());

  InitLocalRaftPeerPB();

  multi_raft_manager_ = std::make_unique<consensus::MultiRaftManager>(server_->messenger(),
                                                                      &server_->proxy_cache(),
                                                                      local_peer_pb_.cloud_info());

  if (FLAGS_enable_wait_queues && !PREDICT_FALSE(FLAGS_disable_deadlock_detection)) {
    waiting_txn_registry_ = std::make_unique<docdb::LocalWaitingTxnRegistry>(
        client_future(), scoped_refptr<server::Clock>(server_->clock()), fs_manager_->uuid(),
        waiting_txn_pool());
  }

  struct Metas {
    std::mutex ready_metas_mutex;
    std::mutex non_ready_metas_mutex;
    std::deque<RaftGroupMetadataPtr> ready_metas GUARDED_BY(ready_metas_mutex);
    std::vector<RaftGroupMetadataPtr> non_ready_metas GUARDED_BY(non_ready_metas_mutex);
  };

  Metas metas;
  // Since at this time in the server, we shouldn't be doing any work. Thus, we can
  // use num_cpus - 1 threads to minimize time to open all metadatas.
  int max_open_meta_threads = FLAGS_num_open_tablets_metadata_simultaneously;
  if (max_open_meta_threads == 0) {
    max_open_meta_threads = num_cpus - 1;
  }
  TaskRunner open_metadata_runner;
  RETURN_NOT_OK(open_metadata_runner.Init(max_open_meta_threads));

  // First, load all of the tablet metadata. We do this before we start
  // submitting the actual OpenTablet() tasks so that we don't have to compete
  // for disk resources, etc, with bootstrap processes and running tablets.
  MonoTime start(MonoTime::Now());
  for (const string& tablet_id : tablet_ids) {
    RETURN_NOT_OK(open_metadata_runner.status());
    open_metadata_runner.Submit([this, tablet_id, &metas]() -> Status {
      RaftGroupMetadataPtr meta;
      RETURN_NOT_OK_PREPEND(OpenTabletMeta(tablet_id, &meta),
                            "Failed to open tablet metadata for tablet: " + tablet_id);
      if (PREDICT_FALSE(!CanServeTabletData(meta->tablet_data_state()))) {
        std::lock_guard lock(metas.non_ready_metas_mutex);
        metas.non_ready_metas.push_back(meta);
      } else if (FLAGS_enable_restart_transaction_status_tablets_first &&
                 meta->table_type() == TRANSACTION_STATUS_TABLE_TYPE) {
        // Prioritize bootstrapping transaction status tablets first.
        std::lock_guard lock(metas.ready_metas_mutex);
        metas.ready_metas.push_front(meta);
      } else {
        std::lock_guard lock(metas.ready_metas_mutex);
        metas.ready_metas.push_back(meta);
      }
      return Status::OK();
    });
  }
  // If any open metadata task failed, the TaskRunner will stop waiting for other
  // pending tasks to complete and immediately return the failure.
  RETURN_NOT_OK(open_metadata_runner.Wait(StopWaitIfFailed::kTrue));

  MonoDelta elapsed = MonoTime::Now().GetDeltaSince(start);
  LOG(INFO) << "Loaded metadata for " << tablet_ids.size() << " tablet in "
            << elapsed.ToMilliseconds() << " ms";
  ts_open_metadata_time_us_->IncrementBy(elapsed.ToMicroseconds());

  // Validator should be created before tablets are open.
  tablet_metadata_validator_ = std::make_unique<TabletMetadataValidator>(LogPrefix(), this);

  // Now submit the "Open" task for each.
  {
    std::lock_guard lock(metas.ready_metas_mutex);
    for (const RaftGroupMetadataPtr& meta : metas.ready_metas) {
      RegisterDataAndWalDir(
          fs_manager_, meta->table_id(), meta->raft_group_id(), meta->data_root_dir(),
          meta->wal_root_dir());
      scoped_refptr<TransitionInProgressDeleter> deleter;
      RETURN_NOT_OK(StartTabletStateTransition(
          meta->raft_group_id(), "opening tablet", &deleter));

      TabletPeerPtr tablet_peer = VERIFY_RESULT(CreateAndRegisterTabletPeer(meta, NEW_PEER));
      RETURN_NOT_OK(open_tablet_pool_->SubmitFunc(
          std::bind(&TSTabletManager::OpenTablet, this, meta, deleter)));
    }
  }

  // After the OpenTablet tasks, we then handle the non ready tablets async.
  {
    std::lock_guard lock(metas.non_ready_metas_mutex);
    for (const RaftGroupMetadataPtr& meta : metas.non_ready_metas) {
      scoped_refptr<TransitionInProgressDeleter> deleter;
      RETURN_NOT_OK(StartTabletStateTransition(
          meta->raft_group_id(), "handle non ready tablet", &deleter));

      RETURN_NOT_OK(open_tablet_pool_->SubmitFunc(
          std::bind(&TSTabletManager::HandleNonReadyTabletOnStartup, this, meta, deleter)));
    }
  }

  // Background task initiation.
  const int32_t bg_superblock_flush_interval_secs = FLAGS_bg_superblock_flush_interval_secs;
  if (FLAGS_lazily_flush_superblock && bg_superblock_flush_interval_secs > 0) {
    superblock_flush_bg_task_.reset(new BackgroundTask(
        std::function<void()>([this]() { FlushDirtySuperblocks(); }), "tablet manager",
        "bg superblock flush",
        MonoDelta::FromSeconds(bg_superblock_flush_interval_secs).ToChronoMilliseconds()));
    RETURN_NOT_OK(superblock_flush_bg_task_->Init());
  }

  {
    std::lock_guard lock(mutex_);
    state_ = MANAGER_RUNNING;
  }

  RETURN_NOT_OK(mem_manager_->Init());

  RETURN_NOT_OK(full_compaction_manager_->Init());

  RETURN_NOT_OK(tablet_metadata_validator_->Init());

  tablets_cleaner_ = std::make_unique<rpc::Poller>(
      LogPrefix(), std::bind(&TSTabletManager::CleanupSplitTablets, this));

  verify_tablet_data_poller_ = std::make_unique<rpc::Poller>(
      LogPrefix(), std::bind(&TSTabletManager::VerifyTabletData, this));

  metrics_emitter_ = std::make_unique<rpc::Poller>(
      LogPrefix(), std::bind(&TSTabletManager::EmitMetrics, this));

  metrics_cleaner_ = std::make_unique<rpc::Poller>(
      LogPrefix(), std::bind(&TSTabletManager::CleanupOldMetrics, this));

  waiting_txn_registry_poller_ = std::make_unique<rpc::Poller>(
      LogPrefix(), std::bind(&TSTabletManager::PollWaitingTxnRegistry, this));

  return Status::OK();
}

Status TSTabletManager::RegisterServiceCallback(
    StatefulServiceKind service_kind, ConsensusChangeCallback callback) {
  std::lock_guard lock(service_registration_mutex_);
  SCHECK(
      !service_consensus_change_cb_.contains(service_kind), AlreadyPresent,
      "Service of kind $0 is already registered", StatefulServiceKind_Name(service_kind));

  service_consensus_change_cb_[service_kind] = callback;

  return Status::OK();
}

void TSTabletManager::CleanupCheckpoints() {
  for (const auto& data_root : fs_manager_->GetDataRootDirs()) {
    auto tables_dir = JoinPathSegments(data_root, FsManager::kRocksDBDirName);
    auto tables = fs_manager_->env()->GetChildren(tables_dir, ExcludeDots::kTrue);
    if (!tables.ok()) {
      LOG_WITH_PREFIX(WARNING)
          << "Failed to get tables in " << tables_dir << ": " << tables.status();
      continue;
    }
    for (const auto& table : *tables) {
      auto table_dir = JoinPathSegments(tables_dir, table);
      auto tablets = fs_manager_->env()->GetChildren(table_dir, ExcludeDots::kTrue);
      if (!tablets.ok()) {
        LOG_WITH_PREFIX(WARNING)
            << "Failed to get tablets in " << table_dir << ": " << tablets.status();
        continue;
      }
      for (const auto& tablet : *tablets) {
        auto checkpoints_dir = JoinPathSegments(
            table_dir, tablet, RemoteBootstrapSession::kCheckpointsDir);
        if (fs_manager_->env()->FileExists(checkpoints_dir)) {
          LOG_WITH_PREFIX(INFO) << "Cleaning up checkpoints dir: " << yb::ToString(checkpoints_dir);
          auto status = fs_manager_->env()->DeleteRecursively(checkpoints_dir);
          WARN_NOT_OK(status, Format("Cleanup of checkpoints dir $0 failed", checkpoints_dir));
        }
      }
    }
  }
}

Status TSTabletManager::Start() {
  if (FLAGS_cleanup_split_tablets_interval_sec > 0) {
    tablets_cleaner_->Start(
        &server_->messenger()->scheduler(), FLAGS_cleanup_split_tablets_interval_sec * 1s);
    LOG(INFO) << "Split tablets cleanup monitor started...";
  } else {
    LOG(INFO)
        << "Split tablets cleanup is disabled by cleanup_split_tablets_interval_sec flag set to 0";
  }
  if (FLAGS_verify_tablet_data_interval_sec > 0) {
    verify_tablet_data_poller_->Start(
        &server_->messenger()->scheduler(), FLAGS_verify_tablet_data_interval_sec * 1s);
    LOG(INFO) << "Tablet data verification task started...";
  } else {
    LOG(INFO)
        << "Tablet data verification is disabled by verify_tablet_data_interval_sec flag set to 0";
  }
  metrics_emitter_->Start(&server_->messenger()->scheduler(), 1s);
  if (FLAGS_cleanup_metrics_interval_sec > 0) {
    metrics_cleaner_->Start(
        &server_->messenger()->scheduler(), FLAGS_cleanup_metrics_interval_sec * 1s);
    LOG(INFO) << "Old metrics cleanup task started...";
  } else {
    LOG(INFO)
        << "Old metrics cleanup is disabled by cleanup_metrics_interval_sec flag set to 0";
  }

  if (waiting_txn_registry_) {
    waiting_txn_registry_poller_->Start(
        &server_->messenger()->scheduler(), FLAGS_send_wait_for_report_interval_ms * 1ms);
  }

  return Status::OK();
}

void TSTabletManager::CleanupSplitTablets() {
  VLOG_WITH_PREFIX_AND_FUNC(3) << "looking for tablets to cleanup...";
  auto tablet_peers = GetTabletPeers();
  for (const auto& tablet_peer : tablet_peers) {
    if (tablet_peer->CanBeDeleted()) {
      const auto& tablet_id = tablet_peer->tablet_id();
      if (PREDICT_FALSE(FLAGS_TEST_skip_deleting_split_tablets)) {
        LOG_WITH_PREFIX(INFO) << Format("Skipped triggering delete of tablet $0", tablet_id);
      } else {
        LOG_WITH_PREFIX(INFO) << Format("Triggering delete of tablet $0", tablet_id);
        client().DeleteNotServingTablet(
            tablet_peer->tablet_id(), [tablet_id](const Status& status) {
              LOG(INFO) << Format("Tablet $0 deletion result: $1", tablet_id, status);
            });
      }
    }
  }
}

Status TSTabletManager::WaitForAllBootstrapsToFinish(MonoDelta timeout) {
  CHECK_EQ(state(), MANAGER_RUNNING);

  if (timeout.Initialized()) {
    if (!open_tablet_pool_->WaitFor(timeout)) {
      return STATUS(TimedOut, "Timeout waiting for all bootstraps to finish");
    }
  } else {
    open_tablet_pool_->Wait();
  }

  Status s = Status::OK();

  SharedLock<RWMutex> shared_lock(mutex_);
  for (const TabletMap::value_type& entry : tablet_map_) {
    if (entry.second->state() == tablet::FAILED) {
      if (s.ok()) {
        s = entry.second->error();
      }
    }
  }

  return s;
}

Result<scoped_refptr<TransitionInProgressDeleter>>
TSTabletManager::StartTabletStateTransitionForCreation(const TabletId& tablet_id) {
  scoped_refptr<TransitionInProgressDeleter> deleter;
  SharedLock<RWMutex> lock(mutex_);
  TRACE("Acquired tablet manager lock");

  // Sanity check that the tablet isn't already registered.
  if (LookupTabletUnlocked(tablet_id) != nullptr) {
    return STATUS(AlreadyPresent, "Tablet already registered", tablet_id);
  }

  RETURN_NOT_OK(StartTabletStateTransition(tablet_id, "creating tablet", &deleter));

  return deleter;
}

Result<TabletPeerPtr> TSTabletManager::CreateNewTablet(
    const tablet::TableInfoPtr& table_info,
    const string& tablet_id,
    const dockv::Partition& partition,
    RaftConfigPB config,
    const bool colocated,
    const std::vector<SnapshotScheduleId>& snapshot_schedules,
    const std::unordered_set<StatefulServiceKind>& hosted_services) {
  SCOPED_WAIT_STATUS(CreatingNewTablet);
  if (state() != MANAGER_RUNNING) {
    return STATUS_FORMAT(IllegalState, "Manager is not running: $0", state());
  }
  CHECK(IsRaftConfigMember(server_->instance_pb().permanent_uuid(), config));

  for (int i = 0; i < config.peers_size(); ++i) {
    const auto& config_peer = config.peers(i);
    CHECK(config_peer.has_member_type());
  }

  // Set the initial opid_index for a RaftConfigPB to -1.
  config.set_opid_index(consensus::kInvalidOpIdIndex);

  scoped_refptr<TransitionInProgressDeleter> deleter =
      VERIFY_RESULT(StartTabletStateTransitionForCreation(tablet_id));

  // Create the metadata.
  TRACE("Creating new metadata...");
  string data_root_dir;
  string wal_root_dir;
  GetAndRegisterDataAndWalDir(
      fs_manager_, table_info->table_id, tablet_id, &data_root_dir, &wal_root_dir);
  fs_manager_->SetTabletPathByDataPath(tablet_id, data_root_dir);
  auto create_result = RaftGroupMetadata::CreateNew(tablet::RaftGroupMetadataData {
    .fs_manager = fs_manager_,
    .table_info = table_info,
    .raft_group_id = tablet_id,
    .partition = partition,
    .tablet_data_state = TABLET_DATA_READY,
    .colocated = colocated,
    .snapshot_schedules = snapshot_schedules,
    .hosted_services = hosted_services,
  }, data_root_dir, wal_root_dir);
  if (!create_result.ok()) {
    UnregisterDataWalDir(table_info->table_id, tablet_id, data_root_dir, wal_root_dir);
  }
  RETURN_NOT_OK_PREPEND(create_result, "Couldn't create tablet metadata")
  RaftGroupMetadataPtr meta = std::move(*create_result);
  LOG(INFO) << TabletLogPrefix(tablet_id)
            << "Created tablet metadata for table: " << table_info->table_id;

  // We must persist the consensus metadata to disk before starting a new
  // tablet's TabletPeer and Consensus implementation.
  std::unique_ptr<ConsensusMetadata> cmeta = VERIFY_RESULT_PREPEND(
      ConsensusMetadata::Create(
          fs_manager_, tablet_id, fs_manager_->uuid(), config, consensus::kMinimumTerm),
      "Unable to create new ConsensusMeta for tablet " + tablet_id);
  TabletPeerPtr new_peer = VERIFY_RESULT(CreateAndRegisterTabletPeer(meta, NEW_PEER));

  // We can run this synchronously since there is nothing to bootstrap.
  RETURN_NOT_OK(
      open_tablet_pool_->SubmitFunc(std::bind(&TSTabletManager::OpenTablet, this, meta, deleter)));

  return new_peer;
}

struct TabletCreationMetaData {
  TabletId tablet_id;
  scoped_refptr<TransitionInProgressDeleter> transition_deleter;
  dockv::Partition partition;
  docdb::KeyBounds key_bounds;
  RaftGroupMetadataPtr raft_group_metadata;
};

namespace {

// Creates SplitTabletsCreationMetaData for two new tablets for `tablet` splitting based on request.
SplitTabletsCreationMetaData PrepareTabletCreationMetaDataForSplit(
    const tablet::SplitTabletRequestPB& request, const tablet::Tablet& tablet) {
  SplitTabletsCreationMetaData metas;

  const auto& split_partition_key = request.split_partition_key();
  const auto& split_encoded_key = request.split_encoded_key();

  auto source_partition = tablet.metadata()->partition();
  const auto& source_key_bounds = tablet.key_bounds();

  {
    TabletCreationMetaData meta;
    meta.tablet_id = request.new_tablet1_id();
    meta.partition = *source_partition;
    meta.key_bounds = source_key_bounds;
    meta.key_bounds.upper.Reset(split_encoded_key);
    meta.partition.set_partition_key_end(split_partition_key);
    metas.push_back(meta);
  }

  {
    TabletCreationMetaData meta;
    meta.tablet_id = request.new_tablet2_id();
    meta.partition = *source_partition;
    meta.key_bounds = source_key_bounds;
    meta.key_bounds.lower.Reset(split_encoded_key);
    meta.partition.set_partition_key_start(split_partition_key);
    metas.push_back(meta);
  }

  return metas;
}

}  // namespace

Status TSTabletManager::CleanUpSubtabletIfExistsOnDisk(
    const RaftGroupMetadata& source_tablet_meta, const TabletId& tablet_id, Env* env) {
  // Delete on-disk data for a tablet that was partially created as part of a split / clone op.
  // TODO(tsplit): add test for this.
  const auto data_dir = source_tablet_meta.GetSubRaftGroupDataDir(tablet_id);
  if (env->FileExists(data_dir)) {
    LOG(INFO) << Format("Cleaning up data dir ($0) for subtablet ($1)", data_dir, tablet_id);
    RETURN_NOT_OK_PREPEND(
        env->DeleteRecursively(data_dir),
        Format("Unable to recursively delete data dir for tablet $0", tablet_id));
  }
  RETURN_NOT_OK(Log::DeleteOnDiskData(
      env, tablet_id, source_tablet_meta.GetSubRaftGroupWalDir(tablet_id),
      fs_manager_->uuid()));
  RETURN_NOT_OK(ConsensusMetadata::DeleteOnDiskData(fs_manager_, tablet_id));
  return Status::OK();
}

Status TSTabletManager::StartSubtabletsSplit(
    const RaftGroupMetadata& source_tablet_meta, SplitTabletsCreationMetaData* tcmetas) {
  auto* const env = fs_manager_->env();

  auto iter = tcmetas->begin();
  while (iter != tcmetas->end()) {
    const auto& subtablet_id = iter->tablet_id;

    auto transition_deleter_result = StartTabletStateTransitionForCreation(subtablet_id);
    if (transition_deleter_result.ok()) {
      iter->transition_deleter = *transition_deleter_result;
    } else if (transition_deleter_result.status().IsAlreadyPresent()) {
      // State transition for sub tablet with subtablet_id could be already registered because its
      // remote bootstrap (from already split parent tablet leader) is in progress.
      // TODO(asrivastava): We can also get this error as a result of a slow delete of a partially
      // created tablet. We would fail the subsequent restore so it's not a huge issue for clone,
      // but we should eventually fix this for tablet splitting.
      iter = tcmetas->erase(iter);
      continue;
    } else {
      return transition_deleter_result.status();
    }

    // Try to load metadata from previous not completed split.
    if (fs_manager_->LookupTablet(subtablet_id)) {
      auto load_result = RaftGroupMetadata::Load(fs_manager_, subtablet_id);
      if (load_result.ok() && CanServeTabletData((*load_result)->tablet_data_state())) {
        // Sub tablet has been already created and ready during previous split attempt at this node
        // or as a result of remote bootstrap from another node, no need to re-create.
        iter = tcmetas->erase(iter);
        continue;
      }
    }

    RETURN_NOT_OK(CleanUpSubtabletIfExistsOnDisk(source_tablet_meta, subtablet_id, env));
    ++iter;
  }
  return Status::OK();
}

void TSTabletManager::CreatePeerAndOpenTablet(
    const tablet::RaftGroupMetadataPtr& meta,
    const scoped_refptr<TransitionInProgressDeleter>& deleter) {
  Status s = ResultToStatus(CreateAndRegisterTabletPeer(meta, NEW_PEER));
  if (!s.ok()) {
    s = s.CloneAndPrepend("Failed to create and register tablet peer");
    if (s.IsShutdownInProgress()) {
      // If shutdown is in progress, it is not a failure to not being able to create and register
      // tablet peer.
      LOG_WITH_PREFIX(WARNING) << s;
    } else {
      LOG_WITH_PREFIX(DFATAL) << s;
    }
    return;
  }
  s = open_tablet_pool_->SubmitFunc(std::bind(&TSTabletManager::OpenTablet, this, meta, deleter));
  if (!s.ok()) {
    LOG(DFATAL) << Format("Failed to schedule opening tablet $0: $1", meta->table_id(), s);
    return;
  }
}

Status TSTabletManager::ApplyTabletSplit(
    tablet::SplitOperation* operation, log::Log* raft_log,
    boost::optional<RaftConfigPB> committed_raft_config) {
  if (PREDICT_FALSE(FLAGS_TEST_crash_before_apply_tablet_split_op)) {
    LOG(FATAL) << "Crashing due to FLAGS_TEST_crash_before_apply_tablet_split_op";
  }

  if (state() != MANAGER_RUNNING) {
    return STATUS_FORMAT(IllegalState, "Manager is not running: $0", state());
  }

  const auto& split_op_id = operation->op_id();
  SCHECK_FORMAT(
      split_op_id.valid() && !split_op_id.empty(), InvalidArgument,
      "Incorrect ID for SPLIT_OP: $0", operation->ToString());
  auto tablet = VERIFY_RESULT(operation->tablet_safe());
  const auto tablet_id = tablet->tablet_id();
  const auto* request = operation->request();
  SCHECK_EQ(
      request->tablet_id(), tablet_id, IllegalState,
      Format(
          "Unexpected SPLIT_OP $0 designated for tablet $1 to be applied to tablet $2",
          split_op_id, request->tablet_id(), tablet_id));
  SCHECK(
      tablet_id != request->new_tablet1_id() && tablet_id != request->new_tablet2_id(),
      IllegalState,
      Format(
          "One of SPLIT_OP $0 destination tablet IDs ($1, $2) is the same as source tablet ID $3",
          split_op_id, request->new_tablet1_id(), request->new_tablet2_id(), tablet_id));

  LOG_WITH_PREFIX(INFO) << "Tablet " << tablet_id << " split operation " << split_op_id
                        << " apply started";

  auto tablet_peer = VERIFY_RESULT(GetTablet(tablet_id));
  if (raft_log == nullptr) {
    raft_log = VERIFY_RESULT(tablet_peer->GetRaftConsensus())->log().get();
  }
  if (!committed_raft_config) {
    committed_raft_config =
        VERIFY_RESULT(tablet_peer->GetRaftConsensus())->CommittedConfigUnlocked();
  }

  MAYBE_FAULT(FLAGS_TEST_fault_crash_in_split_before_log_flushed);
  TEST_PAUSE_IF_FLAG(TEST_pause_apply_tablet_split);

  auto& meta = *CHECK_NOTNULL(tablet->metadata());

  if (meta.tablet_data_state() == TABLET_DATA_SPLIT_COMPLETED) {
    LOG_WITH_PREFIX(INFO) << "Tablet " << tablet_id
                          << " split operation has been trivially applied, "
                          << "because tablet is already in TABLET_DATA_SPLIT_COMPLETED state";
    return Status::OK();
  }

  // TODO(tsplit): We can later implement better per-disk distribution during compaction of split
  // tablets.
  const auto table_id = meta.table_id();
  const auto data_root_dir =
      VERIFY_RESULT(GetAssignedRootDirForTablet(TabletDirType::kData, table_id, tablet_id));
  const auto wal_root_dir =
      VERIFY_RESULT(GetAssignedRootDirForTablet(TabletDirType::kWal, table_id, tablet_id));

  if (FLAGS_TEST_apply_tablet_split_inject_delay_ms > 0) {
    LOG(INFO) << "TEST: ApplyTabletSplit: injecting delay of "
              << FLAGS_TEST_apply_tablet_split_inject_delay_ms << " ms for "
              << AsString(*operation);
    std::this_thread::sleep_for(FLAGS_TEST_apply_tablet_split_inject_delay_ms * 1ms);
    LOG(INFO) << "TEST: ApplyTabletSplit: delay finished";
  }

  auto tcmetas = PrepareTabletCreationMetaDataForSplit(request->ToGoogleProtobuf(), *tablet);

  RETURN_NOT_OK(StartSubtabletsSplit(meta, &tcmetas));

  for (const auto& tcmeta : tcmetas) {
    RegisterDataAndWalDir(fs_manager_, table_id, tcmeta.tablet_id, data_root_dir, wal_root_dir);
    fs_manager_->SetTabletPathByDataPath(tcmeta.tablet_id, data_root_dir);
  }

  bool successfully_completed = false;
  auto se = ScopeExit([&] {
    if (!successfully_completed) {
      for (const auto& tcmeta : tcmetas) {
        UnregisterDataWalDir(table_id, tcmeta.tablet_id, data_root_dir, wal_root_dir);
      }
    }
  });

  std::unique_ptr<ConsensusMetadata> cmeta = VERIFY_RESULT(ConsensusMetadata::Create(
      fs_manager_, tablet_id, fs_manager_->uuid(), committed_raft_config.value(),
      split_op_id.term));
  if (request->has_split_parent_leader_uuid()) {
    cmeta->set_leader_uuid(request->split_parent_leader_uuid().ToBuffer());
    LOG_WITH_PREFIX(INFO) << "Using Raft config: " << committed_raft_config->ShortDebugString();
  }
  cmeta->set_split_parent_tablet_id(tablet_id);

  for (auto& tcmeta : tcmetas) {
    const auto& new_tablet_id = tcmeta.tablet_id;

    // Copy raft group metadata.
    tcmeta.raft_group_metadata = VERIFY_RESULT(tablet->CreateSubtablet(
        new_tablet_id, tcmeta.partition, tcmeta.key_bounds, split_op_id,
        operation->hybrid_time()));
    LOG_WITH_PREFIX(INFO) << "Created raft group metadata for table: " << table_id
                          << " tablet: " << new_tablet_id;

    // Store consensus metadata.
    // Here we reuse the same cmeta instance for both new tablets. This is safe, because:
    // 1) Their consensus metadata only differ by tablet id.
    // 2) Flush() will save it into a new path corresponding to tablet ID we set before flushing.
    cmeta->set_tablet_id(new_tablet_id);
    RETURN_NOT_OK(cmeta->Flush());

    const auto& dest_wal_dir = tcmeta.raft_group_metadata->wal_dir();
    RETURN_NOT_OK(raft_log->CopyTo(dest_wal_dir, split_op_id));

    MAYBE_FAULT(FLAGS_TEST_fault_crash_in_split_after_log_copied);

    tcmeta.raft_group_metadata->set_tablet_data_state(TABLET_DATA_READY);
    RETURN_NOT_OK(tcmeta.raft_group_metadata->Flush());
  }

  if (PREDICT_FALSE(FLAGS_TEST_crash_before_source_tablet_mark_split_done)) {
    LOG(FATAL) << "Crashing due to FLAGS_TEST_crash_before_source_tablet_mark_split_done";
  }

  meta.SetSplitDone(
      split_op_id, request->new_tablet1_id().ToBuffer(), request->new_tablet2_id().ToBuffer());
  RETURN_NOT_OK(meta.Flush());

  tablet->SplitDone();

  if (PREDICT_FALSE(FLAGS_TEST_crash_after_tablet_split_completed)) {
    LOG(FATAL) << "Crashing due to FLAGS_TEST_crash_after_tablet_split_completed";
  }

  for (auto& tcmeta : tcmetas) {
    // Call CreatePeerAndOpenTablet asynchronously to avoid write-locking TSTabletManager::mutex_
    // here since apply of SPLIT_OP is done under ReplicaState lock and this could lead to deadlock
    // in case of reverse lock order in some other thread.
    // See https://github.com/yugabyte/yugabyte-db/issues/4312 for more details.
    RETURN_NOT_OK(apply_pool_->SubmitFunc(std::bind(
        &TSTabletManager::CreatePeerAndOpenTablet, this, tcmeta.raft_group_metadata,
        tcmeta.transition_deleter)));
  }

  successfully_completed = true;
  LOG_WITH_PREFIX(INFO) << "Tablet " << tablet_id << " split operation has been applied";
  ts_split_op_apply_->Increment();
  return Status::OK();
}

Status TSTabletManager::DoApplyCloneTablet(
    tablet::CloneOperation* operation, log::Log* raft_log,
    std::optional<RaftConfigPB> committed_raft_config) {
  if (FLAGS_TEST_fail_apply_clone_op) {
    return STATUS(
        RuntimeError, "Failing DoApplyCloneTablet due to FLAGS_TEST_fail_apply_clone_op");
  }

  auto clone_op_id = operation->op_id();
  const auto source_tablet = VERIFY_RESULT(operation->tablet_safe());
  const auto source_table = VERIFY_RESULT(source_tablet->GetTableInfo(kColocationIdNotSet));
  const auto source_tablet_id = source_tablet->tablet_id();
  const auto* request = operation->request();
  const auto source_snapshot_id = VERIFY_RESULT(
      FullyDecodeTxnSnapshotId(request->source_snapshot_id()));
  const auto target_snapshot_id = VERIFY_RESULT(
      FullyDecodeTxnSnapshotId(request->target_snapshot_id()));
  const auto target_table_id = request->target_table_id().ToBuffer();
  const auto target_tablet_id = request->target_tablet_id().ToBuffer();
  const auto target_namespace_name = request->target_namespace_name().ToBuffer();
  const auto target_pg_table_id = request->target_pg_table_id().ToBuffer();
  const auto target_skip_table_tombstone_check =
      request->target_skip_table_tombstone_check();
  const boost::optional<qlexpr::IndexInfo> target_table_index_info =
      request->has_target_index_info() ?
      boost::optional<qlexpr::IndexInfo>(request->target_index_info().ToGoogleProtobuf()) :
      boost::none;
  Schema target_schema;
  dockv::PartitionSchema target_partition_schema;
  RETURN_NOT_OK(SchemaFromPB(request->target_schema().ToGoogleProtobuf(), &target_schema));
  RETURN_NOT_OK(dockv::PartitionSchema::FromPB(
      request->target_partition_schema().ToGoogleProtobuf(),
      target_schema, &target_partition_schema));

  auto tablet_peer = VERIFY_RESULT(GetTablet(source_tablet_id));
  if (raft_log == nullptr) {
    raft_log = VERIFY_RESULT(tablet_peer->GetRaftConsensus())->log().get();
  }
  if (!committed_raft_config) {
    committed_raft_config =
        VERIFY_RESULT(tablet_peer->GetRaftConsensus())->CommittedConfigUnlocked();
  }

  // State transition for clone target could be already registered because it is remote
  // bootstrapping from an existing quorum. We would ideally avoid this by passing
  // clone_request_seq_no everywhere we call StartRemoteBootstrap (which would cause the RBS to be
  // rejected instead), but it is worth having this check to be safe.
  // TODO(anubhav): go through StartRemoteBootstrap calls and add clone_request_seq_no wherever
  //                possible.
  auto transition_deleter_result = StartTabletStateTransitionForCreation(target_tablet_id);
  if (!transition_deleter_result.ok()) {
    auto status = transition_deleter_result.status();
    if (!status.IsAlreadyPresent()) {
      return status;
    }
    // TODO(asrivastava): We can also get this error as a result of a slow delete of a partially
    // created tablet. We would fail the subsequent restore so it's not a huge issue for clone, but
    // we should fix this for tablet splitting.
    LOG_WITH_PREFIX(INFO) << "Clone target tablet is already present or in transition: " << status;
    return Status::OK();
  }

  // Data and WAL dirs should be the same as the source tablet so we can hard link SSTs.
  const auto& source_meta = *CHECK_NOTNULL(source_tablet->metadata());
  RETURN_NOT_OK(CleanUpSubtabletIfExistsOnDisk(
      source_meta, target_tablet_id, fs_manager_->env()));
  string data_root_dir = source_meta.data_root_dir();
  string wal_root_dir = source_meta.wal_root_dir();
  RegisterDataAndWalDir(
      fs_manager_, target_table_id, target_tablet_id, data_root_dir, wal_root_dir);
  fs_manager_->SetTabletPathByDataPath(target_tablet_id, data_root_dir);

  bool successfully_created_target = false;
  auto se = ScopeExit([&] {
    if (!successfully_created_target) {
      UnregisterDataWalDir(target_table_id, target_tablet_id, data_root_dir, wal_root_dir);
    }
  });

  std::unique_ptr<ConsensusMetadata> cmeta = VERIFY_RESULT(ConsensusMetadata::Create(
      fs_manager_, target_tablet_id, fs_manager_->uuid(), committed_raft_config.value(),
      clone_op_id.term));
  cmeta->set_clone_source_info(request->clone_request_seq_no(), source_tablet_id);
  RETURN_NOT_OK(cmeta->Flush());

  auto log_prefix = consensus::MakeTabletLogPrefix(target_tablet_id, server_->permanent_uuid());
  auto target_table = std::make_shared<tablet::TableInfo>(
      log_prefix,
      tablet::Primary(source_table->primary()),
      target_table_id,
      target_namespace_name,
      source_table->table_name,
      source_table->table_type,
      /* Fixed by restore, but we need it to get partition_schema so might as well set it. */
      target_schema,
      *source_table->index_map, /* fixed by restore */
      std::move(target_table_index_info),
      source_table->schema_version, /* fixed by restore */
      target_partition_schema,
      target_pg_table_id,
      tablet::SkipTableTombstoneCheck(target_skip_table_tombstone_check));
  std::vector<tablet::TableInfoPtr> colocated_tables_infos;

  for (const auto& colocated_table : ToRepeatedPtrField(request->colocated_tables())) {
    VLOG(3) << Format(
        "Adding table $0 to the tablet: $1", colocated_table.table_id(), target_tablet_id);
    colocated_tables_infos.push_back(VERIFY_RESULT(
        tablet::TableInfo::LoadFromPB(log_prefix, target_table_id, colocated_table)));
  }
  // Setup raft group metadata. If we crash between here and when we set the tablet data state to
  // TABLET_DATA_READY, the tablet will be deleted on the next bootstrap.
  tablet::RaftGroupMetadataData target_meta_data{
      .fs_manager = fs_manager_,
      .table_info = target_table,
      .raft_group_id = target_tablet_id,
      .partition = *source_meta.partition(),
      .tablet_data_state = TABLET_DATA_INIT_STARTED,
      .colocated = source_meta.colocated(),
      .snapshot_schedules = {},
      .hosted_services = {},
      .colocated_tables_infos = colocated_tables_infos,
  };
  auto target_meta =
      VERIFY_RESULT(RaftGroupMetadata::CreateNew(target_meta_data, data_root_dir, wal_root_dir));
  // Create target parent table directory if required.
  auto target_parent_table_dir = DirName(target_meta->snapshots_dir());
  RETURN_NOT_OK_PREPEND(
      fs_manager_->CreateDirIfMissingAndSync(target_parent_table_dir),
      Format("Failed to create RocksDB table directory $0", target_parent_table_dir));
  auto target_snapshot_dir = JoinPathSegments(
      VERIFY_RESULT(target_meta->TopSnapshotsDir()), target_snapshot_id.ToString());

  // Copy or create the snapshot into the target snapshot directory.
  if (request->clone_from_active_rocksdb()) {
    RETURN_NOT_OK_PREPEND(source_tablet->snapshots().Create(tablet::CreateSnapshotData {
        .snapshot_hybrid_time = operation->hybrid_time(),
        .hybrid_time = operation->hybrid_time(),
        .op_id = clone_op_id,
        .snapshot_dir = target_snapshot_dir,
        .schedule_id = SnapshotScheduleId::Nil(),
    }), Format("Could not create from active rocksdb of tablet $0", source_tablet_id));
  } else {
    auto source_snapshot_dir = JoinPathSegments(VERIFY_RESULT(
        source_tablet->metadata()->TopSnapshotsDir()), source_snapshot_id.ToString());
    LOG(INFO) << Format("Hard-linking from $0 to $1", source_snapshot_dir, target_snapshot_dir);
    RETURN_NOT_OK(CopyDirectory(
        fs_manager_->env(), source_snapshot_dir, target_snapshot_dir, UseHardLinks::kTrue,
        CreateIfMissing::kTrue));
  }

  if (PREDICT_FALSE(FLAGS_TEST_crash_before_clone_target_marked_ready)) {
    LOG(FATAL) << "Crashing due to FLAGS_TEST_crash_before_clone_target_marked_ready";
  }
  // Mark this tablet as ready now that we have copied the snapshot files.
  target_meta->set_tablet_data_state(TABLET_DATA_READY);
  RETURN_NOT_OK(target_meta->Flush());

  // Call CreatePeerAndOpenTablet asynchronously to avoid write-locking TSTabletManager::mutex_
  // here since apply of CLONE_OP is done under ReplicaState lock and this could lead to deadlock
  // in case of reverse lock order in some other thread.
  // See https://github.com/yugabyte/yugabyte-db/issues/4312 for more details.
  RETURN_NOT_OK(apply_pool_->SubmitFunc(std::bind(
      &TSTabletManager::CreatePeerAndOpenTablet, this, target_meta, *transition_deleter_result)));
  successfully_created_target = true;

  return Status::OK();
}

Status TSTabletManager::ApplyCloneTablet(
    tablet::CloneOperation* operation, log::Log* raft_log,
    std::optional<RaftConfigPB> committed_raft_config) {
  // The semantics of clone op are different from most operations. During apply, a clone op will:
  // 1. ATTEMPT to create a clone of source tablet AND
  // 2. Set last_attempted_clone_seq_no_ to clone_request_seq_no
  // Thus, a clone op that has failed to create a target tablet will still apply successfully.
  // As long as applying the clone op affects each replica of the source tablet in the same way,
  // the invariant that applying the same op to identical replicas A and B will yield identical
  // replicas A' and B' is maintained. Since the only way the clone op modifies the source tablet
  // is to set last_attempted_clone_seq_no_, this assumption is met.

  if (state() != MANAGER_RUNNING) {
    return STATUS_FORMAT(IllegalState, "Manager is not running: $0", state());
  }

  auto source_tablet = VERIFY_RESULT(operation->tablet_safe());
  const auto source_tablet_id = source_tablet->tablet_id();
  const auto clone_request_seq_no = operation->request()->clone_request_seq_no();

  auto& source_meta = *CHECK_NOTNULL(source_tablet->metadata());
  if (source_meta.HasAttemptedClone(clone_request_seq_no)) {
    LOG_WITH_PREFIX(INFO) << Format(
        "Clone operation for tablet $0 has been trivially applied because clone was already "
        "attempted with the same seq no ($1)", source_tablet_id, clone_request_seq_no);
    return Status::OK();
  }

  LOG(INFO) << Format(
      "Starting apply of clone op with seq_no $0 on tablet $1", clone_request_seq_no,
      source_tablet_id);
  auto status = DoApplyCloneTablet(operation, raft_log, committed_raft_config);
  if (!status.ok()) {
    if (FLAGS_TEST_expect_clone_apply_failure) {
      LOG(INFO) << "Failed to apply clone tablet: " << status;
    } else {
      LOG(DFATAL) << "Failed to apply clone tablet: " << status;
    }
  }

  if (PREDICT_FALSE(FLAGS_TEST_crash_before_mark_clone_attempted)) {
    LOG(FATAL) << "Crashing due to FLAGS_TEST_crash_before_mark_clone_attempted";
  }
  source_meta.MarkClonesAttemptedUpTo(clone_request_seq_no);
  RETURN_NOT_OK(source_meta.Flush());
  LOG_WITH_PREFIX(INFO) << Format(
      "Clone operation for tablet $0 with seq_no $1 has been marked as attempted", source_tablet_id,
      clone_request_seq_no);
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
    LOG(WARNING) << LogPrefix(tablet_id, uuid) << "Remote bootstrap: " << s;
    return s;
  }
  return Status::OK();
}

Status HandleReplacingStaleTablet(
    RaftGroupMetadataPtr meta,
    TabletPeerPtr old_tablet_peer,
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
      int64_t last_logged_term = meta->tombstone_last_logged_opid().term;
      RETURN_NOT_OK(CheckLeaderTermNotLower(tablet_id,
                                            uuid,
                                            leader_term,
                                            last_logged_term));
      break;
    }
    case TABLET_DATA_SPLIT_COMPLETED:
    case TABLET_DATA_READY: {
      if (tablet_id == master::kSysCatalogTabletId) {
        LOG(FATAL) << LogPrefix(tablet_id, uuid) << " Remote bootstrap: "
                   << "Found tablet in " << TabletDataState_Name(data_state)
                   << " state during StartRemoteBootstrap()";
      }
      // There's a valid race here that can lead us to come here:
      // 1. Leader sends a second remote bootstrap request as a result of receiving a
      // TABLET_NOT_FOUND from this tserver while it was in the middle of a remote bootstrap.
      // 2. The remote bootstrap request arrives after the first one is finished, and it is able to
      // grab the mutex.
      // 3. This tserver finds that it already has the metadata for the tablet, and determines that
      // it needs to replace the tablet setting replacing_tablet to true.
      // In this case, the master can simply ignore this error.
      return STATUS_FORMAT(
          IllegalState, "Tablet $0 in $1 state", tablet_id, TabletDataState_Name(data_state));
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
  const TabletId& tablet_id = req.tablet_id();
  const PeerId& bootstrap_peer_uuid = req.bootstrap_source_peer_uuid();
  const auto& kLogPrefix = TabletLogPrefix(tablet_id);
  const auto& private_addr = req.bootstrap_source_private_addr()[0].host();
  scoped_refptr<TransitionInProgressDeleter> deleter;

  LongOperationTracker tracker("StartRemoteBootstrap", 5s);

  // RegisterRemoteClientAndLookupTablet increments num_tablets_being_remote_bootstrapped_ and
  // populates bootstrap_source_addresses_ before looking up the tablet. Define this
  // ScopeExit before calling RegisterRemoteClientAndLookupTablet so that if it fails,
  // we cleanup as expected.
  auto decrement_num_session = ScopeExit([this, &private_addr]() {
    DecrementRemoteSessionCount(private_addr, &remote_bootstrap_clients_);
  });
  TabletPeerPtr old_tablet_peer = VERIFY_RESULT(RegisterRemoteClientAndLookupTablet(
      tablet_id, private_addr, kLogPrefix, &remote_bootstrap_clients_,
      std::bind(
          &TSTabletManager::StartTabletStateTransition, this, tablet_id,
          Format("remote bootstrapping tablet from peer $0", bootstrap_peer_uuid), &deleter)));

  bool replacing_tablet = old_tablet_peer != nullptr;
  bool has_faulty_drive = fs_manager_->has_faulty_drive();
  if (has_faulty_drive && !replacing_tablet) {
    return STATUS(ServiceUnavailable,
                  "Reject RBS because tserver cannot create new tablet if there is faulty drive. "
                  "If the faulty drive has been fixed, the tserver needs to be restarted for "
                  "the FSManager state to be refreshed");
  }

  RaftGroupMetadataPtr meta = replacing_tablet ? old_tablet_peer->tablet_metadata() : nullptr;

  HostPort bootstrap_peer_addr = HostPortFromPB(DesiredHostPort(
      req.bootstrap_source_broadcast_addr(), req.bootstrap_source_private_addr(),
      req.bootstrap_source_cloud_info(), server_->MakeCloudInfoPB()));

  auto rbs_source_role = "LEADER";
  ServerRegistrationPB tablet_leader_peer_conn_info;
  if (!req.is_served_by_tablet_leader()) {
    *tablet_leader_peer_conn_info.mutable_broadcast_addresses() =
        req.tablet_leader_broadcast_addr();
    *tablet_leader_peer_conn_info.mutable_private_rpc_addresses() =
        req.tablet_leader_private_addr();
    *tablet_leader_peer_conn_info.mutable_cloud_info() = req.tablet_leader_cloud_info();
    rbs_source_role = "FOLLOWER";
  }

  int64_t leader_term = req.caller_term();

  if (replacing_tablet) {
    // Make sure the existing tablet peer is shut down and tombstoned.
    RETURN_NOT_OK(HandleReplacingStaleTablet(meta,
                                             old_tablet_peer,
                                             tablet_id,
                                             fs_manager_->uuid(),
                                             leader_term));
  }

  auto rb_client = InitRemoteClient<RemoteBootstrapClient>(
      kLogPrefix, tablet_id, bootstrap_peer_uuid, bootstrap_peer_addr.ToString(),
      kDebugBootstrapString);

  if (replacing_tablet) {
    RETURN_NOT_OK(rb_client->SetTabletToReplace(meta, leader_term));
  }

  TEST_PAUSE_IF_FLAG(TEST_pause_before_remote_bootstrap);

  // Download and persist the remote superblock in TABLET_DATA_COPYING state.
  RETURN_NOT_OK(rb_client->Start(bootstrap_peer_uuid,
                                 &server_->proxy_cache(),
                                 bootstrap_peer_addr,
                                 tablet_leader_peer_conn_info,
                                 &meta,
                                 this));

  // From this point onward, the superblock is persisted in TABLET_DATA_COPYING
  // state, and we need to tombstone the tablet if additional steps prior to
  // getting to a TABLET_DATA_READY state fail.

  if (PREDICT_FALSE(FLAGS_TEST_simulate_already_present_in_remote_bootstrap)) {
    LOG_WITH_PREFIX(INFO)
        << "Simulating AlreadyPresent error in TSTabletManager::StartRemoteBootstrap.";
    return STATUS(AlreadyPresent, "failed");
  }

  // Registering a non-initialized TabletPeer offers visibility through the Web UI.
  TabletPeerPtr tablet_peer = VERIFY_RESULT(
        CreateAndRegisterTabletPeer(meta, replacing_tablet ? REPLACEMENT_PEER : NEW_PEER));
  MarkTabletBeingRemoteBootstrapped(tablet_peer->tablet_id(),
      tablet_peer->tablet_metadata()->table_id());
  // Set the bootstrap source info for the peer's status listener for external visibility.
  tablet_peer->status_listener()->SetStatusPrefix(
      Format("Bootstrap Source: $0 ($1) [$2]",
             bootstrap_peer_uuid, bootstrap_peer_addr, rbs_source_role));

  // TODO: If we ever make this method asynchronous, we need to move this code somewhere else.
  auto se = ScopeExit([this, tablet_peer] {
    UnmarkTabletBeingRemoteBootstrapped(tablet_peer->tablet_id(),
        tablet_peer->tablet_metadata()->table_id());
  });

  // Download all of the remote files.
  TOMBSTONE_NOT_OK(rb_client->FetchAll(tablet_peer->status_listener()),
                   meta,
                   fs_manager_->uuid(),
                   "Remote bootstrap: Unable to fetch data from remote peer " +
                       bootstrap_peer_uuid + " (" + bootstrap_peer_addr.ToString() + ")",
                   this);

  MAYBE_FAULT(FLAGS_TEST_fault_crash_after_rb_files_fetched);

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
                   "Remote bootstrap: Failed calling Finish()",
                   this);

  LOG(INFO) << kLogPrefix << "Remote bootstrap: Opening tablet";

  // TODO(hector):  ENG-3173: We need to simulate a failure in OpenTablet during remote bootstrap
  // and verify that this tablet server gets remote bootstrapped again by the leader. We also need
  // to check what happens when this server receives raft consensus requests since at this point,
  // this tablet server could be a voter (if the ChangeRole request in Finish succeeded and its
  // initial role was PRE_VOTER).
  OpenTablet(meta, nullptr);
  // If OpenTablet fails, tablet_peer->error() will be set.
  RETURN_NOT_OK(ShutdownAndTombstoneTabletPeerNotOk(
      tablet_peer->error(), tablet_peer, meta, fs_manager_->uuid(),
      "Remote bootstrap: OpenTablet() failed", this));

  auto status = rb_client->VerifyChangeRoleSucceeded(VERIFY_RESULT(tablet_peer->GetConsensus()));
  if (!status.ok()) {
    // If for some reason this tserver wasn't promoted (e.g. from PRE-VOTER to VOTER), the leader
    // will find out and do the CHANGE_CONFIG.
    LOG(WARNING) << kLogPrefix << "Remote bootstrap finished. "
                               << "Failure calling VerifyChangeRoleSucceeded: "
                               << status.ToString();
  } else {
    LOG(INFO) << kLogPrefix << "Remote bootstrap for tablet ended successfully";
  }

  WARN_NOT_OK(rb_client->Remove(), "Remove remote bootstrap sessions failed");

  return Status::OK();
}

Status TSTabletManager::StartRemoteSnapshotTransfer(
    const StartRemoteSnapshotTransferRequestPB& req) {
  const TabletId& tablet_id = req.tablet_id();
  const PeerId& source_uuid = req.source_peer_uuid();
  const auto& kLogPrefix = TabletLogPrefix(tablet_id);
  const auto& private_addr = req.source_private_addr()[0].host();
  const auto& source_addr = HostPortFromPB(DesiredHostPort(
      req.source_broadcast_addr(), req.source_private_addr(), req.source_cloud_info(),
      server_->MakeCloudInfoPB()));

  LongOperationTracker tracker("StartRemoteSnapshotTransfer", 5s);

  // RegisterRemoteClientAndLookupTablet increments num_remote_snapshot_transfer_clients_ and
  // populates snapshot_transfer_source_addresses_ before looking up the tablet. Define this
  // ScopeExit before calling RegisterRemoteClientAndLookupTablet so that if it fails,
  // we cleanup as expected.
  auto decrement_num_session = ScopeExit([this, &private_addr]() {
    DecrementRemoteSessionCount(private_addr, &snapshot_transfer_clients_);
  });
  TabletPeerPtr tablet = VERIFY_RESULT(RegisterRemoteClientAndLookupTablet(
      tablet_id, private_addr, kLogPrefix, &snapshot_transfer_clients_));

  SCHECK(tablet, InvalidArgument, Format("Could not find tablet $0", tablet_id));
  const auto& rocksdb_dir = tablet->tablet_metadata()->rocksdb_dir();

  auto remote_snapshot_client = InitRemoteClient<RemoteSnapshotTransferClient>(
      kLogPrefix, tablet_id, source_uuid, source_addr.ToString(), kDebugSnapshotTransferString);

  // Download and persist the remote superblock.
  RETURN_NOT_OK(remote_snapshot_client->Start(
      &server_->proxy_cache(), source_uuid, source_addr, rocksdb_dir));

  // Download the remote file specified in the request.
  auto snapshot_id = SnapshotIdToString(req.snapshot_id());
  auto new_snapshot_id =
      req.has_new_snapshot_id() ? SnapshotIdToString(req.new_snapshot_id()) : snapshot_id;
  RETURN_NOT_OK_PREPEND(
      remote_snapshot_client->FetchSnapshot(snapshot_id, new_snapshot_id),
      "remote snapshot transfer: Unable to fetch data from remote tablet " + source_uuid + " (" +
          source_addr.ToString() + ")");

  RETURN_NOT_OK(remote_snapshot_client->Finish());

  WARN_NOT_OK(remote_snapshot_client->Remove(), "Remove remote snapshot transfer session failed");

  return Status::OK();
}

// Create and register a new TabletPeer, given tablet metadata.
Result<TabletPeerPtr> TSTabletManager::CreateAndRegisterTabletPeer(
    const RaftGroupMetadataPtr& meta,
    RegisterTabletPeerMode mode,
    MarkDirtyAfterRegister mark_dirty_after_register) {
  const string& tablet_id = meta->raft_group_id();
  TabletPeerPtr tablet_peer(new tablet::TabletPeer(
      meta,
      local_peer_pb_,
      scoped_refptr<server::Clock>(server_->clock()),
      fs_manager_->uuid(),
      Bind(&TSTabletManager::ApplyChange, Unretained(this), tablet_id),
      metric_registry_,
      this,
      server_->client_future()));
  std::lock_guard lock(mutex_);
  RETURN_NOT_OK(RegisterTabletUnlocked(tablet_id, tablet_peer, mode));
  if (mark_dirty_after_register) {
    InsertOrDie(&dirty_tablets_, tablet_id, TabletReportState{next_report_seq_});
  }
  return tablet_peer;
}

Status TSTabletManager::DeleteTablet(
    const string& tablet_id,
    TabletDataState delete_type,
    tablet::ShouldAbortActiveTransactions should_abort_active_txns,
    const boost::optional<int64_t>& cas_config_opid_index_less_or_equal,
    bool hide_only,
    bool keep_data,
    boost::optional<TabletServerErrorPB::Code>* error_code) {
  TEST_PAUSE_IF_FLAG(TEST_pause_delete_tablet);

  if (delete_type != TABLET_DATA_DELETED && delete_type != TABLET_DATA_TOMBSTONED) {
    return STATUS(InvalidArgument, "DeleteTablet() requires an argument that is one of "
                                   "TABLET_DATA_DELETED or TABLET_DATA_TOMBSTONED",
                                   Substitute("Given: $0 ($1)",
                                              TabletDataState_Name(delete_type), delete_type));
  }

  TRACE("Deleting tablet $0", tablet_id);

  TabletPeerPtr tablet_peer;
  scoped_refptr<TransitionInProgressDeleter> deleter;
  {
    // Acquire the lock in exclusive mode as we'll add a entry to the
    // transition_in_progress_ map.
    std::lock_guard lock(mutex_);
    TRACE("Acquired tablet manager lock");
    RETURN_NOT_OK(CheckRunningUnlocked(error_code));

    tablet_peer = LookupTabletUnlocked(tablet_id);
    if (!tablet_peer) {
      *error_code = TabletServerErrorPB::TABLET_NOT_FOUND;
      return STATUS(NotFound, "Tablet not found", tablet_id);
    }
    // Sanity check that the tablet's deletion isn't already in progress
    Status s = StartTabletStateTransition(tablet_id, "deleting tablet", &deleter);
    if (PREDICT_FALSE(!s.ok())) {
      *error_code = TabletServerErrorPB::TABLET_NOT_RUNNING;
      return s;
    }
  }

  // If the tablet is already deleted, the CAS check isn't possible because
  // consensus and therefore the log is not available.
  TabletDataState data_state = tablet_peer->tablet_metadata()->tablet_data_state();
  bool tablet_deleted = (data_state == TABLET_DATA_DELETED || data_state == TABLET_DATA_TOMBSTONED);

  // If a tablet peer is in the FAILED state, then we need to be able to tombstone or delete this
  // tablet. If the tablet is tombstoned, then this TS can be remote bootstrapped with the same
  // tablet.
  bool tablet_failed = tablet_peer->state() == RaftGroupStatePB::FAILED;

  // They specified an "atomic" delete. Check the committed config's opid_index.
  // TODO: There's actually a race here between the check and shutdown, but
  // it's tricky to fix. We could try checking again after the shutdown and
  // restarting the tablet if the local replica committed a higher config
  // change op during that time, or potentially something else more invasive.
  if (cas_config_opid_index_less_or_equal && !tablet_deleted && !tablet_failed) {
    auto consensus_result = tablet_peer->GetConsensus();
    if (!consensus_result) {
      *error_code = TabletServerErrorPB::TABLET_NOT_RUNNING;
      return consensus_result.status();
    }
    RaftConfigPB committed_config = consensus_result.get()->CommittedConfig();
    if (committed_config.opid_index() > *cas_config_opid_index_less_or_equal) {
      *error_code = TabletServerErrorPB::CAS_FAILED;
      return STATUS(IllegalState, Substitute("Request specified cas_config_opid_index_less_or_equal"
                                             " of $0 but the committed config has opid_index of $1",
                                             *cas_config_opid_index_less_or_equal,
                                             committed_config.opid_index()));
    }
  }

  RaftGroupMetadataPtr meta = tablet_peer->tablet_metadata();
  if (hide_only) {
    meta->SetHidden(true);
    return meta->Flush();
  }
  RETURN_NOT_OK(tablet_peer->Shutdown(
      should_abort_active_txns, tablet::DisableFlushOnShutdown::kTrue));

  yb::OpId last_logged_opid = tablet_peer->GetLatestLogEntryOpId();

  if (!keep_data) {
    Status s = DeleteTabletData(meta,
                                delete_type,
                                fs_manager_->uuid(),
                                last_logged_opid,
                                this,
                                fs_manager_);
    if (PREDICT_FALSE(!s.ok())) {
      s = s.CloneAndPrepend(Substitute("Unable to delete on-disk data from tablet $0",
                                       tablet_id));
      LOG(WARNING) << s.ToString();
      tablet_peer->SetFailed(s);
      return s;
    }

    tablet_peer->status_listener()->StatusMessage("Deleted tablet blocks from disk");
  }

  // We only remove DELETED tablets from the tablet map.
  if (delete_type == TABLET_DATA_DELETED) {
    std::lock_guard lock(mutex_);
    RETURN_NOT_OK(CheckRunningUnlocked(error_code));
    CHECK_EQ(1, tablet_map_.erase(tablet_id)) << tablet_id;
    dirty_tablets_.erase(tablet_id);
  }

  // We unregister TOMBSTONED tablets in addition to DELETED tablets because they do not have
  // any more data on disk, so we shouldn't count these tablets when load balancing the disks.
  UnregisterDataWalDir(meta->table_id(),
                       tablet_id,
                       meta->data_root_dir(),
                       meta->wal_root_dir());

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

// NO_THREAD_SAFETY_ANALYSIS because this analysis does not work with unique_lock.
Status TSTabletManager::StartTabletStateTransition(
    const string& tablet_id,
    const string& reason,
    scoped_refptr<TransitionInProgressDeleter>* deleter) NO_THREAD_SAFETY_ANALYSIS {
  std::unique_lock<std::mutex> lock(transition_in_progress_mutex_);
  const auto emplace_result = transition_in_progress_.emplace(tablet_id, reason);
  if (!emplace_result.second) {
    return STATUS_FORMAT(
        AlreadyPresent, "State transition of tablet $0 already in progress: $1", tablet_id,
        *emplace_result.first);
  }
  deleter->reset(new TransitionInProgressDeleter(
      &transition_in_progress_, &transition_in_progress_mutex_, tablet_id));
  return Status::OK();
}

bool TSTabletManager::IsTabletInTransition(const TabletId& tablet_id) const {
  std::lock_guard lock(transition_in_progress_mutex_);
  return transition_in_progress_.contains(tablet_id);
}

Status TSTabletManager::OpenTabletMeta(const string& tablet_id,
                                       RaftGroupMetadataPtr* metadata) {
  LOG(INFO) << "Loading metadata for tablet " << tablet_id;
  TRACE("Loading metadata...");
  auto load_result = RaftGroupMetadata::Load(fs_manager_, tablet_id);
  RETURN_NOT_OK_PREPEND(load_result,
                        Format("Failed to load tablet metadata for tablet id $0", tablet_id));
  TRACE("Metadata loaded");
  metadata->swap(*load_result);
  return Status::OK();
}

void TSTabletManager::OpenTablet(const RaftGroupMetadataPtr& meta,
                                 const scoped_refptr<TransitionInProgressDeleter>& deleter) {
  string tablet_id = meta->raft_group_id();
  TRACE_EVENT1("tserver", "TSTabletManager::OpenTablet",
               "tablet_id", tablet_id);

  TabletPeerPtr tablet_peer = CHECK_RESULT(GetTablet(tablet_id));
  TabletPeerWeakPtr peer_weak_ptr(tablet_peer);

  tablet::TabletPtr tablet;
  scoped_refptr<Log> log;
  const string kLogPrefix = TabletLogPrefix(tablet_id);

  LOG(INFO) << kLogPrefix << "Bootstrapping tablet";
  TRACE("Bootstrapping tablet");

  std::unique_ptr<ConsensusMetadata> cmeta;
  auto s = ConsensusMetadata::Load(
      meta->fs_manager(), tablet_id, meta->fs_manager()->uuid(), &cmeta);
  if (!s.ok()) {
    LOG(ERROR) << kLogPrefix << "Tablet failed to load consensus meta data: " << s;
    tablet_peer->SetFailed(s);
    return;
  }

  consensus::ConsensusBootstrapInfo bootstrap_info;
  bool bootstrap_retryable_requests = true;

  auto bootstrap_state_manager = std::make_shared<tablet::TabletBootstrapStateManager>(
      tablet_id, fs_manager_, meta->wal_dir());
  s = bootstrap_state_manager->Init();
  if(!s.ok()) {
    LOG(ERROR) << kLogPrefix << "Tablet failed to init bootstrap state manager: " << s;
    tablet_peer->SetFailed(s);
    return;
  }

  consensus::RetryableRequests retryable_requests(
      mem_manager_->FindOrCreateOverheadMemTrackerForTablet(cmeta->tablet_id()), kLogPrefix);
  retryable_requests.SetServerClock(server_->Clock());
  retryable_requests.SetRequestTimeout(GetAtomicFlag(&FLAGS_retryable_request_timeout_secs));

  if (GetAtomicFlag(&FLAGS_enable_copy_retryable_requests_from_parent) &&
          cmeta->has_split_parent_tablet_id()) {
    auto parent_tablet_requests = GetTabletRetryableRequests(cmeta->split_parent_tablet_id());
    if (parent_tablet_requests.ok()) {
      retryable_requests.CopyFrom(*parent_tablet_requests);
      bootstrap_retryable_requests = false;
    } else {
      LOG(INFO) << kLogPrefix << "Failed to get tablet retryable requests: "
                << ResultToStatus(parent_tablet_requests);
    }
  }
  // Create metadata cache if its not created yet.
  auto metadata_cache = YBMetaDataCache();
  if (!metadata_cache) {
    metadata_cache = CreateYBMetaDataCache();
  }

  LOG_TIMING_PREFIX(INFO, kLogPrefix, "bootstrapping tablet") {
    // Read flag before CAS to avoid TSAN race conflict with GetAllFlags.
    if (GetAtomicFlag(&FLAGS_TEST_force_single_tablet_failure) &&
        CompareAndSetFlag(&FLAGS_TEST_force_single_tablet_failure,
                          true /* expected */, false /* val */)) {
      LOG(ERROR) << "Setting the state of a tablet to FAILED";
      tablet_peer->SetFailed(STATUS(InternalError, "Setting tablet to failed state for test",
                                    tablet_id));
      return;
    }

    // TODO: handle crash mid-creation of tablet? do we ever end up with a
    // partially created tablet here?
    s = tablet_peer->SetBootstrapping();
    if (!s.ok()) {
      LOG(ERROR) << kLogPrefix << "Tablet failed to set bootstrapping: " << s;
      tablet_peer->SetFailed(s);
      return;
    }
    TEST_PAUSE_IF_FLAG(TEST_pause_after_set_bootstrapping);

    tablet::TabletInitData tablet_init_data = {
        .metadata = meta,
        .client_future = server_->client_future(),
        .clock = scoped_refptr<server::Clock>(server_->clock()),
        .parent_mem_tracker = mem_manager_->tablets_overhead_mem_tracker(),
        .block_based_table_mem_tracker = mem_manager_->block_based_table_mem_tracker(),
        .metric_registry = metric_registry_,
        .log_anchor_registry = tablet_peer->log_anchor_registry(),
        .tablet_options = tablet_options_,
        .log_prefix_suffix = " P " + tablet_peer->permanent_uuid(),
        .transaction_participant_context = tablet_peer.get(),
        .local_tablet_filter = std::bind(&TSTabletManager::PreserveLocalLeadersOnly, this, _1),
        .transaction_coordinator_context = tablet_peer.get(),
        .txns_enabled = tablet::TransactionsEnabled::kTrue,
        // We are assuming we're never dealing with the system catalog tablet in TSTabletManager.
        .is_sys_catalog = tablet::IsSysCatalogTablet::kFalse,
        .snapshot_coordinator = nullptr,
        .tablet_splitter = this,
        .allowed_history_cutoff_provider =
            std::bind(&TSTabletManager::AllowedHistoryCutoff, this, _1),
        .transaction_manager_provider = [server = server_]() -> client::TransactionManager& {
          return server->TransactionManager();
        },
        .waiting_txn_registry = waiting_txn_registry_.get(),
        .wait_queue_pool = waiting_txn_pool_.get(),
        .full_compaction_pool = full_compaction_pool(),
        .admin_triggered_compaction_pool = admin_triggered_compaction_pool(),
        .post_split_compaction_added = ts_post_split_compaction_added_,
        .metadata_cache = metadata_cache,
        .get_min_xcluster_schema_version =
            std::bind(&TabletServer::GetMinXClusterSchemaVersion, server_, _1, _2),
        .messenger = server_->messenger()};
    tablet::BootstrapTabletData data = {
      .tablet_init_data = tablet_init_data,
      .listener = tablet_peer->status_listener(),
      .append_pool = append_pool(),
      .allocation_pool = allocation_pool_.get(),
      .log_sync_pool = log_sync_pool(),
      .retryable_requests = &retryable_requests,
      .bootstrap_state_manager = bootstrap_state_manager.get(),
      .bootstrap_retryable_requests = bootstrap_retryable_requests,
      .consensus_meta = cmeta.get(),
      .pre_log_rollover_callback = [peer_weak_ptr, kLogPrefix]() {
        auto peer = peer_weak_ptr.lock();
        if (peer) {
          Status s = peer->SubmitFlushBootstrapStateTask();
          LOG_IF(WARNING, !s.ok() && !s.IsNotSupported()) << kLogPrefix
              <<  "Failed to submit retryable requests task: " << s.ToString();
        }
      },
    };
    s = BootstrapTablet(data, &tablet, &log, &bootstrap_info);
    if (!s.ok()) {
      LOG(ERROR) << kLogPrefix << "Tablet failed to bootstrap: " << s;
      tablet_peer->SetFailed(s);
      return;
    }
  }

  MonoTime start(MonoTime::Now());
  LOG_TIMING_PREFIX(INFO, kLogPrefix, "starting tablet") {
    TRACE("Initializing tablet peer");
    auto s = tablet_peer->InitTabletPeer(
        tablet,
        server_->mem_tracker(),
        server_->messenger(),
        &server_->proxy_cache(),
        log,
        tablet->GetTableMetricsEntity(),
        tablet->GetTabletMetricsEntity(),
        raft_pool(),
        raft_notifications_pool(),
        tablet_prepare_pool(),
        &retryable_requests,
        bootstrap_state_manager,
        std::move(cmeta),
        multi_raft_manager_.get(),
        flush_bootstrap_state_pool());

    if (!s.ok()) {
      LOG(ERROR) << kLogPrefix << "Tablet failed to init: "
                 << s.ToString();
      tablet_peer->SetFailed(s);
      return;
    }

    // Enable flush retryable requests after the peer is fully initialized.
    tablet_peer->EnableFlushBootstrapState();

    TRACE("Starting tablet peer");
    s = tablet_peer->Start(bootstrap_info);
    if (!s.ok()) {
      LOG(ERROR) << kLogPrefix << "Tablet failed to start: "
                 << s.ToString();
      tablet_peer->SetFailed(s);
      return;
    }

    if (server_->GetCDCService()) {
      tablet_peer->log()->SetGetXClusterMinIndexToRetainFunc(
          server_->GetCDCService()->GetXClusterMinRequiredIndexFunc());
    }

    tablet_peer->RegisterMaintenanceOps(server_->maintenance_manager());
  }

  auto elapsed_ms = MonoTime::Now().GetDeltaSince(start).ToMilliseconds();
  if (elapsed_ms > FLAGS_tablet_start_warn_threshold_ms) {
    LOG(WARNING) << kLogPrefix << "Tablet startup took " << elapsed_ms << "ms";
    if (Trace::CurrentTrace()) {
      LOG(INFO) << kLogPrefix << "Trace:";
      Trace::CurrentTrace()->DumpToLogInfo(true);
    }
  }

  tablet->TriggerPostSplitCompactionIfNeeded();

  if (tablet->ShouldDisableLbMove()) {
    std::lock_guard lock(mutex_);
    tablets_blocked_from_lb_.insert(tablet->tablet_id());
    VLOG(2) << TabletLogPrefix(tablet->tablet_id())
            << " marking as maybe being compacted after split.";
  }

  tablet_metadata_validator_->ScheduleValidation(*tablet->metadata());
}

Status TSTabletManager::TriggerAdminCompaction(const TabletPtrs& tablets, bool should_wait) {
  CountDownLatch latch(tablets.size());
  std::vector<TabletId> tablet_ids;
  auto start_time = CoarseMonoClock::Now();
  uint64_t total_size = 0U;
  for (auto tablet : tablets) {
    Status status;
    if (should_wait) {
      status = tablet->TriggerAdminFullCompactionWithCallbackIfNeeded(latch.CountDownCallback());
    } else {
      status = tablet->TriggerAdminFullCompactionIfNeeded();
    }
    RETURN_NOT_OK(status);
    tablet_ids.push_back(tablet->tablet_id());
    total_size += tablet->GetCurrentVersionSstFilesSize();
  }
  VLOG(1) << yb::Format(
      "Beginning batch admin compaction for tablets $0, $1 bytes", tablet_ids, total_size);

  if (should_wait) {
    latch.Wait();
    LOG(INFO) << yb::Format(
        "Admin compaction finished for tablets $0, $1 bytes took $2 seconds", tablet_ids,
        total_size, ToSeconds(CoarseMonoClock::Now() - start_time));
  }
  return Status::OK();
}

void TSTabletManager::StartShutdown() {
  {
    std::lock_guard lock(mutex_);
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
        LOG_WITH_PREFIX(INFO) << "Shutting down tablet manager...";
        state_ = MANAGER_QUIESCING;
        break;
      }
      default: {
        LOG(FATAL) << "Invalid state: " << TSTabletManagerStatePB_Name(state_);
      }
    }
  }

  {
    std::lock_guard lock(service_registration_mutex_);
    service_consensus_change_cb_.clear();
  }

  tablets_cleaner_->Shutdown();

  verify_tablet_data_poller_->Shutdown();

  tablet_metadata_validator_->Shutdown();

  metrics_emitter_->Shutdown();

  metrics_cleaner_->Shutdown();

  waiting_txn_registry_poller_->Shutdown();

  mem_manager_->Shutdown();

  full_compaction_manager_->Shutdown();

  // Wait for all remote operations to finish.
  WaitForRemoteSessionsToEnd(TabletRemoteSessionType::kBootstrap, kDebugBootstrapString);
  WaitForRemoteSessionsToEnd(
      TabletRemoteSessionType::kSnapshotTransfer, kDebugSnapshotTransferString);

  // Shut down the bootstrap pool, so new tablets are registered after this point.
  open_tablet_pool_->Shutdown();

  // Take a snapshot of the peers list -- that way we don't have to hold
  // on to the lock while shutting them down, which might cause a lock
  // inversion. (see KUDU-308 for example).
  for (const TabletPeerPtr& peer : GetTabletPeers()) {
    if (peer->StartShutdown()) {
      shutting_down_peers_.push_back(peer);
    }
  }

  multi_raft_manager_->StartShutdown();

  if (waiting_txn_registry_) {
    waiting_txn_registry_->StartShutdown();
  }
}

void TSTabletManager::CompleteShutdown() {
  for (const TabletPeerPtr& peer : shutting_down_peers_) {
    peer->CompleteShutdown(
        tablet::DisableFlushOnShutdown(FLAGS_TEST_disable_flush_on_shutdown),
        tablet::AbortOps::kFalse);
  }

  // Shut down the apply pool.
  apply_pool_->Shutdown();

  if (raft_pool_) {
    raft_pool_->Shutdown();
  }
  if (log_sync_pool_) {
    log_sync_pool_->Shutdown();
  }
  if (flush_bootstrap_state_pool_) {
    flush_bootstrap_state_pool_->Shutdown();
  }
  if (tablet_prepare_pool_) {
    tablet_prepare_pool_->Shutdown();
  }
  if (append_pool_) {
    append_pool_->Shutdown();
  }
  if (admin_triggered_compaction_pool_) {
    admin_triggered_compaction_pool_->Shutdown();
  }
  if (superblock_flush_bg_task_) {
    superblock_flush_bg_task_->Shutdown();
  }
  if (full_compaction_pool_) {
    full_compaction_pool_->Shutdown();
  }
  if (waiting_txn_pool_) {
    waiting_txn_pool_->Shutdown();
  }

  {
    std::lock_guard l(mutex_);
    tablet_map_.clear();
    dirty_tablets_.clear();

    std::lock_guard dir_assignment_lock(dir_assignment_mutex_);
    table_data_assignment_map_.clear();
    table_wal_assignment_map_.clear();
    data_dirs_per_drive_.clear();
    wal_dirs_per_drive_.clear();

    state_ = MANAGER_SHUTDOWN;
  }

  multi_raft_manager_->CompleteShutdown();

  if (waiting_txn_registry_) {
    waiting_txn_registry_->CompleteShutdown();
  }
}

std::string TSTabletManager::LogPrefix() const {
  return "P " + fs_manager_->uuid() + ": ";
}

std::string TSTabletManager::TabletLogPrefix(const TabletId& tablet_id) const {
  return tserver::LogPrefix(tablet_id, fs_manager_->uuid());
}

bool TSTabletManager::ClosingUnlocked() const {
  return state_ == MANAGER_QUIESCING || state_ == MANAGER_SHUTDOWN;
}

Status TSTabletManager::RegisterTablet(const TabletId& tablet_id,
                                       const TabletPeerPtr& tablet_peer,
                                       RegisterTabletPeerMode mode) {
  std::lock_guard lock(mutex_);
  return RegisterTabletUnlocked(tablet_id, tablet_peer, mode);
}

Status TSTabletManager::RegisterTabletUnlocked(const TabletId& tablet_id,
                                               const TabletPeerPtr& tablet_peer,
                                               RegisterTabletPeerMode mode) {
  if (ClosingUnlocked()) {
    auto result = STATUS_FORMAT(
        ShutdownInProgress, "Unable to register tablet peer: $0: closing", tablet_id);
    LOG(WARNING) << result;
    return result;
  }

  // If we are replacing a tablet peer, we delete the existing one first.
  if (mode == REPLACEMENT_PEER && tablet_map_.erase(tablet_id) != 1) {
    auto result = STATUS_FORMAT(
        NotFound, "Unable to remove previous tablet peer $0: not registered", tablet_id);
    LOG(WARNING) << result;
    return result;
  }
  if (!InsertIfNotPresent(&tablet_map_, tablet_id, tablet_peer)) {
    auto result = STATUS_FORMAT(
        AlreadyPresent, "Unable to register tablet peer $0: already registered", tablet_id);
    LOG(WARNING) << result;
    return result;
  }

  LOG_WITH_PREFIX(INFO) << "Registered tablet " << tablet_id;

  return Status::OK();
}

template <class Key>
TabletPeerPtr TSTabletManager::DoLookupTablet(const Key& tablet_id) const {
  SharedLock<RWMutex> shared_lock(mutex_);
  return LookupTabletUnlocked(tablet_id);
}

TabletPeerPtr TSTabletManager::LookupTablet(const TabletId& tablet_id) const {
  return DoLookupTablet(tablet_id);
}

TabletPeerPtr TSTabletManager::LookupTablet(const Slice& tablet_id) const {
  return DoLookupTablet(tablet_id);
}

template <class Key>
Result<tablet::TabletPeerPtr> TSTabletManager::DoGetTablet(const Key& tablet_id) const {
  auto tablet = LookupTablet(tablet_id);
  SCHECK(tablet != nullptr, NotFound, Format("Tablet $0 not found", tablet_id));
  return tablet;
}

Result<tablet::TabletPeerPtr> TSTabletManager::GetTablet(const TabletId& tablet_id) const {
  return DoGetTablet(tablet_id);
}

Result<tablet::TabletPeerPtr> TSTabletManager::GetTablet(const Slice& tablet_id) const {
  return DoGetTablet(tablet_id);
}

Result<consensus::RetryableRequests> TSTabletManager::GetTabletRetryableRequests(
    const TabletId& tablet_id) const {
  return VERIFY_RESULT(GetTablet(tablet_id))->GetRetryableRequests();
}

template <class Key>
TabletPeerPtr TSTabletManager::LookupTabletUnlocked(const Key& tablet_id) const {
  auto it = tablet_map_.find(std::string_view(tablet_id));
  return it != tablet_map_.end() ? it->second : nullptr;
}

template <class Key>
Result<TabletPeerPtr> TSTabletManager::DoGetServingTablet(const Key& tablet_id) const {
  auto tablet = VERIFY_RESULT(GetTablet(tablet_id));
  RETURN_NOT_OK(CheckCanServeTabletData(*tablet->tablet_metadata()));
  return tablet;
}

Result<TabletPeerPtr> TSTabletManager::GetServingTablet(const TabletId& tablet_id) const {
  return DoGetServingTablet(tablet_id);
}

Result<TabletPeerPtr> TSTabletManager::GetServingTablet(const Slice& tablet_id) const {
  return DoGetServingTablet(tablet_id);
}

const NodeInstancePB& TSTabletManager::NodeInstance() const {
  return server_->instance_pb();
}

Status TSTabletManager::GetRegistration(ServerRegistrationPB* reg) const {
  return server_->GetRegistration(reg, server::RpcOnly::kTrue);
}

TSTabletManager::TabletPeers TSTabletManager::GetTabletPeers(TabletPtrs* tablet_ptrs) const {
  SharedLock<RWMutex> shared_lock(mutex_);
  TabletPeers peers;
  GetTabletPeersUnlocked(&peers);
  if (tablet_ptrs) {
    for (const auto& peer : peers) {
      if (!peer) continue;
      auto tablet_ptr = peer->shared_tablet();
      if (tablet_ptr) {
        tablet_ptrs->push_back(tablet_ptr);
      }
    }
  }
  return peers;
}

TSTabletManager::TabletPeers TSTabletManager::GetTabletPeersWithTableId(
    const TableId& table_id) const {
  const auto peers = GetTabletPeers();
  TabletPeers filtered_peers;
  for (const auto& peer : peers) {
    if (peer && peer->tablet_metadata()->table_id() == table_id) {
      filtered_peers.push_back(peer);
    }
  }
  return filtered_peers;
}

void TSTabletManager::GetTabletPeersUnlocked(TabletPeers* tablet_peers) const {
  DCHECK(tablet_peers != nullptr);
  // See AppendKeysFromMap for why this is done.
  if (tablet_peers->empty()) {
    tablet_peers->reserve(tablet_map_.size());
  }
  for (const auto& entry : tablet_map_) {
    if (entry.second != nullptr) {
      tablet_peers->push_back(entry.second);
    }
  }
}

TSTabletManager::TabletPeers TSTabletManager::GetStatusTabletPeers() {
  // Currently, there is no better way to get all the status tablets hosted at a TabletServer.
  // Hence we iterate though all the tablets and consider only the ones that have a non-null
  // transaction coordinator.
  TabletPeers status_tablet_peers;
  SharedLock<RWMutex> shared_lock(mutex_);

  for (const auto& entry : tablet_map_) {
    // shared_tablet() might return nullptr during initialization of the tablet_peer.
    const auto& tablet_ptr = entry.second == nullptr ? nullptr : entry.second->shared_tablet();
    if (tablet_ptr && tablet_ptr->transaction_coordinator()) {
      status_tablet_peers.push_back(entry.second);
    }
  }
  return status_tablet_peers;
}

void TSTabletManager::PreserveLocalLeadersOnly(std::vector<const TabletId*>* tablet_ids) const {
  SharedLock<decltype(mutex_)> shared_lock(mutex_);
  auto filter = [this](const TabletId* id) REQUIRES_SHARED(mutex_) {
    auto it = tablet_map_.find(*id);
    if (it == tablet_map_.end()) {
      return true;
    }
    auto leader_status = it->second->LeaderStatus();
    return leader_status != consensus::LeaderStatus::LEADER_AND_READY;
  };
  tablet_ids->erase(std::remove_if(tablet_ids->begin(), tablet_ids->end(), filter),
                    tablet_ids->end());
}

void TSTabletManager::ApplyChange(const string& tablet_id,
                                  shared_ptr<consensus::StateChangeContext> context) {
  WARN_NOT_OK(
      apply_pool_->SubmitFunc(
          std::bind(&TSTabletManager::MarkTabletDirty, this, tablet_id, context)),
      "Unable to run MarkDirty callback")
}

void TSTabletManager::MarkTabletDirty(const TabletId& tablet_id,
                                      std::shared_ptr<consensus::StateChangeContext> context) {
  {
    std::lock_guard lock(mutex_);
    MarkDirtyUnlocked(tablet_id, context);
  }
  NotifyConfigChangeToStatefulServices(tablet_id);
}

void TSTabletManager::NotifyConfigChangeToStatefulServices(const TabletId& tablet_id) {
  auto tablet_peer = GetServingTablet(tablet_id);
  if (!tablet_peer.ok()) {
    VLOG_WITH_FUNC(1) << "Received notification of tablet config change "
                      << "but tablet peer is not ready. Tablet ID: " << tablet_id
                      << " Error: " << tablet_peer.status();
    return;
  }

  const auto service_list = tablet_peer.get()->tablet_metadata()->GetHostedServiceList();
  for (auto& service_kind : service_list) {
    SharedLock lock(service_registration_mutex_);
    if (service_consensus_change_cb_.contains(service_kind)) {
      service_consensus_change_cb_[service_kind].Run(tablet_peer.get());
    }
  }
}

void TSTabletManager::MarkTabletBeingRemoteBootstrapped(
    const TabletId& tablet_id, const TableId& table_id) {
  std::lock_guard lock(mutex_);
  tablets_being_remote_bootstrapped_.insert(tablet_id);
  tablets_being_remote_bootstrapped_per_table_[table_id].insert(tablet_id);
  MaybeDoChecksForTests(table_id);
  LOG(INFO) << "Concurrent remote bootstrap sessions: "
            << tablets_being_remote_bootstrapped_.size()
            << ", Concurrent remote bootstrap sessions for table " << table_id
            << ": " << tablets_being_remote_bootstrapped_per_table_[table_id].size();
}

void TSTabletManager::UnmarkTabletBeingRemoteBootstrapped(
    const TabletId& tablet_id, const TableId& table_id) {
  std::lock_guard lock(mutex_);
  tablets_being_remote_bootstrapped_.erase(tablet_id);
  tablets_being_remote_bootstrapped_per_table_[table_id].erase(tablet_id);
}

size_t TSTabletManager::TEST_GetNumDirtyTablets() const {
  SharedLock<RWMutex> lock(mutex_);
  return dirty_tablets_.size();
}

Status TSTabletManager::GetNumTabletsPendingBootstrap(
    IsTabletServerReadyResponsePB* resp) const {
  if (state() != MANAGER_RUNNING) {
    resp->set_num_tablets_not_running(INT_MAX);
    resp->set_total_tablets(INT_MAX);
    return Status::OK();
  }

  SharedLock<RWMutex> shared_lock(mutex_);
  int num_pending = 0;
  int total_tablets = 0;
  for (const auto& entry : tablet_map_) {
    RaftGroupStatePB state = entry.second->state();
    TabletDataState data_state = entry.second->data_state();
    // Do not count tablets that will never get to RUNNING state.
    if (!CanServeTabletData(data_state)) {
      continue;
    }
    bool not_started_or_bootstrap = state == NOT_STARTED || state == BOOTSTRAPPING;
    if (not_started_or_bootstrap || state == RUNNING) {
      total_tablets++;
    }
    if (not_started_or_bootstrap) {
      num_pending++;
    }
  }

  LOG(INFO) << num_pending << " tablets pending bootstrap out of " << total_tablets;
  resp->set_num_tablets_not_running(num_pending);
  resp->set_total_tablets(total_tablets);

  return Status::OK();
}

int TSTabletManager::GetNumLiveTablets() const {
  int count = 0;
  SharedLock<RWMutex> lock(mutex_);
  for (const auto& entry : tablet_map_) {
    RaftGroupStatePB state = entry.second->state();
    if (state == BOOTSTRAPPING ||
        state == RUNNING) {
      count++;
    }
  }
  return count;
}

int TSTabletManager::GetLeaderCount() const {
  int count = 0;
  SharedLock<RWMutex> lock(mutex_);
  for (const auto& entry : tablet_map_) {
    consensus::LeaderStatus leader_status = entry.second->LeaderStatus(/* allow_stale =*/ true);
    if (leader_status != consensus::LeaderStatus::NOT_LEADER) {
      count++;
    }
  }
  return count;
}

void TSTabletManager::MarkDirtyUnlocked(const TabletId& tablet_id,
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
  VLOG(2) << TabletLogPrefix(tablet_id)
          << "Marking dirty. Reason: " << AsString(context)
          << ". Will report this tablet to the Master in the next heartbeat "
          << "as part of report #" << next_report_seq_;
  server_->heartbeater()->TriggerASAP();
}

void TSTabletManager::InitLocalRaftPeerPB() {
  DCHECK_EQ(state(), MANAGER_INITIALIZING);
  local_peer_pb_.set_permanent_uuid(fs_manager_->uuid());
  ServerRegistrationPB reg;
  CHECK_OK(server_->GetRegistration(&reg, server::RpcOnly::kTrue));
  TakeRegistration(&reg, &local_peer_pb_);
}

void TSTabletManager::CreateReportedTabletPB(const TabletPeerPtr& tablet_peer,
                                             ReportedTabletPB* reported_tablet) {
  reported_tablet->set_tablet_id(tablet_peer->tablet_id());
  reported_tablet->set_state(tablet_peer->state());
  reported_tablet->set_tablet_data_state(tablet_peer->tablet_metadata()->tablet_data_state());
  if (tablet_peer->state() == tablet::FAILED) {
    AppStatusPB* error_status = reported_tablet->mutable_error();
    StatusToPB(tablet_peer->error(), error_status);
  }
  reported_tablet->set_schema_version(tablet_peer->tablet_metadata()->schema_version());

  tablet_peer->tablet_metadata()->GetTableIdToSchemaVersionMap(
      reported_tablet->mutable_table_to_version());

  {
    auto tablet_ptr = tablet_peer->shared_tablet();
    if (tablet_ptr != nullptr) {
      reported_tablet->set_should_disable_lb_move(tablet_ptr->ShouldDisableLbMove());
    }
  }
  reported_tablet->set_fs_data_dir(tablet_peer->tablet_metadata()->data_root_dir());

  // We cannot get consensus state information unless the TabletPeer is running.
  auto consensus_result = tablet_peer->GetConsensus();
  if (consensus_result) {
    *reported_tablet->mutable_committed_consensus_state() =
        consensus_result.get()->ConsensusState(consensus::CONSENSUS_CONFIG_COMMITTED);
  }

  // Set the hide status of the tablet.
  reported_tablet->set_is_hidden(tablet_peer->tablet_metadata()->hidden());
}

void TSTabletManager::GenerateTabletReport(TabletReportPB* report, bool include_bootstrap) {
  report->Clear();
  // Creating the tablet report can be slow in the case that it is in the
  // middle of flushing its consensus metadata. We don't want to hold
  // lock_ for too long, even in read mode, since it can cause other readers
  // to block if there is a waiting writer (see KUDU-2193). So, we just make
  // a local copy of the set of replicas.
  vector<std::shared_ptr<TabletPeer>> to_report;
  TabletIdSet tablet_ids;
  size_t dirty_count, report_limit;
  {
    std::lock_guard write_lock(mutex_);
    uint32_t cur_report_seq = next_report_seq_++;
    report->set_sequence_number(cur_report_seq);

    TabletIdSet::iterator i = tablets_blocked_from_lb_.begin();
    while (i != tablets_blocked_from_lb_.end()) {
      TabletPeerPtr* tablet_peer = FindOrNull(tablet_map_, *i);
      if (tablet_peer) {
          const auto tablet = (*tablet_peer)->shared_tablet();
          // If tablet is null, one of two things may be true:
          // 1. TabletPeer::InitTabletPeer was not called yet
          //
          // Skip and keep tablet in tablets_blocked_from_lb_ till call InitTabletPeer.
          //
          // 2. TabletPeer::CompleteShutdown was called
          //
          // Tablet will be removed from tablets_blocked_from_lb_ with next GenerateTabletReport
          // since tablet_peer will be removed from tablet_map_
          if (tablet == nullptr) {
            ++i;
            continue;
          }
          const std::string& tablet_id = tablet->tablet_id();
          if (!tablet->ShouldDisableLbMove()) {
            i = tablets_blocked_from_lb_.erase(i);
            VLOG(1) << "Tablet " << tablet_id << " is no longer blocked from load-balancing.";
            InsertOrUpdate(&dirty_tablets_, tablet_id, TabletReportState{cur_report_seq});
          } else {
            ++i;
          }
      } else {
          VLOG(1) << "Tablet " << *i
                  << " was marked as blocked from load balancing but was not found";
          i = tablets_blocked_from_lb_.erase(i);
      }
    }

    if (include_bootstrap) {
      for (auto const& tablet_id : tablets_being_remote_bootstrapped_) {
        VLOG(1) << "Tablet " << tablet_id << " being remote bootstrapped and marked for report";
        InsertOrUpdate(&dirty_tablets_, tablet_id, TabletReportState{cur_report_seq});
      }
    }
    for (const DirtyMap::value_type& dirty_entry : dirty_tablets_) {
      const TabletId& tablet_id = dirty_entry.first;
      tablet_ids.insert(tablet_id);
    }

    for (auto const& tablet_id : tablet_ids) {
      TabletPeerPtr* tablet_peer = FindOrNull(tablet_map_, tablet_id);
      if (tablet_peer) {
        // Dirty entry, report on it.
        to_report.push_back(*tablet_peer);
      } else {
        // Tell the Master that this tablet was removed from the TServer side.
        report->add_removed_tablet_ids(tablet_id);
        // Don't count this as a 'dirty_tablet_' because the Master may not have it either.
        dirty_tablets_.erase(tablet_id);
      }
    }
    dirty_count = dirty_tablets_.size();
    report_limit = report_limit_;
  }
  for (const auto& replica : to_report) {
    CreateReportedTabletPB(replica, report->add_updated_tablets());
    // Enforce a max tablet limit on reported tablets.
    if (implicit_cast<size_t>(report->updated_tablets_size()) >= report_limit) break;
  }
  report->set_remaining_tablet_count(
      narrow_cast<int>(dirty_count - report->updated_tablets_size()));
}

void TSTabletManager::StartFullTabletReport(TabletReportPB* report) {
  report->Clear();
  // Creating the tablet report can be slow in the case that it is in the
  // middle of flushing its consensus metadata. We don't want to hold
  // lock_ for too long, even in read mode, since it can cause other readers
  // to block if there is a waiting writer (see KUDU-2193). So, we just make
  // a local copy of the set of replicas.
  vector<std::shared_ptr<TabletPeer>> to_report;
  size_t dirty_count, report_limit;
  {
    std::lock_guard write_lock(mutex_);
    uint32_t cur_report_seq = next_report_seq_++;
    report->set_sequence_number(cur_report_seq);
    GetTabletPeersUnlocked(&to_report);
    // Mark all tablets as dirty, to be cleaned when reading the heartbeat response.
    for (const auto& peer : to_report) {
      InsertOrUpdate(&dirty_tablets_, peer->tablet_id(), TabletReportState{cur_report_seq});
    }
    dirty_count = dirty_tablets_.size();
    report_limit = report_limit_;
  }
  for (const auto& replica : to_report) {
    CreateReportedTabletPB(replica, report->add_updated_tablets());
    // Enforce a max tablet limit on reported tablets.
    if (implicit_cast<size_t>(report->updated_tablets_size()) >= report_limit) break;
  }
  report->set_remaining_tablet_count(
      narrow_cast<int32_t>(dirty_count - report->updated_tablets_size()));
}

void TSTabletManager::MarkTabletReportAcknowledged(uint32_t acked_seq,
                                                   const TabletReportUpdatesPB& updates,
                                                   bool dirty_check) {
  std::lock_guard l(mutex_);

  CHECK_LT(acked_seq, next_report_seq_);

  // Clear the "dirty" state for any tablets processed in this report.
  for (auto const & tablet : updates.tablets()) {
    auto it = dirty_tablets_.find(tablet.tablet_id());
    if (it != dirty_tablets_.end()) {
      const TabletReportState& state = it->second;
      if (state.change_seq <= acked_seq) {
        // This entry has not changed since this tablet report, we no longer need to track it
        // as dirty. Next modification will be re-added with a higher sequence number.
        dirty_tablets_.erase(it);
      }
    }
  }
#ifndef NDEBUG
  // Verify dirty_tablets_ always processes all tablet changes.
  if (dirty_check) {
    for (auto const & d : dirty_tablets_) {
      if (d.second.change_seq <= acked_seq) {
        LOG(DFATAL) << "Dirty Tablet should have been reported but wasn't: "
                    << d.first << "@" << d.second.change_seq << " <= " << acked_seq;
      }
    }
  }
#endif
}

void TSTabletManager::HandleNonReadyTabletOnStartup(
    const RaftGroupMetadataPtr& meta,
    const scoped_refptr<TransitionInProgressDeleter>& deleter) {
  Status s = DoHandleNonReadyTabletOnStartup(meta.get(), deleter);
  LOG_IF(FATAL, !s.ok())
      << TabletLogPrefix(meta->raft_group_id())
      << " Failed to handle non ready tablet on tserver startup: " << s;
}

Status TSTabletManager::DoHandleNonReadyTabletOnStartup(
    const RaftGroupMetadataPtr& meta,
    const scoped_refptr<TransitionInProgressDeleter>& deleter) {
  const string& tablet_id = meta->raft_group_id();
  TabletDataState data_state = meta->tablet_data_state();
  CHECK(data_state == TABLET_DATA_DELETED ||
        data_state == TABLET_DATA_TOMBSTONED ||
        data_state == TABLET_DATA_COPYING ||
        data_state == TABLET_DATA_INIT_STARTED)
      << "Unexpected TabletDataState in tablet " << tablet_id << ": "
      << TabletDataState_Name(data_state) << " (" << data_state << ")";

  if (data_state == TABLET_DATA_COPYING) {
    // We tombstone tablets that failed to remotely bootstrap.
    data_state = TABLET_DATA_TOMBSTONED;
  }

  if (data_state == TABLET_DATA_INIT_STARTED) {
    // We delete tablets that failed to completely initialize after a split.
    // TODO(tsplit): https://github.com/yugabyte/yugabyte-db/issues/8013
    data_state = TABLET_DATA_DELETED;
  }

  const string kLogPrefix = TabletLogPrefix(tablet_id);

  // If the tablet is already fully tombstoned with no remaining data or WAL,
  // then no need to roll anything forward.
  bool skip_deletion = meta->IsTombstonedWithNoRocksDBData() &&
                       !Log::HasOnDiskData(meta->fs_manager(), meta->wal_dir());

  LOG_IF(WARNING, !skip_deletion)
      << kLogPrefix << "Tablet Manager startup: Rolling forward tablet deletion "
      << "of type " << TabletDataState_Name(data_state);

  if (!skip_deletion) {
    // Passing no OpId will retain the last_logged_opid that was previously in the metadata.
    RETURN_NOT_OK(DeleteTabletData(meta, data_state, fs_manager_->uuid(), yb::OpId()));
  }

  // Register TOMBSTONED tablets and mark dirty so that they get reported to the Master,
  // which allows us to permanently delete replica tombstones when a table gets deleted.
  if (data_state == TABLET_DATA_TOMBSTONED) {
    RETURN_NOT_OK(CreateAndRegisterTabletPeer(meta, NEW_PEER,
        MarkDirtyAfterRegister::kTrue));
  }

  return Status::OK();
}

void TSTabletManager::GetAndRegisterDataAndWalDir(FsManager* fs_manager,
                                                  const string& table_id,
                                                  const string& tablet_id,
                                                  string* data_root_dir,
                                                  string* wal_root_dir) {
  // Skip sys catalog table and kudu table from modifying the map.
  if (table_id == master::kSysCatalogTableId) {
    return;
  }
  LOG(INFO) << "Get and update data/wal directory assignment map for table: " \
            << table_id << " and tablet " << tablet_id;
  std::lock_guard dir_assignment_lock(dir_assignment_mutex_);
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
  // Break ties by choosing the data directory with the least number of tablets overall.
  table_data_assignment_iter = table_data_assignment_map_.find(table_id);
  auto data_assignment_value_map = table_data_assignment_iter->second;
  string min_dir;
  uint64_t min_dir_count = kuint64max;
  uint64_t min_tablet_counts_across_tables = kuint64max;
  for (auto& [dir, tablets_in_dir] : data_assignment_value_map) {
    if (min_dir_count > tablets_in_dir.size() ||
        (min_dir_count == tablets_in_dir.size() &&
         min_tablet_counts_across_tables > data_dirs_per_drive_[dir])) {
      min_dir = dir;
      min_dir_count = tablets_in_dir.size();
      min_tablet_counts_across_tables = data_dirs_per_drive_[min_dir];
    }
  }
  *data_root_dir = min_dir;
  // Increment the count for min_dir.
  auto data_assignment_value_iter = table_data_assignment_map_[table_id].find(min_dir);
  data_assignment_value_iter->second.insert(tablet_id);
  data_dirs_per_drive_[min_dir] += 1;

  // Find the wal directory with the least count of tablets for this table.
  // Break ties by choosing the wal directory with the least number of tablets overall.
  min_dir = "";
  min_dir_count = kuint64max;
  min_tablet_counts_across_tables = kuint64max;
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
  for (auto& [dir, tablets_in_dir] : wal_assignment_value_map) {
    if (min_dir_count > tablets_in_dir.size() ||
        (min_dir_count == tablets_in_dir.size() &&
         min_tablet_counts_across_tables > wal_dirs_per_drive_[dir])) {
      min_dir = dir;
      min_dir_count = tablets_in_dir.size();
      min_tablet_counts_across_tables = wal_dirs_per_drive_[min_dir];
    }
  }
  *wal_root_dir = min_dir;
  auto wal_assignment_value_iter = table_wal_assignment_map_[table_id].find(min_dir);
  wal_assignment_value_iter->second.insert(tablet_id);
  wal_dirs_per_drive_[min_dir] += 1;
}

void TSTabletManager::RegisterDataAndWalDir(FsManager* fs_manager,
                                            const string& table_id,
                                            const string& tablet_id,
                                            const string& data_root_dir,
                                            const string& wal_root_dir) {
  // Skip sys catalog table from modifying the map.
  if (table_id == master::kSysCatalogTableId) {
    return;
  }
  LOG(INFO) << "Update data/wal directory assignment map for table: "
            << table_id << " and tablet " << tablet_id;
  std::lock_guard dir_assignment_lock(dir_assignment_mutex_);
  // Initialize the map if the directory mapping does not exist.
  auto data_root_dirs = fs_manager->GetDataRootDirs();
  CHECK(!data_root_dirs.empty()) << "No data root directories found";
  auto table_data_assignment_iter = table_data_assignment_map_.find(table_id);
  if (table_data_assignment_iter == table_data_assignment_map_.end()) {
    for (const string& data_root : data_root_dirs) {
      unordered_set<string> tablet_id_set;
      table_data_assignment_map_[table_id][data_root] = tablet_id_set;
    }
  }
  // Increment the count for data_root_dir.
  table_data_assignment_iter = table_data_assignment_map_.find(table_id);
  auto data_assignment_value_map = table_data_assignment_iter->second;
  auto data_assignment_value_iter = table_data_assignment_map_[table_id].find(data_root_dir);
  if (data_assignment_value_iter == table_data_assignment_map_[table_id].end()) {
    LOG(DFATAL) << "Unexpected data dir: " << data_root_dir;
    unordered_set<string> tablet_id_set;
    tablet_id_set.insert(tablet_id);
    table_data_assignment_map_[table_id][data_root_dir] = tablet_id_set;
  } else {
    data_assignment_value_iter->second.insert(tablet_id);
  }
  // Increment the total tablet assignments for the drive
  data_dirs_per_drive_[data_root_dir] += 1;

  auto wal_root_dirs = fs_manager->GetWalRootDirs();
  CHECK(!wal_root_dirs.empty()) << "No wal root directories found";
  auto table_wal_assignment_iter = table_wal_assignment_map_.find(table_id);
  if (table_wal_assignment_iter == table_wal_assignment_map_.end()) {
    for (const string& wal_root : wal_root_dirs) {
      unordered_set<string> tablet_id_set;
      table_wal_assignment_map_[table_id][wal_root] = tablet_id_set;
    }
  }
  // Increment the count for wal_root_dir.
  table_wal_assignment_map_[table_id][wal_root_dir].insert(tablet_id);
  table_wal_assignment_iter = table_wal_assignment_map_.find(table_id);
  auto wal_assignment_value_map = table_wal_assignment_iter->second;
  auto wal_assignment_value_iter = table_wal_assignment_map_[table_id].find(wal_root_dir);
  if (wal_assignment_value_iter == table_wal_assignment_map_[table_id].end()) {
    LOG(DFATAL) << "Unexpected wal dir: " << wal_root_dir;
    unordered_set<string> tablet_id_set;
    tablet_id_set.insert(tablet_id);
    table_wal_assignment_map_[table_id][wal_root_dir] = tablet_id_set;
  } else {
    wal_assignment_value_iter->second.insert(tablet_id);
  }
  // Increment the total tablet assignments for the drive
  wal_dirs_per_drive_[wal_root_dir] += 1;
}

TSTabletManager::TableDiskAssignmentMap* TSTabletManager::GetTableDiskAssignmentMapUnlocked(
    TabletDirType dir_type) {
  switch (dir_type) {
    case TabletDirType::kData:
      return &table_data_assignment_map_;
    case TabletDirType::kWal:
      return &table_wal_assignment_map_;
  }
  FATAL_INVALID_ENUM_VALUE(TabletDirType, dir_type);
}

Result<const std::string&> TSTabletManager::GetAssignedRootDirForTablet(
    TabletDirType dir_type, const TableId& table_id, const TabletId& tablet_id) {
  std::lock_guard dir_assignment_lock(dir_assignment_mutex_);

  TableDiskAssignmentMap* table_assignment_map = GetTableDiskAssignmentMapUnlocked(dir_type);
  auto tablets_by_root_dir = table_assignment_map->find(table_id);
  if (tablets_by_root_dir == table_assignment_map->end()) {
    return STATUS_FORMAT(
        IllegalState, "Table ID $0 is not in $1 table assignment map", table_id, dir_type);
  }
  for (auto& data_dir_and_tablets : tablets_by_root_dir->second) {
    if (data_dir_and_tablets.second.count(tablet_id) > 0) {
      return data_dir_and_tablets.first;
    }
  }
  return STATUS_FORMAT(
      IllegalState, "Tablet ID $0 is not found in $1 assignment map for table $2", tablet_id,
      dir_type, table_id);
}

void TSTabletManager::UnregisterDataWalDir(const string& table_id,
                                           const string& tablet_id,
                                           const string& data_root_dir,
                                           const string& wal_root_dir) {
  // Skip sys catalog table from modifying the map.
  if (table_id == master::kSysCatalogTableId) {
    return;
  }
  LOG(INFO) << "Unregister data/wal directory assignment map for table: "
            << table_id << " and tablet " << tablet_id;
  std::lock_guard lock(dir_assignment_mutex_);
  auto table_data_assignment_iter = table_data_assignment_map_.find(table_id);
  if (table_data_assignment_iter == table_data_assignment_map_.end()) {
    // It is possible that we can't find an assignment for the table if the operations followed in
    // this order:
    // 1. The only tablet for a table gets tombstoned, and UnregisterDataWalDir removes it from
    //    the maps.
    // 2. TSTabletManager gets restarted (so the maps are cleared).
    // 3. During TsTabletManager initialization, the tombstoned TABLET won't get registered,
    //    so if a DeleteTablet request with type DELETED gets sent, UnregisterDataWalDir won't
    //    find the table.

    // Check that both maps should be consistent.
    DCHECK(table_wal_assignment_map_.find(table_id) == table_wal_assignment_map_.end());
  }
  if (table_data_assignment_iter != table_data_assignment_map_.end()) {
    auto data_assignment_value_iter = table_data_assignment_map_[table_id].find(data_root_dir);
    DCHECK(data_assignment_value_iter != table_data_assignment_map_[table_id].end())
      << "No data directory index found for table: " << table_id;
    if (data_assignment_value_iter != table_data_assignment_map_[table_id].end()) {
      data_assignment_value_iter->second.erase(tablet_id);
      data_dirs_per_drive_[data_root_dir] -= 1;
    } else {
      LOG(WARNING) << "Tablet " << tablet_id << " not in the set for data directory "
                   << data_root_dir << "for table " << table_id;
    }
  }
  auto table_wal_assignment_iter = table_wal_assignment_map_.find(table_id);
  if (table_wal_assignment_iter != table_wal_assignment_map_.end()) {
    auto wal_assignment_value_iter = table_wal_assignment_map_[table_id].find(wal_root_dir);
    DCHECK(wal_assignment_value_iter != table_wal_assignment_map_[table_id].end())
      << "No wal directory index found for table: " << table_id;
    if (wal_assignment_value_iter != table_wal_assignment_map_[table_id].end()) {
      wal_assignment_value_iter->second.erase(tablet_id);
      wal_dirs_per_drive_[data_root_dir] -= 1;
    } else {
      LOG(WARNING) << "Tablet " << tablet_id << " not in the set for wal directory "
                   << wal_root_dir << "for table " << table_id;
    }
  }
}

client::YBClient& TSTabletManager::client() {
  return *client_future().get();
}

const std::shared_future<client::YBClient*>& TSTabletManager::client_future() {
  return server_->client_future();
}

void TSTabletManager::MaybeDoChecksForTests(const TableId& table_id) {
  // First check that the global RBS limits are respected if the flag is non-zero.
  if (PREDICT_FALSE(FLAGS_TEST_crash_if_remote_bootstrap_sessions_greater_than > 0) &&
      tablets_being_remote_bootstrapped_.size() >
          FLAGS_TEST_crash_if_remote_bootstrap_sessions_greater_than) {
    string tablets;
    // The purpose of limiting the number of remote bootstraps is to cap how much
    // network bandwidth all the RBS sessions use.
    // When we finish transferring the files, we wait until the role of the new peer
    // has been changed from PRE_VOTER to VOTER before we remove the tablet_id
    // from tablets_being_remote_bootstrapped_. Since it's possible to be here
    // because a few tablets are already open, and in the RUNNING state, but still
    // in the tablets_being_remote_bootstrapped_ list, we check the state of each
    // tablet before deciding if the load balancer has violated the concurrent RBS limit.
    size_t count = 0;
    for (const auto& tablet_id : tablets_being_remote_bootstrapped_) {
      TabletPeerPtr* tablet_peer = FindOrNull(tablet_map_, tablet_id);
      if (tablet_peer && (*tablet_peer)->state() == RaftGroupStatePB::RUNNING) {
        continue;
      }
      if (!tablets.empty()) {
        tablets += ", ";
      }
      tablets += tablet_id;
      count++;
    }
    if (count > FLAGS_TEST_crash_if_remote_bootstrap_sessions_greater_than) {
      LOG(FATAL) << "Exceeded the specified maximum number of concurrent remote bootstrap sessions."
                 << " Specified: " << FLAGS_TEST_crash_if_remote_bootstrap_sessions_greater_than
                 << ", number concurrent remote bootstrap sessions: "
                 << tablets_being_remote_bootstrapped_.size() << ", for tablets: " << tablets;
    }
  }

  // Check that the per-table RBS limits are respected if the flag is non-zero.
  if (PREDICT_FALSE(FLAGS_TEST_crash_if_remote_bootstrap_sessions_per_table_greater_than > 0) &&
      tablets_being_remote_bootstrapped_per_table_[table_id].size() >
          FLAGS_TEST_crash_if_remote_bootstrap_sessions_per_table_greater_than) {
    string tablets;
    size_t count = 0;
    for (const auto& tablet_id : tablets_being_remote_bootstrapped_per_table_[table_id]) {
      TabletPeerPtr* tablet_peer = FindOrNull(tablet_map_, tablet_id);
      if (tablet_peer && (*tablet_peer)->state() == RaftGroupStatePB::RUNNING) {
        continue;
      }
      if (!tablets.empty()) {
        tablets += ", ";
      }
      tablets += tablet_id;
      count++;
    }
    if (count > FLAGS_TEST_crash_if_remote_bootstrap_sessions_per_table_greater_than) {
      LOG(FATAL) << "Exceeded the specified maximum number of concurrent remote bootstrap "
                 << "sessions per table. Specified: "
                 << FLAGS_TEST_crash_if_remote_bootstrap_sessions_per_table_greater_than
                 << ", number of concurrent remote bootstrap sessions for table " << table_id
                 << ": " << tablets_being_remote_bootstrapped_per_table_[table_id].size()
                 << ", for tablets: " << tablets;
    }
  }
}

Status TSTabletManager::UpdateSnapshotsInfo(const master::TSSnapshotsInfoPB& info) {
  bool restorations_updated;
  RestorationCompleteTimeMap restoration_complete_time;
  {
    std::lock_guard lock(snapshot_schedule_allowed_history_cutoff_mutex_);
    ++snapshot_schedules_version_;
    snapshot_schedule_allowed_history_cutoff_.clear();
    for (const auto& schedule : info.schedules()) {
      auto schedule_id = VERIFY_RESULT(FullyDecodeSnapshotScheduleId(schedule.id()));
      snapshot_schedule_allowed_history_cutoff_.emplace(
          schedule_id, HybridTime::FromPB(schedule.last_snapshot_hybrid_time()));
      missing_snapshot_schedules_.erase(schedule_id);
    }
    HybridTime restorations_update_ht(info.last_restorations_update_ht());
    restorations_updated = restorations_update_ht != last_restorations_update_ht_;
    if (restorations_updated) {
      last_restorations_update_ht_ = restorations_update_ht;
      for (const auto& entry : info.restorations()) {
        auto id = VERIFY_RESULT(FullyDecodeTxnSnapshotRestorationId(entry.id()));
        auto complete_time = HybridTime::FromPB(entry.complete_time_ht());
        restoration_complete_time.emplace(id, complete_time);
      }
    }
  }
  if (!restorations_updated) {
    return Status::OK();
  }
  std::vector<tablet::TabletPtr> tablets;
  {
    SharedLock<RWMutex> shared_lock(mutex_);
    tablets.reserve(tablet_map_.size());
    for (const auto& entry : tablet_map_) {
      auto tablet = entry.second->shared_tablet();
      if (tablet) {
        tablets.push_back(tablet);
      }
    }
  }
  for (const auto& tablet : tablets) {
    RETURN_NOT_OK(tablet->CheckRestorations(restoration_complete_time));
  }
  return Status::OK();
}

docdb::HistoryCutoff TSTabletManager::AllowedHistoryCutoff(
    tablet::RaftGroupMetadata* metadata) {
  HybridTime result = HybridTime::kMax;
  // CDC SDK safe time
  if (metadata->cdc_sdk_safe_time() != HybridTime::kInvalid) {
    VLOG(1) << "CDC SDK historycutoff: " << metadata->cdc_sdk_safe_time()
            << " for tablet: " << metadata->raft_group_id();
    result = metadata->cdc_sdk_safe_time();
  }

  auto xcluster_safe_time_result =
      server_->GetXClusterContext().GetSafeTime(metadata->namespace_id());
  if (!xcluster_safe_time_result) {
    VLOG(1) << "XCluster GetSafeTime call failed with " << xcluster_safe_time_result.status()
            << " for namespace: " << metadata->namespace_id();
    // GetSafeTime call fails when special safetime value is set for a namespace -- this can happen
    // when we have new replication setup and safe time is not yet computed. In this case, we return
    // HybridTime::kMin to stop compaction from deleting any of the existing versions of documents.
    return { HybridTime::kInvalid, HybridTime::kMin };
  }
  auto opt_xcluster_safe_time = *xcluster_safe_time_result;
  if (opt_xcluster_safe_time) {
    VLOG(1) << "XCluster historycutoff: " << *opt_xcluster_safe_time
            << " for tablet: " << metadata->raft_group_id();
    result.MakeAtMost(*opt_xcluster_safe_time);
  }

  auto schedules = metadata->SnapshotSchedules();
  if (!schedules.empty()) {
    std::vector<SnapshotScheduleId> schedules_to_remove;
    auto se = ScopeExit([&schedules_to_remove, metadata]() {
      if (schedules_to_remove.empty()) {
        return;
      }
      bool any_removed = false;
      for (const auto& schedule_id : schedules_to_remove) {
        any_removed = metadata->RemoveSnapshotSchedule(schedule_id) || any_removed;
      }
      if (any_removed) {
        WARN_NOT_OK(metadata->Flush(), "Failed to flush metadata");
      }
    });
    std::lock_guard lock(snapshot_schedule_allowed_history_cutoff_mutex_);
    for (const auto& schedule_id : schedules) {
      auto it = snapshot_schedule_allowed_history_cutoff_.find(schedule_id);
      if (it == snapshot_schedule_allowed_history_cutoff_.end()) {
        // We don't know this schedule.
        auto emplace_result =
            missing_snapshot_schedules_.emplace(schedule_id, snapshot_schedules_version_);
        if (!emplace_result.second &&
            emplace_result.first->second + 2 <= snapshot_schedules_version_) {
          // We don't know this schedule, and there are already 2 rounds of heartbeat passed
          // after we first time found that we don't know this schedule.
          // So it means that schedule was deleted.
          // One round is not enough, because schedule could be added after heartbeat processed on
          // master, but response not yet received on TServer.
          schedules_to_remove.push_back(schedule_id);
          continue;
        }
        return { HybridTime::kInvalid, HybridTime::kMin };
      }
      if (!it->second) {
        // Schedules does not have snapshots yet.
        return { HybridTime::kInvalid, HybridTime::kMin };
      }
      result.MakeAtMost(it->second);
    }
  }
  VLOG(1) << "Setting the allowed historycutoff: " << result
          << " for tablet: " << metadata->raft_group_id();
  return { HybridTime::kInvalid, result };
}

void TSTabletManager::FlushDirtySuperblocks() {
  for (const auto& peer : GetTabletPeers()) {
    if (peer->state() == RUNNING && peer->tablet_metadata()->IsLazySuperblockFlushEnabled()) {
      auto s = peer->tablet_metadata()->Flush(tablet::OnlyIfDirty::kTrue);
      if (!s.ok()) {
        LOG(WARNING) << "Failed flushing superblock for tablet " << peer->tablet_id()
                     << " from background thread: " << s;
      }
    }
  }
}

void TSTabletManager::WaitForRemoteSessionsToEnd(
    TabletRemoteSessionType session_type, const std::string& debug_session_string) const {
  const MonoDelta kSingleWait = 10ms;
  const MonoDelta kReportInterval = 5s;
  const MonoDelta kMaxWait = 30s;
  MonoDelta waited = MonoDelta::kZero;
  MonoDelta next_report_time = kReportInterval;
  while (true) {
    {
      std::lock_guard lock(mutex_);
      auto& remote_clients = session_type == TabletRemoteSessionType::kBootstrap
                                 ? remote_bootstrap_clients_
                                 : snapshot_transfer_clients_;
      const auto& remaining_sessions = remote_clients.num_clients_;
      const auto& source_addresses = remote_clients.source_addresses_;
      if (remaining_sessions == 0) return;

      if (waited >= next_report_time) {
        if (waited >= kMaxWait) {
          std::string addr = "";
          for (auto iter = source_addresses.begin(); iter != source_addresses.end(); iter++) {
            if (iter == source_addresses.begin()) {
              addr += iter->first;
            } else {
              addr += "," + iter->first;
            }
          }
          LOG_WITH_PREFIX(DFATAL) << Format(
              "Waited for $0ms. Still had $1 pending $2: $3", waited.ToMilliseconds(),
              remaining_sessions, debug_session_string, addr);
        } else {
          LOG_WITH_PREFIX(WARNING) << Format(
              "Still waiting for $0 ongoing $1 to finish after $2", remaining_sessions,
              debug_session_string, waited);
        }
        next_report_time = std::min(kMaxWait, waited + kReportInterval);
      }
    }

    SleepFor(kSingleWait);
    waited += kSingleWait;
  }
}

Result<tablet::TabletPeerPtr> TSTabletManager::RegisterRemoteClientAndLookupTablet(
    const TabletId& tablet_id, const std::string& private_addr, const std::string& log_prefix,
    RemoteClients* remote_clients, std::function<Status()> callback) {
  SCHECK_NOTNULL(remote_clients);

  std::lock_guard lock(mutex_);
  // To prevent racing against Shutdown, we increment this as soon as we start. This should be
  // done before checking for ClosingUnlocked, as on shutdown, we proceed in reverse:
  // - first mark as closing
  // - then wait for num_tablets_being_remote_bootstrapped_ == 0
  remote_clients->num_clients_++;
  remote_clients->source_addresses_[private_addr]++;

  auto tablet = VERIFY_RESULT(CheckStateAndLookupTabletUnlocked(tablet_id, log_prefix));
  RETURN_NOT_OK(callback());
  return tablet;
}

void TSTabletManager::DecrementRemoteSessionCount(
    const std::string& private_addr, RemoteClients* remote_clients) {
  std::lock_guard lock(mutex_);
  auto& source_addresses = remote_clients->source_addresses_;
  auto iter = source_addresses.find(private_addr);
  if (iter != source_addresses.end()) {
    if (--(iter->second) == 0) {
      source_addresses.erase(iter);
    }
  }
  remote_clients->num_clients_--;
}

template <class Key>
Result<tablet::TabletPeerPtr> TSTabletManager::CheckStateAndLookupTabletUnlocked(
    const Key& tablet_id, const std::string& log_prefix) const {
  if (ClosingUnlocked()) {
    auto result = STATUS_FORMAT(
        IllegalState, "Starting remote session in wrong state: $0",
        TSTabletManagerStatePB_Name(state_));
    LOG(WARNING) << log_prefix << result;
    return result;
  }

  return LookupTabletUnlocked(tablet_id);
}

template <class RemoteClient>
std::unique_ptr<RemoteClient> TSTabletManager::InitRemoteClient(
    const std::string& log_prefix, const TabletId& tablet_id, const PeerId& source_uuid,
    const std::string& source_addr, const std::string& debug_session_string) {
  const auto& init_msg = Format(
      "$0 Initiating $1 from Peer $2 ($3)", log_prefix, debug_session_string, source_uuid,
      source_addr);
  LOG(INFO) << init_msg;
  TRACE(init_msg);
  return std::make_unique<RemoteClient>(tablet_id, fs_manager_);
}

client::YBMetaDataCache* TSTabletManager::CreateYBMetaDataCache() {
  // Acquire lock for creating new instances, and make sure that some other thread has not already
  // initialized the cache.
  std::lock_guard<simple_spinlock> lock(metadata_cache_spinlock_);
  if (!metadata_cache_) {
    auto meta_data_cache_mem_tracker =
        MemTracker::FindOrCreateTracker(0, "Metadata cache", server_->mem_tracker());
    metadata_cache_holder_ = std::make_shared<client::YBMetaDataCache>(
        server_->client_future().get(), false /* Update permissions cache */,
        meta_data_cache_mem_tracker);
    metadata_cache_.store(metadata_cache_holder_.get(), std::memory_order_release);
  }
  return metadata_cache_holder_.get();
}

// Reader doesn't acquire any lock, writer ensures that only one writer creates the object under
// lock.
client::YBMetaDataCache* TSTabletManager::YBMetaDataCache() const {
  return metadata_cache_.load(std::memory_order_acquire);
}

Status DeleteTabletData(const RaftGroupMetadataPtr& meta,
                        TabletDataState data_state,
                        const string& uuid,
                        const yb::OpId& last_logged_opid,
                        TSTabletManager* ts_manager,
                        FsManager* fs_manager) {
  const string& tablet_id = meta->raft_group_id();
  const string kLogPrefix = LogPrefix(tablet_id, uuid);
  LOG(INFO) << kLogPrefix << "Deleting tablet data with delete state "
            << TabletDataState_Name(data_state);
  CHECK(data_state == TABLET_DATA_DELETED ||
        data_state == TABLET_DATA_TOMBSTONED)
      << "Unexpected data_state to delete tablet " << meta->raft_group_id() << ": "
      << TabletDataState_Name(data_state) << " (" << data_state << ")";

  // Note: Passing an unset 'last_logged_opid' will retain the last_logged_opid
  // that was previously in the metadata.
  RETURN_NOT_OK(meta->DeleteTabletData(data_state, last_logged_opid));
  LOG(INFO) << kLogPrefix << "Tablet deleted. Last logged OpId: "
            << meta->tombstone_last_logged_opid();
  MAYBE_FAULT(FLAGS_TEST_fault_crash_after_blocks_deleted);

  RETURN_NOT_OK(Log::DeleteOnDiskData(
      meta->fs_manager()->env(), meta->raft_group_id(), meta->wal_dir(),
      meta->fs_manager()->uuid()));
  MAYBE_FAULT(FLAGS_TEST_fault_crash_after_wal_deleted);

  // We do not delete the superblock or the consensus metadata when tombstoning
  // a tablet.
  if (data_state == TABLET_DATA_TOMBSTONED) {
    return Status::OK();
  }

  // Only TABLET_DATA_DELETED tablets get this far.
  RETURN_NOT_OK(ConsensusMetadata::DeleteOnDiskData(meta->fs_manager(), meta->raft_group_id()));
  MAYBE_FAULT(FLAGS_TEST_fault_crash_after_cmeta_deleted);
  return meta->DeleteSuperBlock();
}

void LogAndTombstone(const RaftGroupMetadataPtr& meta,
                     const std::string& msg,
                     const std::string& uuid,
                     const Status& s,
                     TSTabletManager* ts_manager) {
  const string& tablet_id = meta->raft_group_id();
  const string kLogPrefix = LogPrefix(tablet_id, uuid);
  LOG(WARNING) << kLogPrefix << msg << ": " << s.ToString();

  // Tombstone the tablet when remote bootstrap fails.
  LOG(INFO) << kLogPrefix << "Tombstoning tablet after failed remote bootstrap";
  Status delete_status = DeleteTabletData(meta,
                                          TABLET_DATA_TOMBSTONED,
                                          uuid,
                                          yb::OpId(),
                                          ts_manager);

  if (PREDICT_FALSE(FLAGS_TEST_sleep_after_tombstoning_tablet_secs > 0)) {
    // We sleep here so that the test can verify that the state of the tablet is
    // TABLET_DATA_TOMBSTONED.
    LOG(INFO) << "Sleeping after remote bootstrap failed";
    SleepFor(MonoDelta::FromSeconds(FLAGS_TEST_sleep_after_tombstoning_tablet_secs));
  }

  if (PREDICT_FALSE(!delete_status.ok())) {
    // This failure should only either indicate a bug or an IO error.
    LOG(FATAL) << kLogPrefix << "Failed to tombstone tablet after remote bootstrap: "
               << delete_status.ToString();
  }

  // Remove the child tracker if present.
  if (ts_manager != nullptr) {
    auto tracker = MemTracker::FindTracker(
        Format("tablet-$0", meta->raft_group_id()), ts_manager->server()->mem_tracker());
    if (tracker) {
      tracker->UnregisterFromParent();
    }
  }
}

TransitionInProgressDeleter::TransitionInProgressDeleter(
    TransitionInProgressMap* map, std::mutex* mutex, const TabletId& tablet_id)
    : in_progress_(map), mutex_(mutex), tablet_id_(tablet_id) {}

TransitionInProgressDeleter::~TransitionInProgressDeleter() {
  std::string transition;
  {
    std::unique_lock<std::mutex> lock(*mutex_);
    const auto iter = in_progress_->find(tablet_id_);
    CHECK(iter != in_progress_->end());
    transition = iter->second;
    in_progress_->erase(iter);
  }
  LOG(INFO) << "Deleted transition in progress " << transition
            << " for tablet " << tablet_id_;
}

Status ShutdownAndTombstoneTabletPeerNotOk(
    const Status& status, const tablet::TabletPeerPtr& tablet_peer,
    const tablet::RaftGroupMetadataPtr& meta, const std::string& uuid, const char* msg,
    TSTabletManager* ts_tablet_manager) {
  if (status.ok()) {
    return status;
  }
  // If shutdown was initiated by someone else we should not wait for shutdown to complete.
  if (tablet_peer && tablet_peer->StartShutdown()) {
    tablet_peer->CompleteShutdown(tablet::DisableFlushOnShutdown::kFalse, tablet::AbortOps::kFalse);
  }
  tserver::LogAndTombstone(meta, msg, uuid, status, ts_tablet_manager);
  return status;
}

} // namespace yb::tserver
