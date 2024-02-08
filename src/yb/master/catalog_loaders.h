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

#include <type_traits>

#include <boost/preprocessor/cat.hpp>

#include "yb/master/master_fwd.h"
#include "yb/master/catalog_manager.h"
#include "yb/master/permissions_manager.h"
#include "yb/master/sys_catalog.h"

namespace yb {
namespace master {

struct SysCatalogLoadingState {
  std::unordered_map<TableId, std::vector<TableId>> parent_to_child_tables;
  std::vector<std::pair<std::function<void()>, std::string>> post_load_tasks;
  const LeaderEpoch epoch;

  void AddPostLoadTask(std::function<void()>&& func, std::string&& msg) {
    post_load_tasks.push_back({std::move(func), std::move(msg)});
  }

  void Reset() {
    parent_to_child_tables.clear();
    post_load_tasks.clear();
  }
};

#define DECLARE_LOADER_CLASS(name, key_type, entry_pb_name, mutex) \
  class BOOST_PP_CAT(name, Loader) : \
      public Visitor<BOOST_PP_CAT(BOOST_PP_CAT(Persistent, name), Info)> { \
  public: \
    explicit BOOST_PP_CAT(name, Loader)( \
                                         CatalogManager* catalog_manager, \
                                         SysCatalogLoadingState* state) \
      : catalog_manager_(catalog_manager), state_(state) {} \
    \
  private: \
    Status Visit( \
        const key_type& key, \
        const entry_pb_name& metadata) REQUIRES(mutex) override; \
    \
    CatalogManager *catalog_manager_; \
    \
    SysCatalogLoadingState* state_; \
    \
    DISALLOW_COPY_AND_ASSIGN(BOOST_PP_CAT(name, Loader)); \
  };

// We have two naming schemes for Sys...EntryPB classes (plural vs. singluar), hence we need the
// "entry_pb_suffix" argument to the macro.
//
// SysTablesEntryPB
// SysTabletsEntryPB
//
// vs.
//
// SysNamespaceEntryPB
// SysUDTypeEntryPB
// SysClusterConfigEntryPB
// SysRedisConfigEntryPB
// SysRoleEntryPB
// SysConfigEntryPB

// These config PBs don't have associated loaders here:
//
// SysSecurityConfigEntryPB
// SysSnapshotEntryPB
// SysYSQLCatalogConfigEntryPB

DECLARE_LOADER_CLASS(Table,         TableId,     SysTablesEntryPB,        catalog_manager_->mutex_);
DECLARE_LOADER_CLASS(Tablet,        TabletId,    SysTabletsEntryPB,       catalog_manager_->mutex_);
DECLARE_LOADER_CLASS(Namespace,     NamespaceId, SysNamespaceEntryPB,     catalog_manager_->mutex_);
DECLARE_LOADER_CLASS(UDType,        UDTypeId,    SysUDTypeEntryPB,        catalog_manager_->mutex_);
DECLARE_LOADER_CLASS(ClusterConfig, std::string, SysClusterConfigEntryPB, catalog_manager_->mutex_);
DECLARE_LOADER_CLASS(RedisConfig,   std::string, SysRedisConfigEntryPB,   catalog_manager_->mutex_);
DECLARE_LOADER_CLASS(Role,       RoleName,    SysRoleEntryPB,
    catalog_manager_->permissions_manager()->mutex());
DECLARE_LOADER_CLASS(SysConfig,     std::string, SysConfigEntryPB,
    catalog_manager_->permissions_manager()->mutex());

#undef DECLARE_LOADER_CLASS

bool ShouldLoadObject(const SysNamespaceEntryPB& metadata);
bool ShouldLoadObject(const SysTablesEntryPB& pb);
bool ShouldLoadObject(const SysTabletsEntryPB& pb);

}  // namespace master
}  // namespace yb
