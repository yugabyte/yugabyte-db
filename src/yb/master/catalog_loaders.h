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

namespace yb {
namespace master {

// TODO: deduplicate the boilerplate below.

class TableLoader : public Visitor<PersistentTableInfo> {
 public:
  explicit TableLoader(CatalogManager* catalog_manager) : catalog_manager_(catalog_manager) {}

  CHECKED_STATUS Visit(const TableId& table_id, const SysTablesEntryPB& metadata) override;

 private:
  CatalogManager *catalog_manager_;

  DISALLOW_COPY_AND_ASSIGN(TableLoader);
};

class TabletLoader : public Visitor<PersistentTabletInfo> {
 public:
  explicit TabletLoader(CatalogManager* catalog_manager) : catalog_manager_(catalog_manager) {}

  CHECKED_STATUS Visit(const TabletId& tablet_id, const SysTabletsEntryPB& metadata) override;

 private:
  CatalogManager *catalog_manager_;

  DISALLOW_COPY_AND_ASSIGN(TabletLoader);
};

class NamespaceLoader : public Visitor<PersistentNamespaceInfo> {
 public:
  explicit NamespaceLoader(CatalogManager* catalog_manager) : catalog_manager_(catalog_manager) {}

  CHECKED_STATUS Visit(const NamespaceId& ns_id, const SysNamespaceEntryPB& metadata) override;

 private:
  CatalogManager *catalog_manager_;

  DISALLOW_COPY_AND_ASSIGN(NamespaceLoader);
};

class UDTypeLoader : public Visitor<PersistentUDTypeInfo> {
 public:
  explicit UDTypeLoader(CatalogManager* catalog_manager) : catalog_manager_(catalog_manager) {}

  CHECKED_STATUS Visit(const UDTypeId& udtype_id, const SysUDTypeEntryPB& metadata) override;

 private:
  CatalogManager *catalog_manager_;

  DISALLOW_COPY_AND_ASSIGN(UDTypeLoader);
};

class ClusterConfigLoader : public Visitor<PersistentClusterConfigInfo> {
 public:
  explicit ClusterConfigLoader(CatalogManager* catalog_manager)
      : catalog_manager_(catalog_manager) {}

  CHECKED_STATUS Visit(
      const std::string& unused_id, const SysClusterConfigEntryPB& metadata) override;

 private:
  CatalogManager* catalog_manager_;

  DISALLOW_COPY_AND_ASSIGN(ClusterConfigLoader);
};

class RedisConfigLoader : public Visitor<PersistentRedisConfigInfo> {
 public:
  explicit RedisConfigLoader(CatalogManager* catalog_manager)
      : catalog_manager_(catalog_manager) {}

  CHECKED_STATUS Visit(const std::string& key, const SysRedisConfigEntryPB& metadata) override;

 private:
  CatalogManager* catalog_manager_;

  DISALLOW_COPY_AND_ASSIGN(RedisConfigLoader);
};

class RoleLoader : public Visitor<PersistentRoleInfo> {
 public:
  explicit RoleLoader(CatalogManager* catalog_manager) : catalog_manager_(catalog_manager) {}

  CHECKED_STATUS Visit(const RoleName& role_name, const SysRoleEntryPB& metadata) override;

 private:
  CatalogManager *catalog_manager_;

  DISALLOW_COPY_AND_ASSIGN(RoleLoader);
};

class SysConfigLoader : public Visitor<PersistentSysConfigInfo> {
 public:
  explicit SysConfigLoader(CatalogManager* catalog_manager) : catalog_manager_(catalog_manager) {}

  CHECKED_STATUS Visit(const string& config_type, const SysConfigEntryPB& metadata) override;

 private:
  CatalogManager *catalog_manager_;

  DISALLOW_COPY_AND_ASSIGN(SysConfigLoader);
};

}  // namespace master
}  // namespace yb

#endif  // YB_MASTER_CATALOG_LOADERS_H
