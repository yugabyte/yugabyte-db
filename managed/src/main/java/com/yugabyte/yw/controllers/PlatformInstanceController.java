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
import com.yugabyte.yw.rbac.annotations.AuthzPath;
import com.yugabyte.yw.rbac.annotations.PermissionAttribute;
import com.yugabyte.yw.rbac.annotations.RequiredPermissionOnResource;
import com.yugabyte.yw.rbac.annotations.Resource;
import com.yugabyte.yw.rbac.enums.SourceType;
import java.io.File;
import java.net.URL;
import java.util.Objects;
import java.util.Optional;
import java.util.UUID;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.mvc.Http;
import play.mvc.Result;

public class PlatformInstanceController extends AuthenticatedController {

  public static final Logger LOG = LoggerFactory.getLogger(PlatformInstanceController.class);

  @Inject private PlatformReplicationManager replicationManager;

  @Inject private RuntimeConfGetter runtimeConfGetter;

  @Inject CustomerTaskManager taskManager;

  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(
                resourceType = ResourceType.OTHER,
                action = Action.SUPER_ADMIN_ACTIONS),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result createInstance(UUID configUUID, Http.Request request) {
    Optional<HighAvailabilityConfig> config = HighAvailabilityConfig.getOrBadRequest(configUUID);

    PlatformInstanceFormData formData =
        parseJsonAndValidate(request, PlatformInstanceFormData.class);
    // Cannot create a remote instance before creating a local instance.
    if (!formData.is_local && !config.get().getLocal().isPresent()) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          "Cannot create a remote platform instance before creating local platform instance");
      // Cannot create a remote instance if local instance is follower.
    } else if (!formData.is_local && !config.get().isLocalLeader()) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Cannot create a remote platform instance on a follower platform instance");
      // Cannot create multiple local platform instances.
    } else if (formData.is_local && config.get().getLocal().isPresent()) {
      throw new PlatformServiceException(BAD_REQUEST, "Local platform instance already exists");
      // Cannot create multiple leader platform instances.
    } else if (formData.is_leader && config.get().isLocalLeader()) {
      throw new PlatformServiceException(BAD_REQUEST, "Leader platform instance already exists");
    } else if (!formData.is_local
        && !replicationManager.testConnection(
            config.get(), formData.getCleanAddress(), config.get().getAcceptAnyCertificate())) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Standby YBA instance is unreachable or hasn't been configured yet");
    }

    PlatformInstance instance =
        PlatformInstance.create(
            config.get(), formData.getCleanAddress(), formData.is_leader, formData.is_local);

    // Mark this instance as "failed over to" initially since it is a leader instance.
    if (instance.getIsLeader()) {
      config.get().updateLastFailover();
    }

    // Sync instances immediately after being added
    replicationManager.oneOffSync();

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
    Optional<HighAvailabilityConfig> config = HighAvailabilityConfig.getOrBadRequest(configUUID);

    Optional<PlatformInstance> instanceToDelete = PlatformInstance.get(instanceUUID);

    boolean instanceUUIDValid =
        instanceToDelete.isPresent()
            && config.get().getInstances().stream().anyMatch(i -> i.getUuid().equals(instanceUUID));

    if (!instanceUUIDValid) {
      throw new PlatformServiceException(NOT_FOUND, "Invalid instance UUID");
    }

    if (!config.get().isLocalLeader()) {
      throw new PlatformServiceException(
          BAD_REQUEST, "Follower platform instance cannot delete platform instances");
    }

    if (instanceToDelete.get().getIsLocal()) {
      throw new PlatformServiceException(BAD_REQUEST, "Cannot delete local instance");
    }

    // Clear metrics for remote instance
    replicationManager.clearMetrics(instanceToDelete.get());

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
    Optional<HighAvailabilityConfig> config = HighAvailabilityConfig.getOrBadRequest(configUUID);

    Optional<PlatformInstance> localInstance = config.get().getLocal();
    if (!localInstance.isPresent()) {
      throw new PlatformServiceException(BAD_REQUEST, "No local platform instance for config");
    }

    return PlatformResults.withData(localInstance.get());
  }

  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(
                resourceType = ResourceType.OTHER,
                action = Action.SUPER_ADMIN_ACTIONS),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public synchronized Result promoteInstance(
      UUID configUUID, UUID instanceUUID, String curLeaderAddr, boolean force, Http.Request request)
      throws java.net.MalformedURLException {
    Optional<HighAvailabilityConfig> config = HighAvailabilityConfig.getOrBadRequest(configUUID);

    Optional<PlatformInstance> instance = PlatformInstance.get(instanceUUID);

    boolean instanceUUIDValid =
        instance.isPresent()
            && config.get().getInstances().stream().anyMatch(i -> i.getUuid().equals(instanceUUID));

    if (!instanceUUIDValid) {
      throw new PlatformServiceException(NOT_FOUND, "Invalid platform instance UUID");
    }

    if (!instance.get().getIsLocal()) {
      throw new PlatformServiceException(BAD_REQUEST, "Cannot promote a remote platform instance");
    }

    if (instance.get().getIsLeader()) {
      throw new PlatformServiceException(BAD_REQUEST, "Cannot promote a leader platform instance");
    }

    RestorePlatformBackupFormData formData =
        parseJsonAndValidate(request, RestorePlatformBackupFormData.class);

    if (StringUtils.isBlank(curLeaderAddr)) {
      Optional<PlatformInstance> leaderInstance = config.get().getLeader();
      if (!leaderInstance.isPresent()) {
        throw new PlatformServiceException(BAD_REQUEST, "Could not find leader instance");
      }

      curLeaderAddr = leaderInstance.get().getAddress();
    }

    // Validate we can reach current leader
    if (!force) {
      if (!replicationManager.testConnection(
          config.get(), curLeaderAddr, config.get().getAcceptAnyCertificate())) {
        throw new PlatformServiceException(
            BAD_REQUEST, "Could not connect to current leader and force parameter not set.");
      }
    }

    // Make sure the backup file provided exists.
    Optional<File> backup =
        replicationManager.listBackups(new URL(curLeaderAddr)).stream()
            .filter(f -> f.getName().equals(formData.backup_file))
            .findFirst();
    if (!backup.isPresent()) {
      throw new PlatformServiceException(BAD_REQUEST, "Could not find backup file");
    }

    // Cache local instance address before restore so we can query to new corresponding model.
    String localInstanceAddr = instance.get().getAddress();

    // Save the local HA config before it is wiped out.
    replicationManager.saveLocalHighAvailabilityConfig(config.get());

    // Restore the backup.
    if (!replicationManager.restoreBackup(backup.get())) {
      throw new PlatformServiceException(BAD_REQUEST, "Could not restore backup");
    }

    // Handle any incomplete tasks that may be leftover from the backup that was restored.
    taskManager.handleAllPendingTasks();

    // Promote the local instance.
    PlatformInstance.getByAddress(localInstanceAddr)
        .ifPresent(replicationManager::promoteLocalInstance);

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
