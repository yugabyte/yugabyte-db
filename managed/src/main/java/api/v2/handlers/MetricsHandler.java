// Copyright (c) YugabyteDB, Inc.

package api.v2.handlers;

import api.v2.models.PrometheusHostInfo;
import api.v2.utils.ApiControllerUtils;
import com.google.inject.Inject;
import com.yugabyte.yw.metrics.MetricUrlProvider;
import play.mvc.Http;

public class MetricsHandler extends ApiControllerUtils {
  @Inject private MetricUrlProvider metricUrlProvider;

  public PrometheusHostInfo getPrometheusHostInfo(Http.Request request) throws Exception {
    PrometheusHostInfo prometheusHostInfo = new PrometheusHostInfo();
    prometheusHostInfo.setPrometheusUrl(metricUrlProvider.getMetricsExternalUrl());
    prometheusHostInfo.setUseBrowserFqdn(metricUrlProvider.getMetricsLinkUseBrowserFqdn());
    return prometheusHostInfo;
  }
}
