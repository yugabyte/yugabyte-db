//
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

#include "yb/util/disk_space_checker.h"

#include <algorithm>

#include "yb/util/env.h"
#include "yb/util/flag_validators.h"
#include "yb/util/flags.h"
#include "yb/util/logging.h"
#include "yb/util/shared_lock.h"

using namespace std::literals;

#if defined ADDRESS_SANITIZER
// ASAN tests run on machines with limited disk space, so disable disk full checks.
constexpr bool kRejectWritesWhenDiskFullDefault = false;
#else
constexpr bool kRejectWritesWhenDiskFullDefault = true;
#endif

DEFINE_RUNTIME_bool(reject_writes_when_disk_full, kRejectWritesWhenDiskFullDefault,
    "Reject incoming writes to the tablet if we are running out of disk space.");

DEFINE_RUNTIME_uint32(max_disk_throughput_mbps, 300,
    "The maximum disk throughput the disk attached to this node can support in MBps.");

DEFINE_RUNTIME_uint32(reject_writes_min_disk_space_check_interval_sec, 60,
    "Interval in seconds to check for disk space availability. The check will switch to aggressive "
    "mode (every 10s) if the available disk space is less than --max_disk_throughput_mbps * "
    "--reject_writes_min_disk_space_check_interval_sec. NOTE: Use a value higher than 10. If a "
    "value less than 10 is used, then we always run in aggressive check mode, potentially causing "
    "performance degradations.");

DEFINE_RUNTIME_uint64(reject_writes_min_disk_space_mb, 0,
    "Reject writes if less than this much disk space is available on the WAL or the data "
    "directory and --reject_writes_when_disk_full is enabled. If set to 0, defaults to "
    "--max_disk_throughput_mbps * min(10, --reject_writes_min_disk_space_check_interval_sec).");

DEFINE_RUNTIME_uint32(reject_writes_min_disk_space_pct, 5,
    "Reject writes if the available disk space on the WAL or the data directory falls below this "
    "percentage of the total disk capacity and --reject_writes_when_disk_full is enabled. For "
    "example, a value 5 rejects writes when free space drops below 5% of the disk's total "
    "capacity. This lets the rejection threshold scale automatically with disk size. If both this "
    "flag and --reject_writes_min_disk_space_mb yield a threshold, the larger of the two is used. "
    "Ignored if zero.");

DEFINE_validator(max_disk_throughput_mbps, FLAG_GT_VALUE_VALIDATOR(0));
DEFINE_validator(reject_writes_min_disk_space_check_interval_sec, FLAG_GT_VALUE_VALIDATOR(0));

namespace yb {

DiskSpaceChecker::DiskSpaceChecker(Env* env, std::string path, bool always_check_disk)
    : env_(env), path_(std::move(path)), always_check_disk_(always_check_disk) {}

bool DiskSpaceChecker::HasSufficientDiskSpace() {
  if (!FLAGS_reject_writes_when_disk_full || path_.empty()) {
    return true;
  }

  const auto now = CoarseMonoClock::Now();
  const auto last_check_time = last_check_time_.load(std::memory_order_acquire);

  std::unique_lock l(mutex_, std::defer_lock);
  if (always_check_disk_) {
    l.lock();
  } else {
    auto cached_check_interval_sec = frequent_check_interval_sec_.load(std::memory_order_acquire);
    if (cached_check_interval_sec == 0) {
      cached_check_interval_sec = FLAGS_reject_writes_min_disk_space_check_interval_sec;
    }

    if (IsInitialized(last_check_time) &&
        (now - last_check_time < cached_check_interval_sec * 1s)) {
      return has_free_space_.load(std::memory_order_acquire);
    }

    if (!l.try_lock_for(std::chrono::milliseconds(0))) {
      // Someone else is already checking disk space. Just use the cached value.

      if (!IsInitialized(last_check_time)) {
        // Always wait for the initial value to be valid.
        SharedLock shared_l(mutex_);
      }

      return has_free_space_.load(std::memory_order_acquire);
    }
  }

  bool has_space = true;
  const uint32 kAggressiveCheckIntervalSec = 10;
  // Lets assume we need to check frequently. If we have enough space, we will increment to a
  // higher value.
  auto check_interval_sec =
      std::min(kAggressiveCheckIntervalSec, FLAGS_reject_writes_min_disk_space_check_interval_sec);

  uint64 min_allowed_disk_space_mb =
      FLAGS_reject_writes_min_disk_space_mb ? FLAGS_reject_writes_min_disk_space_mb
                                            : FLAGS_max_disk_throughput_mbps * check_interval_sec;

  // If a percentage-based threshold is configured (non-zero), derive a minimum disk space from the
  // total disk capacity and use whichever threshold (MB-based or percentage-based) is larger. This
  // lets the rejection threshold scale automatically with disk size.
  if (FLAGS_reject_writes_min_disk_space_pct > 0) {
    auto stats_result = env_->GetFilesystemStatsBytes(path_);
    if (stats_result.ok()) {
      const uint64 pct_based_min_disk_space_mb = static_cast<uint64>(
          stats_result->total_space * FLAGS_reject_writes_min_disk_space_pct / 100.0) / 1024 / 1024;
      min_allowed_disk_space_mb = std::max(min_allowed_disk_space_mb, pct_based_min_disk_space_mb);
    } else {
      YB_LOG_EVERY_N_SECS(WARNING, 300)
          << "Unable to get filesystem stats to compute percentage-based disk space threshold: "
          << stats_result.status();
    }
  }

  const uint64 min_space_to_trigger_aggressive_check_mb =
      FLAGS_max_disk_throughput_mbps * FLAGS_reject_writes_min_disk_space_check_interval_sec;

  auto free_space_result = env_->GetFreeSpaceBytes(path_);
  if (!free_space_result.ok()) {
    YB_LOG_EVERY_N_SECS(WARNING, 300) << "Unable to get free space: " << free_space_result;

    // Fallback to the last known value.
    return has_free_space_.load(std::memory_order_acquire);
  }
  const auto free_space_mb = *free_space_result / 1024 / 1024;

  if (free_space_mb < min_allowed_disk_space_mb) {
    YB_LOG_EVERY_N_SECS(WARNING, 600) << "Not enough disk space available on " << path_
                                      << ". Free space: " << *free_space_result << " bytes";
    has_space = false;
  } else if (free_space_mb < min_space_to_trigger_aggressive_check_mb) {
    YB_LOG_EVERY_N_SECS(WARNING, 600)
        << "Low disk space on " << path_ << ". Free space: " << *free_space_result << " bytes";
  } else {
    // We have enough space so no need to check frequently.
    check_interval_sec = FLAGS_reject_writes_min_disk_space_check_interval_sec;
  }

  frequent_check_interval_sec_.store(check_interval_sec, std::memory_order_release);
  has_free_space_.store(has_space, std::memory_order_release);
  last_check_time_.store(now, std::memory_order_release);

  return has_space;
}

}  // namespace yb
