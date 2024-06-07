//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
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

#include "yb/util/callsite_profiling.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/sync_point.h"

DEFINE_test_flag(bool, enable_sync_points, false, "Enable sync points for testing.");

namespace yb {

SyncPoint* SyncPoint::GetInstance() {
  static SyncPoint sync_point;
  return &sync_point;
}

void SyncPoint::LoadDependency(const std::vector<Dependency>& dependencies) {
  std::unique_lock lock(mutex_);
  successors_.clear();
  predecessors_.clear();
  cleared_points_.clear();
  for (const auto& dependency : dependencies) {
    successors_[dependency.predecessor].push_back(dependency.successor);
    predecessors_[dependency.successor].push_back(dependency.predecessor);
  }
  YB_PROFILE(cv_.notify_all());
}

bool SyncPoint::PredecessorsAllCleared(const std::string& point) {
  if (!enabled_) {
    return true;
  }

  for (const auto& pred : predecessors_[point]) {
    if (cleared_points_.count(pred) == 0) {
      return false;
    }
  }
  return true;
}

void SyncPoint::SetCallBack(const std::string& point, std::function<void(void*)> callback) {
  std::unique_lock lock(mutex_);
  callbacks_[point] = callback;
}

void SyncPoint::ClearAllCallBacks() {
  std::unique_lock lock(mutex_);
  while (num_callbacks_running_ > 0) {
    cv_.wait(lock);
  }
  callbacks_.clear();
}

void SyncPoint::EnableProcessing() {
  DCHECK(FLAGS_TEST_enable_sync_points);
  std::unique_lock lock(mutex_);
  enabled_ = true;
}

void SyncPoint::DisableProcessing() {
  {
    std::unique_lock lock(mutex_);
    enabled_ = false;
  }
  YB_PROFILE(cv_.notify_all());
}

void SyncPoint::ClearTrace() {
  std::unique_lock lock(mutex_);
  cleared_points_.clear();
}

void SyncPoint::Process(const std::string& point, void* cb_arg) {
  std::unique_lock lock(mutex_);

  if (!enabled_) return;

  const auto callback_pair = callbacks_.find(point);
  if (callback_pair != callbacks_.end()) {
    ++num_callbacks_running_;
    lock.unlock();
    callback_pair->second(cb_arg);
    lock.lock();
    --num_callbacks_running_;
    YB_PROFILE(cv_.notify_all());
  }

  if (!PredecessorsAllCleared(point)) {
    LOG(INFO) << "Entered SyncPoint wait: " << point;
    cv_.wait(lock, [this, &point]() { return PredecessorsAllCleared(point); });
    LOG(INFO) << "Leaving SyncPoint wait: " << point;
  }

  cleared_points_.insert(point);
  YB_PROFILE(cv_.notify_all());
}

void TEST_sync_point(const std::string& point, void* cb_arg) {
  if (PREDICT_FALSE(FLAGS_TEST_enable_sync_points)) {
    yb::SyncPoint::GetInstance()->Process(point, cb_arg);
  }
}
}  // namespace yb
