// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.inject.Inject;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.rbac.PermissionInfo;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.common.rbac.PermissionUtil;
import com.yugabyte.yw.common.rbac.RoleBindingUtil;
import com.yugabyte.yw.common.rbac.RoleUtil;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.forms.RoleBindingFormData;
import com.yugabyte.yw.forms.RoleFormData;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.rbac.Role;
import com.yugabyte.yw.models.rbac.Role.RoleType;
import com.yugabyte.yw.models.rbac.RoleBinding;
import com.yugabyte.yw.models.rbac.RoleBinding.RoleBindingType;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiImplicitParam;
import io.swagger.annotations.ApiImplicitParams;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.ApiParam;
import io.swagger.annotations.Authorization;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.EnumUtils;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;

@Api(
    value = "RBAC management",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
@Slf4j
public class RBACController extends AuthenticatedController {

  private final PermissionUtil permissionUtil;
  private final RoleUtil roleUtil;
  private final RoleBindingUtil roleBindingUtil;

  @Inject
  public RBACController(
      PermissionUtil permissionUtil, RoleUtil roleUtil, RoleBindingUtil roleBindingUtil) {
    this.permissionUtil = permissionUtil;
    this.roleUtil = roleUtil;
    this.roleBindingUtil = roleBindingUtil;
  }

  /**
   * List all the permissions info for each resource type. Optionally can be filtered with a query
   * param 'resourceType' to get specific resource permissions.
   *
   * @param customerUUID
   * @param resourceType
   * @return list of all permissions info
   */
  @ApiOperation(
      value = "List all the permissions available",
      nickname = "listPermissions",
      response = PermissionInfo.class,
      responseContainer = "List")
  public Result listPermissions(
      UUID customerUUID,
      @ApiParam(value = "Optional resource type to filter permission list") String resourceType) {
    // Check if customer exists.
    Customer customer = Customer.getOrBadRequest(customerUUID);

    List<PermissionInfo> permissionInfoList = Collections.emptyList();
    if (EnumUtils.isValidEnum(ResourceType.class, resourceType)) {
      permissionInfoList = permissionUtil.getAllPermissionInfo(ResourceType.valueOf(resourceType));
    } else {
      permissionInfoList = permissionUtil.getAllPermissionInfo();
    }
    return PlatformResults.withData(permissionInfoList);
  }

  /**
   * Get the information of a single Role with its UUID.
   *
   * @param customerUUID
   * @param roleUUID
   * @return the role information
   */
  @ApiOperation(value = "Get a role's information", nickname = "getRole", response = Role.class)
  public Result getRole(UUID customerUUID, UUID roleUUID) {
    // Check if customer exists.
    Customer customer = Customer.getOrBadRequest(customerUUID);
    Role role = Role.getOrBadRequest(customerUUID, roleUUID);
    return PlatformResults.withData(role);
  }

  /**
   * List the information of all roles available. Optionally can be filtered with a query param
   * 'roleType' of Custom or System.
   *
   * @param customerUUID
   * @param roleType
   * @return the list of roles and their information
   */
  @ApiOperation(
      value = "List all the roles available",
      nickname = "listRoles",
      response = Role.class,
      responseContainer = "List")
  public Result listRoles(
      UUID customerUUID,
      @ApiParam(value = "Optional role type to filter roles list") String roleType) {
    // Check if customer exists.
    Customer customer = Customer.getOrBadRequest(customerUUID);

    // Get all roles for the customer if 'roleType' is not a valid enum
    List<Role> roleList = Collections.emptyList();
    if (EnumUtils.isValidEnum(RoleType.class, roleType)) {
      roleList = Role.getAll(customerUUID, RoleType.valueOf(roleType));
    } else {
      roleList = Role.getAll(customerUUID);
    }
    return PlatformResults.withData(roleList);
  }

  /**
   * Create a custom role with a given name and a given set of permissions.
   *
   * @param customerUUID
   * @param request
   * @return the info of the role created
   */
  @ApiOperation(value = "Create a custom role", nickname = "createRole", response = Role.class)
  public Result createRole(UUID customerUUID, Http.Request request) {
    // Check if customer exists.
    Customer customer = Customer.getOrBadRequest(customerUUID);

    // Parse request body.
    JsonNode requestBody = request.body().asJson();
    RoleFormData roleFormData =
        formFactory.getFormDataOrBadRequest(requestBody, RoleFormData.class);

    // Check if role with given name for that customer already exists.
    if (Role.get(customerUUID, roleFormData.name) != null) {
      String errorMsg = "Role with given name already exists: " + roleFormData.name;
      log.error(errorMsg);
      throw new PlatformServiceException(BAD_REQUEST, errorMsg);
    }

    // Create a custom role now.
    Role role =
        roleUtil.createRole(
            customerUUID,
            roleFormData.name,
            roleFormData.description,
            RoleType.Custom,
            roleFormData.permissionList);
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.Role,
            role.getRoleUUID().toString(),
            Audit.ActionType.Create,
            Json.toJson(roleFormData));
    return PlatformResults.withData(role);
  }

  /**
   * Edit the set of permissions of a given custom role.
   *
   * @param customerUUID
   * @param roleUUID
   * @param request
   * @return the info of the edited role
   */
  @ApiOperation(value = "Edit a custom role", nickname = "editRole", response = Role.class)
  public Result editRole(UUID customerUUID, UUID roleUUID, Http.Request request) {
    // Check if customer exists.
    Customer customer = Customer.getOrBadRequest(customerUUID);

    // Parse request body.
    JsonNode requestBody = request.body().asJson();
    RoleFormData roleFormData =
        formFactory.getFormDataOrBadRequest(requestBody, RoleFormData.class);

    // Check if role with given UUID exists for that customer.
    Role role = Role.get(customerUUID, roleUUID);
    if (role == null) {
      String errorMsg =
          String.format(
              "Role with UUID '%s' doesn't exist for customer '%s'.", roleUUID, customerUUID);
      log.error(errorMsg);
      throw new PlatformServiceException(BAD_REQUEST, errorMsg);
    }

    // Ensure we are not modifying system defined roles.
    if (RoleType.System.equals(role.getRoleType())) {
      String errorMsg = "Cannot modify System Role with given UUID: " + roleUUID;
      log.error(errorMsg);
      throw new PlatformServiceException(BAD_REQUEST, errorMsg);
    }

    // Edit the custom role with given description and permissions.
    // Description and set of permissions are the only editable fields in a role.
    role =
        roleUtil.editRole(
            customerUUID, roleUUID, roleFormData.description, roleFormData.permissionList);
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.Role,
            role.getRoleUUID().toString(),
            Audit.ActionType.Edit,
            Json.toJson(roleFormData));
    return PlatformResults.withData(role);
  }

  /**
   * Delete a custom role with a given role UUID.
   *
   * @param customerUUID
   * @param roleUUID
   * @param request
   * @return a success message if deleted properly.
   */
  @ApiOperation(
      value = "Delete a custom role",
      nickname = "deleteRole",
      response = YBPSuccess.class)
  public Result deleteRole(UUID customerUUID, UUID roleUUID, Http.Request request) {
    // Check if customer exists.
    Customer customer = Customer.getOrBadRequest(customerUUID);

    // Check if role with given UUID exists.
    Role role = Role.get(customerUUID, roleUUID);
    if (role == null) {
      String errorMsg =
          String.format(
              "Role with UUID '%s' doesn't exist for customer '%s'.", roleUUID, customerUUID);
      log.error(errorMsg);
      throw new PlatformServiceException(BAD_REQUEST, errorMsg);
    }

    // Ensure we are not deleting system defined roles.
    if (RoleType.System.equals(role.getRoleType())) {
      String errorMsg = "Cannot delete System Role with given UUID: " + roleUUID;
      log.error(errorMsg);
      throw new PlatformServiceException(BAD_REQUEST, errorMsg);
    }

    // Delete the custom role.
    roleUtil.deleteRole(customerUUID, roleUUID);
    auditService()
        .createAuditEntryWithReqBody(
            request, Audit.TargetType.Role, roleUUID.toString(), Audit.ActionType.Delete);
    return YBPSuccess.withMessage(
        String.format(
            "Successfully deleted role with UUID '%s' for customer '%s'", roleUUID, customerUUID));
  }

  @ApiOperation(
      value = "Get all the role bindings available",
      nickname = "getRoleBindings",
      response = RoleBinding.class,
      responseContainer = "Map")
  public Result listRoleBinding(
      UUID customerUUID,
      @ApiParam(value = "Optional user UUID to filter role binding map") UUID userUUID) {
    // Check if customer exists.
    Customer customer = Customer.getOrBadRequest(customerUUID);

    Map<UUID, List<RoleBinding>> roleBindingMap = new HashMap<>();
    if (userUUID != null) {
      // Get the role bindings for the given user if 'userUUID' is not null.
      Users user = Users.getOrBadRequest(customerUUID, userUUID);
      roleBindingMap.put(user.getUuid(), RoleBinding.getAll(user.getUuid()));
    } else {
      // Get all the users for the given customer.
      // Merge all the role bindings for users of the customer.
      List<Users> usersInCustomer = Users.getAll(customerUUID);
      for (Users user : usersInCustomer) {
        roleBindingMap.put(user.getUuid(), RoleBinding.getAll(user.getUuid()));
      }
    }
    return PlatformResults.withData(roleBindingMap);
  }

  @ApiOperation(
      value = "Edit the role bindings of a user",
      nickname = "editRoleBinding",
      response = RoleBinding.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "RoleBindingFormData",
          value = "set role bindings form data",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.RoleBindingFormData",
          required = true))
  public Result setRoleBindings(UUID customerUUID, UUID userUUID, Http.Request request) {
    // Check if customer exists.
    Customer customer = Customer.getOrBadRequest(customerUUID);

    // Check if user UUID exists within the above customer
    Users user = Users.getOrBadRequest(customerUUID, userUUID);

    // Parse request body.
    JsonNode requestBody = request.body().asJson();
    RoleBindingFormData roleBindingFormData =
        formFactory.getFormDataOrBadRequest(requestBody, RoleBindingFormData.class);

    // Validate the roles and resource group definitions.
    roleBindingUtil.validateRoles(userUUID, roleBindingFormData.getRoleResourceDefinitions());
    roleBindingUtil.validateResourceGroups(
        customerUUID, roleBindingFormData.getRoleResourceDefinitions());

    // Delete all existing user role bindings and create new given role bindings.
    List<RoleBinding> createdRoleBindings =
        roleBindingUtil.setUserRoleBindings(
            userUUID, roleBindingFormData.getRoleResourceDefinitions(), RoleBindingType.Custom);

    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.RoleBinding,
            userUUID.toString(),
            Audit.ActionType.Edit,
            Json.toJson(roleBindingFormData));
    return PlatformResults.withData(createdRoleBindings);
  }
}
