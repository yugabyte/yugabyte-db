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

#ifndef YB_SERVER_TEST_CLOCK_H
#define YB_SERVER_TEST_CLOCK_H

#include "yb/server/clock.h"

namespace yb {
namespace server {

class TestClock : public Clock {
 public:
  explicit TestClock(ClockPtr clock) : impl_(std::move(clock)) {}

  template <class Duration>
  HybridTime SetDelta(Duration duration) {
    return SetDelta(
        HybridTime(std::chrono::duration_cast<std::chrono::microseconds>(duration).count(), 0));
  }

  HybridTime SetDelta(HybridTime new_delta) {
    return delta_.exchange(new_delta);
  }

 private:
  CHECKED_STATUS Init() override { return impl_->Init(); }

  HybridTime Now() override { return AddDelta(impl_->Now()); }

  HybridTime NowLatest() override { return AddDelta(impl_->NowLatest()); }

  CHECKED_STATUS GetGlobalLatest(HybridTime* t) override {
    auto status = impl_->GetGlobalLatest(t);
    if (status.ok()) {
      *t = AddDelta(*t);
    }
    return status;
  }

  void Update(const HybridTime& to_update) override {
    impl_->Update(SubDelta(to_update));
  }

  CHECKED_STATUS WaitUntilAfter(const HybridTime& then,
                                const MonoTime& deadline) override {
    return impl_->WaitUntilAfter(SubDelta(then), deadline);
  }

  CHECKED_STATUS WaitUntilAfterLocally(const HybridTime& then,
                                       const MonoTime& deadline) override {
    return impl_->WaitUntilAfterLocally(SubDelta(then), deadline);
  }

  bool IsAfter(HybridTime t) override { return impl_->IsAfter(SubDelta(t)); }

  void RegisterMetrics(const scoped_refptr<MetricEntity>& metric_entity) override {
    impl_->RegisterMetrics(metric_entity);
  }

  std::string Stringify(HybridTime hybrid_time) override {
    return impl_->Stringify(hybrid_time);
  }

  HybridTime AddDelta(HybridTime v) const {
    return HybridTime(v.ToUint64() + delta_.load().ToUint64());
  }

  HybridTime SubDelta(HybridTime v) const {
    return HybridTime(v.ToUint64() - delta_.load().ToUint64());
  }

  ClockPtr impl_;
  std::atomic<HybridTime> delta_{HybridTime(0)};
};

class TestClockDeltaChanger {
 public:
  template <class Delta>
  TestClockDeltaChanger(Delta new_delta, TestClock* test_clock)
      : test_clock_(test_clock), clock_holder_(test_clock),
        old_delta_(test_clock->SetDelta(new_delta)) {
  }

  TestClockDeltaChanger(TestClockDeltaChanger&& rhs)
      : test_clock_(rhs.test_clock_), clock_holder_(std::move(rhs.clock_holder_)),
        old_delta_(rhs.old_delta_) {
    rhs.test_clock_ = nullptr;
  }

  TestClockDeltaChanger(const TestClockDeltaChanger&) = delete;
  void operator=(const TestClockDeltaChanger&) = delete;

  ~TestClockDeltaChanger() {
    if (test_clock_) {
      test_clock_->SetDelta(old_delta_);
      clock_holder_.reset();
    }
  }

 private:
  TestClock* test_clock_;
  ClockPtr clock_holder_;
  HybridTime old_delta_;
};

} // namespace server
} // namespace yb

#endif // YB_SERVER_TEST_CLOCK_H
