// Copyright (c) YugaByte, Inc.
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

#include <array>
#include <atomic>
#include <functional>
#include <iosfwd>
#include <string>
#include <vector>
#include <unordered_map>

#include "yb/util/flags.h"

#include "yb/gutil/macros.h"
#include "yb/gutil/ref_counted.h"
#include "yb/gutil/strings/stringpiece.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/gutil/threading/thread_collision_warner.h"

#include "yb/util/atomic.h" // For GetAtomicFlag
#include "yb/util/locks.h"
#include "yb/util/memory/arena_fwd.h"
#include "yb/util/monotime.h"

DECLARE_bool(export_wait_state_names);

#define SET_WAIT_STATUS_TO(ptr, state) \
  if (ptr) ptr->set_state(state)
#define SET_WAIT_STATUS(state) \
  SET_WAIT_STATUS_TO(yb::util::WaitStateInfo::CurrentWaitState(), (state))

#define SET_WAIT_STATUS_TO_IF_AT(ptr, prev_state, state) \
  if (ptr) ptr->set_state_if(prev_state, state)
#define SET_WAIT_STATUS_IF_AT(prev_state, state) \
  SET_WAIT_STATUS_TO_IF_AT(yb::util::WaitStateInfo::CurrentWaitState(), (prev_state), (state))

// Note that we are not taking ownership or even shared ownership of the ptr.
// The ptr should be live until this is done.
#define SCOPED_ADOPT_WAIT_STATE(ptr) \
  yb::util::ScopedWaitState _scoped_state { ptr }

#define SCOPED_WAIT_STATUS_FOR(ptr, state) \
  yb::util::ScopedWaitStatus _scoped_status { (ptr), (state) }
#define SCOPED_WAIT_STATUS(state) \
  SCOPED_WAIT_STATUS_FOR(yb::util::WaitStateInfo::CurrentWaitState(), (state))

#define PUSH_WAIT_STATUS_TO(ptr, state) \
  if (ptr) ptr->push_state(state)
#define PUSH_WAIT_STATUS(state) \
  PUSH_WAIT_STATUS_TO(yb::util::WaitStateInfo::CurrentWaitState(), (state))

#define POP_WAIT_STATUS_TO(ptr, state) \
  if (ptr) ptr->pop_state(state)
#define POP_WAIT_STATUS(state) \
  POP_WAIT_STATUS_TO(yb::util::WaitStateInfo::CurrentWaitState(), (state))

/* ----------
 * YB AUH Wait Components
 * ----------
 */
#define YB_PG        0x0
#define YB_TSERVER   0x4
#define YB_YBC       0x8
#define YB_PERFORM   0xC
/* ----------
 * YB AUH Wait Classes
 * ----------
 */
 // 0x01 - 0x0C used in pgstat.h for Pg Wait classes
#define YB_RPC                       0x00000000U
#define YB_FLUSH_AND_COMPACTION      0x01000000U
#define YB_CONSENSUS                 0x02000000U
#define YB_TABLET_WAIT               0x03000000U
#define YB_ROCKSDB                   0x04000000U
#define YB_COMMON                    0x05000000U
                                          
#define YB_CQL_WAIT_STATE            0x06000000U
#define YB_CLIENT                    0x07000000U

#define YB_PG_WAIT_PERFORM           0x08000000U
#define YB_PG_CLIENT_SERVICE         0x09000000U

