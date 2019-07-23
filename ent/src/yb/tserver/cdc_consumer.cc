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
#include <chrono>

#include "yb/tserver/cdc_consumer.h"
#include "yb/tserver/twodc_output_client.h"
#include "yb/tserver/tablet_server.h"
#include "yb/tserver/cdc_poller.h"

#include "yb/cdc/cdc_consumer.pb.h"
#include "yb/cdc/cdc_consumer_proxy_manager.h"

#include "yb/util/string_util.h"
#include "yb/util/thread.h"

using namespace std::chrono_literals;

namespace yb {

namespace tserver {
namespace enterprise {

Result<std::unique_ptr<CDCConsumer>> CDCConsumer::Create(
    std::function<bool(const std::string&)> is_leader_for_tablet,
    rpc::ProxyCache* proxy_cache,
    const string& ts_uuid) {
  auto cdc_consumer = std::unique_ptr<CDCConsumer>(
      new CDCConsumer(std::move(is_leader_for_tablet), proxy_cache, ts_uuid));
  RETURN_NOT_OK(yb::Thread::Create(
      "CDCConsumer", "Poll", &CDCConsumer::RunThread, cdc_consumer.get(),
      &cdc_consumer->run_trigger_poll_thread_));
  RETURN_NOT_OK(ThreadPoolBuilder("Handle").Build(&cdc_consumer->thread_pool_));
  return cdc_consumer;
}

CDCConsumer::CDCConsumer(std::function<bool(const std::string&)> is_leader_for_tablet,
                         rpc::ProxyCache* proxy_cache,
                         const string& ts_uuid) :
  is_leader_for_tablet_(std::move(is_leader_for_tablet)),
  proxy_manager_(std::make_unique<cdc::CDCConsumerProxyManager>(proxy_cache)),
  log_prefix_(Format("[TS $0]:", ts_uuid)) {}

CDCConsumer::~CDCConsumer() {
  {
    std::unique_lock<std::mutex> l(should_run_mutex_);
    should_run_ = false;
    cond_.notify_all();
  }

  if (run_trigger_poll_thread_) {
    WARN_NOT_OK(ThreadJoiner(run_trigger_poll_thread_.get()).Join(), "Could not join thread");
  }

  if (thread_pool_) {
    thread_pool_->Shutdown();
  }
}

void CDCConsumer::RunThread() {
  while (true) {
    std::unique_lock<std::mutex> l(should_run_mutex_);
    cond_.wait_for(l, 1000ms);
    if (!should_run_) {
      return;
    }
    TriggerPollForNewTablets();
  }
}

void CDCConsumer::RefreshWithNewRegistryFromMaster(
    const cdc::ConsumerRegistryPB& consumer_registry) {
  UpdateInMemoryState(consumer_registry);
  cond_.notify_all();
}

std::vector<std::string> CDCConsumer::TEST_producer_tablets_running() {
  std::shared_lock<rw_spinlock> pollers_lock(producer_pollers_map_mutex_);

  std::vector<string> tablets;
  for (const auto& producer : producer_pollers_map_) {
    tablets.push_back(producer.first.tablet_id);
  }
  return tablets;
}

void CDCConsumer::UpdateInMemoryState(const cdc::ConsumerRegistryPB& consumer_registry) {
  std::unique_lock<rw_spinlock> lock(master_data_mutex_);

  producer_consumer_tablet_map_from_master_.clear();
  for (const auto& producer_map : consumer_registry.producer_map()) {
    const auto& producer_entry_pb = producer_map.second;
    proxy_manager_->UpdateProxies(producer_entry_pb);
    for (const auto& stream_entry : producer_entry_pb.stream_map()) {
      const auto& stream_entry_pb = stream_entry.second;
      for (const auto& tablet_entry : stream_entry_pb.consumer_producer_tablet_map()) {
        const auto& consumer_tablet_id = tablet_entry.first;
        for (const auto& producer_tablet_id : tablet_entry.second.tablets()) {
          cdc::ProducerTabletInfo producer_tablet_info({stream_entry.first, producer_tablet_id});
          cdc::ConsumerTabletInfo consumer_tablet_info(
              {consumer_tablet_id, stream_entry_pb.consumer_table_id()});
          producer_consumer_tablet_map_from_master_[producer_tablet_info] = consumer_tablet_info;
        }
      }
    }
  }
}

void CDCConsumer::TriggerPollForNewTablets() {
  std::shared_lock<rw_spinlock> master_lock(master_data_mutex_);

  for (const auto& entry : producer_consumer_tablet_map_from_master_) {
    bool start_polling;
    {
      std::shared_lock<rw_spinlock> pollers_lock(producer_pollers_map_mutex_);
      start_polling = producer_pollers_map_.find(entry.first) == producer_pollers_map_.end() &&
                      is_leader_for_tablet_(entry.second.tablet_id);
    }
    if (start_polling) {
      // This is a new tablet, trigger a poll.
      std::unique_lock<rw_spinlock> pollers_lock(producer_pollers_map_mutex_);
      producer_pollers_map_[entry.first] = std::make_unique<CDCPoller>(
          entry.first, entry.second,
          std::bind(&CDCConsumer::ShouldContinuePolling, this, entry.first),
          std::bind(&cdc::CDCConsumerProxyManager::GetProxy, proxy_manager_.get(), entry.first),
          std::bind(&CDCConsumer::RemoveFromPollersMap, this, entry.first),
          thread_pool_.get());
      LOG_WITH_PREFIX(INFO) << Format("Start polling for producer tablet $0",
                                      entry.first.tablet_id);
      producer_pollers_map_[entry.first]->Poll();
    }
  }
}

void CDCConsumer::RemoveFromPollersMap(const cdc::ProducerTabletInfo& producer_tablet_info) {
  LOG_WITH_PREFIX(INFO) << Format("Stop polling for producer tablet $0",
                                  producer_tablet_info.tablet_id);
  std::unique_lock<rw_spinlock> pollers_lock(producer_pollers_map_mutex_);
  producer_pollers_map_.erase(producer_tablet_info);
}

bool CDCConsumer::ShouldContinuePolling(const cdc::ProducerTabletInfo& producer_tablet_info) {
  std::shared_lock<rw_spinlock> master_lock(master_data_mutex_);

  const auto& it = producer_consumer_tablet_map_from_master_.find(producer_tablet_info);
  if (it == producer_consumer_tablet_map_from_master_.end()) {
    // We no longer care about this tablet, abort the cycle.
    return false;
  }
  return is_leader_for_tablet_(it->second.tablet_id);
}

std::string CDCConsumer::LogPrefix() {
  return log_prefix_;
}

} // namespace enterprise
} // namespace tserver
} // namespace yb
