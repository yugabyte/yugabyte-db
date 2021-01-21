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
import com.yugabyte.yw.forms.PlatformBackupFrequencyFormData;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.Form;
import play.data.FormFactory;
import play.mvc.Result;

import java.io.File;
import java.time.Duration;
import java.util.Collections;
import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;

public class PlatformReplicationController extends AuthenticatedController {

  public static final Logger LOG = LoggerFactory.getLogger(PlatformReplicationController.class);

  @Inject
  private PlatformReplicationManager replicationManager;

  @Inject
  private FormFactory formFactory;

  public Result startPeriodicBackup(UUID configUUID) {
    try {
      HighAvailabilityConfig config = HighAvailabilityConfig.get(configUUID);
      if (config == null) {
        return ApiResponse.error(NOT_FOUND, "Invalid config UUID");
      }

      Form<PlatformBackupFrequencyFormData> formData =
        formFactory.form(PlatformBackupFrequencyFormData.class).bindFromRequest();

      if (formData.hasErrors()) {
        return ApiResponse.error(BAD_REQUEST, formData.errorsAsJson());
      }

      if (!config.isLeaderLocal()) {
        return ApiResponse.error(BAD_REQUEST, "This platform instance is not a leader");
      }

      Duration frequency = Duration.ofMillis(formData.get().frequency_milliseconds);

      // Restart the backup schedule with the new frequency.
      return ok(replicationManager.setFrequencyStartAndEnable(frequency));
    } catch (Exception e) {
      LOG.error("Error starting backup schedule", e);

      return ApiResponse.error(INTERNAL_SERVER_ERROR, "Error starting replication schedule");
    }
  }

  public Result stopPeriodicBackup(UUID configUUID) {
    try {
      HighAvailabilityConfig config = HighAvailabilityConfig.get(configUUID);
      if (config == null) {
        return ApiResponse.error(NOT_FOUND, "Invalid config UUID");
      }

      return ok(replicationManager.stopAndDisable());
    } catch (Exception e) {
      LOG.error("Error cancelling backup schedule", e);

      return ApiResponse.error(INTERNAL_SERVER_ERROR, "Error stopping replication schedule");
    }
  }

  public Result getBackupInfo(UUID configUUID) {
    try {
      HighAvailabilityConfig config = HighAvailabilityConfig.get(configUUID);
      if (config == null) {
        return ApiResponse.error(NOT_FOUND, "Invalid config UUID");
      }

      return ok(replicationManager.getBackupInfo());
    } catch (Exception e) {
      LOG.error("Error retrieving backup frequency", e);

      return ApiResponse.error(INTERNAL_SERVER_ERROR, "Error retrieving replication frequency");
    }
  }

  public Result listBackups() {
    try {
      List<String> backups = replicationManager.listBackups()
        .stream()
        .map(File::getName)
        .sorted(Collections.reverseOrder())
        .collect(Collectors.toList());
      return ApiResponse.success(backups);
    } catch (Exception e) {
      LOG.error("Error listing backups", e);

      return ApiResponse.error(INTERNAL_SERVER_ERROR, "Error listing backups");
    }
  }
}
