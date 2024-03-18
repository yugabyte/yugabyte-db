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

#pragma once

#include <memory>

#include "yb/cdc/cdc_consumer.pb.h"
#include "yb/client/client_fwd.h"
#include "yb/common/common_fwd.h"
#include "yb/common/entity_ids_types.h"
#include "yb/util/result.h"

namespace yb {

struct XClusterPollerStats;

namespace cdc {
class ConsumerRegistryPB;
}  // namespace cdc

namespace pgwrapper {
class PGConn;
}  // namespace pgwrapper

namespace master {
class TSHeartbeatRequestPB;
}  // namespace master

namespace rpc {
class ProxyCache;
}  // namespace rpc

namespace tserver {

class TabletServer;
class XClusterPoller;

class XClusterConsumerIf {
 public:
  virtual ~XClusterConsumerIf() = default;

  virtual void Shutdown() = 0;

  virtual void HandleMasterHeartbeatResponse(
      const cdc::ConsumerRegistryPB* consumer_registry, int32_t cluster_config_version) = 0;
  virtual SchemaVersion GetMinXClusterSchemaVersion(
      const TableId& table_id, const ColocationId& colocation_id) = 0;
  virtual int32_t cluster_config_version() const = 0;
  virtual Status ReloadCertificates() = 0;
  virtual void PopulateMasterHeartbeatRequest(
      master::TSHeartbeatRequestPB* req, bool needs_full_tablet_report) = 0;
  virtual std::vector<XClusterPollerStats> GetPollerStats() const = 0;

  virtual cdc::XClusterRole TEST_GetXClusterRole() const = 0;
  virtual std::vector<TabletId> TEST_producer_tablets_running() const = 0;
  virtual uint32_t TEST_GetNumSuccessfulWriteRpcs() = 0;
  virtual std::vector<std::shared_ptr<XClusterPoller>> TEST_ListPollers() const = 0;
  virtual std::vector<std::shared_ptr<client::YBClient>> GetYbClientsList() const = 0;
  virtual void ClearAllClientMetaCaches() const = 0;
};

typedef std::function<Result<pgwrapper::PGConn>(const std::string&, const CoarseTimePoint&)>
    ConnectToPostgresFunc;
typedef std::function<Result<std::pair<NamespaceId, NamespaceName>>(const TabletId&)>
    GetNamespaceInfoFunc;

Result<std::unique_ptr<XClusterConsumerIf>> CreateXClusterConsumer(
    std::function<int64_t(const TabletId&)> get_leader_term, ConnectToPostgresFunc connect_to_pg,
    GetNamespaceInfoFunc get_namespace_info, rpc::ProxyCache* proxy_cache, TabletServer* tserver);

}  // namespace tserver
}  // namespace yb
