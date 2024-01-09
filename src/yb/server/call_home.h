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

#include <memory>

#include "yb/rpc/io_thread_pool.h"

#include "yb/server/server_base.h"
#include "yb/util/status_fwd.h"
#include "yb/util/curl_util.h"

namespace yb {

enum class CollectionLevel { ALL, LOW, MEDIUM, HIGH };

class Collector {
 public:
  explicit Collector(server::RpcAndWebServerBase* server);

  virtual ~Collector();

  bool Run(CollectionLevel collection_level);
  virtual void Collect(CollectionLevel collection_level) = 0;

  const std::string& as_json() { return json_; }

  virtual std::string collector_name() = 0;

  virtual CollectionLevel collection_level() = 0;

 protected:
  void AppendPairToJson(const std::string& key, const std::string& value, std::string* out);

  server::RpcAndWebServerBase* server_;
  std::string json_;
};

class CallHome {
 public:
  explicit CallHome(server::RpcAndWebServerBase* server);
  virtual ~CallHome();

  void DoCallHome();
  void ScheduleCallHome(int delay_seconds = 10);
  void Shutdown();

 protected:
  template <class ServerType, class CallHomeType>
  friend void TestCallHome(
      const std::string& webserver_dir, const std::set<std::string>& additional_collections,
      ServerType* server);

  template <class ServerType, class CallHomeType>
  friend void TestGFlagsCallHome(ServerType* server);

  template <typename T>
  void AddCollector() {
    collectors_.emplace_back(std::make_unique<T>(server_));
  }

  virtual bool SkipCallHome() = 0;

  std::string BuildJson();

  void BuildJsonAndSend();

  Status GetAddr();

  CollectionLevel GetCollectionLevel();

  void SendData(const std::string& payload);
  server::RpcAndWebServerBase* server_ = nullptr;
  yb::rpc::IoThreadPool pool_;
  std::unique_ptr<yb::rpc::Scheduler> scheduler_;
  EasyCurl curl_;
  std::vector<std::unique_ptr<Collector>> collectors_;
  bool is_shutdown_ = false;
};

std::string GetCurrentUser();

}  // namespace yb
