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
      RoleType roleType,
      Set<PermissionInfoIdentifier> permissionList)
      throws PlatformServiceException {
    permissionUtil.validatePermissionList(permissionList);
    log.info(
        "Creating {} Role '{}' with customer UUID '{}' and permissions {}.",
        roleType,
        name,
        customerUUID,
        permissionList);
    return Role.create(customerUUID, name, roleType, permissionList);
  }

  public Role editRolePermissions(UUID roleUUID, Set<PermissionInfoIdentifier> permissionList)
      throws PlatformServiceException {
    Role role = Role.getOrBadRequest(roleUUID);
    permissionUtil.validatePermissionList(permissionList);
    log.info(
        "Editing {} Role ('{}':'{}') with permissions {}.",
        role.getRoleType(),
        role.getName(),
        role.getRoleUUID(),
        permissionList);
    role.updatePermissionList(permissionList);
    return role;
  }

  public void deleteRole(UUID roleUUID) throws PlatformServiceException {
    // Later need to check if any policy uses this role.
    Role role = Role.getOrBadRequest(roleUUID);
    log.info(
        "Deleting {} Role ('{}':'{}').", role.getRoleType(), role.getName(), role.getRoleUUID());
    role.delete();
  }
}
