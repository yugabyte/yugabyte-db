// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.alerts;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.Iterables;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.AlertTemplate;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.KubernetesUtil;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.alerts.impl.AlertTemplateService;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.models.AlertConfiguration;
import com.yugabyte.yw.models.AlertConfiguration.Severity;
import com.yugabyte.yw.models.AlertConfigurationThreshold;
import com.yugabyte.yw.models.AlertDefinition;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.common.Condition;
import com.yugabyte.yw.models.filters.AlertDefinitionFilter;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.stream.Collectors;
import org.apache.commons.io.IOUtils;
import org.junit.Assume;
import org.junit.Before;
import org.junit.Test;
import play.libs.Json;

public class KubernetesAlertQueriesTest extends FakeDBApplication {

  private static final List<AlertTemplate> VM_TEMPLATES =
      List.of(
          AlertTemplate.NODE_CPU_USAGE,
          AlertTemplate.MEMORY_CONSUMPTION,
          AlertTemplate.NODE_DISK_USAGE);
  private static final List<AlertTemplate> K8S_TEMPLATES =
      List.of(
          AlertTemplate.DB_K8S_CPU_USAGE,
          AlertTemplate.DB_K8S_MEMORY_USAGE,
          AlertTemplate.DB_K8S_DATA_VOLUME_USAGE);

