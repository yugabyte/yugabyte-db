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
//

#include "yb/ash/wait_state.h"

#include <arpa/inet.h>

#include "yb/util/debug-util.h"
#include "yb/util/tostring.h"
#include "yb/util/trace.h"

DEFINE_test_flag(bool, yb_enable_ash, false, "True to enable Active Session History");
DEFINE_test_flag(bool, export_wait_state_names, yb::IsDebug(), "Exports wait-state name as a "
                 "human understandable string.");


namespace yb::ash {

namespace {
  // The current wait_state_ for this thread.
  thread_local WaitStateInfoPtr threadlocal_wait_state_;
}  // namespace

void AshMetadata::set_client_host_port(const HostPort &host_port) {
  client_host_port = host_port;
}

std::string AshMetadata::ToString() const {
  return YB_STRUCT_TO_STRING(
      yql_endpoint_tserver_uuid, root_request_id, query_id, rpc_request_id, client_host_port);
}

std::string AshAuxInfo::ToString() const {
  return YB_STRUCT_TO_STRING(table_id, tablet_id, method);
}

void AshAuxInfo::UpdateFrom(const AshAuxInfo &other) {
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

WaitStateInfo::WaitStateInfo(AshMetadata &&meta)
    : metadata_(std::move(meta)) {}

void WaitStateInfo::set_code(WaitStateCode c) {
  TRACE(ash::ToString(c));
  code_ = c;
}

WaitStateCode WaitStateInfo::code() const {
  return code_;
}

std::atomic<WaitStateCode>& WaitStateInfo::mutable_code() {
  return code_;
}

std::string WaitStateInfo::ToString() const {
  std::lock_guard lock(mutex_);
  return YB_CLASS_TO_STRING(metadata, code, aux_info);
}

void WaitStateInfo::set_rpc_request_id(int64_t rpc_request_id) {
  std::lock_guard lock(mutex_);
  metadata_.rpc_request_id = rpc_request_id;
}

void WaitStateInfo::set_root_request_id(const Uuid &root_request_id) {
  std::lock_guard lock(mutex_);
  metadata_.root_request_id = root_request_id;
}

void WaitStateInfo::set_query_id(uint64_t query_id) {
  std::lock_guard lock(mutex_);
  metadata_.query_id = query_id;
}

uint64_t WaitStateInfo::query_id() {
  std::lock_guard lock(mutex_);
  return metadata_.query_id;
}

void WaitStateInfo::set_client_host_port(const HostPort &host_port) {
  std::lock_guard lock(mutex_);
  metadata_.set_client_host_port(host_port);
}

void WaitStateInfo::set_yql_endpoint_tserver_uuid(const Uuid &yql_endpoint_tserver_uuid) {
  std::lock_guard lock(mutex_);
  metadata_.yql_endpoint_tserver_uuid = yql_endpoint_tserver_uuid;
}

void WaitStateInfo::UpdateMetadata(const AshMetadata &meta) {
  std::lock_guard lock(mutex_);
  metadata_.UpdateFrom(meta);
}

void WaitStateInfo::UpdateAuxInfo(const AshAuxInfo &aux) {
  std::lock_guard lock(mutex_);
  aux_info_.UpdateFrom(aux);
}

void WaitStateInfo::SetCurrentWaitState(WaitStateInfoPtr wait_state) {
  threadlocal_wait_state_ = std::move(wait_state);
}

const WaitStateInfoPtr& WaitStateInfo::CurrentWaitState() {
  if (!threadlocal_wait_state_) {
    VLOG_WITH_FUNC(3) << " returning nullptr";
  }
  return threadlocal_wait_state_;
}

//
// ScopedAdoptWaitState
//
ScopedAdoptWaitState::ScopedAdoptWaitState(WaitStateInfoPtr wait_state)
    : prev_state_(WaitStateInfo::CurrentWaitState()) {
  WaitStateInfo::SetCurrentWaitState(std::move(wait_state));
}

ScopedAdoptWaitState::~ScopedAdoptWaitState() {
  WaitStateInfo::SetCurrentWaitState(std::move(prev_state_));
}

//
// ScopedWaitStatus
//
ScopedWaitStatus::ScopedWaitStatus(WaitStateCode code)
    : code_(code),
      prev_code_(
          WaitStateInfo::CurrentWaitState()
              ? WaitStateInfo::CurrentWaitState()->mutable_code().exchange(code_)
              : code_) {
}

ScopedWaitStatus::~ScopedWaitStatus() {
  const auto &wait_state = WaitStateInfo::CurrentWaitState();
  if (wait_state) {
    auto expected = code_;
    if (!wait_state->mutable_code().compare_exchange_strong(expected, prev_code_)) {
      VLOG(3) << __func__ << " not reverting to prev_code_: " << prev_code_ << " since "
              << " current_code: " << expected << " is not " << code_;
    }
  }
}

}  // namespace yb::ash
