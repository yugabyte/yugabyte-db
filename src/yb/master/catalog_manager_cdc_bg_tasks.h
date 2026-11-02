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

#pragma once

#include <memory>

#include "yb/master/leader_epoch.h"
#include "yb/master/master_fwd.h"
#include "yb/util/status_fwd.h"

namespace yb {

class BackgroundTask;

namespace master {

class CatalogManager;

class CatalogManagerCdcBgTasks final {
 public:
  explicit CatalogManagerCdcBgTasks(Master* master);
  ~CatalogManagerCdcBgTasks();

  Status Init();
  void Shutdown();

 private:
  void Run();
  void RunOnceAsLeader(const LeaderEpoch& epoch);

  CatalogManager* catalog_manager_;
  std::unique_ptr<BackgroundTask> task_;
};

}  // namespace master
}  // namespace yb
