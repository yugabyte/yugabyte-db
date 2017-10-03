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

#include "kudu/server/logical_clock.h"

#include "kudu/gutil/atomicops.h"
#include "kudu/gutil/bind.h"
#include "kudu/gutil/strings/substitute.h"
#include "kudu/util/metrics.h"
#include "kudu/util/monotime.h"
#include "kudu/util/status.h"

namespace kudu {
namespace server {

METRIC_DEFINE_gauge_uint64(server, logical_clock_timestamp,
                           "Logical Clock Timestamp",
                           kudu::MetricUnit::kUnits,
                           "Logical clock timestamp.");

using base::subtle::Atomic64;
using base::subtle::Barrier_AtomicIncrement;
using base::subtle::NoBarrier_CompareAndSwap;

Timestamp LogicalClock::Now() {
  return Timestamp(Barrier_AtomicIncrement(&now_, 1));
}

Timestamp LogicalClock::NowLatest() {
  return Now();
}

Status LogicalClock::Update(const Timestamp& to_update) {
  DCHECK_NE(to_update.value(), Timestamp::kInvalidTimestamp.value())
      << "Updating the clock with an invalid timestamp";
  Atomic64 new_value = to_update.value();

  while (true) {
    Atomic64 current_value = NoBarrier_Load(&now_);
    // if the incoming value is less than the current one, or we've failed the
    // CAS because the current clock increased to higher than the incoming value,
    // we can stop the loop now.
    if (new_value <= current_value) return Status::OK();
    // otherwise try a CAS
    if (PREDICT_TRUE(NoBarrier_CompareAndSwap(&now_, current_value, new_value)
        == current_value))
      break;
  }
  return Status::OK();
}

Status LogicalClock::WaitUntilAfter(const Timestamp& then,
                                    const MonoTime& deadline) {
  return Status::ServiceUnavailable(
      "Logical clock does not support WaitUntilAfter()");
}

Status LogicalClock::WaitUntilAfterLocally(const Timestamp& then,
                                           const MonoTime& deadline) {
  if (IsAfter(then)) return Status::OK();
  return Status::ServiceUnavailable(
      "Logical clock does not support WaitUntilAfterLocally()");
}

bool LogicalClock::IsAfter(Timestamp t) {
  return base::subtle::Acquire_Load(&now_) >= t.value();
}

LogicalClock* LogicalClock::CreateStartingAt(const Timestamp& timestamp) {
  // initialize at 'timestamp' - 1 so that the  first output value is 'timestamp'.
  return new LogicalClock(timestamp.value() - 1);
}

uint64_t LogicalClock::NowForMetrics() {
  // We don't want reading metrics to change the clock.
  return NoBarrier_Load(&now_);
}


void LogicalClock::RegisterMetrics(const scoped_refptr<MetricEntity>& metric_entity) {
  METRIC_logical_clock_timestamp.InstantiateFunctionGauge(
      metric_entity,
      Bind(&LogicalClock::NowForMetrics, Unretained(this)))
    ->AutoDetachToLastValue(&metric_detacher_);
}

string LogicalClock::Stringify(Timestamp timestamp) {
  return strings::Substitute("L: $0", timestamp.ToUint64());
}

}  // namespace server
}  // namespace kudu

