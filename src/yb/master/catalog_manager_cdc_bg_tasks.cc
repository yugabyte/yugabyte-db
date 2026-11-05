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
// The following only applies to changes made to this file as part of YugabyteDB development.
//
// Portions Copyright (c) YugabyteDB, Inc.
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

#include "yb/master/catalog_manager_cdc_bg_tasks.h"

#include "yb/master/catalog_manager.h"
#include "yb/master/cdcsdk_manager.h"
#include "yb/master/master.h"
#include "yb/master/scoped_leader_shared_lock.h"

#include "yb/util/background_task.h"
#include "yb/util/debug/long_operation_tracker.h"
#include "yb/util/flags.h"
#include "yb/util/status_log.h"

using namespace std::literals;

DECLARE_bool(TEST_cdcsdk_disable_deleted_stream_cleanup);
DECLARE_bool(TEST_cdcsdk_disable_drop_table_cleanup);
DECLARE_int32(master_bg_task_long_operation_warning_ms);

DEFINE_RUNTIME_int32(catalog_manager_cdc_bg_task_wait_ms, 1000,
    "Amount of time the catalog manager CDC background task thread waits between runs");

namespace yb::master {

CatalogManagerCdcBgTasks::CatalogManagerCdcBgTasks(Master* master)
    : catalog_manager_(master->catalog_manager_impl()) {}

CatalogManagerCdcBgTasks::~CatalogManagerCdcBgTasks() = default;

Status CatalogManagerCdcBgTasks::Init() {
  task_ = std::make_unique<BackgroundTask>(
      [this] { Run(); },
      "catalog manager",
      "cdc-bgtasks",
      std::chrono::milliseconds(FLAGS_catalog_manager_cdc_bg_task_wait_ms));
  Status s = task_->Init();
  if (!s.ok()) {
    task_.reset();
    return s;
  }
  s = task_->Wake();
  if (!s.ok()) {
    task_->Shutdown();
    task_.reset();
    return s;
  }
  return Status::OK();
}

void CatalogManagerCdcBgTasks::Shutdown() {
  if (!task_) {
    return;
  }
  task_->Shutdown();
  task_.reset();
}

void CatalogManagerCdcBgTasks::RunOnceAsLeader(const LeaderEpoch& epoch) {
  LongOperationTracker long_operation_tracker(
      "CatalogManagerCdcBgTasks::RunOnceAsLeader",
      FLAGS_master_bg_task_long_operation_warning_ms * 1ms);
  if (!FLAGS_TEST_cdcsdk_disable_deleted_stream_cleanup) {
    WARN_NOT_OK(
        catalog_manager_->CleanUpDeletedCDCSDKStreams(epoch),
        "Failed cleaning deleted CDCSDK streams");
  }

  if (!FLAGS_TEST_cdcsdk_disable_drop_table_cleanup) {
    WARN_NOT_OK(
        catalog_manager_->CleanUpCDCSDKStreamsMetadata(epoch),
        "Failed cleanup CDCSDK streams metadata");
  }

  catalog_manager_->cdcsdk_manager_->RunBgTasks(epoch);
}

void CatalogManagerCdcBgTasks::Run() {
  task_->SetInterval(std::chrono::milliseconds(FLAGS_catalog_manager_cdc_bg_task_wait_ms));

  SCOPED_LEADER_SHARED_LOCK(l, catalog_manager_);
  if (!l.catalog_status().ok()) {
    LOG(WARNING) << "CDC cleanup background task thread going to sleep: "
                 << l.catalog_status().ToString();
  } else if (l.leader_status().ok()) {
    RunOnceAsLeader(l.epoch());
  }
}

}  // namespace yb::master
