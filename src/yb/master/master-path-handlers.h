// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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
#ifndef YB_MASTER_MASTER_PATH_HANDLERS_H
#define YB_MASTER_MASTER_PATH_HANDLERS_H

#include <string>
#include <sstream>
#include <vector>

#include "yb/common/wire_protocol.pb.h"
#include "yb/gutil/macros.h"
#include "yb/master/catalog_manager.h"
#include "yb/server/webserver.h"

namespace yb {

class Schema;

namespace master {

static constexpr char kTserverAlive[] = "ALIVE";
static constexpr char kTserverDead[] = "DEAD";

class Master;
struct TabletReplica;
class TSDescriptor;
class TSRegistrationPB;

// Web page support for the master.
class MasterPathHandlers {
 public:
  explicit MasterPathHandlers(Master* master)
    : master_(master),
      output_precision_(6) {
  }

  ~MasterPathHandlers();

  const string kYBOrange = "#f75821";
  const string kYBDarkBlue = "#202951";
  const string kYBLightBlue = "#3eb1cc";
  const string kYBGray = "#5e647a";

  CHECKED_STATUS Register(Webserver* server);

  string BytesToHumanReadable (uint64_t bytes);

 private:
  enum TableType {
    kUserTable,
    kIndexTable,
    kSystemTable,
    kNumTypes
  };

  struct TabletCounts {
    uint32_t user_tablet_leaders = 0;
    uint32_t user_tablet_followers = 0;
    uint32_t system_tablet_leaders = 0;
    uint32_t system_tablet_followers = 0;
  };

  // Map of tserver UUID -> TabletCounts
  typedef std::unordered_map<std::string, TabletCounts> TabletCountMap;

  const string table_type_[3] = {"User", "Index", "System"};

  const string kNoPlacementUUID = "NONE";

  static inline void TServerTable(std::stringstream* output);

  void TServerDisplay(const std::string& current_uuid,
                      std::vector<std::shared_ptr<TSDescriptor>>* descs,
                      TabletCountMap* tmap,
                      std::stringstream* output);

  void CallIfLeaderOrPrintRedirect(const Webserver::WebRequest& req, std::stringstream* output,
                                   const Webserver::PathHandlerCallback& callback);

  void RootHandler(const Webserver::WebRequest& req,
                   std::stringstream* output);
  void HandleTabletServers(const Webserver::WebRequest& req,
                           std::stringstream* output);
  void HandleCatalogManager(const Webserver::WebRequest& req,
                            std::stringstream* output,
                            bool skip_system_tables = false);
  void HandleTablePage(const Webserver::WebRequest& req,
                       std::stringstream *output);
  void HandleMasters(const Webserver::WebRequest& req,
                     std::stringstream* output);
  void HandleDumpEntities(const Webserver::WebRequest& req,
                          std::stringstream* output);
  void HandleGetClusterConfig(const Webserver::WebRequest& req, std::stringstream* output);

  // Convert location of peers to HTML, indicating the roles
  // of each tablet server in a consensus configuration.
  // This method will display 'locations' in the order given.
  std::string RaftConfigToHtml(const std::vector<TabletReplica>& locations,
                               const std::string& tablet_id) const;

  // Convert the specified TSDescriptor to HTML, adding a link to the
  // tablet server's own webserver if specified in 'desc'.
  std::string TSDescriptorToHtml(const TSDescriptor& desc,
                                 const std::string& tablet_id) const;

  // Convert the specified server registration to HTML, adding a link
  // to the server's own web server (if specified in 'reg') with
  // anchor text 'link_text'.
  std::string RegistrationToHtml(
      const ServerRegistrationPB& reg, const std::string& link_text) const;

  Master* master_;

  const int output_precision_;
  DISALLOW_COPY_AND_ASSIGN(MasterPathHandlers);
};

void HandleTabletServersPage(const Webserver::WebRequest& req, std::stringstream* output);

} // namespace master
} // namespace yb
#endif /* YB_MASTER_MASTER_PATH_HANDLERS_H */
