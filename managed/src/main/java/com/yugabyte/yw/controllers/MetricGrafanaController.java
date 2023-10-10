// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.rbac.annotations.AuthzPath;
import com.yugabyte.yw.rbac.annotations.PermissionAttribute;
import com.yugabyte.yw.rbac.annotations.RequiredPermissionOnResource;
import com.yugabyte.yw.rbac.annotations.Resource;
import com.yugabyte.yw.rbac.enums.SourceType;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.io.IOUtils;
import play.Environment;
import play.mvc.Result;
import play.mvc.Results;

@Api(
    value = "Grafana Dashboard",
    authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
@Singleton
@Slf4j
public class MetricGrafanaController extends AuthenticatedController {

  private final Environment environment;

  @Inject
  public MetricGrafanaController(Environment environment) {
    this.environment = environment;
  }

  @ApiOperation(
      value = "Get Grafana Dashboard",
      response = String.class,
      nickname = "GrafanaDashboard")
  @AuthzPath({
    @RequiredPermissionOnResource(
        requiredPermission =
            @PermissionAttribute(resourceType = ResourceType.OTHER, action = Action.READ),
        resourceLocation = @Resource(path = Util.CUSTOMERS, sourceType = SourceType.ENDPOINT))
  })
  public Result getGrafanaDashboard() {
    String resourcePath = "metric/Dashboard.json";
    String dashboardContent = "";
    try (InputStream dashboardStream = environment.resourceAsStream(resourcePath)) {
      if (dashboardStream == null) {
        return Results.status(NOT_FOUND);
      }
      dashboardContent = IOUtils.toString(dashboardStream, StandardCharsets.UTF_8);
    } catch (Exception e) {
      log.error("Error in reading dashboard file: {}", e);
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
    }
    return Results.status(OK, dashboardContent);
  }
}
