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
#include "yb/server/intentsdb-path-handler.h"

#include <functional>
#include <memory>
#include <string>

#include "yb/rpc/messenger.h"
#include "yb/server/webserver.h"
#include "yb/tserver/tserver.pb.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/tserver_admin.proxy.h"
#include "yb/util/net/net_util.h"
#include "yb/util/status_log.h"

namespace yb {

using yb::tserver::ReadIntentsRequestPB;
using yb::tserver::ReadIntentsResponsePB;
using yb::rpc::Messenger;
using std::shared_ptr;
using std::stringstream;

using namespace std::placeholders;

DEFINE_bool(enable_intentsdb_page, false, "Enable displaying the contents of intentsdb page.");

namespace {

void IntentsDBPathHandler(
    rpc::Messenger* messenger,
    Webserver* webserver,
    const Webserver::WebRequest& req,
    Webserver::WebResponse* resp) {
  std::stringstream *output = &resp->output;
  if (!FLAGS_enable_intentsdb_page) {
    (*output) << "Intentsdb is disabled, please set the gflag enable_intentsdb_page to true"
                 "to show the content of intentsdb.";
    return;
  }

  HostPort host_port("localhost", tserver::TabletServer::kDefaultPort);

  auto proxy_cache = std::make_unique<rpc::ProxyCache>(messenger);
  auto ts_admin_proxy_ = std::make_unique<tserver::TabletServerAdminServiceProxy>(
    proxy_cache.get(), host_port);

  ReadIntentsRequestPB intents_req;
  ReadIntentsResponsePB intents_resp;

  auto it = req.parsed_args.find("tablet_id");
  if (it != req.parsed_args.end()) {
    intents_req.set_tablet_id(it->second);
  }

  rpc::RpcController rpc;
  rpc.set_timeout(MonoDelta::FromMilliseconds(1000 * 60));
  Status s = ts_admin_proxy_->ReadIntents(intents_req, &intents_resp, &rpc);
  if (!s.ok()) {
    (*output) << "Error when reading intents information";
    return;
  }

  (*output) << Format("Intents size: $0\n", intents_resp.intents().size()) << "\n";
  for (const auto& item : intents_resp.intents()) {
    (*output) << Format("$0\n", item);
  }
}

} // anonymous namespace

void AddIntentsDBPathHandler(rpc::Messenger* messenger, Webserver* webserver) {
  webserver->RegisterPathHandler(
      "/intentsdb", "IntentsDB",
      std::bind(IntentsDBPathHandler, messenger, webserver, _1, _2), false, false);
}

} // namespace yb
