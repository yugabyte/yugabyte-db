//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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

#include <string.h>

#include "yb/gutil/sysinfo.h"

#include "yb/rocksdb/env.h"
#include "yb/rocksdb/statistics.h"

namespace rocksdb {
// Auto-scoped.
// Records the measure time into the corresponding histogram if statistics
// is not nullptr. It is also saved into *elapsed if the pointer is not nullptr.
template <TimeResolution kTimeResolution>
class StopWatch {
 public:
  StopWatch(Env* const env, uint64_t* elapsed)
      : env_(env),
        statistics_(nullptr),
        hist_type_(HISTOGRAM_ENUM_MAX),
        ticker_(TICKER_ENUM_MAX),
        elapsed_(elapsed),
        stats_enabled_(false),
        start_time_(elapsed_ != nullptr ? env->NowCpuCycles() : 0) {}

  StopWatch(
      Env* const env, Statistics* statistics, const Histograms hist_type,
      uint64_t* elapsed = nullptr)
      : env_(env),
        statistics_(statistics),
        hist_type_(hist_type),
        ticker_(TICKER_ENUM_MAX),
        elapsed_(elapsed),
        stats_enabled_(statistics && statistics->HistEnabledForType(hist_type)),
        start_time_((stats_enabled_ || elapsed_ != nullptr) ? env->NowCpuCycles() : 0) {}

  StopWatch(
      Env* const env, Statistics* statistics, const Tickers ticker, uint64_t* elapsed = nullptr)
      : env_(env),
        statistics_(statistics),
        hist_type_(HISTOGRAM_ENUM_MAX),
        ticker_(ticker),
        elapsed_(elapsed),
        stats_enabled_(statistics),
        start_time_((stats_enabled_ || elapsed_ != nullptr) ? env->NowCpuCycles() : 0) {
  }

  ~StopWatch() {
    if (!elapsed_ && !stats_enabled_) {
      return;
    }
    // Multiply first and then divide to minimize rounding error (important for some tests), use
    // double to avoid overflow.
    const auto elapsed = 1.0 * (env_->NowCpuCycles() - start_time_) *
                         UnitsInSecond(kTimeResolution) / base::CyclesPerSecond();
    if (elapsed_) {
      *elapsed_ = elapsed;
    }
    if (stats_enabled_) {
      if (hist_type_ < HISTOGRAM_ENUM_MAX) {
        statistics_->measureTime(hist_type_, elapsed);
      }
      if (ticker_ < TICKER_ENUM_MAX) {
        statistics_->recordTick(ticker_, elapsed);
      }
    }
  }

 private:
  Env* const env_;
  Statistics* statistics_;
  const Histograms hist_type_;
  const Tickers ticker_;
  uint64_t* elapsed_;
  bool stats_enabled_;
  uint64_t start_time_;
};

using StopWatchMicro = StopWatch<TimeResolution::kMicros>;
using StopWatchNano = StopWatch<TimeResolution::kNanos>;

} // namespace rocksdb
