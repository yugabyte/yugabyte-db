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
#include "yb/util/net/rate_limiter.h"

#include "yb/util/size_literals.h"
#include "yb/util/status.h"
#include "yb/util/flags.h"

using namespace yb::size_literals;

DEFINE_UNKNOWN_int32(rate_limiter_min_size, 32_KB, "Minimum size for each transmission request");
DEFINE_UNKNOWN_uint64(rate_limiter_min_rate, 1000, "Minimum transmission rate in bytes/sec");

namespace yb {

RateLimiter::RateLimiter() {}

RateLimiter::RateLimiter(const std::function<uint64_t()>& max_transmission_rate_updater)
    : target_rate_updater_(max_transmission_rate_updater) {}

RateLimiter::~RateLimiter() {
  VLOG(1) << "Total rate limiter rate: " << GetRate();
}

uint64_t RateLimiter::GetMaxSizeForNextTransmission() {
  if (!active()) {
    return 0;
  }
  UpdateRate();
  VLOG(2) << "Max size for next time slot: "
             << target_rate_ * time_slot_ms_ / MonoTime::kMillisecondsPerSecond;
  VLOG(2) << "max_transmission_rate: " << target_rate_;
  VLOG(2) << "time_slot_ms_: " << time_slot_ms_;
  return GetSizeForNextTimeSlot();
}

uint64_t RateLimiter::GetRate() {
  if (!init_) {
    return 0;
  }

  auto elapsed = end_time_.GetDeltaSince(start_time_);
  if (elapsed.ToMicroseconds() == 0) {
    return 0;
  }

  VLOG(2) << "Elapsed: " << elapsed;
  VLOG(2) << "Total bytes: " << total_bytes_;
  return MonoTime::kMicrosecondsPerSecond * total_bytes_ / elapsed.ToMicroseconds();
}

void RateLimiter::UpdateDataSizeAndMaybeSleep(uint64_t data_size) {
  auto now = MonoTime::Now();
  auto elapsed = now.GetDeltaSince(end_time_);
  end_time_ = now;
  total_bytes_ += data_size;
  UpdateRate();
  UpdateTimeSlotSizeAndMaybeSleep(data_size, elapsed);
}

void RateLimiter::UpdateTimeSlotSizeAndMaybeSleep(uint64_t data_size, MonoDelta elapsed) {
  if (!active()) {
    return;
  }

  // If the rate is greater than target_rate_, sleep until both rates are equal.
  if (MonoTime::kMillisecondsPerSecond * data_size > target_rate_ * elapsed.ToMilliseconds()) {
    auto sleep_time =
        MonoTime::kMillisecondsPerSecond * data_size / target_rate_ - elapsed.ToMilliseconds();
    VLOG(1) << " target_rate_=" << target_rate_
            << " elapsed=" << elapsed.ToMilliseconds()
            << " received size=" << data_size
            << " and sleeping for=" << sleep_time;
    SleepFor(MonoDelta::FromMilliseconds(sleep_time));
    total_time_slept_ += MonoDelta::FromMilliseconds(sleep_time);
    end_time_ = MonoTime::Now();
    // If we slept for more than 80% of time_slot_ms_, reduce the size of this time slot.
    if (sleep_time > time_slot_ms_ * 80 / 100) {
      time_slot_ms_ = std::max(min_time_slot_, time_slot_ms_ / 2);
    }
  } else {
    time_slot_ms_ = std::min(max_time_slot_, time_slot_ms_ * 2);
  }
}

void RateLimiter::UpdateRate() {
  if (!active()) {
    VLOG(1) << "RateLimiter inactive";
    return;
  }
  auto target_rate = target_rate_updater_();
  VLOG(1) << "New target_rate: " << target_rate;
  if (target_rate != target_rate_) {
    rate_start_time_ = MonoTime::Now();
  }
  target_rate_ = target_rate;

  if (target_rate_ < FLAGS_rate_limiter_min_rate) {
    VLOG(1) << "Received transmission rate is less than minimum " << FLAGS_rate_limiter_min_rate;
    target_rate_ = FLAGS_rate_limiter_min_rate;
  }
}

inline uint64_t RateLimiter::GetSizeForNextTimeSlot() {
  VLOG(1) << "target_rate_ " << target_rate_;
  // We don't want to transmit less than min_size_ bytes.
  min_time_slot_ = (MonoTime::kMillisecondsPerSecond * FLAGS_rate_limiter_min_size + target_rate_) /
                   target_rate_;
  VLOG(1) << "min_size=" << FLAGS_rate_limiter_min_size
          << " max_transmission_rate=" << target_rate_
          << " min_time_slot=" << min_time_slot_;
  VLOG(1) << "Max allowed bytes per time slot: "
          << target_rate_ * max_time_slot_ / MonoTime::kMillisecondsPerSecond;
  auto time_slot_size = target_rate_ * time_slot_ms_ / MonoTime::kMillisecondsPerSecond;
  VLOG(1) << "time_slot_size: " << time_slot_size << " in " << time_slot_ms_ << " ms.";
  return time_slot_size;
}

void RateLimiter::Init() {
  start_time_ = MonoTime::Now();
  rate_start_time_ = start_time_;
  end_time_ = start_time_;
  init_ = true;
}

void RateLimiter::SetTargetRate(uint64_t target_rate) {
  target_rate_ = target_rate;
}

Status RateLimiter::SendOrReceiveData(std::function<Status()> send_rcv_func,
                                      std::function<uint64()> reply_size_func) {
  auto start = MonoTime::Now();
  if (!init_) {
    Init();
  }

  UpdateRate();
  auto status = send_rcv_func();
  auto now = MonoTime::Now();
  auto elapsed = now.GetDeltaSince(start);
  if (status.ok()) {
    auto data_size = reply_size_func();
    total_bytes_ += data_size;
    end_time_ = MonoTime::Now();
    UpdateTimeSlotSizeAndMaybeSleep(data_size, elapsed);
  }
  return status;
}
} // namespace yb
