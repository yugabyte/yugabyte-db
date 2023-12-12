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

import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.forms.LdapDnToYbaRoleData;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.LdapDnToYbaRole;
import com.yugabyte.yw.models.Users.Role;
import com.yugabyte.yw.rbac.annotations.AuthzPath;
import com.yugabyte.yw.rbac.annotations.PermissionAttribute;
import com.yugabyte.yw.rbac.annotations.RequiredPermissionOnResource;
import com.yugabyte.yw.rbac.annotations.Resource;
import com.yugabyte.yw.rbac.enums.SourceType;
import io.ebean.DB;
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
import play.mvc.Http;
import play.mvc.Result;

@Api(
    value = "LDAP Role management",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class LdapDnToYbaRoleController extends AuthenticatedController {

  /**
   * Lists the Role - Ldap Group Mapping
   *
   * <p>Returns a Json where the attribute ldapDnToYbaRolePairs has a list of {
   * yugabytePlatformRole: , distinguishedName: } objects
   */
  @ApiOperation(
      value = "List LDAP Mappings",
      notes = "List LDAP Mappings",
      response = LdapDnToYbaRoleData.class,
      nickname = "listLdapDnToYbaRoles")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result list(UUID customerUUID) {
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
      notes = "Set LDAP Mappings",
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
  public Result update(UUID customerUUID, Http.Request request) {
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
}
