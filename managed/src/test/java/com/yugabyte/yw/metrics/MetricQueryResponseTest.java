// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.metrics;

import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.instanceOf;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.hamcrest.Matchers.containsInAnyOrder;
import static org.hamcrest.core.AllOf.allOf;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThat;
import static org.junit.Assert.assertTrue;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.metrics.K8sPodNodeNameResolver.ReleaseInfo;
import com.yugabyte.yw.models.MetricConfigDefinition;
import com.yugabyte.yw.models.MetricConfigDefinition.Layout;
import com.yugabyte.yw.models.MetricConfigDefinition.Layout.Axis;
import java.util.List;
import java.util.stream.Collectors;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class MetricQueryResponseTest {

  private static final String METRIC_NAME = "metric_name";
  private static final MetricSettings METRIC_SETTINGS = MetricSettings.defaultSettings(METRIC_NAME);

  @InjectMocks MetricQueryResponse metricQueryResponse;

  @Test
  public void testMetricsWithNodePrefix() {
    JsonNode responseJson =
        Json.parse(
            "{\"status\":\"success\",\"data\":{\"resultType\":\"vector\",\"result\":[{\"metric\":\n"
                + " {\"node_prefix\":"
                + " \"1-host\"},\"value\":[1479278137,\"0.027751899056199826\"]},{\"metric\":\n"
                + " {\"node_prefix\": \"1-host\"},"
                + " \"value\":[1479278137,\"0.04329469299783263\"]}]}}");

    MetricQueryResponse queryResponse = Json.fromJson(responseJson, MetricQueryResponse.class);

    List<MetricGraphData> data =
        queryResponse.getGraphData(METRIC_NAME, new MetricConfigDefinition(), METRIC_SETTINGS);

    assertEquals(data.size(), 2);
    for (int i = 0; i < data.size(); i++) {
      assertThat(data.get(i).name, allOf(notNullValue(), equalTo("1-host")));
      assertThat(data.get(i).type, allOf(notNullValue(), equalTo("scatter")));
      assertThat(data.get(i).x, allOf(notNullValue(), instanceOf(ArrayNode.class)));
      assertThat(data.get(i).y, allOf(notNullValue(), instanceOf(ArrayNode.class)));
    }
  }

  @Test
  public void testSingleMetrics() {
    JsonNode responseJson =
        Json.parse(
            "{\"status\":\"success\",\"data\":{\"resultType\":\"vector\",\"result\":[{\"metric\":\n"
                + " {\"cpu\":\"system\"},\"value\":[1479278137,\"0.027751899056199826\"]}]}}");

    MetricQueryResponse queryResponse = Json.fromJson(responseJson, MetricQueryResponse.class);

    List<MetricGraphData> data =
        queryResponse.getGraphData(METRIC_NAME, new MetricConfigDefinition(), METRIC_SETTINGS);
    assertEquals(data.size(), 1);
    assertThat(data.get(0).name, allOf(notNullValue(), equalTo("system")));
    assertThat(data.get(0).type, allOf(notNullValue(), equalTo("scatter")));
    assertThat(data.get(0).x, allOf(notNullValue(), instanceOf(ArrayNode.class)));
    assertThat(data.get(0).y, allOf(notNullValue(), instanceOf(ArrayNode.class)));
  }

  @Test
  public void testMultipleMetrics() {
    JsonNode responseJson =
        Json.parse(
            "{\"status\":\"success\",\"data\":{\"resultType\":\"matrix\",\"result\":[{\"metric\":{\"memory\":\"buffered\"},\"values\":[[1479281730,\"0\"],\n"
                + " [1479281732,\"0\"],[1479281734,\"0\"]]},{\"metric\":{\"memory\":\"cached\"},\"values\":[[1479281730,\"2902821546.6666665\"],[1479281732,\"2901345621.3333335\"],\n"
                + " [1479281734,\"2901345621.3333335\"]]},{\"metric\":{\"memory\":\"free\"},\"values\":[[1479281730,\"137700693.33333334\"],[1479281732,\"139952128\"],\n"
                + " [1479281734,\"139952128\"]]},{\"metric\":{\"memory\":\"used\"},\"values\":[[1479281730,\"4193042432\"],[1479281732,\"4192290133.3333335\"],[1479281734,\"4192290133.3333335\"]]}]}}");

    MetricQueryResponse queryResponse = Json.fromJson(responseJson, MetricQueryResponse.class);
    List<MetricGraphData> data =
        queryResponse.getGraphData(METRIC_NAME, new MetricConfigDefinition(), METRIC_SETTINGS);
    List<JsonNode> memoryTags = responseJson.findValues("memory");

    assertEquals(4, data.size());
    assertEquals(4, memoryTags.size());
    for (int i = 0; i < data.size(); i++) {
      assertThat(data.get(i).name, allOf(notNullValue()));
      assertThat(memoryTags.get(i).asText(), equalTo(data.get(i).name));
      assertThat(data.get(i).type, allOf(notNullValue(), equalTo("scatter")));
      assertThat(data.get(i).x, allOf(notNullValue(), instanceOf(ArrayNode.class)));
      assertThat(data.get(i).y, allOf(notNullValue(), instanceOf(ArrayNode.class)));
    }
  }

  @Test
  public void testMetricsWithAlias() {
    JsonNode responseJson =
        Json.parse(
            "{\"status\":\"success\",\"data\":{\"resultType\":\"matrix\",\"result\":[{\"metric\":{\"service_method\":\"ExecuteRequest\"},\"values\":[[1507603405,\"4116.2\"]]},{\"metric\":{\"service_method\":\"ParseRequest\"},\"values\":[[1507603405,\"2.8\"]]},{\"metric\":{\"service_method\":\"ProcessRequest\"},\"values\":[[1507603405,\"4138.099999999999\"]]}]}}");

    MetricQueryResponse queryResponse = Json.fromJson(responseJson, MetricQueryResponse.class);
    Layout layout =
        new Layout()
            .setTitle("CQL Metrics")
            .setYaxis(
                new Axis()
                    .setAlias(
                        ImmutableMap.of(
                            "ExecuteRequest", "Execute",
                            "ParseRequest", "Parse",
                            "ProcessRequest", "Process")));

    MetricConfigDefinition config = new MetricConfigDefinition().setLayout(layout);
    List<MetricGraphData> data = queryResponse.getGraphData(METRIC_NAME, config, METRIC_SETTINGS);
    assertEquals(data.size(), 3);
    assertThat(data.get(0).type, allOf(notNullValue(), equalTo("scatter")));
    assertThat(data.get(0).x, allOf(notNullValue(), instanceOf(ArrayNode.class)));
    assertThat(data.get(0).y, allOf(notNullValue(), instanceOf(ArrayNode.class)));
    assertThat(data.get(0).name, allOf(notNullValue(), equalTo("Execute")));
    assertThat(data.get(1).name, allOf(notNullValue(), equalTo("Parse")));
    assertThat(data.get(2).name, allOf(notNullValue(), equalTo("Process")));
  }

  @Test
  public void testMetricsWithMultiVariableAlias() {
    JsonNode responseJson =
        Json.parse(
            "{\"status\":\"success\",\"data\":{\"resultType\":\"matrix\",\"result\":[{\"metric\":{\"service_method\":\"local\",\"service_type\":\"read\"},\"values\":[[1507603405,\"0.0\"]]},{\"metric\":{\"service_method\":\"local\",\"service_type\":\"write\"},\"values\":[[1507603405,\"0.0\"]]},{\"metric\":{\"service_method\":\"remote\",\"service_type\":\"read\"},\"values\":[[1507603405,\"0.0\"]]},{\"metric\":{\"service_method\":\"remote\",\"service_type\":\"write\"},\"values\":[[1507603405,\"0.0\"]]}]}}");

    MetricQueryResponse queryResponse = Json.fromJson(responseJson, MetricQueryResponse.class);
    Layout layout =
        new Layout()
            .setTitle("CQL Metrics")
            .setYaxis(
                new Axis()
                    .setAlias(
                        ImmutableMap.of(
                            "remote,read", "Remote Read",
                            "remote,write", "Remote Write",
                            "local,read", "Local Read",
                            "local,write", "Local Write")));

    MetricConfigDefinition config = new MetricConfigDefinition().setLayout(layout);
    List<MetricGraphData> data = queryResponse.getGraphData(METRIC_NAME, config, METRIC_SETTINGS);
    assertEquals(data.size(), 4);
    assertThat(data.get(0).type, allOf(notNullValue(), equalTo("scatter")));
    assertThat(data.get(0).x, allOf(notNullValue(), instanceOf(ArrayNode.class)));
    assertThat(data.get(0).y, allOf(notNullValue(), instanceOf(ArrayNode.class)));
    for (int i = 0; i < data.size(); ++i) {
      assertTrue(layout.getYaxis().getAlias().values().contains(data.get(i).name));
    }
  }

  @Test
  public void testContainerMetricsWithPodName() {
    JsonNode responseJson =
        Json.parse(
            "{\"status\":\"success\",\"data\":{\"resultType\":\"vector\",\"result\":[{\"metric\":\n"
                + " {\"pod_name\":\"yb-tserver-test-pod\",\"container_name\":\"yb-tserver\","
                + "\"namespace\":\"test-namespace\"},\"value\":[1479278137,\"0.5\"]}]}}");

    MetricQueryResponse queryResponse = Json.fromJson(responseJson, MetricQueryResponse.class);

    List<MetricGraphData> data =
        queryResponse.getGraphData(
            "container_cpu_usage",
            new MetricConfigDefinition(),
            MetricSettings.defaultSettings("container_cpu_usage"));

    assertEquals(data.size(), 1);
    MetricGraphData metricData = data.get(0);
    assertThat(metricData.instanceName, allOf(notNullValue(), equalTo("yb-tserver-test-pod")));
    assertThat(metricData.type, allOf(notNullValue(), equalTo("scatter")));
    assertThat(metricData.x, allOf(notNullValue(), instanceOf(ArrayNode.class)));
    assertThat(metricData.y, allOf(notNullValue(), instanceOf(ArrayNode.class)));
  }

  // The following volume-metric tests reproduce the scenarios reported against real universes
  // where PVC labels emitted by the yugabyte Helm chart in newNamingStyle look like
  // `<fullname>datadir<N>-<fullname>yb-<server>-<idx>` (i.e. the volume-claim-template name is
  // prefixed with `<fullname>`, so `datadir<N>` is in the middle of the label rather than at
  // the start). Regressions here previously caused the datadir suffix to be dropped and the RR
  // pods to be resolved as primary because the pod-name portion couldn't be recovered.

  @Test
  public void testContainerVolumeMetricsSingleAzPrimaryNewNamingStyle() {
    String releaseName = "ybamalyshev-single-pdmc";
    // yugabyte chart in newNamingStyle: PVC = `<fullname>datadir<N>-<fullname>yb-<server>-<idx>`
    // where `<fullname>` = helm release name + "-".
    String pvc = releaseName + "-datadir0-" + releaseName + "-yb-tserver-0";
    JsonNode responseJson =
        Json.parse(
            "{\"status\":\"success\",\"data\":{\"resultType\":\"vector\",\"result\":[{\"metric\":"
                + " {\"persistentvolumeclaim\":\""
                + pvc
                + "\",\"namespace\":\"ybamalyshev-single\",\"az_name\":\"us-west1-a\"},"
                + "\"value\":[1479278137,\"0.5\"]}]}}");
    MetricQueryResponse queryResponse = Json.fromJson(responseJson, MetricQueryResponse.class);
    K8sPodNodeNameResolver resolver =
        K8sPodNodeNameResolver.ofReleases(
            true /* newNamingStyle */,
            ImmutableList.of(
                new ReleaseInfo(
                    releaseName,
                    "ybamalyshev-single",
                    "us-west1-a",
                    false /* isMultiAZ */,
                    false /* isReadReplica */)));

    List<MetricGraphData> data =
        queryResponse.getGraphData(
            "container_volume_stats",
            new MetricConfigDefinition(),
            MetricSettings.defaultSettings("container_volume_stats"),
            resolver);

    assertEquals(1, data.size());
    // Single-AZ primary must not carry the AZ or `-readonly` suffix, but must preserve the
    // datadir suffix so per-volume series stay distinguishable on the graph.
    assertThat(data.get(0).instanceName, allOf(notNullValue(), equalTo("yb-tserver-0_datadir0")));
  }

  @Test
  public void testContainerVolumeMetricsMultiAzReadReplicaNewNamingStyle() {
    // Note the `arr` in the release name: `azRR` = azName ("us-west1-a") + "rr" -> "us-west1-arr".
    // See KubernetesUtil.getHelmReleaseName; this is what causes RR release names to sort
    // differently from primary ones and is why matching purely by namespace collapses them.
    String releaseName = "ybamalyshev-o-us-west1-arr-nlwt";
    String pvc = releaseName + "-datadir0-" + releaseName + "-yb-tserver-0";
    // Primary release for the same AZ - added to the resolver so we can prove that the RR pod
    // still resolves to its own release and doesn't fall through to the (namespace-shared)
    // primary entry.
    ReleaseInfo primary =
        new ReleaseInfo(
            "ybamalyshev-o-us-west1-a-abcd",
            "ybamalyshev-o",
            "us-west1-a",
            true /* isMultiAZ */,
            false /* isReadReplica */);
    ReleaseInfo readReplica =
        new ReleaseInfo(
            releaseName,
            "ybamalyshev-o",
            "us-west1-a",
            true /* isMultiAZ */,
            true /* isReadReplica */);
    JsonNode responseJson =
        Json.parse(
            "{\"status\":\"success\",\"data\":{\"resultType\":\"vector\",\"result\":[{\"metric\":"
                + " {\"persistentvolumeclaim\":\""
                + pvc
                + "\",\"namespace\":\"ybamalyshev-o\",\"az_name\":\"us-west1-a\"},"
                + "\"value\":[1479278137,\"0.5\"]}]}}");
    MetricQueryResponse queryResponse = Json.fromJson(responseJson, MetricQueryResponse.class);
    K8sPodNodeNameResolver resolver =
        K8sPodNodeNameResolver.ofReleases(
            true /* newNamingStyle */, ImmutableList.of(primary, readReplica));

    List<MetricGraphData> data =
        queryResponse.getGraphData(
            "container_volume_stats",
            new MetricConfigDefinition(),
            MetricSettings.defaultSettings("container_volume_stats"),
            resolver);

    assertEquals(1, data.size());
    // Multi-AZ RR must produce the `_<az>-readonly` suffix (matching KubernetesUtil.
    // getKubernetesNodeName) followed by the datadir suffix.
    assertThat(
        data.get(0).instanceName,
        allOf(notNullValue(), equalTo("yb-tserver-0_us-west1-a-readonly_datadir0")));
  }

  @Test
  public void testContainerVolumeMetricsOldNamingStylePreservesDatadir() {
    // Old naming style: PVC is `datadir<N>-yb-<server>-<idx>` (no fullname prefix on either
    // side). Confirm the pattern still handles this and the fallback path still tags the
    // datadir suffix on the reconstructed instance name.
    String pvc = "datadir0-yb-tserver-0";
    JsonNode responseJson =
        Json.parse(
            "{\"status\":\"success\",\"data\":{\"resultType\":\"vector\",\"result\":[{\"metric\":"
                + " {\"persistentvolumeclaim\":\""
                + pvc
                + "\",\"namespace\":\"yb-old-univ\",\"az_name\":\"us-west1-a\"},"
                + "\"value\":[1479278137,\"0.5\"]}]}}");
    MetricQueryResponse queryResponse = Json.fromJson(responseJson, MetricQueryResponse.class);

    List<MetricGraphData> data =
        queryResponse.getGraphData(
            "container_volume_stats",
            new MetricConfigDefinition(),
            MetricSettings.defaultSettings("container_volume_stats"),
            null /* resolver - trigger the label-only fallback (defaults to multi-AZ) */);

    assertEquals(1, data.size());
    // With no resolver, the fallback path defaults to multi-AZ naming so the AZ suffix should be
    // added and the datadir suffix preserved.
    assertThat(
        data.get(0).instanceName,
        allOf(notNullValue(), equalTo("yb-tserver-0_us-west1-a_datadir0")));
  }

  @Test
  public void testK8sKeysGetPodAndVolumeInstanceNames() {
    JsonNode podJson =
        Json.parse(
            "{\"status\":\"success\",\"data\":{\"resultType\":\"vector\",\"result\":[{\"metric\":\n"
                + " {\"pod_name\":\"yb-master-test-pod\",\"container_name\":\"yb-master\","
                + "\"namespace\":\"test-namespace\"},\"value\":[1479278137,\"0.5\"]}]}}");
    List<MetricGraphData> podData =
        Json.fromJson(podJson, MetricQueryResponse.class)
            .getGraphData(
                "k8s_memory_usage",
                new MetricConfigDefinition(),
                MetricSettings.defaultSettings("k8s_memory_usage"));
    assertThat(podData.get(0).instanceName, equalTo("yb-master-test-pod"));

    JsonNode volumeJson =
        Json.parse(
            "{\"status\":\"success\",\"data\":{\"resultType\":\"vector\",\"result\":[{\"metric\":"
                + " {\"persistentvolumeclaim\":\"datadir0-yb-master-0\","
                + "\"namespace\":\"test-namespace\"},\"value\":[1479278137,\"0.5\"]}]}}");
    List<MetricGraphData> volumeData =
        Json.fromJson(volumeJson, MetricQueryResponse.class)
            .getGraphData(
                "k8s_volume_disk_volume_used",
                new MetricConfigDefinition(),
                MetricSettings.defaultSettings("k8s_volume_disk_volume_used"));
    assertThat(volumeData.get(0).instanceName, equalTo("yb-master-0_datadir0"));
  }

  @Test
  public void testK8sVolumeKeysGroupedByClaimAreNamedByVolume() {
    JsonNode responseJson =
        Json.parse(
            "{\"status\":\"success\",\"data\":{\"resultType\":\"vector\",\"result\":["
                + "{\"metric\":{\"persistentvolumeclaim\":\"datadir0-yb-master-0\","
                + "\"namespace\":\"ns\"},\"value\":[1479278137,\"10\"]},"
                + "{\"metric\":{\"persistentvolumeclaim\":\"datadir0-yb-tserver-0\","
                + "\"namespace\":\"ns\"},\"value\":[1479278137,\"20\"]}]}}");
    MetricConfigDefinition config =
        new MetricConfigDefinition()
            .setGroupBy("namespace,persistentvolumeclaim")
            .setLayout(
                new Layout()
                    .setYaxis(
                        new Axis()
                            .setAlias(
                                ImmutableMap.of(
                                    "k8s_volume_disk_volume_used/k8s_volume_disk_volume_capacity",
                                    "Disk Usage"))));
    List<MetricGraphData> data =
        Json.fromJson(responseJson, MetricQueryResponse.class)
            .getGraphData(
                "k8s_volume_disk_volume_usage_percent",
                config,
                MetricSettings.defaultSettings("k8s_volume_disk_volume_usage_percent"));
    assertThat(
        data.stream().map(d -> d.name).collect(Collectors.toList()),
        containsInAnyOrder("yb-master-0_datadir0", "yb-tserver-0_datadir0"));
  }

  @Test
  public void testK8sKeysAreClassifiedByTheirMetricsYmlKey() {
    JsonNode memoryJson =
        Json.parse(
            "{\"status\":\"success\",\"data\":{\"resultType\":\"vector\",\"result\":[{\"metric\":"
                + "{\"pod_name\":\"yb-tserver-0\",\"saved_name\":\"container_memory_cache\","
                + "\"namespace\":\"test-namespace\"},\"value\":[1479278137,\"0.5\"]}]}}");
    List<MetricGraphData> memoryData =
        Json.fromJson(memoryJson, MetricQueryResponse.class)
            .getGraphData(
                "",
                new MetricConfigDefinition(),
                MetricSettings.defaultSettings("k8s_memory_usage"));
    assertThat(memoryData.get(0).instanceName, equalTo("yb-tserver-0"));

    JsonNode ybaDiskJson =
        Json.parse(
            "{\"status\":\"success\",\"data\":{\"resultType\":\"vector\",\"result\":[{\"metric\":"
                + "{\"persistentvolumeclaim\":\"yba-yugaware-storage\"},"
                + "\"value\":[1479278137,\"0.5\"]}]}}");
    List<MetricGraphData> ybaDiskData =
        Json.fromJson(ybaDiskJson, MetricQueryResponse.class)
            .getGraphData(
                "kubelet_volume_stats_used_bytes",
                new MetricConfigDefinition().setGroupBy("persistentvolumeclaim"),
                MetricSettings.defaultSettings("yba_container_disk_used"));
    assertThat(ybaDiskData.get(0).name, equalTo("yba-yugaware-storage"));
  }

  @Test
  public void testK8sVolumeKeysKeepOldNamingNamespacesApart() {
    String series =
        "{\"metric\":{\"persistentvolumeclaim\":\"datadir0-yb-tserver-0\",\"namespace\":\"%s\"},"
            + "\"value\":[1479278137,\"%s\"]}";
    JsonNode responseJson =
        Json.parse(
            "{\"status\":\"success\",\"data\":{\"resultType\":\"vector\",\"result\":["
                + String.format(series, "yb-old-us-west1-a", "1")
                + ","
                + String.format(series, "yb-old-us-west1-b", "2")
                + ","
                + String.format(series, "yb-old-rr-us-west1-a", "3")
                + "]}}");
    K8sPodNodeNameResolver resolver =
        K8sPodNodeNameResolver.ofReleases(
            false /* newNamingStyle */,
            ImmutableList.of(
                new ReleaseInfo("ignored-a", "yb-old-us-west1-a", "us-west1-a", true, false),
                new ReleaseInfo("ignored-b", "yb-old-us-west1-b", "us-west1-b", true, false),
                new ReleaseInfo("ignored-rr", "yb-old-rr-us-west1-a", "us-west1-a", true, true)));
    MetricConfigDefinition config =
        new MetricConfigDefinition().setGroupBy("namespace,persistentvolumeclaim");
    List<MetricGraphData> data =
        Json.fromJson(responseJson, MetricQueryResponse.class)
            .getGraphData(
                "k8s_volume_disk_volume_used",
                config,
                MetricSettings.defaultSettings("k8s_volume_disk_volume_used"),
                resolver);
    assertThat(
        data.stream().map(d -> d.name).collect(Collectors.toList()),
        containsInAnyOrder(
            "yb-tserver-0_us-west1-a_datadir0",
            "yb-tserver-0_us-west1-b_datadir0",
            "yb-tserver-0_us-west1-a-readonly_datadir0"));
  }
}
