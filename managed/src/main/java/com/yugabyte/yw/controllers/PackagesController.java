// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.google.inject.Inject;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.forms.PackagesRequestParams;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.common.YbaApi.YbaApiVisibility;
import com.yugabyte.yw.rbac.annotations.AuthzPath;
import com.yugabyte.yw.rbac.annotations.PermissionAttribute;
import com.yugabyte.yw.rbac.annotations.RequiredPermissionOnResource;
import com.yugabyte.yw.rbac.annotations.Resource;
import com.yugabyte.yw.rbac.enums.SourceType;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiImplicitParam;
import io.swagger.annotations.ApiImplicitParams;
import io.swagger.annotations.ApiOperation;
import java.io.File;
import java.util.StringJoiner;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import play.mvc.Http;
import play.mvc.Result;
import play.mvc.Results;

@Api(value = "PackagesController")
@Slf4j
public class PackagesController extends AbstractPlatformController {

  @Inject private RuntimeConfigFactory runtimeConfigFactory;

  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  @YbaApi(visibility = YbaApiVisibility.INTERNAL, sinceYBAVersion = "2.16.0.0")
  @ApiOperation(
      value = "YbaApi Internal. Fetch a package",
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
  /* This is used by YBC to download YBC binaries from YBA during YBC software upgrade.
  Will be deprecated/removed once YBC upgrade improvements work is taken up
   */
  public Result fetchPackage(Http.Request request) {

    PackagesRequestParams taskParams = parseJsonAndValidate(request, PackagesRequestParams.class);
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
    return Results.ok(new File(fileLoc)).withHeader(CONTENT_TYPE, "application/gzip");
  }
}
