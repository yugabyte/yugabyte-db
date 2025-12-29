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

import com.google.inject.Inject;
import com.yugabyte.yw.common.CustomerTaskManager;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.common.ha.PlatformReplicationManager;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.forms.PlatformInstanceFormData;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.RestorePlatformBackupFormData;
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
import java.net.URL;
import java.util.Objects;
import java.util.Optional;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import play.mvc.Http;
import play.mvc.Result;

@Api(
    value = "Platform Instance",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
@Slf4j
public class PlatformInstanceController extends AuthenticatedController {
  @Inject private PlatformReplicationManager replicationManager;

  @Inject private RuntimeConfGetter runtimeConfGetter;

  @Inject CustomerTaskManager taskManager;

  @ApiOperation(
      notes = "Available since YBA version 2.20.0.",
      value = "Create platform instance",
      response = PlatformInstance.class,
      nickname = "createInstance")
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "PlatformInstanceFormRequest",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.PlatformInstanceFormData",
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
  public Result createInstance(UUID configUUID, Http.Request request) {
    PlatformInstance instance =
        HighAvailabilityConfig.doWithLock(
            configUUID,
            config -> {
              PlatformInstanceFormData formData =
                  parseJsonAndValidate(request, PlatformInstanceFormData.class);
              // Cannot create a remote instance before creating a local instance.
              if (!formData.is_local && !config.getLocal().isPresent()) {
                throw new PlatformServiceException(
                    BAD_REQUEST,
                    "Cannot create a remote platform instance before creating local platform"
                        + " instance");
                // Cannot create a remote instance if local instance is follower.
              }
              if (!formData.is_local && !config.isLocalLeader()) {
                throw new PlatformServiceException(
                    BAD_REQUEST,
                    "Cannot create a remote platform instance on a follower platform instance");
                // Cannot create multiple local platform instances.
              }
              if (formData.is_local && config.getLocal().isPresent()) {
                throw new PlatformServiceException(
                    BAD_REQUEST, "Local platform instance already exists");
                // Cannot create multiple leader platform instances.
              }
              if (formData.is_leader && config.isLocalLeader()) {
                throw new PlatformServiceException(
                    BAD_REQUEST, "Leader platform instance already exists");
              }
              if (!formData.is_local
                  && !replicationManager.testConnection(
                      config, formData.getCleanAddress(), config.getAcceptAnyCertificate())) {
                throw new PlatformServiceException(
                    BAD_REQUEST,
                    "Standby YBA instance is unreachable or hasn't been configured yet");
              }

              PlatformInstance i =
                  PlatformInstance.create(
                      config, formData.getCleanAddress(), formData.is_leader, formData.is_local);

              // Mark this instance as "failed over to" initially since it is a leader instance.
              if (i.getIsLeader()) {
                config.updateLastFailover();
              }

              // Reload from DB.
              config.refresh();
              // Save the local HA config before DB record is replaced in backup-restore during
              // promotion.
              replicationManager.saveLocalHighAvailabilityConfig(config);
              // Sync instances immediately after being added
              replicationManager.oneOffSync();
              return i;
            });
    auditService()
        .createAuditEntry(
            request,
            Audit.TargetType.PlatformInstance,
            Objects.toString(instance.getUuid(), null),
            Audit.ActionType.Create);
    return PlatformResults.withData(instance);
  }

  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(
                resourceType = ResourceType.OTHER,
                action = Action.SUPER_ADMIN_ACTIONS),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result deleteInstance(UUID configUUID, UUID instanceUUID, Http.Request request) {
    HighAvailabilityConfig.doWithLock(
        configUUID,
        config -> {
          if (!config.isLocalLeader()) {
            throw new PlatformServiceException(
                BAD_REQUEST, "Follower platform instance cannot delete platform instances");
          }
          Optional<PlatformInstance> instanceToDelete = PlatformInstance.get(instanceUUID);
          boolean instanceUUIDValid =
              instanceToDelete.isPresent()
                  && config.getInstances().stream().anyMatch(i -> i.getUuid().equals(instanceUUID));
          if (!instanceUUIDValid) {
            throw new PlatformServiceException(NOT_FOUND, "Invalid instance UUID");
          }
          if (instanceToDelete.get().getIsLocal()) {
            throw new PlatformServiceException(BAD_REQUEST, "Cannot delete local instance");
          }
          // Clear metrics for remote instance
          replicationManager.clearMetrics(instanceToDelete.get());
          // Reload from DB.
          config.refresh();
          // Save the local HA config before DB record is replaced in backup-restore during
          // promotion.
          replicationManager.saveLocalHighAvailabilityConfig(config);
          return null;
        });
    auditService()
        .createAuditEntry(
            request,
            Audit.TargetType.PlatformInstance,
            instanceUUID.toString(),
            Audit.ActionType.Delete);
    PlatformInstance.delete(instanceUUID);
    return ok();
  }

  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(
                resourceType = ResourceType.OTHER,
                action = Action.SUPER_ADMIN_ACTIONS),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result getLocal(UUID configUUID) {
    HighAvailabilityConfig config = HighAvailabilityConfig.getOrBadRequest(configUUID);

    Optional<PlatformInstance> localInstance = config.getLocal();
    if (!localInstance.isPresent()) {
      throw new PlatformServiceException(BAD_REQUEST, "No local platform instance for config");
    }

    return PlatformResults.withData(localInstance.get());
  }

  @ApiOperation(
      notes = "Available since YBA version 2.20.0.",
      value = "Promote platform instance",
      nickname = "promoteInstance")
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "PlatformBackupRestoreRequest",
          paramType = "body",
          dataType = "com.yugabyte.yw.forms.RestorePlatformBackupFormData",
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
  public Result promoteInstance(
      UUID configUUID,
      UUID instanceUUID,
      String currLeaderAddr,
      boolean force,
      Http.Request request)
      throws java.net.MalformedURLException {
    if (Util.hasYBAShutdownStarted()) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot promote while shutdown is in progress");
    }
    HighAvailabilityConfig.doWithLock(
        configUUID,
        config -> {
          Optional<PlatformInstance> instance = PlatformInstance.get(instanceUUID);

          boolean instanceUUIDValid =
              instance.isPresent()
                  && config.getInstances().stream().anyMatch(i -> i.getUuid().equals(instanceUUID));

          if (!instanceUUIDValid) {
            throw new PlatformServiceException(NOT_FOUND, "Invalid platform instance UUID");
          }

          if (!instance.get().getIsLocal()) {
            throw new PlatformServiceException(
                BAD_REQUEST, "Cannot promote a remote platform instance");
          }

          if (instance.get().getIsLeader()) {
            throw new PlatformServiceException(
                BAD_REQUEST, "Cannot promote a leader platform instance");
          }

          RestorePlatformBackupFormData formData =
              parseJsonAndValidate(request, RestorePlatformBackupFormData.class);

          String leader = currLeaderAddr;
          if (StringUtils.isBlank(leader)) {
            Optional<PlatformInstance> leaderInstance = config.getLeader();
            if (!leaderInstance.isPresent()) {
              throw new PlatformServiceException(BAD_REQUEST, "Could not find leader instance");
            }
            leader = leaderInstance.get().getAddress();
          }
          // Validate we can reach current leader if force is not set.
          if (force) {
            log.warn("Connection test to the current leader is skipped");
          } else if (!replicationManager.testConnection(
              config, leader, config.getAcceptAnyCertificate())) {
            throw new PlatformServiceException(
                BAD_REQUEST, "Could not connect to current leader and force parameter not set.");
          }

          URL leaderUrl = Util.toURL(leader);
          // Make sure the backup file provided exists.
          File backup =
              replicationManager.listBackups(leaderUrl).stream()
                  .filter(f -> f.getName().equals(formData.backup_file))
                  .findFirst()
                  .orElseThrow(
                      () ->
                          new PlatformServiceException(
                              BAD_REQUEST,
                              "Could not find backup file from " + leaderUrl.getHost()));

          // Cache local instance address before restore so we can query to new corresponding model.
          String localInstanceAddr = instance.get().getAddress();

          // Backward compatibility if the config is not present.
          // This conditional check is to avoid incorrect config if this promoteInstance is run
          // again
          // after a promotion failure before isLocal is applied after a restore.
          if (replicationManager
              .maybeGetLocalHighAvailabilityConfig(config.getClusterKey())
              .isEmpty()) {
            // Save the local HA config before DB record is replaced in backup-restore during
            // promotion.
            replicationManager.saveLocalHighAvailabilityConfig(config);
          }

          log.info("Restoring YBA DB using backup {}", backup);
          // Restore the backup.
          // For K8s, restore Yba DB inline instead of restoring after restart.
          if (!replicationManager.restoreBackup(backup, false /* k8sRestoreYbaDbOnRestart */)) {
            throw new PlatformServiceException(BAD_REQUEST, "Could not restore backup");
          }
          // Handle any incomplete tasks that may be leftover from the backup that was restored.
          taskManager.handleAllPendingTasks();

          // Promote the local instance.
          PlatformInstance.getByAddress(localInstanceAddr)
              .ifPresent(replicationManager::promoteLocalInstance);
          return null;
        });
    auditService()
        .createAuditEntry(
            request,
            Audit.TargetType.PlatformInstance,
            instanceUUID.toString(),
            Audit.ActionType.Promote);

    if (runtimeConfGetter.getGlobalConf(GlobalConfKeys.haShutdownLevel) > 0) {
      Util.shutdownYbaProcess(5);
    }
    return ok();
  }
}
