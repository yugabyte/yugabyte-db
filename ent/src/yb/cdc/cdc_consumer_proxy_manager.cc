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
//

#include <shared_mutex>

#include "yb/cdc/cdc_consumer_proxy_manager.h"
#include "yb/cdc/cdc_consumer_util.h"
#include "yb/cdc/cdc_service.proxy.h"
#include "yb/cdc/cdc_consumer.pb.h"

#include "yb/util/random_util.h"

#include "yb/common/wire_protocol.h"

#include "yb/rpc/proxy.h"

namespace yb {
namespace cdc {

CDCConsumerProxyManager::~CDCConsumerProxyManager() {}

CDCConsumerProxyManager::CDCConsumerProxyManager(rpc::ProxyCache* proxy_cache) :
    proxy_cache_(proxy_cache) {}

cdc::CDCServiceProxy* CDCConsumerProxyManager::GetProxy(
    const cdc::ProducerTabletInfo& producer_tablet_info) {
  std::shared_lock<rw_spinlock> l(proxies_mutex_);
  // TODO(Rahul): Change this into a meta cache implementation.
  return RandomElement(proxies_).get();
}

void CDCConsumerProxyManager::UpdateProxies(const cdc::ProducerEntryPB& producer_entry_pb) {
  std::unique_lock<rw_spinlock> l(proxies_mutex_);
  for (const auto& hp_pb : producer_entry_pb.tserver_addrs()) {
    proxies_.push_back(std::make_unique<cdc::CDCServiceProxy>(proxy_cache_, HostPortFromPB(hp_pb)));
  }
}

} // namespace cdc
} // namespace yb
