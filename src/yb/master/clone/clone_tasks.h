// Copyright (c) YugabyteDB, Inc.
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

#include "yb/common/hybrid_time.h"

#include "yb/master/async_rpc_tasks_base.h"

#include "yb/tserver/tserver_admin.pb.h"
#include "yb/tserver/tserver_service.pb.h"

namespace yb {
namespace master {

class AsyncCloneTablet: public AsyncTabletLeaderTask {
 public:
  AsyncCloneTablet(
      Master* master,
      ThreadPool* callback_pool,
      const TabletInfoPtr& tablet,
      LeaderEpoch epoch,
      tablet::CloneTabletRequestPB req);

  server::MonitoredTaskType type() const override {
    return server::MonitoredTaskType::kCloneTablet;
  }

  std::string type_name() const override { return "Clone Tablet"; }

  std::string description() const override;

 private:
  void HandleResponse(int attempt) override;
  bool SendRequest(int attempt) override;

  tablet::CloneTabletRequestPB req_;
  tserver::CloneTabletResponsePB resp_;
};

class AsyncClonePgSchema : public RetrySpecificTSRpcTask {
 public:
  using ClonePgSchemaCallbackType = std::function<Status(Status)>;
  AsyncClonePgSchema(
      Master* master, ThreadPool* callback_pool, const std::string& permanent_uuid,
      const std::string& source_db_name, const std::string& target_db_name, HybridTime restore_time,
      const std::string& source_owner, const std::string& target_owner,
      ClonePgSchemaCallbackType callback, MonoTime deadline);

  server::MonitoredTaskType type() const override {
    return server::MonitoredTaskType::kClonePgSchema;
  }

  std::string type_name() const override { return "Clone PG Schema Objects"; }

  std::string description() const override;

 protected:
  void HandleResponse(int attempt) override;
  void Finished(const Status& status) override;
  bool SendRequest(int attempt) override;
  MonoTime ComputeDeadline() const override;

  // Not associated with a tablet.
  TabletId tablet_id() const override { return TabletId(); }

 private:
  std::string source_db_name_;
  std::string target_db_name_;
  std::string source_owner_;
  std::string target_owner_;
  HybridTime restore_ht_;
  tserver::ClonePgSchemaResponsePB resp_;
  ClonePgSchemaCallbackType callback_;
};

class AsyncClearMetacache : public RetrySpecificTSRpcTask {
 public:
  using ClearMetacacheCallbackType = std::function<Status()>;
  AsyncClearMetacache(
      Master* master, ThreadPool* callback_pool, const std::string& permanent_uuid,
      const std::string& namespace_id, ClearMetacacheCallbackType callback);

  server::MonitoredTaskType type() const override {
    return server::MonitoredTaskType::kClearMetaCache;
  }

  std::string type_name() const override { return "Clear all meta-caches of a tserver"; }

  std::string description() const override;

 protected:
  void HandleResponse(int attempt) override;
  bool SendRequest(int attempt) override;
  // Not associated with a tablet.
  TabletId tablet_id() const override { return TabletId(); }

 private:
  std::string namespace_id;
  tserver::ClearMetacacheResponsePB resp_;
  ClearMetacacheCallbackType callback_;
};

class AsyncEnableDbConns : public RetrySpecificTSRpcTask {
 public:
  using EnableDbConnsCallbackType = std::function<Status(Status)>;
  AsyncEnableDbConns(
      Master* master, ThreadPool* callback_pool, const std::string& permanent_uuid,
      const std::string& target_db_name, EnableDbConnsCallbackType callback);

  server::MonitoredTaskType type() const override {
    return server::MonitoredTaskType::kEnableDbConns;
  }

  std::string type_name() const override { return "Enable DB connections"; }

  std::string description() const override;

 protected:
  void HandleResponse(int attempt) override;
  bool SendRequest(int attempt) override;
  // Not associated with a tablet.
  TabletId tablet_id() const override { return TabletId(); }

 private:
  std::string target_db_name_;
  tserver::EnableDbConnsResponsePB resp_;
  EnableDbConnsCallbackType callback_;
};



} // namespace master
} // namespace yb
