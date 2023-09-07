// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.google.inject.Inject;
import com.yugabyte.yw.controllers.handlers.GFlagsValidationHandler;
import com.yugabyte.yw.forms.GFlagsValidationFormData;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.rbac.annotations.AuthzPath;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.io.IOException;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.mvc.Http;
import play.mvc.Result;

@Api(
    value = "UI_ONLY",
    hidden = true,
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class GFlagsValidationUiOnlyController extends AuthenticatedController {

  @Inject private GFlagsValidationHandler gflagsValidationHandler;

  public static final Logger LOG = LoggerFactory.getLogger(GFlagsValidationUiOnlyController.class);

  /**
   * GET UI Only endpoint for extracting gflags metadata
   *
   * @return JSON response of all gflags metadata per version, serverType
   */
  @ApiOperation(value = "UI_ONLY", hidden = true)
  @AuthzPath
  public Result listGFlags(String version, String gflag, String serverType, Boolean mostUsedGFlags)
      throws IOException {
    return PlatformResults.withData(
        gflagsValidationHandler.listGFlags(version, gflag, serverType, mostUsedGFlags));
  }

  /**
   * POST UI Only endpoint for validating input gflag.
   *
   * @return JSON response of errors in input gflags
   */
  @ApiOperation(value = "UI_ONLY", hidden = true)
  @AuthzPath
  public Result validateGFlags(String version, Http.Request request) throws IOException {
    GFlagsValidationFormData gflags = parseJsonAndValidate(request, GFlagsValidationFormData.class);
    auditService()
        .createAuditEntry(request, Audit.TargetType.GFlags, version, Audit.ActionType.Validate);
    return PlatformResults.withData(gflagsValidationHandler.validateGFlags(version, gflags));
  }

  /**
   * GET UI Only endpoint for extracting input gflag metadata
   *
   * @return JSON response of a input gflag metadata
   */
  @ApiOperation(value = "UI_ONLY", hidden = true)
  @AuthzPath
  public Result getGFlagMetadata(String version, String gflag, String serverType)
      throws IOException {
    return PlatformResults.withData(
        gflagsValidationHandler.getGFlagsMetadata(version, serverType, gflag));
  }
}
