// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.common.rbac;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.models.rbac.Role;
import com.yugabyte.yw.models.rbac.Role.RoleType;
import io.ebean.annotation.Transactional;
import java.util.HashSet;
import java.util.Set;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Singleton
public class RoleUtil {
  PermissionUtil permissionUtil;
  RoleBindingUtil roleBindingUtil;

  @Inject
  public RoleUtil(PermissionUtil permissionUtil, RoleBindingUtil roleBindingUtil) {
    this.permissionUtil = permissionUtil;
    this.roleBindingUtil = roleBindingUtil;
  }

  public Role createRole(
      UUID customerUUID,
      String name,
      String description,
      RoleType roleType,
      Set<Permission> permissionList)
      throws PlatformServiceException {
    permissionUtil.validatePermissionList(permissionList);
    log.info(
        "Creating {} Role '{}' with customer UUID '{}' and permissions {}.",
        roleType,
        name,
        customerUUID,
        permissionList);
    return Role.create(customerUUID, name, description, roleType, permissionList);
  }

  public Set<ResourceType> getResourceTypesToExpand(
      Set<Permission> oldPermissionSet, Set<Permission> newPermissionSet) {
    // Generic resource type is the resource type of a permission which is not valid on a specific
    // resource (i.e. permissionValidOnResource = false).
    Set<ResourceType> oldGenericResourceTypes = new HashSet<>();
    Set<ResourceType> newGenericResourceTypes = new HashSet<>();

    // Get the set of Generic Resource Types from the old permission list.
    for (Permission permission : oldPermissionSet) {
      PermissionInfo permissionInfo = permissionUtil.permissionInfoMap.get(permission);
      if (!permissionInfo.isPermissionValidOnResource()) {
        oldGenericResourceTypes.add(permissionInfo.getResourceType());
      }
    }

    // Get the set of Generic Resource Types from the new permission list.
    for (Permission permission : newPermissionSet) {
      PermissionInfo permissionInfo = permissionUtil.permissionInfoMap.get(permission);
      if (!permissionInfo.isPermissionValidOnResource()) {
        newGenericResourceTypes.add(permissionInfo.getResourceType());
      }
    }

    // Get only the new generic resource types which were not present before.
    newGenericResourceTypes.removeAll(oldGenericResourceTypes);
    return newGenericResourceTypes;
  }

  @Transactional
  public Role editRole(
      UUID customerUUID, UUID roleUUID, String description, Set<Permission> newPermissionSet)
      throws PlatformServiceException {
    Role role = Role.getOrBadRequest(customerUUID, roleUUID);
    Set<Permission> oldPermissionSet =
        new HashSet<Permission>(role.getPermissionDetails().getPermissionList());

    permissionUtil.validatePermissionList(newPermissionSet);
    log.info(
        "Editing {} Role ('{}':'{}'): Replacing old permissions '{}' with new permissions '{}'.",
        role.getRoleType(),
        role.getName(),
        role.getRoleUUID(),
        oldPermissionSet,
        newPermissionSet);
    // Update the permission list for the role.
    role.updateRole(description, newPermissionSet);

    // Expand the role bindings based on the extra generic permissions given.
    Set<ResourceType> resourceTypesToExpand =
        getResourceTypesToExpand(oldPermissionSet, newPermissionSet);
    log.info(
        "Editing {} Role ('{}':'{}'): Need to expand role bindings with resource types {}.",
        role.getRoleType(),
        role.getName(),
        role.getRoleUUID(),
        resourceTypesToExpand);
    roleBindingUtil.expandRoleBindings(customerUUID, role, resourceTypesToExpand);
    return role;
  }

  public void deleteRole(UUID customerUUID, UUID roleUUID) throws PlatformServiceException {
    // Later need to check if any policy uses this role.
    Role role = Role.getOrBadRequest(customerUUID, roleUUID);
    log.info(
        "Deleting {} Role ('{}':'{}').", role.getRoleType(), role.getName(), role.getRoleUUID());
    role.delete();
  }
}
