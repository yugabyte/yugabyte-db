// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.metrics;

import com.typesafe.config.Config;
import com.yugabyte.yw.common.Util;
import java.net.URLEncoder;
import java.util.TimeZone;
import javax.inject.Inject;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Singleton
public class MetricUrlProvider {
  public static final String DATE_FORMAT_STRING = "yyyy-MM-dd HH:mm:ss";
  private final Config appConfig;

  @Inject
  public MetricUrlProvider(Config appConfig) {
    this.appConfig = appConfig;
  }

  /**
   * Get the metrics base uri based on the appConfig yb.metrics.uri
   *
   * @return returns metrics url string
   */
  public String getMetricsUrl() {
    String metricsUrl = appConfig.getString("yb.metrics.url");
    if (metricsUrl == null || metricsUrl.isEmpty()) {
      throw new RuntimeException("yb.metrics.url not set");
    }

    return metricsUrl;
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
        this.getMetricsUrl().replace("/api/v1", ""),
        URLEncoder.encode(queryExpr),
        durationSecs,
        endString);
  }
}
