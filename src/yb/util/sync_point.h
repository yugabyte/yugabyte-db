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
#pragma once

#include <condition_variable>
#include <functional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace yb {

// This class provides facility to reproduce race conditions deterministically
// in unit tests.
// Developer could specify sync points in the codebase via TEST_SYNC_POINT and
// DEBUG_ONLY_TEST_SYNC_POINT. Each sync point represents a position in the execution stream of a
// thread. In the unit test, 'Happens After' relationship among sync points could be setup via
// SyncPoint::LoadDependency, to reproduce a desired interleave of threads execution. Refer to
// (DBTest,TransactionLogIteratorRace), for an example use case.

class SyncPoint {
 public:
  static SyncPoint* GetInstance();

  struct Dependency {
    std::string predecessor;
    std::string successor;
  };
  // call once at the beginning of a test to setup the dependency between
  // sync points
  void LoadDependency(const std::vector<Dependency>& dependencies);

  // Set up a call back function in sync point.
  void SetCallBack(const std::string& point, std::function<void(void*)> callback);
  // Clear all call back functions.
  void ClearAllCallBacks();

  // enable sync point processing (disabled on startup)
  void EnableProcessing();

  // disable sync point processing
  void DisableProcessing();

  // remove the execution trace of all sync points
  void ClearTrace();

  // triggered by TEST_SYNC_POINT and DEBUG_ONLY_TEST_SYNC_POINT, blocking execution until all
  // predecessors are executed. And/or call registered callback function, with argument `cb_arg`
  void Process(const std::string& point, void* cb_arg = nullptr);

  // TODO: it might be useful to provide a function that blocks until all
  // sync points are cleared.

 private:
  SyncPoint() {}

  bool PredecessorsAllCleared(const std::string& point);

  // successor/predecessor map loaded from LoadDependency
  std::unordered_map<std::string, std::vector<std::string> > successors_;
  std::unordered_map<std::string, std::vector<std::string> > predecessors_;
  std::unordered_map<std::string, std::function<void(void*)> > callbacks_;

  std::mutex mutex_;
  std::condition_variable cv_;
  // sync points that have been passed through
  std::unordered_set<std::string> cleared_points_;
  bool enabled_ = false;
  int num_callbacks_running_ = 0;
};

void TEST_sync_point(const std::string& point, void* cb_arg = nullptr);

// Use TEST_SYNC_POINT to specify sync points inside code base.
// Sync points can have happens-after depedency on other sync points,
// configured at runtime via SyncPoint::LoadDependency. This could be
// utilized to re-produce race conditions between threads.
// See TransactionLogIteratorRace in db_test.cc for an example use case.
#define TEST_SYNC_POINT(x) yb::TEST_sync_point(x)

#define TEST_SYNC_POINT_CALLBACK(x, y) yb::TEST_sync_point(x, y)

// Use DEBUG_ONLY_TEST_SYNC_POINT in perf sensitive code path. It is no op in release build.
// Sync point processing only happens in test mode (FLAGS_TEST_running_test), but it still requires
// a function call and boolean check which in some places need to be avoided.
#ifdef NDEBUG
#define DEBUG_ONLY_TEST_SYNC_POINT(x)
#define DEBUG_ONLY_TEST_SYNC_POINT_CALLBACK(x, y)
#else
#define DEBUG_ONLY_TEST_SYNC_POINT(x) TEST_SYNC_POINT(x)
#define DEBUG_ONLY_TEST_SYNC_POINT_CALLBACK(x, y) TEST_SYNC_POINT_CALLBACK(x, y)
#endif  // NDEBUG

}  // namespace yb
