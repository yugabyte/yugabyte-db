// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.controllers;

import com.yugabyte.yw.common.AppInit;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.metrics.MetricService;
import com.yugabyte.yw.models.Metric;
import com.yugabyte.yw.models.common.YbaApi;
import com.yugabyte.yw.models.filters.MetricFilter;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.PlatformMetrics;
import io.prometheus.client.Collector;
import io.prometheus.client.Collector.MetricFamilySamples;
import io.prometheus.client.CollectorRegistry;
import io.prometheus.client.exporter.common.TextFormat;
import io.swagger.annotations.Api;
import io.swagger.annotations.ApiOperation;
import io.swagger.annotations.Authorization;
import java.io.BufferedWriter;
import java.io.ByteArrayOutputStream;
import java.io.OutputStreamWriter;
import java.time.temporal.ChronoUnit;
import java.util.*;
import java.util.function.Function;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import java.util.zip.GZIPOutputStream;
import javax.inject.Inject;
import kamon.Kamon;
import kamon.module.Module;
import kamon.prometheus.PrometheusReporter;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import play.mvc.*;

@Api(value = "Metrics", authorizations = @Authorization(AbstractPlatformController.API_KEY_AUTH))
@Slf4j
public class MetricsController extends Controller {

  @Inject
  public MetricsController(AppInit appInit) {
    // Bind AppInit so that the metrics are not published before the app initialisation completes.
    // No-op performed.
  }

  @Inject private MetricService metricService;

  private Date lastErrorPrinted = null;

  private PrometheusReporter reporter =
      new PrometheusReporter(PrometheusReporter.DefaultConfigPath(), Kamon.config());
  private Module.Registration registry = Kamon.addReporter("YBA Metrics Reporter", reporter);

  @ApiOperation(
      notes = "Available since YBA version 2.8.0.0.",
      value = "Get Prometheus metrics",
      response = String.class,
      nickname = "MetricsDetail")
  @YbaApi(visibility = YbaApi.YbaApiVisibility.PUBLIC, sinceYBAVersion = "2.8.0.0")
  public Result index(Http.Request request) {
    try (ByteArrayOutputStream response = new ByteArrayOutputStream(1 << 20)) {
      Optional<String> acceptEncoding = request.header("Accept-Encoding");
      if (acceptEncoding.isPresent() && acceptEncoding.get().contains("gzip")) {
        try (GZIPOutputStream gzipStream = new GZIPOutputStream(response)) {
          return writeMetrics(response, gzipStream);
        }
      } else {
        return writeMetrics(response, null);
      }
    } catch (Exception e) {
      if (lastErrorPrinted == null
          || lastErrorPrinted.before(CommonUtils.nowMinus(1, ChronoUnit.HOURS))) {
        log.error("Failed to retrieve metrics", e);
        lastErrorPrinted = new Date();
      }
      throw new PlatformServiceException(INTERNAL_SERVER_ERROR, e.getMessage());
    }
  }

  private Result writeMetrics(ByteArrayOutputStream response, GZIPOutputStream gzipStream)
      throws Exception {
    boolean isGzip = gzipStream != null;
    try (OutputStreamWriter osw = new OutputStreamWriter(isGzip ? gzipStream : response);
        BufferedWriter bw = new BufferedWriter(osw, 1 << 20)) {
      // Write runtime metrics
      TextFormat.write004(bw, CollectorRegistry.defaultRegistry.metricFamilySamples());
      // Write persisted metrics
      TextFormat.write004(bw, Collections.enumeration(getPrecalculatedMetrics()));
      // Write Kamon metrics
      bw.write(getKamonMetrics());
      bw.flush();
      osw.flush();
      if (isGzip) {
        gzipStream.finish();
      }
      response.flush();
      if (isGzip) {
        return Results.status(OK)
            .sendBytes(response.toByteArray(), Optional.of("text/plain"))
            .withHeaders("Content-Encoding", "gzip");
      } else {
        return Results.status(OK, response.toString());
      }
    }
  }

  private String getKamonMetrics() {
    return reporter.scrapeData();
  }

  private List<Collector.MetricFamilySamples> getPrecalculatedMetrics() {
    List<MetricFamilySamples> result = new ArrayList<>();
    List<Metric> allMetrics = metricService.list(MetricFilter.builder().expired(false).build());

    Map<String, List<Metric>> metricsByName =
        allMetrics.stream()
            .collect(Collectors.groupingBy(Metric::getName, TreeMap::new, Collectors.toList()));

    Map<String, PlatformMetrics> platformMetricsMap =
        Stream.of(PlatformMetrics.values())
            .collect(Collectors.toMap(PlatformMetrics::getMetricName, Function.identity()));
    for (Map.Entry<String, List<Metric>> metric : metricsByName.entrySet()) {
      String metricName = metric.getKey();
      List<Metric> metrics = metric.getValue();
      PlatformMetrics knownMetric = platformMetricsMap.get(metricName);
      String help = knownMetric != null ? knownMetric.getHelp() : metrics.get(0).getHelp();
      String unit = knownMetric != null ? knownMetric.getUnitName() : metrics.get(0).getUnit();
      if (unit == null) {
        // Prometheus client library expects empty string in case metric has no unit
        unit = StringUtils.EMPTY;
      }
      if (!unit.isEmpty() && !metricName.endsWith("_" + unit)) {
        // Seems like one of the metrics have invalid unit.
        // Let's just clean unit for this one and log it.
        log.warn("Metric name {} should end with '{}'", metricName, "_" + unit);
        unit = StringUtils.EMPTY;
      }
      Collector.Type type = metrics.get(0).getType().getPrometheusType();

      List<Collector.MetricFamilySamples.Sample> samples =
          metrics.stream().map(this::convert).collect(Collectors.toList());
      result.add(new Collector.MetricFamilySamples(metricName, unit, type, help, samples));
    }
    return result;
  }

  private Collector.MetricFamilySamples.Sample convert(Metric metric) {
    List<String> labelNames = new ArrayList<>(metric.getLabels().keySet());
    List<String> labelValues = new ArrayList<>(metric.getLabels().values());
    return new Collector.MetricFamilySamples.Sample(
        metric.getName(), labelNames, labelValues, metric.getValue());
  }
}
