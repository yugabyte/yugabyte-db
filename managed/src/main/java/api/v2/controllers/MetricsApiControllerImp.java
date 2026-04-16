// Copyright (c) YugabyteDB, Inc.

package api.v2.controllers;

import api.v2.handlers.MetricsHandler;
import api.v2.models.PrometheusHostInfo;
import com.google.inject.Inject;
import play.mvc.Http;

public class MetricsApiControllerImp extends MetricsApiControllerImpInterface {
  @Inject private MetricsHandler metricsHandler;

  @Override
  public PrometheusHostInfo getPrometheusHostInfo(Http.Request request) throws Exception {
    return metricsHandler.getPrometheusHostInfo(request);
  }
}
