// Copyright (c) YugabyteDB, Inc.
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

#include "yb/ash/wait_state.h"

#include <arpa/inet.h>

#include "yb/util/debug-util.h"
#include "yb/util/tostring.h"
#include "yb/util/trace.h"

// The reason to include yb_ash_enable_infra in this file and not
// pg_wrapper.cc:
//
// The runtime gFlag yb_enable_ash should only be enabled if the
// non-runtime gFlag yb_ash_enable_infra is true. Postgres GUC
// check framework is used to enforce this check. But if both the flags
// are to be enabled at startup, yb_ash_enable_infra must be registered
// first, otherwise the check will incorrectly fail.
//
// Postmaster processes the list of GUCs twice, once directly from the arrays
// in guc.c and once from the config file that WriteConfigFile() writes into.
// AppendPgGFlags() decides the order of Pg gFlags that are going to be written
// in the same order that GetAllFlags() returns, and GetAllFlags() sorts it
// internally by the filename (which includes some parts of the filepath as well)
// and since this is in the folder 'ash', which is lexicographically smaller than
// the folder 'yql', the flags of this file will be written to the config file
// before the flags of pg_wrapper.cc and, and hence processed first by postmaster.
// In the same file, the flags will be sorted lexicographically based on their
// names, so yb_ash_enable_infra will come before yb_enable_ash.
//
// So, to ensure that the GUC check hook doesn't fail, these two flags are
// defined here. Both the flags are not defined in pg_wrapper.cc since yb_enable_ash
// is required in other parts of the code as well like cql_server.cc and yb_rpc.cc.

DEFINE_NON_RUNTIME_PG_PREVIEW_FLAG(bool, yb_ash_enable_infra, false,
    "Allocate shared memory for ASH, start the background worker, create "
    "instrumentation hooks and enable querying the yb_active_session_history "
    "view.");

DEFINE_RUNTIME_PG_PREVIEW_FLAG(bool, yb_enable_ash, false,
    "Starts sampling and instrumenting YSQL and YCQL queries, "
    "and various background activities. This does nothing if "
    "ysql_yb_enable_ash_infra is disabled.");

DEFINE_NON_RUNTIME_PG_FLAG(int32, yb_ash_circular_buffer_size, 16 * 1024,
    "Size (in KiBs) of ASH circular buffer that stores the samples");

DEFINE_RUNTIME_PG_FLAG(int32, yb_ash_sampling_interval_ms, 1000,
    "Time (in milliseconds) between two consecutive sampling events");
DEPRECATE_FLAG(int32, ysql_yb_ash_sampling_interval, "2024_03");

DEFINE_RUNTIME_PG_FLAG(int32, yb_ash_sample_size, 500,
    "Number of samples captured from each component per sampling event");

DEFINE_test_flag(bool, export_wait_state_names, yb::kIsDebug,
    "Exports wait-state name as a human understandable string.");
DEFINE_test_flag(bool, trace_ash_wait_code_updates, yb::kIsDebug,
    "Add a trace line whenever the wait state code is updated.");
DEFINE_test_flag(uint32, yb_ash_sleep_at_wait_state_ms, 0,
    "How long to sleep/delay when entering a particular wait state.");
DEFINE_test_flag(uint32, yb_ash_wait_code_to_sleep_at, 0,
    "If enabled, add a sleep/delay when we enter the specified wait state.");
DEPRECATE_FLAG(bool, TEST_export_ash_uuids_as_hex_strings, "04_2024");
DEFINE_test_flag(bool, ash_debug_aux, false, "Set ASH aux_info to the first 16 characters"
    " of the method tserver is running");
DEFINE_test_flag(bool, ash_fetch_wait_states_for_raft_log, true, "Should ASH fetch "
      "background task states, such as raft log sync/append.");
DEFINE_test_flag(bool, ash_fetch_wait_states_for_rocksdb_flush_and_compaction, true,
      "Should ASH fetch background task states, such as rocksdb flush and compaction.");

