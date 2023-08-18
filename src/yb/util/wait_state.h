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
#define ADOPT_WAIT_STATE(ptr) yb::util::WaitStateInfo::SetCurrentWaitState(ptr)
#define SCOPED_ADOPT_WAIT_STATE(ptr) \
  yb::util::ScopedWaitState _scoped_state { ptr }

#define SCOPED_WAIT_STATUS_FOR(ptr, state) \
  yb::util::ScopedWaitStatus _scoped_status { (ptr), (state) }
#define SCOPED_WAIT_STATUS(state) \
  SCOPED_WAIT_STATUS_FOR(yb::util::WaitStateInfo::CurrentWaitState(), (state))


/* ----------
 * YB AUH Wait Components
 * ----------
 */
#define YB_PGGATE    0xF0000000U
#define YB_TSERVER   0xE0000000U
#define YB_CQL       0xD0000000U
#define YB_PG        0x00000000U
/* ----------
 * YB AUH Wait Classes
 * ----------
 */
#define YB_PG_WAIT_PERFORM           0xFE000000U
#define YB_RPC                       0xEF000000U
#define YB_FLUSH_AND_COMPACTION      0xEE000000U
#define YB_CONSENSUS                 0xED000000U
#define YB_TABLET_WAIT               0xEC000000U
#define YB_ROCKSDB                   0xEB000000U
#define YB_CLIENT                    0xEA000000U
#define YB_CQL_WAIT_STATE            0xDF000000U

// For debugging purposes:
// Uncomment the following line to track state changes in wait events.
// #define TRACK_WAIT_HISTORY
namespace yb {
namespace util {

YB_DEFINE_ENUM_TYPE(
    WaitStateCode,
    uint32_t,
    ((Unused, 0))

    // General states for incoming RPCs
    ((Created, YB_RPC)) // The rpc has been created.
       (Queued) // Rpc has been queued. Waiting for Service threads to pick up
       (Handling) // Rpc handler is currently working on this rpc.
       (HandlingDone) // Rpc handler is done.
       (QueueingResponse) // The response is being queued.
       (ResponseQueued) // The response has been queued, waiting for Reactor to transfer the response.
       (ResponseTransferred) // Response has been transferred. RPC is done now.

    // Writes
    ((AcquiringLocks, YB_TABLET_WAIT))  // A write-rpc is acquiring the required locks.
        (ConflictResolution)            // Doing conflict resolution.
        (ExecuteWrite)   // Write query is executing
        (SubmittingToRaft)   // Write query is submitting the write operation to Raft.
    // Reads
    (GetSafeTime)(GetSubDoc)(DoRead)

    // OperationDriver
    ((ExecuteAsync, YB_CONSENSUS))  // Raft request  being enqueued for preparer.
        (PrepareAndStart) // 
        (SubmittedToPreparer) // raft-operation has been submitted to preparer.
        (AddedToLeader) // raft-operation written to the leader's log. Waiting for response from followers.
        (AddedToFollower) // On the follower side.
        (HandleFailure) // Ran into some kind of failure.
      (Applying) // operation has been replicated/acknowledged by a majority. Applying it to the tablet.
      (ApplyDone) // apply is done.

    // UpdateConsensus
      (Updating) // Update rpc is called on a follower
      (UpdateReplica) // Handling UpdateReplica
      (DoneUpdate)   // Done update

    // Debugging
    // Various specific operations in SubmittedToPrepare state
      (SubmittedWriteToPreparer)
      (SubmittedChangeMetadataToPreparer)
      (SubmittedUpdateTransactionToPreparer)
      (SubmittedSnapshotToPreparer)
      (SubmittedTruncateToPreparer)
      (SubmittedEmptyToPreparer)
      (SubmittedHistoryCutoffToPreparer)
      (SubmittedSplitToPreparer)
      (SubmittedChangeAutoFlagsConfigToPreparer)
      (SubmittedUnexpectedToPreparer)

    // Flush and Compaction
    ((StartFlush, YB_FLUSH_AND_COMPACTION))(StartCompaction)
    (OpenFile)(CloseFile)(DeleteFile)(WriteToFile)
    (StartSubcompactionThreads)(WaitOnSubcompactionThreads)

    // CQL Wait Events
    ((Parse, YB_CQL_WAIT_STATE))(Analyze)(Execute)(ExecuteWaitingForCB)
    (CQLRead)(CQLWrite)

    ((RocksDB, YB_ROCKSDB))
       (BlockCacheLookupInCache)
       (BlockCacheLookupCompressed)
       (BlockCacheReadFromDisk)

    // Perform Wait Events
    ((DmlRead, YB_PG_WAIT_PERFORM)) (DmlWrite) (DmlReadWrite)

    // YBClient
    ((Flushing, YB_CLIENT))
      (FlushedWaitingForCB)
      (YBClientRpcsSent)
      (YBCCallbackCalled)
      (LookingUpTablet)
      (TabletLookupFinished)
    )

YB_DEFINE_ENUM(MessengerType, (kTserver)(kCQLServer))

struct AUHMetadata {
  std::vector<uint64_t> top_level_request_id;
  std::vector<uint64_t> top_level_node_id;
  int64_t query_id = 0;
  int64_t current_request_id = 0;
  uint32_t client_node_host = 0;
  uint16_t client_node_port = 0;

