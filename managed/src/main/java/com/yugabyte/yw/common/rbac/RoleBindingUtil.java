// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.common.rbac;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.forms.rbac.ResourcePermissionData;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.rbac.ResourceGroup;
import com.yugabyte.yw.models.rbac.ResourceGroup.ResourceDefinition;
import com.yugabyte.yw.models.rbac.Role;
import com.yugabyte.yw.models.rbac.Role.RoleType;
import com.yugabyte.yw.models.rbac.RoleBinding;
import com.yugabyte.yw.models.rbac.RoleBinding.RoleBindingType;
import io.ebean.annotation.Transactional;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Singleton
public class RoleBindingUtil {
  PermissionUtil permissionUtil;
  RuntimeConfGetter confGetter;

  @Inject
  public RoleBindingUtil(PermissionUtil permissionUtil, RuntimeConfGetter confGetter) {
    this.permissionUtil = permissionUtil;
    this.confGetter = confGetter;
  }

  public RoleBinding createRoleBinding(
      UUID userUUID, UUID roleUUID, RoleBindingType type, ResourceGroup resourceGroup) {
    Users user = Users.getOrBadRequest(userUUID);
    Role role = Role.getOrBadRequest(user.getCustomerUUID(), roleUUID);
    log.info(
        "Creating {} RoleBinding with user UUID '{}', role UUID {} on resource group {}.",
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

  public void validateRoles(
      UUID customerUUID, List<RoleResourceDefinition> roleResourceDefinitions) {
    for (RoleResourceDefinition roleResourceDefinition : roleResourceDefinitions) {
      Role role = Role.getOrBadRequest(customerUUID, roleResourceDefinition.getRoleUUID());
      if (Users.Role.SuperAdmin.name().equals(role.getName())) {
        String errMsg =
            String.format(
                "Cannot assign SuperAdmin role to a user in the roleResourceDefinition: %s.",
                roleResourceDefinition.toString());
        log.error(errMsg);
        throw new PlatformServiceException(BAD_REQUEST, errMsg);
      }
    }
  }

  public void validateResourceGroups(
      UUID customerUUID, List<RoleResourceDefinition> roleResourceDefinitions) {
    for (RoleResourceDefinition roleResourceDefinition : roleResourceDefinitions) {
      validateRoleResourceDefinition(customerUUID, roleResourceDefinition);
    }
  }

  public void validateRoleResourceDefinition(
      UUID customerUUID, RoleResourceDefinition roleResourceDefinition) {

    // System role validation.
    Role role = Role.get(customerUUID, roleResourceDefinition.getRoleUUID());
    if (RoleType.System.equals(role.getRoleType())) {
      // Validate system roles cannot be scoped down.
      if (roleResourceDefinition.getResourceGroup() != null) {
        String errMsg =
            String.format(
                "Cannot specify a resource group for system role ('%s':'%s').",
                role.getName(), role.getRoleUUID());
        log.error(errMsg);
        throw new PlatformServiceException(BAD_REQUEST, errMsg);
      }
    } else {
      // Custom role validation.
      if (roleResourceDefinition.getResourceGroup() == null) {
        String errMsg =
            String.format(
                "Must specify resource group for custom role ('%s':'%s').",
                role.getName(), role.getRoleUUID());
        log.error(errMsg);
        throw new PlatformServiceException(BAD_REQUEST, errMsg);
      }
      // Check that the resource definition set in the resource group is not empty for custom roles.
      if (roleResourceDefinition.getResourceGroup().getResourceDefinitionSet() == null
          || roleResourceDefinition.getResourceGroup().getResourceDefinitionSet().isEmpty()) {
        String errMsg =
            String.format(
                "resourceDefinitionSet cannot be empty in the roleResourceDefinition: %s.",
                roleResourceDefinition.toString());
        log.error(errMsg);
        throw new PlatformServiceException(BAD_REQUEST, errMsg);
      }

      // Basic validatation on each resource definition individually on custom roles.
      for (ResourceDefinition resourceDefinition :
          roleResourceDefinition.getResourceGroup().getResourceDefinitionSet()) {
        validateResourceDefinition(customerUUID, resourceDefinition);
      }

      // Validate that for the given custom role, there is some resource definition that has
      // allowAll = true from the permissions which have "permissionValidOnResource" = false. Which
      // indicates that it is a generic permission, not valid on a specific resource.
      for (Permission permission : role.getPermissionDetails().getPermissionList()) {
        PermissionInfo permissionInfo = permissionUtil.getPermissionInfo(permission);
        if (!permissionInfo.isPermissionValidOnResource()) {
          if (!hasGenericResourceDefinition(
              customerUUID,
              roleResourceDefinition.getResourceGroup().getResourceDefinitionSet(),
              permission.getResourceType())) {
            if (ResourceType.OTHER.equals(permission.getResourceType())) {
              String errMsg =
                  String.format(
                      "For permission '%s' from role '%s' to be valid, it needs a resource"
                          + " definition with '%s' and customerUUID in the resourceUUIDSet.",
                      permission.toString(), role.getName(), permission.getResourceType());
              log.error(errMsg);
              throw new PlatformServiceException(BAD_REQUEST, errMsg);
            } else {
              String errMsg =
                  String.format(
                      "For permission '%s' from role '%s' to be valid, "
                          + "it needs a resource definition with '%s' and allowAll = true.",
                      permission.toString(), role.getName(), permission.getResourceType());
              log.error(errMsg);
              throw new PlatformServiceException(BAD_REQUEST, errMsg);
            }
          }
        }
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
    // Check that both of the fields `allowAll` or `resourceUUIDSet` are not filled.
    if (resourceDefinition.isAllowAll() && !resourceDefinition.getResourceUUIDSet().isEmpty()) {
      String errMsg =
          String.format(
              "Both 'allowAll' and 'resourceUUIDSet' cannot be filled in resourceDefinition: %s.",
              resourceDefinition.toString());
      log.error(errMsg);
      throw new PlatformServiceException(BAD_REQUEST, errMsg);
    }

    // Check that for OTHER resource types, 'allowAll' is false and only one resource UUID is
    // given, which is the customer UUID.
    if (resourceDefinition.getResourceType().equals(ResourceType.OTHER)) {
      if (resourceDefinition.isAllowAll()
          || !(resourceDefinition.getResourceUUIDSet().size() == 1)
          || (!resourceDefinition.getResourceUUIDSet().iterator().next().equals(customerUUID))) {
        String errMsg =
            String.format(
                "For OTHER resource type, 'allowAll' must be false and "
                    + "'resourceUUIDSet' should have only customerUUID in resourceDefinition: %s.",
                resourceDefinition.toString());
        log.error(errMsg);
        throw new PlatformServiceException(BAD_REQUEST, errMsg);
      }
    }
  }

  /**
   * This function checks if there is atleast one generic resource definition. Generic resource
   * definition is a resource definition that groups all resources of a type. This check is used
   * specifically for the permissions which have "permissionValidOnResource" = false.
   *
   * @param customerUUID
   * @param resourceDefinitionSet
   * @param resourceType
   * @return true if there exists a generic resource definition, else false.
   */
  public boolean hasGenericResourceDefinition(
      UUID customerUUID, Set<ResourceDefinition> resourceDefinitionSet, ResourceType resourceType) {
    for (ResourceDefinition resourceDefinition : resourceDefinitionSet) {
      if (resourceType.equals(resourceDefinition.getResourceType())) {
        // Check that for OTHER resource types, 'allowAll' is false and only one resource UUID is
        // given, which is the customer UUID.
        if (ResourceType.OTHER.equals(resourceType)) {
          if (resourceDefinition.getResourceUUIDSet().size() == 1
              && resourceDefinition.getResourceUUIDSet().contains(customerUUID)) {
            return true;
          }
        }
        // Check that for UNIVERSE, ROLE, USER resource types, allowAll = true.
        else {
          if (resourceDefinition.isAllowAll()) {
            return true;
          }
        }
      }
    }
    return false;
  }

  /**
   * Populates the list of role resource definitions with the system default resource groups for all
   * system defined roles.
   *
   * @param customerUUID
   * @param userUUID
   * @param roleResourceDefinitions
   */
  public void populateSystemRoleResourceGroups(
      UUID customerUUID, UUID userUUID, List<RoleResourceDefinition> roleResourceDefinitions) {
    // If there are no system roles assigned to the role bindings of a user, add ConnectOnly Role to
    // the role bindings.
    if (!roleResourceDefinitions.stream()
        .anyMatch(
            rrd ->
                RoleType.System.equals(
                    Role.getOrBadRequest(customerUUID, rrd.getRoleUUID()).getRoleType()))) {
      Role connectOnlyRole = Role.getOrBadRequest(customerUUID, Users.Role.ConnectOnly.name());
      roleResourceDefinitions.add(new RoleResourceDefinition(connectOnlyRole.getRoleUUID(), null));
    }

    // All the system roles should not have any resource group. They will be populated with default
    // values as they cannot be scoped.
    for (RoleResourceDefinition roleResourceDefinition : roleResourceDefinitions) {
      Role role = Role.getOrBadRequest(customerUUID, roleResourceDefinition.getRoleUUID());
      if (RoleType.System.equals(role.getRoleType())) {
        ResourceGroup systemDefaultResourceGroup =
            ResourceGroup.getSystemDefaultResourceGroup(
                customerUUID, userUUID, Users.Role.valueOf(role.getName()));
        roleResourceDefinition.setResourceGroup(systemDefaultResourceGroup);
      }
    }
  }

  /**
   * This method goes through all existing role bindings with the given role and expands all the
   * resource definitions to allow access to all the resources which have the given generic resource
   * types.
   *
   * @param customerUUID
   * @param role
   * @param genericResourceTypesToExpand
   */
  public void expandRoleBindings(
      UUID customerUUID, Role role, Set<ResourceType> genericResourceTypesToExpand) {
    List<RoleBinding> roleBindingsWithRole = RoleBinding.getAllWithRole(role.getRoleUUID());
    for (RoleBinding roleBinding : roleBindingsWithRole) {
      for (ResourceDefinition resourceDefinition :
          roleBinding.getResourceGroup().getResourceDefinitionSet()) {
        if (genericResourceTypesToExpand.contains(resourceDefinition.getResourceType())) {
          ResourceDefinition oldResourceDefinition = resourceDefinition.clone();
          switch (resourceDefinition.getResourceType()) {
            case OTHER:
              // For "OTHER" resource type, ensure allowAll is false and resource UUID set has only
              // the customer UUID.
              resourceDefinition.setAllowAll(false);
              resourceDefinition.setResourceUUIDSet(new HashSet<>(Arrays.asList(customerUUID)));
              break;
            default:
              // For the rest of the resource types, ensure allowAll is true and empty resource UUID
              // set.
              resourceDefinition.setAllowAll(true);
              resourceDefinition.setResourceUUIDSet(new HashSet<>());
              break;
          }
          log.info(
              "Expanded {} RoleBinding '{}' with user UUID '{}', role UUID '{}', from "
                  + "old resource definition '{}' to new resource definition '{}'.",
              roleBinding.getType(),
              roleBinding.getUuid(),
              roleBinding.getUser().getUuid(),
              roleBinding.getRole().getRoleUUID(),
              oldResourceDefinition,
              resourceDefinition);
        }
      }
      roleBinding.update();
    }
  }

  /**
   * This function returns a set of ResourcePermissionData. This is required for UI to process the
   * resources and permissions available for a user. Whenever this function is called, we compute
   * all the permissions a user has on demand so it is always consistent.
   *
   * @param customerUUID
   * @param userUUID
   * @return
   */
  public Set<ResourcePermissionData> getUserResourcePermissions(
      UUID customerUUID, UUID userUUID, ResourceType resourceType) {
    Set<ResourcePermissionData> resourcePermissions = new HashSet<>();
    Map<ResourceType, Set<Action>> nonResourcePermissionsToAdd = new HashMap<>();
    Map<ResourceType, Map<UUID, Set<Permission>>> userResourcePermissions = new HashMap<>();
    List<RoleBinding> userAssociatedRoleBindings = RoleBinding.getAll(userUUID);
    // Iterate through each role binding for the user.
    for (RoleBinding roleBinding : userAssociatedRoleBindings) {
      // Get all the permissions in that role from the binding.
      Set<Permission> userPermissionsForRole =
          roleBinding.getRole().getPermissionDetails().getPermissionList();
      // Add the permissions that are not valid only on specific resources to the final
      // `resourcePermissions` with resourceUUID as null.
      for (Permission permission : userPermissionsForRole) {
        if (!permissionUtil.getPermissionInfo(permission).isPermissionValidOnResource()) {
          if (!nonResourcePermissionsToAdd.containsKey(permission.getResourceType())) {
            nonResourcePermissionsToAdd.put(permission.getResourceType(), new HashSet<>());
          }
          nonResourcePermissionsToAdd.get(permission.getResourceType()).add(permission.getAction());
        }
      }
      // Iterate through each resource group definition for each role binding.
      for (ResourceDefinition resourceDefinition :
          roleBinding.getResourceGroup().getResourceDefinitionSet()) {
        // Filter out the role binding definitions to check for based on the given resource type.
        // Optimisation step. If resource type is null, get all resource types.
        if (resourceType == null || resourceDefinition.getResourceType().equals(resourceType)) {
          Set<UUID> resourceUUIDsToAdd;
          // In case the allowAll option is selected for that resource definition,
          // then populate all resource UUIDs of that particular resource type for that customer.
          if (resourceDefinition.isAllowAll()) {
            resourceUUIDsToAdd =
                resourceDefinition.getResourceType().getAllResourcesUUID(customerUUID);
          }
          // Else populate only the valid resource UUIDs from the resourceUUIDSet in the resource
          // definition.
          else {
            resourceUUIDsToAdd =
                resourceDefinition.getResourceUUIDSet().stream()
                    .filter(
                        rUUID ->
                            resourceDefinition
                                .getResourceType()
                                .isValidResource(customerUUID, rUUID))
                    .collect(Collectors.toSet());
          }
          // Iterate through each resource UUID
          for (UUID resourceUUID : resourceUUIDsToAdd) {
            // Create a new key value pair if the resource type key isn't already there in the map.
            if (!userResourcePermissions.containsKey(resourceDefinition.getResourceType())) {
              userResourcePermissions.put(resourceDefinition.getResourceType(), new HashMap<>());
            }
            // Create a new inner key value pair if the resource UUID key isn't already there in the
            // map.
            if (!userResourcePermissions
                .get(resourceDefinition.getResourceType())
                .containsKey(resourceUUID)) {
              userResourcePermissions
                  .get(resourceDefinition.getResourceType())
                  .put(resourceUUID, new HashSet<>());
            }
            // Add the permissions to the inner key value pair, at the map path resource type ->
            // resource UUID -> permission set, if it doesn't exist.
            userResourcePermissions
                .get(resourceDefinition.getResourceType())
                .get(resourceUUID)
                .addAll(
                    userPermissionsForRole.stream()
                        .filter(
                            permission ->
                                resourceDefinition
                                    .getResourceType()
                                    .equals(permission.getResourceType()))
                        .collect(Collectors.toSet()));
          }
        }
      }
    }

    // Format the data into the required output class format.
    // Add all the permissions valid on specific resources to outut.
    for (ResourceType resourceTypeToReturn : userResourcePermissions.keySet()) {
      for (UUID resourceUUID : userResourcePermissions.get(resourceTypeToReturn).keySet()) {
        ResourcePermissionData resourcePermissionData =
            new ResourcePermissionData(
                resourceTypeToReturn,
                resourceUUID,
                userResourcePermissions.get(resourceTypeToReturn).get(resourceUUID).stream()
                    .filter(
                        permission ->
                            permissionUtil
                                .getPermissionInfo(permission)
                                .isPermissionValidOnResource())
                    .map(permission -> permission.getAction())
                    .collect(Collectors.toSet()));
        resourcePermissions.add(resourcePermissionData);
      }
    }

    // Add all the non resource specific permissions such as UNIVERSE.CREATE, etc.
    for (ResourceType resourceTypeToReturn : nonResourcePermissionsToAdd.keySet()) {
      if (resourceType == null || resourceType.equals(resourceTypeToReturn)) {
        resourcePermissions.add(
            new ResourcePermissionData(
                resourceTypeToReturn, null, nonResourcePermissionsToAdd.get(resourceTypeToReturn)));
      }
    }
    return resourcePermissions;
  }

  public static void cleanupRoleBindings(ResourceType resourceType, UUID resourceUUID) {
    List<RoleBinding> allRoleBindings = RoleBinding.getAll();
    for (RoleBinding roleBinding : allRoleBindings) {
      ResourceGroup resourceGroup = roleBinding.getResourceGroup();
      // This method removes the UUIDs in place for efficiency.
      ResourceGroup.removeResource(resourceGroup, resourceType, resourceUUID);
      roleBinding.setResourceGroup(resourceGroup);
      roleBinding.update();
    }
  }

  public Set<UUID> getResourceUuids(UUID userUUID, ResourceType resourceType, Action action) {
    Users user = Users.getOrBadRequest(userUUID);
    Customer customer = Customer.get(user.getCustomerUUID());
    boolean useNewRbacAuthz = confGetter.getGlobalConf(GlobalConfKeys.useNewRbacAuthz);
    Set<UUID> resourceUuids = resourceType.getAllResourcesUUID(customer.getUuid());
    if (resourceUuids.isEmpty() || !useNewRbacAuthz) {
      return resourceUuids;
    }

    Set<UUID> roleBindingUUIDs = new HashSet<>();
    List<RoleBinding> userRoleBindings = RoleBinding.fetchRoleBindingsForUser(userUUID);
    for (RoleBinding roleBinding : userRoleBindings) {
      Role role = roleBinding.getRole();
      if (role.getPermissionDetails()
          .getPermissionList()
          .contains(new Permission(resourceType, action))) {
        Set<ResourceDefinition> rDSet =
            roleBinding.getResourceGroup().getResourceDefinitionSet().stream()
                .filter(rD -> resourceType.equals(rD.getResourceType()))
                .collect(Collectors.toSet());
        for (ResourceDefinition rD : rDSet) {
          if (rD.isAllowAll()) {
            return resourceUuids;
          } else {
            roleBindingUUIDs.addAll(rD.getResourceUUIDSet());
          }
        }
      }
    }
    return roleBindingUUIDs;
  }
}
