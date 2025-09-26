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

#include "yb/master/yql_virtual_table.h"

namespace yb {
namespace master {

// VTable implementation of system_schema.indexes.
class YQLIndexesVTable : public YQLVirtualTable {
 public:
  explicit YQLIndexesVTable(const TableName& table_name,
                            const NamespaceName& namespace_name,
                            Master * const master);
  Result<VTableDataPtr> RetrieveData(const QLReadRequestPB& request) const override;

 protected:
  Schema CreateSchema() const;
 private:
  static constexpr const char* const kKeyspaceName = "keyspace_name";
  static constexpr const char* const kTableName = "table_name";
  static constexpr const char* const kIndexName = "index_name";
  static constexpr const char* const kKind = "kind";
  static constexpr const char* const kOptions = "options";
  static constexpr const char* const kTableId = "table_id";
  static constexpr const char* const kIndexId = "index_id";
  static constexpr const char* const kTransactions = "transactions";
  static constexpr const char* const kIsUnique = "is_unique";
  static constexpr const char* const kNumTablets = "tablets";
};

}  // namespace master
}  // namespace yb