// For debugging purposes:
// Uncomment the following line to track state changes in wait events.
// #define TRACK_WAIT_HISTORY
namespace yb {
namespace util {

YB_DEFINE_ENUM_TYPE(
    WaitStateComponent,
    uint8_t,
    ((PG, YB_PG))
    ((TServer, YB_TSERVER))
    ((YCQL, YB_YBC))
    ((PGPerform, YB_PERFORM))
    (Unused)
    );

YB_DEFINE_ENUM_TYPE(
    WaitStateCode,
    uint32_t,
    ((Unused, YB_COMMON))
      (ActiveOnCPU)
      (PassiveOnCPU)

    // General states for incoming RPCs
    ((Created, YB_RPC)) // The rpc has been created.
       // Response Queued waiting for network transfer. -- Is this expected to take long?
       (ResponseQueued) // The response has been queued, waiting for Reactor to transfer the response.

    // Writes
    ((TabletActiveOnCPU, YB_TABLET_WAIT))  // A write-rpc is acquiring the required locks.
    (LockedBatchEntry_Lock)
    (MVCCWaitForSafeTime)
    (BackfillIndexWaitForAFreeSlot)

    (TransactionStatusCache_DoGetCommitData)
    (XreplCatalogManagerWaitForIsBootstrapRequired)
    (RpcsWaitOnMutexInShutdown)
    (TxnCoordWaitForMutexInPrepareForDeletion)
    (PgResponseCache_Get)
    (TxnResolveSealedStatus)
    (PgClientSessionStartExchange)
    (WaitForYsqlBackendsCatalogVersion)
    (CreatingNewTablet)
    (RetryableRequestsSaveToDisk)
    (WriteAutoFlagsConfigToDisk)
    (WriteInstanceMetadataToDisk)
    (WriteSysCatalogSnapshotToDisk)
    (SaveRaftGroupMetadataToDisk)
    (TakeRWCLock)
    (SysCatalogTableSyncWrite)
    (WaitOnTxnConflict)
    (WaitOnTxnResolve)
    (WaitOnShutdown)

    // OperationDriver
    ((RaftActiveOnCPU, YB_CONSENSUS))  // Raft request  being enqueued for preparer. -- never seen
      (WALLogSync) // waiting for WALEdits to be persisted.
      (WaitOnWAL)
      (RaftWaitingForQuorum)
      (ApplyingRaftEdits)
      (ConsensusMetaFlush)
      (ReplicaStateTakeUpdateLock)
      (ReplicaStateWaitForMajorityReplicatedHtLeaseExpiration)
      (DumpRunningRpcWaitOnReactor)

    ((RocksDBActiveOnCPU, YB_ROCKSDB))
       (BlockCacheReadFromDisk)
       (RocksDBReadIO)

    // Flush and Compaction
    ((StartFlush, YB_FLUSH_AND_COMPACTION))(StartCompaction)
    (OpenFile)
    (CloseFile)
    (DeleteFile)
    (WriteToFile)
    (StartSubcompactionThreads)(WaitOnSubcompactionThreads)

    // CQL Wait Events
    ((Parse, YB_CQL_WAIT_STATE))(Analyze)(Execute)(ExecuteWaitingForCB)
    (CQLActiveOnCPU)
    (CQLRead)(CQLWrite)
    (CQLWaitingOnDocdb)

    ((PgPerformHandling, YB_PG_CLIENT_SERVICE))
    (PGWaitingOnDocdb)
    (PGActiveOnCPU)

    // YBClient
    ((YBCActiveOnCPU, YB_CLIENT))
      (LookingUpTablet)
      (YBCSyncLeaderMasterRpc)
      (YBCFindMasterProxy)

    // Perform Wait Events
    ((StorageRead, YB_PG_WAIT_PERFORM))
    (StorageWrite)(CatalogRead)(CatalogWrite)
    (DmlRead)(DmlWrite)
    )

YB_DEFINE_ENUM(MessengerType, (kTserver)(kCQLServer))

struct AUHMetadata {
  std::vector<uint64_t> top_level_request_id;
  std::vector<uint64_t> top_level_node_id;
  int64_t query_id = 0;
  int64_t current_request_id = 0;
  uint32_t client_node_host = 0;
  uint16_t client_node_port = 0;
  WaitStateComponent component = WaitStateComponent::Unused;

  void set_client_node_ip(const std::string &endpoint);

  std::string ToString() const {
    return yb::Format("{ top_level_node_id: $0, top_level_request_id: $1, query_id: $2, current_request_id: $3, client_node_ip: $4:$5, component: $6 }",
                      top_level_node_id, top_level_request_id, query_id, current_request_id, client_node_host, client_node_port, component);
  }

  void UpdateFrom(const AUHMetadata &other) {
    if (!other.top_level_request_id.empty()) {
      top_level_request_id = other.top_level_request_id;
    }
    if (!other.top_level_node_id.empty()) {
      top_level_node_id = other.top_level_node_id;
    }
    if (other.query_id != 0) {
      query_id = other.query_id;
    }
    if (other.current_request_id != 0) {
      current_request_id = other.current_request_id;
    }
    if (other.client_node_host != 0) {
      client_node_host = other.client_node_host;
    }
    if (other.client_node_port != 0) {
      client_node_port = other.client_node_port;
    }
    if (other.component != WaitStateComponent::Unused) {
      component = other.component;
    }
  }

  template <class PB>
  void ToPB(PB* pb) const {
    if ((int)top_level_request_id.size() == 2) {
      pb->add_top_level_request_id(top_level_request_id[0]);
      pb->add_top_level_request_id(top_level_request_id[1]);
    }
    if ((int)top_level_node_id.size() == 2) {
      pb->add_top_level_node_id(top_level_node_id[0]);
      pb->add_top_level_node_id(top_level_node_id[1]);
    }
    if (query_id != 0) {
      pb->set_query_id(query_id);
    }
    if (current_request_id != 0) {
      pb->set_current_request_id(current_request_id);
    }
    if (client_node_host != 0) {
      pb->set_client_node_host(client_node_host);
    }
    if (client_node_port != 0) {
      pb->set_client_node_port(client_node_port);
    }
    // component is not saved in the PB.
  }

