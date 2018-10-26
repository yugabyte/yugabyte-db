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

#include "yb/common/roles_permissions.h"
#include "yb/gutil/strings/substitute.h"

namespace yb {
std::string get_canonical_keyspace(const std::string &keyspace) {
  return strings::Substitute("$0/$1", kRolesDataResource, keyspace);
}

std::string get_canonical_table(const std::string &keyspace, const std::string &table) {
  return strings::Substitute("$0/$1/$2", kRolesDataResource, keyspace, table);
}

std::string get_canonical_role(const std::string &role) {
  return strings::Substitute("$0/$1", kRolesRoleResource, role);
}

std::string PermissionName(PermissionType permission) {
  switch(permission) {
    case PermissionType::ALTER_PERMISSION: return "ALTER";
    case PermissionType::CREATE_PERMISSION: return "CREATE";
    case PermissionType::DROP_PERMISSION: return "DROP";
    case PermissionType::SELECT_PERMISSION: return "SELECT";
    case PermissionType::MODIFY_PERMISSION: return "MODIFY";
    case PermissionType::AUTHORIZE_PERMISSION: return "AUTHORIZE";
    case PermissionType::DESCRIBE_PERMISSION: return "DESCRIBE";
    case PermissionType::ALL_PERMISSION:
      LOG(DFATAL) << "Invalid use of ALL_PERMISSION";
      break;
  }
  return "";
}
} // namespace yb
