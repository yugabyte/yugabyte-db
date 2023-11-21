/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.models.ScopedRuntimeConfig.GLOBAL_SCOPE_UUID;

import com.google.inject.Inject;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.ConfKeyInfo;
import com.yugabyte.yw.common.config.RuntimeConfService;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.forms.RuntimeConfigFormData;
import com.yugabyte.yw.forms.RuntimeConfigFormData.ConfigEntry;
import com.yugabyte.yw.forms.RuntimeConfigFormData.ScopedConfig;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
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
import java.util.Map;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import play.mvc.Http;
import play.mvc.Result;

@Api(
    value = "Runtime configuration",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
@Slf4j
public class RuntimeConfController extends AuthenticatedController {
  private final SettableRuntimeConfigFactory settableRuntimeConfigFactory;
  private final Result mutableKeysResult;

  private final RuntimeConfService runtimeConfService;

  private final TokenAuthenticator tokenAuthenticator;

  private final Map<String, ConfKeyInfo<?>> keyMetaData;

  @Inject
  public RuntimeConfController(
      SettableRuntimeConfigFactory settableRuntimeConfigFactory,
      RuntimeConfService runtimeConfService,
      TokenAuthenticator tokenAuthenticator,
      Map<String, ConfKeyInfo<?>> keyMetaData) {
    this.settableRuntimeConfigFactory = settableRuntimeConfigFactory;
    this.runtimeConfService = runtimeConfService;
    this.mutableKeysResult = PlatformResults.withData(runtimeConfService.getMutableKeys());
    this.tokenAuthenticator = tokenAuthenticator;
    this.keyMetaData = keyMetaData;
  }

  @ApiOperation(
      value = "List mutable keys",
      response = String.class,
      responseContainer = "List",
      notes = "List all the mutable runtime config keys")
  @AuthzPath
  public Result listKeys() {
    return mutableKeysResult;
  }

  @ApiOperation(
      value = "List mutable keys",
      response = ConfKeyInfo.class,
      responseContainer = "List",
      notes = "List all the mutable runtime config keys with metadata")
  @AuthzPath
  public Result listKeyInfo() {
    return PlatformResults.withData(keyMetaData.values());
  }

  @YbaApi(visibility = YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.20.1.0")
  @ApiOperation(
      value = "YbaApi Internal. List feature flags",
      response = ConfigEntry.class,
      responseContainer = "List",
      notes = "List all the feature flag runtime config keys and their values.")
  @AuthzPath
  public Result listFeatureFlags() {
    return PlatformResults.withData(runtimeConfService.getFeatureFlagEntries());
  }

  @ApiOperation(
      value = "List configuration scopes",
      response = RuntimeConfigFormData.class,
      notes =
          "Lists all (including empty scopes) runtime config scopes for current customer. "
              + "List includes the Global scope that spans multiple customers, scope for customer "
              + "specific overrides for current customer and one scope each for each universe and "
              + "provider.")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result listScopes(UUID customerUUID, Http.Request request) {
    boolean isSuperAdmin = tokenAuthenticator.superAdminAuthentication(request);
    return PlatformResults.withData(
        runtimeConfService.listScopes(Customer.getOrBadRequest(customerUUID), isSuperAdmin));
  }

  @ApiOperation(
      value = "List configuration entries for a scope",
      response = ScopedConfig.class,
      notes = "Lists all runtime config entries for a given scope for current customer.")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result getConfig(
      UUID customerUUID, UUID scopeUUID, boolean includeInherited, Http.Request request) {
    boolean isSuperAdmin = tokenAuthenticator.superAdminAuthentication(request);
    return PlatformResults.withData(
        runtimeConfService.getConfig(customerUUID, scopeUUID, includeInherited, isSuperAdmin));
  }

  @ApiOperation(
      value = "Get a configuration key",
      nickname = "getConfigurationKey",
      response = String.class,
      produces = "text/plain")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result getKey(UUID customerUUID, UUID scopeUUID, String path, Http.Request request) {
    boolean isSuperAdmin = tokenAuthenticator.superAdminAuthentication(request);
    return ok(runtimeConfService.getKeyOrBadRequest(customerUUID, scopeUUID, path, isSuperAdmin));
  }

  @ApiOperation(
      value = "Update a configuration key",
      consumes = "text/plain",
      response = YBPSuccess.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "newValue",
          value = "New value for config key",
          paramType = "body",
          dataType = "java.lang.String",
          required = true))
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.UPDATE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  @Transactional
  public Result setKey(UUID customerUUID, UUID scopeUUID, String path, Http.Request request) {
    String contentType = request.contentType().orElse("UNKNOWN");
    if (!contentType.equals("text/plain")) {
      throw new PlatformServiceException(
          UNSUPPORTED_MEDIA_TYPE, "Accepts: text/plain but content-type: " + contentType);
    }
    String newValue = request.body().asText();
    if (newValue == null) {
      newValue = "";
    }
    verifyGlobalScope(scopeUUID, request);
    boolean isSuperAdmin = tokenAuthenticator.superAdminAuthentication(request);
    runtimeConfService.setKey(customerUUID, scopeUUID, path, newValue, isSuperAdmin);
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.RuntimeConfigKey,
            scopeUUID.toString() + ":" + path,
            Audit.ActionType.Update);
    return YBPSuccess.empty();
  }

  @ApiOperation(value = "Delete a configuration key", response = YBPSuccess.class)
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.DELETE),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  @Transactional
  public Result deleteKey(UUID customerUUID, UUID scopeUUID, String path, Http.Request request) {
    boolean isSuperAdmin = tokenAuthenticator.superAdminAuthentication(request);
    verifyGlobalScope(scopeUUID, request);
    runtimeConfService.deleteKey(customerUUID, scopeUUID, path, isSuperAdmin);
    auditService()
        .createAuditEntry(
            request,
            Audit.TargetType.RuntimeConfigKey,
            scopeUUID.toString() + ":" + path,
            Audit.ActionType.Delete);
    return YBPSuccess.empty();
  }

  private void verifyGlobalScope(UUID scopeUUID, Http.Request request) {
    if (scopeUUID == GLOBAL_SCOPE_UUID) {
      boolean isSuperAdmin = tokenAuthenticator.superAdminAuthentication(request);
      if (!isSuperAdmin) {
        throw new PlatformServiceException(FORBIDDEN, "Only superadmin can modify global scope");
      }
    }
  }
}
