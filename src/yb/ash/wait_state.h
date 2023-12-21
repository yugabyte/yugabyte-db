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

#include <atomic>
#include <string>

#include "yb/common/entity_ids_types.h"
#include "yb/common/wire_protocol.h"

#include "yb/gutil/casts.h"

#include "yb/util/enums.h"
#include "yb/util/locks.h"
#include "yb/util/net/net_util.h"
#include "yb/util/uuid.h"

DECLARE_bool(TEST_export_wait_state_names);

#define SET_WAIT_STATUS_TO(ptr, code) \
  if ((ptr)) (ptr)->set_code(BOOST_PP_CAT(yb::ash::WaitStateCode::k, code))
#define SET_WAIT_STATUS(code) \
  SET_WAIT_STATUS_TO(yb::ash::WaitStateInfo::CurrentWaitState(), code)

#define ADOPT_WAIT_STATE(ptr) \
  yb::ash::ScopedAdoptWaitState _scoped_state { (ptr) }

#define SCOPED_WAIT_STATUS(code) \
  yb::ash::ScopedWaitStatus _scoped_status(BOOST_PP_CAT(yb::ash::WaitStateCode::k, code))

// Wait components refer to which process the specific wait-event is part of.
// Generally, these are PG, TServer, YBClient/Perform layer, and PgGate.
//
// Within each component, we further group wait events into similar groups called
// classes. Rpc related wait-events may be grouped together under "Rpc".
// Consensus related wait-events may be grouped together under a group -- "consensus".
// and so on.
//
// We use a 32-bit uint to represent a wait-event.
//   <8-bit reserved> <4-bit Component> <4-bit Class> <16-bit Event>
// - The hightest 8 bits are set to 0, and reserved for future use.
// - The next 4 bits of the wait-event-code represents the component.
// - The next 4 bits of the wait-event-code represents the wait-event class.
// - Each wait-event class may have up to 2^16 wait-events.

// YB ASH Wait Components (4 bits)
#define YB_ASH_COMPONENT_PGGATE    0xFU
#define YB_ASH_COMPONENT_TSERVER   0xEU
#define YB_ASH_COMPONENT_YBC       0xDU
#define YB_ASH_COMPONENT_PG        0xCU

#define YB_ASH_COMPONENT_POSITION  20U
#define YB_ASH_CLASS_POSITION      16U
#define YB_ASH_WAIT_EVENT_MASK     ((1U << 24U) - 1U)

#define YB_ASH_MAKE_CLASS(comp, c) \
    YB_ASH_WAIT_EVENT_MASK &   \
    (((comp) << YB_ASH_COMPONENT_POSITION) | ((c) << YB_ASH_CLASS_POSITION))

// YB ASH Wait Classes (4 bits)
#define YB_ASH_CLASS_PG                   YB_ASH_MAKE_CLASS(YB_ASH_COMPONENT_PG,  0xFU)

#define YB_ASH_CLASS_RPC                  YB_ASH_MAKE_CLASS(YB_ASH_COMPONENT_TSERVER, 0xFU)
#define YB_ASH_CLASS_FLUSH_AND_COMPACTION YB_ASH_MAKE_CLASS(YB_ASH_COMPONENT_TSERVER, 0xEU)
#define YB_ASH_CLASS_CONSENSUS            YB_ASH_MAKE_CLASS(YB_ASH_COMPONENT_TSERVER, 0xDU)
#define YB_ASH_CLASS_TABLET_WAIT          YB_ASH_MAKE_CLASS(YB_ASH_COMPONENT_TSERVER, 0xCU)
#define YB_ASH_CLASS_ROCKSDB              YB_ASH_MAKE_CLASS(YB_ASH_COMPONENT_TSERVER, 0xBU)
#define YB_ASH_CLASS_COMMON               YB_ASH_MAKE_CLASS(YB_ASH_COMPONENT_TSERVER, 0xAU)

#define YB_ASH_CLASS_PG_CLIENT_SERVICE    YB_ASH_MAKE_CLASS(YB_ASH_COMPONENT_YBC, 0xFU)
#define YB_ASH_CLASS_CQL_WAIT_STATE       YB_ASH_MAKE_CLASS(YB_ASH_COMPONENT_YBC, 0xEU)
#define YB_ASH_CLASS_CLIENT               YB_ASH_MAKE_CLASS(YB_ASH_COMPONENT_YBC, 0xDU)



