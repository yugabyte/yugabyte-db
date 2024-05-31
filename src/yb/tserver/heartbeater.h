// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.
//
// The following only applies to changes made to this file as part of YugaByte development.
//
// Portions Copyright (c) YugaByte, Inc.
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

#include <memory>

#include "yb/server/server_base_options.h"

#include "yb/master/master_heartbeat.fwd.h"
#include "yb/tserver/tserver_fwd.h"
#include "yb/util/status_fwd.h"

namespace yb {
namespace tserver {

// Interface data providers to be used for filling data into heartbeat request.
// Data provider could fill in data into TSHeartbeatRequestPB that will be send by Heartbeater
// after that.
class HeartbeatDataProvider {
 public:
  explicit HeartbeatDataProvider(TabletServer* server) : server_(*CHECK_NOTNULL(server)) {}
  virtual ~HeartbeatDataProvider() {}

  // Add data to heartbeat, provider could skip and do nothing if is it too early for example for
  // periodical provider.
  // Called on every heartbeat from Heartbeater::Thread::TryHeartbeat.
  virtual void AddData(
      const master::TSHeartbeatResponsePB& last_resp, master::TSHeartbeatRequestPB* req) = 0;

  const std::string& LogPrefix() const;

  TabletServer& server() { return server_; }

 private:
  TabletServer& server_;
};

// Component of the Tablet Server which is responsible for heartbeating to the
// leader master.
//
// TODO: send heartbeats to non-leader masters.
class Heartbeater {
 public:
  Heartbeater(
      const TabletServerOptions& options, TabletServer* server,
      std::vector<std::unique_ptr<HeartbeatDataProvider>>&& data_providers);
  Heartbeater(const Heartbeater& other) = delete;
  void operator=(const Heartbeater& other) = delete;

  Status Start();
  Status Stop();

  // Trigger a heartbeat as soon as possible, even if the normal
  // heartbeat interval has not expired.
  void TriggerASAP();

  void set_master_addresses(server::MasterAddressesPtr master_addresses);
  std::string get_leader_master_hostport();

  ~Heartbeater();

 private:
  class Thread;
  std::unique_ptr<Thread> thread_;
};

class PeriodicalHeartbeatDataProvider : public HeartbeatDataProvider {
 public:
  explicit PeriodicalHeartbeatDataProvider(TabletServer* server) : HeartbeatDataProvider(server) {}

  void AddData(
      const master::TSHeartbeatResponsePB& last_resp, master::TSHeartbeatRequestPB* req) override;

  CoarseTimePoint prev_run_time() const { return prev_run_time_; }

 private:
  virtual void DoAddData(bool needs_full_tablet_report, master::TSHeartbeatRequestPB* req) = 0;
  virtual MonoDelta Period() const = 0;

  CoarseTimePoint prev_run_time_;
  bool needs_full_tablet_report_ = false;
};

} // namespace tserver
} // namespace yb
