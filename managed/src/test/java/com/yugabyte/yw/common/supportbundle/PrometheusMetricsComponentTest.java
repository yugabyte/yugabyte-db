// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.common.supportbundle;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.doAnswer;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.yugabyte.yw.common.SupportBundleUtil;
import com.yugabyte.yw.common.metrics.remoteread.RemoteReadClient;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.metrics.MetricUrlProvider;
import java.io.ByteArrayOutputStream;
import java.time.Instant;
import java.util.List;
import java.util.Map;
import java.util.function.BiConsumer;
import org.apache.commons.lang3.tuple.Pair;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

/**
 * The prometheus dump is read back by Perf Advisor, which keys on the label field being named
 * "metric" - Prometheus's own name for it, and what the PromQL export writes. The remote read
 * export wrote "metrics" and silently broke that restore (PLAT-22150), so the name is pinned here.
 */
@RunWith(MockitoJUnitRunner.class)
public class PrometheusMetricsComponentTest {

  @Mock private MetricQueryHelper metricQueryHelper;
  @Mock private SupportBundleUtil supportBundleUtil;
  @Mock private RemoteReadClient remoteReadClient;
  @Mock private MetricUrlProvider metricUrlProvider;

  private final ObjectMapper objectMapper = new ObjectMapper();
  private PrometheusMetricsComponent component;

  @Before
  public void setUp() {
    component =
        new PrometheusMetricsComponent(
            metricQueryHelper,
            supportBundleUtil,
            remoteReadClient,
            metricUrlProvider,
            objectMapper);
  }

  @Test
  public void testMetricsJsonUsesThePrometheusLabelFieldName() throws Exception {
    doAnswer(
            invocation -> {
              BiConsumer<Map<String, String>, List<Pair<Long, Double>>> consumer =
                  invocation.getArgument(4);
              consumer.accept(
                  Map.of("__name__", "cpu_usage", "node_prefix", "yb-test"),
                  List.of(Pair.of(1704103200000L, 0.75), Pair.of(1704103215000L, 0.80)));
              return null;
            })
        .when(remoteReadClient)
        .readMetrics(anyString(), any(Instant.class), any(Instant.class), any(), any());

    ByteArrayOutputStream out = new ByteArrayOutputStream();
    component.writeMetricsJson(
        "http://localhost:9090",
        Instant.parse("2024-01-01T09:59:00Z"),
        Instant.parse("2024-01-01T10:01:00Z"),
        Map.of("node_prefix", "yb-test"),
        // Below the 15s spacing of the points above, so neither is downsampled away.
        1,
        out);

    JsonNode dump = objectMapper.readTree(out.toByteArray());
    assertTrue(dump.isArray());
    assertEquals(1, dump.size());

    JsonNode series = dump.get(0);
    assertFalse("the label field must not be named \"metrics\"", series.has("metrics"));
    assertTrue(series.has("metric"));
    assertEquals("cpu_usage", series.get("metric").get("__name__").asText());

    // Timestamps are emitted in seconds and values as strings, matching a PromQL query result.
    JsonNode values = series.get("values");
    assertEquals(2, values.size());
    assertEquals(1704103200L, values.get(0).get(0).asLong());
    assertEquals("0.75", values.get(0).get(1).asText());
    assertEquals("0.8", values.get(1).get(1).asText());
  }
}