namespace yb::ash {

YB_DEFINE_TYPED_ENUM(WaitStateCode, uint32_t,
    ((kUnused, 0))
    ((kOnCpu_Active, YB_ASH_CLASS_COMMON))
    (kOnCpu_Passive)
    (kRpc_Done)
    (kRpcs_WaitOnMutexInShutdown)
    (kRetryableRequests_SaveToDisk)
    ((kMVCC_WaitForSafeTime, YB_ASH_CLASS_TABLET_WAIT))
    (kLockedBatchEntry_Lock)
    (kBackfillIndex_WaitForAFreeSlot)
    (kCreatingNewTablet)
    (kSaveRaftGroupMetadataToDisk)
    (kTransactionStatusCache_DoGetCommitData)
    (kWaitForYsqlBackendsCatalogVersion)
    (kWriteAutoFlagsConfigToDisk)
    (kWriteInstanceMetadataToDisk)
    (kWriteSysCatalogSnapshotToDisk)
    (kDumpRunningRpc_WaitOnReactor)
    (kConflictResolution_ResolveConficts)
    (kConflictResolution_WaitOnConflictingTxns)
    ((kWAL_Open, YB_ASH_CLASS_CONSENSUS)) // waiting for WALEdits to be persisted.
    (kWAL_Close)
    (kWAL_Write)
    (kWAL_AllocateNewSegment)
    (kWAL_Sync)
    (kWAL_Wait)
    (kWaitOnWAL)
    (kRaft_WaitingForQuorum)
    (kRaft_ApplyingEdits)
    (kConsensusMeta_Flush)
    (kReplicaState_TakeUpdateLock)
    (kReplicaState_WaitForMajorityReplicatedHtLeaseExpiration)
    ((kRocksDB_OnCpu_Active, YB_ASH_CLASS_ROCKSDB))
    (kRocksDB_ReadBlockFromFile)
    (kRocksDB_ReadIO));

struct AshMetadata {
  Uuid root_request_id = Uuid::Nil();
  Uuid yql_endpoint_tserver_uuid = Uuid::Nil();
  int64_t query_id = 0;
  int64_t rpc_request_id = 0;
  HostPort client_host_port{};

  void set_client_host_port(const HostPort& host_port);

  std::string ToString() const;

  void UpdateFrom(const AshMetadata& other) {
    if (!other.root_request_id.IsNil()) {
      root_request_id = other.root_request_id;
    }
    if (!other.yql_endpoint_tserver_uuid.IsNil()) {
      yql_endpoint_tserver_uuid = other.yql_endpoint_tserver_uuid;
    }
    if (other.query_id != 0) {
      query_id = other.query_id;
    }
    if (other.rpc_request_id != 0) {
      rpc_request_id = other.rpc_request_id;
    }
    if (other.client_host_port != HostPort()) {
      client_host_port = other.client_host_port;
    }
  }

  template <class PB>
  void ToPB(PB* pb) const {
    if (!root_request_id.IsNil()) {
      root_request_id.ToBytes(pb->mutable_root_request_id());
    } else {
      pb->clear_root_request_id();
    }
    if (!yql_endpoint_tserver_uuid.IsNil()) {
      yql_endpoint_tserver_uuid.ToBytes(pb->mutable_yql_endpoint_tserver_uuid());
    } else {
      pb->clear_yql_endpoint_tserver_uuid();
    }
    if (query_id != 0) {
      pb->set_query_id(query_id);
    } else {
      pb->clear_query_id();
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
    Uuid yql_endpoint_tserver_uuid = Uuid::Nil();
    if (pb.has_yql_endpoint_tserver_uuid()) {
      Result<Uuid> result = Uuid::FromSlice(pb.yql_endpoint_tserver_uuid());
      WARN_NOT_OK(result, "Could not decode uuid from protobuf.");
      if (result.ok()) {
        yql_endpoint_tserver_uuid = *result;
      }
    }
    return AshMetadata{
        root_request_id,                       // root_request_id
        yql_endpoint_tserver_uuid,             // yql_endpoint_tserver_uuid
        pb.query_id(),                         // query_id
        pb.rpc_request_id(),                   // rpc_request_id
        HostPortFromPB(pb.client_host_port())  // client_host_port
    };
  }
};

struct AshAuxInfo {
  TableId table_id = "";
  TabletId tablet_id = "";
  std::string method = "";

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

class WaitStateInfo;
using WaitStateInfoPtr = std::shared_ptr<WaitStateInfo>;

class WaitStateInfo {
 public:
  WaitStateInfo() = default;
  explicit WaitStateInfo(AshMetadata&& meta);

  void set_code(WaitStateCode c);
  WaitStateCode code() const;
  std::atomic<WaitStateCode>& mutable_code();

  void set_root_request_id(const Uuid& id) EXCLUDES(mutex_);
  void set_yql_endpoint_tserver_uuid(const Uuid& yql_endpoint_tserver_uuid) EXCLUDES(mutex_);
  int64_t query_id() EXCLUDES(mutex_);
  void set_query_id(int64_t query_id) EXCLUDES(mutex_);
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
      wait_state->UpdateMetadata(AshMetadata::FromPB(pb));
    }
  }

  template <class PB>
  void MetadataToPB(PB* pb) EXCLUDES(mutex_) {
    std::lock_guard lock(mutex_);
    metadata_.ToPB(pb);
  }

  template <class PB>
  void ToPB(PB* pb) EXCLUDES(mutex_) {
    std::lock_guard lock(mutex_);
    metadata_.ToPB(pb->mutable_metadata());
    WaitStateCode code = this->code();
    pb->set_wait_status_code(yb::to_underlying(code));
    if (FLAGS_TEST_export_wait_state_names) {
      pb->set_wait_status_code_as_string(yb::ToString(code));
    }
    aux_info_.ToPB(pb->mutable_aux_info());
  }

  std::string ToString() const EXCLUDES(mutex_);

 private:
  std::atomic<WaitStateCode> code_{WaitStateCode::kUnused};

  mutable simple_spinlock mutex_;
  AshMetadata metadata_ GUARDED_BY(mutex_);
  AshAuxInfo aux_info_ GUARDED_BY(mutex_);
};

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
  explicit ScopedWaitStatus(WaitStateCode code);
  ~ScopedWaitStatus();

 private:
  const WaitStateCode code_;
  const WaitStateCode prev_code_;

  DISALLOW_COPY_AND_ASSIGN(ScopedWaitStatus);
};

}  // namespace yb::ash
