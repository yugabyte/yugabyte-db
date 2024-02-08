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

#include "yb/master/table_index.h"

#include "yb/gutil/map-util.h"
#include "yb/master/catalog_entity_info.h"

namespace yb {
namespace master {

scoped_refptr<TableInfo> TableIndex::FindTableOrNull(const TableId& id) const {
  auto result = tables_.find(id);
  if (result == tables_.end()) {
    return nullptr;
  }
  return *result;
}

TableIndex::TablesRange TableIndex::GetAllTables() const {
  return tables_.get<ColocatedUserTableTag>();
}

TableIndex::TablesRange TableIndex::GetPrimaryTables() const {
  return tables_.get<ColocatedUserTableTag>().equal_range(false);
}

void TableIndex::Clear() {
  tables_.clear();
}

void TableIndex::AddOrReplace(const scoped_refptr<TableInfo>& table) {
  auto [pos, insertion_successful] = tables_.insert(table);
  if (!insertion_successful) {
    std::string first_id = (*pos)->id();
    auto replace_successful = tables_.replace(pos, table);
    if (!replace_successful) {
      LOG(ERROR) << Format(
          "Multiple tables prevented inserting a new table with id $0. First id was $1",
          table->id(), first_id);
    }
  }
}

size_t TableIndex::Erase(const TableId& id) {
  return tables_.erase(id);
}

size_t TableIndex::Size() const {
  return tables_.size();
}

}  // namespace master
}  // namespace yb
