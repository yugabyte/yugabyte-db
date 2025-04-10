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

#include "yb/util/concurrent_value.h"

#include "yb/yql/pgwrapper/pg_wrapper_context.h"

namespace yb {

class JsonWriter;

namespace tserver {

class DbServerBase : public server::RpcAndWebServerBase, public pgwrapper::PgWrapperContext {
 public:
  DbServerBase(
      std::string name, const server::ServerBaseOptions& options,
      const std::string& metrics_namespace,
      std::shared_ptr<MemTracker> mem_tracker);
  ~DbServerBase();

  client::TransactionManager& TransactionManager();

  client::TransactionPool& TransactionPool();

  virtual MonoDelta default_client_timeout() = 0;
  virtual void SetupAsyncClientInit(client::AsyncClientInitializer* async_client_init) = 0;

  virtual client::LocalTabletFilter CreateLocalTabletFilter() = 0;

  virtual void WriteServerMetaCacheAsJson(JsonWriter* writer) = 0;

  const std::shared_future<client::YBClient*>& client_future() const;

  Status StartSharedMemoryNegotiation() override;

  Status StopSharedMemoryNegotiation() override;

  Status SkipSharedMemoryNegotiation();

  int SharedMemoryNegotiationFd() override;

  ConcurrentPointerReference<TServerSharedData> shared_object() const;

  Status Init() override;

  Status Start() override;

  void Shutdown() override;

 protected:
  void EnsureTransactionPoolCreated();

  void WriteMainMetaCacheAsJson(JsonWriter* writer);

  std::unique_ptr<SharedMemoryManager> shared_mem_manager_;

  std::unique_ptr<client::AsyncClientInitializer> async_client_init_;

  std::atomic<client::TransactionPool*> transaction_pool_{nullptr};
  std::atomic<client::TransactionManager*> transaction_manager_{nullptr};
  std::mutex transaction_pool_mutex_;
  std::unique_ptr<client::TransactionManager> transaction_manager_holder_;
  std::unique_ptr<client::TransactionPool> transaction_pool_holder_;
};

}  // namespace tserver
}  // namespace yb
