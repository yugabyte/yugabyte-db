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

#ifndef YB_MASTER_CATALOG_MANAGER_BG_TASKS_H
#define YB_MASTER_CATALOG_MANAGER_BG_TASKS_H

#include <atomic>
#include <unordered_set>

#include "yb/common/entity_ids_types.h"
#include "yb/util/status_fwd.h"
#include "yb/util/mutex.h"
#include "yb/util/condition_variable.h"
#include "yb/gutil/ref_counted.h"

namespace yb {

class Thread;

namespace master {

class CatalogManager;

namespace enterprise {
class CatalogManager;
}

class CatalogManagerBgTasks final {
 public:
  explicit CatalogManagerBgTasks(CatalogManager *catalog_manager);

  ~CatalogManagerBgTasks() {}

  Status Init();
  void Shutdown();

  void Wake();
  void Wait(int msec);
  void WakeIfHasPendingUpdates();

 private:
  void TryResumeBackfillForTables(std::unordered_set<TableId>* tables);
  void Run();

 private:
  std::atomic<bool> closing_;
  bool pending_updates_;
  mutable Mutex lock_;
  ConditionVariable cond_;
  scoped_refptr<yb::Thread> thread_;
  enterprise::CatalogManager *catalog_manager_;
  bool was_leader_ = false;
};

}  // namespace master
}  // namespace yb

#endif  // YB_MASTER_CATALOG_MANAGER_BG_TASKS_H
