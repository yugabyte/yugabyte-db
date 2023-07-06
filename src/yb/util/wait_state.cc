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

#include "yb/util/wait_state.h"

#include "yb/util/tostring.h"

using yb::util::WaitStateCode;

namespace yb::util {

// __thread WaitStateInfoPtr WaitStateInfo::threadlocal_wait_state_;
thread_local WaitStateInfoPtr WaitStateInfo::threadlocal_wait_state_;

WaitStateInfo::WaitStateInfo(AUHMetadata meta)
  : metadata_(meta)
#ifdef TRACK_WAIT_HISTORY
  , num_updates_(0) 
#endif 
  {}

void WaitStateInfo::set_state(WaitStateCode c) {
  VLOG(3) << this << " " << ToString() << " setting state to " << util::ToString(c);
  code_ = c;
  #ifdef TRACK_WAIT_HISTORY
  {
    std::lock_guard<simple_spinlock> l(mutex_);
    history_.emplace_back(code_);
    // history_.push_back(code_);
  }
  num_updates_++;
  #endif
}

WaitStateCode WaitStateInfo::get_state() const {
  return code_;
}

void WaitStateInfo::set_metadata(AUHMetadata meta) {
  metadata_ = meta;
}

AUHMetadata WaitStateInfo::metadata() {
  return metadata_;
}

WaitStateCode WaitStateInfo::code() {
  return code_;
}

std::string WaitStateInfo::ToString() const {
  #ifdef TRACK_WAIT_HISTORY
  std::vector<WaitStateCode> history;
  {
    std::lock_guard<simple_spinlock> l(mutex_);
    history = history_;
  }
  return yb::Format("meta : $0, status: $1, updates: $2, history : $3", metadata_.ToString(),
    util::ToString(code_), num_updates_, yb::ToString(history));
  #else
    return yb::Format("meta : $0, status: $1", metadata_.ToString(), util::ToString(code_));
  #endif // TRACK_WAIT_HISTORY
}

WaitStateInfoPtr WaitStateInfo::CurrentWaitState() {
  return threadlocal_wait_state_;
}

void WaitStateInfo::SetCurrentWaitState(WaitStateInfoPtr wait_state) {
  threadlocal_wait_state_ = wait_state;
}

ScopedWaitState::ScopedWaitState(WaitStateInfoPtr wait_state) {
  prev_state_ = WaitStateInfo::CurrentWaitState();
  WaitStateInfo::SetCurrentWaitState(wait_state);
}

ScopedWaitState::~ScopedWaitState() {
  WaitStateInfo::SetCurrentWaitState(prev_state_);
}

ScopedWaitStatus::ScopedWaitStatus(WaitStateInfoPtr wait_state, WaitStateCode state)
    : wait_state_(wait_state), state_(state) {
  if (wait_state_) {
    prev_state_ = wait_state_->get_state();
    wait_state_->set_state(state_);
  }
}

ScopedWaitStatus::ScopedWaitStatus(WaitStateCode state)
    : wait_state_(WaitStateInfo::CurrentWaitState()), state_(state) {
  if (wait_state_) {
    prev_state_ = wait_state_->get_state();
    wait_state_->set_state(state_);
  }
}

ScopedWaitStatus::~ScopedWaitStatus() {
  if (wait_state_ && wait_state_->get_state() == state_) {
    wait_state_->set_state(prev_state_);
  }
}
}