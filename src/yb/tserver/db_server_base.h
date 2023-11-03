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

#pragma once

#include <future>

#include "yb/client/client_fwd.h"

#include "yb/server/server_base.h"

#include "yb/tserver/tserver_util_fwd.h"

namespace yb {
namespace tserver {

class DbServerBase : public server::RpcAndWebServerBase {
 public:
  DbServerBase(
      std::string name, const server::ServerBaseOptions& options,
      const std::string& metrics_namespace,
      std::shared_ptr<MemTracker> mem_tracker);
  ~DbServerBase();

  int GetSharedMemoryFd();

  client::TransactionManager& TransactionManager();

  client::TransactionPool& TransactionPool();

  virtual MonoDelta default_client_timeout() = 0;
  virtual const std::string& permanent_uuid() const = 0;
  virtual void SetupAsyncClientInit(client::AsyncClientInitialiser* async_client_init) = 0;

  virtual client::LocalTabletFilter CreateLocalTabletFilter() = 0;

  const std::shared_future<client::YBClient*>& client_future() const;

  tserver::TServerSharedData& shared_object();

  Status Init() override;

  Status Start() override;

  void Shutdown() override;

 protected:
  void EnsureTransactionPoolCreated();

  // Shared memory owned by the tablet server.
  std::unique_ptr<tserver::TServerSharedObject> shared_object_;

  std::unique_ptr<client::AsyncClientInitialiser> async_client_init_;

  std::atomic<client::TransactionPool*> transaction_pool_{nullptr};
  std::atomic<client::TransactionManager*> transaction_manager_{nullptr};
  std::mutex transaction_pool_mutex_;
  std::unique_ptr<client::TransactionManager> transaction_manager_holder_;
  std::unique_ptr<client::TransactionPool> transaction_pool_holder_;
};

}  // namespace tserver
}  // namespace yb
