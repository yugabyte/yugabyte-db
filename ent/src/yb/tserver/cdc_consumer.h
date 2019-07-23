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

#ifndef ENT_SRC_YB_TSERVER_CDC_CONSUMER_H
#define ENT_SRC_YB_TSERVER_CDC_CONSUMER_H

#include <unordered_map>

#include "yb/cdc/cdc_consumer_util.h"

#include "yb/util/locks.h"

namespace yb {

class Thread;
class ThreadPool;

namespace rpc {

class ProxyCache;

} // namespace rpc

namespace cdc {

class CDCConsumerProxyManager;
class ConsumerRegistryPB;

} // namespace cdc

namespace tserver {
namespace enterprise {

class CDCPoller;


class CDCConsumer {
 public:
  static Result<std::unique_ptr<CDCConsumer>> Create(
      std::function<bool(const std::string&)> is_leader_for_tablet,
      rpc::ProxyCache* proxy_cache,
      const std::string& ts_uuid);
  ~CDCConsumer();
  // Refreshes the in memory state when we receive a new registry from master.
  void RefreshWithNewRegistryFromMaster(const cdc::ConsumerRegistryPB& consumer_registry);

  std::vector<std::string> TEST_producer_tablets_running();

  std::string LogPrefix();

 private:
  CDCConsumer(std::function<bool(const std::string&)> is_leader_for_tablet,
              rpc::ProxyCache* proxy_cache,
              const std::string& ts_uuid);

  // Runs a thread that periodically polls for any new threads.
  void RunThread();

  // Loops through all the entries in the registry and creates a producer -> consumer tablet
  // mapping.
  void UpdateInMemoryState(const cdc::ConsumerRegistryPB& consumer_producer_map);

  // Loops through all entries in registry from master to check if all producer tablets are being
  // polled for.
  void TriggerPollForNewTablets();

  bool ShouldContinuePolling(const cdc::ProducerTabletInfo& producer_tablet_info);

  void RemoveFromPollersMap(const cdc::ProducerTabletInfo& producer_tablet_info);

  // Mutex for producer_consumer_tablet_map_from_master_.
  rw_spinlock master_data_mutex_;

  // Mutex for producer_pollers_map_.
  rw_spinlock producer_pollers_map_mutex_;

  // Mutex and cond for should_run_ state.
  std::mutex should_run_mutex_;
  std::condition_variable cond_;

  std::function<bool(const std::string&)> is_leader_for_tablet_;
  std::unique_ptr<cdc::CDCConsumerProxyManager> proxy_manager_;

  std::unordered_map<cdc::ProducerTabletInfo, cdc::ConsumerTabletInfo,
                     cdc::ProducerTabletInfo::Hash> producer_consumer_tablet_map_from_master_;

  scoped_refptr<Thread> run_trigger_poll_thread_;

  std::unordered_map<cdc::ProducerTabletInfo, std::unique_ptr<CDCPoller>,
                     cdc::ProducerTabletInfo::Hash> producer_pollers_map_;

  std::unique_ptr<ThreadPool> thread_pool_;

  std::string log_prefix_;

  bool should_run_ = true;

};

} // namespace enterprise
} // namespace tserver
} // namespace yb

#endif // ENT_SRC_YB_TSERVER_CDC_CONSUMER_H
