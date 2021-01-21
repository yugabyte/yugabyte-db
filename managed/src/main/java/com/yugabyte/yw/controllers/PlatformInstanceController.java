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
import com.yugabyte.yw.common.PlatformReplicationManager;
import com.yugabyte.yw.forms.PlatformInstanceFormData;
import com.yugabyte.yw.forms.RestorePlatformBackupFormData;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.PlatformInstance;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.Form;
import play.data.FormFactory;
import play.mvc.Result;

import java.io.File;
import java.util.Optional;
import java.util.UUID;

public class PlatformInstanceController extends AuthenticatedController {

  public static final Logger LOG = LoggerFactory.getLogger(PlatformInstanceController.class);

  @Inject
  private PlatformReplicationManager replicationManager;

  @Inject
  private FormFactory formFactory;

  public Result createInstance(UUID configUUID) {
    try {
      HighAvailabilityConfig config = HighAvailabilityConfig.get(configUUID);
      if (config == null) {
        return ApiResponse.error(NOT_FOUND, "Invalid config UUID");
      }

      Form<PlatformInstanceFormData> formData =
        formFactory.form(PlatformInstanceFormData.class).bindFromRequest();
      if (formData.hasErrors()) {
        return ApiResponse.error(BAD_REQUEST, formData.errorsAsJson());
      }

      // Cannot create a remote instance before creating a local instance.
      if (!formData.get().is_local && config.getLocal() == null) {
        return ApiResponse.error(
          BAD_REQUEST,
          "Cannot create a remote platform instance before creating local platform instance"
        );
      // Cannot create a remote instance if local instance is follower.
      } else if (!formData.get().is_local && !config.isLeaderLocal()) {
        return ApiResponse.error(
          BAD_REQUEST,
          "Cannot create a remote platform instance on a follower platform instance"
        );
      // Cannot create multiple local platform instances.
      } else if (formData.get().is_local && config.getLocal() != null) {
        return ApiResponse.error(BAD_REQUEST, "Local platform instance already exists");
      // Cannot create multiple leader platform instances.
      } else if (formData.get().is_leader && config.getLeader() != null) {
        return ApiResponse.error(BAD_REQUEST, "Leader platform instance already exists");
      }

      PlatformInstance instance = PlatformInstance.create(
        config,
        formData.get().address,
        formData.get().is_leader,
        formData.get().is_local
      );

      return ApiResponse.success(instance);
    } catch (Exception e) {
      LOG.error("Error creating platform instance", e);

      return ApiResponse.error(INTERNAL_SERVER_ERROR, "Error creating platform instance");
    }
  }

  public Result deleteInstance(UUID configUUID, UUID instanceUUID) {
    try {
      HighAvailabilityConfig config = HighAvailabilityConfig.get(configUUID);
      if (config == null) {
        return ApiResponse.error(NOT_FOUND, "Invalid config UUID");
      }

      boolean instanceUUIDValid = config.getInstances()
        .stream()
        .anyMatch(i -> i.getUUID().equals(instanceUUID));

      if (!instanceUUIDValid) {
        return ApiResponse.error(NOT_FOUND, "Invalid instance UUID");
      }

      if (!config.isLeaderLocal()) {
        return ApiResponse.error(
          BAD_REQUEST,
          "Follower platform instance cannot delete platform instances"
        );
      }

      PlatformInstance instanceToDelete = PlatformInstance.get(instanceUUID);

      if (instanceToDelete.getIsLocal()) {
        return ApiResponse.error(BAD_REQUEST, "Cannot delete local instance");
      }

      PlatformInstance.delete(instanceUUID);

      return ok();
    } catch (Exception e) {
      LOG.error("Error deleting platform instance", e);

      return ApiResponse.error(INTERNAL_SERVER_ERROR, "Error deleting platform instance");
    }
  }

  public Result getLeader(UUID configUUID) {
    try {
      HighAvailabilityConfig config = HighAvailabilityConfig.get(configUUID);
      if (config == null) {
        return ApiResponse.error(NOT_FOUND, "Invalid config UUID");
      }

      PlatformInstance leaderInstance = config.getLeader();
      if (leaderInstance == null) {
        return ApiResponse.error(BAD_REQUEST, "No leader platform instance for config");
      }

      return ApiResponse.success(leaderInstance);
    } catch (Exception e) {
      LOG.error("Error retrieving leader platform instance for config", e);

      return ApiResponse.error(
        INTERNAL_SERVER_ERROR,
        "Error retrieving leader platform instance for config"
      );
    }
  }

  public Result getLocal(UUID configUUID) {
    try {
      HighAvailabilityConfig config = HighAvailabilityConfig.get(configUUID);
      if (config == null) {
        return ApiResponse.error(NOT_FOUND, "Invalid config UUID");
      }

      PlatformInstance localInstance = config.getLocal();
      if (localInstance == null) {
        return ApiResponse.error(BAD_REQUEST, "No local platform instance for config");
      }

      return ApiResponse.success(localInstance);
    } catch (Exception e) {
      LOG.error("Error retrieving local platform instance for config", e);

      return ApiResponse.error(
        INTERNAL_SERVER_ERROR,
        "Error retrieving local platform instance for config"
      );
    }
  }

  // TODO: (Daniel) - This needs to validate that another instance is not still
  //  reachable + is a leader, etc (#6505)
  public Result promoteInstance(UUID configUUID, UUID instanceUUID) {
    try {
      HighAvailabilityConfig config = HighAvailabilityConfig.get(configUUID);
      if (config == null) {
        return ApiResponse.error(NOT_FOUND, "Invalid config UUID");
      }

      boolean instanceUUIDValid = config.getInstances()
        .stream()
        .anyMatch(i -> i.getUUID().equals(instanceUUID));

      if (!instanceUUIDValid) {
        return ApiResponse.error(NOT_FOUND, "Invalid platform instance UUID");
      }

      PlatformInstance instance = PlatformInstance.get(instanceUUID);

      if (!instance.getIsLocal()) {
        return ApiResponse.error(BAD_REQUEST, "Cannot promote a remote platform instance");
      }

      if (instance.getIsLeader()) {
        return ApiResponse.error(BAD_REQUEST, "Cannot promote a leader platform instance");
      }

      Form<RestorePlatformBackupFormData> formData =
        formFactory.form(RestorePlatformBackupFormData.class).bindFromRequest();
      if (formData.hasErrors()) {
        return ApiResponse.error(BAD_REQUEST, formData.errorsAsJson());
      }

      // Make sure the backup file provided exists.
      Optional<File> backup = replicationManager.listBackups()
        .stream()
        .filter(f -> f.getName().equals(formData.get().backup_file))
        .findFirst();
      if (!backup.isPresent()) {
        return ApiResponse.error(BAD_REQUEST, "Could not find backup file");
      }

      String localInstanceAddr = instance.getAddress();

      // Run the backup.
      backup.ifPresent(file -> replicationManager.restoreBackup(file.getAbsolutePath()));

      // Promote the local instance to leader.
      PlatformInstance newInstance = PlatformInstance.getByAddress(localInstanceAddr);
      config = newInstance.getConfig();
      HighAvailabilityConfig.promoteInstance(config, newInstance);
      config.getInstances().forEach(i -> i.setIsLocal(i.getUUID().equals(newInstance.getUUID())));

      // Finally, start the new backup schedule.
      replicationManager.start();

      return ok();
    } catch (Exception e) {
      LOG.error("Error promoting platform instance", e);

      return ApiResponse.error(INTERNAL_SERVER_ERROR, "Error promoting platform instance");
    }
  }
}
