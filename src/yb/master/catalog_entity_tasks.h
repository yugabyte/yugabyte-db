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

#include "yb/master/master_fwd.h"
#include "yb/master/multi_step_monitored_task.h"

namespace yb {

namespace master {

class MultiStepTableTaskBase : public MultiStepCatalogEntityTask {
 protected:
  MultiStepTableTaskBase(
      CatalogManager& catalog_manager, ThreadPool& async_task_pool, rpc::Messenger& messenger,
      TableInfoPtr table_info, const LeaderEpoch& epoch);

  CatalogManager& catalog_manager_;
  const TableInfoPtr table_info_;
};

class MultiStepNamespaceTaskBase : public MultiStepCatalogEntityTask {
 protected:
  MultiStepNamespaceTaskBase(
      CatalogManager& catalog_manager, ThreadPool& async_task_pool, rpc::Messenger& messenger,
      NamespaceInfo& namespace_info, const LeaderEpoch& epoch);

  CatalogManager& catalog_manager_;
  NamespaceInfo& namespace_info_;
};

}  // namespace master
}  // namespace yb
