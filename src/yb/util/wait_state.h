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

// Note that we are not taking ownership or even shared ownership of the ptr.
// The ptr should be live until this is done.
#define ADOPT_WAIT_STATE(ptr) yb::util::WaitStateInfo::SetCurrentWaitState(ptr)
#define SCOPED_ADOPT_WAIT_STATE(ptr) \
  yb::util::ScopedWaitState _scoped_state { ptr }

#define SCOPED_WAIT_STATUS_FOR(ptr, state) \
  yb::util::ScopedWaitStatus _scoped_status { (ptr), (state) }
#define SCOPED_WAIT_STATUS(state) \
  SCOPED_WAIT_STATUS_FOR(yb::util::WaitStateInfo::CurrentWaitState(), (state))


// For debugging purposes:
// Uncomment the following line to track state changes in wait events.
// #define TRACK_WAIT_HISTORY
namespace yb {
namespace util {

YB_DEFINE_ENUM(
    WaitStateCode,
    (Unused)
    // General states for incoming RPCs
    (Created)(Queued)(Handling)(QueueingResponse)(ResponseQueued)
    // Writes
    (AcquiringLocks)(ConflictResolution)(ExecuteWrite)(SubmittingToRaft)
      // OperationDriver
      (ExecuteAsync)(PrepareAndStart)(SubmittedToPreparer)(AddedToLeader)(AddedToFollower)(HandleFailure)
      (Applying)(ApplyDone)
    // UpdateConsensus
      (Updating)(UpdateReplica)(DoneUpdate)
    // Debugging
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
    // Reads
    (GetSafeTime)(GetSubDoc))

struct AUHMetadata {
  std::string top_level_request_id;
  std::string top_level_node_id;
  int64_t query_id = 0;
  int64_t current_request_id = 0;
  std::string client_node_ip;

  std::string ToString() const {
    return yb::Format("{ top_level_node_id: $0, top_level_request_id: $1, query_id: $2, current_request_id: $3, client_node_ip: $4 }",
                      top_level_node_id, top_level_request_id, query_id, current_request_id, client_node_ip);
  }

  template <class PB>
  void ToPBExceptCurrentRequestId(PB* pb) const {
    pb->set_top_level_node_id(top_level_node_id);
    pb->set_top_level_request_id(top_level_request_id);
    pb->set_query_id(query_id);
    pb->set_client_node_ip(client_node_ip);
  }

  template <class PB>
  void ToPB(PB* pb) const {
    ToPBExceptCurrentRequestId(pb);
    pb->set_current_request_id(current_request_id);
  }

  template <class PB>
  static AUHMetadata FromPBExceptCurrentRequestId(const PB& pb) {
    return AUHMetadata{
        .top_level_node_id = pb.top_level_node_id(),
        .top_level_request_id = pb.top_level_request_id(),
        .query_id = pb.query_id(),
        .current_request_id = 0,
        .client_node_ip = pb.client_node_ip()
    };
  }

  template <class PB>
  static AUHMetadata FromPB(const PB& pb) {
    return AUHMetadata{
        .top_level_node_id = pb.top_level_node_id(),
        .top_level_request_id = pb.top_level_request_id(),
        .query_id = pb.query_id(),
        .current_request_id = pb.current_request_id(),
        .client_node_ip = pb.client_node_ip()
    };
  }

  template <class PB>
  void UpdateFromPBExceptCurrentRequstId(const PB& pb) {
    top_level_node_id = pb.top_level_node_id();
    top_level_request_id = pb.top_level_request_id();
    query_id = pb.query_id();
    client_node_ip = pb.client_node_ip();
  }
};

class WaitStateInfo;
// typedef WaitStateInfo* WaitStateInfoPtr;
typedef std::shared_ptr<WaitStateInfo> WaitStateInfoPtr;
class WaitStateInfo {
 public:
  WaitStateInfo(AUHMetadata meta);

  void set_state(WaitStateCode c);

  WaitStateCode get_state() const;

  std::string ToString() const;

  static WaitStateInfoPtr CurrentWaitState();
  static void SetCurrentWaitState(WaitStateInfoPtr);

  template <class PB>
  static void UpdateCurrentWaitStateFromPBExceptCurrentRequstId(const PB& pb) {
    auto wait_state = util::WaitStateInfo::CurrentWaitState();
    if (wait_state && pb.has_auh_metadata()) {
      auto& auh_metadata = wait_state->metadata();
      auh_metadata.UpdateFromPBExceptCurrentRequstId(pb.auh_metadata());
    }
  }
  AUHMetadata& metadata() {
    return metadata_;
  }

 private:
  AUHMetadata metadata_;
  WaitStateCode code_ = WaitStateCode::Unused;

#ifdef TRACK_WAIT_HISTORY
  std::atomic_int16_t num_updates_;
  mutable simple_spinlock mutex_;
  std::vector<WaitStateCode> history_;
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

}  // namespace util
}  // namespace yb
