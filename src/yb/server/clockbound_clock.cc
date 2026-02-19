// Copyright (c) YugabyteDB, Inc.
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

extern "C" {
#include <clockbound.h>
}

#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "yb/server/clockbound_clock.h"

#include "yb/gutil/port.h"
#include "yb/gutil/sysinfo.h"
#include "yb/server/hybrid_clock.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/math_util.h"
#include "yb/util/monotime.h"
#include "yb/util/result.h"

static constexpr auto kAutoConfigNumClockboundCtxs = 0;

// There are multiple levels of time synchronization in increasing order
// of accuracy.
//
// 1. Random NTP servers for time synchronization:
//   If the cluster nodes use this method for time sync, do NOT use
//   "clockbound" as a time source (which internally uses AWS
//   clockbound agent). Instead, use WallClock, which conservatively
//   assumes that the clock skew between any two nodes in the database
//   cluster will not exceed `max_clock_skew_usec` (which is 500ms
//   by default). This assumption may or may not hold in practice.
//
// 2. AWS Time Sync Service:
//   AWS time sync service uses GPS and atomic clocks in each data
//   center to provide accurate timestamps. Starting from this level,
//   AWS clockbound provides an upper bound on the clock error. The
//   bound varies with time and is typically under 500us. When the
//   clockbound lib cannot provide an upper bound, it returns an
//   error (tserver crashes in this case).
//
// 3. AWS Time Sync Service with PHC:
//   PHC is a special hardware clock on the local node in addition to
//   AWS time sync service. The hardware clock is more accurate and
//   supports both NTP and PTP. With NTP, the clock error is typically
//   under 100us. The 100us estimate is not an upper bound.
//
// 4. Precision Time Protocol (PTP) for time synchronization.
//   PTP requires PHC (and other infrastructure provided by AWS).
//   This is strictly better than NTP and should be used whenever
//   possible. However, this requires installing new network drivers.
//   PTP provides a clock error typically under 40us.
//
// AWS Time Sync Clock Error Summary:
// 1. Old hardware: ~500us
// 2. New hardware, PHC, with NTP: ~100us
// 3. New hardware, PHC, with PTP: ~40us
DEFINE_NON_RUNTIME_uint64(clockbound_clock_error_estimate_usec, 2500,
    "An estimate of the clock error in microseconds."
    " When the estimate is too low and the reported clock error exceeds the estimate,"
    " the database timestamps fall behind real time."
    " When the estimate is too high, the database is more prone to false read restarts.");

// See the comment on CreateClockboundClock for cautious upgrade/rollback path.
DEFINE_RUNTIME_bool(clockbound_mixed_clock_mode, false,
    "When true, use max_clock_skew_usec as read time uncertainty interval (same as WallClock)."
    " When false, use low clock errors reported by AWS clockbound to compute the read time"
    " uncertainty interval.");

DEFINE_NON_RUNTIME_uint32(clockbound_num_ctxs, kAutoConfigNumClockboundCtxs,
    "Number of clockbound contexts to open."
    " When set to 0, the number of contexts is automatically determined."
    " This is a performance optimization to reduce contention on the clockbound context.");

DEFINE_RUNTIME_uint64(max_wait_for_clock_sync_at_startup_ms, 120000,
    "Timeout in milliseconds of waiting for clock synchronization at startup."
    " When set to 0, waiting is disabled.");

// Use this bound for global limit in mixed clock mode.
DECLARE_uint64(max_clock_skew_usec);

namespace yb::server {

namespace {

const std::string kClockboundClockName = "clockbound";

// ClockboundClock
//
// Uses clockbound_ctx and provides a thread-safe interface
// to the clockbound API.
//
// This class is made thread-safe by serializing access to clockbound_ctx
// objects.
//
// clockbound_ctx is not thread-safe.
class ClockboundClock : public PhysicalClock {
 public:
  ClockboundClock() : ctxs_(
      FLAGS_clockbound_num_ctxs == kAutoConfigNumClockboundCtxs
      ? base::MaxCPUIndex() + 1 : FLAGS_clockbound_num_ctxs) {
    auto num_ctxs = ctxs_.size();
    LOG_WITH_FUNC(INFO) << "Opening " << num_ctxs << " clockbound_ctx ...";
    for (size_t i = 0; i < num_ctxs; i++) {
      clockbound_err open_err;
      auto ctx = clockbound_open(CLOCKBOUND_SHM_DEFAULT_PATH, &open_err);
      // clockbound_open returns nullptr on failure.
      if (ctx == nullptr) {
        LOG_WITH_FUNC(FATAL)
            << "Opening clockbound_ctx failed with error "
            << ClockboundErrorToStatus(open_err);
      }
      ctxs_[i].ctx = ctx;
    }
  }

