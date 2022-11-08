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

#pragma once

#include <functional>
#include <string>

#include "yb/common/clock.h"
#include "yb/common/hybrid_time.h"

#include "yb/gutil/ref_counted.h"

#include "yb/util/status_fwd.h"
#include "yb/util/monotime.h"

namespace yb {
class faststring;
class MetricEntity;
class MonoDelta;
class Slice;
class Status;
namespace server {

// An interface for a clock that can be used to assign timestamps to operations.
// Implementations must respect the following assumptions:
// 1 - Now() must return monotonically increasing numbers
//     i.e. for any two calls, i.e. Now returns timestamp1 and timestamp2, it must
//     hold that timestamp1 < timestamp2.
// 2 - Update() must never set the clock backwards (corollary of 1).
class Clock : public ClockBase {
 public:

  // Initializes the clock.
  virtual Status Init() = 0;

  // Update the clock with a transaction timestamp originating from
  // another server. For instance replicas can call this so that,
  // if elected leader, they are guaranteed to generate timestamps
  // higher than the timestamp of the last transaction accepted from the
  // leader.
  virtual void Update(const HybridTime& to_update) = 0;

  // Register the clock metrics in the given entity.
  virtual void RegisterMetrics(const scoped_refptr<MetricEntity>& metric_entity) = 0;

  virtual ~Clock() {}
};

typedef scoped_refptr<Clock> ClockPtr;

template <class Request>
void UpdateClock(const Request& request, Clock* clock) {
  auto propagated_hybrid_time = HybridTime::FromPB(request.propagated_hybrid_time());
  // If the client sent us a hybrid_time, decode it and update the clock so that all future
  // hybrid_times are greater than the passed hybrid_time.
  if (!propagated_hybrid_time) {
    return;
  }
  clock->Update(propagated_hybrid_time);
}

} // namespace server
} // namespace yb
