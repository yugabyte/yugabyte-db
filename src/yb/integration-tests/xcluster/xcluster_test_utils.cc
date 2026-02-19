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

#include "yb/integration-tests/xcluster/xcluster_test_utils.h"

#include "yb/client/client.h"
#include "yb/client/xcluster_client.h"

#include "yb/master/master_ddl.pb.h"
#include "yb/util/backoff_waiter.h"
#include "yb/util/is_operation_done_result.h"
#include "yb/util/status.h"

namespace yb::XClusterTestUtils {

Result<NamespaceId> GetNamespaceId(client::YBClient& client, const NamespaceName& ns_name) {
  master::GetNamespaceInfoResponsePB resp;
  RETURN_NOT_OK(client.GetNamespaceInfo(ns_name, YQL_DATABASE_PGSQL, &resp));
  return resp.namespace_().id();
}

Result<bool> CheckpointReplicationGroup(
    client::YBClient& producer_client, const xcluster::ReplicationGroupId& replication_group_id,
    const NamespaceName& namespace_name, MonoDelta timeout, bool automatic_ddl_mode) {
  auto namespace_id =
      VERIFY_RESULT(XClusterTestUtils::GetNamespaceId(producer_client, namespace_name));
  auto xcluster_client = client::XClusterClient(producer_client);
  RETURN_NOT_OK(xcluster_client.CreateOutboundReplicationGroup(
      replication_group_id, {namespace_id}, automatic_ddl_mode));

  std::promise<Result<bool>> promise;
  auto future = promise.get_future();
  RETURN_NOT_OK(xcluster_client.IsBootstrapRequired(
      CoarseMonoClock::now() + timeout, replication_group_id, namespace_id,
      [&promise](Result<bool> res) { promise.set_value(res); }));
  return future.get();
}

Status CreateReplicationFromCheckpoint(
    client::YBClient& producer_client, const xcluster::ReplicationGroupId& replication_group_id,
    const std::string& target_master_addr, MonoDelta timeout) {
  auto xcluster_client = client::XClusterClient(producer_client);
  RETURN_NOT_OK(xcluster_client.CreateXClusterReplicationFromCheckpoint(
      replication_group_id, target_master_addr));

  RETURN_NOT_OK(LoggedWaitFor(
      [&]() -> Result<bool> {
        auto result = VERIFY_RESULT(xcluster_client.IsCreateXClusterReplicationDone(
            replication_group_id, target_master_addr));
        if (!result.status().ok()) {
          return result.status();
        }
        return result.done();
      },
      timeout, __func__));

  return Status::OK();
}

}  // namespace yb::XClusterTestUtils
