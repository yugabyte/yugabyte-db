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

#ifndef KUDU_SERVER_LOGICAL_CLOCK_H_
#define KUDU_SERVER_LOGICAL_CLOCK_H_

#include <string>

#include "kudu/server/clock.h"
#include "kudu/util/metrics.h"
#include "kudu/util/status.h"

namespace kudu {
class MonoDelta;
class MonoTime;
namespace server {

// An implementation of Clock that behaves as a plain Lamport Clock.
// In a single node, single tablet, setting this generates exactly the
// same Timestamp sequence as the original MvccManager did, but it can be
// updated to make sure replicas generate new timestamps on becoming leader.
// This can be used as a deterministic timestamp generator that has the same
// consistency properties as a HybridTime clock.
//
// The Wait* methods are unavailable in this implementation and will
// return Status::ServiceUnavailable().
//
// NOTE: this class is thread safe.
class LogicalClock : public Clock {
 public:

  virtual Status Init() OVERRIDE { return Status::OK(); }

  virtual Timestamp Now() OVERRIDE;

  // In the logical clock this call is equivalent to Now();
  virtual Timestamp NowLatest() OVERRIDE;

  virtual Status Update(const Timestamp& to_update) OVERRIDE;

  // The Wait*() functions are not available for this clock.
  virtual Status WaitUntilAfter(const Timestamp& then,
                                const MonoTime& deadline) OVERRIDE;
  virtual Status WaitUntilAfterLocally(const Timestamp& then,
                                       const MonoTime& deadline) OVERRIDE;

  virtual bool IsAfter(Timestamp t) OVERRIDE;

  virtual void RegisterMetrics(const scoped_refptr<MetricEntity>& metric_entity) OVERRIDE;

  virtual std::string Stringify(Timestamp timestamp) OVERRIDE;

  // Logical clock doesn't support COMMIT_WAIT.
  virtual bool SupportsExternalConsistencyMode(ExternalConsistencyMode mode) OVERRIDE {
    return mode != COMMIT_WAIT;
  }

  // Creates a logical clock whose first output value on a Now() call is 'timestamp'.
  static LogicalClock* CreateStartingAt(const Timestamp& timestamp);

 private:
  // Should use LogicalClock::CreatingStartingAt()
  explicit LogicalClock(Timestamp::val_type initial_time) : now_(initial_time) {}

  // Used to get the timestamp for metrics.
  uint64_t NowForMetrics();

  base::subtle::Atomic64 now_;

  FunctionGaugeDetacher metric_detacher_;
};

}  // namespace server
}  // namespace kudu

#endif /* KUDU_SERVER_LOGICAL_CLOCK_H_ */

