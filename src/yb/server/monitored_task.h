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

#ifndef YB_SERVER_MONITORED_TASK_H
#define YB_SERVER_MONITORED_TASK_H

#include <memory>
#include <string>
#include <type_traits>

#include "yb/gutil/ref_counted.h"

#include "yb/util/status_fwd.h"
#include "yb/util/enums.h"
#include "yb/util/math_util.h"
#include "yb/util/monotime.h"

namespace yb {
namespace server {

YB_DEFINE_ENUM(MonitoredTaskState,
  (kWaiting)    // RPC not issued, or is waiting to be retried.
  (kRunning)    // RPC has been issued.
  (kComplete)   // RPC completed successfully.
  (kFailed)     // RPC completed with failure.
  (kAborted)    // RPC was aborted before it completed.
  (kScheduling) // RPC is being scheduled.
);

YB_DEFINE_ENUM(MonitoredTaskType,
  (kAddServer)
  (kAddTableToTablet)
  (kAlterTable)
  (kBackfillDone)
  (kBackfillTable)
  (kBackfillTabletChunk)
  (kChangeConfig)
  (kCopartitionTable)
  (kCreateReplica)
  (kDeleteReplica)
  (kFlushTablets)
  (kGetSafeTime)
  (kGetTabletSplitKey)
  (kPrepareDeleteTransactionTablet)
  (kRemoveServer)
  (kRemoveTableFromTablet)
  (kSnapshotOp)
  (kSplitTablet)
  (kStartElection)
  (kTestRetry)
  (kTruncateTablet)
  (kTryStepDown)
  (kUpdateTransactionTablesVersion)
);

class MonitoredTask : public std::enable_shared_from_this<MonitoredTask> {
 public:
  virtual ~MonitoredTask() {}

  // Abort this task and return its value before it was successfully aborted. If the task entered
  // a different terminal state before we were able to abort it, return that state.
  virtual MonitoredTaskState AbortAndReturnPrevState(const Status& status) = 0;

  // Task State.
  virtual MonitoredTaskState state() const = 0;

  virtual MonitoredTaskType type() const = 0;

  // Task Type Identifier.
  virtual std::string type_name() const = 0;

  // Task description.
  virtual std::string description() const = 0;

  // Task start time, may be !Initialized().
  virtual MonoTime start_timestamp() const = 0;

  // Task completion time, may be !Initialized().
  virtual MonoTime completion_timestamp() const = 0;

  // Whether task was started by the LB.
  virtual bool started_by_lb() const {
    return false;
  }

  std::string ToString() const;

  static bool IsStateTerminal(MonitoredTaskState state) {
    return state == MonitoredTaskState::kComplete ||
           state == MonitoredTaskState::kFailed ||
           state == MonitoredTaskState::kAborted;
  }
};

} // namespace server
} // namespace yb

#endif  // YB_SERVER_MONITORED_TASK_H
