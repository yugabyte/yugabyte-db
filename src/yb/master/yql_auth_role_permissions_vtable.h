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

// VTable implementation of system_auth.role_permissions
class YQLAuthRolePermissionsVTable : public YQLVirtualTable {
 public:
  explicit YQLAuthRolePermissionsVTable(const TableName& table_name,
                                        const NamespaceName& namespace_name,
                                        yb::master::Master * const master);

  Result<VTableDataPtr> RetrieveData(const QLReadRequestPB& request) const override;

 protected:
  Schema CreateSchema() const;

 private:
  static constexpr const char* const kRole = "role";
  static constexpr const char* const kResource = "resource";
  static constexpr const char* const kPermissions = "permissions";

};

}  // namespace master
}  // namespace yb
