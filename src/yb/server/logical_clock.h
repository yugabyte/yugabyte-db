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

#include <string>

#include "yb/server/clock.h"
#include "yb/util/status_fwd.h"

namespace yb {
class MonoDelta;
class MonoTime;
namespace server {

// An implementation of Clock that behaves as a plain Lamport Clock.  In a single node, single
// tablet, setting this generates exactly the same Timestamp sequence as the original MvccManager
// did, but it can be updated to make sure replicas generate new hybrid_times on becoming leader.
// This can be used as a deterministic hybrid_time generator that has the same consistency
// properties as a HybridTime clock.
//
// The Wait* methods are unavailable in this implementation and will return
// Status::ServiceUnavailable().
//
// NOTE: this class is thread safe.
class LogicalClock : public Clock {
 public:
  Status Init() override;

  // Returns the current value of the clock without incrementing it.
  HybridTime Peek();

  virtual void Update(const HybridTime& to_update) override;

  virtual void RegisterMetrics(const scoped_refptr<MetricEntity>& metric_entity) override;

  // Creates a logical clock whose first output value on a Now() call is 'hybrid_time'.
  static LogicalClock* CreateStartingAt(const HybridTime& hybrid_time);

 private:
  // Should use LogicalClock::CreatingStartingAt()
  explicit LogicalClock(HybridTime::val_type initial_time)
      : now_(initial_time) {}

  // Used to get the hybrid_time for metrics.
  uint64_t NowForMetrics();

  HybridTimeRange NowRange() override;

  std::atomic<uint64_t> now_;

  std::shared_ptr<void> metric_detacher_;
};

}  // namespace server
}  // namespace yb
