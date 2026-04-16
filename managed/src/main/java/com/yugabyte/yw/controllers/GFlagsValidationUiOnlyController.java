// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.controllers;

import com.google.inject.Inject;
import com.yugabyte.yw.common.gflags.GFlagDetails;
import com.yugabyte.yw.controllers.handlers.GFlagsValidationHandler;
import com.yugabyte.yw.forms.GFlagsValidationFormData;
import com.yugabyte.yw.forms.PlatformResults;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.rbac.annotations.AuthzPath;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiImplicitParam;
import io.swagger.annotations.ApiImplicitParams;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.io.IOException;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.mvc.Http;
import play.mvc.Result;

@Api(
    value = "GFlags Validation APIs",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class GFlagsValidationUiOnlyController extends AuthenticatedController {

  @Inject private GFlagsValidationHandler gflagsValidationHandler;

  public static final Logger LOG = LoggerFactory.getLogger(GFlagsValidationUiOnlyController.class);

  /**
   * GET endpoint for extracting gflags metadata
   *
   * @return JSON response of all gflags metadata per version, serverType
   */
  @ApiOperation(
      notes = "WARNING: This is a preview API that could change.",
      value = "List all gflags for a release",
      response = GFlagDetails.class,
      responseContainer = "List")
  @YbaApi(visibility = YbaApi.YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.20.9.0")
  @AuthzPath
  public Result listGFlags(
      String version,
      String gflag,
      String serverType,
      Boolean mostUsedGFlags,
      Boolean showExperimental)
      throws IOException {
    return PlatformResults.withData(
        gflagsValidationHandler.listGFlags(
            version, gflag, serverType, mostUsedGFlags, showExperimental));
  }

  /**
   * POST endpoint for validating input gflag.
   *
   * @return JSON response of errors in input gflags
   */
  @ApiOperation(
      notes = "WARNING: This is a preview API that could change.",
      value = "Validate gflags",
      response = GFlagsValidationFormData.GFlagsValidationResponse.class,
      responseContainer = "List")
  @ApiImplicitParams(
      @ApiImplicitParam(
          name = "gflag_validation_form_data",
          value = "GFlag validation form data",
          dataType = "com.yugabyte.yw.forms.GFlagsValidationFormData",
          required = true,
          paramType = "body"))
  @YbaApi(visibility = YbaApi.YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.20.9.0")
  @AuthzPath
  public Result validateGFlags(String version, Http.Request request) throws IOException {
    GFlagsValidationFormData gflags = parseJsonAndValidate(request, GFlagsValidationFormData.class);
    auditService()
        .createAuditEntry(request, Audit.TargetType.GFlags, version, Audit.ActionType.Validate);
    return PlatformResults.withData(gflagsValidationHandler.validateGFlags(version, gflags));
  }

  /**
   * GET endpoint for extracting input gflag metadata
   *
   * @return JSON response of a input gflag metadata
   */
  @ApiOperation(
      notes = "WARNING: This is a preview API that could change.",
      value = "Get gflag metadata",
      response = GFlagDetails.class)
  @YbaApi(visibility = YbaApi.YbaApiVisibility.PREVIEW, sinceYBAVersion = "2.20.9.0")
  @AuthzPath
  public Result getGFlagMetadata(String version, String gflag, String serverType)
      throws IOException {
    return PlatformResults.withData(
        gflagsValidationHandler.getGFlagsMetadata(version, serverType, gflag));
  }
}
