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

#include <gmock/gmock.h>

#include "yb/client/xcluster_client.h"
#include "yb/util/is_operation_done_result.h"

namespace yb::client {

class MockXClusterClient : public XClusterClient {
 public:
  explicit MockXClusterClient(YBClient& yb_client);

  virtual ~MockXClusterClient();

  MOCK_METHOD(
      Result<UniverseUuid>, SetupDbScopedUniverseReplication,
      (const xcluster::ReplicationGroupId&, const std::vector<HostPort>&,
       const std::vector<NamespaceName>&, const std::vector<NamespaceId>&,
       const std::vector<TableId>&, const std::vector<xrepl::StreamId>&, bool),
      (override));

  MOCK_METHOD(
      Result<IsOperationDoneResult>, IsSetupUniverseReplicationDone,
      (const xcluster::ReplicationGroupId&), (override));

  MOCK_METHOD(
      Status, AddNamespaceToDbScopedUniverseReplication,
      (const xcluster::ReplicationGroupId& replication_group_id,
       const UniverseUuid& target_universe_uuid, const NamespaceName& namespace_name,
       const NamespaceId& source_namespace_id, const std::vector<TableId>& source_table_ids,
       const std::vector<xrepl::StreamId>& bootstrap_ids),
      (override));
};

class MockXClusterRemoteClientHolder : public XClusterRemoteClientHolder {
 public:
  MockXClusterRemoteClientHolder();

  virtual ~MockXClusterRemoteClientHolder();

  MockXClusterClient& GetMockXClusterClient() {
    return *static_cast<MockXClusterClient*>(xcluster_client_.get());
  }

  MOCK_METHOD(void, Shutdown, (), (override));
};

}  // namespace yb::client
