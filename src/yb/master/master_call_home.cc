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

#include "yb/master/catalog_manager_if.h"
#include "yb/master/master.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/ts_descriptor.h"
#include "yb/master/ts_manager.h"

#include "yb/rpc/rpc_controller.h"

#include "yb/tserver/tserver_service.proxy.h"

#include "yb/util/flags.h"
#include "yb/util/format.h"

DEFINE_RUNTIME_int32(callhome_ysql_cluster_stats_rpc_timeout_ms, 90000,
    "Timeout in milliseconds for the CollectYsqlCallHomeStats RPC from the master leader "
    "to the closest TServer during YSQL call-home stats collection.");

DECLARE_bool(enable_ysql);

using std::string;
using std::vector;

using strings::Substitute;

namespace yb {

namespace master {

class BasicCollector : public MasterCollector {
 public:
  using MasterCollector::MasterCollector;

  void Collect(CollectionLevel collection_level) override {
    AppendPairToJson("server_type", "master", &json_);

    auto config = master()->catalog_manager()->GetClusterConfig();
    if (config.ok()) {
      AppendPairToJson("cluster_uuid", config->cluster_uuid(), &json_);
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
    auto status = master()->catalog_manager()->ListTables(&req, &resp);
    if (!status.ok()) {
      LOG(INFO) << "Error getting number of tables";
      return;
    }
    if (collection_level == CollectionLevel::LOW) {
      json_ = Substitute("\"tables\":$0", resp.tables_size());
    } else {
      // TODO: Add more table details.
      json_ = Substitute("\"tables\":$0", resp.tables_size());
    }
  }

  string collector_name() override { return "TablesCollector"; }

  CollectionLevel collection_level() override { return CollectionLevel::ALL; }
};

class MasterInfoCollector : public MasterCollector {
 public:
  using MasterCollector::MasterCollector;

  void Collect(CollectionLevel collection_level) override {
    vector<ServerEntryPB> masters;
    Status s = master()->ListMasters(&masters);
    if (s.ok()) {
      if (collection_level == CollectionLevel::LOW) {
        json_ = Substitute("\"masters\":$0", masters.size());
      } else {
        // TODO(hector): Add more details.
        json_ = Substitute("\"masters\":$0", masters.size());
      }
    }
  }

  string collector_name() override { return "MasterInfoCollector"; }

  CollectionLevel collection_level() override { return CollectionLevel::ALL; }
};

class TServersInfoCollector : public MasterCollector {
 public:
  using MasterCollector::MasterCollector;

  void Collect(CollectionLevel collection_level) override {
    json_ = Substitute("\"tservers\":$0", master()->ts_manager()->NumDescriptors());
  }

  string collector_name() override { return "TServersInfoCollector"; }

  CollectionLevel collection_level() override { return CollectionLevel::ALL; }
};

class TabletsCollector : public MasterCollector {
 public:
  using MasterCollector::MasterCollector;

  void Collect(CollectionLevel collection_level) override {
    int ntablets = 1;
    json_ = Substitute("\"tablets\":$0", ntablets);
  }

  string collector_name() override { return "TabletsCollector"; }

  CollectionLevel collection_level() override { return CollectionLevel::ALL; }
};

// Collects YSQL cluster-wide statistics via RPC to the closest TServer.
class YsqlClusterStatsCollector : public MasterCollector {
 public:
  using MasterCollector::MasterCollector;

  void Collect(CollectionLevel collection_level) override {
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
  Result<string> CollectViaRpc() {
    auto closest_ts = VERIFY_RESULT(master()->catalog_manager()->GetClosestLiveTserver());

    std::shared_ptr<tserver::TabletServerServiceProxy> proxy;
    RETURN_NOT_OK(closest_ts->GetProxy(&proxy));

    tserver::CollectYsqlCallHomeStatsRequestPB req;
    tserver::CollectYsqlCallHomeStatsResponsePB resp;
    rpc::RpcController controller;
    controller.set_timeout(
        MonoDelta::FromMilliseconds(FLAGS_callhome_ysql_cluster_stats_rpc_timeout_ms));

    RETURN_NOT_OK(proxy->CollectYsqlCallHomeStats(req, &resp, &controller));

    if (resp.has_error()) {
      return StatusFromPB(resp.error().status());
    }
    return resp.json_stats();
  }
};

MasterCallHome::MasterCallHome(Master* server) : CallHome(server) {
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
  auto* master = down_cast<Master*>(server_);
  if (!master->catalog_manager()->CheckIsLeaderAndReady().ok()) {
    VLOG(3) << "This master instance is not a leader. Skipping call home";
    return true;
  }
  return false;
}

}  // namespace master

}  // namespace yb
