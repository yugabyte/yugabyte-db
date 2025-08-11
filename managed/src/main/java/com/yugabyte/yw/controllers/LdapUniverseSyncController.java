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
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.controllers.handlers.LdapUniverseSyncHandler;
import com.yugabyte.yw.forms.LdapUnivSyncFormData;
import com.yugabyte.yw.forms.PlatformResults.YBPTask;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
import com.yugabyte.yw.rbac.annotations.AuthzPath;
import com.yugabyte.yw.rbac.annotations.PermissionAttribute;
import com.yugabyte.yw.rbac.annotations.RequiredPermissionOnResource;
import com.yugabyte.yw.rbac.annotations.Resource;
import com.yugabyte.yw.rbac.enums.SourceType;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiImplicitParam;
import io.swagger.annotations.ApiImplicitParams;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.util.UUID;
import play.mvc.Http;
import play.mvc.Result;

@Api(
    value = "LDAP Universe Sync",
    tags = "LDAP Role management",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class LdapUniverseSyncController extends AuthenticatedController {

  @Inject private LdapUniverseSyncHandler ldapUniverseSyncHandler;

  @Inject private RuntimeConfGetter confGetter;

  /**
   * Performs a universe sync with the custom parameters
   *
   * @param customerUUID
   * @param universeUUID
   * @param request
   */
  @ApiOperation(
      notes = "WARNING: This is a preview API that could change.",
      value = "Perform an LDAP users sync on the universe",
      nickname = "syncLdapUniverse",
      response = YBPTask.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "syncLdapUniverse",
          value = "config to sync universe roles with ldap users",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.LdapUnivSyncFormData",
          required = true))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.UNIVERSE, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.UNIVERSES, sourceType = SourceType.ENDPOINT))
  })
  @YbaApi(visibility = YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.20.1.0")
  public Result syncUniverse(UUID customerUUID, UUID universeUUID, Http.Request request) {

    Customer customer = Customer.getOrBadRequest(customerUUID);
    Universe universe = Universe.getOrBadRequest(universeUUID, customer);
    String errorMsg;

    if (!confGetter.getConfForScope(universe, UniverseConfKeys.ldapUniverseSync)) {
      errorMsg =
          "Please enable the runtime flag: yb.security.ldap.ldap_universe_sync to perform the"
              + " sync.";
      throw new PlatformServiceException(BAD_REQUEST, errorMsg);
    }

    // Parse request body
    LdapUnivSyncFormData ldapUnivSyncFormData =
        formFactory.getFormDataOrBadRequest(request.body().asJson(), LdapUnivSyncFormData.class);

    if (ldapUnivSyncFormData.getTargetApi().equals(LdapUnivSyncFormData.TargetApi.ycql)) {
      if (ldapUnivSyncFormData.getDbuserPassword().isEmpty()) {
        errorMsg =
            String.format(
                "Password is required for the user(YCQL): %s", ldapUnivSyncFormData.getDbUser());
        throw new PlatformServiceException(BAD_REQUEST, errorMsg);
      }
    }

    // call the appropriate handler
    UUID taskUUID =
        ldapUniverseSyncHandler.syncUniv(
            customerUUID, customer, universeUUID, universe, ldapUnivSyncFormData);

    // audit the task
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.Universe,
            universeUUID.toString(),
            Audit.ActionType.LdapUniverseSync,
            taskUUID);

    return new YBPTask(taskUUID, universeUUID).asResult();
  }
}