  // Returns PhysicalTime{real_time, clock error} on success.
  // Returns an error status on failure.
  Result<PhysicalTime> Now() override {
    clockbound_now_result result;
    // Thread Safety:
    // 1. clockbound_ctx is not thread safe.
    // 2. Return value of clockbound_now is not thread safe.
    auto num_ctxs = ctxs_.size();
#if defined(__APPLE__)
    int ctx_index = static_cast<int>(
        std::hash<std::thread::id>()(std::this_thread::get_id()) % num_ctxs);
#else
    int ctx_index = sched_getcpu();
    if (ctx_index < 0) {
      ctx_index = static_cast<int>(
          std::hash<std::thread::id>()(std::this_thread::get_id()) % num_ctxs);
    }
#endif
    for (size_t i = 0;; i++) {
      auto &padded_ctx = ctxs_[(ctx_index + i) % num_ctxs];
      std::unique_lock<std::mutex> lock(padded_ctx.mutex, std::defer_lock);
      if (i >= num_ctxs - 1) {
        // Last iteration, lock the mutex.
        lock.lock();
      } else {
        // Lock the mutex without contention.
        if (!lock.try_lock()) {
          continue;
        }
      }
      auto err = clockbound_now(padded_ctx.ctx, &result);
      if (err != nullptr) {
        RETURN_NOT_OK(ClockboundErrorToStatus(*err));
      }
      break;
    }

    // Always check whether the clock went out of sync.
    SCHECK_EQ(result.clock_status, CLOCKBOUND_STA_SYNCHRONIZED,
              ServiceUnavailable,
              result.clock_status == CLOCKBOUND_STA_FREE_RUNNING
                  ? "Clock status is free running, time cannot be trusted."
                  : "Clock status is unknown, time cannot be trusted.");

    auto earliest = MonoTime::TimespecToMicros(result.earliest);
    auto latest = MonoTime::TimespecToMicros(result.latest);

    auto error = ceil_div(latest - earliest, MicrosTime(2));
    // This check is not mandatory. However, when the clock error
    // is unreasonably high such as 250ms, it indicates a serious
    // infrastructure failure and we report that scenario.
    //
    // Use half of max_clock_skew_usec as the maximum allowed clock error.
    MicrosTime max_clock_error =
        ANNOTATE_UNPROTECTED_READ(FLAGS_max_clock_skew_usec) / 2;
    SCHECK_LE(error, max_clock_error, ServiceUnavailable,
              Format("Clock error: $0 exceeds maximum allowed clock error: $1."
                     " This indicates a node restart or an infrastructure failure."
                     " Ensure that the clocks are synchronized properly.",
                     error, max_clock_error));

    auto real_time = earliest + error;
    return PhysicalTime{ real_time, error };
  }

  ~ClockboundClock() {
    for (auto &padded_ctx : ctxs_) {
      auto close_err = clockbound_close(padded_ctx.ctx);
      LOG_IF(WARNING, close_err != nullptr)
          << "Failed to close clockbound_ctx with error: "
          << ClockboundErrorToStatus(*close_err);
    }
  }

 private:
  static Status ClockboundErrorToStatus(const clockbound_err &err) {
    switch (err.kind) {
      case CLOCKBOUND_ERR_NONE:
        return STATUS_FORMAT(IllegalState, "This state is not reachable");
      case CLOCKBOUND_ERR_SYSCALL:
        return STATUS_FORMAT(
          IOError, "clockbound API failed with error: $0, and detail: $1",
          strerror(err.sys_errno), err.detail);
      case CLOCKBOUND_ERR_SEGMENT_NOT_INITIALIZED:
        return STATUS_FORMAT(IOError, "Segment not initialized");
      case CLOCKBOUND_ERR_SEGMENT_MALFORMED:
        return STATUS_FORMAT(IOError, "Segment malformed");
      case CLOCKBOUND_ERR_CAUSALITY_BREACH:
        return STATUS_FORMAT(IOError, "Segment and clock reads out of order");
    }
    return STATUS_FORMAT(NotSupported, "Unknown error code: $0", err.kind);
  }

