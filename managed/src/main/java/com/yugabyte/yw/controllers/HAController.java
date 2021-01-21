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
import com.yugabyte.yw.forms.HAConfigFormData;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.PlatformInstance;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.data.Form;
import play.data.FormFactory;
import play.libs.Json;
import play.mvc.Result;

import java.util.List;
import java.util.UUID;

public class HAController extends AuthenticatedController {

  public static final Logger LOG = LoggerFactory.getLogger(HAController.class);

  @Inject
  private PlatformReplicationManager replicationManager;

  @Inject
  private FormFactory formFactory;

  // TODO: (Daniel) - This could be a task
  public Result createHAConfig() {
    try {
      Form<HAConfigFormData> formData = formFactory.form(HAConfigFormData.class).bindFromRequest();
      if (formData.hasErrors()) {
        return ApiResponse.error(BAD_REQUEST, formData.errorsAsJson());
      }

      if (!HighAvailabilityConfig.list().isEmpty()) {
        LOG.warn("Only support for a single HA Config has currently been implemented");

        return ApiResponse.error(BAD_REQUEST, "An HA Config already exists");
      }

      HighAvailabilityConfig config = HighAvailabilityConfig.create(formData.get().cluster_key);

      return ApiResponse.success(config);
    } catch (Exception e) {
      LOG.error("Error creating HA config", e);

      return ApiResponse.error(INTERNAL_SERVER_ERROR, "Error creating HA config");
    }
  }

  public Result getHAConfig() {
    try {
      List<HighAvailabilityConfig> configs = HighAvailabilityConfig.list();

      if (configs.isEmpty()) {
        return ApiResponse.error(NOT_FOUND, "No HA config exists");
      }

      return ApiResponse.success(configs.get(0));
    } catch (Exception e) {
      LOG.error("Error retrieving HA config", e);

      return ApiResponse.error(INTERNAL_SERVER_ERROR, "Error retrieving HA config");
    }
  }

  public Result editHAConfig(UUID configUUID) {
    try {
      HighAvailabilityConfig config = HighAvailabilityConfig.get(configUUID);
      if (config == null) {
        return ApiResponse.error(NOT_FOUND, "Invalid config UUID");
      }

      Form<HAConfigFormData> formData = formFactory.form(HAConfigFormData.class).bindFromRequest();
      if (formData.hasErrors()) {
        return ApiResponse.error(BAD_REQUEST, formData.errorsAsJson());
      }

      replicationManager.stop();
      HighAvailabilityConfig.update(config, formData.get().cluster_key);
      replicationManager.start();

      return ApiResponse.success(config);
    } catch (Exception e) {
      LOG.error("Error updating cluster key", e);

      return ApiResponse.error(INTERNAL_SERVER_ERROR, "Error updating cluster key");
    }
  }

  // TODO: (Daniel) - This could be a task
  public Result deleteHAConfig(UUID configUUID) {
    try {
      HighAvailabilityConfig config = HighAvailabilityConfig.get(configUUID);
      if (config == null) {
        return ApiResponse.error(NOT_FOUND, "Invalid config UUID");
      }

      // Stop the backup schedule.
      replicationManager.stop();
      HighAvailabilityConfig.delete(configUUID);

      return ok();
    } catch (Exception e) {
      LOG.error("Error deleting HA config", e);

      return ApiResponse.error(INTERNAL_SERVER_ERROR, "Error deleting HA config");
    }
  }

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
