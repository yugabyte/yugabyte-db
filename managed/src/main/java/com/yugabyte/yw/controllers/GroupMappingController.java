/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.controllers;

import com.google.inject.Inject;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.common.rbac.RoleBindingUtil;
import com.yugabyte.yw.forms.LdapDnToYbaRoleData;
import com.yugabyte.yw.forms.OidcGroupToYbaRolesData;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.GroupMappingInfo;
import com.yugabyte.yw.models.GroupMappingInfo.GroupType;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
import com.yugabyte.yw.models.rbac.Role;
import com.yugabyte.yw.rbac.annotations.AuthzPath;
import com.yugabyte.yw.rbac.annotations.PermissionAttribute;
import com.yugabyte.yw.rbac.annotations.RequiredPermissionOnResource;
import com.yugabyte.yw.rbac.annotations.Resource;
import com.yugabyte.yw.rbac.enums.SourceType;
import io.ebean.annotation.Transactional;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiImplicitParam;
import io.swagger.annotations.ApiImplicitParams;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import play.mvc.Http;
import play.mvc.Result;

@Api(
    value = "LDAP/OIDC Role management",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
@Slf4j
public class GroupMappingController extends AuthenticatedController {

  @Inject TokenAuthenticator tokenAuthenticator;
  @Inject RuntimeConfGetter confGetter;

  private final String oidcAutoCreateKey = "yb.security.oidc_enable_auto_create_users";

  /**
   * Lists the Role - Ldap Group Mapping
   *
   * <p>Returns a Json where the attribute ldapDnToYbaRolePairs has a list of {
   * yugabytePlatformRole: , distinguishedName: } objects
   */
  @ApiOperation(
      value = "List LDAP Mappings",
      notes =
          "<b style=\"color:#ff0000\">Deprecated since YBA version 2024.2.0.0.</b> Please use the"
              + " v2 /auth/group-mappings GET endpoint instead. Note that this API will not return"
              + " the custom roles assigned to groups via the new"
              + " /api/v2/customers/{cUUID}/auth/group-mappings API.",
      response = LdapDnToYbaRoleData.class,
      nickname = "listLdapDnToYbaRoles")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  @YbaApi(visibility = YbaApiVisibility.DEPRECATED, sinceYBAVersion = "2024.2.0.0")
  @Deprecated
  public Result listLdapMappings(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);

    List<GroupMappingInfo> ldapMappings =
        GroupMappingInfo.find
            .query()
            .where()
            .eq("customer_uuid", customerUUID)
            .eq("type", "LDAP")
            .findList();
    LdapDnToYbaRoleData result = new LdapDnToYbaRoleData();
    result.setLdapDnToYbaRolePairs(
        ldapMappings.stream()
            .map(
                lm -> {
                  LdapDnToYbaRoleData.LdapDnYbaRoleDataPair lmd =
                      new LdapDnToYbaRoleData.LdapDnYbaRoleDataPair();
                  lmd.setYbaRole(
                      Users.Role.valueOf(Role.get(customerUUID, lm.getRoleUUID()).getName()));
                  lmd.setDistinguishedName(lm.getIdentifier());
                  return lmd;
                })
            .collect(Collectors.toList()));

    return PlatformResults.withData(result);
  }

  /**
   * Sets the Role - Ldap Group Mapping to the given Json
   *
   * <p>Takes a Json where the attribute ldapDnToYbaRolePairs has a list of { yugabytePlatformRole:
   * , distinguishedName: } objects
   */
  @ApiOperation(
      value = "Set LDAP Mappings",
      notes =
          "<b style=\"color:#ff0000\">Deprecated since YBA version 2024.2.0.0.</b> Please use the"
              + " v2 /auth/group-mappings PUT endpoint instead",
      response = Result.class,
      nickname = "setLdapDnToYbaRoles")
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "ldapMappings",
          value = "New LDAP Mappings to be set",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.LdapDnToYbaRoleData",
          required = true))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(
                resourceType = ResourceType.OTHER,
                action = Action.SUPER_ADMIN_ACTIONS),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  @YbaApi(visibility = YbaApiVisibility.DEPRECATED, sinceYBAVersion = "2024.2.0.0")
  @Transactional
  @Deprecated
  public Result updateLdapMappings(UUID customerUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);

    LdapDnToYbaRoleData data = parseJsonAndValidate(request, LdapDnToYbaRoleData.class);
    data.validate();

    // Preserve existing behaviour.
    // Delete all existing LDAP mappings.
    GroupMappingInfo.find
        .query()
        .where()
        .eq("customer_uuid", customerUUID)
        .eq("type", "LDAP")
        .findList()
        .forEach(gm -> gm.delete());

    data.getLdapDnToYbaRolePairs()
        .forEach(
            ldp -> {
              String distinguishedName = ldp.getDistinguishedName().strip();
              Users.Role role = ldp.getYbaRole();

              GroupMappingInfo entity =
                  GroupMappingInfo.find
                      .query()
                      .where()
                      .eq("customer_uuid", customerUUID)
                      .eq("type", "LDAP")
                      .ieq("identifier", distinguishedName)
                      .findOne();

              if (entity == null) {
                entity =
                    GroupMappingInfo.create(
                        customerUUID,
                        Role.get(customerUUID, role.name()).getRoleUUID(),
                        distinguishedName,
                        GroupType.LDAP);
              } else {
                entity.setRoleUUID(Role.get(customerUUID, role.name()).getRoleUUID());
              }
              entity.save();
              // Add role binding for group if RBAC is enabled.
              if (confGetter.getGlobalConf(GlobalConfKeys.useNewRbacAuthz)) {
                RoleBindingUtil.clearRoleBindingsForGroup(entity);
                RoleBindingUtil.createSystemRoleBindingsForGroup(entity);
              }
            });
    auditService()
        .createAuditEntryWithReqBody(request, Audit.TargetType.GroupMapping, Audit.ActionType.Set);

    return PlatformResults.YBPSuccess.withMessage("LDAP Group Mapping Updated!");
  }

  @YbaApi(
      visibility = YbaApiVisibility.DEPRECATED,
      sinceYBAVersion = "2024.2.0.0",
      runtimeConfig = oidcAutoCreateKey)
  @ApiOperation(
      value = "Set OIDC Mappings",
      response = Result.class,
      notes =
          "<b style=\"color:#ff0000\">Deprecated since YBA version 2024.2.0.0.</b> Please use the"
              + " v2 /auth/group-mappings PUT endpoint instead",
      nickname = "mapOidcGroupToYbaRoles")
  @Transactional
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "oidcMappings",
          value = "New OIDC Mappings to be set",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.OidcGroupToYbaRolesData",
          required = true))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(
                resourceType = ResourceType.OTHER,
                action = Action.SUPER_ADMIN_ACTIONS),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  @Deprecated
  public Result updateOidcMappings(UUID customerUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);

    OidcGroupToYbaRolesData data = parseJsonAndValidate(request, OidcGroupToYbaRolesData.class);
    data.validate();

    boolean isSuperAdmin = tokenAuthenticator.superAdminAuthentication(request);
    if (!isSuperAdmin) {
      throw new PlatformServiceException(BAD_REQUEST, "Only SuperAdmin can update group mappings!");
    }

    data.getOidcGroupToYbaRolesPairs()
        .forEach(
            rolePair -> {
              String groupName = rolePair.getGroupName();

              GroupMappingInfo entity =
                  GroupMappingInfo.find
                      .query()
                      .where()
                      .eq("customer_uuid", customerUUID)
                      .eq("type", "OIDC")
                      .ieq("identifier", groupName)
                      .findOne();

              // update group entry if present or create new entry
              if (entity == null) {
                entity =
                    GroupMappingInfo.create(
                        customerUUID,
                        getUnifiedRole(customerUUID, rolePair.getRoles()),
                        groupName,
                        GroupType.OIDC);
              } else {
                entity.setRoleUUID(getUnifiedRole(customerUUID, rolePair.getRoles()));
              }
              entity.save();
              // Add role binding for group if RBAC is enabled.
              if (confGetter.getGlobalConf(GlobalConfKeys.useNewRbacAuthz)) {
                RoleBindingUtil.clearRoleBindingsForGroup(entity);
                RoleBindingUtil.createSystemRoleBindingsForGroup(entity);
              }
            });

    auditService()
        .createAuditEntryWithReqBody(request, Audit.TargetType.GroupMapping, Audit.ActionType.Set);

    return PlatformResults.YBPSuccess.withMessage("OIDC Group Mapping Updated!");
  }

  @YbaApi(
      visibility = YbaApiVisibility.DEPRECATED,
      sinceYBAVersion = "2024.2.0.0",
      runtimeConfig = oidcAutoCreateKey)
  @ApiOperation(
      value = "List OIDC Group Mappings",
      response = OidcGroupToYbaRolesData.class,
      notes =
          "<b style=\"color:#ff0000\">Deprecated since YBA version 2024.2.0.0.</b> Please use the"
              + " v2 /auth/group-mappings GET endpoint instead. Note that this API will not return"
              + " the custom roles assigned to groups via the new"
              + " /api/v2/customers/{cUUID}/auth/group-mappings API.",
      nickname = "listOidcGroupToYbaRoles")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  @Deprecated
  public Result listOidcMappings(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);

    List<GroupMappingInfo> groupInfoList =
        GroupMappingInfo.find
            .query()
            .where()
            .eq("customer_uuid", customerUUID)
            .eq("type", "OIDC")
            .findList();
    OidcGroupToYbaRolesData result = new OidcGroupToYbaRolesData();
    result.setOidcGroupToYbaRolesPairs(
        groupInfoList.stream()
            .map(
                info -> {
                  OidcGroupToYbaRolesData.OidcGroupToYbaRolesPair orl =
                      new OidcGroupToYbaRolesData.OidcGroupToYbaRolesPair();
                  orl.setGroupName(info.getIdentifier());
                  orl.setRoles(List.of(info.getRoleUUID()));
                  return orl;
                })
            .collect(Collectors.toList()));

    return PlatformResults.withData(result);
  }

  @YbaApi(
      visibility = YbaApiVisibility.DEPRECATED,
      sinceYBAVersion = "2024.2.0.0",
      runtimeConfig = oidcAutoCreateKey)
  @ApiOperation(
      value = "Delete a OIDC group mapping",
      response = PlatformResults.YBPSuccess.class,
      notes =
          "<b style=\"color:#ff0000\">Deprecated since YBA version 2024.2.0.0.</b> Please use the"
              + " v2 /auth/group-mappings/{groupUUID} DELETE endpoint instead")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.DELETE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  @Transactional
  @Deprecated
  public Result deleteOidcGroupMapping(UUID customerUUID, String groupName, Http.Request request) {
    boolean isSuperAdmin = tokenAuthenticator.superAdminAuthentication(request);
    if (!isSuperAdmin) {
      throw new PlatformServiceException(BAD_REQUEST, "Only SuperAdmin can delete group mappings!");
    }
    GroupMappingInfo entity =
        GroupMappingInfo.find
            .query()
            .where()
            .eq("customer_uuid", customerUUID)
            .eq("type", "OIDC")
            .ieq("identifier", groupName)
            .findOne();
    if (entity == null) {
      throw new PlatformServiceException(NOT_FOUND, "No OIDC group found with name: " + groupName);
    }
    entity.delete();
    log.info("Deleted OIDC group with name: " + groupName);
    auditService()
        .createAuditEntry(
            request, Audit.TargetType.GroupMapping, groupName, Audit.ActionType.Delete);
    return PlatformResults.YBPSuccess.empty();
  }

  private UUID getUnifiedRole(UUID cUUID, List<UUID> roles) {
    Users.Role role = Users.Role.ConnectOnly;
    for (UUID roleUUID : roles) {
      Users.Role systemRole = Users.Role.valueOf(Role.get(cUUID, roleUUID).getName());
      role = Users.Role.union(role, systemRole);
    }
    return Role.get(cUUID, role.name()).getRoleUUID();
  }
}