  MicrosTime MaxGlobalTime(PhysicalTime time) override {
    LOG_WITH_FUNC(FATAL)
        << "Internal Error: MaxGlobalTime must not be called"
        << " for ClockboundClock";
  }

  // Padded to avoid false sharing.
  struct alignas(CACHELINE_SIZE) PaddedClockboundCtx {
    std::mutex mutex;
    clockbound_ctx *ctx;
  };
  static_assert(sizeof(PaddedClockboundCtx) % CACHELINE_SIZE == 0);

  std::vector<PaddedClockboundCtx> ctxs_;
};

// RealTimeAlignedClock
// a. Leverages AWS clockbound as the underlying physical time source.
//   In steady state, combines the best properties of both NtpClock and
//   WallClock.
// b. NtpClock drastically reduces the length of the uncertainty interval.
//   This decreases the number of read restart requests. However,
//   NtpClock::Now() falls behind real time.
// c. WallClock always picks real time. However, it relies on a large
//   uncertainty interval. This leads to a large number of false read restart
//   errors.
//
// This clock has the following desirable properties,
// a. Less restart read requests compared to WallClock.
// b. Prevents stale reads (see below for proof outline of why this is true).
// c. Provides better availability than WallClock since only the
//   problematic nodes crash.
// d. Does NOT deviate from real time in steady state.
//
// Thread-safe.
//
// clockbound API
// ==============
//
// Clock accuracy information can be gathered reliably using the client
// library at github.com/aws/clock-bound. Remember that this is accurate only
// when using a Time Sync Service. The library provides three key pieces of
// information:
// 1. The status of the clock: cannot rely on clock information when
//    when the clocks are out of sync.
// 2. EARLIEST: the minimum possible value of the reference clock.
// 3. LATEST: the maximum possible value of the reference clock.
//
// Clock Logic
// ===========
//
// Let's call
// a. User specified estimate for clock error as EST_ERROR.
//   EST_ERROR = FLAGS_clockbound_clock_error_estimate_usec.
// b. clock error as clock_error.
//   clock_error = (latest - earliest)/2.
//
// NOW = earliest + MIN(EST_ERROR, clock_error)
// GLOBAL_LIMIT = latest + EST_ERROR
//
// Reducing Read Restart Requests
// ==============================
//
// The length of uncertainty interval is:
//   clock_error + MAX(EST_ERROR, clock_error).
//
// In non-PHC cases, EST_ERROR = 1000us, clock_error = 500us.
// The uncertainty interval is ~= 1000us + 500us = 1500us = 1.5ms.
// This is a significant improvement over the 500ms uncertainty interval
// in WallClock.
//
// Preventing Stale Reads
// ======================
//
// Setup:
//
//         LOCAL_EARLIEST               LOCAL_LATEST   LOCAL_GLOBAL_LIMIT
//  local node:  [----LOCAL_NOW-------------]              <|>
//
//                                 REMOTE_EARLIEST
//  remote node:                         [----REMOTE_NOW-------]
//
// a. Our node is the local node. Any other node in the cluster is
//   a remote node.
// b. The time uncertainty interval returned by AWS clockbound library
//   is represented by [---------]. Left bound is EARLIEST and the
//   right bound is LATEST.
//                                               EARLIEST   LATEST
// c. The time returned by this clock is called NOW. [---NOW---].
//
// Objective: Prove that NOW on any remote node is within the GLOBAL_LIMIT of
//   the local node.
//
// Proof Outline:
//
// Observation1: NOW is no more than EST_ERROR away from EARLIEST.
// This follows from the definition,
//   NOW = earliest + MIN(EST_ERROR, clock_error) <= earliest + EST_ERROR.
//
// Observation2: Local and remote uncertainty intervals overlap.
// The reference time must be in both local and remote uncertainty intervals
// because AWS clockbound guarantees that the reference time is within
// the uncertainty interval. So, the intervals at least overlap at
// reference time. This means that REMOTE_EARLIEST <= LOCAL_LATEST.
//
// From observation1 and observation2, it follows that the remote node's
// NOW is atmost EST_ERROR away from the local node's LATEST. This proves
// GLOBAL_LIMIT can be LOCAL_LATEST + EST_ERROR.
//
// Availability
// ============
//
// This clock has better availability than WallClock despite crashing
// in unsynchronized state. WallClock may also crash in unsynchronized
// state through use of HybridClock when it detects that the clock skew
// is too high. Moreover, it only crashes nodes that are behind in time.
// It has no mechanism to detect which node is out of sync. This is unlike
// clockbound backed clocks which only crash the problematic node. In
// case of WallClock, simply, one out of sync node with a high clock is
// enough to crash every other node in the cluster. This is not a problem
// with this clock.
//
// Reliability
// ===========
//
// Time synchronization is a hard problem. It is already impressive that
// the underlying infrastructure lets us compute an upper bound on the
// clock error. However, this is not without assumptions. The system
// must be configured correctly. AWS recommends that chrony be configured
// with maxclockerror set to 50. The clockbound must also be run with
// --max-drift-rate 50.
//
// ntp_gettime also returns a maxerror value. However, this is less
// reliable than clockbound since
// 1. clockbound accounts for max drift rate using --max-drift-rate flag.
// 2. clockbound also computes PHC Error Bound. This is important for PTP.
//
// Compatibility with Real Time
// ============================
//
// Terminology: Real Time = CLOCK_REALTIME
//
// Why don't we simply use NtpClock?
//
// In steady state, real time is a really good approximation of the
// reference time. Falling behind real time is not ideal.
// In particular, the external timestamps such as
// such as yb_read_time, yb_lock_status, and timestamps originating in
// postgres, all assume real time. Real time is a better
// approximation to these external timestamps than earliest.
//
// Example: the yb_locks_min_txn_age only displays locks of transactions
// older than the specified age. When Now() falls behind real time,
// some transactions are ommitted from the pg_locks output since they
// are considered too new, even when yb_locks_min_txn_age is zero.
class RealTimeAlignedClock : public PhysicalClock {
 public:
  explicit RealTimeAlignedClock(PhysicalClockPtr time_source)
      : time_source_(time_source) {
    // Wait for clock sync
    auto startup_timeout_ms =
        ANNOTATE_UNPROTECTED_READ(FLAGS_max_wait_for_clock_sync_at_startup_ms);
    if (startup_timeout_ms > 0) {
      auto sync_result = WaitForClockSync(startup_timeout_ms);
      if (sync_result.ok()) {
        LOG(INFO) << "Clock in synchronized state. " << sync_result.ToString();
      } else {
        LOG(FATAL) << "Failed to synchronize clock. Reason: " << sync_result.status().ToString();
      }
    }
  }

