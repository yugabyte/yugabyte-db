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

#pragma once

#include <future>

#include "yb/client/client_fwd.h"

#include "yb/server/server_base_options.h"

#include "yb/server/server_fwd.h"
#include "yb/util/atomic.h"

namespace yb {
class Thread;

namespace client {

YB_STRONGLY_TYPED_BOOL(AutoStart);

class AsyncClientInitialiser {
 public:
  AsyncClientInitialiser(
      const std::string& client_name, MonoDelta default_timeout, const std::string& tserver_uuid,
      const server::ServerBaseOptions* opts, scoped_refptr<MetricEntity> metric_entity,
      const std::shared_ptr<MemTracker>& parent_mem_tracker,
      rpc::Messenger* messenger);

  ~AsyncClientInitialiser();

  void Shutdown() { stopping_ = true; }

  void Start(const server::ClockPtr& clock = nullptr);

  YBClient* client() const;

  YBClientBuilder& builder() {
    return *client_builder_;
  }

  const std::shared_future<client::YBClient*>& get_client_future() const {
    return client_future_;
  }

  void AddPostCreateHook(std::function<void(client::YBClient*)> functor) {
    post_create_hooks_.push_back(std::move(functor));
  }

 private:
  void InitClient(const server::ClockPtr& clock);

  std::unique_ptr<YBClientBuilder> client_builder_;
  rpc::Messenger* messenger_ = nullptr;
  std::promise<client::YBClient*> client_promise_;
  mutable std::shared_future<client::YBClient*> client_future_;
  AtomicUniquePtr<client::YBClient> client_holder_;
  std::vector<std::function<void(client::YBClient*)>> post_create_hooks_;

  scoped_refptr<Thread> init_client_thread_;
  std::atomic<bool> stopping_ = {false};
};

}  // namespace client
}  // namespace yb
