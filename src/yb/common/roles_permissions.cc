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

#include "yb/common/roles_permissions.h"

#include "yb/util/flags.h"
#include "yb/util/logging.h"

#include "yb/gutil/strings/substitute.h"

DECLARE_bool(ycql_enable_list_roles_permissions);

using std::string;
using std::vector;

namespace yb {

const std::unordered_map<string, vector<PermissionType>> all_permissions_by_resource = {
    {"KEYSPACE", {ALTER_PERMISSION, AUTHORIZE_PERMISSION, CREATE_PERMISSION, DROP_PERMISSION,
                  MODIFY_PERMISSION, SELECT_PERMISSION}},
    {"ALL_KEYSPACES", {ALTER_PERMISSION, AUTHORIZE_PERMISSION, CREATE_PERMISSION, DROP_PERMISSION,
                       MODIFY_PERMISSION, SELECT_PERMISSION}},
    {"TABLE", {ALTER_PERMISSION, AUTHORIZE_PERMISSION, DROP_PERMISSION, MODIFY_PERMISSION,
               SELECT_PERMISSION}},
    // DESCRIBE is added while the ycql_enable_list_roles_permissions AutoFlag is on (see below).
    {"ROLE", {ALTER_PERMISSION, AUTHORIZE_PERMISSION, DROP_PERMISSION}},
    {"ALL_ROLES", {ALTER_PERMISSION, AUTHORIZE_PERMISSION, CREATE_PERMISSION, DESCRIBE_PERMISSION,
                   DROP_PERMISSION}}
};

const std::vector<PermissionType> empty_permissions;

// As in Apache Cassandra, DESCRIBE on a single role lets the grantee list its permissions
// (LIST PERMISSIONS OF role). It is gated with LIST by the ycql_enable_list_roles_permissions
// AutoFlag, so that during a rolling upgrade neither GRANT DESCRIBE ON ROLE nor the creator's
// grant on a new role stores it before every process supports it.
const std::vector<PermissionType> role_permissions_with_describe = {
    ALTER_PERMISSION, AUTHORIZE_PERMISSION, DESCRIBE_PERMISSION, DROP_PERMISSION};

const vector<PermissionType>& all_permissions_for_resource(ResourceType resource_type) {
  if (resource_type == ResourceType::ROLE && FLAGS_ycql_enable_list_roles_permissions) {
    return role_permissions_with_describe;
  }
  const auto iter = all_permissions_by_resource.find(ResourceType_Name(resource_type));
  if (iter == all_permissions_by_resource.end()) {
    return empty_permissions;
  }
  return iter->second;
}

bool valid_permission_for_resource(PermissionType permission, ResourceType resource_type) {
  const vector<PermissionType>& all_permissions = all_permissions_for_resource(resource_type);
  for (const auto& p : all_permissions) {
    if (p == permission) {
      return true;
    }
  }
  return false;
}

std::string get_canonical_keyspace(const std::string &keyspace) {
  return strings::Substitute("$0/$1", kRolesDataResource, keyspace);
}

std::string get_canonical_table(const std::string &keyspace, const std::string &table) {
  return strings::Substitute("$0/$1/$2", kRolesDataResource, keyspace, table);
}

std::string get_canonical_role(const std::string &role) {
  return strings::Substitute("$0/$1", kRolesRoleResource, role);
}

std::string PermissionName(const PermissionType permission) {
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
