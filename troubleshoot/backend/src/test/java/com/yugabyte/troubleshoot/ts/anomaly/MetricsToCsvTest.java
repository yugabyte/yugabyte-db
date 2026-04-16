package com.yugabyte.troubleshoot.ts.anomaly;

import com.yugabyte.troubleshoot.ts.metric.client.PrometheusClient;
import com.yugabyte.troubleshoot.ts.metric.models.MetricRangeQuery;
import com.yugabyte.troubleshoot.ts.metric.models.MetricResponse;
import com.yugabyte.troubleshoot.ts.service.ServiceTest;
import java.io.File;
import java.io.PrintWriter;
import java.time.Duration;
import java.time.ZonedDateTime;
import java.util.Random;
import lombok.SneakyThrows;
import org.springframework.beans.factory.annotation.Autowired;

@ServiceTest
public class MetricsToCsvTest {

  @Autowired PrometheusClient prometheusClient;

  @SneakyThrows
  public void metricsToCsv() {
    MetricRangeQuery metricRangeQuery = new MetricRangeQuery();
    metricRangeQuery.setQuery(
        "(avg(rate(rpc_latency_sum{service_type=\"SQLProcessor\", node_prefix=\"yb-15-pf-arm-gp3\", server_type=\"yb_cqlserver\", service_method=\"InsertStmt\"}[3600s])) by (service_method)) / (avg(rate(rpc_latency_count{service_type=\"SQLProcessor\", node_prefix=\"yb-15-pf-arm-gp3\", server_type=\"yb_cqlserver\", service_method=\"InsertStmt\"}[3600s])) by (service_method))");
    metricRangeQuery.setStart(ZonedDateTime.parse("2024-02-20T15:36:00+00:00"));
    metricRangeQuery.setEnd(ZonedDateTime.parse("2024-02-21T15:36:00+00:00"));
    metricRangeQuery.setStep(Duration.ofMinutes(1));
    MetricResponse response =
        prometheusClient.queryRange("https://portal.dev.yugabyte.com:9090/", metricRangeQuery);

    String outputFile =
        "/Users/amalysh86/code/yugabyte-db/troubleshoot/backend/src/test/resources/"
            + "anomaly/no_anomalies_1_day.csv";
    File csvOutputFile = new File(outputFile);
    try (PrintWriter pw = new PrintWriter(csvOutputFile)) {
      response.getData().getResult().stream()
          .flatMap(r -> r.getValues().stream())
          .map(val -> val.getLeft().longValue() + "," + val.getRight().longValue())
          .forEach(pw::println);
    }
  }

  @SneakyThrows
  public void generateMetricsCsv() {
    long firstTs = 1708443360L;
    long lastTs = 1708529760L;
    long initialValue = 70;

    String outputFile =
        "/Users/amalysh86/code/yugabyte-db/troubleshoot/backend/src/test/resources/"
            + "anomaly/cpu_usage_high.csv";
    File csvOutputFile = new File(outputFile);
    try (PrintWriter pw = new PrintWriter(csvOutputFile)) {
      long totalPoints = (lastTs - firstTs) / 60L;
      long point = 0;
      long value = initialValue;
      for (long i = firstTs; i <= lastTs; i += 60L) {
        pw.println(
            i + "," + (value + new Random().nextDouble(initialValue * 0.1) - initialValue * 0.05));
        point++;
      }
    }
  }
}