  template <class PB>
  static AUHMetadata FromPB(const PB& pb) {
    return AUHMetadata{
        .top_level_request_id = std::vector<uint64_t>(pb.top_level_request_id().begin(), pb.top_level_request_id().end()),
        .top_level_node_id = std::vector<uint64_t>(pb.top_level_node_id().begin(), pb.top_level_node_id().end()),
        .query_id = pb.query_id(),
        .current_request_id = pb.current_request_id(),
        .client_node_host = pb.client_node_host(),
        .client_node_port = static_cast<uint16_t>(pb.client_node_port()),
        .component = WaitStateComponent::Unused
    };
  }
};

struct AUHAuxInfo {
  std::string tablet_id;
  std::string table_id;
  std::string method;

  std::string ToString() const;

  void UpdateFrom(const AUHAuxInfo &other);

  template <class PB>
  void ToPB(PB* pb) const {
    pb->set_tablet_id(tablet_id);
    pb->set_table_id(table_id);
    pb->set_method(method);
  }

  template <class PB>
  static AUHAuxInfo FromPB(const PB& pb) {
    return AUHAuxInfo{
      .tablet_id = pb.tablet_id(),
      .table_id = pb.table_id(),
      .method = pb.method()
    };
  }
};

bool WaitsForLock(WaitStateCode c);
bool WaitsForIO(WaitStateCode c);
bool WaitsForThread(WaitStateCode c);
bool WaitsForSomething(WaitStateCode c);

class WaitStateInfo;

// typedef WaitStateInfo* WaitStateInfoPtr;
typedef std::shared_ptr<WaitStateInfo> WaitStateInfoPtr;
class WaitStateInfo {
 public:
  WaitStateInfo() = default;
  WaitStateInfo(AUHMetadata meta);

  void set_state(WaitStateCode c);
  void set_state_if(WaitStateCode prev, WaitStateCode c);
  WaitStateCode get_state() const;
  WaitStateCode get_frozen_state() const;
  WaitStateCode get_current_state() const;
  void push_state(WaitStateCode c);
  void pop_state(WaitStateCode c);

  static WaitStateInfoPtr CurrentWaitState();
  static void SetCurrentWaitState(WaitStateInfoPtr);

  void UpdateMetadata(const AUHMetadata& meta) EXCLUDES(mutex_);
  void UpdateAuxInfo(const AUHAuxInfo& aux) EXCLUDES(mutex_);
  void set_current_request_id(int64_t id) EXCLUDES(mutex_);
  void set_top_level_request_id(uint64_t id) EXCLUDES(mutex_);
  int64_t query_id() EXCLUDES(mutex_);
  void set_query_id(int64_t query_id) EXCLUDES(mutex_);
  void set_client_node_ip(const std::string &endpoint) EXCLUDES(mutex_);
  void set_top_level_node_id(const std::vector<uint64_t> &top_level_node_id) EXCLUDES(mutex_);

  template <class PB>
  static void UpdateMetadataFromPB(const PB& pb) {
    auto wait_state = CurrentWaitState();
    if (wait_state) {
      wait_state->UpdateMetadata(AUHMetadata::FromPB(pb));
    }
  }

  static uint32_t ToUint32(WaitStateComponent comp, WaitStateCode code) {
    uint32_t res = to_underlying(comp);
    CHECK_LE(res, 0xF);
    res = (res << 28);
    res = res | to_underlying(code);
    return res;
  }

  static WaitStateComponent DecodeWaitStateComponent(uint32_t encoded_wait_status_code) {
    return WaitStateComponent(encoded_wait_status_code >> 28);
  }

  static WaitStateCode DecodeWaitStateCode(uint32_t encoded_wait_status_code) {
    return WaitStateCode(encoded_wait_status_code & 0x0F00FFFF);
  }

  template <class PB>
  void ToPB(PB *pb) {
    std::lock_guard<simple_spinlock> l(mutex_);
    metadata_.ToPB(pb->mutable_metadata());
    WaitStateComponent component = metadata_.component;
    WaitStateCode code = get_state();
    pb->set_encoded_wait_status_code(ToUint32(component, code));
    if (FLAGS_export_wait_state_names) {
      pb->set_wait_status_code_as_string(yb::ToString(code));
    }
    aux_info_.ToPB(pb->mutable_aux_info());
  }

