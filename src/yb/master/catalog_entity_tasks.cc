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

#include "yb/master/catalog_entity_tasks.h"

#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager.h"

namespace yb::master {

MultiStepTableTaskBase::MultiStepTableTaskBase(
    CatalogManager& catalog_manager, ThreadPool& async_task_pool, rpc::Messenger& messenger,
    TableInfoPtr table_info, const LeaderEpoch& epoch)
    : MultiStepCatalogEntityTask(
          catalog_manager.GetValidateEpochFunc(), async_task_pool, messenger, *table_info, epoch),
      catalog_manager_(catalog_manager),
      table_info_(std::move(table_info)) {}

MultiStepNamespaceTaskBase::MultiStepNamespaceTaskBase(
    CatalogManager& catalog_manager, ThreadPool& async_task_pool, rpc::Messenger& messenger,
    NamespaceInfo& namespace_info, const LeaderEpoch& epoch)
    : MultiStepCatalogEntityTask(
          catalog_manager.GetValidateEpochFunc(), async_task_pool, messenger, namespace_info,
          epoch),
      catalog_manager_(catalog_manager),
      namespace_info_(namespace_info) {}

}  // namespace yb::master
