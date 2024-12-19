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
#pragma once

#include <sys/socket.h>

#include <atomic>
#include <string>

#include "yb/ash/wait_state_fwd.h"

#include "yb/common/entity_ids_types.h"
#include "yb/common/wire_protocol.h"

#include "yb/gutil/casts.h"

#include "yb/util/atomic.h"
#include "yb/util/enums.h"
#include "yb/util/locks.h"
#include "yb/util/net/net_util.h"
#include "yb/util/uuid.h"

DECLARE_bool(ysql_yb_enable_ash);

#define SET_WAIT_STATUS_TO_CODE(ptr, code) \
  if ((ptr)) (ptr)->set_code(code, __PRETTY_FUNCTION__)
#define SET_WAIT_STATUS_TO(ptr, code) \
  SET_WAIT_STATUS_TO_CODE(ptr, BOOST_PP_CAT(yb::ash::WaitStateCode::k, code))
#define SET_WAIT_STATUS(code) \
  SET_WAIT_STATUS_TO(yb::ash::WaitStateInfo::CurrentWaitState(), code)

#define ADOPT_WAIT_STATE(ptr) \
  yb::ash::ScopedAdoptWaitState _scoped_state { (ptr) }

#define SCOPED_WAIT_STATUS(code) \
  yb::ash::ScopedWaitStatus _scoped_status( \
      BOOST_PP_CAT(yb::ash::WaitStateCode::k, code), __PRETTY_FUNCTION__)

#define ASH_ENABLE_CONCURRENT_UPDATES_FOR(ptr) \
  yb::ash::EnableConcurrentUpdates(ptr)
#define ASH_ENABLE_CONCURRENT_UPDATES() \
  ASH_ENABLE_CONCURRENT_UPDATES_FOR(yb::ash::WaitStateInfo::CurrentWaitState())


