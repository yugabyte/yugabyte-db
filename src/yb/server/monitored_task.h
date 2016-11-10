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

#ifndef YB_SERVER_MONITORED_TASK_H
#define YB_SERVER_MONITORED_TASK_H

#include <string>

#include "yb/gutil/ref_counted.h"
#include "yb/util/monotime.h"

namespace yb {

class MonitoredTask : public RefCountedThreadSafe<MonitoredTask> {
 public:
  virtual ~MonitoredTask() {}

  enum State {
    kStateWaiting,  // RPC not issued, or is waiting to be retried.
    kStateRunning,  // RPC has been issued.
    kStateComplete,
    kStateFailed,
    kStateAborted,
  };

  static string state(State state) {
    switch (state) {
    case MonitoredTask::kStateWaiting:
      return "Waiting";
    case MonitoredTask::kStateRunning:
      return "Running";
    case MonitoredTask::kStateComplete:
      return "Complete";
    case MonitoredTask::kStateFailed:
      return "Failed";
    case MonitoredTask::kStateAborted:
      return "Aborted";
    }
    return "UNKNOWN_STATE";
  }

  // Abort the ongoing task.
  virtual void Abort() = 0;

  // Task State
  virtual State state() const = 0;

  // Task Type Identifier
  virtual std::string type_name() const = 0;

  // Task description
  virtual std::string description() const = 0;

  // Task start time, may be !Initialized()
  virtual MonoTime start_timestamp() const = 0;

  // Task completion time, may be !Initialized()
  virtual MonoTime completion_timestamp() const = 0;
};

} // namespace yb

#endif  // YB_SERVER_MONITORED_TASK_H
