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

#include "yb/master/master_call_home.h"

#include "yb/common/wire_protocol.h"

#include "yb/master/master_cluster.proxy.h"
#include "yb/master/master_ddl.proxy.h"

#include "yb/rpc/rpc_controller.h"

#include "yb/server/server_base.h"

#include "yb/tserver/tserver_service.proxy.h"
#include "yb/tserver/ysql_call_home_stats.h"

#include "yb/util/flags.h"
#include "yb/util/format.h"
#include "yb/util/net/net_util.h"

DEFINE_RUNTIME_int32(callhome_ysql_cluster_stats_rpc_timeout_ms, 90000,
    "Timeout in milliseconds for the CollectYsqlCallHomeStats RPC from the master leader "
    "to the closest TServer during YSQL call-home stats collection.");

DECLARE_bool(enable_ysql);

using std::string;

namespace yb {

namespace master {

namespace {

const auto kLocalRpcTimeout = MonoDelta::FromSeconds(10);

class MasterCollector : public Collector {
 public:
  using Collector::Collector;

 protected:
  // A proxy to this master's own services.
  template <class Proxy>
  Proxy LocalProxy() {
    return Proxy(&server_->proxy_cache(), HostPort(server_->first_rpc_address()));
  }

  template <class Resp>
  static Status ResponseStatus(const Status& rpc_status, const Resp& resp) {
    RETURN_NOT_OK(rpc_status);
    if (resp.has_error()) {
      return StatusFromPB(resp.error().status());
    }
    return Status::OK();
  }

  Result<ListTabletServersResponsePB> ListTabletServers() {
    ListTabletServersRequestPB req;
    ListTabletServersResponsePB resp;
    rpc::RpcController controller;
    controller.set_timeout(kLocalRpcTimeout);
    auto proxy = LocalProxy<MasterClusterProxy>();
    RETURN_NOT_OK(ResponseStatus(proxy.ListTabletServers(req, &resp, &controller), resp));
    return resp;
  }
};

class BasicCollector : public MasterCollector {
 public:
  using MasterCollector::MasterCollector;

  void Collect(CollectionLevel collection_level) override {
    AppendPairToJson("server_type", "master", &json_);

    GetMasterClusterConfigRequestPB req;
    GetMasterClusterConfigResponsePB resp;
    rpc::RpcController controller;
    controller.set_timeout(kLocalRpcTimeout);
    auto proxy = LocalProxy<MasterClusterProxy>();
    if (ResponseStatus(proxy.GetMasterClusterConfig(req, &resp, &controller), resp).ok()) {
      AppendPairToJson("cluster_uuid", resp.cluster_config().cluster_uuid(), &json_);
    }
  }

  string collector_name() override { return "BasicCollector"; }

  CollectionLevel collection_level() override { return CollectionLevel::LOW; }
};

class TablesCollector : public MasterCollector {
 public:
  using MasterCollector::MasterCollector;

  void Collect(CollectionLevel collection_level) override {
    ListTablesRequestPB req;
    req.set_exclude_system_tables(true);
    ListTablesResponsePB resp;
    rpc::RpcController controller;
    controller.set_timeout(kLocalRpcTimeout);
    auto proxy = LocalProxy<MasterDdlProxy>();
    if (!ResponseStatus(proxy.ListTables(req, &resp, &controller), resp).ok()) {
      LOG(INFO) << "Error getting number of tables";
      return;
    }
    json_ = Format("\"tables\":$0", resp.tables_size());
  }

  string collector_name() override { return "TablesCollector"; }

  CollectionLevel collection_level() override { return CollectionLevel::ALL; }
};

class MasterInfoCollector : public MasterCollector {
 public:
  using MasterCollector::MasterCollector;

  void Collect(CollectionLevel collection_level) override {
    ListMastersRequestPB req;
    ListMastersResponsePB resp;
    rpc::RpcController controller;
    controller.set_timeout(kLocalRpcTimeout);
    auto proxy = LocalProxy<MasterClusterProxy>();
    if (ResponseStatus(proxy.ListMasters(req, &resp, &controller), resp).ok()) {
      json_ = Format("\"masters\":$0", resp.masters_size());
    }
  }

  string collector_name() override { return "MasterInfoCollector"; }

  CollectionLevel collection_level() override { return CollectionLevel::ALL; }
};

class TServersInfoCollector : public MasterCollector {
 public:
  using MasterCollector::MasterCollector;