  // Returns earliest + min(clock_error, EST_ERROR).
  // Also returns clock_error for metrics.
  //
  // See class comment for the correctness argument.
  Result<PhysicalTime> Now() override {
    auto result = VERIFY_RESULT(time_source_->Now());

    // Log warning if clock error exceeds estimate
    if (result.max_error > FLAGS_clockbound_clock_error_estimate_usec) {
      YB_LOG_EVERY_N_SECS(WARNING, 1)
          << "Clock error: " << result.max_error
          << " exceeds estimate: " << FLAGS_clockbound_clock_error_estimate_usec;
    }

    auto [real_time, error] = std::move(result);
    auto earliest = real_time - error;
    auto timepoint = earliest + std::min(error, FLAGS_clockbound_clock_error_estimate_usec);
    return PhysicalTime{timepoint, error};
  }

  // Returns latest + EST_ERROR.
  //
  // See class comment for the correctness argument.
  MicrosTime MaxGlobalTime(PhysicalTime time) override {
    // time_point = earliest + min(clock_error, EST_ERROR).
    auto earliest = time.time_point - std::min(
        time.max_error, FLAGS_clockbound_clock_error_estimate_usec);
    auto real_time = earliest + time.max_error;
    // For safety, do not use the tighter bound unless
    // everyone else is also using the same physical clock.
    auto bound = ANNOTATE_UNPROTECTED_READ(FLAGS_clockbound_mixed_clock_mode)
        ? FLAGS_max_clock_skew_usec
        : time.max_error + FLAGS_clockbound_clock_error_estimate_usec;
    return real_time + bound;
  }

