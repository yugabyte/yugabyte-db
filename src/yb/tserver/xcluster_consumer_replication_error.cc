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

#include "yb/tserver/xcluster_consumer_replication_error.h"

#include "yb/gutil/map-util.h"

namespace yb::tserver {

XClusterConsumerReplicationErrorCollector::XClusterConsumerReplicationErrorCollector() {}

void XClusterConsumerReplicationErrorCollector::AddPoller(const XClusterPollerId& poller_id) {
  std::lock_guard l(mutex_);
  InsertOrDie(&error_map_, poller_id, ReplicationError());
}

void XClusterConsumerReplicationErrorCollector::RemovePoller(const XClusterPollerId& poller_id) {
  std::lock_guard l(mutex_);
  error_map_.erase(poller_id);
}

void XClusterConsumerReplicationErrorCollector::StoreError(
    const XClusterPollerId& poller_id, ReplicationErrorPb error) {
  std::lock_guard l(mutex_);
  DCHECK(error_map_.contains(poller_id));
  if (!error_map_.contains(poller_id)) {
    LOG(WARNING) << "xCluster Poller '" << poller_id
                 << "' reporting error when it is not expected to";
    return;
  }
  auto& poller_entry = error_map_.at(poller_id);
  poller_entry.error = error;
  poller_entry.send_state = XClusterReplicationErrorSendState::kNotSent;

  errors_send_state_ = XClusterReplicationErrorSendState::kNotSent;
}

void XClusterConsumerReplicationErrorCollector::TransitionErrorsFromSendingToSent() {
  std::lock_guard l(mutex_);
  if (errors_send_state_ == XClusterReplicationErrorSendState::kSent) {
    return;
  }

  for (auto& [_, error_info] : error_map_) {
    if (error_info.send_state == XClusterReplicationErrorSendState::kSending) {
      error_info.send_state = XClusterReplicationErrorSendState::kSent;
    }
  }

  if (errors_send_state_ == XClusterReplicationErrorSendState::kSending) {
    errors_send_state_ = XClusterReplicationErrorSendState::kSent;
  }
  // If errors_send_state_ is kNotSent, then it means we have more errors to send.
}

XClusterReplicationErrorsToSendMap XClusterConsumerReplicationErrorCollector::GetErrorsToSend(
    bool get_all_errors) {
  XClusterReplicationErrorsToSendMap errors_to_send;
  std::lock_guard l(mutex_);

  if (!get_all_errors && errors_send_state_ == XClusterReplicationErrorSendState::kSent) {
    // Nothing new to send.
    return errors_to_send;
  }

  for (auto& [poller_id, error_info] : error_map_) {
    if (error_info.error == ReplicationErrorPb::REPLICATION_ERROR_UNINITIALIZED) {
      continue;
    }
    if (get_all_errors || error_info.send_state != XClusterReplicationErrorSendState::kSent) {
      errors_to_send[poller_id.replication_group_id][poller_id.consumer_table_id]
                    [poller_id.producer_tablet_id] = {poller_id.leader_term, error_info.error};
      error_info.send_state = XClusterReplicationErrorSendState::kSending;
    }
  }

  if (!errors_to_send.empty()) {
    errors_send_state_ = XClusterReplicationErrorSendState::kSending;
  }

  return errors_to_send;
}

XClusterReplicationErrorSendState XClusterConsumerReplicationErrorCollector::TEST_GetSendState()
    const {
  std::lock_guard l(mutex_);
  return errors_send_state_;
}

std::unordered_map<XClusterPollerId, XClusterConsumerReplicationErrorCollector::ReplicationError>
XClusterConsumerReplicationErrorCollector::TEST_GetErrorMap() const {
  std::lock_guard l(mutex_);
  return error_map_;
}
}  // namespace yb::tserver
