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

#include "yb/tablet/tablet_peer.h"

#include <algorithm>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "yb/consensus/consensus.h"
#include "yb/consensus/consensus.pb.h"
#include "yb/consensus/consensus_util.h"
#include "yb/consensus/log.h"
#include "yb/consensus/log_anchor_registry.h"
#include "yb/consensus/log_util.h"
#include "yb/consensus/opid_util.h"
#include "yb/consensus/raft_consensus.h"
#include "yb/consensus/retryable_requests.h"
#include "yb/consensus/state_change_context.h"

#include "yb/docdb/consensus_frontier.h"

#include "yb/gutil/casts.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/gutil/sysinfo.h"

#include "yb/master/master_ddl.pb.h"

#include "yb/rocksdb/db/memtable.h"

#include "yb/rpc/messenger.h"
#include "yb/rpc/periodic.h"
#include "yb/rpc/strand.h"
#include "yb/rpc/thread_pool.h"

#include "yb/tablet/operations/change_auto_flags_config_operation.h"
#include "yb/tablet/operations/change_metadata_operation.h"
#include "yb/tablet/operations/history_cutoff_operation.h"
#include "yb/tablet/operations/operation_driver.h"
#include "yb/tablet/operations/snapshot_operation.h"
#include "yb/tablet/operations/split_operation.h"
#include "yb/tablet/operations/truncate_operation.h"
#include "yb/tablet/operations/update_txn_operation.h"
#include "yb/tablet/operations/write_operation.h"
#include "yb/tablet/tablet.h"
#include "yb/tablet/tablet.pb.h"
#include "yb/tablet/tablet_bootstrap_if.h"
#include "yb/tablet/tablet_metadata.h"
#include "yb/tablet/tablet_metrics.h"
#include "yb/tablet/tablet_peer_mm_ops.h"
#include "yb/tablet/tablet_retention_policy.h"
#include "yb/tablet/transaction_participant.h"
#include "yb/tablet/write_query.h"

#include "yb/util/debug-util.h"
#include "yb/util/env_util.h"
#include "yb/util/fault_injection.h"
#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/logging.h"
#include "yb/util/metrics.h"
#include "yb/util/scope_exit.h"
#include "yb/util/status_format.h"
#include "yb/util/status_log.h"
#include "yb/util/stopwatch.h"
#include "yb/util/sync_point.h"
#include "yb/util/threadpool.h"
#include "yb/util/trace.h"

using namespace std::literals;
using namespace std::placeholders;
using std::shared_ptr;
using std::string;
using std::vector;

DEFINE_test_flag(int32, delay_init_tablet_peer_ms, 0,
                 "Wait before executing init tablet peer for specified amount of milliseconds.");

DEFINE_UNKNOWN_int32(cdc_min_replicated_index_considered_stale_secs, 900,
    "If cdc_min_replicated_index hasn't been replicated in this amount of time, we reset its"
    "value to max int64 to avoid retaining any logs");

DEFINE_UNKNOWN_bool(propagate_safe_time, true,
    "Propagate safe time to read from leader to followers");

DEFINE_RUNTIME_bool(abort_active_txns_during_xrepl_bootstrap, true,
    "Abort active transactions during xcluster and cdc bootstrapping. Inconsistent replicated data "
    "may be produced if this is disabled.");
TAG_FLAG(abort_active_txns_during_xrepl_bootstrap, advanced);

DEFINE_test_flag(double, fault_crash_leader_before_changing_role, 0.0,
                 "The leader will crash before changing the role (from PRE_VOTER or PRE_OBSERVER "
                 "to VOTER or OBSERVER respectively) of the tablet server it is remote "
                 "bootstrapping.");

DECLARE_int32(ysql_transaction_abort_timeout_ms);

DECLARE_int64(cdc_intent_retention_ms);

DECLARE_bool(enable_flush_retryable_requests);

