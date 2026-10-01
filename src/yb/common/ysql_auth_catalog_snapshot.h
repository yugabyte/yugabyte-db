// Copyright (c) YugabyteDB, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file except
// in compliance with the License. You may obtain a copy of the License at
//
// http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software distributed under the License
// is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express
// or implied. See the License for the specific language governing permissions and limitations
// under the License.

#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>

#include "yb/util/monotime.h"
#include "yb/util/sync_point.h"

namespace yb {

// Bound clock/lease waits separately from ordinary RPC work. These are initial rollout limits,
// not independently tunable operator settings; tests can shrink them without bypassing admission.
struct YsqlAuthSnapshotLimits {
  size_t workers;
  size_t max_tasks;
};

inline YsqlAuthSnapshotLimits MasterAuthSnapshotLimits() {
  YsqlAuthSnapshotLimits limits{32, 128};
  TEST_SYNC_POINT_CALLBACK("MasterClusterService::AuthSnapshot::Limits", &limits);
  return limits;
}

inline YsqlAuthSnapshotLimits TserverAuthSnapshotLimits() {
  YsqlAuthSnapshotLimits limits{16, 64};
  TEST_SYNC_POINT_CALLBACK("PgClientService::AuthSnapshot::Limits", &limits);
  return limits;
}

inline CoarseTimePoint AuthSnapshotDeadline(CoarseTimePoint rpc_deadline, const char* test_point) {
  auto timeout = std::chrono::milliseconds(5000);
  TEST_SYNC_POINT_CALLBACK(test_point, &timeout);
  return std::min(rpc_deadline, CoarseMonoClock::Now() + timeout);
}

}  // namespace yb
