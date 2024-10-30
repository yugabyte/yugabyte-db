// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.google.inject.Inject;
import com.yugabyte.yw.common.gflags.GFlagGroup;
import com.yugabyte.yw.controllers.handlers.GFlagsValidationHandler;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.rbac.annotations.AuthzPath;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.io.IOException;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.mvc.Result;

@Api(
    value = "UI_ONLY",
    hidden = true,
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class GFlagGroupsController extends AuthenticatedController {

  @Inject private GFlagsValidationHandler gflagsValidationHandler;

  public static final Logger LOG = LoggerFactory.getLogger(GFlagGroupsController.class);

  /**
   * GET endpoint for extracting gflag groups
   *
   * @return JSON response that list all gflag groups for a version
   */
  @ApiOperation(
      notes = "YbaApi Internal. Get GFlag groups",
      value = "Get gflag groups",
      response = GFlagGroup.class)
  @YbaApi(visibility = YbaApi.YbaApiVisibility.INTERNAL, sinceYBAVersion = "2024.1.1.0")
  @AuthzPath
  public Result getGFlagGroups(String version, String group) throws IOException {
    return PlatformResults.withData(gflagsValidationHandler.getGFlagGroups(version, group));
  }
}
