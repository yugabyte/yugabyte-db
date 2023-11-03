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

#include "yb/tserver/tserver_call_home.h"
#include <boost/system/error_code.hpp>
#include "yb/gutil/walltime.h"
#include "yb/tserver/ts_tablet_manager.h"

using std::string;

using strings::Substitute;

namespace yb {

namespace tserver {

class BasicCollector : public TserverCollector {
 public:
  using TserverCollector::TserverCollector;

  void Collect(CollectionLevel collection_level) override {
    AppendPairToJson("cluster_uuid", tserver()->cluster_uuid(), &json_);
    AppendPairToJson("node_uuid", tserver()->permanent_uuid(), &json_);
    AppendPairToJson("server_type", "tserver", &json_);

    // Only collect hostname and username if collection level is medium or high.
    if (collection_level != CollectionLevel::LOW) {
      AppendPairToJson("hostname", tserver()->get_hostname(), &json_);
      AppendPairToJson("current_user", GetCurrentUser(), &json_);
    }

    AppendPairToJson("timestamp", std::to_string(WallTime_Now()), &json_);
  }

  string collector_name() override { return "BasicCollector"; }

  virtual CollectionLevel collection_level() override { return CollectionLevel::LOW; }
};

class TabletsCollector : public TserverCollector {
 public:
  using TserverCollector::TserverCollector;

  void Collect(CollectionLevel collection_level) override {
    int ntablets = tserver()->tablet_manager()->GetNumLiveTablets();
    json_ = Substitute("\"tablets\":$0", ntablets);
  }

  string collector_name() override { return "TabletsCollector"; }

  virtual CollectionLevel collection_level() override { return CollectionLevel::ALL; }
};

TserverCallHome::TserverCallHome(TabletServer* server) : CallHome(server) {
  AddCollector<BasicCollector>();
  AddCollector<TabletsCollector>();
}

}  // namespace tserver

}  // namespace yb