namespace yb {
namespace tablet {

METRIC_DEFINE_event_stats(table, op_prepare_queue_length, "Operation Prepare Queue Length",
                        MetricUnit::kTasks,
                        "Number of operations waiting to be prepared within this tablet. "
                        "High queue lengths indicate that the server is unable to process "
                        "operations as fast as they are being written to the WAL.");

METRIC_DEFINE_event_stats(table, op_prepare_queue_time, "Operation Prepare Queue Time",
                        MetricUnit::kMicroseconds,
                        "Time that operations spent waiting in the prepare queue before being "
                        "processed. High queue times indicate that the server is unable to "
                        "process operations as fast as they are being written to the WAL.");

METRIC_DEFINE_event_stats(table, op_prepare_run_time, "Operation Prepare Run Time",
                        MetricUnit::kMicroseconds,
                        "Time that operations spent being prepared in the tablet. "
                        "High values may indicate that the server is under-provisioned or "
                        "that operations are experiencing high contention with one another for "
                        "locks.");

using consensus::Consensus;
using consensus::ConsensusBootstrapInfo;
using consensus::ConsensusMetadata;
using consensus::ConsensusOptions;
using consensus::ConsensusRound;
using consensus::PeerMemberType;
using consensus::RaftConfigPB;
using consensus::RaftConsensus;
using consensus::RaftPeerPB;
using consensus::StateChangeContext;
using consensus::StateChangeReason;
using log::Log;
using log::LogAnchorRegistry;
using rpc::Messenger;
using strings::Substitute;
using tserver::TabletServerErrorPB;

// ============================================================================
//  Tablet Peer
// ============================================================================
TabletPeer::TabletPeer(
    const RaftGroupMetadataPtr& meta,
    const consensus::RaftPeerPB& local_peer_pb,
    const scoped_refptr<server::Clock>& clock,
    const std::string& permanent_uuid,
    Callback<void(std::shared_ptr<StateChangeContext> context)> mark_dirty_clbk,
    MetricRegistry* metric_registry,
    TabletSplitter* tablet_splitter,
    const std::shared_future<client::YBClient*>& client_future)
    : meta_(meta),
      tablet_id_(meta->raft_group_id()),
      local_peer_pb_(local_peer_pb),
      state_(RaftGroupStatePB::NOT_STARTED),
      operation_tracker_(consensus::MakeTabletLogPrefix(tablet_id_, permanent_uuid)),
      status_listener_(new TabletStatusListener(meta)),
      clock_(clock),
      log_anchor_registry_(new LogAnchorRegistry()),
      mark_dirty_clbk_(std::move(mark_dirty_clbk)),
      permanent_uuid_(permanent_uuid),
      preparing_operations_counter_(operation_tracker_.LogPrefix()),
      metric_registry_(metric_registry),
      tablet_splitter_(tablet_splitter),
      client_future_(client_future) {}

TabletPeer::~TabletPeer() {
  std::lock_guard lock(lock_);
  // We should either have called Shutdown(), or we should have never called
  // Init().
  LOG_IF_WITH_PREFIX(DFATAL, tablet_) << "TabletPeer not fully shut down.";
}

Status TabletPeer::InitTabletPeer(
    const TabletPtr& tablet,
    const std::shared_ptr<MemTracker>& server_mem_tracker,
    Messenger* messenger,
    rpc::ProxyCache* proxy_cache,
    const scoped_refptr<Log>& log,
    const scoped_refptr<MetricEntity>& table_metric_entity,
    const scoped_refptr<MetricEntity>& tablet_metric_entity,
    ThreadPool* raft_pool,
    ThreadPool* tablet_prepare_pool,
    consensus::RetryableRequestsManager* retryable_requests_manager,
    std::unique_ptr<ConsensusMetadata> consensus_meta,
    consensus::MultiRaftManager* multi_raft_manager,
    ThreadPool* flush_retryable_requests_pool) {
  DCHECK(tablet) << "A TabletPeer must be provided with a Tablet";
  DCHECK(log) << "A TabletPeer must be provided with a Log";

  if (FLAGS_TEST_delay_init_tablet_peer_ms > 0) {
    std::this_thread::sleep_for(FLAGS_TEST_delay_init_tablet_peer_ms * 1ms);
  }

  // Additional `consensus` variable is required for the out-of-lock-block usage in this method.
  shared_ptr<consensus::RaftConsensus> consensus;
  {
    std::lock_guard lock(lock_);
    auto state = state_.load(std::memory_order_acquire);
    if (state != RaftGroupStatePB::BOOTSTRAPPING) {
      return STATUS_FORMAT(
          IllegalState, "Invalid tablet state for init: $0", RaftGroupStatePB_Name(state));
    }
    tablet_ = tablet;
    tablet_weak_ = tablet;
    log_ = log;
    // "Publish" the log pointer so it can be retrieved using the log() accessor.
    log_atomic_ = log.get();
    service_thread_pool_ = &messenger->ThreadPool();
    strand_.reset(new rpc::Strand(&messenger->ThreadPool()));
    messenger_ = messenger;

    tablet->SetMemTableFlushFilterFactory([log] {
      auto largest_log_op_index = log->GetLatestEntryOpId().index;
      return [largest_log_op_index] (const rocksdb::MemTable& memtable, bool) -> Result<bool> {
        auto frontiers = memtable.Frontiers();
        if (frontiers) {
          const auto largest_memtable_op_index =
              down_cast<const docdb::ConsensusFrontier&>(frontiers->Largest()).op_id().index;
          // We can only flush this memtable if all operations written to it have also been written
          // to the log (maybe not synced, if durable_wal_write is disabled, but that's OK).
          auto should_flush = largest_memtable_op_index <= largest_log_op_index;
          if (!should_flush) {
            LOG(WARNING)
              << "Skipping flush on memtable with ops ahead of log. "
              << "Memtable index: " << largest_memtable_op_index
              << " - log index: " << largest_log_op_index;
          }
          return should_flush;
        }

        // It is correct to not have frontiers when memtable is empty
        if (memtable.IsEmpty()) {
          return true;
        }

        // This is a degenerate case that should ideally never occur. An empty memtable got into the
        // list of immutable memtables. We say it is OK to flush it and move on.
        static const char* error_msg =
            "A memtable with no frontiers set found when deciding what memtables to "
            "flush! This should not happen.";
        LOG(ERROR) << error_msg << " Stack trace:\n" << GetStackTrace();
        return STATUS(IllegalState, error_msg);
      };
    });

    tablet_->SetCleanupPool(raft_pool);

    ConsensusOptions options;
    options.tablet_id = meta_->raft_group_id();

    TRACE("Creating consensus instance");

    if (!consensus_meta) {
      RETURN_NOT_OK(ConsensusMetadata::Load(meta_->fs_manager(), tablet_id_,
                                            meta_->fs_manager()->uuid(), &consensus_meta));
    }

    consensus_ = RaftConsensus::Create(
        options,
        std::move(consensus_meta),
        local_peer_pb_,
        table_metric_entity,
        tablet_metric_entity,
        clock_,
        this,
        messenger,
        proxy_cache,
        log_.get(),
        server_mem_tracker,
        tablet_->mem_tracker(),
        mark_dirty_clbk_,
        tablet_->table_type(),
        raft_pool,
        retryable_requests_manager,
        multi_raft_manager);
    has_consensus_.store(true, std::memory_order_release);
    consensus = consensus_;

    auto flush_retryable_requests_pool_token = flush_retryable_requests_pool
        ? flush_retryable_requests_pool->NewToken(ThreadPool::ExecutionMode::SERIAL) : nullptr;
    retryable_requests_flusher_ = std::make_shared<RetryableRequestsFlusher>(
        tablet_id_, consensus_, std::move(flush_retryable_requests_pool_token));

    tablet_->SetHybridTimeLeaseProvider(std::bind(&TabletPeer::HybridTimeLease, this, _1, _2));
    operation_tracker_.SetPostTracker(
        std::bind(&RaftConsensus::TrackOperationMemory, consensus_.get(), _1));

    prepare_thread_ = std::make_unique<Preparer>(consensus_.get(), tablet_prepare_pool);

    // "Publish" the tablet object right before releasing the lock.
    tablet_obj_state_.store(TabletObjectState::kAvailable, std::memory_order_release);
  }
  // End of lock scope for lock_.

  auto raft_config = consensus->CommittedConfig();
  ChangeConfigReplicated(raft_config);  // Set initial flag value.

  RETURN_NOT_OK(prepare_thread_->Start());

  if (tablet_->metrics() != nullptr) {
    TRACE("Starting instrumentation");
    operation_tracker_.StartInstrumentation(tablet_->GetTabletMetricsEntity());
  }
  operation_tracker_.StartMemoryTracking(tablet_->mem_tracker());

  RETURN_NOT_OK(set_cdc_min_replicated_index(meta_->cdc_min_replicated_index()));

  TRACE("TabletPeer::Init() finished");
  VLOG_WITH_PREFIX(2) << "Peer Initted";

  return Status::OK();
}

Result<FixedHybridTimeLease> TabletPeer::HybridTimeLease(
    HybridTime min_allowed, CoarseTimePoint deadline) {
  auto time = VERIFY_RESULT(WaitUntil(clock_.get(), min_allowed, deadline));
  // min_allowed could contain non zero logical part, so we add one microsecond to be sure that
  // the resulting ht_lease is at least min_allowed.
  auto min_allowed_micros = min_allowed.CeilPhysicalValueMicros();
  MicrosTime lease_micros =
      VERIFY_RESULT(VERIFY_RESULT(GetConsensus())
                        ->MajorityReplicatedHtLeaseExpiration(min_allowed_micros, deadline));
  if (lease_micros >= kMaxHybridTimePhysicalMicros) {
    // This could happen when leader leases are disabled.
    return FixedHybridTimeLease();
  }
  return FixedHybridTimeLease {
    .time = time,
    .lease = HybridTime(lease_micros, /* logical */ 0)
  };
}

Result<HybridTime> TabletPeer::PreparePeerRequest() {
  auto leader_term =
      VERIFY_RESULT(GetRaftConsensus())->GetLeaderState(/* allow_stale= */ true).term;
  if (leader_term >= 0) {
    auto last_write_ht = tablet_->mvcc_manager()->LastReplicatedHybridTime();
    auto propagated_history_cutoff =
        tablet_->RetentionPolicy()->HistoryCutoffToPropagate(last_write_ht);

    if (propagated_history_cutoff.cotables_cutoff_ht ||
        propagated_history_cutoff.primary_cutoff_ht) {
      VLOG_WITH_PREFIX(2) << "Propagate history cutoff: " << propagated_history_cutoff;

      auto operation = std::make_unique<HistoryCutoffOperation>(tablet_);
      auto request = operation->AllocateRequest();
      if (propagated_history_cutoff.primary_cutoff_ht) {
        request->set_primary_cutoff_ht(
            propagated_history_cutoff.primary_cutoff_ht.ToUint64());
      }
      if (propagated_history_cutoff.cotables_cutoff_ht) {
        request->set_cotables_cutoff_ht(
            propagated_history_cutoff.cotables_cutoff_ht.ToUint64());
      }
      Submit(std::move(operation), leader_term);
    }
  }

  if (!FLAGS_propagate_safe_time) {
    return HybridTime::kInvalid;
  }

  // Get the current majority-replicated HT leader lease without any waiting.
  auto ht_lease = VERIFY_RESULT(HybridTimeLease(
      /* min_allowed= */ HybridTime::kMin, /* deadline */ CoarseTimePoint::max()));
  return tablet_->mvcc_manager()->SafeTime(ht_lease);
}

Status TabletPeer::MajorityReplicated() {
  auto ht_lease = VERIFY_RESULT(HybridTimeLease(
      /* min_allowed= */ HybridTime::kMin, /* deadline */ CoarseTimePoint::max()));

  tablet_->mvcc_manager()->UpdatePropagatedSafeTimeOnLeader(ht_lease);
  return Status::OK();
}

void TabletPeer::ChangeConfigReplicated(const RaftConfigPB& config) {
  tablet_->mvcc_manager()->SetLeaderOnlyMode(config.peers_size() == 1);
}

uint64_t TabletPeer::NumSSTFiles() {
  return tablet_->GetCurrentVersionNumSSTFiles();
}

void TabletPeer::ListenNumSSTFilesChanged(std::function<void()> listener) {
  tablet_->ListenNumSSTFilesChanged(std::move(listener));
}

Status TabletPeer::CheckOperationAllowed(const OpId& op_id, consensus::OperationType op_type) {
  return tablet_->CheckOperationAllowed(op_id, op_type);
}

Status TabletPeer::Start(const ConsensusBootstrapInfo& bootstrap_info) {
  {
    std::lock_guard l(state_change_lock_);
    TRACE("Starting consensus");

    VLOG_WITH_PREFIX(2) << "Peer starting";

    auto consensus = GetRaftConsensusUnsafe();
    CHECK_NOTNULL(consensus.get()); // Sanity check, consensus must be alive at this point.
    VLOG(2) << "RaftConfig before starting: " << consensus->CommittedConfig().DebugString();

    // If tablet was previously considered shutdown w.r.t. metrics,
    // fix that for a tablet now being reinstated.
    DVLOG_WITH_PREFIX(3)
      << "Remove from set of tablets that have been shutdown so as to allow reporting metrics";
    metric_registry_->tablets_shutdown_erase(tablet_id());

    RETURN_NOT_OK(consensus->Start(bootstrap_info));
    RETURN_NOT_OK(UpdateState(RaftGroupStatePB::BOOTSTRAPPING, RaftGroupStatePB::RUNNING,
                              "Incorrect state to start TabletPeer, "));
  }
  if (tablet_->transaction_coordinator()) {
    tablet_->transaction_coordinator()->Start();
  }

  if (tablet_->transaction_participant()) {
    tablet_->transaction_participant()->Start();
  }

  // The context tracks that the current caller does not hold the lock for consensus state.
  // So mark dirty callback, e.g., consensus->ConsensusState() for master consensus callback of
  // SysCatalogStateChanged, can get the lock when needed.
  auto context =
      std::make_shared<StateChangeContext>(StateChangeReason::TABLET_PEER_STARTED, false);
  // Because we changed the tablet state, we need to re-report the tablet to the master.
  mark_dirty_clbk_.Run(context);

  return tablet_->EnableCompactions(/* blocking_rocksdb_shutdown_start_ops_pause */ nullptr);
}

bool TabletPeer::StartShutdown() {
  LOG_WITH_PREFIX(INFO) << "Initiating TabletPeer shutdown";

  {
    std::lock_guard lock(lock_);
    DEBUG_ONLY_TEST_SYNC_POINT("TabletPeer::StartShutdown:1");
    if (tablet_) {
      tablet_->StartShutdown();
    }
  }

  {
    RaftGroupStatePB state = state_.load(std::memory_order_acquire);
    for (;;) {
      if (state == RaftGroupStatePB::QUIESCING || state == RaftGroupStatePB::SHUTDOWN) {
        return false;
      }
      if (state_.compare_exchange_strong(
          state, RaftGroupStatePB::QUIESCING, std::memory_order_acq_rel)) {
        LOG_WITH_PREFIX(INFO) << "Started shutdown from state: " << RaftGroupStatePB_Name(state);
        break;
      }
    }
  }

  std::lock_guard l(state_change_lock_);
  // Even though Tablet::Shutdown() also unregisters its ops, we have to do it here
  // to ensure that any currently running operation finishes before we proceed with
  // the rest of the shutdown sequence. In particular, a maintenance operation could
  // indirectly end up calling into the log, which we are about to shut down.
  UnregisterMaintenanceOps();

  auto consensus = GetRaftConsensusUnsafe();
  if (consensus) {
    consensus->Shutdown();
  }

  return true;
}

void TabletPeer::CompleteShutdown(
    const DisableFlushOnShutdown disable_flush_on_shutdown, const AbortOps abort_ops) {
  auto* strand = strand_.get();
  if (strand) {
    strand->Shutdown();
  }

  preparing_operations_counter_.Shutdown();

  // TODO: KUDU-183: Keep track of the pending tasks and send an "abort" message.
  LOG_SLOW_EXECUTION(WARNING, 1000,
      Substitute("TabletPeer: tablet $0: Waiting for Operations to complete", tablet_id())) {
    operation_tracker_.WaitForAllToFinish();
  }

  if (prepare_thread_) {
    prepare_thread_->Stop();
  }

  if (log_) {
    WARN_NOT_OK(log_->Close(), LogPrefix() + "Error closing the Log");
  }

  VLOG_WITH_PREFIX(1) << "Shut down!";

  if (tablet_) {
    tablet_->CompleteShutdown(disable_flush_on_shutdown, abort_ops);
  }

  tablet_obj_state_.store(TabletObjectState::kDestroyed, std::memory_order_release);

  // Only mark the peer as SHUTDOWN when all other components have shut down.
  std::shared_ptr<consensus::RaftConsensus> consensus;
  {
    std::lock_guard lock(lock_);
    strand_.reset();
    if (retryable_requests_flusher_) {
      retryable_requests_flusher_->Shutdown();
      retryable_requests_flusher_.reset();
    }
    // Release mem tracker resources.
    has_consensus_.store(false, std::memory_order_release);
    // Clear the consensus and destroy it outside the lock.
    consensus_.swap(consensus);
    prepare_thread_.reset();
    tablet_.reset();
    auto state = state_.load(std::memory_order_acquire);
    LOG_IF_WITH_PREFIX(DFATAL, state != RaftGroupStatePB::QUIESCING) <<
        "Bad state when completing shutdown: " << RaftGroupStatePB_Name(state);
    state_.store(RaftGroupStatePB::SHUTDOWN, std::memory_order_release);

    if (metric_registry_) {
      DVLOG_WITH_PREFIX(3)
        << "Add to set of tablets that have been shutdown so as to avoid reporting metrics";
      metric_registry_->tablets_shutdown_insert(tablet_id());
    }
  }
}

void TabletPeer::WaitUntilShutdown() {
  const MonoDelta kSingleWait = 10ms;
  const MonoDelta kReportInterval = 5s;
  const MonoDelta kMaxWait = 30s;

  MonoDelta waited = MonoDelta::kZero;
  MonoDelta last_reported = MonoDelta::kZero;
  while (state_.load(std::memory_order_acquire) != RaftGroupStatePB::SHUTDOWN) {
    if (waited >= last_reported + kReportInterval) {
      if (waited >= kMaxWait) {
        LOG_WITH_PREFIX(DFATAL)
            << "Wait for shutdown " << waited << " exceeded kMaxWait " << kMaxWait;
      } else {
        LOG_WITH_PREFIX(WARNING) << "Long wait for shutdown: " << waited;
      }
      last_reported = waited;
    }
    SleepFor(kSingleWait);
    waited += kSingleWait;
  }

  if (metric_registry_) {
    DVLOG_WITH_PREFIX(3)
      << "Add to set of tablets that have been shutdown so as to avoid reporting metrics";
    metric_registry_->tablets_shutdown_insert(tablet_id());
  }
}

Status TabletPeer::Shutdown(
    ShouldAbortActiveTransactions should_abort_active_txns,
    DisableFlushOnShutdown disable_flush_on_shutdown) {
  auto is_shutdown_initiated = StartShutdown();

  if (should_abort_active_txns) {
    // Once raft group state enters QUIESCING state,
    // new queries cannot be processed from then onwards.
    // Aborting any remaining active transactions in the tablet.
    AbortSQLTransactions();
  }

  if (is_shutdown_initiated) {
    CompleteShutdown(disable_flush_on_shutdown, AbortOps(should_abort_active_txns));
  } else {
    WaitUntilShutdown();
  }
  return Status::OK();
}

void TabletPeer::AbortSQLTransactions() const {
  if (!tablet_) {
    return;
  }
  auto deadline =
      CoarseMonoClock::Now() + MonoDelta::FromMilliseconds(FLAGS_ysql_transaction_abort_timeout_ms);
  WARN_NOT_OK(
      tablet_->AbortSQLTransactions(deadline),
      "Cannot abort transactions for tablet " + tablet_->tablet_id());
}

Status TabletPeer::CheckRunning() const {
  auto state = state_.load(std::memory_order_acquire);
  if (state != RaftGroupStatePB::RUNNING) {
    if (state == RaftGroupStatePB::QUIESCING) {
      return STATUS(ShutdownInProgress, "The tablet is shutting down");
    }
    return STATUS_FORMAT(IllegalState, "The tablet is not in a running state: $0",
                         RaftGroupStatePB_Name(state));
  }

  return Status::OK();
}

bool TabletPeer::IsShutdownStarted() const {
  auto state = state_.load(std::memory_order_acquire);
  return state == RaftGroupStatePB::QUIESCING || state == RaftGroupStatePB::SHUTDOWN;
}

Status TabletPeer::CheckShutdownOrNotStarted() const {
  RaftGroupStatePB value = state_.load(std::memory_order_acquire);
  if (value != RaftGroupStatePB::SHUTDOWN && value != RaftGroupStatePB::NOT_STARTED) {
    return STATUS(IllegalState, Substitute("The tablet is not in a shutdown state: $0",
                                           RaftGroupStatePB_Name(value)));
  }

  return Status::OK();
}

Status TabletPeer::WaitUntilConsensusRunning(const MonoDelta& timeout) {
  MonoTime start(MonoTime::Now());

  int backoff_exp = 0;
  const int kMaxBackoffExp = 8;
  while (true) {
    RaftGroupStatePB cached_state = state_.load(std::memory_order_acquire);
    if (cached_state == RaftGroupStatePB::QUIESCING || cached_state == RaftGroupStatePB::SHUTDOWN) {
      return STATUS(IllegalState,
          Substitute("The tablet is already shutting down or shutdown. State: $0",
                     RaftGroupStatePB_Name(cached_state)));
    }
    if (cached_state == RUNNING && has_consensus_.load(std::memory_order_acquire) &&
        VERIFY_RESULT(GetRaftConsensus())->IsRunning()) {
      break;
    }
    MonoTime now(MonoTime::Now());
    MonoDelta elapsed(now.GetDeltaSince(start));
    if (elapsed.MoreThan(timeout)) {
      return STATUS(TimedOut, Substitute("Consensus is not running after waiting for $0. State; $1",
                                         elapsed.ToString(), RaftGroupStatePB_Name(cached_state)));
    }
    SleepFor(MonoDelta::FromMilliseconds(1 << backoff_exp));
    backoff_exp = std::min(backoff_exp + 1, kMaxBackoffExp);
  }
  return Status::OK();
}

void TabletPeer::WriteAsync(std::unique_ptr<WriteQuery> query) {
  ScopedOperation preparing_token(&preparing_operations_counter_);
  auto status = CheckRunning();
  if (!status.ok()) {
    query->Cancel(status);
    return;
  }

  query->operation().set_preparing_token(std::move(preparing_token));
  tablet_->AcquireLocksAndPerformDocOperations(std::move(query));
}

Result<HybridTime> TabletPeer::ReportReadRestart() {
  tablet_->metrics()->Increment(TabletCounters::kRestartReadRequests);
  return tablet_->SafeTime(RequireLease::kTrue);
}

void TabletPeer::Submit(std::unique_ptr<Operation> operation, int64_t term) {
  auto status = CheckRunning();

  if (status.ok()) {
    auto driver = NewLeaderOperationDriver(&operation, term);
    if (driver.ok()) {
      (**driver).ExecuteAsync();
    } else {
      status = driver.status();
    }
  }
  if (!status.ok()) {
    operation->Aborted(status, /* was_pending= */ false);
  }
}

Status TabletPeer::SubmitUpdateTransaction(
    std::unique_ptr<UpdateTxnOperation> operation, int64_t term) {
  if (!operation->tablet_is_set()) {
    auto tablet = VERIFY_RESULT(shared_tablet_safe());
    operation->SetTablet(tablet);
  }
  auto scoped_read_operation = VERIFY_RESULT(operation->tablet_safe())
                                   ->CreateScopedRWOperationBlockingRocksDbShutdownStart();
  if (!scoped_read_operation.ok()) {
    auto status = MoveStatus(scoped_read_operation).CloneAndPrepend(operation->ToString());
    operation->CompleteWithStatus(status);
    return status;
  }
  Submit(std::move(operation), term);
  return Status::OK();
}

HybridTime TabletPeer::SafeTimeForTransactionParticipant() {
  return tablet_->mvcc_manager()->SafeTimeForFollower(
      /* min_allowed= */ HybridTime::kMin, /* deadline= */ CoarseTimePoint::min());
}

Result<HybridTime> TabletPeer::WaitForSafeTime(HybridTime safe_time, CoarseTimePoint deadline) {
  return tablet_->SafeTime(RequireLease::kFallbackToFollower, safe_time, deadline);
}

Status TabletPeer::GetLastReplicatedData(RemoveIntentsData* data) {
  std::shared_ptr<consensus::RaftConsensus> consensus;
  TabletPtr tablet;
  {
    std::lock_guard lock(lock_);
    consensus = consensus_;
    tablet = tablet_;
  }
  if (!consensus) {
    return STATUS(IllegalState, "Consensus destroyed");
  }
  if (!tablet) {
    return STATUS(IllegalState, "Tablet destroyed");
  }
  data->op_id = consensus->GetLastCommittedOpId();
  data->log_ht = tablet->mvcc_manager()->LastReplicatedHybridTime();
  return Status::OK();
}

void TabletPeer::UpdateClock(HybridTime hybrid_time) {
  clock_->Update(hybrid_time);
}

std::unique_ptr<UpdateTxnOperation> TabletPeer::CreateUpdateTransaction(
    std::shared_ptr<LWTransactionStatePB> request) {
  // TODO: safe handling for the case when tablet is not set.
  auto result = std::make_unique<UpdateTxnOperation>(CHECK_RESULT(shared_tablet_safe()));
  result->TakeRequest(std::move(request));
  return result;
}

void TabletPeer::GetTabletStatusPB(TabletStatusPB* status_pb_out) {
  std::lock_guard lock(lock_);
  DCHECK(status_pb_out != nullptr);
  DCHECK(status_listener_.get() != nullptr);
  const auto disk_size_info = GetOnDiskSizeInfo();
  status_pb_out->set_tablet_id(status_listener_->tablet_id());
  status_pb_out->set_namespace_name(status_listener_->namespace_name());
  status_pb_out->set_table_name(status_listener_->table_name());
  status_pb_out->set_table_id(status_listener_->table_id());
  status_pb_out->set_last_status(status_listener_->last_status());
  status_listener_->partition()->ToPB(status_pb_out->mutable_partition());
  status_pb_out->set_state(state_);
  status_pb_out->set_tablet_data_state(meta_->tablet_data_state());
  auto tablet = tablet_;
  if (tablet) {
    status_pb_out->set_table_type(tablet->table_type());
  }
  disk_size_info.ToPB(status_pb_out);
  // Set hide status of the tablet.
  status_pb_out->set_is_hidden(meta_->hidden());
  status_pb_out->set_parent_data_compacted(meta_->parent_data_compacted());
  for (const auto& table : meta_->GetAllColocatedTables()) {
    status_pb_out->add_colocated_table_ids(table);
  }
}

Status TabletPeer::RunLogGC() {
  if (!CheckRunning().ok()) {
    return Status::OK();
  }
  auto s = reset_cdc_min_replicated_index_if_stale();
  if (!s.ok()) {
    LOG_WITH_PREFIX(WARNING) << "Unable to reset cdc min replicated index " << s;
  }
  int64_t min_log_index;
  if (VLOG_IS_ON(2)) {
    std::string details;
    min_log_index = VERIFY_RESULT(GetEarliestNeededLogIndex(&details));
    LOG_WITH_PREFIX(INFO) << __func__ << ": " << details;
  } else {
     min_log_index = VERIFY_RESULT(GetEarliestNeededLogIndex());
  }
  int32_t num_gced = 0;
  return log_->GC(min_log_index, &num_gced);
}

Result<TabletPtr> TabletPeer::shared_tablet_safe() const {
  // Note that there is still a possible race condition between the time we check the tablet state
  // and the time we access the tablet shared pointer through the weak pointer.
  auto tablet_obj_state = tablet_obj_state_.load(std::memory_order_acquire);
  if (tablet_obj_state != TabletObjectState::kAvailable) {
    return STATUS_FORMAT(
        IllegalState,
        "Tablet not running: tablet object $0 has invalid state $1",
        tablet_id_, tablet_obj_state);
  }
  // The weak pointer is safe to access as long as the state is not kUninitialized, because it
  // never changes after the state is set to kAvailable. However, we still need to check if
  // lock() returns nullptr.
  auto tablet_ptr = tablet_weak_.lock();
  if (tablet_ptr)
    return tablet_ptr;
  return STATUS_FORMAT(
      IllegalState,
      "Tablet object $0 has already been deallocated",
      tablet_id_);
}

TabletDataState TabletPeer::data_state() const {
  std::lock_guard lock(lock_);
  return meta_->tablet_data_state();
}

string TabletPeer::HumanReadableState() const {
  std::lock_guard lock(lock_);
  TabletDataState data_state = meta_->tablet_data_state();
  RaftGroupStatePB state = this->state();
  // If failed, any number of things could have gone wrong.
  if (state == RaftGroupStatePB::FAILED) {
    return Substitute("$0 ($1): $2", RaftGroupStatePB_Name(state),
                      TabletDataState_Name(data_state),
                      error_.get()->ToString());
  // If it's remotely bootstrapping, or tombstoned, that is the important thing
  // to show.
  } else if (!CanServeTabletData(data_state)) {
    return TabletDataState_Name(data_state);
  } else if (data_state == TabletDataState::TABLET_DATA_SPLIT_COMPLETED) {
    return RaftGroupStatePB_Name(state) + " (split)";
  }
  // Otherwise, the tablet's data is in a "normal" state, so we just display
  // the runtime state (BOOTSTRAPPING, RUNNING, etc).
  return RaftGroupStatePB_Name(state);
}

namespace {

consensus::OperationType MapOperationTypeToPB(OperationType operation_type) {
  switch (operation_type) {
    case OperationType::kWrite:
      return consensus::WRITE_OP;

    case OperationType::kChangeMetadata:
      return consensus::CHANGE_METADATA_OP;

    case OperationType::kUpdateTransaction:
      return consensus::UPDATE_TRANSACTION_OP;

    case OperationType::kSnapshot:
      return consensus::SNAPSHOT_OP;

    case OperationType::kTruncate:
      return consensus::TRUNCATE_OP;

    case OperationType::kHistoryCutoff:
      return consensus::HISTORY_CUTOFF_OP;

    case OperationType::kSplit:
      return consensus::SPLIT_OP;

    case OperationType::kChangeAutoFlagsConfig:
      return consensus::CHANGE_AUTO_FLAGS_CONFIG_OP;

    case OperationType::kEmpty:
      LOG(FATAL) << "OperationType::kEmpty cannot be converted to consensus::OperationType";
  }
  FATAL_INVALID_ENUM_VALUE(OperationType, operation_type);
}

} // namespace

void TabletPeer::GetInFlightOperations(Operation::TraceType trace_type,
                                       vector<consensus::OperationStatusPB>* out) const {
  for (const auto& driver : operation_tracker_.GetPendingOperations()) {
    if (driver->operation() == nullptr) {
      continue;
    }
    auto op_type = driver->operation_type();
    if (op_type == OperationType::kEmpty) {
      // This is a special-purpose in-memory-only operation for updating propagated safe time on
      // a follower.
      continue;
    }

    consensus::OperationStatusPB status_pb;
    driver->GetOpId().ToPB(status_pb.mutable_op_id());
    status_pb.set_operation_type(MapOperationTypeToPB(op_type));
    status_pb.set_description(driver->ToString());
    int64_t running_for_micros =
        MonoTime::Now().GetDeltaSince(driver->start_time()).ToMicroseconds();
    status_pb.set_running_for_micros(running_for_micros);
    if (trace_type == Operation::TRACE_TXNS && driver->trace()) {
      status_pb.set_trace_buffer(driver->trace()->DumpToString(true));
    }
    out->push_back(status_pb);
  }
}

Result<int64_t> TabletPeer::GetEarliestNeededLogIndex(std::string* details) const {
  if (PREDICT_FALSE(!log_)) {
    auto status = STATUS(Uninitialized, "Log not ready (tablet peer not yet initialized?)");
    LOG(DFATAL) << status;
    return status;
  }

  // First, we anchor on the last OpId in the Log to establish a lower bound
  // and avoid racing with the other checks. This limits the Log GC candidate
  // segments before we check the anchors.
  auto latest_log_entry_op_id = log_->GetLatestEntryOpId();
  int64_t min_index = latest_log_entry_op_id.index;
  if (details) {
    *details += Format("Latest log entry op id: $0\n", latest_log_entry_op_id);
  }

  // If we never have written to the log, no need to proceed.
  if (min_index == 0) {
    return min_index;
  }

  // Next, we interrogate the anchor registry.
  // Returns OK if minimum known, NotFound if no anchors are registered.
  {
    int64_t min_anchor_index;
    Status s = log_anchor_registry_->GetEarliestRegisteredLogIndex(&min_anchor_index);
    if (PREDICT_FALSE(!s.ok())) {
      DCHECK(s.IsNotFound()) << "Unexpected error calling LogAnchorRegistry: " << s.ToString();
    } else {
      min_index = std::min(min_index, min_anchor_index);
      if (details) {
        *details += Format("Min anchor index: $0\n", min_anchor_index);
      }
    }
  }

  // Next, interrogate the OperationTracker.
  int64_t min_pending_op_index = std::numeric_limits<int64_t>::max();
  for (const auto& driver : operation_tracker_.GetPendingOperations()) {
    auto tx_op_id = driver->GetOpId();
    // A operation which doesn't have an opid hasn't been submitted for replication yet and
    // thus has no need to anchor the log.
    if (tx_op_id != yb::OpId::Invalid()) {
      min_pending_op_index = std::min(min_pending_op_index, tx_op_id.index);
    }
  }

  min_index = std::min(min_index, min_pending_op_index);
  if (details && min_pending_op_index != std::numeric_limits<int64_t>::max()) {
    *details += Format("Min pending op id index: $0\n", min_pending_op_index);
  }

  auto min_retryable_request_op_id = VERIFY_RESULT(GetRaftConsensus())->MinRetryableRequestOpId();
  min_index = std::min(min_index, min_retryable_request_op_id.index);
  if (details) {
    *details += Format("Min retryable request op id: $0\n", min_retryable_request_op_id);
  }

  auto tablet = VERIFY_RESULT(shared_tablet_safe());
  auto* transaction_coordinator = tablet->transaction_coordinator();
  if (transaction_coordinator) {
    auto transaction_coordinator_min_op_index = transaction_coordinator->PrepareGC(details);
    min_index = std::min(min_index, transaction_coordinator_min_op_index);
  }

  // We keep at least one committed operation in the log so that we can always recover safe time
  // during bootstrap.
  // Last committed op id should be read before MaxPersistentOpId to avoid race condition
  // described in MaxPersistentOpIdForDb.
  //
  // If we read last committed op id AFTER reading last persistent op id (INCORRECT):
  // - We read max persistent op id and find there is no new data, so we ignore it.
  // - New data gets written and Raft-committed, but not yet flushed to an SSTable.
  // - We read the last committed op id, which is greater than what max persistent op id would have
  //   returned.
  // - We garbage-collect the Raft log entries corresponding to the new data.
  // - Power is lost and the server reboots, losing committed data.
  //
  // If we read last committed op id BEFORE reading last persistent op id (CORRECT):
  // - We read the last committed op id.
  // - We read max persistent op id and find there is no new data, so we ignore it.
  // - New data gets written and Raft-committed, but not yet flushed to an SSTable.
  // - We still don't garbage-collect the logs containing the committed but unflushed data,
  //   because the earlier value of the last committed op id that we read prevents us from doing so.
  auto last_committed_op_id = VERIFY_RESULT(GetConsensus())->GetLastCommittedOpId();
  min_index = std::min(min_index, last_committed_op_id.index);
  if (details) {
    *details += Format("Last committed op id: $0\n", last_committed_op_id);
  }

  if (tablet_->table_type() != TableType::TRANSACTION_STATUS_TABLE_TYPE) {
    tablet_->FlushIntentsDbIfNecessary(latest_log_entry_op_id);
    auto max_persistent_op_id = VERIFY_RESULT(
        tablet_->MaxPersistentOpId(true /* invalid_if_no_new_data */));
    if (max_persistent_op_id.regular.valid()) {
      min_index = std::min(min_index, max_persistent_op_id.regular.index);
      if (details) {
        *details += Format("Max persistent regular op id: $0\n", max_persistent_op_id.regular);
      }
    }
    if (max_persistent_op_id.intents.valid()) {
      min_index = std::min(min_index, max_persistent_op_id.intents.index);
      if (details) {
        *details += Format("Max persistent intents op id: $0\n", max_persistent_op_id.intents);
      }
    }
  }

  if (meta_->IsLazySuperblockFlushEnabled()) {
    // Unapplied change metadata operations, if any, are taken into account above. The below
    // takes into accounts any applied but unflushed change metadata operations.

    // TODO(lazy_sb_flush): MinUnflushedChangeMetadataOpId() requires flush_lock_ which can be
    // expensive to get during a superblock flush. Get rid of the below logic, if possible, post
    // https://github.com/yugabyte/yugabyte-db/issues/16684.
    auto min_unflushed_change_metadata_index = meta_->MinUnflushedChangeMetadataOpId().index;
    min_index = std::min(min_index, min_unflushed_change_metadata_index);
    if (details) {
      *details += Format(
          "Min unflushed CHANGE_METADATA_OP index: $0\n", min_unflushed_change_metadata_index);
    }
  }

  if (details) {
    *details += Format("Earliest needed log index: $0\n", min_index);
  }

  return min_index;
}

Result<std::pair<OpId, HybridTime>> TabletPeer::GetOpIdAndSafeTimeForXReplBootstrap() const {
  auto tablet = VERIFY_RESULT(shared_tablet_safe());

  SCHECK_NE(
      tablet->table_type(), TableType::TRANSACTION_STATUS_TABLE_TYPE, IllegalState,
      "Transaction status table cannot be bootstrapped.");

  auto op_id = GetLatestLogEntryOpId();

  // The bootstrap_time is the minium time from which the provided OpId will be transactionally
  // consistent. It is important to call AbortSQLTransactions, which resolves the pending
  // transactions and aborts the active ones. This step synchronizes our clock with the
  // transaction status tablet clock, ensuring that the bootstrap_time we compute later is correct.
  // Ex: Our safe time is 100, and we have a pending intent for which the log got GCed. So this
  // transaction cannot be replicated. If the transaction is still active it needs to be aborted.
  // If, the coordinator is at 110 and the transaction was committed at 105. We need to move our
  // clock to 110 and pick a higher bootstrap_time so that the commit is not part of the bootstrap.
  if (GetAtomicFlag(&FLAGS_abort_active_txns_during_xrepl_bootstrap)) {
    AbortSQLTransactions();
  }
  auto bootstrap_time = VERIFY_RESULT(tablet->SafeTime(RequireLease::kTrue));
  return std::make_pair(std::move(op_id), std::move(bootstrap_time));
}

Status TabletPeer::GetGCableDataSize(int64_t* retention_size) const {
  RETURN_NOT_OK(CheckRunning());
  int64_t min_op_idx = VERIFY_RESULT(GetEarliestNeededLogIndex());
  RETURN_NOT_OK(log_->GetGCableDataSize(min_op_idx, retention_size));
  return Status::OK();
}

log::Log* TabletPeer::log() const {
  Log* log = log_atomic_.load(std::memory_order_acquire);
  LOG_IF_WITH_PREFIX(FATAL, !log) << "log() called before the log instance is initialized.";
  return log;
}

yb::OpId TabletPeer::GetLatestLogEntryOpId() const {
  Log* log = log_atomic_.load(std::memory_order_acquire);
  if (log) {
    return log->GetLatestEntryOpId();
  }
  return yb::OpId();
}

Status TabletPeer::set_cdc_min_replicated_index_unlocked(int64_t cdc_min_replicated_index) {
  VLOG(1) << "Setting cdc min replicated index to " << cdc_min_replicated_index;
  RETURN_NOT_OK(meta_->set_cdc_min_replicated_index(cdc_min_replicated_index));
  Log* log = log_atomic_.load(std::memory_order_acquire);
  if (log) {
    log->set_cdc_min_replicated_index(cdc_min_replicated_index);
  }
  cdc_min_replicated_index_refresh_time_ = MonoTime::Now();
  return Status::OK();
}

Status TabletPeer::set_cdc_min_replicated_index(int64_t cdc_min_replicated_index) {
  std::lock_guard l(cdc_min_replicated_index_lock_);
  return set_cdc_min_replicated_index_unlocked(cdc_min_replicated_index);
}

Status TabletPeer::reset_cdc_min_replicated_index_if_stale() {
  std::lock_guard l(cdc_min_replicated_index_lock_);
  auto seconds_since_last_refresh =
      MonoTime::Now().GetDeltaSince(cdc_min_replicated_index_refresh_time_).ToSeconds();
  if (seconds_since_last_refresh >
      GetAtomicFlag(&FLAGS_cdc_min_replicated_index_considered_stale_secs)) {
    LOG_WITH_PREFIX(INFO) << "Resetting cdc min replicated index. Seconds since last update: "
                          << seconds_since_last_refresh;
    RETURN_NOT_OK(set_cdc_min_replicated_index_unlocked(std::numeric_limits<int64_t>::max()));
  }
  return Status::OK();
}

int64_t TabletPeer::get_cdc_min_replicated_index() {
  return meta_->cdc_min_replicated_index();
}

Status TabletPeer::set_cdc_sdk_min_checkpoint_op_id(const OpId& cdc_sdk_min_checkpoint_op_id) {
  VLOG(1) << "Setting CDCSDK min checkpoint opId to " << cdc_sdk_min_checkpoint_op_id.ToString();
  RETURN_NOT_OK(meta_->set_cdc_sdk_min_checkpoint_op_id(cdc_sdk_min_checkpoint_op_id));
  return Status::OK();
}

Status TabletPeer::set_cdc_sdk_safe_time(const HybridTime& cdc_sdk_safe_time) {
  VLOG(1) << "Setting CDCSDK safe time to " << cdc_sdk_safe_time;
  RETURN_NOT_OK(meta_->set_cdc_sdk_safe_time(cdc_sdk_safe_time));
  return Status::OK();
}

HybridTime TabletPeer::get_cdc_sdk_safe_time() {
  return meta_->cdc_sdk_safe_time();
}

OpId TabletPeer::cdc_sdk_min_checkpoint_op_id() {
  return meta_->cdc_sdk_min_checkpoint_op_id();
}

CoarseTimePoint TabletPeer::cdc_sdk_min_checkpoint_op_id_expiration() {
  auto tablet = shared_tablet();
  if (tablet) {
    auto txn_participant = tablet->transaction_participant();
    if (txn_participant) {
      return txn_participant->GetCheckpointExpirationTime();
    }
  }

  return CoarseTimePoint();
}

bool TabletPeer::is_under_cdc_sdk_replication() {
  return meta_->is_under_cdc_sdk_replication();
}

OpId TabletPeer::GetLatestCheckPoint() {
  auto tablet = shared_tablet();
  if (tablet) {
    auto txn_participant = tablet->transaction_participant();
    if (txn_participant) {
      return txn_participant->GetLatestCheckPoint();
    }
  }
  return OpId();
}

Result<NamespaceId> TabletPeer::GetNamespaceId() {
  auto namespace_id = tablet()->metadata()->namespace_id();
  if (!namespace_id.empty()) {
    return namespace_id;
  }
  // This is empty the first time we try to fetch the namespace id from the tablet metadata, so
  // fetch it from the client and populate the tablet metadata.
  auto* client = client_future().get();
  master::GetNamespaceInfoResponsePB resp;
  auto tablet = VERIFY_RESULT(shared_tablet_safe());
  auto* metadata = tablet->metadata();
  auto namespace_name = metadata->namespace_name();
  auto db_type = YQL_DATABASE_CQL;
  switch (metadata->table_type()) {
    case PGSQL_TABLE_TYPE:
      db_type = YQL_DATABASE_PGSQL;
      break;
    case REDIS_TABLE_TYPE:
      db_type = YQL_DATABASE_REDIS;
      break;
    default:
      db_type = YQL_DATABASE_CQL;
  }

  RETURN_NOT_OK(client->GetNamespaceInfo({} /* namesapce_id */, namespace_name, db_type, &resp));
  namespace_id = resp.namespace_().id();
  if (namespace_id.empty()) {
    return STATUS(IllegalState, Format("Could not get namespace id for $0",
                                       namespace_name));
  }
  RETURN_NOT_OK(metadata->set_namespace_id(namespace_id));
  return namespace_id;
}

Status TabletPeer::SetCDCSDKRetainOpIdAndTime(
    const OpId& cdc_sdk_op_id, const MonoDelta& cdc_sdk_op_id_expiration,
    const HybridTime& cdc_sdk_safe_time) {
  RETURN_NOT_OK(set_cdc_sdk_min_checkpoint_op_id(cdc_sdk_op_id));
  RETURN_NOT_OK(set_cdc_sdk_safe_time(cdc_sdk_safe_time));

  {
    std::lock_guard lock(lock_);
    RETURN_NOT_OK(CheckRunning());
    auto txn_participant = tablet_->transaction_participant();
    if (txn_participant) {
      txn_participant->SetIntentRetainOpIdAndTime(cdc_sdk_op_id, cdc_sdk_op_id_expiration);
    }
  }
  return Status::OK();
}

Result<MonoDelta> TabletPeer::GetCDCSDKIntentRetainTime(const int64_t& cdc_sdk_latest_active_time) {
  MonoDelta cdc_sdk_intent_retention = MonoDelta::kZero;
  // If cdc_sdk_latest_update_time is not updated to default CoarseTimePoint::min() value,
  // It's mean that, no need to retain the intents. This can happen in below case:-
  //      a. Only XCluster streams are defined for the tablet.
  //      b. CDCSDK stream for the tablet is expired.
  if (cdc_sdk_latest_active_time == 0) {
    return cdc_sdk_intent_retention;
  }

  auto txn_participant = VERIFY_RESULT(shared_tablet_safe())->transaction_participant();
  if (txn_participant) {
    // Get the current tablet LEADER's intent retention expiration time.
    // Check how many milliseconds time remaining w.r.t current time, update
    // all the FOLLOWERs as their cdc_sdk_min_checkpoint_op_id_expiration_.
    MonoDelta max_retain_time =
        MonoDelta::FromMilliseconds(GetAtomicFlag(&FLAGS_cdc_intent_retention_ms));
    auto lastest_active_time =
        MonoDelta::FromMicroseconds(GetCurrentTimeMicros() - cdc_sdk_latest_active_time);
    if (max_retain_time >= lastest_active_time) {
      cdc_sdk_intent_retention = max_retain_time - lastest_active_time;
    }
  }
  return cdc_sdk_intent_retention;
}

Result<bool> TabletPeer::SetAllCDCRetentionBarriers(
    int64 cdc_wal_index, OpId cdc_sdk_intents_op_id, MonoDelta cdc_sdk_op_id_expiration,
    HybridTime cdc_sdk_history_cutoff, bool require_history_cutoff,
    bool initial_retention_barrier) {

  auto tablet = VERIFY_RESULT(shared_tablet_safe());
  Log* log = log_atomic_.load(std::memory_order_acquire);

  {
    std::lock_guard lock(cdc_min_replicated_index_lock_);
    cdc_min_replicated_index_refresh_time_ = MonoTime::Now();
  }

  if (initial_retention_barrier) {
    RETURN_NOT_OK(tablet->SetAllInitialCDCRetentionBarriers(
        log, cdc_wal_index, cdc_sdk_intents_op_id, cdc_sdk_history_cutoff,
        require_history_cutoff));
    return true;
  } else {
    return tablet->MoveForwardAllCDCRetentionBarriers(
        log, cdc_wal_index, cdc_sdk_intents_op_id, cdc_sdk_op_id_expiration,
        cdc_sdk_history_cutoff, require_history_cutoff);
  }
}

// Applies to both CDCSDK and XCluster streams attempting to set their initial
// retention barrier
Result<bool> TabletPeer::SetAllInitialCDCRetentionBarriers(
    int64 cdc_wal_index, OpId cdc_sdk_intents_op_id, HybridTime cdc_sdk_history_cutoff,
    bool require_history_cutoff) {

  MonoDelta cdc_sdk_op_id_expiration =
      MonoDelta::FromMilliseconds(GetAtomicFlag(&FLAGS_cdc_intent_retention_ms));
  return SetAllCDCRetentionBarriers(
      cdc_wal_index, cdc_sdk_intents_op_id, cdc_sdk_op_id_expiration, cdc_sdk_history_cutoff,
      require_history_cutoff, true /* initial_retention_barrier */);
}

// Applies only to CDCSDK streams
Result<bool> TabletPeer::SetAllInitialCDCSDKRetentionBarriers(
    OpId cdc_sdk_op_id, HybridTime cdc_sdk_history_cutoff, bool require_history_cutoff) {

  return SetAllInitialCDCRetentionBarriers(
      cdc_sdk_op_id.index, cdc_sdk_op_id, cdc_sdk_history_cutoff, require_history_cutoff);
}

// Applies to the combined requirement of both CDCSDK and XCluster streams.
// UpdatePeersAndMetrics will call this with the strictest requirements
// corresponding to the slowest consumer of this tablet among all streams.
Result<bool> TabletPeer::MoveForwardAllCDCRetentionBarriers(
    int64 cdc_wal_index, OpId cdc_sdk_intents_op_id, MonoDelta cdc_sdk_op_id_expiration,
    HybridTime cdc_sdk_history_cutoff, bool require_history_cutoff) {

  return SetAllCDCRetentionBarriers(
      cdc_wal_index, cdc_sdk_intents_op_id, cdc_sdk_op_id_expiration, cdc_sdk_history_cutoff,
      require_history_cutoff, false /* initial_retention_barrier */);
}

std::unique_ptr<Operation> TabletPeer::CreateOperation(consensus::LWReplicateMsg* replicate_msg) {
  // TODO: handle cases where tablet is unset safely.
  auto tablet = CHECK_RESULT(shared_tablet_safe());
  switch (replicate_msg->op_type()) {
    case consensus::WRITE_OP:
      DCHECK(replicate_msg->has_write()) << "WRITE_OP replica"
          " operation must receive a WriteRequestPB";
      // We use separate preparing token only on leader, so here it could be empty.
      return std::make_unique<WriteOperation>(tablet);

    case consensus::CHANGE_METADATA_OP:
      DCHECK(replicate_msg->has_change_metadata_request()) << "CHANGE_METADATA_OP replica"
          " operation must receive an ChangeMetadataRequestPB";
      return std::make_unique<ChangeMetadataOperation>(tablet, log());

    case consensus::UPDATE_TRANSACTION_OP:
      DCHECK(replicate_msg->has_transaction_state()) << "UPDATE_TRANSACTION_OP replica"
          " operation must receive an TransactionStatePB";
      return std::make_unique<UpdateTxnOperation>(tablet);

    case consensus::TRUNCATE_OP:
      DCHECK(replicate_msg->has_truncate()) << "TRUNCATE_OP replica"
          " operation must receive an TruncateRequestPB";
      return std::make_unique<TruncateOperation>(tablet);

    case consensus::SNAPSHOT_OP:
       DCHECK(replicate_msg->has_snapshot_request()) << "SNAPSHOT_OP replica"
          " operation must receive an TabletSnapshotOpRequestPB";
      return std::make_unique<SnapshotOperation>(tablet);

    case consensus::HISTORY_CUTOFF_OP:
       DCHECK(replicate_msg->has_history_cutoff()) << "HISTORY_CUTOFF_OP replica"
          " transaction must receive an HistoryCutoffPB";
      return std::make_unique<HistoryCutoffOperation>(tablet);

    case consensus::SPLIT_OP:
       DCHECK(replicate_msg->has_split_request()) << "SPLIT_OP replica"
          " operation must receive an SplitOpRequestPB";
      return std::make_unique<SplitOperation>(tablet, tablet_splitter_);

    case consensus::CHANGE_AUTO_FLAGS_CONFIG_OP:
      DCHECK(replicate_msg->has_auto_flags_config())
          << "CHANGE_AUTO_FLAGS_CONFIG_OP replica"
             " operation must receive an AutoFlagsConfigPB";
      return std::make_unique<ChangeAutoFlagsConfigOperation>(
          tablet, replicate_msg->mutable_auto_flags_config());

    case consensus::UNKNOWN_OP: FALLTHROUGH_INTENDED;
    case consensus::NO_OP: FALLTHROUGH_INTENDED;
    case consensus::CHANGE_CONFIG_OP:
      FATAL_INVALID_ENUM_VALUE(consensus::OperationType, replicate_msg->op_type());
  }
  FATAL_INVALID_ENUM_VALUE(consensus::OperationType, replicate_msg->op_type());
}

Status TabletPeer::StartReplicaOperation(
    const scoped_refptr<ConsensusRound>& round, HybridTime propagated_safe_time) {
  RaftGroupStatePB value = state();
  if (value != RaftGroupStatePB::RUNNING && value != RaftGroupStatePB::BOOTSTRAPPING) {
    return STATUS(IllegalState, RaftGroupStatePB_Name(value));
  }

  auto* replicate_msg = round->replicate_msg().get();
  DCHECK(replicate_msg->has_hybrid_time());
  auto operation = CreateOperation(replicate_msg);

  // TODO(todd) Look at wiring the stuff below on the driver
  // It's imperative that we set the round here on any type of operation, as this
  // allows us to keep the reference to the request in the round instead of copying it.
  operation->set_consensus_round(round);
  HybridTime ht(replicate_msg->hybrid_time());
  operation->set_hybrid_time(ht);
  clock_->Update(ht);

  // This sets the monotonic counter to at least replicate_msg.monotonic_counter() atomically.
  tablet_->UpdateMonotonicCounter(replicate_msg->monotonic_counter());

  auto* operation_ptr = operation.get();
  OperationDriverPtr driver = VERIFY_RESULT(NewReplicaOperationDriver(&operation));

  operation_ptr->consensus_round()->SetCallback(driver.get());

  if (propagated_safe_time) {
    driver->SetPropagatedSafeTime(propagated_safe_time, tablet_->mvcc_manager());
  }

  driver->ExecuteAsync();
  return Status::OK();
}

void TabletPeer::SetPropagatedSafeTime(HybridTime ht) {
  auto driver = NewReplicaOperationDriver(nullptr);
  if (!driver.ok()) {
    LOG_WITH_PREFIX(ERROR) << "Failed to create operation driver to set propagated hybrid time";
    return;
  }
  (**driver).SetPropagatedSafeTime(ht, tablet_->mvcc_manager());
  (**driver).ExecuteAsync();
}

bool TabletPeer::ShouldApplyWrite() {
  return tablet_->ShouldApplyWrite();
}

Result<std::shared_ptr<consensus::Consensus>> TabletPeer::GetConsensus() const {
  return GetRaftConsensus();
}

Result<shared_ptr<consensus::RaftConsensus>> TabletPeer::GetRaftConsensus() const {
  std::lock_guard lock(lock_);
  // Cannot use NotFound status for the shutting down case as later the status may be extended with
  // TabletServerErrorPB::TABLET_NOT_RUNNING error code, and this combination of the status and
  // the code is not expected and is not considered as a retryable operation at least by yb-client.
  // Refer to https://github.com/yugabyte/yugabyte-db/issues/19033 for the details.
  SCHECK(!IsShutdownStarted(), IllegalState, "Tablet peer $0 is shutting down", LogPrefix());
  SCHECK(consensus_, IllegalState, "Tablet peer $0 is not started yet", LogPrefix());
  return consensus_;
}

shared_ptr<consensus::RaftConsensus> TabletPeer::GetRaftConsensusUnsafe() const {
  std::lock_guard lock(lock_);
  return consensus_;
}

std::shared_ptr<RetryableRequestsFlusher> TabletPeer::shared_retryable_requests_flusher() const {
  std::lock_guard lock(lock_);
  return retryable_requests_flusher_;
}

Result<OperationDriverPtr> TabletPeer::NewLeaderOperationDriver(
    std::unique_ptr<Operation>* operation, int64_t term) {
  if (term == OpId::kUnknownTerm) {
    return STATUS(InvalidArgument, "Leader operation driver for unknown term");
  }
  return NewOperationDriver(operation, term);
}

Result<OperationDriverPtr> TabletPeer::NewReplicaOperationDriver(
    std::unique_ptr<Operation>* operation) {
  return NewOperationDriver(operation, OpId::kUnknownTerm);
}

Result<OperationDriverPtr> TabletPeer::NewOperationDriver(std::unique_ptr<Operation>* operation,
                                                          int64_t term) {
  auto operation_driver = CreateOperationDriver();
  RETURN_NOT_OK(operation_driver->Init(operation, term));
  return operation_driver;
}

void TabletPeer::RegisterMaintenanceOps(MaintenanceManager* maint_mgr) {
  // Taking state_change_lock_ ensures that we don't shut down concurrently with
  // this last start-up task.
  // Note that the state_change_lock_ is taken in Shutdown(),
  // prior to calling UnregisterMaintenanceOps().

  std::lock_guard l(state_change_lock_);

  if (state() != RaftGroupStatePB::RUNNING) {
    LOG_WITH_PREFIX(WARNING) << "Not registering maintenance operations: tablet not RUNNING";
    return;
  }

  DCHECK(maintenance_ops_.empty());

  auto tablet_result = shared_tablet_safe();
  if (!tablet_result.ok()) {
    LOG_WITH_PREFIX(WARNING) << "Not registering maintenance operations: "
                             << tablet_result.status();
    return;
  }
  auto log_gc = std::make_unique<LogGCOp>(this, *tablet_result);
  maint_mgr->RegisterOp(log_gc.get());
  maintenance_ops_.push_back(std::move(log_gc));
  LOG_WITH_PREFIX(INFO) << "Registered log gc";
}

void TabletPeer::UnregisterMaintenanceOps() {
  DCHECK(state_change_lock_.is_locked());
  for (auto& op : maintenance_ops_) {
    op->Unregister();
  }
  maintenance_ops_.clear();
}

TabletOnDiskSizeInfo TabletPeer::GetOnDiskSizeInfo() const {
  TabletOnDiskSizeInfo info;

  if (consensus_) {
    info.consensus_metadata_disk_size = consensus_->OnDiskSize();
  }

  if (tablet_) {
    info.sst_files_disk_size = tablet_->GetCurrentVersionSstFilesSize();
    info.uncompressed_sst_files_disk_size =
        tablet_->GetCurrentVersionSstFilesUncompressedSize();
  }

  auto log = log_atomic_.load(std::memory_order_acquire);
  if (log) {
    info.wal_files_disk_size = log->OnDiskSize();
  }

  info.RecomputeTotalSize();
  return info;
}

size_t TabletPeer::GetNumLogSegments() const {
  auto log = log_atomic_.load(std::memory_order_acquire);
  return log ? log->num_segments() : 0;
}

std::string TabletPeer::LogPrefix() const {
  return Substitute("T $0 P $1 [state=$2]: ",
      tablet_id_, permanent_uuid_, RaftGroupStatePB_Name(state()));
}

scoped_refptr<OperationDriver> TabletPeer::CreateOperationDriver() {
  return scoped_refptr<OperationDriver>(new OperationDriver(
      &operation_tracker_,
      GetRaftConsensusUnsafe().get(),
      prepare_thread_.get(),  // May be nullptr
      tablet_->table_type()));
}

Result<client::YBClient*> TabletPeer::client() const {
  auto cached_value = client_cache_.load(std::memory_order_acquire);
  if (cached_value != nullptr) {
    return cached_value;
  }
  auto future_status = client_future_.wait_for(
      TransactionRpcTimeout().ToSteadyDuration());
  if (future_status != std::future_status::ready) {
    return STATUS(TimedOut, "Client not ready");
  }
  auto result = client_future_.get();
  client_cache_.store(result, std::memory_order_release);
  return result;
}

int64_t TabletPeer::LeaderTerm() const {
  auto consensus = GetRaftConsensusUnsafe();
  return consensus ? consensus->LeaderTerm() : yb::OpId::kUnknownTerm;
}

Result<HybridTime> TabletPeer::LeaderSafeTime() const {
  return tablet_->SafeTime();
}

consensus::LeaderStatus TabletPeer::LeaderStatus(bool allow_stale) const {
  auto consensus = GetRaftConsensusUnsafe();
  return consensus ? consensus->GetLeaderStatus(allow_stale) : consensus::LeaderStatus::NOT_LEADER;
}

bool TabletPeer::IsLeaderAndReady() const {
  return LeaderStatus() == consensus::LeaderStatus::LEADER_AND_READY;
}

bool TabletPeer::IsNotLeader() const {
  return LeaderStatus() == consensus::LeaderStatus::NOT_LEADER;
}

Result<HybridTime> TabletPeer::HtLeaseExpiration() const {
  auto consensus = VERIFY_RESULT(GetRaftConsensus());
  HybridTime result(
      CHECK_RESULT(consensus->MajorityReplicatedHtLeaseExpiration(0, CoarseTimePoint::max())), 0);
  return std::max(result, tablet_->mvcc_manager()->LastReplicatedHybridTime());
}

TableType TabletPeer::TEST_table_type() {
  auto tablet_result = shared_tablet_safe();
  if (!tablet_result.ok()) {
    LOG(WARNING) << "Cannot determine table type: " << tablet_result.status();
    return static_cast<TableType>(-1);
  }
  return (*tablet_result)->table_type();
}

void TabletPeer::SetFailed(const Status& error) {
  DCHECK(error_.get(std::memory_order_acquire) == nullptr);
  error_ = MakeAtomicUniquePtr<Status>(error);
  auto state = state_.load(std::memory_order_acquire);
  while (state != RaftGroupStatePB::FAILED && state != RaftGroupStatePB::QUIESCING &&
         state != RaftGroupStatePB::SHUTDOWN) {
    if (state_.compare_exchange_weak(state, RaftGroupStatePB::FAILED, std::memory_order_acq_rel)) {
      LOG_WITH_PREFIX(INFO) << "Changed state from " << RaftGroupStatePB_Name(state)
                            << " to FAILED";
      break;
    }
  }
}

Status TabletPeer::UpdateState(RaftGroupStatePB expected, RaftGroupStatePB new_state,
                               const std::string& error_message) {
  RaftGroupStatePB old = expected;
  if (!state_.compare_exchange_strong(old, new_state, std::memory_order_acq_rel)) {
    return STATUS_FORMAT(
        InvalidArgument, "$0 Expected state: $1, got: $2",
        error_message, RaftGroupStatePB_Name(expected), RaftGroupStatePB_Name(old));
  }

  LOG_WITH_PREFIX(INFO) << "Changed state from " << RaftGroupStatePB_Name(old) << " to "
                        << RaftGroupStatePB_Name(new_state);
  return Status::OK();
}

void TabletPeer::Enqueue(rpc::ThreadPoolTask* task) {
  rpc::ThreadPool* thread_pool = service_thread_pool_.load(std::memory_order_acquire);
  if (!thread_pool) {
    task->Done(STATUS(Aborted, "Thread pool not ready"));
    return;
  }

  thread_pool->Enqueue(task);
}

void TabletPeer::StrandEnqueue(rpc::StrandTask* task) {
  rpc::Strand* strand = strand_.get();
  if (!strand) {
    task->Done(STATUS(Aborted, "Thread pool not ready"));
    return;
  }

  strand->Enqueue(task);
}

bool TabletPeer::CanBeDeleted() {
  const auto consensus = GetRaftConsensusUnsafe();
  if (!consensus || consensus->LeaderTerm() == OpId::kUnknownTerm) {
    return false;
  }

  const auto tablet = shared_tablet();
  if (!tablet) {
    return false;
  }

  auto op_id = tablet->metadata()->GetOpIdToDeleteAfterAllApplied();
  if (!op_id.valid()) {
    return false;
  }

  const auto all_applied_op_id = consensus.get()->GetAllAppliedOpId();
  if (all_applied_op_id < op_id) {
    return false;
  }

  LOG_WITH_PREFIX(INFO) << Format(
      "Marked tablet $0 as requiring cleanup due to all replicas have been split (all applied op "
      "id: $1, split op id: $2, data state: $3)",
      tablet_id(), all_applied_op_id, op_id, TabletDataState_Name(data_state()));

  return true;
}

rpc::Scheduler& TabletPeer::scheduler() const {
  return messenger_->scheduler();
}

// Called from within RemoteBootstrapSession and RemoteBootstrapServiceImpl.
Status TabletPeer::ChangeRole(const std::string& requestor_uuid) {
  MAYBE_FAULT(FLAGS_TEST_fault_crash_leader_before_changing_role);
  auto consensus = VERIFY_RESULT_PREPEND(GetConsensus(), "Unable to change role for tablet peer");

  // If peer being bootstrapped is already a VOTER, don't send the ChangeConfig request. This could
  // happen when a tserver that is already a VOTER in the configuration tombstones its tablet, and
  // the leader starts bootstrapping it.
  const auto config = consensus->CommittedConfig();
  for (const RaftPeerPB& peer_pb : config.peers()) {
    if (peer_pb.permanent_uuid() != requestor_uuid) {
      continue;
    }

    switch (peer_pb.member_type()) {
      case PeerMemberType::OBSERVER:
        FALLTHROUGH_INTENDED;
      case PeerMemberType::VOTER:
        LOG(ERROR) << "Peer " << peer_pb.permanent_uuid() << " is a "
                   << PeerMemberType_Name(peer_pb.member_type())
                   << " Not changing its role after remote bootstrap";

        // Even though this is an error, we return Status::OK() so the remote server doesn't
        // tombstone its tablet.
        return Status::OK();

      case PeerMemberType::PRE_OBSERVER:
        FALLTHROUGH_INTENDED;
      case PeerMemberType::PRE_VOTER: {
        consensus::ChangeConfigRequestPB req;
        consensus::ChangeConfigResponsePB resp;

        req.set_tablet_id(tablet_id());
        req.set_type(consensus::CHANGE_ROLE);
        RaftPeerPB* peer = req.mutable_server();
        peer->set_permanent_uuid(requestor_uuid);

        boost::optional<TabletServerErrorPB::Code> error_code;
        return consensus->ChangeConfig(req, &DoNothingStatusCB, &error_code);
      }
      case PeerMemberType::UNKNOWN_MEMBER_TYPE:
        return STATUS(
            IllegalState,
            Substitute(
                "Unable to change role for peer $0 in config for "
                "tablet $1. Peer has an invalid member type $2",
                peer_pb.permanent_uuid(), tablet_id(), PeerMemberType_Name(peer_pb.member_type())));
    }
    LOG(FATAL) << "Unexpected peer member type " << PeerMemberType_Name(peer_pb.member_type());
  }
  return STATUS(
      IllegalState,
      Substitute("Unable to find peer $0 in config for tablet $1", requestor_uuid, tablet_id()));
}

void TabletPeer::EnableFlushRetryableRequests() {
  flush_retryable_requests_enabled_.store(true, std::memory_order_relaxed);
}

bool TabletPeer::FlushRetryableRequestsEnabled() const {
  return GetAtomicFlag(&FLAGS_enable_flush_retryable_requests) &&
      flush_retryable_requests_enabled_.load(std::memory_order_relaxed);
}

Result<consensus::RetryableRequests> TabletPeer::GetRetryableRequests() {
  return VERIFY_RESULT(GetRaftConsensus())->GetRetryableRequests();
}

Status TabletPeer::FlushRetryableRequests() {
  if (!FlushRetryableRequestsEnabled()) {
    return STATUS(NotSupported, "flush_retryable_requests is not supported");
  }
  auto retryable_requests_flusher = shared_retryable_requests_flusher();
  SCHECK_FORMAT(retryable_requests_flusher,
                IllegalState,
                "Tablet $0 retryable_requests_flusher not initialized",
                tablet_id_);
  return retryable_requests_flusher->FlushRetryableRequests();
}

Result<OpId> TabletPeer::CopyRetryableRequestsTo(const std::string& dest_path) {
  if (!FlushRetryableRequestsEnabled()) {
    return STATUS(NotSupported, "flush_retryable_requests is not supported");
  }
  auto retryable_requests_flusher = shared_retryable_requests_flusher();
  SCHECK_FORMAT(retryable_requests_flusher,
                IllegalState,
                "Tablet $0 retryable_requests_flusher not initialized",
                tablet_id_);
  return retryable_requests_flusher->CopyRetryableRequestsTo(dest_path);
}

Status TabletPeer::SubmitFlushRetryableRequestsTask() {
  if (!FlushRetryableRequestsEnabled()) {
    return STATUS(NotSupported, "flush_retryable_requests is not supported");
  }
  auto retryable_requests_flusher = shared_retryable_requests_flusher();
  SCHECK_FORMAT(retryable_requests_flusher,
                IllegalState,
                "Tablet $0 retryable_requests_flusher not initialized",
                tablet_id_);
  return retryable_requests_flusher->SubmitFlushRetryableRequestsTask();
}

bool TabletPeer::TEST_HasRetryableRequestsOnDisk() {
  if (!FlushRetryableRequestsEnabled()) {
    return false;
  }
  auto retryable_requests_flusher = shared_retryable_requests_flusher();
  return retryable_requests_flusher
      ? retryable_requests_flusher->TEST_HasRetryableRequestsOnDisk()
      : false;
}

RetryableRequestsFlushState TabletPeer::TEST_RetryableRequestsFlusherState() const {
  if (!FlushRetryableRequestsEnabled()) {
    return RetryableRequestsFlushState::kFlushIdle;
  }
  auto retryable_requests_flusher = shared_retryable_requests_flusher();
  return retryable_requests_flusher
      ? retryable_requests_flusher->flush_state()
      : RetryableRequestsFlushState::kFlushIdle;
}

Preparer* TabletPeer::DEBUG_GetPreparer() { return prepare_thread_.get(); }

}  // namespace tablet
}  // namespace yb
