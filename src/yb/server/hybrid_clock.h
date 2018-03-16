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

#ifndef YB_SERVER_HYBRID_CLOCK_H_
#define YB_SERVER_HYBRID_CLOCK_H_

#include <atomic>
#include <string>
#if !defined(__APPLE__)
#include <sys/timex.h>
#endif // !defined(__APPLE__)

#include "yb/gutil/ref_counted.h"
#include "yb/server/clock.h"
#include "yb/util/locks.h"
#include "yb/util/metrics.h"
#include "yb/util/physical_time.h"

namespace yb {
namespace server {

// The HybridTime clock.
//
// HybridTime should not be used on a distributed cluster running on OS X hosts,
// since NTP clock error is not available.
class HybridClock : public Clock {
 public:
  HybridClock();
  explicit HybridClock(PhysicalClockPtr clock);
  explicit HybridClock(const std::string& time_source);

  virtual CHECKED_STATUS Init() override;

  // Obtains the hybrid_time corresponding to the current time.
  virtual HybridTime Now() override;

  // Obtains the hybrid_time corresponding to latest possible current
  // time.
  virtual HybridTime NowLatest() override;

  // Updates the clock with a hybrid_time originating on another machine.
  virtual void Update(const HybridTime& to_update) override;

  virtual void RegisterMetrics(const scoped_refptr<MetricEntity>& metric_entity) override;

  // Obtains the hybrid_time corresponding to the current time and the associated
  // error in micros. This may fail if the clock is unsynchronized or synchronized
  // but the error is too high and, since we can't do anything about it,
  // LOG(FATAL)'s in that case.
  void NowWithError(HybridTime* hybrid_time, uint64_t* max_error_usec);

  virtual std::string Stringify(HybridTime hybrid_time) override;

  // Static encoding/decoding methods for hybrid_times. Public mostly
  // for testing/debugging purposes.

  // Returns the logical value embedded in 'hybrid_time'
  static uint64_t GetLogicalValue(const HybridTime& hybrid_time);

  // Returns the physical value embedded in 'hybrid_time', in microseconds.
  static uint64_t GetPhysicalValueMicros(const HybridTime& hybrid_time);

  // Returns the physical value embedded in 'hybrid_time', in nanoseconds.
  static uint64_t GetPhysicalValueNanos(const HybridTime& hybrid_time);

  // Obtains a new HybridTime with the logical value zeroed out.
  static HybridTime HybridTimeFromMicroseconds(uint64_t micros);

  // Obtains a new HybridTime that embeds both the physical and logical values.
  static HybridTime HybridTimeFromMicrosecondsAndLogicalValue(
      MicrosTime micros, LogicalTimeComponent logical_value);

  // Creates a new hybrid_time whose physical time is GetPhysicalValue(original) +
  // 'micros_to_add' and which retains the same logical value.
  static HybridTime AddPhysicalTimeToHybridTime(const HybridTime& original,
                                                const MonoDelta& to_add);

  // Given two hybrid times, determines whether the delta between end and begin them is higher,
  // lower or equal to the given delta and returns 1, -1 and 0 respectively. Note that if end <
  // begin we return -1.
  static int CompareHybridClocksToDelta(const HybridTime& begin, const HybridTime& end,
                                        const MonoDelta& delta);

  // Outputs a string containing the physical and logical values of the hybrid_time,
  // separated.
  static std::string StringifyHybridTime(const HybridTime& hybrid_time);

  static void RegisterProvider(std::string name, PhysicalClockProvider provider);

  const PhysicalClockPtr& TEST_clock() { return clock_; }

 private:
  PhysicalClockPtr clock_;

  // Used to get the hybrid_time for metrics.
  uint64_t NowForMetrics();

  // Used to get the current error, for metrics.
  uint64_t ErrorForMetrics();

  struct HybridClockComponents {
    // the last clock read/update, in microseconds.
    uint64_t last_usec;
    // the next logical value to be assigned to a hybrid_time
    uint64_t logical;

    bool operator< (const HybridClockComponents& o) const {
      return last_usec < o.last_usec || last_usec == o.last_usec && logical < o.logical;
    }

    bool operator<= (const HybridClockComponents& o) const {
      return last_usec < o.last_usec || last_usec == o.last_usec && logical <= o.logical;
    }
  };
  std::atomic<HybridClockComponents> components_{{0, 0}};

  // How many bits to left shift a microseconds clock read. The remainder
  // of the hybrid_time will be reserved for logical values.
  static const int kBitsToShift;

  // Mask to extract the pure logical bits.
  static const uint64_t kLogicalBitMask;

  // The scaling factor used to obtain ppms. From the adjtimex source:
  // "scale factor used by adjtimex freq param.  1 ppm = 65536"
  static const double kAdjtimexScalingFactor;

  enum State {
    kNotInitialized,
    kInitialized
  };

  State state_ = kNotInitialized;

  // Clock metrics are set to detach to their last value. This means
  // that, during our destructor, we'll need to access other class members
  // declared above this. Hence, this member must be declared last.
  FunctionGaugeDetacher metric_detacher_;
};

}  // namespace server
}  // namespace yb

#endif /* YB_SERVER_HYBRID_CLOCK_H_ */