  AUHMetadata& metadata() REQUIRES(mutex_) {
    return metadata_;
  }

  simple_spinlock* get_mutex() RETURN_CAPABILITY(mutex_);

  std::string ToString() const EXCLUDES(mutex_);

  static void freeze();
  static void unfreeze();

  static void AssertIOAllowed();
  static void AssertWaitAllowed();
  void check_and_update_thread_id(util::WaitStateCode p, util::WaitStateCode n);
 private:
  std::atomic<WaitStateCode> state_to_pop_to_{WaitStateCode::Unused};
  std::atomic<WaitStateCode> code_{WaitStateCode::Unused};
  std::atomic<WaitStateCode> frozen_state_code_{WaitStateCode::Unused};
  static std::atomic<bool> freeze_;

  mutable simple_spinlock mutex_;
  AUHMetadata metadata_ GUARDED_BY(mutex_);
  AUHAuxInfo aux_info_ GUARDED_BY(mutex_);

#ifndef NDEBUG
  static simple_spinlock does_io_lock_;
  static simple_spinlock does_wait_lock_;
  static std::unordered_map<util::WaitStateCode, std::atomic_bool> does_io GUARDED_BY(does_io_lock_);
  static std::unordered_map<util::WaitStateCode, std::atomic_bool> does_wait GUARDED_BY(does_wait_lock_);

  std::string thread_name_;
  std::atomic<int64_t> thread_id_ = 0;
#endif

#ifdef TRACK_WAIT_HISTORY
  std::atomic_int16_t num_updates_ GUARDED_BY(mutex_);
  std::vector<WaitStateCode> history_ GUARDED_BY(mutex_);
#endif

  // Similar to thread-local trace:
  // The current wait_state_ for this thread.
  // static __thread WaitStateInfoPtr threadlocal_wait_state_;
  static thread_local WaitStateInfoPtr threadlocal_wait_state_;
  friend class ScopedWaitStatus;
  friend class ScopedWaitState;
};

class ScopedWaitState {
 public:
  ScopedWaitState(WaitStateInfoPtr wait_state);
  ~ScopedWaitState();

 private:
  WaitStateInfoPtr prev_state_;
};

class ScopedWaitStatus {
 public:
  ScopedWaitStatus(WaitStateCode state);
  ScopedWaitStatus(WaitStateInfoPtr wait_state, WaitStateCode state);
  ~ScopedWaitStatus();
  void ResetToPrevStatus();

 private:
  WaitStateInfoPtr wait_state_;
  const WaitStateCode state_;
  WaitStateCode prev_state_;
};


// Link to source codes for the classes below
// https://github.com/open-telemetry/opentelemetry-cpp/blob/main/sdk/src/common/fast_random_number_generator.h
// https://github.com/open-telemetry/opentelemetry-cpp/blob/main/sdk/src/common/random.h
class FastRandomNumberGenerator
{
public:
  using result_type = uint64_t;

  FastRandomNumberGenerator() noexcept = default;

  template <class SeedSequence>
  FastRandomNumberGenerator(SeedSequence &seed_sequence) noexcept
  {
    seed(seed_sequence);
  }

  uint64_t operator()() noexcept
  {
    // Uses the xorshift128p random number generation algorithm described in
    // https://en.wikipedia.org/wiki/Xorshift
    auto &state_a = state_[0];
    auto &state_b = state_[1];
    auto t        = state_a;
    auto s        = state_b;
    state_a       = s;
    t ^= t << 23;        // a
    t ^= t >> 17;        // b
    t ^= s ^ (s >> 26);  // c
    state_b = t;
    return t + s;
  }

  // RandomNumberGenerator concept functions required from standard library.
  // See http://www.cplusplus.com/reference/random/mt19937/
  template <class SeedSequence>
  void seed(SeedSequence &seed_sequence) noexcept
  {
    seed_sequence.generate(reinterpret_cast<uint32_t *>(state_.data()),
                           reinterpret_cast<uint32_t *>(state_.data() + state_.size()));
  }

  static constexpr uint64_t min() noexcept { return 0; }

  static constexpr uint64_t max() noexcept { return std::numeric_limits<uint64_t>::max(); }

private:
  std::array<uint64_t, 2> state_{};
};

class AUHRandom
{
public:
  /**
   * @return an unsigned 64 bit random number
   */
  static uint64_t GenerateRandom64() noexcept;
  /**
   * Fill the passed span with random bytes.
   *
   * @param buffer A span of bytes.
   */

private:
  /**
   * @return a seeded thread-local random number generator.
   */
  static FastRandomNumberGenerator &GetRandomNumberGenerator() noexcept;
};

}  // namespace util
}  // namespace yb
