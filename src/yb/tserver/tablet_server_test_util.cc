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
#include "yb/tserver/tablet_server_test_util.h"

#include "yb/consensus/consensus.proxy.h"
#include "yb/server/server_base.proxy.h"
#include "yb/tserver/tserver_admin.proxy.h"
#include "yb/tserver/tserver_service.proxy.h"

namespace yb {
namespace tserver {

using consensus::ConsensusServiceProxy;

void CreateTsClientProxies(const HostPort& addr,
                           rpc::ProxyCache* proxy_cache,
                           std::unique_ptr<TabletServerServiceProxy>* proxy,
                           std::unique_ptr<TabletServerAdminServiceProxy>* admin_proxy,
                           std::unique_ptr<ConsensusServiceProxy>* consensus_proxy,
                           std::unique_ptr<server::GenericServiceProxy>* generic_proxy) {
  proxy->reset(new TabletServerServiceProxy(proxy_cache, addr));
  admin_proxy->reset(new TabletServerAdminServiceProxy(proxy_cache, addr));
  consensus_proxy->reset(new ConsensusServiceProxy(proxy_cache, addr));
  generic_proxy->reset(new server::GenericServiceProxy(proxy_cache, addr));
}

} // namespace tserver
} // namespace yb
