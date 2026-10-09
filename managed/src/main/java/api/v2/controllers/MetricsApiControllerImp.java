// Copyright (c) YugabyteDB, Inc.

package api.v2.controllers;

import api.v2.handlers.MetricsHandler;
import api.v2.models.PrometheusHostInfo;
import com.google.inject.Inject;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.audit.AuditService;
import com.yugabyte.yw.controllers.handlers.GFlagsAuditHandler;
import play.mvc.Http;

public class MetricsApiControllerImp extends MetricsApiControllerImpInterface {
  private final MetricsHandler metricsHandler;

  @Inject
  public MetricsApiControllerImp(
      AuditService auditService,
      Config config,
      GFlagsAuditHandler gFlagsAuditHandler,
      MetricsHandler metricsHandler) {
    super(auditService, config, gFlagsAuditHandler);
    this.metricsHandler = metricsHandler;
  }

  @Override
  public PrometheusHostInfo getPrometheusHostInfo(Http.Request request) throws Exception {
    return metricsHandler.getPrometheusHostInfo();
  }
}
