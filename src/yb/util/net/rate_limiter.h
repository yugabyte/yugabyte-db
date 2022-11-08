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

#pragma once

#include <functional>
#include <vector>

#include "yb/util/monotime.h"
#include "yb/util/status_fwd.h"

namespace yb {

class RateLimiter {
 public:
  // Inactive rate limiter. When calling SendOrReceiveData, the rate limiter will update its
  // statistics and call the passed send_rcv_func, but it will not try to control the rate.
  RateLimiter();

  // Active rate limiter. target_rate_updater_ will be set to max_transmission_rate_updater.
  explicit RateLimiter(const std::function<uint64_t()>& max_transmission_rate_updater);

  ~RateLimiter();

  // This method sets the function that will provide the desired target. For example, for remote
  // bootstrap sessions, the desired target for each session will depend on the total number of
  // concurrent remote bootstrap sessions: target_aggregate_rate / number_of_sessions.
  void SetTargetRateUpdater(std::function<uint64_t()>&& target_rate_updater) {
    target_rate_updater_ = std::move(target_rate_updater);
  }

  // This function will be in charge of sending/receiving the data by calling send_rcv_func. This
  // function might sleep before returning to keep the transmission rate as close as possible to the
  // desired target.
  Status SendOrReceiveData(std::function<Status()> send_rcv_func,
                           std::function<uint64_t()> reply_size_func);

  // Calculates the size for the next transmission so that the transmission rate remains as close
  // as possible to the target rate.
  uint64_t GetMaxSizeForNextTransmission();

  // Get the transmission rate. Calculated as the number of bytes transmitted divided by the
  // time elapsed since the object was initialized.
  uint64_t GetRate();

  bool IsInitialized() { return init_; }

  // If the user of RateLimiter doesn't want to use SendOrReceiveData to send or receive data, this
  // method updates the internal stats and sleeps if it determines that the current rate is higher
  // than the rate provided by target_rate_updater_.
  void UpdateDataSizeAndMaybeSleep(uint64_t data_size);

  void Init();

  // We can only have an active rate limiter if the user has provided a function to update the rate.
  bool active() { return target_rate_updater_ != nullptr; }

  void SetTargetRate(uint64_t target_rate);

  MonoDelta total_time_slept() { return total_time_slept_; }

#if defined(OS_MACOSX)
  // Only used in MacOS. Instead of using the elapsed time for the calculation, we use the time
  // we spent sleeping. Used only for testing.
  // additional_time is passed by the test to include this time in the rate calculation.
  uint64_t MacOSRate(MonoDelta additional_time = MonoDelta::FromMilliseconds(0)) {
    return total_time_slept_.ToMilliseconds() || additional_time.ToMilliseconds()  > 0 ?
        MonoTime::kMillisecondsPerSecond * total_bytes_ /
            (total_time_slept_ + additional_time).ToMilliseconds() :
        0;
  }
#endif

  uint64_t time_slot_ms() const {
    return time_slot_ms_;
  }

  uint64_t total_bytes() const {
    return total_bytes_;
  }

 private:
  void UpdateRate();
  void UpdateTimeSlotSizeAndMaybeSleep(uint64_t data_size, MonoDelta elapsed);
  uint64_t GetSizeForNextTimeSlot();

  bool init_ = false;

  // Time when this rate was initialized. Used to calculate the long-term rate.
  MonoTime start_time_;

  // Last time stats were updated.
  MonoTime end_time_;

  // Reset every time the rate changes
  MonoTime rate_start_time_;

  // Total amount of time this object has spent sleeping.
  MonoDelta total_time_slept_ = MonoDelta::FromMicroseconds(0);

  // Total number of bytes sent or received by the user of this RateLimiter object.
  uint64_t total_bytes_ = 0;

  // Time slot in milliseconds. This is just used internally and it's never exposed to the user.
  // The transmission data size is calculated as the number of bytes that should be transmitted in
  // time_slot_ms_ milliseconds in order to achieve the desired rate.
  uint64_t time_slot_ms_ = 100;

  // The minimum size that we will ever use for time_slot_ms_.
  uint64_t min_time_slot_ = 10;

  // The maximum size that we will ever use for time_slot_ms_.
  uint64_t max_time_slot_ = 500;

  // Maximum transmission rate in bytes/sec. Set by calling target_rate_updater_().
  uint64_t target_rate_ = 0;
  std::vector<uint64_t> transmissions_rates_;

  std::function<uint64_t()> target_rate_updater_;
};

} // namespace yb
