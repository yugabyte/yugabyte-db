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

#pragma once

#include "yb/cdc/cdc_types.h"
#include "yb/cdc/xcluster_types.h"
#include "yb/common/common_types.pb.h"

#include "yb/tserver/xcluster_poller_id.h"

#include "yb/util/enums.h"

namespace yb::tserver {

YB_DEFINE_ENUM(
    XClusterReplicationErrorSendState,
    (kNotSent)  // Error has not been sent to master yet.
    (kSending)  // Error has been sent but master has not yet acknowledged it yet.
    (kSent));   // Error has been sent and master has acknowledged it.

using XClusterReplicationErrorsToSendMap =
    std::unordered_map<cdc::ReplicationGroupId, yb::xcluster::ReplicationGroupErrors>;

// Collection of errors from the various Pollers running on this tserver. This class helps keep
// track of which errors were sent to master.
class XClusterConsumerReplicationErrorCollector {
 public:
  XClusterConsumerReplicationErrorCollector();

  // Add a new Poller to the map. If the poller already exists, this will FATAL.
  void AddPoller(const XClusterPollerId& poller_id) EXCLUDES(mutex_);
  void RemovePoller(const XClusterPollerId& poller_id) EXCLUDES(mutex_);

  void StoreError(const XClusterPollerId& poller_id, ReplicationErrorPb error) EXCLUDES(mutex_);

  void TransitionErrorsFromSendingToSent() EXCLUDES(mutex_);

  XClusterReplicationErrorsToSendMap GetErrorsToSend(bool get_all_errors) EXCLUDES(mutex_);

 private:
  FRIEND_TEST(XClusterConsumerReplicationError, TestCollector);

  struct ReplicationError {
    ReplicationErrorPb error = ReplicationErrorPb::REPLICATION_ERROR_UNINITIALIZED;
    // No need to send UNINITIALIZED error to master.
    XClusterReplicationErrorSendState send_state = XClusterReplicationErrorSendState::kSent;
  };

  XClusterReplicationErrorSendState TEST_GetSendState() const EXCLUDES(mutex_);
  std::unordered_map<XClusterPollerId, ReplicationError> TEST_GetErrorMap() const EXCLUDES(mutex_);

  mutable std::mutex mutex_;

  // Stores the error status of each poller. This is accessed on master heartbeat where we need to
  // be fast. Storing it per poller would require atomics or locks per poller which can slow us
  // down, hence storing in a dedicated map.
  std::unordered_map<XClusterPollerId, ReplicationError> error_map_ GUARDED_BY(mutex_);

  // Used to prevent unnecessary scan of the map in performance critical code.
  XClusterReplicationErrorSendState errors_send_state_ GUARDED_BY(mutex_) =
      XClusterReplicationErrorSendState::kSent;
};

}  // namespace yb::tserver