  void Collect(CollectionLevel collection_level) override {
    auto tservers = ListTabletServers();
    if (tservers.ok()) {
      json_ = Format("\"tservers\":$0", tservers->servers_size());
    }
  }

  string collector_name() override { return "TServersInfoCollector"; }

  CollectionLevel collection_level() override { return CollectionLevel::ALL; }
};

class TabletsCollector : public MasterCollector {
 public:
  using MasterCollector::MasterCollector;

  void Collect(CollectionLevel collection_level) override {
    int ntablets = 1;
    json_ = Format("\"tablets\":$0", ntablets);
  }

  string collector_name() override { return "TabletsCollector"; }

  CollectionLevel collection_level() override { return CollectionLevel::ALL; }
};

class YsqlClusterStatsCollector : public MasterCollector {
 public:
  using MasterCollector::MasterCollector;

  void Collect(CollectionLevel collection_level) override {
    if (!throttle_.ShouldCollect()) {
      return;
    }
    auto result = CollectViaRpc();
    if (!result.ok()) {
      LOG(WARNING) << "YSQL Call Home Stats: Failed to collect cluster stats: "
                   << result.status();
      return;
    }
    json_ = Format("\"ysql_cluster_stats\":$0", *result);
  }

  string collector_name() override { return "YsqlClusterStatsCollector"; }

  CollectionLevel collection_level() override { return CollectionLevel::ALL; }

 private:
  // A live tserver, preferring one in this master's zone, as
  // CatalogManager::GetClosestLiveTserver does.
  Result<HostPort> ClosestLiveTserver() {
    auto tservers = VERIFY_RESULT(ListTabletServers());
    const auto& options = server_->options();
    std::optional<HostPort> any_live;
    for (const auto& server : tservers.servers()) {
      if (!server.alive()) {
        continue;
      }
      const auto& common = server.registration().common();
      if (common.private_rpc_addresses().empty()) {
        continue;
      }
      auto address = HostPortFromPB(common.private_rpc_addresses(0));
      if (common.cloud_info().placement_cloud() == options.placement_cloud() &&
          common.cloud_info().placement_region() == options.placement_region() &&
          common.cloud_info().placement_zone() == options.placement_zone()) {
        return address;
      }
      if (!any_live) {
        any_live = address;
      }
    }
    if (any_live) {
      return *any_live;
    }
    return STATUS(NotFound, "No live tserver");
  }

  Result<string> CollectViaRpc() {
    tserver::TabletServerServiceProxy proxy(&server_->proxy_cache(), VERIFY_RESULT(
        ClosestLiveTserver()));

    tserver::CollectYsqlCallHomeStatsRequestPB req;
    tserver::CollectYsqlCallHomeStatsResponsePB resp;
    rpc::RpcController controller;
    controller.set_timeout(
        MonoDelta::FromMilliseconds(FLAGS_callhome_ysql_cluster_stats_rpc_timeout_ms));

    RETURN_NOT_OK(proxy.CollectYsqlCallHomeStats(req, &resp, &controller));

    if (resp.has_error()) {
      return StatusFromPB(resp.error().status());
    }
    return resp.json_stats();
  }

  tserver::YsqlCollectionThrottle throttle_;
};

}  // namespace

MasterCallHome::MasterCallHome(server::RpcAndWebServerBase* server) : CallHome(server) {
  AddCollector<BasicCollector>();
  AddCollector<MasterInfoCollector>();
  AddCollector<TServersInfoCollector>();
  AddCollector<TablesCollector>();
  AddCollector<TabletsCollector>();
  if (FLAGS_enable_ysql) {
    AddCollector<YsqlClusterStatsCollector>();
  }
}

bool MasterCallHome::SkipCallHome() {
  IsMasterLeaderReadyRequestPB req;
  IsMasterLeaderReadyResponsePB resp;
  rpc::RpcController controller;
  controller.set_timeout(kLocalRpcTimeout);
  MasterClusterProxy proxy(&server_->proxy_cache(), HostPort(server_->first_rpc_address()));
  auto status = proxy.IsMasterLeaderServiceReady(req, &resp, &controller);
  if (!status.ok() || resp.has_error()) {
    VLOG(3) << "This master instance is not a leader. Skipping call home";
    return true;
  }
  return false;
}

}  // namespace master

}  // namespace yb
