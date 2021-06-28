// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import com.yugabyte.yw.common.YWServiceException;

import play.mvc.Result;
import play.mvc.Controller;
import play.mvc.Results;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import io.prometheus.client.exporter.common.TextFormat;
import io.prometheus.client.CollectorRegistry;
import java.io.ByteArrayOutputStream;
import java.io.OutputStreamWriter;

public class MetricsController extends Controller {

  public static final Logger LOG = LoggerFactory.getLogger(MetricsController.class);

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
