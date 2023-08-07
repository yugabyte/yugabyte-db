package com.yugabyte.yw.common.rbac;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.google.inject.Singleton;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.rbac.ResourceGroup;
import com.yugabyte.yw.models.rbac.ResourceGroup.ResourceDefinition;
import com.yugabyte.yw.models.rbac.Role;
import com.yugabyte.yw.models.rbac.RoleBinding;
import com.yugabyte.yw.models.rbac.RoleBinding.RoleBindingType;
import io.ebean.annotation.Transactional;
import java.util.List;
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

  @Transactional
  public List<RoleBinding> setUserRoleBindings(
      UUID userUUID,
      List<RoleResourceDefinition> roleResourceDefinitions,
      RoleBindingType roleBindingType) {
    // Delete all existing role binding entries for user.
    List<RoleBinding> userRoleBindings = RoleBinding.getAll(userUUID);
    for (RoleBinding roleBinding : userRoleBindings) {
      roleBinding.delete();
    }

    // Create and save all given roleBindings for user.
    for (RoleResourceDefinition roleResourceDefinition : roleResourceDefinitions) {
      createRoleBinding(
          userUUID,
          roleResourceDefinition.getRoleUUID(),
          roleBindingType,
          roleResourceDefinition.getResourceGroup());
    }
    return RoleBinding.getAll(userUUID);
  }

  public void validateRoles(UUID userUUID, List<RoleResourceDefinition> roleResourceDefinitions) {
    UUID customerUUID = Users.getOrBadRequest(userUUID).getCustomerUUID();
    for (RoleResourceDefinition roleResourceDefinition : roleResourceDefinitions) {
      Role.getOrBadRequest(customerUUID, roleResourceDefinition.getRoleUUID());
    }
  }

  public void validateResourceGroups(
      UUID customerUUID, List<RoleResourceDefinition> roleResourceDefinitions) {
    for (RoleResourceDefinition roleResourceDefinition : roleResourceDefinitions) {
      for (ResourceDefinition resourceDefinition :
          roleResourceDefinition.getResourceGroup().getResourceDefinitionSet()) {
        validateResourceDefinition(customerUUID, resourceDefinition);
      }
    }
  }

  public void validateResourceDefinition(UUID customerUUID, ResourceDefinition resourceDefinition) {
    // Check that the resource type is valid.
    if (resourceDefinition.getResourceType() == null) {
      String errMsg =
          String.format(
              "Resource type cannot be null inside resource definition %s.",
              resourceDefinition.toString());
      log.error(errMsg);
      throw new PlatformServiceException(BAD_REQUEST, errMsg);
    }
    // Check if the resources given by UUID exist or not.
    for (UUID resourceUUID : resourceDefinition.getResourceUUIDSet()) {
      if (!resourceDefinition.getResourceType().isValidResource(customerUUID, resourceUUID)) {
        String errMsg =
            String.format(
                "Resource UUID '%s' of type '%s' is not valid or doesn't exist.",
                resourceUUID, resourceDefinition.getResourceType());
        log.error(errMsg);
        throw new PlatformServiceException(BAD_REQUEST, errMsg);
      }
    }
  }
}
