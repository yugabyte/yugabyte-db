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
#ifndef KUDU_MASTER_MASTER_PATH_HANDLERS_H
#define KUDU_MASTER_MASTER_PATH_HANDLERS_H

#include "kudu/gutil/macros.h"
#include "kudu/server/webserver.h"

#include <string>
#include <sstream>
#include <vector>

namespace kudu {

class Schema;

namespace master {

class Master;
struct TabletReplica;
class TSDescriptor;
class TSRegistrationPB;

// Web page support for the master.
class MasterPathHandlers {
 public:
  explicit MasterPathHandlers(Master* master)
    : master_(master) {
  }

  ~MasterPathHandlers();

  Status Register(Webserver* server);

 private:
  void HandleTabletServers(const Webserver::WebRequest& req,
                           std::stringstream* output);
  void HandleCatalogManager(const Webserver::WebRequest& req,
                            std::stringstream* output);
  void HandleTablePage(const Webserver::WebRequest& req,
                       std::stringstream *output);
  void HandleMasters(const Webserver::WebRequest& req,
                     std::stringstream* output);
  void HandleDumpEntities(const Webserver::WebRequest& req,
                          std::stringstream* output);

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
  // anchor text 'link_text'. 'RegistrationType' must be
  // TSRegistrationPB or MasterRegistrationPB.
  template<class RegistrationType>
  std::string RegistrationToHtml(const RegistrationType& reg,
                                 const std::string& link_text) const;

  Master* master_;
  DISALLOW_COPY_AND_ASSIGN(MasterPathHandlers);
};

void HandleTabletServersPage(const Webserver::WebRequest& req, std::stringstream* output);

} // namespace master
} // namespace kudu
#endif /* KUDU_MASTER_MASTER_PATH_HANDLERS_H */
