// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
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

#include "yb/tablet/operations/operation.h"

#include "yb/tablet/tablet_peer.h"

namespace yb {
namespace tablet {

using consensus::DriverType;

Operation::Operation(std::unique_ptr<OperationState> state,
                     DriverType type,
                     OperationType operation_type)
    : state_(std::move(state)),
      type_(type),
      operation_type_(operation_type) {
}

OperationState::OperationState(TabletPeer* tablet_peer)
    : tablet_peer_(tablet_peer),
      completion_clbk_(new OperationCompletionCallback()),
      hybrid_time_error_(0),
      external_consistency_mode_(CLIENT_PROPAGATED) {
}

Arena* OperationState::arena() {
  if (!arena_) {
    arena_.emplace(32 * 1024, 4 * 1024 * 1024);
  }
  return arena_.get_ptr();
}

OperationState::~OperationState() {
}

void OperationState::set_hybrid_time(const HybridTime& hybrid_time) {
  // make sure we set the hybrid_time only once
  std::lock_guard<simple_spinlock> l(mutex_);
  DCHECK_EQ(hybrid_time_, HybridTime::kInvalidHybridTime);
  hybrid_time_ = hybrid_time;
}

void OperationState::TrySetHybridTimeFromClock() {
  std::lock_guard<simple_spinlock> l(mutex_);
  if (!hybrid_time_.is_valid()) {
    hybrid_time_ = tablet_peer_->clock().Now();
  }
}

OperationCompletionCallback::OperationCompletionCallback()
    : code_(tserver::TabletServerErrorPB::UNKNOWN_ERROR) {
}

void OperationCompletionCallback::set_error(const Status& status,
                                            tserver::TabletServerErrorPB::Code code) {
  status_ = status;
  code_ = code;
}

void OperationCompletionCallback::set_error(const Status& status) {
  status_ = status;
}

bool OperationCompletionCallback::has_error() const {
  return !status_.ok();
}

const Status& OperationCompletionCallback::status() const {
  return status_;
}

const tserver::TabletServerErrorPB::Code OperationCompletionCallback::error_code() const {
  return code_;
}

void OperationCompletionCallback::OperationCompleted() {}

OperationCompletionCallback::~OperationCompletionCallback() {}

void OperationMetrics::Reset() {
  commit_wait_duration_usec = 0;
}


}  // namespace tablet
}  // namespace yb
