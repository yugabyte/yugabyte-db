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

#include "yb/server/hybrid_clock.h"

#include <algorithm>
#include <mutex>

#include <glog/logging.h>
#include "yb/gutil/bind.h"
#include "yb/gutil/strings/substitute.h"
#include "yb/gutil/walltime.h"
#include "yb/util/debug/trace_event.h"
#include "yb/util/errno.h"
#include "yb/util/flag_tags.h"
#include "yb/util/locks.h"
#include "yb/util/logging.h"
#include "yb/util/metrics.h"
#include "yb/util/status.h"

DEFINE_uint64(max_clock_sync_error_usec, 10 * 1000 * 1000,
              "Maximum allowed clock synchronization error as reported by NTP "
              "before the server will abort.");
DEFINE_bool(disable_clock_sync_error, true,
            "Whether or not we should keep running if we detect a clock synchronization issue.");
TAG_FLAG(disable_clock_sync_error, advanced);
TAG_FLAG(max_clock_sync_error_usec, advanced);
TAG_FLAG(max_clock_sync_error_usec, runtime);

DEFINE_bool(use_hybrid_clock, true,
            "Whether HybridClock should be used as the default clock"
            " implementation. This should be disabled for testing purposes only.");
TAG_FLAG(use_hybrid_clock, hidden);

DEFINE_bool(use_mock_wall_clock, false,
            "Whether HybridClock should use a mock wall clock which is updated manually"
            "instead of reading time from the system clock, for tests.");
TAG_FLAG(use_mock_wall_clock, hidden);

METRIC_DEFINE_gauge_uint64(server, hybrid_clock_hybrid_time,
                           "Hybrid Clock HybridTime",
                           yb::MetricUnit::kMicroseconds,
                           "Hybrid clock hybrid_time.");
METRIC_DEFINE_gauge_uint64(server, hybrid_clock_error,
                           "Hybrid Clock Error",
                           yb::MetricUnit::kMicroseconds,
                           "Server clock maximum error.");

using yb::Status;
using strings::Substitute;