namespace yb {
class Trace;
}  // namespace yb
namespace yb::ash {

// Wait components refer to which process the specific wait event is part of.
// Generally, these are PG, TServer and YBClient/Perform layer.
//
// Within each component, we further group wait events into similar groups called
// classes. Rpc related wait events may be grouped together under "Rpc".
// Consensus related wait events may be grouped together under a group -- "consensus".
// and so on.
//
// If the bit representation of wait event code is changed, don't forget to change the
// 'YBCGetWaitEvent*' functions.
//
// We use a 32-bit uint to represent a wait event. This is kept the same as PG to
// simplify the extraction of component, class and event name from wait event code.
//   <4-bit Component> <4-bit Class> <8-bit Reserved> <16-bit Event>
// - The highest 4 bits of the wait event code represents the component.
// - The next 4 bits of the wait event code represents the wait event class of
//   a specific wait event component.
// - The next 8 bits are set to 0, and reserved for future use.
// - Each wait event class may have up to 2^16 wait events.

#define YB_ASH_CLASS_BITS          4U
#define YB_ASH_CLASS_POSITION      24U
#define YB_ASH_COMPONENT_POSITION  (YB_ASH_CLASS_POSITION + YB_ASH_CLASS_BITS)
#define YB_ASH_COMPONENT_BITS      4U

#define YB_ASH_MAKE_EVENT(class) \
    (static_cast<uint32_t>(yb::to_underlying(BOOST_PP_CAT(yb::ash::Class::k, class))) << \
     YB_ASH_CLASS_POSITION)

// YB ASH Wait Components (4 bits)
// Don't reorder this enum
YB_DEFINE_TYPED_ENUM(Component, uint8_t,
    (kYSQL)
    (kYCQL)
    (kTServer)
    (kMaster));

// YB ASH Wait Classes (4 bits)
// Don't reorder this enum
YB_DEFINE_TYPED_ENUM(Class, uint8_t,
    // PG classes
    (kTServerWait)

    // QL/YB Client classes
    (kYCQLQuery)
    (kClient)

    // Docdb related classes
    (kRpc)
    (kConsensus)
    (kTabletWait)
    (kRocksDB)
    (kCommon));

// This is YB equivalent of wait events from pgstat.h, the term wait event and wait state
// is used interchangeably in the code. The uint32_t values of all the wait events across PG
// and WaitStateCode is distinct, so PG can use these wait events as well.
//
// The difference between PG and us is that this enum is only 28 bits long. 4 bits (component bits)
// are prepended while fetching the wait events so that we can reuse some wait events across
// components. Another difference is that if a PG wait event were casted/interpreted as an
// ASH wait event, the bits where we expect to see class information would actually contain type
// information.
//
// The wait event type is not directly encoded in our wait events.
YB_DEFINE_TYPED_ENUM(WaitStateCode, uint32_t,
    // Don't change the value of kUnused
    ((kUnused, 0xFFFFFFFFU))

    // Wait states related to postgres
    // Don't change the position of kYSQLReserved
    ((kYSQLReserved, YB_ASH_MAKE_EVENT(TServerWait)))
    (kCatalogRead)
    (kIndexRead)
    (kTableRead)
    (kStorageFlush)
    (kCatalogWrite)
    (kIndexWrite)
    (kTableWrite)
    (kWaitingOnTServer)

    // Common wait states
    ((kOnCpu_Active, YB_ASH_MAKE_EVENT(Common)))
    (kOnCpu_Passive)
    (kIdle)
    (kRpc_Done)
    (kDeprecated_Rpcs_WaitOnMutexInShutdown)
    (kRetryableRequests_SaveToDisk)

    // Wait states related to tablet wait
    ((kMVCC_WaitForSafeTime, YB_ASH_MAKE_EVENT(TabletWait)))
    (kLockedBatchEntry_Lock)
    (kBackfillIndex_WaitForAFreeSlot)
    (kCreatingNewTablet)
    (kSaveRaftGroupMetadataToDisk)
    (kTransactionStatusCache_DoGetCommitData)
    (kWaitForYSQLBackendsCatalogVersion)
    (kWriteSysCatalogSnapshotToDisk)
    (kDumpRunningRpc_WaitOnReactor)
    (kConflictResolution_ResolveConficts)
    (kConflictResolution_WaitOnConflictingTxns)

    // Wait states related to consensus
    ((kRaft_WaitingForReplication, YB_ASH_MAKE_EVENT(Consensus)))
    (kRaft_ApplyingEdits)
    (kWAL_Append)
    (kWAL_Sync)
    (kConsensusMeta_Flush)
    (kReplicaState_TakeUpdateLock)

    // Wait states related to RocksDB
    ((kRocksDB_ReadBlockFromFile, YB_ASH_MAKE_EVENT(RocksDB)))
    (kRocksDB_OpenFile)
    (kRocksDB_WriteToFile)
    (kRocksDB_Flush)
    (kRocksDB_Compaction)
    (kRocksDB_PriorityThreadPoolTaskPaused)
    (kRocksDB_CloseFile)
    (kRocksDB_RateLimiter)
    (kRocksDB_WaitForSubcompaction)
    (kRocksDB_NewIterator)

    // Wait states related to YCQL
    ((kYCQL_Parse, YB_ASH_MAKE_EVENT(YCQLQuery)))
    (kYCQL_Read)
    (kYCQL_Write)
    (kYCQL_Analyze)
    (kYCQL_Execute)

    // Wait states related to YBClient
    ((kYBClient_WaitingOnDocDB, YB_ASH_MAKE_EVENT(Client)))
    (kYBClient_LookingUpTablet)
);

// We also want to track background operations such as, log-append
// flush and compactions. However, as they are not user-generated, they
// do not have an automatic query id from the ql layer. We use these
// fixed query-ids to identify these background tasks.
YB_DEFINE_TYPED_ENUM(FixedQueryId, uint8_t,
  ((kQueryIdForLogAppender, 1))
  ((kQueryIdForFlush, 2))
  ((kQueryIdForCompaction, 3))
  ((kQueryIdForRaftUpdateConsensus, 4))
  ((kQueryIdForUncomputedQueryId, 5))
  ((kQueryIdForLogBackgroundSync, 6))
  ((kQueryIdForYSQLBackgroundWorker, 7))
);

YB_DEFINE_TYPED_ENUM(WaitStateType, uint8_t,
  (kCpu)
  (kDiskIO)
  (kNetwork)
  (kWaitOnCondition)
);

// List of pggate sync RPCs instrumented (in pg_client.cc)
// Make sure that kAsyncRPC is always 0
YB_DEFINE_TYPED_ENUM(PggateRPC, uint16_t,
  ((kNoRPC, 0))
  (kAlterDatabase)
  (kAlterTable)
  (kBackfillIndex)
  (kCreateDatabase)
  (kCreateReplicationSlot)
  (kCreateTable)
  (kCreateTablegroup)
  (kDropReplicationSlot)
  (kDropDatabase)
  (kDropTable)
  (kDropTablegroup)
  (kFinishTransaction)
  (kGetLockStatus)
  (kGetReplicationSlot)
  (kListLiveTabletServers)
  (kListReplicationSlots)
  (kGetIndexBackfillProgress)
  (kOpenTable)
  (kGetTablePartitionList)
  (kReserveOids)
  (kRollbackToSubTransaction)
  (kTabletServerCount)
  (kTruncateTable)
  (kValidatePlacement)
  (kGetTableDiskSize)
  (kWaitForBackendsCatalogVersion)
  (kInsertSequenceTuple)
  (kUpdateSequenceTuple)
  (kFetchSequenceTuple)
  (kReadSequenceTuple)
  (kDeleteSequenceTuple)
  (kDeleteDBSequences)
  (kCheckIfPitrActive)
  (kIsObjectPartOfXRepl)
  (kGetTserverCatalogVersionInfo)
  (kCancelTransaction)
  (kGetActiveTransactionList)
  (kGetTableKeyRanges)
  (kGetNewObjectId)
  (kTabletsMetadata)
  (kYCQLStatementStats)
  (kServersMetrics)
  (kListClones)
  (kCronGetLastMinute)
  (kCronSetLastMinute)
  (kAcquireAdvisoryLock)
  (kReleaseAdvisoryLock)
);

struct WaitStatesDescription {
  ash::WaitStateCode code;
  std::string description;

