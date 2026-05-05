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

#include "yb/tserver/tserver_call_home.h"

#include "yb/tserver/tablet_server.h"
#include "yb/tserver/ts_tablet_manager.h"
#include "yb/tserver/ysql_call_home_stats.h"

#include "yb/util/format.h"

DECLARE_bool(enable_ysql);

using std::string;

namespace yb {

namespace tserver {

class BasicCollector : public TserverCollector {
 public:
  using TserverCollector::TserverCollector;

  void Collect(CollectionLevel collection_level) override {
    AppendPairToJson("server_type", "tserver", &json_);
    AppendPairToJson("cluster_uuid", tserver()->cluster_uuid(), &json_);
  }

  string collector_name() override { return "BasicCollector"; }

  CollectionLevel collection_level() override { return CollectionLevel::LOW; }
};

class TabletsCollector : public TserverCollector {
 public:
  using TserverCollector::TserverCollector;

  void Collect(CollectionLevel collection_level) override {
    int ntablets = tserver()->tablet_manager()->GetNumLiveTablets();
    json_ = Format("\"tablets\":$0", ntablets);
  }

  string collector_name() override { return "TabletsCollector"; }

  CollectionLevel collection_level() override { return CollectionLevel::ALL; }
};

class YsqlNodeStatsCollector : public TserverCollector {
 public:
  using TserverCollector::TserverCollector;

  void Collect(CollectionLevel collection_level) override {
    if (!throttle_.ShouldCollect()) {
      return;
    }
    auto stats_json = BuildStatsJson(tserver(), {"template1"}, YsqlNodeQueries::kNodeLevel, {});
    json_ = Format("\"ysql_node_stats\":$0", stats_json);
  }

  string collector_name() override { return "YsqlNodeStatsCollector"; }

  CollectionLevel collection_level() override { return CollectionLevel::ALL; }

 private:
  YsqlCollectionThrottle throttle_;
};

TserverCallHome::TserverCallHome(TabletServer* server) : CallHome(server) {
  AddCollector<BasicCollector>();
  AddCollector<TabletsCollector>();
  if (FLAGS_enable_ysql) {
    AddCollector<YsqlNodeStatsCollector>();
  }
}

}  // namespace tserver

}  // namespace yb
