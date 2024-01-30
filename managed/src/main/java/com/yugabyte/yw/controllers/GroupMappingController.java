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
import com.yugabyte.yw.forms.LdapDnToYbaRoleData;
import com.yugabyte.yw.forms.OidcGroupToYbaRolesData;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.LdapDnToYbaRole;
import com.yugabyte.yw.models.OidcGroupToYbaRoles;
import com.yugabyte.yw.models.Users.Role;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
import com.yugabyte.yw.rbac.annotations.AuthzPath;
import com.yugabyte.yw.rbac.annotations.PermissionAttribute;
import com.yugabyte.yw.rbac.annotations.RequiredPermissionOnResource;
import com.yugabyte.yw.rbac.annotations.Resource;
import com.yugabyte.yw.rbac.enums.SourceType;
import io.ebean.DB;
import io.ebean.annotation.Transactional;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiImplicitParam;
import io.swagger.annotations.ApiImplicitParams;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
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

  /**
   * Lists the Role - Ldap Group Mapping
   *
   * <p>Returns a Json where the attribute ldapDnToYbaRolePairs has a list of {
   * yugabytePlatformRole: , distinguishedName: } objects
   */
  @ApiOperation(
      value = "List LDAP Mappings",
      notes = "Available since YBA version 2.18.1.0",
      response = LdapDnToYbaRoleData.class,
      nickname = "listLdapDnToYbaRoles")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  @YbaApi(visibility = YbaApiVisibility.PUBLIC, sinceYBAVersion = "2.18.1.0")
  public Result listLdapMappings(UUID customerUUID) {
    Customer.getOrBadRequest(customerUUID);

    List<LdapDnToYbaRole> ldapMappings =
        LdapDnToYbaRole.find.query().where().eq("customer_uuid", customerUUID).findList();
    LdapDnToYbaRoleData result = new LdapDnToYbaRoleData();
    result.setLdapDnToYbaRolePairs(
        ldapMappings.stream()
            .map(
                lm -> {
                  LdapDnToYbaRoleData.LdapDnYbaRoleDataPair lmd =
                      new LdapDnToYbaRoleData.LdapDnYbaRoleDataPair();
                  lmd.setYbaRole(lm.ybaRole);
                  lmd.setDistinguishedName(lm.distinguishedName);
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
      notes = "Available since YBA version 2.18.1.0",
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
  @YbaApi(visibility = YbaApiVisibility.PUBLIC, sinceYBAVersion = "2.18.1.0")
  public Result updateLdapMappings(UUID customerUUID, Http.Request request) {
    Customer.getOrBadRequest(customerUUID);

    LdapDnToYbaRoleData data = parseJsonAndValidate(request, LdapDnToYbaRoleData.class);
    data.validate();

    DB.getDefault().beginTransaction();

    LdapDnToYbaRole.find.query().delete();

    Map<String, LdapDnToYbaRole> ldapMappingsMap = new HashMap<>();

    data.getLdapDnToYbaRolePairs()
        .forEach(
            ldp -> {
              String distinguishedName = ldp.getDistinguishedName();
              Role role = ldp.getYbaRole();

              if (!ldapMappingsMap.containsKey(distinguishedName)) {
                LdapDnToYbaRole ldapMapping = new LdapDnToYbaRole();
                ldapMapping.distinguishedName = distinguishedName;
                ldapMapping.ybaRole = null;
                ldapMapping.customerUUID = customerUUID;
                ldapMappingsMap.put(distinguishedName, ldapMapping);
              }

              Role dnRole = ldapMappingsMap.get(distinguishedName).ybaRole;
              ldapMappingsMap.get(distinguishedName).ybaRole = Role.union(role, dnRole);
            });

    DB.getDefault().saveAll(ldapMappingsMap.values());

    DB.getDefault().commitTransaction();

    DB.getDefault().endTransaction();

    auditService().createAuditEntry(request, request.body().asJson(), Audit.ActionType.Set);

    return PlatformResults.YBPSuccess.withMessage("LDAP Group Mapping Updated!");
  }

  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.21.0.0")
  @ApiOperation(
      value = "Set OIDC Mappings",
      response = Result.class,
      notes =
          "WARNING: This is a preview API that could change,Available since YBA version 2.21.0.0",
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
  public Result updateOidcMappings(UUID customerUUID, Http.Request request) {
    verifyOidcAutoCreateKey();
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
              OidcGroupToYbaRoles entity =
                  OidcGroupToYbaRoles.find.query().where().eq("group_name", groupName).findOne();

              // update group entry if present or create new entry
              if (entity == null) {
                entity = OidcGroupToYbaRoles.create(customerUUID, groupName, rolePair.getRoles());
              } else {
                entity.setYbaRoles(rolePair.getRoles());
              }
              entity.save();
            });

    auditService()
        .createAuditEntryWithReqBody(
            request, Audit.TargetType.OIDCGroupMapping, Audit.ActionType.Set);

    return PlatformResults.YBPSuccess.withMessage("OIDC Group Mapping Updated!");
  }

  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.21.0.0")
  @ApiOperation(
      value = "List OIDC Group Mappings",
      response = OidcGroupToYbaRolesData.class,
      notes =
          "WARNING: This is a preview API that could change,Available since YBA version 2.21.0.0",
      nickname = "listOidcGroupToYbaRoles")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result listOidcMappings(UUID customerUUID) {
    if (!confGetter.getGlobalConf(GlobalConfKeys.enableOidcAutoCreateUser)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "yb.security.oidc_enable_auto_create_users flag is not enabled.");
    }
    Customer.getOrBadRequest(customerUUID);

    List<OidcGroupToYbaRoles> oidcMappings =
        OidcGroupToYbaRoles.find.query().where().eq("customer_uuid", customerUUID).findList();
    OidcGroupToYbaRolesData result = new OidcGroupToYbaRolesData();
    result.setOidcGroupToYbaRolesPairs(
        oidcMappings.stream()
            .map(
                om -> {
                  OidcGroupToYbaRolesData.OidcGroupToYbaRolesPair orl =
                      new OidcGroupToYbaRolesData.OidcGroupToYbaRolesPair();
                  orl.setGroupName(om.getGroupName());
                  orl.setRoles(om.getYbaRoles());
                  return orl;
                })
            .collect(Collectors.toList()));

    return PlatformResults.withData(result);
  }

  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.21.0.0")
  @ApiOperation(
      value = "Delete a OIDC group mapping",
      response = PlatformResults.YBPSuccess.class,
      notes =
          "WARNING: This is a preview API that could change,Available since YBA version 2.21.0.0")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.DELETE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  @Transactional
  public Result deleteOidcGroupMapping(UUID customerUUID, String groupName, Http.Request request) {
    verifyOidcAutoCreateKey();
    boolean isSuperAdmin = tokenAuthenticator.superAdminAuthentication(request);
    if (!isSuperAdmin) {
      throw new PlatformServiceException(BAD_REQUEST, "Only SuperAdmin can delete group mappings!");
    }
    OidcGroupToYbaRoles entity =
        OidcGroupToYbaRoles.find
            .query()
            .where()
            .eq("customer_uuid", customerUUID)
            .eq("group_name", groupName)
            .findOne();
    if (entity == null) {
      throw new PlatformServiceException(NOT_FOUND, "No OIDC group found with name: " + groupName);
    }
    entity.delete();
    log.info("Deleted OIDC group with name: " + groupName);
    auditService()
        .createAuditEntry(
            request, Audit.TargetType.OIDCGroupMapping, groupName, Audit.ActionType.Delete);
    return PlatformResults.YBPSuccess.empty();
  }

  private void verifyOidcAutoCreateKey() {
    if (!confGetter.getGlobalConf(GlobalConfKeys.enableOidcAutoCreateUser)) {
      throw new PlatformServiceException(
          BAD_REQUEST, "yb.security.oidc_enable_auto_create_users flag is not enabled.");
    }
  }
}
