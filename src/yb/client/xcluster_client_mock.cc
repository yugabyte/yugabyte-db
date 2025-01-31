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

#include "yb/client/xcluster_client_mock.h"
#include "yb/client/client.h"

namespace yb::client {

const xcluster::ReplicationGroupId kDummyReplicationGroupId =
    xcluster::ReplicationGroupId("dummy-replication-group-id");

MockXClusterRemoteClientHolder::MockXClusterRemoteClientHolder()
    : XClusterRemoteClientHolder(kDummyReplicationGroupId) {
  yb_client_ = std::make_unique<MockYBClient>();
  xcluster_client_ = std::make_unique<MockXClusterClient>(*yb_client_);
}

MockXClusterRemoteClientHolder::~MockXClusterRemoteClientHolder() {}

MockXClusterClient::MockXClusterClient(YBClient& yb_client) : XClusterClient(yb_client) {}

MockXClusterClient::~MockXClusterClient() {}

}  // namespace yb::client
