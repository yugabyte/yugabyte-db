// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.yugabyte.yw.common.YWServiceException;
import io.prometheus.client.CollectorRegistry;
import io.prometheus.client.exporter.common.TextFormat;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.io.ByteArrayOutputStream;
import java.io.OutputStreamWriter;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import play.mvc.Controller;
import play.mvc.Result;
import play.mvc.Results;

@Api(value = "Metrics", authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
public class MetricsController extends Controller {

  public static final Logger LOG = LoggerFactory.getLogger(MetricsController.class);

  @ApiOperation(value = "index", response = String.class, nickname = "MetricsDetail")
  public Result index() {
    final ByteArrayOutputStream response = new ByteArrayOutputStream(1 << 20);
    try {
      final OutputStreamWriter osw = new OutputStreamWriter(response);
      TextFormat.write004(osw, CollectorRegistry.defaultRegistry.metricFamilySamples());
      osw.flush();
      osw.close();
      response.flush();
      response.close();
    } catch (Exception e) {
      throw new YWServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
    }

    return Results.status(OK, response.toString());
  }
}
