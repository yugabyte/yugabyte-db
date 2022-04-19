/*
 * Copyright 2021 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0
 * .txt
 */

package com.yugabyte.yw.controllers;

import com.google.inject.Inject;
import com.yugabyte.yw.common.ApiResponse;
import com.yugabyte.yw.common.ha.PlatformReplicationManager;
import com.yugabyte.yw.forms.PlatformBackupFrequencyFormData;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.PlatformInstance;
import java.io.File;
import java.net.URL;
import java.time.Duration;
import java.util.Collections;
import java.util.List;
import java.util.Optional;
import java.util.UUID;
import java.util.stream.Collectors;
import org.apache.commons.lang3.StringUtils;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.Form;
import play.mvc.Result;

public class PlatformReplicationController extends AuthenticatedController {

  public static final Logger LOG = LoggerFactory.getLogger(PlatformReplicationController.class);

  @Inject private PlatformReplicationManager replicationManager;

  public Result startPeriodicBackup(UUID configUUID) {
    try {
      Optional<HighAvailabilityConfig> config = HighAvailabilityConfig.get(configUUID);
      if (!config.isPresent()) {
        return ApiResponse.error(NOT_FOUND, "Invalid config UUID");
      }

      Form<PlatformBackupFrequencyFormData> formData =
          formFactory.getFormDataOrBadRequest(PlatformBackupFrequencyFormData.class);

      if (!config.get().isLocalLeader()) {
        return ApiResponse.error(BAD_REQUEST, "This platform instance is not a leader");
      }

      Duration frequency = Duration.ofMillis(formData.get().frequency_milliseconds);

      // Restart the backup schedule with the new frequency.
      auditService()
          .createAuditEntryWithReqBody(
              ctx(),
              Audit.TargetType.HABackup,
              configUUID.toString(),
              Audit.ActionType.StartPeriodicBackup);
      return ok(replicationManager.setFrequencyStartAndEnable(frequency));
    } catch (Exception e) {
      LOG.error("Error starting backup schedule", e);

      return ApiResponse.error(INTERNAL_SERVER_ERROR, "Error starting replication schedule");
    }
  }

  public Result stopPeriodicBackup(UUID configUUID) {
    try {
      Optional<HighAvailabilityConfig> config = HighAvailabilityConfig.get(configUUID);
      if (!config.isPresent()) {
        return ApiResponse.error(NOT_FOUND, "Invalid config UUID");
      }

      return ok(replicationManager.stopAndDisable());
    } catch (Exception e) {
      LOG.error("Error cancelling backup schedule", e);
      auditService()
          .createAuditEntryWithReqBody(
              ctx(),
              Audit.TargetType.HABackup,
              configUUID.toString(),
              Audit.ActionType.StopPeriodicBackup);
      return ApiResponse.error(INTERNAL_SERVER_ERROR, "Error stopping replication schedule");
    }
  }

  public Result getBackupInfo(UUID configUUID) {
    try {
      Optional<HighAvailabilityConfig> config = HighAvailabilityConfig.get(configUUID);
      if (!config.isPresent()) {
        return ApiResponse.error(NOT_FOUND, "Invalid config UUID");
      }

      return ok(replicationManager.getBackupInfo());
    } catch (Exception e) {
      LOG.error("Error retrieving backup frequency", e);

      return ApiResponse.error(INTERNAL_SERVER_ERROR, "Error retrieving replication frequency");
    }
  }

  public Result listBackups(UUID configUUID, String leaderAddr) {
    try {
      if (StringUtils.isBlank(leaderAddr)) {
        Optional<HighAvailabilityConfig> config = HighAvailabilityConfig.get(configUUID);
        if (!config.isPresent()) {
          return ApiResponse.error(NOT_FOUND, "Invalid config UUID");
        }

        Optional<PlatformInstance> leaderInstance = config.get().getLeader();
        if (!leaderInstance.isPresent()) {
          return ApiResponse.error(BAD_REQUEST, "Could not find leader platform instance");
        }

        leaderAddr = leaderInstance.get().getAddress();
      }

      List<String> backups =
          replicationManager
              .listBackups(new URL(leaderAddr))
              .stream()
              .map(File::getName)
              .sorted(Collections.reverseOrder())
              .collect(Collectors.toList());
      return PlatformResults.withData(backups);
    } catch (Exception e) {
      LOG.error("Error listing backups", e);

      return ApiResponse.error(INTERNAL_SERVER_ERROR, "Error listing backups");
    }
  }
}
