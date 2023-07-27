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

std::string AUHAuxInfo::ToString() const {
  return YB_STRUCT_TO_STRING(table_id, tablet_id, method);
}

void AUHAuxInfo::UpdateFrom(const AUHAuxInfo &other) {
  if (!other.tablet_id.empty()) {
    tablet_id = other.tablet_id;
  }
  if (!other.table_id.empty()) {
    table_id = other.table_id;
  }
  if (!other.method.empty()) {
    method = other.method;
  }
}

WaitStateInfo::WaitStateInfo(AUHMetadata meta)
  : metadata_(meta)
#ifdef TRACK_WAIT_HISTORY
  , num_updates_(0) 
#endif 
  {}

simple_spinlock* WaitStateInfo::get_mutex() {
  return &mutex_;
};

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

std::string WaitStateInfo::ToString() const {
  std::lock_guard<simple_spinlock> l(mutex_);
#ifdef TRACK_WAIT_HISTORY
  return YB_CLASS_TO_STRING(metadata, code, aux_info, num_updates, history);
#else
  return YB_CLASS_TO_STRING(metadata, code, aux_info);
#endif // TRACK_WAIT_HISTORY
}

WaitStateInfoPtr WaitStateInfo::CurrentWaitState() {
  return threadlocal_wait_state_;
}

void WaitStateInfo::set_current_request_id(int64_t current_request_id) {
  std::lock_guard<simple_spinlock> l(mutex_);
  metadata_.current_request_id = current_request_id;
}

void WaitStateInfo::UpdateMetadata(const AUHMetadata& meta) {
  std::lock_guard<simple_spinlock> l(mutex_);
  metadata_.UpdateFrom(meta);
}

void WaitStateInfo::UpdateAuxInfo(const AUHAuxInfo& aux) {
  std::lock_guard<simple_spinlock> l(mutex_);
  aux_info_.UpdateFrom(aux);
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