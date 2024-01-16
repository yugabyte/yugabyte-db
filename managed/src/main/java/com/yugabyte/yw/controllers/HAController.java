/*
 * Copyright 2021 YugaByte, Inc. and Contributors
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
import com.yugabyte.yw.common.ApiResponse;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.ha.PlatformReplicationManager;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.forms.HAConfigFormData;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.PlatformInstance;
import com.yugabyte.yw.rbac.annotations.AuthzPath;
import com.yugabyte.yw.rbac.annotations.PermissionAttribute;
import com.yugabyte.yw.rbac.annotations.RequiredPermissionOnResource;
import com.yugabyte.yw.rbac.annotations.Resource;
import com.yugabyte.yw.rbac.enums.SourceType;
import java.util.Objects;
import java.util.Optional;
import java.util.UUID;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.Form;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;
import play.mvc.Results;

public class HAController extends AuthenticatedController {

  public static final Logger LOG = LoggerFactory.getLogger(HAController.class);

  @Inject private PlatformReplicationManager replicationManager;

  // TODO: (Daniel) - This could be a task
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(
                resourceType = ResourceType.OTHER,
                action = Action.SUPER_ADMIN_ACTIONS),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result createHAConfig(Http.Request request) {
    try {
      Form<HAConfigFormData> formData =
          formFactory.getFormDataOrBadRequest(request, HAConfigFormData.class);

      if (HighAvailabilityConfig.get().isPresent()) {
        LOG.error("An HA Config already exists");

        return ApiResponse.error(BAD_REQUEST, "An HA Config already exists");
      }

      HighAvailabilityConfig config = HighAvailabilityConfig.create(formData.get().cluster_key);
      auditService()
          .createAuditEntryWithReqBody(
              request,
              Audit.TargetType.HAConfig,
              Objects.toString(config.getUuid(), null),
              Audit.ActionType.Create);
      return PlatformResults.withData(config);
    } catch (Exception e) {
      LOG.error("Error creating HA config", e);

      return ApiResponse.error(INTERNAL_SERVER_ERROR, "Error creating HA config");
    }
  }

  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(
                resourceType = ResourceType.OTHER,
                action = Action.SUPER_ADMIN_ACTIONS),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result getHAConfig() {
    try {
      Optional<HighAvailabilityConfig> config = HighAvailabilityConfig.get();

      if (!config.isPresent()) {
        LOG.debug("No HA config exists");

        JsonNode jsonMsg = Json.newObject().put("error", "No HA config exists");
        return Results.status(NOT_FOUND, jsonMsg);
      }

      return PlatformResults.withData(config.get());
    } catch (Exception e) {
      LOG.error("Error retrieving HA config", e);

      return ApiResponse.error(INTERNAL_SERVER_ERROR, "Error retrieving HA config");
    }
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
    try {
      Optional<HighAvailabilityConfig> config = HighAvailabilityConfig.get(configUUID);
      if (!config.isPresent()) {
        return ApiResponse.error(NOT_FOUND, "Invalid config UUID");
      }

      Form<HAConfigFormData> formData =
          formFactory.getFormDataOrBadRequest(request, HAConfigFormData.class);

      replicationManager.stop();
      HighAvailabilityConfig.update(config.get(), formData.get().cluster_key);
      replicationManager.start();
      auditService()
          .createAuditEntryWithReqBody(
              request, Audit.TargetType.HAConfig, configUUID.toString(), Audit.ActionType.Edit);
      return PlatformResults.withData(config);
    } catch (Exception e) {
      LOG.error("Error updating cluster key", e);

      return ApiResponse.error(INTERNAL_SERVER_ERROR, "Error updating cluster key");
    }
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
    try {
      Optional<HighAvailabilityConfig> config = HighAvailabilityConfig.get(configUUID);
      if (!config.isPresent()) {
        return ApiResponse.error(NOT_FOUND, "Invalid config UUID");
      }

      Optional<PlatformInstance> localInstance = config.get().getLocal();
      if (localInstance.isPresent() && !localInstance.get().getIsLeader()) {
        // Revert prometheus from federated mode.
        replicationManager.switchPrometheusToStandalone();
      }

      // Stop the backup schedule.
      replicationManager.stopAndDisable();
      HighAvailabilityConfig.delete(configUUID);
      auditService()
          .createAuditEntry(
              request, Audit.TargetType.HAConfig, configUUID.toString(), Audit.ActionType.Delete);
      return ok();
    } catch (Exception e) {
      LOG.error("Error deleting HA config", e);

      return ApiResponse.error(INTERNAL_SERVER_ERROR, "Error deleting HA config");
    }
  }

  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(
                resourceType = ResourceType.OTHER,
                action = Action.SUPER_ADMIN_ACTIONS),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result generateClusterKey() {
    try {
      String clusterKey = HighAvailabilityConfig.generateClusterKey();
      return ok(Json.newObject().put("cluster_key", clusterKey));
    } catch (Exception e) {
      LOG.error("Error generating cluster key", e);

      return ApiResponse.error(INTERNAL_SERVER_ERROR, "Error generating cluster key");
    }
  }
}