namespace yb {
namespace server {

namespace {

Status CheckDeadlineNotWithinMicros(const MonoTime& deadline, int64_t wait_for_usec) {
  if (!deadline.Initialized()) {
    // No deadline.
    return Status::OK();
  }
  int64_t us_until_deadline = deadline.GetDeltaSince(
      MonoTime::Now(MonoTime::FINE)).ToMicroseconds();
  if (us_until_deadline <= wait_for_usec) {
    return STATUS(TimedOut, Substitute(
        "specified time is $0us in the future, but deadline expires in $1us",
        wait_for_usec, us_until_deadline));
  }
  return Status::OK();
}

}  // anonymous namespace

const int HybridClock::kBitsToShift = HybridTime::kBitsForLogicalComponent;

const uint64_t HybridClock::kLogicalBitMask = HybridTime::kLogicalBitMask;

const uint64_t HybridClock::kNanosPerSec = 1000000;

const double HybridClock::kAdjtimexScalingFactor = 65536;

HybridClock::HybridClock()
    : mock_clock_time_usec_(0),
      mock_clock_max_error_usec_(0),
#if !defined(__APPLE__)
      divisor_(1),
#endif
      tolerance_adjustment_(1),
      last_usec_(0),
      next_logical_(0),
      state_(kNotInitialized) {
}

#if !defined(__APPLE__)
int HybridClock::NtpAdjtime(timex* timex) {
  return ntp_adjtime(timex);
}

int HybridClock::NtpGettime(ntptimeval* timeval) {
  return ntp_gettime(timeval);
}

// Returns the clock modes and checks if the clock is synchronized.
Status HybridClock::GetClockModes(timex* timex) {
  // this makes ntp_adjtime a read-only call
  timex->modes = 0;
  int rc = NtpAdjtime(timex);
  if (PREDICT_FALSE(rc == TIME_ERROR) && !FLAGS_disable_clock_sync_error) {
    return STATUS(ServiceUnavailable,
        Substitute("Error reading clock. Clock considered unsynchronized. Return code: $0", rc));
  }
  // TODO what to do about leap seconds? see KUDU-146
  if (PREDICT_FALSE(rc != TIME_OK) && rc != TIME_ERROR) {
    LOG(ERROR) << Substitute("TODO Server undergoing leap second. Return code: $0", rc);
  }
  return Status::OK();
}

// Returns the current time/max error and checks if the clock is synchronized.
Status HybridClock::GetClockTime(ntptimeval* timeval) {
  int rc = NtpGettime(timeval);
  switch (rc) {
    case TIME_OK:
      return Status::OK();
    case -1: // generic error
      return STATUS(ServiceUnavailable, "Error reading clock. ntp_gettime() failed",
                                        ErrnoToString(errno));
    case TIME_ERROR:
      if (!FLAGS_disable_clock_sync_error) {
        return STATUS(ServiceUnavailable, "Error reading clock. Clock considered unsynchronized");
      } else {
        return Status::OK();
      }
    default:
      // TODO what to do about leap seconds? see KUDU-146
      YB_LOG_FIRST_N(ERROR, 1) << "Server undergoing leap second. This may cause consistency issues"
        << " (rc=" << rc << ")";
      return Status::OK();
  }
}
#endif // !defined(__APPLE__)


Status HybridClock::Init() {
  if (PREDICT_FALSE(FLAGS_use_mock_wall_clock)) {
    LOG(WARNING) << "HybridClock set to mock the wall clock.";
    state_ = kInitialized;
    return Status::OK();
  }
#if defined(__APPLE__)
  LOG(WARNING) << "HybridClock initialized in local mode (OS X only). "
               << "Not suitable for distributed clusters.";
#else
  // Read the current time. This will return an error if the clock is not synchronized.
  uint64_t now_usec;
  uint64_t error_usec;
  RETURN_NOT_OK(WalltimeWithError(&now_usec, &error_usec));

  timex timex;
  RETURN_NOT_OK(GetClockModes(&timex));
  // read whether the STA_NANO bit is set to know whether we'll get back nanos
  // or micros in timeval.time.tv_usec. See:
  // http://stackoverflow.com/questions/16063408/does-ntp-gettime-actually-return-nanosecond-precision
  // set the timeval.time.tv_usec divisor so that we always get micros
  if (timex.status & STA_NANO) {
    divisor_ = 1000;
  } else {
    divisor_ = 1;
  }

  // Calculate the sleep skew adjustment according to the max tolerance of the clock.
  // Tolerance comes in parts per million but needs to be applied a scaling factor.
  tolerance_adjustment_ = (1 + ((timex.tolerance / kAdjtimexScalingFactor) / 1000000.0));

  LOG(INFO) << "HybridClock initialized. Resolution in nanos?: " << (divisor_ == 1000)
            << " Wait times tolerance adjustment: " << tolerance_adjustment_
            << " Current error (microseconds): " << error_usec;
#endif // defined(__APPLE__)

  state_ = kInitialized;

  return Status::OK();
}

HybridTime HybridClock::Now() {
  HybridTime now;
  uint64_t error;

  std::lock_guard<simple_spinlock> lock(lock_);
  NowWithError(&now, &error);
  return now;
}

HybridTime HybridClock::NowLatest() {
  HybridTime now;
  uint64_t error;

  {
    std::lock_guard<simple_spinlock> lock(lock_);
    NowWithError(&now, &error);
  }

  uint64_t now_latest = GetPhysicalValueMicros(now) + error;
  uint64_t now_logical = GetLogicalValue(now);

  return HybridTimeFromMicrosecondsAndLogicalValue(now_latest, now_logical);
}

Status HybridClock::GetGlobalLatest(HybridTime* t) {
  HybridTime now = Now();
  uint64_t now_latest = GetPhysicalValueMicros(now) + FLAGS_max_clock_sync_error_usec;
  uint64_t now_logical = GetLogicalValue(now);
  *t = HybridTimeFromMicrosecondsAndLogicalValue(now_latest, now_logical);
  return Status::OK();
}

void HybridClock::NowWithError(HybridTime* hybrid_time, uint64_t* max_error_usec) {

  DCHECK_EQ(state_, kInitialized) << "Clock not initialized. Must call Init() first.";

  uint64_t now_usec;
  uint64_t error_usec;
  Status s = WalltimeWithError(&now_usec, &error_usec);
  if (PREDICT_FALSE(!s.ok())) {
    LOG(FATAL) << Substitute("Couldn't get the current time: Clock unsynchronized. "
        "Status: $0", s.ToString());
  }

  // If the current time surpasses the last update just return it
  if (PREDICT_TRUE(now_usec > last_usec_)) {
    last_usec_ = now_usec;
    next_logical_ = 1;
    *hybrid_time = HybridTimeFromMicroseconds(last_usec_);
    *max_error_usec = error_usec;
    if (PREDICT_FALSE(VLOG_IS_ON(2))) {
      VLOG(2) << "Current clock is higher than the last one. Resetting logical values."
          << " Physical Value: " << now_usec << " usec Logical Value: 0  Error: "
          << error_usec;
    }
    return;
  }

  // We don't have the last time read max error since it might have originated
  // in another machine, but we can put a bound on the maximum error of the
  // hybrid_time we are providing.
  // In particular we know that the "true" time falls within the interval
  // now_usec +- now.maxerror so we get the following situations:
  //
  // 1)
  // --------|----------|----|---------|--------------------------> time
  //     now - e       now  last   now + e
  // 2)
  // --------|----------|--------------|------|-------------------> time
  //     now - e       now         now + e   last
  //
  // Assuming, in the worst case, that the "true" time is now - error we need to
  // always return: last - (now - e) as the new maximum error.
  // This broadens the error interval for both cases but always returns
  // a correct error interval.

  *max_error_usec = last_usec_ - (now_usec - error_usec);
  *hybrid_time = HybridTimeFromMicrosecondsAndLogicalValue(last_usec_, next_logical_);
  if (PREDICT_FALSE(VLOG_IS_ON(2))) {
    VLOG(2) << "Current clock is lower than the last one. Returning last read and incrementing"
        " logical values. Physical Value: " << now_usec << " usec Logical Value: "
        << next_logical_ << " Error: " << *max_error_usec;
  }
  next_logical_++;
}

Status HybridClock::Update(const HybridTime& to_update) {
  std::lock_guard<simple_spinlock> lock(lock_);
  HybridTime now;
  uint64_t error_ignored;
  NowWithError(&now, &error_ignored);

  if (PREDICT_TRUE(now.CompareTo(to_update) > 0)) return Status::OK();

  uint64_t to_update_physical = GetPhysicalValueMicros(to_update);
  uint64_t to_update_logical = GetLogicalValue(to_update);
  uint64_t now_physical = GetPhysicalValueMicros(now);

  // we won't update our clock if to_update is more than 'max_clock_sync_error_usec'
  // into the future as it might have been corrupted or originated from an out-of-sync
  // server.
  if(!CheckClockSyncError(to_update_physical - now_physical).ok()) {
    return STATUS(InvalidArgument, "Tried to update clock beyond the max. error.");
  }

  last_usec_ = to_update_physical;
  next_logical_ = to_update_logical + 1;
  return Status::OK();
}

bool HybridClock::SupportsExternalConsistencyMode(ExternalConsistencyMode mode) {
  return true;
}

Status HybridClock::WaitUntilAfter(const HybridTime& then_latest,
                                   const MonoTime& deadline) {
  TRACE_EVENT0("clock", "HybridClock::WaitUntilAfter");
  HybridTime now;
  uint64_t error;
  {
    std::lock_guard<simple_spinlock> lock(lock_);
    NowWithError(&now, &error);
  }

  // "unshift" the hybrid_times so that we can measure actual time
  uint64_t now_usec = GetPhysicalValueMicros(now);
  uint64_t then_latest_usec = GetPhysicalValueMicros(then_latest);

  uint64_t now_earliest_usec = now_usec - error;

  // Case 1, event happened definitely in the past, return
  if (PREDICT_TRUE(then_latest_usec < now_earliest_usec)) {
    return Status::OK();
  }

  // Case 2 wait out until we are sure that then_latest has passed

  // We'll sleep then_latest_usec - now_earliest_usec so that the new
  // nw.earliest is higher than then.latest.
  uint64_t wait_for_usec = (then_latest_usec - now_earliest_usec);

  // Additionally adjust the sleep time with the max tolerance adjustment
  // to account for the worst case clock skew while we're sleeping.
  wait_for_usec *= tolerance_adjustment_;

  // Check that sleeping wouldn't sleep longer than our deadline.
  RETURN_NOT_OK(CheckDeadlineNotWithinMicros(deadline, wait_for_usec));

  SleepFor(MonoDelta::FromMicroseconds(wait_for_usec));


  VLOG(1) << "WaitUntilAfter(): Incoming time(latest): " << then_latest_usec
          << " Now(earliest): " << now_earliest_usec << " error: " << error
          << " Waiting for: " << wait_for_usec;

  return Status::OK();
}

Status HybridClock::WaitUntilAfterLocally(const HybridTime& then,
                                          const MonoTime& deadline) {
  while (true) {
    HybridTime now;
    uint64_t error;
    {
      std::lock_guard<simple_spinlock> lock(lock_);
      NowWithError(&now, &error);
    }
    if (now.CompareTo(then) > 0) {
      return Status::OK();
    }
    uint64_t wait_for_usec = GetPhysicalValueMicros(then) - GetPhysicalValueMicros(now);

    // Check that sleeping wouldn't sleep longer than our deadline.
    RETURN_NOT_OK(CheckDeadlineNotWithinMicros(deadline, wait_for_usec));
  }
}

bool HybridClock::IsAfter(HybridTime t) {
  // Manually get the time, rather than using Now(), so we don't end up causing
  // a time update.
  uint64_t now_usec;
  uint64_t error_usec;
  CHECK_OK(WalltimeWithError(&now_usec, &error_usec));

  std::lock_guard<simple_spinlock> lock(lock_);
  now_usec = std::max(now_usec, last_usec_);

  HybridTime now;
  if (now_usec > last_usec_) {
    now = HybridTimeFromMicroseconds(now_usec);
  } else {
    // last_usec_ may be in the future if we were updated from a remote
    // node.
    now = HybridTimeFromMicrosecondsAndLogicalValue(last_usec_, next_logical_);
  }

  return t.value() < now.value();
}

yb::Status HybridClock::CheckClockSyncError(uint64_t error_usec) {
  if (!FLAGS_disable_clock_sync_error && error_usec > FLAGS_max_clock_sync_error_usec) {
    return STATUS(ServiceUnavailable, Substitute("Error: Clock error was too high ($0 us), max "
                                                 "allowed ($1 us).", error_usec,
                                                 FLAGS_max_clock_sync_error_usec));
  }
  return Status::OK();
}

yb::Status HybridClock::WalltimeWithError(uint64_t* now_usec, uint64_t* error_usec) {
  if (PREDICT_FALSE(FLAGS_use_mock_wall_clock)) {
    VLOG(1) << "Current clock time: " << mock_clock_time_usec_ << " error: "
            << mock_clock_max_error_usec_ << ". Updating to time: " << now_usec
            << " and error: " << error_usec;
    *now_usec = mock_clock_time_usec_;
    *error_usec = mock_clock_max_error_usec_;
  } else {
#if defined(__APPLE__)
    *now_usec = GetCurrentTimeMicros();
    // We use a fixed small clock error for Mac OS X builds.
    *error_usec = 1000;
  }
#else
    // Read the time. This will return an error if the clock is not synchronized.
    ntptimeval timeval;
    RETURN_NOT_OK(GetClockTime(&timeval));
    *now_usec = timeval.time.tv_sec * kNanosPerSec + timeval.time.tv_usec / divisor_;
    *error_usec = timeval.maxerror;
  }

