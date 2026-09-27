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
//
// Semantics of LIST ROLES and LIST PERMISSIONS, kept free of executor state so that it can be unit
// tested. The executor reads system_auth.roles and system_auth.role_permissions from the master,
// loads them into an AuthCatalog, and calls ListRoles() / ListPermissions().
//
// The behavior follows Apache Cassandra 3.11 (ListRolesStatement / ListPermissionsStatement),
// verified empirically against Cassandra 3.11.19:
// - Resources and roles named in the statement must exist; this is checked before authorization.
// - A caller who is not a superuser and has no DESCRIBE on ALL ROLES may only see its own roles
//   (itself and every role granted to it, directly or through other roles). In addition,
//   LIST PERMISSIONS OF r is allowed when the caller has DESCRIBE on role r itself; LIST ROLES
//   does not consider DESCRIBE on individual roles.
// - LIST PERMISSIONS always includes grants inherited through role membership, and the "role"
//   column names the role that holds the grant. NORECURSIVE only drops the parent resources of
//   the ON resource.
// - Output is ordered by role, then canonical resource name, then Cassandra's permission order.
//--------------------------------------------------------------------------------------------------

#pragma once

#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "yb/common/common_types.pb.h"

#include "yb/util/status_fwd.h"

namespace yb {
namespace ql {

// One row of system_auth.roles.
struct AuthRoleInfo {
  std::string role;
  bool can_login = false;
  bool is_superuser = false;
  // Roles granted directly to this role.
  std::vector<std::string> member_of;
};

// Snapshot of the role catalog, as read from system_auth.roles and system_auth.role_permissions.
class AuthCatalog {
 public:
  void AddRole(AuthRoleInfo info);

  // Adds the permissions (by name, for example "SELECT") granted to `role` on the canonical
  // resource `resource` (for example "data/ks/t"). Unknown permission names are an error.
  Status AddGrant(const std::string& role, const std::string& resource,
                  const std::vector<std::string>& permission_names);

  bool RoleExists(const std::string& role) const;

  // `role` plus the roles granted to it: transitively when `recursive`, otherwise only the direct
  // grants. Empty if `role` does not exist. Tolerates membership cycles.
  std::set<std::string> RolesOf(const std::string& role, bool recursive) const;

  // True if `role`, or any role granted to it, is a superuser.
  bool IsSuperuser(const std::string& role) const;

  // True if `role`, or any role granted to it, holds `permission` on `resource`.
  bool HasPermission(const std::string& role, const std::string& resource,
                     PermissionType permission) const;

  const std::map<std::string, AuthRoleInfo>& roles() const { return roles_; }

  // role -> canonical resource -> granted permissions.
  const std::map<std::string, std::map<std::string, std::set<PermissionType>>>& grants() const {
    return grants_;
  }

 private:
  std::map<std::string, AuthRoleInfo> roles_;
  std::map<std::string, std::map<std::string, std::set<PermissionType>>> grants_;
};

// The resource of "LIST ... ON resource".
struct ListResourceSpec {
  ResourceType type;
  // Canonical name, for example "data", "data/ks", "data/ks/t", "roles" or "roles/r".
  std::string canonical_name;
};

// One output row of LIST PERMISSIONS. The username column is always equal to role.
struct PermissionRow {
  std::string role;
  // Canonical resource name.
  std::string resource;
  PermissionType permission;
};

// Executes LIST ROLES [OF of_role] [NORECURSIVE] for `caller`. Returns the roles to list, sorted
// by name, or an error status carrying a QL error code (ROLE_NOT_FOUND or UNAUTHORIZED).
Result<std::vector<AuthRoleInfo>> ListRoles(const AuthCatalog& catalog,
                                            const std::string& caller,
                                            const std::optional<std::string>& of_role,
                                            bool recursive);

// Executes LIST permission [ON resource] [OF of_role] [NORECURSIVE] for `caller`. `permission` is
// ALL_PERMISSION for "LIST ALL". Keyspace and table existence is checked during semantic analysis;
// role existence (OF role and ON ROLE) is checked here. Returns the rows, sorted, or an error
// status carrying a QL error code (ROLE_NOT_FOUND or UNAUTHORIZED).
Result<std::vector<PermissionRow>> ListPermissions(const AuthCatalog& catalog,
                                                   const std::string& caller,
                                                   PermissionType permission,
                                                   const std::optional<ListResourceSpec>& resource,
                                                   const std::optional<std::string>& of_role,
                                                   bool recursive);

// Cassandra's display form of a canonical resource name: "<all keyspaces>", "<keyspace ks>",
// "<table ks.t>", "<all roles>" or "<role r>". Returns std::nullopt for names that are not
// data or role resources.
std::optional<std::string> ResourceDisplayName(const std::string& canonical_name);

// Canonical names of `resource` and its parents, most specific first. For example "data/ks/t",
// "data/ks", "data"; or "roles/r", "roles".
std::vector<std::string> ResourceChain(const std::string& canonical_name);

// Permission name as shown by Cassandra, for example "SELECT".
std::string ListPermissionName(PermissionType permission);

}  // namespace ql
}  // namespace yb
