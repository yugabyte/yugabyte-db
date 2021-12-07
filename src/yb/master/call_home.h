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

#ifndef YB_MASTER_CALL_HOME_H
#define YB_MASTER_CALL_HOME_H

#include <memory>

#include "yb/master/master_fwd.h"

#include "yb/rpc/io_thread_pool.h"

#include "yb/tserver/tablet_server.h"

#include "yb/util/status_fwd.h"
#include "yb/util/curl_util.h"

namespace yb {

namespace master {
class MasterTest_TestCallHome_Test;
}

enum class CollectionLevel {
  ALL,
  LOW,
  MEDIUM,
  HIGH
};
enum class ServerType {
  ALL,
  MASTER,
  TSERVER
};

typedef std::function<std::string()> CollectorFunc;

class Collector {
 public:
  virtual ~Collector();

  // Returns true if the collector ran.
  virtual bool Run(CollectionLevel collection_level) = 0;
  virtual void Collect(CollectionLevel collection_level) = 0;

  virtual const std::string& as_json() = 0;
  virtual ServerType server_type() = 0;

  virtual std::string collector_name() = 0;

  virtual CollectionLevel collection_level() = 0;
  virtual ServerType collector_type() = 0;
};

class CallHome {
 public:
  CallHome(server::RpcAndWebServerBase* server, ServerType server_type);
  ~CallHome();

  void DoCallHome();
  void ScheduleCallHome(int delay_seconds = 10);

 private:
  FRIEND_TEST(master::MasterTest, TestCallHome);

  template <typename T>
  void AddCollector();

  std::string BuildJson();

  void BuildJsonAndSend();

  CHECKED_STATUS GetAddr();
  master::Master* master();
  tserver::TabletServer* tserver();

  CollectionLevel GetCollectionLevel();

  void SendData(const std::string& payload);
  server::RpcAndWebServerBase* server_ = nullptr;
  yb::rpc::IoThreadPool pool_;
  std::unique_ptr<yb::rpc::Scheduler> scheduler_;
  EasyCurl curl_;
  ServerType server_type_;
  std::vector<std::unique_ptr<Collector>> collectors_;
};
} // namespace yb

#endif // YB_MASTER_CALL_HOME_H