  // If the clock is synchronized but has max_error beyond max_clock_sync_error_usec
  // we also return a non-ok status.
  RETURN_NOT_OK(CheckClockSyncError(*error_usec));
#endif // defined(__APPLE__)
  return yb::Status::OK();
}

void HybridClock::SetMockClockWallTimeForTests(uint64_t now_usec) {
  CHECK(FLAGS_use_mock_wall_clock);
  std::lock_guard<simple_spinlock> lock(lock_);
  CHECK_GE(now_usec, mock_clock_time_usec_);
  mock_clock_time_usec_ = now_usec;
}

void HybridClock::SetMockMaxClockErrorForTests(uint64_t max_error_usec) {
  CHECK(FLAGS_use_mock_wall_clock);
  std::lock_guard<simple_spinlock> lock(lock_);
  mock_clock_max_error_usec_ = max_error_usec;
}

// Used to get the hybrid_time for metrics.
uint64_t HybridClock::NowForMetrics() {
  return Now().ToUint64();
}

// Used to get the current error, for metrics.
uint64_t HybridClock::ErrorForMetrics() {
  HybridTime now;
  uint64_t error;

  std::lock_guard<simple_spinlock> lock(lock_);
  NowWithError(&now, &error);
  return error;
}

void HybridClock::RegisterMetrics(const scoped_refptr<MetricEntity>& metric_entity) {
  METRIC_hybrid_clock_hybrid_time.InstantiateFunctionGauge(
      metric_entity,
      Bind(&HybridClock::NowForMetrics, Unretained(this)))
    ->AutoDetachToLastValue(&metric_detacher_);
  METRIC_hybrid_clock_error.InstantiateFunctionGauge(
      metric_entity,
      Bind(&HybridClock::ErrorForMetrics, Unretained(this)))
    ->AutoDetachToLastValue(&metric_detacher_);
}

string HybridClock::Stringify(HybridTime hybrid_time) {
  return StringifyHybridTime(hybrid_time);
}

uint64_t HybridClock::GetLogicalValue(const HybridTime& hybrid_time) {
  return hybrid_time.GetLogicalValue();
}

uint64_t HybridClock::GetPhysicalValueMicros(const HybridTime& hybrid_time) {
  return hybrid_time.GetPhysicalValueMicros();
}

uint64_t HybridClock::GetPhysicalValueNanos(const HybridTime& hybrid_time) {
  // Conversion to nanoseconds here is safe from overflow since 2^kBitsToShift is less than
  // MonoTime::kNanosecondsPerMicrosecond. Although, we still just check for sanity.
  uint64_t micros = hybrid_time.value() >> kBitsToShift;
  CHECK(micros <= std::numeric_limits<uint64_t>::max() / MonoTime::kNanosecondsPerMicrosecond);
  return micros * MonoTime::kNanosecondsPerMicrosecond;
}

HybridTime HybridClock::HybridTimeFromMicroseconds(uint64_t micros) {
  return HybridTime(micros << kBitsToShift);
}

HybridTime HybridClock::HybridTimeFromMicrosecondsAndLogicalValue(
    MicrosTime micros, LogicalTimeComponent logical_value) {
  return HybridTime::FromMicrosecondsAndLogicalValue(micros, logical_value);
}

HybridTime HybridClock::AddPhysicalTimeToHybridTime(const HybridTime& original,
                                                    const MonoDelta& to_add) {
  uint64_t new_physical = GetPhysicalValueMicros(original) + to_add.ToMicroseconds();
  uint64_t old_logical = GetLogicalValue(original);
  return HybridTimeFromMicrosecondsAndLogicalValue(new_physical, old_logical);
}

int HybridClock::CompareHybridClocksToDelta(const HybridTime& begin,
                                            const HybridTime& end,
                                            const MonoDelta& delta) {
  if (end < begin) {
    return -1;
  }
  // We use nanoseconds since MonoDelta has nanosecond granularity.
  uint64_t begin_nanos = GetPhysicalValueNanos(begin);
  uint64_t end_nanos = GetPhysicalValueNanos(end);
  uint64_t delta_nanos = delta.ToNanoseconds();
  if (end_nanos - begin_nanos > delta_nanos) {
    return 1;
  } else if (end_nanos - begin_nanos == delta_nanos) {
    uint64_t begin_logical = GetLogicalValue(begin);
    uint64_t end_logical = GetLogicalValue(end);
    if (end_logical > begin_logical) {
      return 1;
    } else if (end_logical < begin_logical) {
      return -1;
    } else {
      return 0;
    }
  } else {
    return -1;
  }
}

string HybridClock::StringifyHybridTime(const HybridTime& hybrid_time) {
  return Substitute("P: $0 usec, L: $1",
                    GetPhysicalValueMicros(hybrid_time),
                    GetLogicalValue(hybrid_time));
}

}  // namespace server
}  // namespace yb