  WaitStatesDescription(ash::WaitStateCode code_, std::string&& description_)
      : code(code_), description(std::move(description_)) {}
};

WaitStateType GetWaitStateType(WaitStateCode code);

struct AshMetadata {
  Uuid root_request_id = Uuid::Nil();
  Uuid top_level_node_id = Uuid::Nil();
  uint64_t query_id = 0;
  pid_t pid = 0;
  uint32_t database_id = 0;
  int64_t rpc_request_id = 0;
  HostPort client_host_port{};
  uint8_t addr_family = AF_UNSPEC;

  void set_client_host_port(const HostPort& host_port);
  void clear_rpc_request_id();

  std::string ToString() const;

  void UpdateFrom(const AshMetadata& other) {
    if (!other.root_request_id.IsNil()) {
      root_request_id = other.root_request_id;
    }
    if (!other.top_level_node_id.IsNil()) {
      top_level_node_id = other.top_level_node_id;
    }
    if (other.query_id != 0) {
      query_id = other.query_id;
    }
    if (other.pid != 0) {
      pid = other.pid;
    }
    if (other.database_id != 0) {
      database_id = other.database_id;
    }
    if (other.rpc_request_id != 0) {
      rpc_request_id = other.rpc_request_id;
    }
    if (other.client_host_port != HostPort()) {
      client_host_port = other.client_host_port;
    }
    if (other.addr_family != AF_UNSPEC) {
      addr_family = other.addr_family;
    }
  }

  template <class PB>
  void ToPB(PB* pb) const {
    if (!root_request_id.IsNil()) {
      root_request_id.ToBytes(pb->mutable_root_request_id());
    } else {
      pb->clear_root_request_id();
    }
    if (!top_level_node_id.IsNil()) {
      top_level_node_id.ToBytes(pb->mutable_top_level_node_id());
    } else {
      pb->clear_top_level_node_id();
    }
    if (query_id != 0) {
      pb->set_query_id(query_id);
    } else {
      pb->clear_query_id();
    }
    if (pid != 0) {
      pb->set_pid(pid);
    } else {
      pb->clear_pid();
    }
    if (database_id != 0) {
      pb->set_database_id(database_id);
    } else {
      pb->clear_database_id();
    }
    if (rpc_request_id != 0) {
      pb->set_rpc_request_id(rpc_request_id);
    } else {
      pb->clear_rpc_request_id();
    }
    if (client_host_port != HostPort()) {
      HostPortToPB(client_host_port, pb->mutable_client_host_port());
    } else {
      pb->clear_client_host_port();
    }
    if (addr_family != AF_UNSPEC) {
      pb->set_addr_family(addr_family);
    } else {
      pb->clear_addr_family();
    }
  }

