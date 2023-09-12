// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.common.rbac;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.rbac.Role;
import com.yugabyte.yw.models.rbac.Role.RoleType;
import java.util.Set;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Singleton
public class RoleUtil {
  PermissionUtil permissionUtil;

  @Inject
  public RoleUtil(PermissionUtil permissionUtil) {
    this.permissionUtil = permissionUtil;
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

  public Role editRole(
      UUID customerUUID, UUID roleUUID, String description, Set<Permission> permissionList)
      throws PlatformServiceException {
    Role role = Role.getOrBadRequest(customerUUID, roleUUID);
    permissionUtil.validatePermissionList(permissionList);
    log.info(
        "Editing {} Role ('{}':'{}') with permissions {}.",
        role.getRoleType(),
        role.getName(),
        role.getRoleUUID(),
        permissionList);
    role.updateRole(description, permissionList);
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
