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
#ifndef KUDU_TSERVER_TABLET_SERVER_TEST_UTIL_H_
#define KUDU_TSERVER_TABLET_SERVER_TEST_UTIL_H_

#include <memory>

#include "kudu/gutil/gscoped_ptr.h"

namespace kudu {
class Sockaddr;

namespace consensus {
class ConsensusServiceProxy;
}

namespace rpc {
class Messenger;
}

namespace server {
class GenericServiceProxy;
}

namespace tserver {
class TabletServerAdminServiceProxy;
class TabletServerServiceProxy;

// Create tablet server client proxies for tests.
void CreateTsClientProxies(const Sockaddr& addr,
                           const std::shared_ptr<rpc::Messenger>& messenger,
                           gscoped_ptr<TabletServerServiceProxy>* proxy,
                           gscoped_ptr<TabletServerAdminServiceProxy>* admin_proxy,
                           gscoped_ptr<consensus::ConsensusServiceProxy>* consensus_proxy,
                           gscoped_ptr<server::GenericServiceProxy>* generic_proxy);

} // namespace tserver
} // namespace kudu

#endif // KUDU_TSERVER_TABLET_SERVER_TEST_UTIL_H_
