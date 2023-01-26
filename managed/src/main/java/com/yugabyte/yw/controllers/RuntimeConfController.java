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
import com.yugabyte.yw.common.config.ConfKeyInfo;
import com.yugabyte.yw.common.config.RuntimeConfService;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPSuccess;
import com.yugabyte.yw.forms.RuntimeConfigFormData;
import com.yugabyte.yw.forms.RuntimeConfigFormData.ScopedConfig;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import io.ebean.annotation.Transactional;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiImplicitParam;
import io.swagger.annotations.ApiImplicitParams;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.util.Map;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
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
  public Result listKeys() {
    return mutableKeysResult;
  }

  @ApiOperation(
      value = "List mutable keys",
      response = ConfKeyInfo.class,
      responseContainer = "List",
      notes = "List all the mutable runtime config keys with metadata")
  public Result listKeyInfo() {
    return PlatformResults.withData(keyMetaData.values());
  }

  @ApiOperation(
      value = "List configuration scopes",
      response = RuntimeConfigFormData.class,
      notes =
          "Lists all (including empty scopes) runtime config scopes for current customer. "
              + "List includes the Global scope that spans multiple customers, scope for customer "
              + "specific overrides for current customer and one scope each for each universe and "
              + "provider.")
  public Result listScopes(UUID customerUUID) {
    boolean isSuperAdmin = tokenAuthenticator.superAdminAuthentication(ctx());
    return PlatformResults.withData(
        runtimeConfService.listScopes(Customer.getOrBadRequest(customerUUID), isSuperAdmin));
  }

  @ApiOperation(
      value = "List configuration entries for a scope",
      response = ScopedConfig.class,
      notes = "Lists all runtime config entries for a given scope for current customer.")
  public Result getConfig(UUID customerUUID, UUID scopeUUID, boolean includeInherited) {
    return PlatformResults.withData(
        runtimeConfService.getConfig(customerUUID, scopeUUID, includeInherited));
  }

  @ApiOperation(
      value = "Get a configuration key",
      nickname = "getConfigurationKey",
      response = String.class,
      produces = "text/plain")
  public Result getKey(UUID customerUUID, UUID scopeUUID, String path) {
    return ok(runtimeConfService.getKeyOrBadRequest(customerUUID, scopeUUID, path));
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
  @Transactional
  public Result setKey(UUID customerUUID, UUID scopeUUID, String path) {
    String contentType = request().contentType().orElse("UNKNOWN");
    if (!contentType.equals("text/plain")) {
      throw new PlatformServiceException(
          UNSUPPORTED_MEDIA_TYPE, "Accepts: text/plain but content-type: " + contentType);
    }
    String newValue = request().body().asText();
    if (newValue == null) {
      throw new PlatformServiceException(BAD_REQUEST, "Cannot set null value");
    }
    verifyGlobalScope(scopeUUID);
    runtimeConfService.setKey(customerUUID, scopeUUID, path, newValue);
    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.RuntimeConfigKey,
            scopeUUID.toString() + ":" + path,
            Audit.ActionType.Update,
            request().body().asJson());
    return YBPSuccess.empty();
  }

  @ApiOperation(value = "Delete a configuration key", response = YBPSuccess.class)
  @Transactional
  public Result deleteKey(UUID customerUUID, UUID scopeUUID, String path) {
    verifyGlobalScope(scopeUUID);
    runtimeConfService.deleteKey(customerUUID, scopeUUID, path);
    auditService()
        .createAuditEntryWithReqBody(
            ctx(),
            Audit.TargetType.RuntimeConfigKey,
            scopeUUID.toString() + ":" + path,
            Audit.ActionType.Delete);
    return YBPSuccess.empty();
  }

  private void verifyGlobalScope(UUID scopeUUID) {
    if (scopeUUID == GLOBAL_SCOPE_UUID) {
      boolean isSuperAdmin = tokenAuthenticator.superAdminAuthentication(ctx());
      if (!isSuperAdmin) {
        throw new PlatformServiceException(FORBIDDEN, "Only superadmin can modify global scope");
      }
    }
  }
}
