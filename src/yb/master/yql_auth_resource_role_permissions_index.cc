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

#include "yb/master/yql_auth_resource_role_permissions_index.h"

#include "yb/common/ql_type.h"
#include "yb/common/schema.h"

#include "yb/master/permissions_manager.h"

#include "yb/util/status_log.h"

namespace yb {
namespace master {

YQLAuthResourceRolePermissionsIndexVTable::YQLAuthResourceRolePermissionsIndexVTable(
        const TableName& table_name, const NamespaceName& namespace_name, Master * const master)
    : YQLVirtualTable(table_name, namespace_name, master, CreateSchema()) {
}

Result<VTableDataPtr> YQLAuthResourceRolePermissionsIndexVTable::RetrieveData(
    const QLReadRequestPB& request) const {
  auto vtable = std::make_shared<qlexpr::QLRowBlock>(schema());
  std::vector<scoped_refptr<RoleInfo>> roles;
  catalog_manager().permissions_manager()->GetAllRoles(&roles);
  for (const auto& rp : roles) {
    auto l = rp->LockForRead();
    const auto& pb = l->pb;
    for (int i = 0; i <  pb.resources_size(); i++) {
      const auto& rp = pb.resources(i);
      auto& row = vtable->Extend();
      RETURN_NOT_OK(SetColumnValue(kResource, rp.canonical_resource(), &row));
      RETURN_NOT_OK(SetColumnValue(kRole, pb.role(), &row));
    }
  }

  return vtable;
}


Schema YQLAuthResourceRolePermissionsIndexVTable::CreateSchema() const {
  SchemaBuilder builder;
  CHECK_OK(builder.AddHashKeyColumn(kResource, DataType::STRING));
  CHECK_OK(builder.AddColumn(kRole, QLType::Create(DataType::STRING)));
  return builder.Build();
}

}  // namespace master
}  // namespace yb
