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

#include "yb/server/hybrid_clock.h"

#include <algorithm>
#include <mutex>

#include "yb/gutil/bind.h"
#include "yb/gutil/walltime.h"

#include "yb/util/errno.h"
#include "yb/util/flags.h"
#include "yb/util/locks.h"
#include "yb/util/logging.h"
#include "yb/util/metrics.h"
#include "yb/util/net/net_util.h"
#include "yb/util/result.h"

DEFINE_UNKNOWN_bool(use_hybrid_clock, true,
            "Whether HybridClock should be used as the default clock"
            " implementation. This should be disabled for testing purposes only.");
TAG_FLAG(use_hybrid_clock, hidden);

METRIC_DEFINE_gauge_uint64(server, hybrid_clock_hybrid_time,
                           "Hybrid Clock HybridTime",
                           yb::MetricUnit::kMicroseconds,
                           "Hybrid clock hybrid_time.");
METRIC_DEFINE_gauge_uint64(server, hybrid_clock_error,
                           "Hybrid Clock Error",
                           yb::MetricUnit::kMicroseconds,
                           "Server clock maximum error.");
METRIC_DEFINE_gauge_int64(server, hybrid_clock_skew,
                           "Hybrid Clock Skew",
                           yb::MetricUnit::kMicroseconds,
                           "Server clock skew.");

DEFINE_NON_RUNTIME_string(time_source, "",
              "The clock source that HybridClock should use. "
              "Leave empty for WallClock, clockbound for ClockboundClock, "
              "and other values depend on added clock providers and "
              "specific for appropriate tests, that adds them.");

DEFINE_UNKNOWN_bool(fail_on_out_of_range_clock_skew, true,
            "In case transactional tables are present, crash the process if clock skew greater "
            "than the configured maximum.");

DEFINE_UNKNOWN_uint64(clock_skew_force_crash_bound_usec, 60000000,
              "If the clock skew larger than this amount (microseconds) is observed, we will force "
              "a crash regardless of the value of fail_on_out_of_range_clock_skew. This is useful "
              "for avoiding really large hybrid clock jumps. Set to 0 to disable the check. Note "
              "that this check is only preformed for clock skew greater than max_clock_skew_usec.");

DECLARE_uint64(max_clock_skew_usec);

using strings::Substitute;

