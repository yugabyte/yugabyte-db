//--------------------------------------------------------------------------------------------------
// Copyright (c) YugaByte, Inc.
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
//--------------------------------------------------------------------------------------------------
#pragma once

#include <set>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <boost/bimap.hpp>

#include "yb/client/client_fwd.h"

#include "yb/master/master_fwd.h"

namespace yb {
namespace master {

// Serves as a metadata cache of YSQL tablegroups, used for quick and convenient lookups.
// Tablegroup IDs are unique cross-database.
// Note that this is only used for tracking tablegroup-related info, no operations propagate to
// catalog manager changes.
// This class is NOT thread-safe, access to it should be guarded by CatalogManager::mutex_.
class YsqlTablegroupManager {
 public:
  class TablegroupInfo;

  // Return a tablegroup helper object, or nullptr if it doesn't exist.
  TablegroupInfo* Find(const TablegroupId& tablegroup_id) const;

  // Return a tablegroup helper object, or nullptr if it doesn't exist.
  TablegroupInfo* FindByTable(const TableId& table_id) const;

  Result<std::unordered_set<TablegroupInfo*>> ListForDatabase(const NamespaceId& database_id) const;

  Result<TablegroupInfo*> Add(const NamespaceId& database_id,
                              const TablegroupId& tablegroup_id,
                              const TabletInfoPtr tablet);

  Status Remove(const TablegroupId& tablegroup_id);

 private:
  template<typename K, typename V>
  using Map = std::unordered_map<K, V>;

  Map<TablegroupId, std::unique_ptr<TablegroupInfo>> tablegroup_map_;

  Map<NamespaceId, std::unordered_set<TablegroupId>> database_tablegroup_ids_map_;

  // This one is modified from inside TablegroupInfo.
  Map<TableId, TablegroupId> table_tablegroup_ids_map_;

 public:
  // Information about a particular tablegroup.
  // This class can modify the tablegroup manager, access to it should be guarded by the same mutex.
  class TablegroupInfo {
   public:
    const TablegroupId& id() const { return tablegroup_id_; }
    const NamespaceId& database_id() const { return database_id_; }
    const TabletInfoPtr& tablet() const { return tablet_; }

    Status AddChildTable(const TableId& table_id, ColocationId colocation_id);

    Status RemoveChildTable(const TableId& table_id);

    bool IsEmpty() const;

    bool HasChildTable(ColocationId colocation_id) const;

    std::unordered_set<TableId> ChildTableIds() const;

    std::string ToString() const;

   private:
    friend class YsqlTablegroupManager;
    friend std::unique_ptr<TablegroupInfo>::deleter_type;

    typedef boost::bimap<TableId, ColocationId> TableMap;

    YsqlTablegroupManager* mgr_;

    const TablegroupId tablegroup_id_;
    const NamespaceId database_id_;
    const TabletInfoPtr tablet_;

    TablegroupInfo(YsqlTablegroupManager* mgr,
                   const TablegroupId& tablegroup_id,
                   const NamespaceId& database_id,
                   const TabletInfoPtr tablet);

    ~TablegroupInfo() = default;

    TableMap table_map_;

    DISALLOW_COPY_AND_ASSIGN(TablegroupInfo);
  };
};

} // namespace master
} // namespace yb
