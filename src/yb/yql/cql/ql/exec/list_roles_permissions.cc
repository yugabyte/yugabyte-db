//--------------------------------------------------------------------------------------------------
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
//--------------------------------------------------------------------------------------------------

#include "yb/yql/cql/ql/exec/list_roles_permissions.h"

#include <algorithm>
#include <tuple>
#include <unordered_map>

#include "yb/common/roles_permissions.h"

#include "yb/gutil/strings/substitute.h"

#include "yb/util/logging.h"
#include "yb/util/result.h"
#include "yb/util/status_format.h"

#include "yb/yql/cql/ql/util/errcodes.h"

namespace yb {
namespace ql {

using std::string;
using std::vector;
using strings::Substitute;

namespace {

// Cassandra lists permissions in the declaration order of its Permission enum, which differs from
// the numeric order of PermissionType (ALTER_PERMISSION is 0, CREATE_PERMISSION is 1).
int CassandraPermissionRank(PermissionType permission) {
  switch (permission) {
    case PermissionType::CREATE_PERMISSION: return 0;
    case PermissionType::ALTER_PERMISSION: return 1;
    case PermissionType::DROP_PERMISSION: return 2;
    case PermissionType::SELECT_PERMISSION: return 3;
    case PermissionType::MODIFY_PERMISSION: return 4;
    case PermissionType::AUTHORIZE_PERMISSION: return 5;
    case PermissionType::DESCRIBE_PERMISSION: return 6;
    case PermissionType::ALL_PERMISSION: break;
  }
  return 7;
}

const std::unordered_map<string, PermissionType>& PermissionsByName() {
  static const auto* const kMap = new std::unordered_map<string, PermissionType>{
      {"CREATE", PermissionType::CREATE_PERMISSION},
      {"ALTER", PermissionType::ALTER_PERMISSION},
      {"DROP", PermissionType::DROP_PERMISSION},
      {"SELECT", PermissionType::SELECT_PERMISSION},
      {"MODIFY", PermissionType::MODIFY_PERMISSION},
      {"AUTHORIZE", PermissionType::AUTHORIZE_PERMISSION},
      {"DESCRIBE", PermissionType::DESCRIBE_PERMISSION},
  };
  return *kMap;
}

// An error carrying a QL error code, with `message` as the whole message text. (ErrorStatus()
// would put the error code name in the message, which the executor adds again when it formats
// the error for the client.)
Status ListError(ErrorCode code, const string& message) {
  return STATUS(QLError, message, Slice(), QLError(code));
}

string RoleDisplayName(const string& role) {
  return Substitute("<role $0>", role);
}

Status RoleNotFound(const string& role) {
  return ListError(ErrorCode::ROLE_NOT_FOUND, Substitute("$0 doesn't exist",
                                                         RoleDisplayName(role)));
}

// Whether `caller` may see everything: superusers and roles with DESCRIBE on ALL ROLES.
bool CanDescribeAllRoles(const AuthCatalog& catalog, const string& caller) {
  return catalog.IsSuperuser(caller) ||
         catalog.HasPermission(caller, kRolesRoleResource, PermissionType::DESCRIBE_PERMISSION);
}

const string kRolesResourcePrefix = string(kRolesRoleResource) + "/";
const string kDataResourcePrefix = string(kRolesDataResource) + "/";

}  // namespace

//--------------------------------------------------------------------------------------------------
// AuthCatalog.

void AuthCatalog::AddRole(AuthRoleInfo info) {
  auto name = info.role;
  roles_[name] = std::move(info);
}

Status AuthCatalog::AddGrant(const string& role, const string& resource,
                             const vector<string>& permission_names) {
  auto& permissions = grants_[role][resource];
  for (const auto& name : permission_names) {
    auto it = PermissionsByName().find(name);
    if (it == PermissionsByName().end()) {
      return STATUS_FORMAT(Corruption, "Unknown permission $0 granted to role $1 on $2",
                           name, role, resource);
    }
    permissions.insert(it->second);
  }
  return Status::OK();
}

bool AuthCatalog::RoleExists(const string& role) const {
  return roles_.find(role) != roles_.end();
}

std::set<string> AuthCatalog::RolesOf(const string& role, bool recursive) const {
  std::set<string> result;
  if (!RoleExists(role)) {
    return result;
  }
  vector<string> pending = {role};
  result.insert(role);
  while (!pending.empty()) {
    const string current = std::move(pending.back());
    pending.pop_back();
    auto it = roles_.find(current);
    if (it == roles_.end()) {
      continue;
    }
    for (const auto& granted : it->second.member_of) {
      if (!RoleExists(granted)) {
        continue;
      }
      if (result.insert(granted).second && recursive) {
        pending.push_back(granted);
      }
    }
    if (!recursive) {
      break;
    }
  }
  return result;
}

bool AuthCatalog::IsSuperuser(const string& role) const {
  for (const auto& r : RolesOf(role, true /* recursive */)) {
    if (roles_.at(r).is_superuser) {
      return true;
    }
  }
  return false;
}

bool AuthCatalog::HasPermission(const string& role, const string& resource,
                                PermissionType permission) const {
  for (const auto& r : RolesOf(role, true /* recursive */)) {
    auto role_it = grants_.find(r);
    if (role_it == grants_.end()) {
      continue;
    }
    auto resource_it = role_it->second.find(resource);
    if (resource_it != role_it->second.end() && resource_it->second.count(permission)) {
      return true;
    }
  }
  return false;
}

//--------------------------------------------------------------------------------------------------
// LIST ROLES.

Result<vector<AuthRoleInfo>> ListRoles(const AuthCatalog& catalog,
                                       const string& caller,
                                       const std::optional<string>& of_role,
                                       bool recursive) {
  if (of_role && !catalog.RoleExists(*of_role)) {
    return RoleNotFound(*of_role);
  }

  std::set<string> names;
  if (CanDescribeAllRoles(catalog, caller)) {
    if (of_role) {
      names = catalog.RolesOf(*of_role, recursive);
    } else {
      for (const auto& entry : catalog.roles()) {
        names.insert(entry.first);
      }
    }
  } else if (!of_role) {
    // Without DESCRIBE on ALL ROLES, Cassandra lists the caller's own roles instead of failing.
    names = catalog.RolesOf(caller, recursive);
  } else if (catalog.RolesOf(caller, true /* recursive */).count(*of_role)) {
    names = catalog.RolesOf(*of_role, recursive);
  } else {
    return ListError(ErrorCode::UNAUTHORIZED,
                     Substitute("You are not authorized to view roles granted to $0", *of_role));
  }

  vector<AuthRoleInfo> result;
  result.reserve(names.size());
  for (const auto& name : names) {
    result.push_back(catalog.roles().at(name));
  }
  return result;
}

//--------------------------------------------------------------------------------------------------
// LIST PERMISSIONS.

Result<vector<PermissionRow>> ListPermissions(const AuthCatalog& catalog,
                                              const string& caller,
                                              PermissionType permission,
                                              const std::optional<ListResourceSpec>& resource,
                                              const std::optional<string>& of_role,
                                              bool recursive) {
  // Existence checks come before authorization, resource first (as in Cassandra).
  if (resource && resource->type == ResourceType::ROLE) {
    const string role = resource->canonical_name.substr(kRolesResourcePrefix.size());
    if (!catalog.RoleExists(role)) {
      return RoleNotFound(role);
    }
  }
  if (of_role && !catalog.RoleExists(*of_role)) {
    return RoleNotFound(*of_role);
  }

  // Besides DESCRIBE on ALL ROLES, `OF r` is allowed when r is one of the caller's roles or the
  // caller has DESCRIBE on role r itself (as in Cassandra; a role's creator gets it).
  if (!CanDescribeAllRoles(catalog, caller) &&
      !(of_role && (catalog.RolesOf(caller, true /* recursive */).count(*of_role) ||
                    catalog.HasPermission(caller, kRolesResourcePrefix + *of_role,
                                          PermissionType::DESCRIBE_PERMISSION)))) {
    return ListError(
        ErrorCode::UNAUTHORIZED,
        of_role ? Substitute("You are not authorized to view $0's permissions", *of_role)
                : string("You are not authorized to view everyone's permissions"));
  }

  // Grants inherited through role membership are always included; NORECURSIVE does not apply to
  // roles in LIST PERMISSIONS.
  std::set<string> grantees;
  if (of_role) {
    grantees = catalog.RolesOf(*of_role, true /* recursive */);
  } else {
    for (const auto& entry : catalog.roles()) {
      grantees.insert(entry.first);
    }
  }

  // NORECURSIVE drops the parent resources of the ON resource.
  std::optional<std::set<string>> resources;
  if (resource) {
    if (recursive) {
      const auto chain = ResourceChain(*resource);
      resources.emplace(chain.begin(), chain.end());
    } else {
      resources.emplace(std::set<string>{resource->canonical_name});
    }
  }

  vector<PermissionRow> rows;
  for (const auto& grantee : grantees) {
    auto role_it = catalog.grants().find(grantee);
    if (role_it == catalog.grants().end()) {
      continue;
    }
    for (const auto& [resource_name, permissions] : role_it->second) {
      if (resources && !resources->count(resource_name)) {
        continue;
      }
      if (!ResourceDisplayName(resource_name)) {
        LOG(WARNING) << "Skipping unknown resource " << resource_name << " granted to "
                     << grantee;
        continue;
      }
      for (const auto granted : permissions) {
        if (permission == PermissionType::ALL_PERMISSION || permission == granted) {
          rows.push_back(PermissionRow{grantee, resource_name, granted});
        }
      }
    }
  }

  std::sort(rows.begin(), rows.end(), [](const PermissionRow& a, const PermissionRow& b) {
    return std::make_tuple(std::cref(a.role), std::cref(a.resource),
                           CassandraPermissionRank(a.permission)) <
           std::make_tuple(std::cref(b.role), std::cref(b.resource),
                           CassandraPermissionRank(b.permission));
  });
  return rows;
}

//--------------------------------------------------------------------------------------------------
// Resource names.

std::optional<string> ResourceDisplayName(const string& canonical_name,
                                          const std::optional<ListResourceSpec>& on) {
  if (on && (on->type == ResourceType::KEYSPACE || on->type == ResourceType::TABLE)) {
    const string keyspace_resource = get_canonical_keyspace(on->keyspace);
    if (canonical_name == keyspace_resource) {
      return Substitute("<keyspace $0>", on->keyspace);
    }
    if (on->type == ResourceType::TABLE && canonical_name == on->canonical_name) {
      return Substitute("<table $0.$1>", on->keyspace,
                        canonical_name.substr(keyspace_resource.size() + 1));
    }
  }
  if (canonical_name == kRolesDataResource) {
    return string("<all keyspaces>");
  }
  if (canonical_name == kRolesRoleResource) {
    return string("<all roles>");
  }
  if (canonical_name.starts_with(kRolesResourcePrefix)) {
    return RoleDisplayName(canonical_name.substr(kRolesResourcePrefix.size()));
  }
  if (canonical_name.starts_with(kDataResourcePrefix)) {
    const string rest = canonical_name.substr(kDataResourcePrefix.size());
    // Without the ON resource, take the first '/' as the separator. This is ambiguous only for
    // quoted keyspace names that contain '/'.
    const auto slash = rest.find('/');
    if (slash == string::npos) {
      return Substitute("<keyspace $0>", rest);
    }
    return Substitute("<table $0.$1>", rest.substr(0, slash), rest.substr(slash + 1));
  }
  return std::nullopt;
}

vector<string> ResourceChain(const ListResourceSpec& resource) {
  vector<string> chain = {resource.canonical_name};
  switch (resource.type) {
    case ResourceType::TABLE:
      chain.push_back(get_canonical_keyspace(resource.keyspace));
      chain.push_back(kRolesDataResource);
      break;
    case ResourceType::KEYSPACE:
      chain.push_back(kRolesDataResource);
      break;
    case ResourceType::ROLE:
      chain.push_back(kRolesRoleResource);
      break;
    case ResourceType::ALL_KEYSPACES:
    case ResourceType::ALL_ROLES:
      break;
  }
  return chain;
}

string ListPermissionName(PermissionType permission) {
  return PermissionName(permission);
}

}  // namespace ql
}  // namespace yb
