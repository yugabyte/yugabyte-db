/*
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.inject.Inject;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.ha.PlatformReplicationManager;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.forms.HAConfigFormData;
import com.yugabyte.yw.forms.HAConfigGetResp;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.PlatformInstance;
import com.yugabyte.yw.models.common.YbaApi;
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
import java.util.List;
import java.util.Objects;
import java.util.Optional;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;
import play.mvc.Results;

@Api(value = "HA", authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
@Slf4j
public class HAController extends AuthenticatedController {
  @Inject private PlatformReplicationManager replicationManager;

  @ApiOperation(
      notes = "Available since YBA version 2.20.0.",
      nickname = "createHAConfig",
      value = "Create high availability config",
      response = HighAvailabilityConfig.class)
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "HAConfigFormRequest",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.HAConfigFormData",
          required = true))
  @YbaApi(visibility = YbaApi.YbaApiVisibility.PUBLIC, sinceYBAVersion = "2.20.0")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(
                resourceType = ResourceType.OTHER,
                action = Action.SUPER_ADMIN_ACTIONS),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public synchronized Result createHAConfig(Http.Request request) {
    if (HighAvailabilityConfig.get().isPresent()) {
      log.error("An HA Config already exists");
      throw new PlatformServiceException(BAD_REQUEST, "An HA Config already exists");
    }
    HAConfigFormData formData = parseJsonAndValidate(request, HAConfigFormData.class);
    HighAvailabilityConfig config =
        HighAvailabilityConfig.create(formData.cluster_key, formData.accept_any_certificate);
    auditService()
        .createAuditEntryWithReqBody(
            request,
            Audit.TargetType.HAConfig,
            Objects.toString(config.getUuid(), null),
            Audit.ActionType.Create);
    return PlatformResults.withData(config);
  }

  @ApiOperation(
      nickname = "getHAConfig",
      value = "Get high availability config",
      response = HAConfigGetResp.class)
  @AuthzPath
  public Result getHAConfig() {
    Optional<HighAvailabilityConfig> config = HighAvailabilityConfig.get();
    if (!config.isPresent()) {
      log.debug("No HA config exists");
      JsonNode jsonMsg = Json.newObject().put("error", "No HA config exists");
      return Results.status(NOT_FOUND, jsonMsg);
    }
    return PlatformResults.withData(new HAConfigGetResp(config.get()));
  }

  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(
                resourceType = ResourceType.OTHER,
                action = Action.SUPER_ADMIN_ACTIONS),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result editHAConfig(UUID configUUID, Http.Request request) {
    HighAvailabilityConfig config =
        HighAvailabilityConfig.maybeGet(configUUID)
            .orElseThrow(() -> new PlatformServiceException(NOT_FOUND, "Invalid config UUID"));
    HAConfigFormData formData = parseJsonAndValidate(request, HAConfigFormData.class);
    // Validate when changing from true to false
    if (!formData.accept_any_certificate && config.getAcceptAnyCertificate()) {
      List<PlatformInstance> remoteInstances = config.getRemoteInstances();
      for (PlatformInstance follower : remoteInstances) {
        if (!replicationManager.testConnection(
            config, follower.getAddress(), false /* acceptAnyCertificate */)) {
          throw new PlatformServiceException(
              INTERNAL_SERVER_ERROR,
              "Error testing certificate connection to remote instance " + follower.getAddress());
        }
      }
    }
    replicationManager.stop();
    try {
      config =
          HighAvailabilityConfig.update(
              configUUID, formData.cluster_key, formData.accept_any_certificate);
      // Save the local HA config before DB record is replaced in backup-restore during promotion.
      replicationManager.saveLocalHighAvailabilityConfig(config);
    } finally {
      replicationManager.start();
    }
    auditService()
        .createAuditEntryWithReqBody(
            request, Audit.TargetType.HAConfig, configUUID.toString(), Audit.ActionType.Edit);
    return PlatformResults.withData(config);
  }

  // TODO: (Daniel) - This could be a task
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(
                resourceType = ResourceType.OTHER,
                action = Action.SUPER_ADMIN_ACTIONS),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result deleteHAConfig(UUID configUUID, Http.Request request) {
    HighAvailabilityConfig config =
        HighAvailabilityConfig.maybeGet(configUUID)
            .orElseThrow(() -> new PlatformServiceException(NOT_FOUND, "Invalid config UUID"));
    Optional<PlatformInstance> localInstance = config.getLocal();
    if (localInstance.isPresent() && !localInstance.get().getIsLeader()) {
      // Revert prometheus from federated mode.
      replicationManager.switchPrometheusToStandalone();
    }
    // Stop the backup schedule.
    replicationManager.stopAndDisable();
    HighAvailabilityConfig.delete(configUUID);
    replicationManager.deleteLocalHighAvailabilityConfig();
    auditService()
        .createAuditEntry(
            request, Audit.TargetType.HAConfig, configUUID.toString(), Audit.ActionType.Delete);
    return ok();
  }

  @ApiOperation(
      notes = "Available since YBA version 2.20.0.",
      value = "Generate cluster key",
      response = JsonNode.class,
      nickname = "generateClusterKey")
  @YbaApi(visibility = YbaApi.YbaApiVisibility.PUBLIC, sinceYBAVersion = "2.20.0")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(
                resourceType = ResourceType.OTHER,
                action = Action.SUPER_ADMIN_ACTIONS),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result generateClusterKey() {
    String clusterKey = HighAvailabilityConfig.generateClusterKey();
    return ok(Json.newObject().put("cluster_key", clusterKey));
  }
}
