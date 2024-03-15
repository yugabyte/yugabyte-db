package com.yugabyte.troubleshoot.ts.metric.client;

import com.yugabyte.troubleshoot.ts.metric.models.MetricQuery;
import com.yugabyte.troubleshoot.ts.metric.models.MetricQueryBase;
import com.yugabyte.troubleshoot.ts.metric.models.MetricRangeQuery;
import com.yugabyte.troubleshoot.ts.metric.models.MetricResponse;
import java.time.Duration;
import java.time.ZoneOffset;
import java.time.ZonedDateTime;
import java.time.format.DateTimeFormatter;
import org.springframework.stereotype.Component;
import org.springframework.util.LinkedMultiValueMap;
import org.springframework.util.MultiValueMap;
import org.springframework.web.client.RestTemplate;
import org.springframework.web.util.UriComponentsBuilder;

@Component
public class PrometheusClient {

  private static final DateTimeFormatter DATE_TIME_FORMATTER =
      DateTimeFormatter.ofPattern("yyyy-MM-dd'T'HH:mm:ss.SSS'Z'");

  private final RestTemplate prometheusClientTemplate;

  public PrometheusClient(RestTemplate prometheusClientTemplate) {
    this.prometheusClientTemplate = prometheusClientTemplate;
  }

  public MetricResponse query(String metricsUrl, MetricQuery request) {
    return prometheusClientTemplate.getForObject(
        buildPrometheusUri(metricsUrl, request), MetricResponse.class);
  }

  public MetricResponse queryRange(String metricsUrl, MetricRangeQuery request) {
    return prometheusClientTemplate.getForObject(
        buildPrometheusUri(metricsUrl, request), MetricResponse.class);
  }

  private String buildPrometheusUri(String metricsUrl, MetricQuery query) {
    MultiValueMap<String, String> params = baseParams(query);
    if (query.getTime() != null) {
      params.add("time", timeParam(query.getTime()));
    }
    return buildPrometheusUri(metricsUrl, "/query", params);
  }

  private String buildPrometheusUri(String metricsUrl, MetricRangeQuery query) {
    MultiValueMap<String, String> params = baseParams(query);
    if (query.getStart() != null) {
      params.add("start", timeParam(query.getStart()));
    }
    if (query.getEnd() != null) {
      params.add("end", timeParam(query.getEnd()));
    }
    if (query.getStep() != null) {
      params.add("step", durationParam(query.getStep()));
    }
    return buildPrometheusUri(metricsUrl, "/query_range", params);
  }

  private String buildPrometheusUri(
      String metricsUrl, String path, MultiValueMap<String, String> params) {
    UriComponentsBuilder uriComponentsBuilder =
        UriComponentsBuilder.fromUriString(metricsUrl).path("/api/v1").path(path);
    if (params != null) {
      uriComponentsBuilder.queryParams(params);
    }
    return uriComponentsBuilder.build().toString();
  }

  private MultiValueMap<String, String> baseParams(MetricQueryBase queryBase) {
    MultiValueMap<String, String> params = new LinkedMultiValueMap<>();
    params.add("query", queryBase.getQuery());
    if (queryBase.getTimeout() != null) {
      params.add("timeout", durationParam(queryBase.getTimeout()));
    }
    return params;
  }

  private String timeParam(ZonedDateTime time) {
    return time.withZoneSameInstant(ZoneOffset.UTC).format(DATE_TIME_FORMATTER);
  }

  private String durationParam(Duration duration) {
    return duration.toSeconds() + "s";
  }
}
