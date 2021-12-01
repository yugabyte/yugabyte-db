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

#include "yb/tserver/db_server_base.h"

#include "yb/client/transaction_manager.h"
#include "yb/client/transaction_pool.h"

#include "yb/server/clock.h"

#include "yb/tserver/tserver_util_fwd.h"
#include "yb/tserver/tserver_shared_mem.h"

#include "yb/util/shared_mem.h"
#include "yb/util/status_log.h"

namespace yb {
namespace tserver {

DbServerBase::DbServerBase(
    std::string name, const server::ServerBaseOptions& options,
    const std::string& metrics_namespace,
    std::shared_ptr<MemTracker> mem_tracker)
    : RpcAndWebServerBase(std::move(name), options, metrics_namespace, std::move(mem_tracker)),
      shared_object_(new tserver::TServerSharedObject(
          CHECK_RESULT(tserver::TServerSharedObject::Create()))) {
}

DbServerBase::~DbServerBase() {
}

client::TransactionManager* DbServerBase::TransactionManager() {
  return transaction_manager_holder_.get();
}

client::TransactionPool* DbServerBase::TransactionPool() {
  auto result = transaction_pool_.load(std::memory_order_acquire);
  if (result) {
    return result;
  }
  std::lock_guard<decltype(transaction_pool_mutex_)> lock(transaction_pool_mutex_);
  if (transaction_pool_holder_) {
    return transaction_pool_holder_.get();
  }
  transaction_manager_holder_ = std::make_unique<client::TransactionManager>(
      client_future().get(), clock(), CreateLocalTabletFilter());
  transaction_manager_.store(transaction_manager_holder_.get(), std::memory_order_release);
  transaction_pool_holder_ = std::make_unique<client::TransactionPool>(
      transaction_manager_holder_.get(), metric_entity().get());
  transaction_pool_.store(transaction_pool_holder_.get(), std::memory_order_release);
  return transaction_pool_holder_.get();
}

tserver::TServerSharedData& DbServerBase::shared_object() {
  return **shared_object_;
}

int DbServerBase::GetSharedMemoryFd() {
  return shared_object_->GetFd();
}

}  // namespace tserver
}  // namespace yb
