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

#pragma once

#include <stdint.h>

#include <set>
#include <type_traits>
#include <utility>

#include "yb/util/flags.h"
#include "yb/util/logging.h"

#include "yb/common/entity_ids.h"
#include "yb/common/roles_permissions.h"

#include "yb/gutil/callback.h"
#include "yb/gutil/integral_types.h"

#include "yb/master/catalog_manager.h"

#include "yb/rpc/rpc.h"

#include "yb/util/debug/lock_debug.h"
#include "yb/util/math_util.h"
#include "yb/util/shared_lock.h"
#include "yb/util/status_callback.h"
#include "yb/util/status_fwd.h"
#include "yb/util/unique_lock.h"

namespace yb {
namespace master {

class PermissionsManager final {
 public:
  explicit PermissionsManager(CatalogManager* catalog_manager);

  // Create a new role for authentication/authorization.
  //
  // The RPC context is provided for logging/tracing purposes.
  // but this function does not itself respond to the RPC.
  Status CreateRole(const CreateRoleRequestPB* req,
                    CreateRoleResponsePB* resp,
                    rpc::RpcContext* rpc) EXCLUDES(mutex_);

  // Alter an existing role for authentication/authorization.
  //
  // The RPC context is provided for logging/tracing purposes,
  // but this function does not itself respond to the RPC.
  Status AlterRole(const AlterRoleRequestPB* req,
                   AlterRoleResponsePB* resp,
                   rpc::RpcContext* rpc) EXCLUDES(mutex_);

  // Delete the role.
  //
  // The RPC context is provided for logging/tracing purposes,
  // but this function does not itself respond to the RPC.
  Status DeleteRole(const DeleteRoleRequestPB* req,
                    DeleteRoleResponsePB* resp,
                    rpc::RpcContext* rpc) EXCLUDES(mutex_);

  // Generic Create Role function for both default roles and user defined roles.
  Status CreateRoleUnlocked(
      const std::string& role_name,
      const std::string& salted_hash,
      const bool login,
      const bool superuser,
      int64_t term,
      // This value is only set to false during the creation of the
      // default role when it doesn't exist.
      const bool increment_roles_version = true) REQUIRES(mutex_);

  // Grant one role to another role.
  Status GrantRevokeRole(const GrantRevokeRoleRequestPB* req,
                         GrantRevokeRoleResponsePB* resp,
                         rpc::RpcContext* rpc) EXCLUDES(mutex_);

  // Grant/Revoke a permission to a role.
  Status GrantRevokePermission(const GrantRevokePermissionRequestPB* req,
                               GrantRevokePermissionResponsePB* resp,
                               rpc::RpcContext* rpc) EXCLUDES(mutex_);

  // Get all the permissions granted to resources.
  Status GetPermissions(const GetPermissionsRequestPB* req,
                        GetPermissionsResponsePB* resp,
                        rpc::RpcContext* rpc) EXCLUDES(mutex_);

  void BuildResourcePermissionsUnlocked() REQUIRES(mutex_);

  // Increment the version stored in roles_version_ if it exists. Otherwise, creates a
  // SysVersionInfo object with version equal to 0 to track the roles versions.
  Status IncrementRolesVersionUnlocked() REQUIRES_SHARED(mutex_);

  // Grant the specified permissions.
  template<class RespClass>
  Status GrantPermissions(
      const RoleName& role_name,
      const std::string& canonical_resource,
      const std::string& resource_name,
      const NamespaceName& keyspace,
      const std::vector<PermissionType>& permissions,
      const ResourceType resource_type,
      RespClass* resp) EXCLUDES(mutex_);

  // For each role in roles_map_ traverse all of its resources and delete any resource that matches
  // the given canonical resource. This is used when a table/keyspace/role is deleted so that we
  // don't leave old permissions alive. This is specially dangerous when a resource with the same
  // canonical name is created again.
  template<class RespClass>
  Status RemoveAllPermissionsForResourceUnlocked(
      const std::string& canonical_resource, RespClass* resp)
      REQUIRES(mutex_);

  template<class RespClass>
  Status RemoveAllPermissionsForResource(const std::string& canonical_resource,
                                         RespClass* resp) EXCLUDES(mutex_);

  Status PrepareDefaultRoles(int64_t term) EXCLUDES(mutex_);

  void GetAllRoles(std::vector<scoped_refptr<RoleInfo>>* roles) EXCLUDES(mutex_);

  // Find all the roles for which 'role' is a member of the list 'member_of'.
  std::vector<std::string> DirectMemberOf(const RoleName& role) REQUIRES_SHARED(mutex_);

  void TraverseRole(const std::string& role_name, std::unordered_set<RoleName>* granted_roles)
      REQUIRES_SHARED(mutex_);

  // Build the recursive map of roles (recursive_granted_roles_). If r1 is granted to r2, and r2
  // is granted to r3, then recursive_granted_roles_["r3"] will contain roles r2, and r1.
  void BuildRecursiveRoles() EXCLUDES(mutex_);

  void BuildRecursiveRolesUnlocked() REQUIRES(mutex_);

  bool IsMemberOf(const RoleName& granted_role, const RoleName& role) REQUIRES_SHARED(mutex_);

  void AddRoleUnlocked(const RoleName& role_name, scoped_refptr<RoleInfo> role_info)
      REQUIRES(mutex_);
  void ClearRolesUnlocked() REQUIRES(mutex_);

  // This is invoked from PrepareDefaultSysConfig in catalog manger and sets up the default state of
  // of cluster security config.
  Status PrepareDefaultSecurityConfigUnlocked(int64_t term)
      REQUIRES(mutex_);

  // Sets the security config loaded from the sys catalog.
  void SetSecurityConfigOnLoadUnlocked(SysConfigInfo* security_config)
      REQUIRES(mutex_);

  using MutexType = rw_spinlock;
  MutexType& mutex() RETURN_CAPABILITY(mutex_) {
    return mutex_;
  }

 private:
  // Role map: RoleName -> RoleInfo
  typedef std::unordered_map<RoleName, scoped_refptr<RoleInfo> > RoleInfoMap;
  RoleInfoMap roles_map_ GUARDED_BY(mutex_);

  typedef std::unordered_map<RoleName, std::unordered_set<RoleName>> RoleMemberMap;

  typedef std::string ResourceName;

  // Resource permissions map: resource -> permissions.
  typedef std::unordered_map<ResourceName, Permissions> ResourcePermissionsMap;

  // Role permissions map: role name -> map of resource permissions.
  typedef std::unordered_map<RoleName, ResourcePermissionsMap> RolePermissionsMap;

  // role_name -> set of granted roles (including those acquired transitively).
  RoleMemberMap recursive_granted_roles_ GUARDED_BY(mutex_);

  RolePermissionsMap recursive_granted_permissions_ GUARDED_BY(mutex_);

  // Permissions cache. Kept in a protobuf to avoid rebuilding it every time we receive a request
  // from a client.
  std::shared_ptr<GetPermissionsResponsePB> permissions_cache_ GUARDED_BY(mutex_);

  mutable MutexType mutex_ ACQUIRED_AFTER(catalog_manager_->mutex_);
  using SharedLock = NonRecursiveSharedLock<MutexType>;
  using LockGuard = std::lock_guard<MutexType>;

  // Cluster security config.
  scoped_refptr<SysConfigInfo> security_config_ GUARDED_BY(mutex_);

  CatalogManager* catalog_manager_;

  DISALLOW_COPY_AND_ASSIGN(PermissionsManager);
};

}  // namespace master
}  // namespace yb
