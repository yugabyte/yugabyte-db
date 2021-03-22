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
import com.yugabyte.yw.common.ApiResponse;
import com.yugabyte.yw.common.CustomerTaskManager;
import com.yugabyte.yw.common.ha.PlatformReplicationManager;
import com.yugabyte.yw.forms.PlatformInstanceFormData;
import com.yugabyte.yw.forms.RestorePlatformBackupFormData;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.PlatformInstance;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.Form;
import play.data.FormFactory;
import play.mvc.Result;

import java.io.File;
import java.net.URL;
import java.util.Optional;
import java.util.UUID;

public class PlatformInstanceController extends AuthenticatedController {

  public static final Logger LOG = LoggerFactory.getLogger(PlatformInstanceController.class);

  @Inject
  private PlatformReplicationManager replicationManager;

  @Inject
  private FormFactory formFactory;

  @Inject
  CustomerTaskManager taskManager;

  public Result createInstance(UUID configUUID) {
    try {
      Optional<HighAvailabilityConfig> config = HighAvailabilityConfig.get(configUUID);
      if (!config.isPresent()) {
        return ApiResponse.error(NOT_FOUND, "Invalid config UUID");
      }

      Form<PlatformInstanceFormData> formData =
        formFactory.form(PlatformInstanceFormData.class).bindFromRequest();
      if (formData.hasErrors()) {
        return ApiResponse.error(BAD_REQUEST, formData.errorsAsJson());
      }

      // Cannot create a remote instance before creating a local instance.
      if (!formData.get().is_local && !config.get().getLocal().isPresent()) {
        return ApiResponse.error(
          BAD_REQUEST,
          "Cannot create a remote platform instance before creating local platform instance"
        );
      // Cannot create a remote instance if local instance is follower.
      } else if (!formData.get().is_local && !config.get().isLocalLeader()) {
        return ApiResponse.error(
          BAD_REQUEST,
          "Cannot create a remote platform instance on a follower platform instance"
        );
      // Cannot create multiple local platform instances.
      } else if (formData.get().is_local && config.get().getLocal().isPresent()) {
        return ApiResponse.error(BAD_REQUEST, "Local platform instance already exists");
      // Cannot create multiple leader platform instances.
      } else if (formData.get().is_leader && config.get().isLocalLeader()) {
        return ApiResponse.error(BAD_REQUEST, "Leader platform instance already exists");
      }

      PlatformInstance instance = PlatformInstance.create(
        config.get(),
        formData.get().address,
        formData.get().is_leader,
        formData.get().is_local
      );

      // Mark this instance as "failed over to" initially since it is a leader instance.
      if (instance.getIsLeader()) {
        config.get().updateLastFailover();
      }

      return ApiResponse.success(instance);
    } catch (Exception e) {
      LOG.error("Error creating platform instance", e);

      return ApiResponse.error(INTERNAL_SERVER_ERROR, "Error creating platform instance");
    }
  }

  public Result deleteInstance(UUID configUUID, UUID instanceUUID) {
    try {
      Optional<HighAvailabilityConfig> config = HighAvailabilityConfig.get(configUUID);
      if (!config.isPresent()) {
        return ApiResponse.error(NOT_FOUND, "Invalid config UUID");
      }

      Optional<PlatformInstance> instanceToDelete = PlatformInstance.get(instanceUUID);

      boolean instanceUUIDValid = instanceToDelete.isPresent() && config.get().getInstances()
        .stream()
        .anyMatch(i -> i.getUUID().equals(instanceUUID));

      if (!instanceUUIDValid) {
        return ApiResponse.error(NOT_FOUND, "Invalid instance UUID");
      }

      if (!config.get().isLocalLeader()) {
        return ApiResponse.error(
          BAD_REQUEST,
          "Follower platform instance cannot delete platform instances"
        );
      }

      if (instanceToDelete.get().getIsLocal()) {
        return ApiResponse.error(BAD_REQUEST, "Cannot delete local instance");
      }

      PlatformInstance.delete(instanceUUID);

      return ok();
    } catch (Exception e) {
      LOG.error("Error deleting platform instance", e);

      return ApiResponse.error(INTERNAL_SERVER_ERROR, "Error deleting platform instance");
    }
  }

