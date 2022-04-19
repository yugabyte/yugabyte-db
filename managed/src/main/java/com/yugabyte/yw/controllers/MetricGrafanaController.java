package com.yugabyte.yw.controllers;

import java.io.InputStream;
import java.net.URL;

import com.google.inject.Inject;
import com.google.inject.Singleton;
import com.yugabyte.yw.common.PlatformServiceException;

import io.swagger.annotations.ApiOperation;
import lombok.extern.slf4j.Slf4j;
import play.Environment;
import play.mvc.Controller;
import play.mvc.Result;
import play.mvc.Results;

import org.apache.commons.io.IOUtils;
import java.nio.charset.StandardCharsets;
import io.swagger.annotations.Authorization;
import io.swagger.annotations.Api;

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
