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

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include <functional>

#include "yb/cdc/xcluster_types.h"
#include "yb/client/client_fwd.h"
#include "yb/common/snapshot.h"
#include "yb/master/master_backup.pb.h"
#include "yb/cdc/xrepl_types.h"
#include "yb/common/entity_ids_types.h"
#include "yb/gutil/callback.h"
#include "yb/rpc/secure_stream.h" // IWYU pragma: keep
#include "yb/util/result.h"
#include "yb/util/status.h"

namespace yb {
namespace client {

class YBClient;
class YBTableName;

}  // namespace client

namespace rpc {

class Messenger;

}  // namespace rpc

namespace master {
class TabletLocationsPB;
class NamespaceIdentifierPB;

typedef std::unordered_map<TableId, xrepl::StreamId> TableBootstrapIdsMap;
typedef Callback<void(Result<TableBootstrapIdsMap>)> BootstrapProducerCallback;

class XClusterRpcTasks {
 public:
  static Result<std::shared_ptr<XClusterRpcTasks>> CreateWithMasterAddrs(
      const xcluster::ReplicationGroupId& producer_id, const std::string& master_addrs);

  ~XClusterRpcTasks();

  Result<google::protobuf::RepeatedPtrField<TabletLocationsPB>> GetTableLocations(
      const std::string& table_id);

  // Calls BootstrapProducer and waits for the callback to complete.
  // TODO: Make this async.
  Status BootstrapProducer(
      const NamespaceIdentifierPB& producer_namespace,
      const std::vector<client::YBTableName>& tables, BootstrapProducerCallback callback);
  Result<SnapshotInfoPB> CreateSnapshot(
      const std::vector<client::YBTableName>& tables, TxnSnapshotId* snapshot_id);
  client::YBClient* client() const { return yb_client_.get(); }
  Result<client::YBClient*> UpdateMasters(const std::string& master_addrs);

 private:
  Result<TableBootstrapIdsMap> HandleBootstrapProducerResponse(
      client::BootstrapProducerResult bootstrap_result);
  Result<SnapshotInfoPB> CreateSnapshotCallback(const TxnSnapshotId& snapshot_id);

  std::unique_ptr<rpc::SecureContext> secure_context_;
  std::unique_ptr<rpc::Messenger> messenger_;
  std::unique_ptr<client::YBClient> yb_client_;
};

}  // namespace master
}  // namespace yb
