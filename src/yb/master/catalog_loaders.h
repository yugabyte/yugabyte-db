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

#ifndef YB_MASTER_CATALOG_LOADERS_H
#define YB_MASTER_CATALOG_LOADERS_H

#include "yb/master/catalog_entity_info.h"
#include "yb/master/sys_catalog-internal.h"

#include <boost/preprocessor/cat.hpp>

namespace yb {
namespace master {

#define DECLARE_LOADER_CLASS(name, key_type, entry_pb_name) \
  class BOOST_PP_CAT(name, Loader) : \
      public Visitor<BOOST_PP_CAT(BOOST_PP_CAT(Persistent, name), Info)> { \
  public: \
    explicit BOOST_PP_CAT(name, Loader)( \
        CatalogManager* catalog_manager, int64_t term = OpId::kUnknownTerm) \
        : catalog_manager_(catalog_manager), term_(term) {} \
    \
  private: \
    CHECKED_STATUS Visit( \
        const key_type& key, \
        const entry_pb_name& metadata) override; \
    \
    CatalogManager *catalog_manager_; \
    \
    int64_t term_; \
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

DECLARE_LOADER_CLASS(Table,         TableId,     SysTablesEntryPB);
DECLARE_LOADER_CLASS(Tablet,        TabletId,    SysTabletsEntryPB);
DECLARE_LOADER_CLASS(Namespace,     NamespaceId, SysNamespaceEntryPB);
DECLARE_LOADER_CLASS(UDType,        UDTypeId,    SysUDTypeEntryPB);
DECLARE_LOADER_CLASS(ClusterConfig, std::string, SysClusterConfigEntryPB);
DECLARE_LOADER_CLASS(RedisConfig,   std::string, SysRedisConfigEntryPB);
DECLARE_LOADER_CLASS(Role,          RoleName,    SysRoleEntryPB);
DECLARE_LOADER_CLASS(SysConfig,     std::string, SysConfigEntryPB);

#undef DECLARE_LOADER_CLASS

}  // namespace master
}  // namespace yb

#endif  // YB_MASTER_CATALOG_LOADERS_H
