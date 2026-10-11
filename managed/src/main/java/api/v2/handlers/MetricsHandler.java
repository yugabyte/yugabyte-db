// Copyright (c) YugabyteDB, Inc.

package api.v2.handlers;

import api.v2.models.PrometheusHostInfo;
import api.v2.utils.ApiControllerUtils;
import com.google.inject.Inject;
import com.yugabyte.yw.common.audit.AuditService;
import com.yugabyte.yw.metrics.MetricUrlProvider;

public class MetricsHandler extends ApiControllerUtils {
  private final MetricUrlProvider metricUrlProvider;

  @Inject
  public MetricsHandler(AuditService auditService, MetricUrlProvider metricUrlProvider) {
    super(auditService);
    this.metricUrlProvider = metricUrlProvider;
  }

  public PrometheusHostInfo getPrometheusHostInfo() {
    PrometheusHostInfo prometheusHostInfo = new PrometheusHostInfo();
    prometheusHostInfo.setPrometheusUrl(metricUrlProvider.getMetricsExternalUrl());
    prometheusHostInfo.setUseBrowserFqdn(metricUrlProvider.getMetricsLinkUseBrowserFqdn());
    return prometheusHostInfo;
  }
}
