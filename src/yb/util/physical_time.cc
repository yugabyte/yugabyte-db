// Copyright (c) YugaByte, Inc.
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

#include "yb/util/physical_time.h"

#if !defined(_APPLE__)
#include <sys/timex.h>
#endif

#include "yb/gutil/walltime.h"

#include "yb/util/errno.h"
#include "yb/util/flag_tags.h"
#include "yb/util/logging.h"

DEFINE_uint64(max_clock_sync_error_usec, 10 * 1000 * 1000,
              "Maximum allowed clock synchronization error as reported by NTP "
              "before the server will abort.");
DEFINE_bool(disable_clock_sync_error, true,
            "Whether or not we should keep running if we detect a clock synchronization issue.");
TAG_FLAG(disable_clock_sync_error, advanced);
TAG_FLAG(max_clock_sync_error_usec, advanced);
TAG_FLAG(max_clock_sync_error_usec, runtime);

namespace yb {

namespace {

Result<PhysicalTime> CheckClockSyncError(PhysicalTime time) {
  if (!FLAGS_disable_clock_sync_error && time.max_error > FLAGS_max_clock_sync_error_usec) {
    return STATUS_FORMAT(ServiceUnavailable, "Error: Clock error was too high ($0 us), max "
                                             "allowed ($1 us).", time.max_error,
                                             FLAGS_max_clock_sync_error_usec);
  }
  return time;
}

#if defined(__APPLE__)

class WallClockImpl : public PhysicalClock {
  Result<PhysicalTime> Now() override {
    // We use a fixed small clock error for Mac OS X builds.
    const MicrosTime kFixedError = 1000;
    return CheckClockSyncError({ static_cast<MicrosTime>(GetCurrentTimeMicros()), kFixedError });
  }
};

#else
CHECKED_STATUS CallAdjTime(timex* tx) {
  // Set mode to 0 to query the current time.
  tx->modes = 0;
  int rc = ntp_adjtime(tx);
  if (rc == TIME_OK) {
    return Status::OK();
  }

  switch (rc) {
    case -1: // generic error
      return STATUS(ServiceUnavailable,
                    "Error reading clock. ntp_adjtime() failed",
                    ErrnoToString(errno));
    case TIME_ERROR:
      if (FLAGS_disable_clock_sync_error) {
        YB_LOG_EVERY_N_SECS(ERROR, 15) << "Clock unsynchronized, status: " << tx->status;
        return Status::OK();
      }
      return STATUS_FORMAT(
          ServiceUnavailable, "Error reading clock. Clock considered unsynchronized, status: $0",
          tx->status);
    default:
      // TODO what to do about leap seconds? see KUDU-146
      YB_LOG_FIRST_N(ERROR, 1) << "Server undergoing leap second. This may cause consistency "
                               << "issues (rc=" << rc << ")";
      return Status::OK();
  }
}

class WallClockImpl : public PhysicalClock {
  Result<PhysicalTime> Now() override {
    const MicrosTime kMicrosPerSec = 1000000;

    timex tx;
    RETURN_NOT_OK(CallAdjTime(&tx));

    if (tx.status & STA_NANO) {
      tx.time.tv_usec /= 1000;
    }
    DCHECK_LT(tx.time.tv_usec, 1000000);

    return CheckClockSyncError(
        { tx.time.tv_sec * kMicrosPerSec + tx.time.tv_usec,
          static_cast<yb::MicrosTime>(tx.maxerror) });
  }
};
#endif

} // namespace

const PhysicalClockPtr& WallClock() {
  static PhysicalClockPtr instance = std::make_shared<WallClockImpl>();
  return instance;
}

Result<PhysicalTime> MockClock::Now() {
  return CheckClockSyncError(value_.load(std::memory_order_acquire));
}

void MockClock::Set(const PhysicalTime& value) {
  value_.store(value, std::memory_order_release);
}

PhysicalClockPtr MockClock::AsClock() {
  return PhysicalClockPtr(this, [](PhysicalClock*){});
}

PhysicalClockProvider MockClock::AsProvider() {
  return std::bind(&MockClock::AsClock, this);
}

} // namespace yb
