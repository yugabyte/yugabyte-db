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

#ifndef YB_MASTER_PERMISSIONS_MANAGER_H
#define YB_MASTER_PERMISSIONS_MANAGER_H

#include "yb/common/entity_ids.h"
#include "yb/common/roles_permissions.h"

#include "yb/master/master.pb.h"
#include "yb/master/catalog_entity_info.h"
#include "yb/master/catalog_manager.h"

#include "yb/rpc/rpc.h"

#include "yb/util/status.h"

namespace yb {
namespace master {

class PermissionsManager final {
 public:
  explicit PermissionsManager(CatalogManager* catalog_manager);

  // Create a new role for authentication/authorization.
  //
  // The RPC context is provided for logging/tracing purposes.
  // but this function does not itself respond to the RPC.
  CHECKED_STATUS CreateRole(const CreateRoleRequestPB* req,
                            CreateRoleResponsePB* resp,
                            rpc::RpcContext* rpc);

  // Alter an existing role for authentication/authorization.
  //
  // The RPC context is provided for logging/tracing purposes,
  // but this function does not itself respond to the RPC.
  CHECKED_STATUS AlterRole(const AlterRoleRequestPB* req,
                           AlterRoleResponsePB* resp,
                           rpc::RpcContext* rpc);

  // Delete the role.
  //
  // The RPC context is provided for logging/tracing purposes,
  // but this function does not itself respond to the RPC.
  CHECKED_STATUS DeleteRole(const DeleteRoleRequestPB* req,
                            DeleteRoleResponsePB* resp,
                            rpc::RpcContext* rpc);

  // Generic Create Role function for both default roles and user defined roles.
  CHECKED_STATUS CreateRoleUnlocked(
      const std::string& role_name,
      const std::string& salted_hash,
      const bool login,
      const bool superuser,
      int64_t term,
      // This value is only set to false during the creation of the
      // default role when it doesn't exist.
      const bool increment_roles_version = true) REQUIRES_SHARED(catalog_manager_->mutex_);

  // Grant one role to another role.
  CHECKED_STATUS GrantRevokeRole(const GrantRevokeRoleRequestPB* req,
                                 GrantRevokeRoleResponsePB* resp,
                                 rpc::RpcContext* rpc);

    // Grant/Revoke a permission to a role.
  CHECKED_STATUS GrantRevokePermission(const GrantRevokePermissionRequestPB* req,
                                       GrantRevokePermissionResponsePB* resp,
                                       rpc::RpcContext* rpc);

  // Get all the permissions granted to resources.
  CHECKED_STATUS GetPermissions(const GetPermissionsRequestPB* req,
                                GetPermissionsResponsePB* resp,
                                rpc::RpcContext* rpc);

  void BuildResourcePermissionsUnlocked();

  // Increment the version stored in roles_version_ if it exists. Otherwise, creates a
  // SysVersionInfo object with version equal to 0 to track the roles versions.
  CHECKED_STATUS IncrementRolesVersionUnlocked() REQUIRES_SHARED(catalog_manager_->mutex_);

  // Grant the specified permissions.
  template<class RespClass>
  CHECKED_STATUS GrantPermissions(
      const RoleName& role_name,
      const std::string& canonical_resource,
      const std::string& resource_name,
      const NamespaceName& keyspace,
      const std::vector<PermissionType>& permissions,
      const ResourceType resource_type,
      RespClass* resp);

  // For each role in roles_map_ traverse all of its resources and delete any resource that matches
  // the given canonical resource. This is used when a table/keyspace/role is deleted so that we
  // don't leave old permissions alive. This is specially dangerous when a resource with the same
  // canonical name is created again.
  template<class RespClass>
  CHECKED_STATUS RemoveAllPermissionsForResourceUnlocked(
      const std::string& canonical_resource, RespClass* resp)
      REQUIRES_SHARED(catalog_manager_->mutex_);

  template<class RespClass>
  CHECKED_STATUS RemoveAllPermissionsForResource(const std::string& canonical_resource,
                                                 RespClass* resp);

  CHECKED_STATUS PrepareDefaultRoles(int64_t term) REQUIRES_SHARED(catalog_manager_->mutex_);

  void GetAllRoles(std::vector<scoped_refptr<RoleInfo>>* roles);

  // Find all the roles for which 'role' is a member of the list 'member_of'.
  std::vector<std::string> DirectMemberOf(const RoleName& role);

  void TraverseRole(const string& role_name, std::unordered_set<RoleName>* granted_roles);

  // Build the recursive map of roles (recursive_granted_roles_). If r1 is granted to r2, and r2
  // is granted to r3, then recursive_granted_roles_["r3"] will contain roles r2, and r1.
  void BuildRecursiveRoles();

  void BuildRecursiveRolesUnlocked();

  bool IsMemberOf(const RoleName& granted_role, const RoleName& role);

  void AddRoleUnlocked(const RoleName& role_name, scoped_refptr<RoleInfo> role_info);
  bool DoesRoleExistUnlocked(const RoleName& role_name);
  void ClearRolesUnlocked();

  // This is invoked from PrepareDefaultSysConfig in catalog manger and sets up the default state of
  // of cluster security config.
  CHECKED_STATUS PrepareDefaultSecurityConfigUnlocked(int64_t term);

  // Sets the security config loaded from the sys catalog.
  void SetSecurityConfigOnLoadUnlocked(SysConfigInfo* security_config);

 private:
  // Role map: RoleName -> RoleInfo
  typedef std::unordered_map<RoleName, scoped_refptr<RoleInfo> > RoleInfoMap;
  RoleInfoMap roles_map_;

  typedef std::unordered_map<RoleName, std::unordered_set<RoleName>> RoleMemberMap;

  typedef std::string ResourceName;

  // Resource permissions map: resource -> permissions.
  typedef std::unordered_map<ResourceName, Permissions> ResourcePermissionsMap;

  // Role permissions map: role name -> map of resource permissions.
  typedef std::unordered_map<RoleName, ResourcePermissionsMap> RolePermissionsMap;

  // role_name -> set of granted roles (including those acquired transitively).
  RoleMemberMap recursive_granted_roles_;

  RolePermissionsMap recursive_granted_permissions_;

  // Permissions cache. Kept in a protobuf to avoid rebuilding it every time we receive a request
  // from a client.
  std::shared_ptr<GetPermissionsResponsePB> permissions_cache_;

  // Cluster security config.
  scoped_refptr<SysConfigInfo> security_config_ = nullptr;

  std::vector<PermissionType> all_permissions_ = {
      PermissionType::ALTER_PERMISSION,
      PermissionType::AUTHORIZE_PERMISSION,
      PermissionType::CREATE_PERMISSION,
      PermissionType::DESCRIBE_PERMISSION,
      PermissionType::DROP_PERMISSION,
      PermissionType::MODIFY_PERMISSION,
      PermissionType::SELECT_PERMISSION
  };

  CatalogManager* catalog_manager_;

  DISALLOW_COPY_AND_ASSIGN(PermissionsManager);
};

}  // namespace master
}  // namespace yb

#endif  // YB_MASTER_PERMISSIONS_MANAGER_H
