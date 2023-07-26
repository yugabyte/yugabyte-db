package com.yugabyte.yw.common.rbac;

import com.google.inject.Singleton;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.rbac.ResourceGroup;
import com.yugabyte.yw.models.rbac.Role;
import com.yugabyte.yw.models.rbac.RoleBinding;
import com.yugabyte.yw.models.rbac.RoleBinding.RoleBindingType;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Singleton
public class RoleBindingUtil {

  public RoleBinding createRoleBinding(
      UUID userUUID, UUID roleUUID, RoleBindingType type, ResourceGroup resourceGroup) {
    Users user = Users.getOrBadRequest(userUUID);
    Role role = Role.getOrBadRequest(user.getCustomerUUID(), roleUUID);
    log.info(
        "Creating {} RoleBinding '{}' with user UUID '{}', role UUID {} on resource group {}.",
        type,
        userUUID,
        roleUUID,
        resourceGroup);
    return RoleBinding.create(user, type, role, resourceGroup);
  }

  public RoleBinding editRoleBinding(
      UUID roleBindingUUID, UUID roleUUID, ResourceGroup resourceGroup) {
    RoleBinding roleBinding = RoleBinding.getOrBadRequest(roleBindingUUID);
    Role role = Role.getOrBadRequest(roleBinding.getUser().getCustomerUUID(), roleUUID);
    log.info(
        "Editing {} RoleBinding '{}' with user UUID '{}', role UUID {} on resource group {}.",
        roleBinding.getType(),
        roleBinding.getUuid(),
        roleBinding.getUser().getUuid(),
        roleUUID,
        resourceGroup);
    roleBinding.edit(role, resourceGroup);
    return roleBinding;
  }
}
