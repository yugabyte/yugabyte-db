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

#include "yb/util/failure_detector.h"

#include <mutex>
#include <unordered_map>

#include "yb/util/logging.h"

#include "yb/gutil/map-util.h"
#include "yb/gutil/stl_util.h"

#include "yb/util/locks.h"
#include "yb/util/status.h"
#include "yb/util/status_log.h"
#include "yb/util/thread.h"

namespace yb {

using std::unordered_map;
using std::string;
using strings::Substitute;

const int64_t RandomizedFailureMonitor::kMinWakeUpTimeMillis = 10;

TimedFailureDetector::TimedFailureDetector(MonoDelta failure_period)
    : failure_period_(std::move(failure_period)) {}

TimedFailureDetector::~TimedFailureDetector() {
  STLDeleteValues(&nodes_);
}

Status TimedFailureDetector::Track(const string& name,
                                   const MonoTime& now,
                                   const FailureDetectedCallback& callback) {
  std::lock_guard lock(lock_);
  auto node = std::make_unique<Node>();
  node->permanent_name = name;
  node->callback = callback;
  node->last_heard_of = now;
  node->status = ALIVE;
  if (!InsertIfNotPresent(&nodes_, name, node.get())) {
    return STATUS(AlreadyPresent,
        Substitute("Node with name '$0' is already being monitored", name));
  }
  node.release();
  return Status::OK();
}

Status TimedFailureDetector::UnTrack(const string& name) {
  std::lock_guard lock(lock_);
  Node* node = EraseKeyReturnValuePtr(&nodes_, name);
  if (PREDICT_FALSE(node == NULL)) {
    return STATUS(NotFound, Substitute("Node with name '$0' not found", name));
  }
  delete node;
  return Status::OK();
}

bool TimedFailureDetector::IsTracking(const std::string& name) {
  std::lock_guard lock(lock_);
  return ContainsKey(nodes_, name);
}

Status TimedFailureDetector::MessageFrom(const std::string& name, const MonoTime& now) {
  VLOG(3) << "Received message from " << name << " at " << now.ToString();
  std::lock_guard lock(lock_);
  Node* node = FindPtrOrNull(nodes_, name);
  if (node == NULL) {
    VLOG(1) << "Not tracking node: " << name;
    return STATUS(NotFound, Substitute("Message from unknown node '$0'", name));
  }
  node->last_heard_of = now;
  node->status = ALIVE;
  return Status::OK();
}

FailureDetector::NodeStatus TimedFailureDetector::GetNodeStatusUnlocked(const std::string& name,
                                                                        const MonoTime& now) {
  Node* node = FindOrDie(nodes_, name);
  if (now.GetDeltaSince(node->last_heard_of).MoreThan(failure_period_)) {
    node->status = DEAD;
  }
  return node->status;
}

void TimedFailureDetector::CheckForFailures(const MonoTime& now) {
  unordered_map<string, FailureDetectedCallback> callbacks;
  {
    std::lock_guard lock(lock_);
    for (const auto& entry : nodes_) {
      if (GetNodeStatusUnlocked(entry.first, now) == DEAD) {
        InsertOrDie(&callbacks, entry.first, entry.second->callback);
      }
    }
  }

  // Invoke failure callbacks outside of lock.
  for (const auto& entry : callbacks) {
    const string& node_name = entry.first;
    const FailureDetectedCallback& callback = entry.second;
    callback.Run(node_name, STATUS(RemoteError, Substitute("Node '$0' failed", node_name)));
  }
}

RandomizedFailureMonitor::RandomizedFailureMonitor(uint32_t random_seed,
                                                   int64_t period_mean_millis,
                                                   int64_t period_stddev_millis)
    : period_mean_millis_(period_mean_millis),
      period_stddev_millis_(period_stddev_millis),
      random_(random_seed),
      run_latch_(0),
      shutdown_(false) {
}

RandomizedFailureMonitor::~RandomizedFailureMonitor() {
  Shutdown();
}

Status RandomizedFailureMonitor::Start() {
  CHECK(!thread_);
  run_latch_.Reset(1);
  return Thread::Create("failure-monitors", "failure-monitor",
                        &RandomizedFailureMonitor::RunThread,
                        this, &thread_);
}

void RandomizedFailureMonitor::Shutdown() {
  if (!thread_) {
    return;
  }

  {
    std::lock_guard l(lock_);
    if (shutdown_) {
      return;
    }
    shutdown_ = true;
  }

  run_latch_.CountDown();
  CHECK_OK(ThreadJoiner(thread_.get()).Join());
  thread_.reset();
}

Status RandomizedFailureMonitor::MonitorFailureDetector(const string& name,
                                                        const scoped_refptr<FailureDetector>& fd) {
  std::lock_guard l(lock_);
  bool inserted = InsertIfNotPresent(&fds_, name, fd);
  if (PREDICT_FALSE(!inserted)) {
    return STATUS(AlreadyPresent, Substitute("Already monitoring failure detector '$0'", name));
  }
  return Status::OK();
}

Status RandomizedFailureMonitor::UnmonitorFailureDetector(const string& name) {
  std::lock_guard l(lock_);
  auto count = fds_.erase(name);
  if (PREDICT_FALSE(count == 0)) {
    return STATUS(NotFound, Substitute("Failure detector '$0' not found", name));
  }
  return Status::OK();
}

void RandomizedFailureMonitor::RunThread() {
  VLOG(1) << "Failure monitor thread starting";

  while (true) {
    int64_t wait_millis = random_.Normal(period_mean_millis_, period_stddev_millis_);
    if (wait_millis < kMinWakeUpTimeMillis) {
      wait_millis = kMinWakeUpTimeMillis;
    }

    MonoDelta wait_delta = MonoDelta::FromMilliseconds(wait_millis);
    VLOG(3) << "RandomizedFailureMonitor sleeping for: " << wait_delta.ToString();
    if (run_latch_.WaitFor(wait_delta)) {
      // CountDownLatch reached 0.
      std::lock_guard lock(lock_);
      // Check if we were told to shutdown.
      if (shutdown_) {
        // Latch fired: exit loop.
        VLOG(1) << "RandomizedFailureMonitor thread shutting down";
        return;
      }
    }

    // Take a copy of the FD map under the lock.
    FDMap fds_copy;
    {
      std::lock_guard l(lock_);
      fds_copy = fds_;
    }

    MonoTime now = MonoTime::Now();
    for (const FDMap::value_type& entry : fds_copy) {
      entry.second->CheckForFailures(now);
    }
  }
}

}  // namespace yb
