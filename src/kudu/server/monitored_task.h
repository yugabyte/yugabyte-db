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

#ifndef KUDU_MONITORED_TASK_H
#define KUDU_MONITORED_TASK_H

#include <string>

#include "kudu/gutil/ref_counted.h"
#include "kudu/util/monotime.h"

namespace kudu {

class MonitoredTask : public RefCountedThreadSafe<MonitoredTask> {
 public:
  virtual ~MonitoredTask() {}

    enum State {
      kStatePreparing,
      kStateRunning,
      kStateComplete,
      kStateFailed,
      kStateAborted,
    };

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

} // namespace kudu

#endif
