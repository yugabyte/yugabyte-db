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
  std::string request_id;
  // TBD: other fields as required.

  std::string ToString() const {
	return request_id;
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

 private:
  const AUHMetadata metadata_;
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