namespace yb::ash {

namespace {

// The current wait_state_ for this thread.
thread_local WaitStateInfoPtr threadlocal_wait_state_;
std::atomic_bool TEST_entered_wait_state_code_for_sleep{false};

void MaybeSleepForTests(WaitStateInfo* state, WaitStateCode c) {
  if (FLAGS_TEST_yb_ash_wait_code_to_sleep_at != to_underlying(c)) {
    return;
  }

  TEST_entered_wait_state_code_for_sleep.store(true, std::memory_order_release);
  auto sleep_time_ms = FLAGS_TEST_yb_ash_sleep_at_wait_state_ms;
  if (sleep_time_ms <= 0) {
    return;
  }

  if (!state) {
    YB_LOG_EVERY_N_SECS(ERROR, 5) << __func__ << " skipping sleep because WaitStateInfo is null";
    return;
  }

  state->TEST_SleepForTests(sleep_time_ms);
}

std::string GetWaitStateDescription(WaitStateCode code) {
  switch (code) {
    case WaitStateCode::kUnused:
      return "This should not be present in view.";
    case WaitStateCode::kYSQLReserved:
      return "This is just a placeholder here for a wait event defined in PG.";
    case WaitStateCode::kCatalogRead:
      return "A YSQL backend is waiting for a catalog read from master.";
    case WaitStateCode::kIndexRead:
      return "A YSQL backend is waiting for a secondary index read from DocDB.";
    case WaitStateCode::kStorageRead:
      return "A YSQL backend is waiting for a table read from DocDB.";
    case WaitStateCode::kStorageFlush:
      return "A YSQL backend is waiting for a table/index read/write from DocDB.";
    case WaitStateCode::kOnCpu_Active:
      return "A rpc/task is being actively processed on a thread.";
    case WaitStateCode::kOnCpu_Passive:
      return "A rpc/task is waiting for a thread to pick it up.";
    case WaitStateCode::kIdle:
      return "The Raft log appender/sync thread is idle.";
    case WaitStateCode::kRpc_Done:
      return "An rpc is done and waiting for the reactor to send the response to a "
          "YSQL/YCQL backend.";
    case WaitStateCode::kDeprecated_Rpcs_WaitOnMutexInShutdown:
      return "An rpc is waiting for the messenger to shutdown (Deprecated).";
    case WaitStateCode::kRetryableRequests_SaveToDisk:
      return "The in-memory state of the retryable requests is being saved to the disk. "
          "This generally happens in the background during a WAL log roll, or remote bootstrap.";
    case WaitStateCode::kMVCC_WaitForSafeTime:
      return "A read/write rpc is waiting for the safe time to be at least the desired read-time.";
    case WaitStateCode::kLockedBatchEntry_Lock:
      return "A read/write rpc is waiting for a DocDB row level lock.";
    case WaitStateCode::kBackfillIndex_WaitForAFreeSlot:
      return "A backfill index rpc is waiting for a slot to open if there are too many backfill "
          "requests at the same time.";
    case WaitStateCode::kCreatingNewTablet:
      return "The CreateTablet RPC is creating a new tablet, this may involve writing metadata "
          "files, causing I/O wait.";
    case WaitStateCode::kSaveRaftGroupMetadataToDisk:
      return "The Raft/tablet metadata is being written to disk. Generally during "
          "snapshot/restore operations.";
    case WaitStateCode::kTransactionStatusCache_DoGetCommitData:
      return "An rpc needs to look up the commit status of a particular transaction.";
    case WaitStateCode::kWaitForYSQLBackendsCatalogVersion:
      return "CREATE INDEX is waiting for YSQL backends to have up-to-date pg_catalog.";
    case WaitStateCode::kWriteSysCatalogSnapshotToDisk:
      return "Writing initial sys catalog snapshot during initdb.";
    case WaitStateCode::kDumpRunningRpc_WaitOnReactor:
      return "DumpRunningRpcs is waiting on reactor threads.";
    case WaitStateCode::kConflictResolution_ResolveConficts:
      return "A read/write rpc is waiting to identify conflicting transactions.";
    case WaitStateCode::kConflictResolution_WaitOnConflictingTxns:
      return "A read/write rpc is waiting for conflicting transactions to complete.";
    case WaitStateCode::kRaft_WaitingForReplication:
      return "A write rpc is waiting for Raft replication.";
    case WaitStateCode::kRaft_ApplyingEdits:
      return "A write rpc is applying Raft edits locally.";
    case WaitStateCode::kWAL_Append:
      return "A write rpc is persisting WAL edits.";
    case WaitStateCode::kWAL_Sync:
      return "A write rpc is synchronizing WAL edits.";
    case WaitStateCode::kConsensusMeta_Flush:
      return "ConsensusMetadata is flushed, say during Raft term/config change or remote "
          "bootstrap etc.";
    case WaitStateCode::kReplicaState_TakeUpdateLock:
      return "A write/alter RPC needs to wait for the ReplicaState lock to replicate a "
          "batch of writes through Raft.";
    case WaitStateCode::kRocksDB_ReadBlockFromFile:
      return "RocksDB is reading a block from a file.";
    case WaitStateCode::kRocksDB_OpenFile:
      return "RocksDB is opening a file.";
    case WaitStateCode::kRocksDB_WriteToFile:
      return "RocksDB is writing to a file.";
    case WaitStateCode::kRocksDB_Flush:
      return "RocksDB is doing a flush.";
    case WaitStateCode::kRocksDB_Compaction:
      return "RocksDB is doing a compaction.";
    case WaitStateCode::kRocksDB_PriorityThreadPoolTaskPaused:
      return "RocksDB paused one of flush/compaction tasks for another one with a higher "
          "priority.";
    case WaitStateCode::kRocksDB_CloseFile:
      return "RocksDB is closing a file.";
    case WaitStateCode::kRocksDB_RateLimiter:
      return "RocksDB flush/compaction is slowing down due to rate limiter throttling "
          "access to disk.";
    case WaitStateCode::kRocksDB_WaitForSubcompaction:
      return "RocksDB is waiting for a compaction to complete.";
    case WaitStateCode::kRocksDB_NewIterator:
      return "RocksDB is waiting for a new iterator to be created.";
    case WaitStateCode::kYCQL_Parse:
      return "YCQL is parsing a query.";
    case WaitStateCode::kYCQL_Read:
      return "YCQL is processing a read query.";
    case WaitStateCode::kYCQL_Write:
      return "YCQL is processing a write query.";
    case WaitStateCode::kYCQL_Analyze:
      return "YCQL is analyzing a query.";
    case WaitStateCode::kYCQL_Execute:
      return "YCQL is executing a query.";
    case WaitStateCode::kYBClient_WaitingOnDocDB:
      return "YB Client is waiting on DocDB to return a response.";
    case WaitStateCode::kYBClient_LookingUpTablet:
      return "YB Client is looking up tablet information from the master.";
  }
  FATAL_INVALID_ENUM_VALUE(WaitStateCode, code);
}

}  // namespace

void WaitStateInfo::VTraceTo(Trace* trace, int level, GStringPiece data) {
  VTRACE_TO(level, trace ? trace : Trace::CurrentTrace(), data);
}

void WaitStateInfo::TEST_SleepForTests(uint32_t sleep_time_ms) {
  // An RPC may enter a specific wait state multiple times. In order to limit the total
  // delay caused to an RPC, we will reduce sleep time by a factor of 2 for each sleep.
  // 1 + 1/2 + 1/4 + ... < 2. Thus, the total delay incurred by any RPC will be bounded by
  // 2 * sleep_time_ms.
  auto decayed_sleep_time_ms = sleep_time_ms >> TEST_num_sleeps_;
  if (decayed_sleep_time_ms == 0) {
    return;
  }
  SleepFor(MonoDelta::FromMilliseconds(decayed_sleep_time_ms));
  if (FLAGS_TEST_trace_ash_wait_code_updates) {
    VTrace(0, yb::Format("Slept for $0 ms.", decayed_sleep_time_ms));
  }
  VLOG_IF(1, TEST_num_sleeps_ == 0) << "Sleeping at " << yb::GetStackTrace();
  TEST_num_sleeps_++;
}

bool WaitStateInfo::TEST_EnteredSleep() {
  return TEST_entered_wait_state_code_for_sleep.load(std::memory_order_acquire);
}

void AshMetadata::set_client_host_port(const HostPort &host_port) {
  client_host_port = host_port;
}

std::string AshMetadata::ToString() const {
  return YB_STRUCT_TO_STRING(
      yql_endpoint_tserver_uuid, root_request_id, query_id, database_id,
      rpc_request_id, client_host_port);
}

std::string AshAuxInfo::ToString() const {
  return YB_STRUCT_TO_STRING(table_id, tablet_id, method);
}

void AshAuxInfo::UpdateFrom(const AshAuxInfo &other) {
  if (!other.tablet_id.empty()) {
    tablet_id = other.tablet_id;
  }
  if (!other.table_id.empty()) {
    table_id = other.table_id;
  }
  if (!other.method.empty()) {
    method = other.method;
  }
}

WaitStateInfo::WaitStateInfo()
    : metadata_(AshMetadata{}) {}

void WaitStateInfo::set_code(WaitStateCode code, const char* location) {
  if (FLAGS_TEST_trace_ash_wait_code_updates) {
    if (FLAGS_tracing_level >= 1) {
      VTrace(1, yb::Format("$0 at $1", ash::ToString(code), location));
    } else {
      VTrace(0, yb::Format("$0", ash::ToString(code)));
    }
  }
  code_ = code;
  MaybeSleepForTests(this, code);
}

WaitStateCode WaitStateInfo::code() const {
  return code_;
}

std::atomic<WaitStateCode>& WaitStateInfo::mutable_code() {
  return code_;
}

std::string WaitStateInfo::ToString() const {
  std::lock_guard lock(mutex_);
  return YB_CLASS_TO_STRING(metadata, code, aux_info);
}

void WaitStateInfo::set_rpc_request_id(int64_t rpc_request_id) {
  std::lock_guard lock(mutex_);
  metadata_.rpc_request_id = rpc_request_id;
}

int64_t WaitStateInfo::rpc_request_id() {
  std::lock_guard lock(mutex_);
  return metadata_.rpc_request_id;
}

void WaitStateInfo::set_root_request_id(const Uuid &root_request_id) {
  std::lock_guard lock(mutex_);
  metadata_.root_request_id = root_request_id;
}

void WaitStateInfo::set_query_id(uint64_t query_id) {
  std::lock_guard lock(mutex_);
  metadata_.query_id = query_id;
}

uint64_t WaitStateInfo::query_id() {
  std::lock_guard lock(mutex_);
  return metadata_.query_id;
}

void WaitStateInfo::set_client_host_port(const HostPort &host_port) {
  std::lock_guard lock(mutex_);
  metadata_.set_client_host_port(host_port);
}

void WaitStateInfo::set_yql_endpoint_tserver_uuid(const Uuid &yql_endpoint_tserver_uuid) {
  std::lock_guard lock(mutex_);
  metadata_.yql_endpoint_tserver_uuid = yql_endpoint_tserver_uuid;
}

void WaitStateInfo::UpdateMetadata(const AshMetadata &meta) {
  std::lock_guard lock(mutex_);
  metadata_.UpdateFrom(meta);
}

void WaitStateInfo::UpdateAuxInfo(const AshAuxInfo &aux) {
  std::lock_guard lock(mutex_);
  aux_info_.UpdateFrom(aux);
}

void WaitStateInfo::SetCurrentWaitState(WaitStateInfoPtr wait_state) {
  threadlocal_wait_state_ = std::move(wait_state);
}

const WaitStateInfoPtr& WaitStateInfo::CurrentWaitState() {
  if (!threadlocal_wait_state_) {
    VLOG_WITH_FUNC(3) << "returning nullptr";
  }
  return threadlocal_wait_state_;
}

void WaitStateInfo::EnableConcurrentUpdates() {
  concurrent_updates_allowed_ = true;
  if (FLAGS_TEST_trace_ash_wait_code_updates) {
    VTrace(0, yb::Format("Enabling concurrent updates"));
  }
}

bool WaitStateInfo::IsConcurrentUpdatesEnabled() {
  return concurrent_updates_allowed_;
}

void EnableConcurrentUpdates(const WaitStateInfoPtr& ptr) {
  if (ptr) {
    ptr->EnableConcurrentUpdates();
  }
}

std::vector<WaitStatesDescription> WaitStateInfo::GetWaitStatesDescription() {
  std::vector<WaitStatesDescription> desc;
  for (const auto& code : WaitStateCodeList()) {
    // These shouldn't be seen in the view
    if (code == WaitStateCode::kUnused || code == WaitStateCode::kYSQLReserved)
      continue;
    desc.emplace_back(code, GetWaitStateDescription(code));
  }
  return desc;
}

//
// ScopedAdoptWaitState
//
ScopedAdoptWaitState::ScopedAdoptWaitState(WaitStateInfoPtr wait_state)
    : prev_state_(WaitStateInfo::CurrentWaitState()) {
  WaitStateInfo::SetCurrentWaitState(std::move(wait_state));
}

ScopedAdoptWaitState::~ScopedAdoptWaitState() {
  WaitStateInfo::SetCurrentWaitState(std::move(prev_state_));
}

//
// ScopedWaitStatus
//
ScopedWaitStatus::ScopedWaitStatus(WaitStateCode code, const char* location)
    : code_(code),
      location_(location),
      prev_code_(
          WaitStateInfo::CurrentWaitState()
              ? WaitStateInfo::CurrentWaitState()->mutable_code().exchange(code_)
              : code_) {
  const auto& wait_state = WaitStateInfo::CurrentWaitState();
  if (!wait_state) {
    return;
  }
  if (FLAGS_TEST_trace_ash_wait_code_updates) {
    if (FLAGS_tracing_level >= 1) {
      wait_state->VTrace(1, yb::Format("$0 at $1", ash::ToString(code), location));
    } else {
      wait_state->VTrace(0, yb::Format("$0", ash::ToString(code)));
    }
  }
  MaybeSleepForTests(wait_state.get(), code);
}

ScopedWaitStatus::~ScopedWaitStatus() {
  const auto& wait_state = WaitStateInfo::CurrentWaitState();
  if (!wait_state) {
    return;
  }

  auto expected = code_;
  if (!wait_state->mutable_code().compare_exchange_strong(expected, prev_code_)) {
    if (FLAGS_TEST_trace_ash_wait_code_updates) {
      wait_state->VTrace(
          0,
          yb::Format("Failed to revert to $0 at $1. We set it to $2 but it is now $3",
              ash::ToString(prev_code_), location_, ash::ToString(code_), ash::ToString(expected)));
    }
    LOG_IF(DFATAL, !wait_state->IsConcurrentUpdatesEnabled())
        << " In " << location_ << " wait-state " << wait_state.get() << " was updated to " << code_
        << " from " << prev_code_ << " but it is currently " << expected
        << ". Not expecting concurrent updates."
        << "\nTrace so far:" << wait_state->DumpTraceToString();
    return;
  }

  if (FLAGS_TEST_trace_ash_wait_code_updates) {
    if (FLAGS_tracing_level >= 1) {
      wait_state->VTrace(
          1, yb::Format("Reverted to $0 at $1", ash::ToString(prev_code_), location_));
    } else {
      wait_state->VTrace(0, yb::Format("Reverted to $0", ash::ToString(prev_code_)));
    }
  }
  MaybeSleepForTests(wait_state.get(), prev_code_);
}

void WaitStateTracker::Track(const yb::ash::WaitStateInfoPtr& wait_state_ptr) {
  std::lock_guard lock(mutex_);
  entries_.emplace(wait_state_ptr);
}

void WaitStateTracker::Untrack(const yb::ash::WaitStateInfoPtr& ptr) {
  std::lock_guard lock(mutex_);
  entries_.erase(ptr);
}

std::vector<yb::ash::WaitStateInfoPtr> WaitStateTracker::GetWaitStates() const {
  std::lock_guard lock(mutex_);
  return {entries_.begin(), entries_.end()};
}

WaitStateType GetWaitStateType(WaitStateCode code) {
  switch (code) {
    case WaitStateCode::kUnused:
    case WaitStateCode::kYSQLReserved:
      return WaitStateType::kCpu;

    case WaitStateCode::kCatalogRead:
    case WaitStateCode::kIndexRead:
    case WaitStateCode::kStorageRead:
    case WaitStateCode::kStorageFlush:
      return WaitStateType::kNetwork;

    case WaitStateCode::kOnCpu_Active:
    case WaitStateCode::kOnCpu_Passive:
      return WaitStateType::kCpu;

    case WaitStateCode::kIdle:
    case WaitStateCode::kRpc_Done:
    case WaitStateCode::kDeprecated_Rpcs_WaitOnMutexInShutdown:
      return WaitStateType::kWaitOnCondition;

    case WaitStateCode::kRetryableRequests_SaveToDisk:
      return WaitStateType::kDiskIO;

    case WaitStateCode::kMVCC_WaitForSafeTime:
    case WaitStateCode::kLockedBatchEntry_Lock:
    case WaitStateCode::kBackfillIndex_WaitForAFreeSlot:
      return WaitStateType::kWaitOnCondition;

    case WaitStateCode::kCreatingNewTablet:
    case WaitStateCode::kSaveRaftGroupMetadataToDisk:
      return WaitStateType::kDiskIO;

    case WaitStateCode::kTransactionStatusCache_DoGetCommitData:
      return WaitStateType::kNetwork;

    case WaitStateCode::kWaitForYSQLBackendsCatalogVersion:
      return WaitStateType::kWaitOnCondition;

    case WaitStateCode::kWriteSysCatalogSnapshotToDisk:
      return WaitStateType::kDiskIO;

    case WaitStateCode::kDumpRunningRpc_WaitOnReactor:
      return WaitStateType::kWaitOnCondition;

    case WaitStateCode::kConflictResolution_ResolveConficts:
      return WaitStateType::kNetwork;

    case WaitStateCode::kConflictResolution_WaitOnConflictingTxns:
      return WaitStateType::kWaitOnCondition;

    case WaitStateCode::kRaft_WaitingForReplication:
      return WaitStateType::kNetwork;

    case WaitStateCode::kRaft_ApplyingEdits:
      return WaitStateType::kCpu;

    case WaitStateCode::kWAL_Append:
    case WaitStateCode::kWAL_Sync:
    case WaitStateCode::kConsensusMeta_Flush:
      return WaitStateType::kDiskIO;

    case WaitStateCode::kReplicaState_TakeUpdateLock:
      return WaitStateType::kWaitOnCondition;

    case WaitStateCode::kRocksDB_ReadBlockFromFile:
    case WaitStateCode::kRocksDB_OpenFile:
    case WaitStateCode::kRocksDB_WriteToFile:
    case WaitStateCode::kRocksDB_CloseFile:
      return WaitStateType::kDiskIO;

    case WaitStateCode::kRocksDB_Flush:
    case WaitStateCode::kRocksDB_Compaction:
      return WaitStateType::kCpu;

    case WaitStateCode::kRocksDB_PriorityThreadPoolTaskPaused:
      return WaitStateType::kWaitOnCondition;

    case WaitStateCode::kRocksDB_RateLimiter:
    case WaitStateCode::kRocksDB_WaitForSubcompaction:
      return WaitStateType::kWaitOnCondition;

    case WaitStateCode::kRocksDB_NewIterator:
      return WaitStateType::kDiskIO;

    case WaitStateCode::kYCQL_Parse:
    case WaitStateCode::kYCQL_Read:
    case WaitStateCode::kYCQL_Write:
    case WaitStateCode::kYCQL_Analyze:
    case WaitStateCode::kYCQL_Execute:
      return WaitStateType::kCpu;

    case WaitStateCode::kYBClient_WaitingOnDocDB:
    case WaitStateCode::kYBClient_LookingUpTablet:
      return WaitStateType::kNetwork;
  }
  FATAL_INVALID_ENUM_VALUE(WaitStateCode, code);
}

namespace {

WaitStateTracker flush_and_compaction_wait_states_tracker;
WaitStateTracker raft_log_appender_wait_states_tracker;
WaitStateTracker pg_shared_memory_perform_tracker;

}  // namespace

WaitStateTracker& FlushAndCompactionWaitStatesTracker() {
  return flush_and_compaction_wait_states_tracker;
}

WaitStateTracker& RaftLogWaitStatesTracker() {
  return raft_log_appender_wait_states_tracker;
}

WaitStateTracker& SharedMemoryPgPerformTracker() {
  return pg_shared_memory_perform_tracker;
}

}  // namespace yb::ash
