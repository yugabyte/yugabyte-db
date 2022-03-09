package com.yugabyte.yw.controllers;

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

@Singleton
@Slf4j
public class MetricGrafanaController extends Controller {

  private final Environment environment;

  @Inject
  public MetricGrafanaController(Environment environment) {
    this.environment = environment;
  }

  @ApiOperation(
      value = "Get Grafana Dashboard Path",
      response = String.class,
      nickname = "GrafanaDashboardPath")
  public Result getGrafanaDashboardPath() {
    String resourcePath = "metric/Dashboard.json";
    URL dashboardURL;
    try {
      dashboardURL = environment.classLoader().getResource(resourcePath);
      if (dashboardURL == null) {
        return Results.status(NOT_FOUND);
      }
    } catch (Exception e) {
      log.error("Error in reading dashboard file: {}", e);
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
    }
    return Results.status(OK, dashboardURL.getPath());
  }
}
