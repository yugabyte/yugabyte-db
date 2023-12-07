/*
 * Copyright 2023 YugaByte, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 * http://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.controllers;

import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.HALeaderResp;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.forms.PlatformResults.YBPError;
import com.yugabyte.yw.models.HighAvailabilityConfig;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.ApiResponse;
import io.swagger.annotations.ApiResponses;
import java.util.Optional;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.mvc.Http;
import play.mvc.Result;

public class HANoAuthController extends AbstractPlatformController {

  public static final Logger LOG = LoggerFactory.getLogger(HANoAuthController.class);

  @ApiOperation(
      value = "WARNING: This is a preview API that could change. Finalize is HA Leader.",
      response = HALeaderResp.class)
  @ApiResponses(
      value = {
        @ApiResponse(
            code = 500,
            message = "If there was an error retrieving the HA Config",
            response = YBPError.class),
        @ApiResponse(
            code = 404,
            message = "If there was no HA Config found",
            response = YBPError.class)
      })
  public Result isHALeader(Http.Request request) {
    try {
      Optional<HighAvailabilityConfig> config = HighAvailabilityConfig.get();

      if (!config.isPresent()) {
        LOG.debug("No HA config exists");

        return new PlatformServiceException(NOT_FOUND, "No HA Config exists").buildResult(request);
      }

      if (config.get().isLocalLeader()) {
        LOG.debug("YBA instance is HA leader");

        return PlatformResults.withData(new HALeaderResp(true));

      } else {
        LOG.debug("YBA instance is HA follower");
        return new PlatformServiceException(NOT_FOUND, "YBA instance is HA follower")
            .buildResult(request);
      }
    } catch (Exception e) {
      LOG.error("Error retrieving HA config", e);

      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, "Error retrieving HA config");
    }
  }
}
