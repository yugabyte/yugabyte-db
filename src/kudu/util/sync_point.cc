//  Licensed to the Apache Software Foundation (ASF) under one
//  or more contributor license agreements.  See the NOTICE file
//  distributed with this work for additional information
//  regarding copyright ownership.  The ASF licenses this file
//  to you under the Apache License, Version 2.0 (the
//  "License"); you may not use this file except in compliance
//  with the License.  You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
//  Unless required by applicable law or agreed to in writing,
//  software distributed under the License is distributed on an
//  "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
//  KIND, either express or implied.  See the License for the
//  specific language governing permissions and limitations
//  under the License.
//
//  Copyright (c) 2014, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.

#include "kudu/util/sync_point.h"

using std::string;
using std::vector;

#ifndef NDEBUG
namespace kudu {

SyncPoint::Dependency::Dependency(string predecessor, string successor)
    : predecessor_(std::move(predecessor)), successor_(std::move(successor)) {}

SyncPoint::SyncPoint()
  : cv_(&mutex_),
    enabled_(false) {
}

SyncPoint* SyncPoint::GetInstance() {
  static SyncPoint sync_point;
  return &sync_point;
}

void SyncPoint::LoadDependency(const vector<Dependency>& dependencies) {
  successors_.clear();
  predecessors_.clear();
  cleared_points_.clear();
  for (const Dependency& dependency : dependencies) {
    successors_[dependency.predecessor_].push_back(dependency.successor_);
    predecessors_[dependency.successor_].push_back(dependency.predecessor_);
  }
}

bool SyncPoint::PredecessorsAllCleared(const string& point) {
  for (const string& pred : predecessors_[point]) {
    if (cleared_points_.count(pred) == 0) {
      return false;
    }
  }
  return true;
}

void SyncPoint::EnableProcessing() {
  MutexLock lock(mutex_);
  enabled_ = true;
}

void SyncPoint::DisableProcessing() {
  MutexLock lock(mutex_);
  enabled_ = false;
}

void SyncPoint::ClearTrace() {
  MutexLock lock(mutex_);
  cleared_points_.clear();
}

void SyncPoint::Process(const string& point) {
  MutexLock lock(mutex_);

  if (!enabled_) return;

  while (!PredecessorsAllCleared(point)) {
    cv_.Wait();
  }

  cleared_points_.insert(point);
  cv_.Broadcast();
}

}  // namespace kudu
#endif  // NDEBUG
