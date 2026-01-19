/*
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0
 * .txt
 */

package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.inject.Inject;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.ha.PlatformReplicationManager;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.forms.PlatformBackupFrequencyFormData;
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
import java.io.File;
import java.time.Duration;
import java.util.Collections;
import java.util.List;
import java.util.Optional;
import java.util.UUID;
import java.util.stream.Collectors;
import org.apache.commons.lang3.StringUtils;
import play.mvc.Http;
import play.mvc.Result;

@Api(
    value = "Platform Replication",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class PlatformReplicationController extends AuthenticatedController {

  @Inject private PlatformReplicationManager replicationManager;

  @ApiOperation(
      notes = "Available since YBA version 2.20.0.",
      value = "Start periodic backup",
      response = JsonNode.class,
      nickname = "startPeriodicBackup")
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "PlatformBackupFrequencyRequest",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.PlatformBackupFrequencyFormData",
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
  public Result startPeriodicBackup(UUID configUUID, Http.Request request) {
    JsonNode result =
        HighAvailabilityConfig.doWithLock(
            configUUID,
            config -> {
              PlatformBackupFrequencyFormData formData =
                  parseJsonAndValidate(request, PlatformBackupFrequencyFormData.class);

              if (!config.isLocalLeader()) {
                throw new PlatformServiceException(
                    BAD_REQUEST, "This platform instance is not a leader");
              }
              Duration frequency = Duration.ofMillis(formData.frequency_milliseconds);
              // Restart the backup schedule with the new frequency.
              return replicationManager.setFrequencyStartAndEnable(frequency);
            });
    auditService()
        .createAuditEntry(
            request,
            Audit.TargetType.HABackup,
            configUUID.toString(),
            Audit.ActionType.StartPeriodicBackup);
    return ok(result);
  }

  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(
                resourceType = ResourceType.OTHER,
                action = Action.SUPER_ADMIN_ACTIONS),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result stopPeriodicBackup(UUID configUUID, Http.Request request) {
    JsonNode result =
        HighAvailabilityConfig.doWithLock(
            configUUID, config -> replicationManager.stopAndDisable());
    auditService()
        .createAuditEntry(
            request,
            Audit.TargetType.HABackup,
            configUUID.toString(),
            Audit.ActionType.StopPeriodicBackup);
    return ok(result);
  }

  @AuthzPath
  public Result getBackupInfo(UUID configUUID) {
    HighAvailabilityConfig.maybeGet(configUUID)
        .orElseThrow(() -> new PlatformServiceException(NOT_FOUND, "Invalid config UUID"));
    return ok(replicationManager.getBackupInfo());
  }

  @ApiOperation(
      notes = "Available since YBA version 2.20.0.",
      nickname = "listBackups",
      value = "List backups",
      response = String.class,
      responseContainer = "List")
  @YbaApi(visibility = YbaApi.YbaApiVisibility.PUBLIC, sinceYBAVersion = "2.20.0")
  @AuthzPath
  public Result listBackups(UUID configUUID, String leaderAddr) {
    HighAvailabilityConfig config =
        HighAvailabilityConfig.maybeGet(configUUID)
            .orElseThrow(() -> new PlatformServiceException(NOT_FOUND, "Invalid config UUID"));
    String leader = leaderAddr;
    if (StringUtils.isBlank(leader)) {
      Optional<PlatformInstance> leaderInstance = config.getLeader();
      if (!leaderInstance.isPresent()) {
        throw new PlatformServiceException(BAD_REQUEST, "Could not find leader platform instance");
      }
      leader = leaderInstance.get().getAddress();
    }
    List<String> backups =
        replicationManager.listBackups(Util.toURL(leader)).stream()
            .map(File::getName)
            .sorted(Collections.reverseOrder())
            .collect(Collectors.toList());
    return PlatformResults.withData(backups);
  }
}
