// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.common.supportbundle;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.containsString;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.doAnswer;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.SupportBundleUtil;
import com.yugabyte.yw.common.metrics.remoteread.RemoteReadClient;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.metrics.MetricUrlProvider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.io.ByteArrayOutputStream;
import java.time.Instant;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.function.BiConsumer;
import org.apache.commons.lang3.tuple.Pair;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;
import prometheus.Types.LabelMatcher;

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
        .readMetricsMatching(anyString(), any(Instant.class), any(Instant.class), any(), any());

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

  @Test
  public void testKubernetesSelectorsCoverOnlyThisUniversesPodsAndClaims() {
    Universe universe = kubernetesUniverse("yb-uni-az1", "yb-tserver-0", "yb-master-0");

    List<PrometheusMetricsComponent.SeriesSelector> selectors =
        component.buildKubernetesSelectors(universe);

    assertEquals(2, selectors.size());
    PrometheusMetricsComponent.SeriesSelector pods = selectors.get(0);
    assertEquals("pods", pods.name());
    // The cAdvisor and kube-state-metrics targets keep every pod matching .*yb-.* on the whole
    // Kubernetes cluster, so the pod names are what keep another universe out of this bundle.
    assertThat(pods.promQl(), containsString("namespace=~\"yb-uni-az1\""));
    assertThat(pods.promQl(), containsString("yb-tserver-0"));
    assertThat(pods.promQl(), containsString("yb-master-0"));
    // No metric names: whatever the targets keep for our pods is what belongs in the bundle,
    // and a name list here would have to track two repositories' scrape configs.
    assertFalse(pods.promQl().contains("__name__"));

    PrometheusMetricsComponent.SeriesSelector volumes = selectors.get(1);
    assertEquals("volumes", volumes.name());
    // Volume stats carry no pod label - the claim name (<volume>-<pod>) is the only way in.
    assertThat(volumes.promQl(), containsString("persistentvolumeclaim=~"));
    assertThat(volumes.promQl(), containsString("(.*)-yb-tserver-0"));
    assertFalse(volumes.promQl().contains("pod_name"));

    // Remote read needs the same thing as regex matchers, since every label here is a set.
    for (PrometheusMetricsComponent.SeriesSelector selector : selectors) {
      assertEquals(2, selector.matchers().size());
      selector.matchers().forEach(matcher -> assertEquals(LabelMatcher.Type.RE, matcher.getType()));
    }
  }

  @Test
  public void testNoKubernetesSelectorsForAVmUniverse() {
    // getK8sPodName() slices the private IP when there is no pod name, so a VM universe would
    // otherwise produce a selector built out of IP fragments.
    UniverseDefinitionTaskParams details = new UniverseDefinitionTaskParams();
    UserIntent userIntent = new UserIntent();
    userIntent.providerType = CloudType.aws;
    details.upsertPrimaryCluster(userIntent, null, null);
    NodeDetails node = new NodeDetails();
    node.nodeName = "yb-dev-node-1";
    node.cloudInfo = new CloudSpecificInfo();
    node.cloudInfo.private_ip = "10.0.0.1";
    details.nodeDetailsSet = Set.of(node);
    Universe universe = new Universe();
    universe.setUniverseDetails(details);

    assertTrue(component.buildKubernetesSelectors(universe).isEmpty());
  }

  private Universe kubernetesUniverse(String namespace, String... podNames) {
    UniverseDefinitionTaskParams details = new UniverseDefinitionTaskParams();
    UserIntent userIntent = new UserIntent();
    userIntent.providerType = CloudType.kubernetes;
    details.upsertPrimaryCluster(userIntent, null, null);
    Set<NodeDetails> nodes = new LinkedHashSet<>();
    for (String podName : podNames) {
      NodeDetails node = new NodeDetails();
      node.nodeName = podName;
      node.cloudInfo = new CloudSpecificInfo();
      node.cloudInfo.kubernetesPodName = podName;
      node.cloudInfo.kubernetesNamespace = namespace;
      nodes.add(node);
    }
    details.nodeDetailsSet = nodes;
    Universe universe = new Universe();
    universe.setUniverseDetails(details);
    return universe;
  }
}