 private:
  // Waits for clock synchronization by retrying when:
  // 1. ServiceUnavailable errors occur (e.g., clock error exceeds
  //    max_clock_skew or status is not synchronized)
  // 2. Clock error exceeds the configured estimate
  // Returns error immediately for non-ServiceUnavailable errors.
  // Returns the return value of Now() when synchronized.
  Result<PhysicalTime> WaitForClockSync(uint64_t timeout_ms) {
    Result<PhysicalTime> last_result = STATUS(IllegalState, "Not initialized");

    auto condition = [this, &last_result]() -> Result<bool> {
      last_result = time_source_->Now();
      if (!last_result.ok()) {
        auto status = last_result.status();
        if (status.IsServiceUnavailable()) {
          // Retry on ServiceUnavailable
          LOG(INFO) << "Retrying. Reason: " << last_result.status().ToString();
          return false;
        }

        // Return other errors immediately
        LOG(INFO) << "Failed. Reason: " << last_result.status().ToString();
        return last_result.status();
      }

      auto clock_error = last_result->max_error;
      auto estimate = FLAGS_clockbound_clock_error_estimate_usec;

      // If clock error is greater than estimate, retry
      if (clock_error > estimate) {
        LOG(INFO) << "Retrying. Reason: Clock error: " << clock_error
                  << " is greater than estimate: " << estimate;
        return false;
      }

      // Success - clock is synchronized
      return true;
    };

    auto status = WaitFor(condition, MonoDelta::FromMilliseconds(timeout_ms),
                          "Wait for clockbound to sync", MonoDelta::FromSeconds(1));
    if (!status.ok()) {
      VLOG_WITH_FUNC(3) << "Failed. Reason: " << status.ToString();
      return status;
    }

    // Log and return the last result
    VLOG_WITH_FUNC(4) << "Returning: " << last_result.ToString();
    return last_result;
  }

  PhysicalClockPtr time_source_;
};

} // anonymous namespace

// Returns a physical clock backed by AWS clockbound.
//
// Requires that all the nodes in the cluster are synchronized using a
// Time Sync Service.
//
// Cloud Support
// =============
//
// Official support is available for AWS.
// Support for Azure/GCP will be planned post AWS adoption.
// However, no additional database changes are expected.
//
// Multi-cloud/Hybrid-cloud support is not planned, so use it at your own risk.
//
// Subject to the above conditions, prefer this clock over WallClock.
//
// Configuration
// =============
//
// Start cluster with gFlag: time_source = clockbound
// Ensure that clockbound_mixed_clock_mode is false.
//
// Mixed Clock Mode
// ================
//
// Sometimes, the nodes are not well synchronized. Poorly synchronized and
// unsynchronized clocks are handled transparently by this clock. However,
// this assumes that all the nodes in the cluster are using this clock. When
// at least one of the nodes is using WallClock, and that node is not well
// synchronized, this clock will not be able to provide
// the same guarantees.
//
// The workaround involves setting clockbound_mixed_clock_mode = true.
//
// Please remember to reset clockbound_mixed_clock_mode = false after all
// the nodes in the cluster are using this clock.
//
// Cautious Upgrade
// ================
//
// 1. Rolling restart with
//   a. clockbound_mixed_clock_mode = true (runtime flag)
//   b. time_source = clockbound
// 2. Set clockbound_mixed_clock_mode = false.
//
// Cautious Rollback
// =================
//
// 1. Set clockbound_mixed_clock_mode = true.
// 2. Rolling restart with time_source is empty.
//
// Notice that the cautious upgrade/rollback path is not standard.
PhysicalClockPtr CreateClockboundClock(PhysicalClockPtr time_source) {
  // Fake time sources are useful for testing purposes where
  // a clockbound agent is not available.
  return std::make_shared<RealTimeAlignedClock>(
      time_source ? time_source : std::make_shared<ClockboundClock>());
}

void RegisterClockboundClockProvider() {
  HybridClock::RegisterProvider(
      kClockboundClockName,
      [](const std::string &options) {
        return CreateClockboundClock();
      });
}

} // namespace yb::server