  public Result getLocal(UUID configUUID) {
    try {
      Optional<HighAvailabilityConfig> config = HighAvailabilityConfig.get(configUUID);
      if (!config.isPresent()) {
        return ApiResponse.error(NOT_FOUND, "Invalid config UUID");
      }

      Optional<PlatformInstance> localInstance = config.get().getLocal();
      if (!localInstance.isPresent()) {
        return ApiResponse.error(BAD_REQUEST, "No local platform instance for config");
      }

      return ApiResponse.success(localInstance.get());
    } catch (Exception e) {
      LOG.error("Error retrieving local platform instance for config", e);

      return ApiResponse.error(
        INTERNAL_SERVER_ERROR,
        "Error retrieving local platform instance for config"
      );
    }
  }

  public Result promoteInstance(UUID configUUID, UUID instanceUUID, String curLeaderAddr) {
    try {
      Optional<HighAvailabilityConfig> config = HighAvailabilityConfig.get(configUUID);
      if (!config.isPresent()) {
        return ApiResponse.error(NOT_FOUND, "Invalid config UUID");
      }

      Optional<PlatformInstance> instance = PlatformInstance.get(instanceUUID);

      boolean instanceUUIDValid = instance.isPresent() && config.get().getInstances()
        .stream()
        .anyMatch(i -> i.getUUID().equals(instanceUUID));

      if (!instanceUUIDValid) {
        return ApiResponse.error(NOT_FOUND, "Invalid platform instance UUID");
      }

      if (!instance.get().getIsLocal()) {
        return ApiResponse.error(BAD_REQUEST, "Cannot promote a remote platform instance");
      }

      if (instance.get().getIsLeader()) {
        return ApiResponse.error(BAD_REQUEST, "Cannot promote a leader platform instance");
      }

      Form<RestorePlatformBackupFormData> formData =
        formFactory.form(RestorePlatformBackupFormData.class).bindFromRequest();
      if (formData.hasErrors()) {
        return ApiResponse.error(BAD_REQUEST, formData.errorsAsJson());
      }

      if (StringUtils.isBlank(curLeaderAddr)) {
        Optional<PlatformInstance> leaderInstance = config.get().getLeader();
        if (!leaderInstance.isPresent()) {
          return ApiResponse.error(BAD_REQUEST, "Could not find leader instance");
        }

        curLeaderAddr = leaderInstance.get().getAddress();
      }

      // Make sure the backup file provided exists.
      Optional<File> backup = replicationManager.listBackups(new URL(curLeaderAddr))
        .stream()
        .filter(f -> f.getName().equals(formData.get().backup_file))
        .findFirst();
      if (!backup.isPresent()) {
        return ApiResponse.error(BAD_REQUEST, "Could not find backup file");
      }

      // Cache local instance address before restore so we can query to new corresponding model.
      String localInstanceAddr = instance.get().getAddress();

      // Restore the backup.
      backup.ifPresent(replicationManager::restoreBackup);

      // Fail any incomplete tasks that may be leftover from the backup that was restored.
      taskManager.failAllPendingTasks();

      // Promote the local instance.
      PlatformInstance.getByAddress(localInstanceAddr)
        .ifPresent(replicationManager::promoteLocalInstance);

      // Start the new backup schedule.
      replicationManager.start();

      // Finally, switch the prometheus configuration to read from swamper targets directly.
      replicationManager.switchPrometheusToStandalone();

      return ok();
    } catch (Exception e) {
      LOG.error("Error promoting platform instance", e);

      return ApiResponse.error(INTERNAL_SERVER_ERROR, "Error promoting platform instance");
    }
  }
}
