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

#include "yb/client/client.h"
#include "yb/client/transaction_manager.h"
#include "yb/client/transaction_pool.h"

#include "yb/server/async_client_initializer.h"
#include "yb/server/clock.h"

#include "yb/tserver/tserver_util_fwd.h"
#include "yb/tserver/tserver_shared_mem.h"

#include "yb/util/init.h"
#include "yb/util/mem_tracker.h"
#include "yb/util/scope_exit.h"
#include "yb/util/shared_mem.h"
#include "yb/util/status_log.h"

namespace yb {
namespace tserver {

DbServerBase::DbServerBase(
    std::string name, const server::ServerBaseOptions& options,
    const std::string& metrics_namespace, std::shared_ptr<MemTracker> mem_tracker)
    : RpcAndWebServerBase(std::move(name), options, metrics_namespace, std::move(mem_tracker)),
      shared_object_(new tserver::TServerSharedObject(
          CHECK_RESULT(tserver::TServerSharedObject::Create()))) {
  MemTracker::GetRootTracker()->LogMemoryLimits();
}

DbServerBase::~DbServerBase() {
}

Status DbServerBase::Init() {
  RETURN_NOT_OK(RpcAndWebServerBase::Init());

  async_client_init_ = std::make_unique<client::AsyncClientInitializer>(
      "server_client", default_client_timeout(), permanent_uuid(), &options(), metric_entity(),
      mem_tracker(), messenger());
  SetupAsyncClientInit(async_client_init_.get());

  return Status::OK();
}

Status DbServerBase::Start() {
  RETURN_NOT_OK(RpcAndWebServerBase::Start());
  async_client_init_->Start(clock_);

  std::string host_name;
  RETURN_NOT_OK(GetHostname(&host_name));

  std::string node_info = Format(
      "Node information: { hostname: '$0', rpc_ip: '$1', webserver_ip: '$2', uuid: '$3' }",
      host_name, yb::ToString(first_rpc_address().address()),
      yb::ToString(VERIFY_RESULT(first_http_address()).address()), fs_manager_->uuid());
  LOG(INFO) << node_info;

  SetGLogHeader("\n" + node_info);

  return Status::OK();
}

void DbServerBase::Shutdown() {
  client::TransactionManager* txn_manager;
  txn_manager = transaction_manager_.load();
  if (txn_manager) {
    txn_manager->Shutdown();
  }
  async_client_init_->Shutdown();
}

const std::shared_future<client::YBClient*>& DbServerBase::client_future() const {
  return async_client_init_->get_client_future();
}

client::TransactionManager& DbServerBase::TransactionManager() {
  auto result = transaction_manager_.load();
  if (result) {
    return *result;
  }
  EnsureTransactionPoolCreated();
  return *transaction_manager_.load();
}

client::TransactionPool& DbServerBase::TransactionPool() {
  auto result = transaction_pool_.load(std::memory_order_acquire);
  if (result) {
    return *result;
  }
  EnsureTransactionPoolCreated();
  return *transaction_pool_.load();
}

void DbServerBase::EnsureTransactionPoolCreated() {
  std::lock_guard lock(transaction_pool_mutex_);
  if (transaction_pool_holder_) {
    return;
  }
  transaction_manager_holder_ = std::make_unique<client::TransactionManager>(
      async_client_init_->get_client_future().get(), clock(), CreateLocalTabletFilter());
  transaction_manager_.store(transaction_manager_holder_.get(), std::memory_order_release);
  transaction_pool_holder_ = std::make_unique<client::TransactionPool>(
      transaction_manager_holder_.get(), metric_entity().get());
  transaction_pool_.store(transaction_pool_holder_.get(), std::memory_order_release);
}

tserver::TServerSharedData& DbServerBase::shared_object() {
  return **shared_object_;
}

int DbServerBase::GetSharedMemoryFd() {
  return shared_object_->GetFd();
}

void DbServerBase::WriteMainMetaCacheAsJson(JsonWriter* writer) {
  writer->String("MainMetaCache");
  auto local_client_future = client_future();
  auto local_client = local_client_future.get();
  local_client->AddMetaCacheInfo(writer);
}

}  // namespace tserver
}  // namespace yb
