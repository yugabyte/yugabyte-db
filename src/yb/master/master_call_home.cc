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

#include "yb/gutil/walltime.h"
#include "yb/master/master_call_home.h"
#include "yb/master/catalog_manager_if.h"
#include "yb/master/master_ddl.pb.h"
#include "yb/master/ts_descriptor.h"
#include "yb/master/ts_manager.h"
#include "yb/util/version_info.h"

using std::string;
using std::vector;

using strings::Substitute;
using yb::master::TSDescriptor;

namespace yb {

namespace master {

class BasicCollector : public MasterCollector {
 public:
  using MasterCollector::MasterCollector;

  void Collect(CollectionLevel collection_level) override {
    auto config = master()->catalog_manager()->GetClusterConfig();
    if (config.ok()) {
      AppendPairToJson("cluster_uuid", config->cluster_uuid(), &json_);
    }
    AppendPairToJson("node_uuid", master()->fs_manager()->uuid(), &json_);
    AppendPairToJson("server_type", "master", &json_);

    // Only collect hostname and username if collection level is medium or high.
    if (collection_level != CollectionLevel::LOW) {
      AppendPairToJson("hostname", master()->get_hostname(), &json_);
      AppendPairToJson("current_user", GetCurrentUser(), &json_);
    }
    json_ += ",\"version_info\":" + VersionInfo::GetAllVersionInfoJson();
    AppendPairToJson("timestamp", std::to_string(WallTime_Now()), &json_);
  }

  string collector_name() override { return "BasicCollector"; }

  virtual CollectionLevel collection_level() override { return CollectionLevel::LOW; }
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

  virtual CollectionLevel collection_level() override { return CollectionLevel::ALL; }
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

  virtual CollectionLevel collection_level() override { return CollectionLevel::ALL; }
};

class TServersInfoCollector : public MasterCollector {
 public:
  using MasterCollector::MasterCollector;

  void Collect(CollectionLevel collection_level) override {
    json_ = Substitute("\"tservers\":$0", master()->ts_manager()->NumDescriptors());
  }

  string collector_name() override { return "TServersInfoCollector"; }

  virtual CollectionLevel collection_level() override { return CollectionLevel::ALL; }
};

class TabletsCollector : public MasterCollector {
 public:
  using MasterCollector::MasterCollector;

  void Collect(CollectionLevel collection_level) override {
    int ntablets = 1;
    json_ = Substitute("\"tablets\":$0", ntablets);
  }

  string collector_name() override { return "TabletsCollector"; }

  virtual CollectionLevel collection_level() override { return CollectionLevel::ALL; }
};

MasterCallHome::MasterCallHome(Master* server) : CallHome(server) {
  AddCollector<BasicCollector>();
  AddCollector<MasterInfoCollector>();
  AddCollector<TServersInfoCollector>();
  AddCollector<TablesCollector>();
  AddCollector<TabletsCollector>();
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