  template <class PB>
  static AshMetadata FromPB(const PB& pb) {
    Uuid root_request_id = Uuid::Nil();
    if (pb.has_root_request_id()) {
      Result<Uuid> result = Uuid::FromSlice(pb.root_request_id());
      WARN_NOT_OK(result, "Could not decode uuid from protobuf.");
      if (result.ok()) {
        root_request_id = *result;
      }
    }
    Uuid top_level_node_id = Uuid::Nil();
    if (pb.has_top_level_node_id()) {
      Result<Uuid> result = Uuid::FromSlice(pb.top_level_node_id());
      WARN_NOT_OK(result, "Could not decode uuid from protobuf.");
      if (result.ok()) {
        top_level_node_id = *result;
      }
    }
    return AshMetadata{
        root_request_id,                       // root_request_id
        top_level_node_id,                     // top_level_node_id
        pb.query_id(),                         // query_id
        pb.pid(),                              // pid
        pb.database_id(),                      // database_id
        pb.rpc_request_id(),                   // rpc_request_id
        HostPortFromPB(pb.client_host_port()), // client_host_port
        static_cast<uint8_t>(pb.addr_family()) // addr_family
    };
  }
};

struct AshAuxInfo {
  TableId table_id{};
  TabletId tablet_id{};
  std::string method{};

  std::string ToString() const;

  void UpdateFrom(const AshAuxInfo& other);

  template <class PB>
  void ToPB(PB* pb) const {
    pb->set_table_id(table_id);
    pb->set_tablet_id(tablet_id);
    pb->set_method(method);
  }

  template <class PB>
  static AshAuxInfo FromPB(const PB& pb) {
    return AshAuxInfo{pb.table_id(), pb.tablet_id(), pb.method()};
  }
};

class WaitStateInfo {
 public:
  WaitStateInfo();
  virtual ~WaitStateInfo() = default;

  void set_code(WaitStateCode c, const char* location);
  WaitStateCode code() const;
  std::atomic<WaitStateCode>& mutable_code();

  void set_root_request_id(const Uuid& id) EXCLUDES(mutex_);
  void set_top_level_node_id(const Uuid& top_level_node_id) EXCLUDES(mutex_);
  uint64_t query_id() EXCLUDES(mutex_);
  void set_query_id(uint64_t query_id) EXCLUDES(mutex_);
  int64_t rpc_request_id() EXCLUDES(mutex_);
  void set_rpc_request_id(int64_t id) EXCLUDES(mutex_);
  void set_client_host_port(const HostPort& host_port) EXCLUDES(mutex_);

  static const WaitStateInfoPtr& CurrentWaitState();
  static void SetCurrentWaitState(WaitStateInfoPtr);

  void UpdateMetadata(const AshMetadata& meta) EXCLUDES(mutex_);
  void UpdateAuxInfo(const AshAuxInfo& aux) EXCLUDES(mutex_);

  template <class PB>
  static void UpdateMetadataFromPB(const PB& pb) {
    const auto& wait_state = CurrentWaitState();
    if (wait_state) {
      // rpc_request_id is generated for each RPC, we don't populate it from PB
      auto metadata = AshMetadata::FromPB(pb);
      metadata.clear_rpc_request_id();
      wait_state->UpdateMetadata(metadata);
    }
  }

  template <class PB>
  void MetadataToPB(PB* pb) EXCLUDES(mutex_) {
    std::lock_guard lock(mutex_);
    metadata_.ToPB(pb);
  }

