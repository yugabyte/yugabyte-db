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

#include "kudu/tserver/tablet_server_test_util.h"

#include "kudu/consensus/consensus.proxy.h"
#include "kudu/rpc/messenger.h"
#include "kudu/server/server_base.proxy.h"
#include "kudu/tserver/tserver_admin.proxy.h"
#include "kudu/tserver/tserver_service.proxy.h"

namespace kudu {
namespace tserver {

using consensus::ConsensusServiceProxy;
using rpc::Messenger;
using std::shared_ptr;

void CreateTsClientProxies(const Sockaddr& addr,
                           const shared_ptr<Messenger>& messenger,
                           gscoped_ptr<TabletServerServiceProxy>* proxy,
                           gscoped_ptr<TabletServerAdminServiceProxy>* admin_proxy,
                           gscoped_ptr<ConsensusServiceProxy>* consensus_proxy,
                           gscoped_ptr<server::GenericServiceProxy>* generic_proxy) {
  proxy->reset(new TabletServerServiceProxy(messenger, addr));
  admin_proxy->reset(new TabletServerAdminServiceProxy(messenger, addr));
  consensus_proxy->reset(new ConsensusServiceProxy(messenger, addr));
  generic_proxy->reset(new server::GenericServiceProxy(messenger, addr));
}

} // namespace tserver
} // namespace kudu