  void set_client_node_ip(const std::string &endpoint);

  std::string ToString() const {
    return yb::Format("{ top_level_node_id: $0, top_level_request_id: $1, query_id: $2, current_request_id: $3, client_node_ip: $4:$5 }",
                      top_level_node_id, top_level_request_id, query_id, current_request_id, client_node_host, client_node_port);
  }

  void UpdateFrom(const AUHMetadata &other) {
    VLOG(2) << "Before Updating " << ToString();
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
    VLOG(2) << "After Updating " << ToString();
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
    VLOG(2) << "After ToPB " << ToString() << "\n returned " << pb->DebugString();
  }

  template <class PB>
  static AUHMetadata FromPB(const PB& pb) {
    return AUHMetadata{
        .top_level_request_id = std::vector<uint64_t>(pb.top_level_request_id().begin(), pb.top_level_request_id().end()),
        .top_level_node_id = std::vector<uint64_t>(pb.top_level_node_id().begin(), pb.top_level_node_id().end()),
        .query_id = pb.query_id(),
        .current_request_id = pb.current_request_id(),
        .client_node_host = pb.client_node_host(),
        .client_node_port = static_cast<uint16_t>(pb.client_node_port())
    };
  }

  template <class PB>
  void UpdateFromPB(const PB& pb) {
    if (pb.has_top_level_node_id()) {
      top_level_node_id = std::vector<uint64_t>(pb.top_level_node_id().begin(), pb.top_level_node_id().end());
    }
    if (pb.has_top_level_request_id()) {
      top_level_request_id = std::vector<uint64_t>(pb.top_level_request_id().begin(), pb.top_level_request_id().end());
    }
    if (pb.has_query_id()) {
      query_id = pb.query_id();
    }
    if (pb.has_client_node_host()) {
      client_node_host = pb.client_node_host();
    }
    if (pb.client_node_port()) {
      client_node_port = static_cast<uint16_t>(pb.client_node_port());
    }
    if (pb.has_current_request_id()) {
      current_request_id = pb.current_request_id();
    }
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

  static WaitStateInfoPtr CurrentWaitState();
  static void SetCurrentWaitState(WaitStateInfoPtr);

  void UpdateMetadata(const AUHMetadata& meta) EXCLUDES(mutex_);
  void UpdateAuxInfo(const AUHAuxInfo& aux) EXCLUDES(mutex_);
  void set_current_request_id(int64_t id) EXCLUDES(mutex_);
  void set_top_level_request_id(uint64_t id) EXCLUDES(mutex_);
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

  template <class PB>
  void ToPB(PB *pb) {
    {
    std::lock_guard<simple_spinlock> l(mutex_);
    metadata_.ToPB(pb->mutable_metadata());
    WaitStateCode code = get_state();
    pb->set_wait_status_code(yb::to_underlying(code));
#ifndef NDEBUG
    pb->set_wait_status_code_as_string(yb::ToString(code));
#endif
    aux_info().ToPB(pb->mutable_aux_info());
    }
    VLOG(1) << "Wrote ToPB " << pb->ShortDebugString();
  }

  AUHMetadata& metadata() REQUIRES(mutex_) {
    return metadata_;
  }

  simple_spinlock* get_mutex() RETURN_CAPABILITY(mutex_);

  std::string ToString() const EXCLUDES(mutex_);

  const AUHAuxInfo& aux_info();

  static void freeze();
  static void unfreeze();

 private:
  std::atomic<WaitStateCode> code_{WaitStateCode::Unused};
  std::atomic<WaitStateCode> frozen_state_code_{WaitStateCode::Unused};
  static std::atomic<bool> freeze_;

  mutable simple_spinlock mutex_;
  AUHMetadata metadata_ GUARDED_BY(mutex_);
  AUHAuxInfo aux_info_ GUARDED_BY(mutex_);
  AUHAuxInfo frozen_aux_info_ GUARDED_BY(mutex_);

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
