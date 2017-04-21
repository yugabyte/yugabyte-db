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

#ifndef YB_SERVER_HYBRID_CLOCK_H_
#define YB_SERVER_HYBRID_CLOCK_H_

#include <string>

#include "yb/gutil/ref_counted.h"
#include "yb/server/clock.h"
#include "yb/util/locks.h"
#include "yb/util/metrics.h"
#include "yb/util/status.h"

namespace yb {
namespace server {

// The HybridTime clock.
//
// HybridTime should not be used on a distributed cluster running on OS X hosts,
// since NTP clock error is not available.
class HybridClock : public Clock {
 public:
  HybridClock();

  virtual CHECKED_STATUS Init() override;

  // Obtains the hybrid_time corresponding to the current time.
  virtual HybridTime Now() override;

  // Obtains the hybrid_time corresponding to latest possible current
  // time.
  virtual HybridTime NowLatest() override;

  // Obtain a hybrid_time which is guaranteed to be later than the current time
  // on any machine in the cluster.
  //
  // NOTE: this is not a very tight bound.
  virtual CHECKED_STATUS GetGlobalLatest(HybridTime* t) override;

  // Updates the clock with a hybrid_time originating on another machine.
  virtual CHECKED_STATUS Update(const HybridTime& to_update) override;

  virtual void RegisterMetrics(const scoped_refptr<MetricEntity>& metric_entity) override;

  // HybridClock supports all external consistency modes.
  virtual bool SupportsExternalConsistencyMode(ExternalConsistencyMode mode) override;

  // Blocks the caller thread until the true time is after 'then'.
  // In other words, waits until the HybridClock::Now() on _all_ nodes
  // will return a value greater than 'then'.
  //
  // The incoming time 'then' is assumed to be the latest time possible
  // at the time the read was performed, i.e. 'then' = now + max_error.
  //
  // This method can be used to make YB behave like Spanner/TrueTime.
  // This is implemented by possibly making the caller thread wait for a
  // a certain period of time.
  //
  // As an example, the following cases might happen:
  //
  // 1 - 'then' is lower than now.earliest() -> Definitely in
  // the past, no wait necessary.
  //
  // 2 - 'then' is greater than > now.earliest(): need to wait until
  // 'then' <= now.earliest()
  //
  // Returns OK if it waited long enough or if no wait was necessary.
  //
  // Returns Status::ServiceUnavailable if the system clock was not
  // synchronized and therefore it couldn't wait out the error.
  //
  // Returns Status::TimedOut() if 'deadline' will pass before the specified
  // hybrid_time. NOTE: unlike most "wait" methods, this may return _immediately_
  // with a timeout, rather than actually waiting for the timeout to expire.
  // This is because, by looking at the current clock, we can know how long
  // we'll have to wait, in contrast to most Wait() methods which are waiting
  // on some external condition to become true.
  virtual CHECKED_STATUS WaitUntilAfter(const HybridTime& then,
                                const MonoTime& deadline) override;

  // Blocks the caller thread until the local time is after 'then'.
  // This is in contrast to the above method, which waits until the time
  // on _all_ machines is past the given time.
  //
  // Returns Status::TimedOut() if 'deadline' will pass before the specified
  // hybrid_time. NOTE: unlike most "wait" methods, this may return _immediately_
  // with a timeout. See WaitUntilAfter() for details.
  virtual CHECKED_STATUS WaitUntilAfterLocally(const HybridTime& then,
                                       const MonoTime& deadline) override;

  // Return true if the given time has passed (i.e any future call
  // to Now() would return a higher value than t).
  //
  // NOTE: this only refers to the _local_ clock, and is not a guarantee
  // that other nodes' clocks have definitely passed this hybrid_time.
  // This is in contrast to WaitUntilAfter() above.
  virtual bool IsAfter(HybridTime t) override;

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
  static HybridTime HybridTimeFromMicrosecondsAndLogicalValue(uint64_t micros,
                                                            uint64_t logical_value);

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

  // Sets the time to be returned by a mock call to the system clock, for tests.
  // Requires that 'FLAGS_use_mock_wall_clock' is set to true and that 'now_usec' is less
  // than the previously set time.
  // NOTE: This refers to the time returned by the system clock, not the time returned
  // by HybridClock, i.e. 'now_usec' is not a HybridTime timestmap and shouldn't have
  // a logical component.
  void SetMockClockWallTimeForTests(uint64_t now_usec);

  // Sets the max. error to be returned by a mock call to the system clock, for tests.
  // Requires that 'FLAGS_use_mock_wall_clock' is set to true.
  // This can be used to make HybridClock report the wall clock as unsynchronized, by
  // setting error to be more than the configured tolerance.
  void SetMockMaxClockErrorForTests(uint64_t max_error_usec);

 private:

  // Obtains the current wallclock time and maximum error in microseconds,
  // and checks if the clock is synchronized.
  //
  // On OS X, the error will always be 0.
  yb::Status WalltimeWithError(uint64_t* now_usec, uint64_t* error_usec);

  // Used to get the hybrid_time for metrics.
  uint64_t NowForMetrics();

  // Used to get the current error, for metrics.
  uint64_t ErrorForMetrics();

  // Set by calls to SetMockClockWallTimeForTests().
  // For testing purposes only.
  uint64_t mock_clock_time_usec_;

  // Set by calls to SetMockClockErrorForTests().
  // For testing purposes only.
  uint64_t mock_clock_max_error_usec_;

#if !defined(__APPLE__)
  uint64_t divisor_;
#endif

  double tolerance_adjustment_;

  mutable simple_spinlock lock_;

  // the last clock read/update, in microseconds.
  uint64_t last_usec_;
  // the next logical value to be assigned to a hybrid_time
  uint64_t next_logical_;

  // How many bits to left shift a microseconds clock read. The remainder
  // of the hybrid_time will be reserved for logical values.
  static const int kBitsToShift;

  // Mask to extract the pure logical bits.
  static const uint64_t kLogicalBitMask;

  static const uint64_t kNanosPerSec;

  // The scaling factor used to obtain ppms. From the adjtimex source:
  // "scale factor used by adjtimex freq param.  1 ppm = 65536"
  static const double kAdjtimexScalingFactor;

  enum State {
    kNotInitialized,
    kInitialized
  };

  State state_;

  // Clock metrics are set to detach to their last value. This means
  // that, during our destructor, we'll need to access other class members
  // declared above this. Hence, this member must be declared last.
  FunctionGaugeDetacher metric_detacher_;
};

}  // namespace server
}  // namespace yb

#endif /* YB_SERVER_HYBRID_CLOCK_H_ */
