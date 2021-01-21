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

import com.fasterxml.jackson.databind.node.ArrayNode;
import com.google.inject.Inject;
import com.yugabyte.yw.common.ApiResponse;
import com.yugabyte.yw.common.PlatformReplicationManager;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import com.yugabyte.yw.models.PlatformInstance;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.mvc.Controller;
import play.mvc.Result;
import play.mvc.With;

import java.util.UUID;

@With(HAAuthenticator.class)
public class InternalHAController extends Controller {

  public static final Logger LOG = LoggerFactory.getLogger(InternalHAController.class);

  @Inject
  private PlatformReplicationManager replicationManager;

  public Result getHAConfigByClusterKey() {
    try {
      String clusterKey = ctx().request().header(HAAuthenticator.HA_CLUSTER_KEY_TOKEN_HEADER).get();
      HighAvailabilityConfig config = HighAvailabilityConfig.getByClusterKey(clusterKey);

      return ApiResponse.success(config);
    } catch (Exception e) {
      LOG.error("Error retrieving HA config");

      return ApiResponse.error(INTERNAL_SERVER_ERROR, "Error retrieving HA config");
    }
  }

  public Result syncInstances(UUID configUUID) {
    try {
      HighAvailabilityConfig config = HighAvailabilityConfig.get(configUUID);
      if (config == null) {
        return ApiResponse.error(NOT_FOUND, "Invalid config UUID");
      }

      PlatformInstance localInstance = config.getLocal();

      if (localInstance == null) {
        LOG.warn("No local instance configured");

        return ApiResponse.error(BAD_REQUEST, "No local instance configured");
      }

      if (localInstance.getIsLeader()) {
        LOG.warn(
          "Rejecting request to import instances due to this process being designated a leader"
        );

        return ApiResponse.error(BAD_REQUEST, "Cannot import instances for a leader");
      }

      replicationManager.importPlatformInstances(config, (ArrayNode) request().body().asJson());
      config.refresh();

      return ApiResponse.success(config);
    } catch (Exception e) {
      LOG.error("Error importing platform instances", e);

      return ApiResponse.error(INTERNAL_SERVER_ERROR, "Error importing platform instances");
    }
  }
}
