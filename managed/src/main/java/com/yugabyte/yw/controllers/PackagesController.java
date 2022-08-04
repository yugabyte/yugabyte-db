// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.google.inject.Inject;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.forms.PackagesRequestParams;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiImplicitParam;
import io.swagger.annotations.ApiImplicitParams;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.io.File;
import java.util.StringJoiner;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import play.mvc.Result;
import play.mvc.Results;

@Api(
    value = "PackagesController",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
@Slf4j
public class PackagesController extends AuthenticatedController {

  @Inject private RuntimeConfigFactory runtimeConfigFactory;

  @ApiOperation(
      value = "Fetch a package",
      nickname = "fetchPackage",
      response = String.class,
      produces = "application/gzip")
  @ApiImplicitParams({
    @ApiImplicitParam(
        name = "Package",
        value = "Package to be imported",
        required = true,
        dataType = "com.yugabyte.yw.forms.PackagesRequestParams",
        paramType = "body")
  })
  public Result fetchPackage() {

    PackagesRequestParams taskParams = parseJsonAndValidate(PackagesRequestParams.class);
    StringJoiner joiner = new StringJoiner("-");
    joiner
        .add(taskParams.packageName)
        .add(taskParams.buildNumber)
        .add(taskParams.osType.name().toLowerCase())
        .add(taskParams.architectureType.name().toLowerCase());
    String fileExt =
        StringUtils.isBlank(taskParams.archiveType) ? "" : "." + taskParams.archiveType;
    String fileLoc =
        runtimeConfigFactory.staticApplicationConf().getString("ybc.releases.path")
            + "/"
            + taskParams.buildNumber
            + "/"
            + joiner.toString()
            + fileExt;
    response().setContentType("application/gzip");
    return Results.ok(new File(fileLoc));
  }
}