  template <class PB>
  void ToPB(PB* pb, bool export_wait_state_names) EXCLUDES(mutex_) {
    std::lock_guard lock(mutex_);
    metadata_.ToPB(pb->mutable_metadata());
    WaitStateCode code = this->code();
    pb->set_wait_state_code(yb::to_underlying(code));
    if (export_wait_state_names) {
      pb->set_wait_state_code_as_string(yb::ToString(code));
    }
    aux_info_.ToPB(pb->mutable_aux_info());
  }

  std::string ToString() const EXCLUDES(mutex_);

  void TEST_SleepForTests(uint32_t sleep_time_ms);
  static bool TEST_EnteredSleep();

  template <class T>
  static std::shared_ptr<T> CreateIfAshIsEnabled() {
    return FLAGS_ysql_yb_enable_ash
              ? std::make_shared<T>()
              : nullptr;
  }

  virtual void VTrace(int level, GStringPiece data) {
    VTraceTo(nullptr, level, data);
  }

  virtual std::string DumpTraceToString() {
    return "n/a";
  }

  void EnableConcurrentUpdates();
  bool IsConcurrentUpdatesEnabled();

  static std::vector<WaitStatesDescription> GetWaitStatesDescription();
  static int GetCircularBufferSizeInKiBs();

 protected:
  void VTraceTo(Trace* trace, int level, GStringPiece data);

 private:
  std::atomic<WaitStateCode> code_{WaitStateCode::kUnused};

  mutable simple_spinlock mutex_;
  AshMetadata metadata_ GUARDED_BY(mutex_);
  AshAuxInfo aux_info_ GUARDED_BY(mutex_);

  std::atomic_bool concurrent_updates_allowed_{false};
  std::atomic<uint8_t> TEST_num_sleeps_{0};
};

void EnableConcurrentUpdates(const WaitStateInfoPtr& ptr);

// A helper to adopt a WaitState and revert to the previous WaitState based on RAII.
// This should only be used on the stack (and thus created and destroyed
// on the same thread).
class ScopedAdoptWaitState {
 public:
  explicit ScopedAdoptWaitState(WaitStateInfoPtr wait_state);
  ~ScopedAdoptWaitState();

 private:
  WaitStateInfoPtr prev_state_;

  DISALLOW_COPY_AND_ASSIGN(ScopedAdoptWaitState);
};

// A helper to set the specified WaitStateCode in the specified wait_state
// and revert to the previous WaitStateCode based on RAII when it goes out of scope.
// This should only be used on the stack (and thus created and destroyed
// on the same thread).
//
// This should be used with the SCOPED_WAIT_STATUS* macro(s)
//
// For synchronously processed RPCs where all the work is expected to happen in the
// same thread, we can use SCOPED_WAIT_STATUS* macros, to set a state and revert to
// the previous state when we exit the scope.
// For RPCs which rely on async mechanisms, or may unilaterally modify the
// status within the function using SET_WAIT_STATUS macros -- in that case, will not
// be reverted back to the previous state.
class ScopedWaitStatus {
 public:
  ScopedWaitStatus(WaitStateCode code, const char* location);
  ~ScopedWaitStatus();

 private:
  const WaitStateCode code_;
  // The location where the scoped wait state is created. Used for printing useful debug messages.
  const char* location_;
  const WaitStateCode prev_code_;

  DISALLOW_COPY_AND_ASSIGN(ScopedWaitStatus);
};

// Used to track wait-states for Flush/Compaction and LocalInboundCalls.
class WaitStateTracker {
 public:
  void Track(const WaitStateInfoPtr&) EXCLUDES(mutex_);
  void Untrack(const WaitStateInfoPtr&) EXCLUDES(mutex_);
  std::vector<yb::ash::WaitStateInfoPtr> GetWaitStates() const EXCLUDES(mutex_);

 private:
  mutable std::mutex mutex_;
  std::unordered_set<yb::ash::WaitStateInfoPtr> entries_ GUARDED_BY(mutex_);
};

WaitStateTracker& FlushAndCompactionWaitStatesTracker();
WaitStateTracker& RaftLogWaitStatesTracker();
WaitStateTracker& SharedMemoryPgPerformTracker();

}  // namespace yb::ash
