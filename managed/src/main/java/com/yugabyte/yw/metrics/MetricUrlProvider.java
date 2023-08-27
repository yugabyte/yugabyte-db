// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.metrics;

import com.cronutils.utils.StringUtils;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import java.net.URLEncoder;
import java.util.TimeZone;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Singleton
public class MetricUrlProvider {

  private static final String API_PATH = "/api/v1";

  private static final String MANAGEMENT_PATH = "/-";
  public static final String DATE_FORMAT_STRING = "yyyy-MM-dd HH:mm:ss";
  private final RuntimeConfGetter runtimeConfGetter;

  @Inject
  public MetricUrlProvider(RuntimeConfGetter runtimeConfGetter) {
    this.runtimeConfGetter = runtimeConfGetter;
  }

  /**
   * Get the metrics base uri based on the appConfig yb.metrics.uri
   *
   * @return returns metrics url string
   */
  public String getMetricsApiUrl() {
    return getMetricsInternalUrl() + API_PATH;
  }

  public String getMetricsManagementUrl() {
    return getMetricsInternalUrl() + MANAGEMENT_PATH;
  }

  public boolean getMetricsLinkUseBrowserFqdn() {
    return runtimeConfGetter.getGlobalConf(GlobalConfKeys.metricsLinkUseBrowserFqdn);
  }

  public String getMetricsExternalUrl() {
    String metricsExternalUrl = runtimeConfGetter.getGlobalConf(GlobalConfKeys.metricsExternalUrl);
    if (StringUtils.isEmpty(metricsExternalUrl)) {
      // Fallback to internal in case external is not explicitly defined
      metricsExternalUrl = getMetricsInternalUrl();
    }

    return metricsExternalUrl;
  }

  public String getExpressionUrl(String queryExpr, Long startUnixTime, Long endUnixTime) {
    String durationSecs = "3600s";
    String endString = "";

    if (endUnixTime != 0 && startUnixTime != 0 && endUnixTime > startUnixTime) {
      // The timezone is set to UTC because If there is a discrepancy between platform and
      // prometheus timezones, the resulting directURL will show incorrect timeframe.
      endString =
          Util.unixTimeToDateString(
              endUnixTime * 1000, DATE_FORMAT_STRING, TimeZone.getTimeZone("UTC"));
      durationSecs = String.format("%ds", (endUnixTime - startUnixTime));
    }

    // Note: this is the URL as prometheus' web interface renders these metrics. It is
    // possible this breaks over time as we upgrade prometheus.
    return String.format(
        "%s/graph?g0.expr=%s&g0.tab=0&g0.range_input=%s&g0.end_input=%s",
        this.getMetricsExternalUrl(), URLEncoder.encode(queryExpr), durationSecs, endString);
  }

  public String getMetricsInternalUrl() {
    String metricsUrl = runtimeConfGetter.getStaticConf().getString("yb.metrics.url");
    if (StringUtils.isEmpty(metricsUrl)) {
      throw new RuntimeException("yb.metrics.url not set");
    }
    return metricsUrl.replace(API_PATH, StringUtils.EMPTY);
  }
}
