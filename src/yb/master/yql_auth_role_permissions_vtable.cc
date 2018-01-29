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

#include "yb/master/catalog_manager.h"
#include "yb/master/master_defaults.h"
#include "yb/master/yql_auth_role_permissions_vtable.h"
#include "yb/common/common.pb.h"
#include "yb/gutil/strings/substitute.h"

namespace yb {
namespace master {

YQLAuthRolePermissionsVTable::YQLAuthRolePermissionsVTable(const Master* const master)
    : YQLVirtualTable(master::kSystemAuthRolePermissionsTableName, master, CreateSchema()) {
}

Status YQLAuthRolePermissionsVTable::RetrieveData(const QLReadRequestPB& request,
                                                  std::unique_ptr<QLRowBlock>* vtable) const {
  vtable->reset(new QLRowBlock(schema_));
  std::vector<scoped_refptr<RoleInfo>> roles;
  master_->catalog_manager()->GetAllRoles(&roles);
  for (const auto& rp : roles) {
    auto l = rp->LockForRead();
    const auto& pb = l->data().pb;
    for (int i = 0; i <  pb.resources_size(); i++) {
      const auto& rp = pb.resources(i);
      QLRow& row = (*vtable)->Extend();
      RETURN_NOT_OK(SetColumnValue(kRole, pb.role(), &row));
      RETURN_NOT_OK(SetColumnValue(kResource, rp.canonical_resource(), &row));

      QLValuePB permissions;
      QLSeqValuePB* list_value = permissions.mutable_list_value();

      for (int j = 0; j < rp.permissions_size(); j++) {
        const auto& permission = rp.permissions(j);
        const char* permission_name  = RoleInfo::permissionName(permission);
        if (permission_name == nullptr) {
          return STATUS(InvalidArgument,
                        strings::Substitute("Unknown Permission $0",
                                            PermissionType_Name(permission)));
        } else {
          (*list_value->add_elems()).set_string_value(permission_name);
        }
      }
      RETURN_NOT_OK(SetColumnValue(kPermissions, permissions, &row));

    }
  }

  return Status::OK();
}


Schema YQLAuthRolePermissionsVTable::CreateSchema() const {
  SchemaBuilder builder;
  CHECK_OK(builder.AddHashKeyColumn(kRole, DataType::STRING));
  CHECK_OK(builder.AddColumn(kResource, QLType::Create(DataType::STRING)));
  CHECK_OK(builder.AddColumn(kPermissions, QLType::CreateTypeList(DataType::STRING)));
  return builder.Build();
}

}  // namespace master
}  // namespace yb
