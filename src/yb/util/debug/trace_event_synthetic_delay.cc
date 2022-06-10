// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
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

#include "yb/util/debug/trace_event_synthetic_delay.h"

#include "yb/gutil/singleton.h"

namespace {
const int kMaxSyntheticDelays = 32;
}  // namespace

namespace yb {
namespace debug {

TraceEventSyntheticDelayClock::TraceEventSyntheticDelayClock() {}
TraceEventSyntheticDelayClock::~TraceEventSyntheticDelayClock() {}

class TraceEventSyntheticDelayRegistry : public TraceEventSyntheticDelayClock {
 public:
  static TraceEventSyntheticDelayRegistry* GetInstance();

  TraceEventSyntheticDelay* GetOrCreateDelay(const char* name);
  void ResetAllDelays();

  // TraceEventSyntheticDelayClock implementation.
  MonoTime Now() override;

 private:
  TraceEventSyntheticDelayRegistry();

  friend class Singleton<TraceEventSyntheticDelayRegistry>;

  Mutex lock_;
  TraceEventSyntheticDelay delays_[kMaxSyntheticDelays];
  TraceEventSyntheticDelay dummy_delay_;
  base::subtle::Atomic32 delay_count_;

  DISALLOW_COPY_AND_ASSIGN(TraceEventSyntheticDelayRegistry);
};

TraceEventSyntheticDelay::TraceEventSyntheticDelay()
    : mode_(STATIC), begin_count_(0), trigger_count_(0), clock_(nullptr) {}

TraceEventSyntheticDelay::~TraceEventSyntheticDelay() {}

TraceEventSyntheticDelay* TraceEventSyntheticDelay::Lookup(
    const std::string& name) {
  return TraceEventSyntheticDelayRegistry::GetInstance()->GetOrCreateDelay(
      name.c_str());
}

void TraceEventSyntheticDelay::Initialize(
    const std::string& name,
    TraceEventSyntheticDelayClock* clock) {
  name_ = name;
  clock_ = clock;
}

void TraceEventSyntheticDelay::SetTargetDuration(const MonoDelta& target_duration) {
  MutexLock lock(lock_);
  target_duration_ = target_duration;
  trigger_count_ = 0;
  begin_count_ = 0;
}

void TraceEventSyntheticDelay::SetMode(Mode mode) {
  MutexLock lock(lock_);
  mode_ = mode;
}

void TraceEventSyntheticDelay::SetClock(TraceEventSyntheticDelayClock* clock) {
  MutexLock lock(lock_);
  clock_ = clock;
}

void TraceEventSyntheticDelay::Begin() {
  // Note that we check for a non-zero target duration without locking to keep
  // things quick for the common case when delays are disabled. Since the delay
  // calculation is done with a lock held, it will always be correct. The only
  // downside of this is that we may fail to apply some delays when the target
  // duration changes.
  ANNOTATE_BENIGN_RACE(&target_duration_, "Synthetic delay duration");
  if (!target_duration_.Initialized())
    return;

  MonoTime start_time = clock_->Now();
  {
    MutexLock lock(lock_);
    if (++begin_count_ != 1)
      return;
    end_time_ = CalculateEndTimeLocked(start_time);
  }
}

void TraceEventSyntheticDelay::BeginParallel(MonoTime* out_end_time) {
  // See note in Begin().
  ANNOTATE_BENIGN_RACE(&target_duration_, "Synthetic delay duration");
  if (!target_duration_.Initialized()) {
    *out_end_time = MonoTime();
    return;
  }

  MonoTime start_time = clock_->Now();
  {
    MutexLock lock(lock_);
    *out_end_time = CalculateEndTimeLocked(start_time);
  }
}

void TraceEventSyntheticDelay::End() {
  // See note in Begin().
  ANNOTATE_BENIGN_RACE(&target_duration_, "Synthetic delay duration");
  if (!target_duration_.Initialized())
    return;

  MonoTime end_time;
  {
    MutexLock lock(lock_);
    if (!begin_count_ || --begin_count_ != 0)
      return;
    end_time = end_time_;
  }
  if (end_time.Initialized())
    ApplyDelay(end_time);
}

void TraceEventSyntheticDelay::EndParallel(const MonoTime& end_time) {
  if (end_time.Initialized())
    ApplyDelay(end_time);
}

MonoTime TraceEventSyntheticDelay::CalculateEndTimeLocked(
    const MonoTime& start_time) {
  if (mode_ == ONE_SHOT && trigger_count_++)
    return MonoTime();
  else if (mode_ == ALTERNATING && trigger_count_++ % 2)
    return MonoTime();
  MonoTime end = start_time;
  end.AddDelta(target_duration_);
  return end;
}

void TraceEventSyntheticDelay::ApplyDelay(const MonoTime& end_time) {
  TRACE_EVENT0("synthetic_delay", name_.c_str());
  while (clock_->Now().ComesBefore(end_time)) {
    // Busy loop.
  }
}

TraceEventSyntheticDelayRegistry*
TraceEventSyntheticDelayRegistry::GetInstance() {
  return Singleton<TraceEventSyntheticDelayRegistry>::get();
}

TraceEventSyntheticDelayRegistry::TraceEventSyntheticDelayRegistry()
    : delay_count_(0) {}

TraceEventSyntheticDelay* TraceEventSyntheticDelayRegistry::GetOrCreateDelay(
    const char* name) {
  // Try to find an existing delay first without locking to make the common case
  // fast.
  int delay_count = base::subtle::Acquire_Load(&delay_count_);
  for (int i = 0; i < delay_count; ++i) {
    if (!strcmp(name, delays_[i].name_.c_str()))
      return &delays_[i];
  }

  MutexLock lock(lock_);
  delay_count = base::subtle::Acquire_Load(&delay_count_);
  for (int i = 0; i < delay_count; ++i) {
    if (!strcmp(name, delays_[i].name_.c_str()))
      return &delays_[i];
  }

  DCHECK(delay_count < kMaxSyntheticDelays)
      << "must increase kMaxSyntheticDelays";
  if (delay_count >= kMaxSyntheticDelays)
    return &dummy_delay_;

  delays_[delay_count].Initialize(std::string(name), this);
  base::subtle::Release_Store(&delay_count_, delay_count + 1);
  return &delays_[delay_count];
}

MonoTime TraceEventSyntheticDelayRegistry::Now() {
  return MonoTime::Now();
}

void TraceEventSyntheticDelayRegistry::ResetAllDelays() {
  MutexLock lock(lock_);
  int delay_count = base::subtle::Acquire_Load(&delay_count_);
  for (int i = 0; i < delay_count; ++i) {
    delays_[i].SetTargetDuration(MonoDelta());
    delays_[i].SetClock(this);
  }
}

void ResetTraceEventSyntheticDelays() {
  TraceEventSyntheticDelayRegistry::GetInstance()->ResetAllDelays();
}

}  // namespace debug
}  // namespace yb

namespace trace_event_internal {

ScopedSyntheticDelay::ScopedSyntheticDelay(const char* name,
                                           AtomicWord* impl_ptr)
    : delay_impl_(GetOrCreateDelay(name, impl_ptr)) {
  delay_impl_->BeginParallel(&end_time_);
}

ScopedSyntheticDelay::~ScopedSyntheticDelay() {
  delay_impl_->EndParallel(end_time_);
}

yb::debug::TraceEventSyntheticDelay* GetOrCreateDelay(
    const char* name,
    AtomicWord* impl_ptr) {
  yb::debug::TraceEventSyntheticDelay* delay_impl =
      reinterpret_cast<yb::debug::TraceEventSyntheticDelay*>(
          base::subtle::Acquire_Load(impl_ptr));
  if (!delay_impl) {
    delay_impl = yb::debug::TraceEventSyntheticDelayRegistry::GetInstance()
                     ->GetOrCreateDelay(name);
    base::subtle::Release_Store(
        impl_ptr, reinterpret_cast<AtomicWord>(delay_impl));
  }
  return delay_impl;
}

}  // namespace trace_event_internal