  private Customer customer;
  private Universe k8s;
  private Universe vm;
  private RuntimeConfGetter confGetter;
  private AlertTemplateService alertTemplateService;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    k8s = ModelFactory.createUniverse("k8s-alerts", customer.getId(), CloudType.kubernetes);
    Provider provider =
        Provider.getOrBadRequest(
            java.util.UUID.fromString(
                k8s.getUniverseDetails().getPrimaryCluster().userIntent.provider));
    Region region = Region.create(provider, "region-1", "Region 1", "default-image");
    CloudInfoInterface.setCloudProviderInfoFromConfig(
        region, Map.of("KUBECONFIG", "/tmp/kubeconfig"));
    region.save();
    AvailabilityZone.createOrThrow(region, "az-1", "AZ 1", "subnet-1");
    vm = ModelFactory.createUniverse("vm-alerts", customer.getId());
    confGetter = app.injector().instanceOf(RuntimeConfGetter.class);
    alertTemplateService = app.injector().instanceOf(AlertTemplateService.class);
  }

  @Test
  public void testSelectorsCoverTheUniversesPods() {
    Map<String, String> selectors = MetricQueryHelper.getAlertKubernetesSelectors(k8s);
    assertEquals(2, selectors.size());
    assertFalse(selectors.get("__k8sNamespaces__").isEmpty());
    String podNames = selectors.get("__k8sPodNames__");
    assertEquals("yb-(master|tserver)(-[0-9]+)?-[0-9]+", podNames);
    for (String pod : List.of("yb-tserver-0", "yb-master-2", "yb-tserver-1-0", "yb-master-1-2")) {
      assertTrue(pod, pod.matches(podNames));
    }
    assertFalse(podNames, "yb-tserver-0-backup".matches(podNames));
  }

  @Test
  public void testNewNamingSelectorsMatchOnlyTheUniversesReleases() {
    k8s =
        Universe.saveDetails(
            k8s.getUniverseUUID(), u -> u.getUniverseDetails().useNewHelmNamingStyle = true);
    String release =
        KubernetesUtil.getHelmReleaseName(
            false, k8s.getUniverseDetails().nodePrefix, k8s.getName(), "AZ 1", false, true);
    String podNames = MetricQueryHelper.getAlertKubernetesSelectors(k8s).get("__k8sPodNames__");
    for (String pod : List.of(release + "-yb-tserver-0", release + "-yb-tserver-1-0")) {
      assertTrue(pod, pod.matches(podNames));
    }
    assertFalse(podNames, "ybother-univ-abcd-yb-tserver-0".matches(podNames));
  }

  @Test
  public void testMultiAzSelectorsUseTheZoneCode() {
    Provider provider =
        Provider.getOrBadRequest(
            java.util.UUID.fromString(
                k8s.getUniverseDetails().getPrimaryCluster().userIntent.provider));
    Region region = Region.getByCode(provider, "region-1");
    AvailabilityZone.createOrThrow(region, "az-2", "AZ 2", "subnet-2");
    k8s =
        Universe.saveDetails(
            k8s.getUniverseUUID(), u -> u.getUniverseDetails().useNewHelmNamingStyle = true);
    String podNames = MetricQueryHelper.getAlertKubernetesSelectors(k8s).get("__k8sPodNames__");
    for (String azCode : List.of("az-1", "az-2")) {
      String release =
          KubernetesUtil.getHelmReleaseName(
              true, k8s.getUniverseDetails().nodePrefix, k8s.getName(), azCode, false, true);
      assertTrue(release, (release + "-yb-tserver-0").matches(podNames));
    }
  }

  // The tasks read KUBENAMESPACE from the region and zone config merged.
  @Test
  public void testSelectorsUseARegionLevelNamespace() {
    Provider provider =
        Provider.getOrBadRequest(
            java.util.UUID.fromString(
                k8s.getUniverseDetails().getPrimaryCluster().userIntent.provider));
    Region region = Region.getByCode(provider, "region-1");
    CloudInfoInterface.setCloudProviderInfoFromConfig(
        region, Map.of("KUBECONFIG", "/tmp/kubeconfig", "KUBENAMESPACE", "region-ns"));
    region.save();
    assertEquals(
        "region-ns", MetricQueryHelper.getAlertKubernetesSelectors(k8s).get("__k8sNamespaces__"));
  }

  @Test
  public void testRenderedQueriesSelectTheUniverse() {
    for (AlertTemplate template : K8S_TEMPLATES) {
      String k8sQuery = query(template, k8s);
      assertTrue(template.name(), k8sQuery.contains("universe_uuid=\"" + k8s.getUniverseUUID()));
      assertFalse(template.name(), k8sQuery.contains("__"));
    }
    assertTrue(
        query(AlertTemplate.DB_K8S_CPU_USAGE, k8s)
            .contains("pod_name=~\"yb-(master|tserver)(-[0-9]+)?-[0-9]+\""));
    for (AlertTemplate template : VM_TEMPLATES) {
      assertFalse(template.name(), query(template, vm).contains("container_"));
    }
  }

  @Test
  public void testDefinitionsFollowTheUniverseType() {
    AlertConfigurationService configurationService =
        app.injector().instanceOf(AlertConfigurationService.class);
    AlertDefinitionService definitionService =
        app.injector().instanceOf(AlertDefinitionService.class);
    for (AlertTemplate template : Iterables.concat(VM_TEMPLATES, K8S_TEMPLATES)) {
      AlertConfiguration configuration =
          configurationService
              .createConfigurationTemplate(customer, template)
              .getDefaultConfiguration();
      configurationService.save(configuration);
      Set<String> universes =
          definitionService
              .list(
                  AlertDefinitionFilter.builder()
                      .configurationUuid(configuration.getUuid())
                      .build())
              .stream()
              .map(d -> d.getLabelValue(KnownAlertLabels.UNIVERSE_UUID))
              .collect(Collectors.toSet());
      Universe expected = K8S_TEMPLATES.contains(template) ? k8s : vm;
      assertEquals(template.name(), Set.of(expected.getUniverseUUID().toString()), universes);
    }
  }

  @Test
  public void testRenderedQueriesPassPromtoolTests() throws Exception {
    try {
      new ProcessBuilder("promtool", "--version").start().waitFor();
    } catch (IOException e) {
      Assume.assumeTrue("promtool is not on PATH", false);
    }
    String vmCpu = query(AlertTemplate.NODE_CPU_USAGE, vm);
    String cpu = query(AlertTemplate.DB_K8S_CPU_USAGE, k8s);
    String memory = query(AlertTemplate.DB_K8S_MEMORY_USAGE, k8s);
    String volume = query(AlertTemplate.DB_K8S_DATA_VOLUME_USAGE, k8s);
    String namespace =
        MetricQueryHelper.getAlertKubernetesSelectors(k8s).get("__k8sNamespaces__").split("\\|")[0];
    String k8sUuid = k8s.getUniverseUUID().toString();
    String vmUuid = vm.getUniverseUUID().toString();

    ObjectNode test = Json.newObject().put("interval", "1m");
    ArrayNode series = test.putArray("input_series");
    // yb-tserver-0: 0.5 core used against a 0.5 core request; memory 950 of 1000 bytes not cache.
    String pod0 = "namespace=\"" + namespace + "\",pod_name=\"yb-tserver-0\"";
    series(
        series,
        "container_cpu_usage_seconds_total{container_name=\"yb-tserver\"," + pod0 + "}",
        "0+30x45");
    series(
        series,
        "container_memory_usage_bytes{container_name=\"yb-tserver\"," + pod0 + "}",
        "1000x45");
    series(series, "container_memory_cache{container_name=\"yb-tserver\"," + pod0 + "}", "50x45");
    series(
        series,
        "container_spec_memory_limit_bytes{container_name=\"yb-tserver\"," + pod0 + "}",
        "1000x45");
    String request0 =
        "ybp_container_resource_requests_cpu_cores{container_name=\"yb-tserver\","
            + pod0
            + ",universe_uuid=\""
            + k8sUuid
            + "\",node_name=\"yb-tserver-0_az-1\"";
    series(series, request0 + "}", "0.5x45");
    // A second copy of the request series with another label value must not break the join.
    series(series, request0 + ",az_name=\"az-1\"}", "0.5x45");
    // yb-tserver-1 has no memory limit: it must not count.
    String pod1 = "namespace=\"" + namespace + "\",pod_name=\"yb-tserver-1\"";
    series(
        series,
        "container_memory_usage_bytes{container_name=\"yb-tserver\"," + pod1 + "}",
        "1000x45");
    series(series, "container_memory_cache{container_name=\"yb-tserver\"," + pod1 + "}", "0x45");
    series(
        series,
        "container_spec_memory_limit_bytes{container_name=\"yb-tserver\"," + pod1 + "}",
        "0x45");
    series(
        series,
        "ybp_container_resource_requests_cpu_cores{container_name=\"yb-tserver\","
            + pod1
            + ",universe_uuid=\""
            + k8sUuid
            + "\",node_name=\"yb-tserver-1_az-1\"}",
        "0.5x45");
    // yb-tserver-0's data volume is 95% full, yb-tserver-1's 10%.
    series(
        series,
        "kubelet_volume_stats_used_bytes{namespace=\""
            + namespace
            + "\",persistentvolumeclaim=\"datadir0-yb-tserver-0\"}",
        "95x45");
    series(
        series,
        "kubelet_volume_stats_capacity_bytes{namespace=\""
            + namespace
            + "\",persistentvolumeclaim=\"datadir0-yb-tserver-0\"}",
        "100x45");
    series(
        series,
        "kubelet_volume_stats_used_bytes{namespace=\""
            + namespace
            + "\",persistentvolumeclaim=\"datadir0-yb-tserver-1\"}",
        "10x45");
    series(
        series,
        "kubelet_volume_stats_capacity_bytes{namespace=\""
            + namespace
            + "\",persistentvolumeclaim=\"datadir0-yb-tserver-1\"}",
        "100x45");
    // A VM node at 95% CPU.
    series(
        series,
        "node_cpu_seconds_total{job=\"node\",mode=\"user\",cpu=\"0\",universe_uuid=\""
            + vmUuid
            + "\",node_name=\"vm-n1\"}",
        "0+57x45");
    series(
        series,
        "node_cpu_seconds_total{job=\"node\",mode=\"idle\",cpu=\"0\",universe_uuid=\""
            + vmUuid
            + "\",node_name=\"vm-n1\"}",
        "0+3x45");

    ArrayNode exprs = test.putArray("promql_expr_test");
    expr(exprs, cpu, "{universe_uuid=\"" + k8sUuid + "\"}", 1);
    expr(exprs, memory, "{universe_uuid=\"" + k8sUuid + "\"}", 95);
    expr(exprs, volume, "{universe_uuid=\"" + k8sUuid + "\"}", 1);
    expr(exprs, vmCpu, "{universe_uuid=\"" + vmUuid + "\"}", 1);

    ObjectNode doc = Json.newObject();
    doc.putArray("rule_files");
    doc.put("evaluation_interval", "1m");
    doc.putArray("tests").add(test);
    File dir = Files.createTempDirectory("k8s-alert-queries").toFile();
    File testFile = new File(dir, "alert_queries_test.yml");
    Files.writeString(testFile.toPath(), Json.prettyPrint(doc), StandardCharsets.UTF_8);
    Process process =
        new ProcessBuilder("promtool", "test", "rules", testFile.getName())
            .directory(dir)
            .redirectErrorStream(true)
            .start();
    String output = IOUtils.toString(process.getInputStream(), StandardCharsets.UTF_8);
    assertEquals(output, 0, process.waitFor());
  }

  private String query(AlertTemplate template, Universe universe) {
    AlertConfiguration configuration =
        ModelFactory.createAlertConfiguration(
            customer,
            universe,
            c ->
                c.setTemplate(template)
                    .setThresholds(
                        Map.of(
                            Severity.SEVERE,
                            new AlertConfigurationThreshold()
                                .setCondition(Condition.GREATER_THAN)
                                .setThreshold(90D))));
    AlertDefinition definition =
        ModelFactory.createAlertDefinition(customer, universe, configuration);
    return alertTemplateService
        .getTemplateDescription(template)
        .getQueryWithThreshold(configuration, definition, Severity.SEVERE, confGetter);
  }

  private static void series(ArrayNode series, String name, String values) {
    series.addObject().put("series", name).put("values", values);
  }

  private static void expr(ArrayNode exprs, String query, String labels, double value) {
    ObjectNode e = exprs.addObject().put("expr", query).put("eval_time", "40m");
    e.putArray("exp_samples").addObject().put("labels", labels).put("value", value);
  }
}