namespace yb {
namespace server {

namespace {

std::mutex providers_mutex;
std::unordered_map<std::string, PhysicalClockProvider> providers;

std::atomic<bool> clock_skew_control_enabled{false};

// options should be in format clock_name[,extra_data] and extra_data would be passed to
// clock factory.
PhysicalClockPtr GetClock(const std::string& options) {
  if (options.empty()) {
    return WallClock();
  }

  auto pos = options.find(',');
  auto name = pos == std::string::npos ? options : options.substr(0, pos);
  auto arg = pos == std::string::npos ? std::string() : options.substr(pos + 1);
  std::lock_guard lock(providers_mutex);
  auto it = providers.find(name);
  if (it == providers.end()) {
    LOG(DFATAL) << "Unknown time source: " << name;
    return WallClock();
  }
  return it->second(arg);
}

} // namespace

void HybridClock::RegisterProvider(std::string name, PhysicalClockProvider provider) {
  std::lock_guard lock(providers_mutex);
  providers.emplace(std::move(name), std::move(provider));
}

HybridClock::HybridClock() : HybridClock(FLAGS_time_source) {}

HybridClock::HybridClock(PhysicalClockPtr clock) : clock_(std::move(clock)) {}

HybridClock::HybridClock(const std::string& time_source) : HybridClock(GetClock(time_source)) {}

Status HybridClock::Init() {
#if defined(__APPLE__)
  LOG(WARNING) << "HybridClock initialized in local mode (OS X only). "
               << "Not suitable for distributed clusters.";
#endif // defined(__APPLE__)

  state_ = kInitialized;

  return Status::OK();
}

HybridTimeRange HybridClock::NowRange() {
  HybridTime now;
  uint64_t error;

  NowWithError(&now, &error);
  auto max_global_now = HybridTime::FromMicros(
      clock_->MaxGlobalTime({now.GetPhysicalValueMicros(), error}));
  return std::make_pair(now, max_global_now);
}

void HybridClock::NowWithError(HybridTime *hybrid_time, uint64_t *max_error_usec) {
  DCHECK_EQ(state_, kInitialized) << "Clock not initialized. Must call Init() first.";

  HybridClockComponents current_components = components_.load(boost::memory_order_acquire);

  auto now = clock_->Now();
  if (PREDICT_FALSE(!now.ok())) {
    LOG(FATAL) << Substitute("Couldn't get the current time: Clock unsynchronized. "
        "Status: $0", now.status().ToString());
  }

  // If the current time surpasses the last update just return it
  HybridClockComponents new_components = { now->time_point, 1 };

  VLOG(4) << __func__ << ", new: " << new_components << ", current: " << current_components;

  if (now->time_point < current_components.last_usec) {
    auto delta_us = current_components.last_usec - now->time_point;
    // Propagated hybrid time cannot be higher than the global limit.
    auto max_global_time = clock_->MaxGlobalTime(*now);
    if (delta_us > FLAGS_max_clock_skew_usec ||
        current_components.last_usec > max_global_time) {
      auto delta = MonoDelta::FromMicroseconds(delta_us);
      auto max_allowed = MonoDelta::FromMicroseconds(
          std::min(FLAGS_max_clock_skew_usec, max_global_time - now->time_point));
      if ((ANNOTATE_UNPROTECTED_READ(FLAGS_fail_on_out_of_range_clock_skew) ||
           (ANNOTATE_UNPROTECTED_READ(FLAGS_clock_skew_force_crash_bound_usec) > 0 &&
            delta_us > ANNOTATE_UNPROTECTED_READ(FLAGS_clock_skew_force_crash_bound_usec))) &&
          clock_skew_control_enabled.load(std::memory_order_acquire)) {
        TryRunChronycTracking();
        TryRunChronycSourcestats();
        LOG(FATAL) << "Too big clock skew is detected: " << delta << ", while max allowed is: "
                   << max_allowed << "; clock_skew_force_crash_bound_usec="
                   << ANNOTATE_UNPROTECTED_READ(FLAGS_clock_skew_force_crash_bound_usec);
      } else {
        YB_LOG_EVERY_N_SECS(ERROR, 1)
            << "Too big clock skew is detected: " << delta << ", while max allowed is: "
            << max_allowed << "; clock_skew_force_crash_bound_usec="
            << ANNOTATE_UNPROTECTED_READ(FLAGS_clock_skew_force_crash_bound_usec);
      }
    }
  } else {
    // Loop over the check in case of concurrent updates making the CAS fail.
    while (now->time_point > current_components.last_usec) {
      if (components_.compare_exchange_weak(current_components, new_components)) {
        *hybrid_time = HybridTime::FromMicros(new_components.last_usec);
        *max_error_usec = now->max_error;
        if (PREDICT_FALSE(VLOG_IS_ON(2))) {
          VLOG(2) << "Current clock is higher than the last one. Resetting logical values."
              << " Time: " << *hybrid_time << ", Error: " << *max_error_usec;
        }
        return;
      }
    }
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

  do {
    new_components.last_usec = current_components.last_usec;
    new_components.logical = current_components.logical + 1;
    new_components.HandleLogicalComponentOverflow();
    // Loop over the check until the CAS succeeds, in case there are concurrent updates.
  } while (!components_.compare_exchange_weak(current_components, new_components));

  *max_error_usec = new_components.last_usec - (now->time_point - now->max_error);

  // We've already atomically incremented the logical, so subtract 1.
  *hybrid_time = HybridTime::FromMicrosecondsAndLogicalValue(
      new_components.last_usec,
      narrow_cast<LogicalTimeComponent>(new_components.logical)).Decremented();
  if (PREDICT_FALSE(VLOG_IS_ON(2))) {
    VLOG(2) << "Current clock is lower than the last one. Returning last read and incrementing"
        " logical values. Hybrid time: " << *hybrid_time << " Error: " << *max_error_usec;
  }
}

void HybridClock::Update(const HybridTime& to_update) {
  if (!to_update.is_valid()) {
    return;
  }

  HybridClockComponents current_components = components_.load(boost::memory_order_acquire);
  HybridClockComponents new_components = {
    to_update.GetPhysicalValueMicros(), to_update.GetLogicalValue() + 1
  };

  // VLOG(4) crashes in TSAN mode
  if (VLOG_IS_ON(4)) {
    LOG(INFO) << __func__ << ", new: " << new_components << ", current: " << current_components;
  }

  new_components.HandleLogicalComponentOverflow();

  // Keep trying to CAS until it works or until HT has advanced past this update.
  while (current_components < new_components &&
      !components_.compare_exchange_weak(current_components, new_components)) {}
}

// Used to get the hybrid_time for metrics.
uint64_t HybridClock::NowForMetrics() {
  return Now().ToUint64();
}

// Used to get the current error, for metrics.
uint64_t HybridClock::ErrorForMetrics() {
  HybridTime now;
  uint64_t error;

  NowWithError(&now, &error);
  return error;
}

int64_t HybridClock::SkewForMetrics() {
  HybridClockComponents current_components = components_.load(boost::memory_order_acquire);
  auto now = clock_->Now();
  if (PREDICT_FALSE(!now.ok())) {
    LOG(DFATAL) << Substitute("Couldn't get the current time: Clock unsynchronized. "
        "Status: $0", now.status().ToString());
    return 0;
  }
  // Making sure we don't return a negative value.
  int64_t potential_skew = current_components.last_usec - now->time_point;
  return std::max<int64_t>(0, potential_skew);
}

std::string HybridClockComponents::ToString() const {
  return Format("{ last_usec: $0 logical: $1 }", last_usec, logical);
}

std::ostream& operator<<(std::ostream& out, const HybridClockComponents& components) {
  return out << components.ToString();
}

void HybridClockComponents::HandleLogicalComponentOverflow() {
  if (logical > HybridTime::kLogicalBitMask) {
    static constexpr uint64_t kMaxOverflowValue = 1ULL << HybridTime::kBitsForLogicalComponent;
    if (logical > kMaxOverflowValue) {
      LOG(FATAL) << "Logical component is too high: last_usec=" << last_usec
                 << "logical=" << logical << ", max allowed is " << kMaxOverflowValue;
    }
    YB_LOG_EVERY_N_SECS(WARNING, 5) << "Logical component overflow: "
        << "last_usec=" << last_usec << ", logical=" << logical;

    last_usec += logical >> HybridTime::kBitsForLogicalComponent;
    logical &= HybridTime::kLogicalBitMask;
  }
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
  METRIC_hybrid_clock_skew.InstantiateFunctionGauge(
      metric_entity,
      Bind(&HybridClock::SkewForMetrics, Unretained(this)))
    ->AutoDetachToLastValue(&metric_detacher_);
}

void HybridClock::EnableClockSkewControl() {
  clock_skew_control_enabled.store(true, std::memory_order_release);
}

}  // namespace server
}  // namespace yb
