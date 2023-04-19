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
//

#pragma once

#include <boost/multi_index/global_fun.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/mem_fun.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index_container.hpp>

#include "yb/master/catalog_entity_info.h"
#include "yb/master/master_fwd.h"
#include "yb/util/version_tracker.h"

namespace yb {
namespace master {

class TableIndex {
 public:
  // TODO(zdrudi): add additional indices.
  // the catalog manager has a pattern of looping through:
  //     * transaction tables (done through an additional set functioning as an index)
  //     * non system tables
  class PrimaryTableTag;
  // Use a boost::multi_index_container to speed up common operations on tables by adding additional
  // indices. Indices:
  //     Primary index on TableId for point lookups.
  //     Secondary index on IsColocatedUserTable to speed up the tablet split manager and load
  //     balancer for clusters with many colocated tables.
  using Tables = boost::multi_index_container<
    TableInfoPtr,
    boost::multi_index::indexed_by<
      boost::multi_index::hashed_unique<
        boost::multi_index::const_mem_fun<TableInfo, const TableId&, &TableInfo::id>>,
      boost::multi_index::hashed_non_unique<
        boost::multi_index::tag<PrimaryTableTag>,
        boost::multi_index::const_mem_fun<TableInfo, bool, &TableInfo::IsColocatedUserTable>>>>;

  using TablesRange = boost::iterator_range<Tables::nth_index<1>::type::iterator>;

  TableIndex() = default;
  ~TableIndex() = default;
  scoped_refptr<TableInfo> FindTableOrNull(const TableId& id) const;

  TablesRange GetAllTables() const;
  TablesRange GetPrimaryTables() const;

  void AddOrReplace(const scoped_refptr<TableInfo>& table);

  void Clear();

  size_t Erase(const TableId& id);

  size_t Size() const;

 private:
  class Impl;
  Tables tables_;
};

}  // namespace master
}  // namespace yb
