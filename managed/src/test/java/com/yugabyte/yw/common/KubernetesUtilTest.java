// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import java.io.IOException;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import org.yaml.snakeyaml.Yaml;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class KubernetesUtilTest extends FakeDBApplication {

  private Customer customer;

  @Before
  public void setup() {
    customer = ModelFactory.testCustomer();
  }

  private Pair<UniverseDefinitionTaskParams, List<AvailabilityZone>> addClusterAndNodeDetailsK8s(
      String ybVersion, boolean createRR) {
    DeviceInfo deviceInfo = new DeviceInfo();
    ObjectNode deviceInfoObjectNode = Json.toJson(deviceInfo).deepCopy();
    Pair<ObjectNode, List<AvailabilityZone>> universeDetailsPair =
        ModelFactory.addClusterAndNodeDetailsK8s(
            ybVersion, createRR, customer, deviceInfoObjectNode);
    UniverseDefinitionTaskParams universeParams =
        Json.fromJson(universeDetailsPair.getFirst(), UniverseDefinitionTaskParams.class);
    universeParams.useNewHelmNamingStyle = true;
    universeParams.getPrimaryCluster().userIntent.defaultServiceScopeAZ = false;
    return new Pair<>(universeParams, universeDetailsPair.getSecond());
  }

  private Map<String, String> universeConfig() {
    Map<String, String> universeConfig = new HashMap<>();
    universeConfig.put(Universe.LABEL_K8S_RESOURCES, "true");
    return universeConfig;
  }

  @Test
  public void testExtraNSScopedServiceToRemoveRRDeleteDifferentNS() throws IOException {
    Pair<UniverseDefinitionTaskParams, List<AvailabilityZone>> pair =
        addClusterAndNodeDetailsK8s("2024.2.0.0-b2", true /* createRR */);
    Map<String, String> universeConfig = universeConfig();
    String serviceEndpoint = TestUtils.readResource("kubernetes/service_endpoint_overrides.yaml");
    Map<String, String> azConfig = new HashMap<>();

    // All primary AZs in one NS "ns-1"
    azConfig.put("OVERRIDES", serviceEndpoint);
    azConfig.put("KUBENAMESPACE", "ns-1");
    List<AvailabilityZone> zones = pair.getSecond();
    CloudInfoInterface.setCloudProviderInfoFromConfig(zones.get(0), azConfig);
    zones.get(0).save();
    CloudInfoInterface.setCloudProviderInfoFromConfig(zones.get(1), azConfig);
    zones.get(1).save();
    CloudInfoInterface.setCloudProviderInfoFromConfig(zones.get(2), azConfig);
    zones.get(2).save();

    // RR in az-4
    azConfig.put("KUBENAMESPACE", "ns-2");
    CloudInfoInterface.setCloudProviderInfoFromConfig(zones.get(3), azConfig);
    zones.get(3).save();

    Map<UUID, Set<String>> svcsToRemove =
        KubernetesUtil.getExtraNSScopeServicesToRemove(
            null /* taskParams */,
            pair.getFirst(),
            universeConfig,
            ClusterType.ASYNC,
            true /* deleteCluster */);
    assertEquals(1, svcsToRemove.size());
    Map.Entry<UUID, Set<String>> entry = svcsToRemove.entrySet().iterator().next();
    assertEquals(zones.get(3).getUuid(), entry.getKey());
    // delete all this NS has
    assertNull(entry.getValue());
  }

  @Test
  public void testExtraNSScopedServiceToRemoveRRDeleteSameNS() throws IOException {
    Pair<UniverseDefinitionTaskParams, List<AvailabilityZone>> pair =
        addClusterAndNodeDetailsK8s("2024.2.0.0-b2", true /* createRR */);
    Map<String, String> universeConfig = universeConfig();
    String serviceEndpoint = TestUtils.readResource("kubernetes/service_endpoint_overrides.yaml");
    Map<String, String> azConfig = new HashMap<>();

    // All AZs in one NS "ns-1"
    azConfig.put("OVERRIDES", serviceEndpoint);
    azConfig.put("KUBENAMESPACE", "ns-1");
    List<AvailabilityZone> zones = pair.getSecond();
    CloudInfoInterface.setCloudProviderInfoFromConfig(zones.get(0), azConfig);
    zones.get(0).save();
    CloudInfoInterface.setCloudProviderInfoFromConfig(zones.get(1), azConfig);
    zones.get(1).save();
    CloudInfoInterface.setCloudProviderInfoFromConfig(zones.get(2), azConfig);
    zones.get(2).save();

    // RR in az-4
    CloudInfoInterface.setCloudProviderInfoFromConfig(zones.get(3), azConfig);
    zones.get(3).save();

    Map<UUID, Set<String>> svcsToRemove =
        KubernetesUtil.getExtraNSScopeServicesToRemove(
            null /* taskParams */,
            pair.getFirst(),
            universeConfig,
            ClusterType.ASYNC,
            true /* deleteCluster */);
    assertEquals(0, svcsToRemove.size());
  }

  @Test
  public void testExtraNSScopedServiceToRemoveOverridesChange() throws IOException {
    Pair<UniverseDefinitionTaskParams, List<AvailabilityZone>> pair =
        addClusterAndNodeDetailsK8s("2024.2.0.0-b2", false /* createRR */);
    Map<String, String> universeConfig = universeConfig();
    String serviceEndpoint = TestUtils.readResource("kubernetes/service_endpoint_overrides.yaml");
    String serviceEndpointsOverriden =
        TestUtils.readResource("kubernetes/modified_service_endpoint_overrides.yaml");
    UniverseDefinitionTaskParams universeParams = pair.getFirst();
    universeParams.getPrimaryCluster().userIntent.universeOverrides = serviceEndpoint;

    UniverseDefinitionTaskParams taskParams =
        Json.fromJson(Json.toJson(universeParams), UniverseDefinitionTaskParams.class);
    taskParams.getPrimaryCluster().userIntent.universeOverrides = serviceEndpointsOverriden;

    Map<UUID, Set<String>> svcsToRemove =
        KubernetesUtil.getExtraNSScopeServicesToRemove(
            taskParams,
            universeParams,
            universeConfig,
            ClusterType.PRIMARY /* clusterType */,
            false /* deleteCluster */);
    assertEquals(1, svcsToRemove.size());
    Map.Entry<UUID, Set<String>> entry = svcsToRemove.entrySet().iterator().next();
    assertEquals(1, entry.getValue().size());
    assertTrue(entry.getValue().contains("yb-master-service"));
  }

  @Test
  public void testExtraNSScopedServiceToRemoveDeleteAZMultiNS() throws IOException {
    Pair<UniverseDefinitionTaskParams, List<AvailabilityZone>> pair =
        addClusterAndNodeDetailsK8s("2024.2.0.0-b2", false /* createRR */);
    Map<String, String> universeConfig = universeConfig();
    String serviceEndpoint = TestUtils.readResource("kubernetes/service_endpoint_overrides.yaml");
    Map<String, String> azConfig = new HashMap<>();

    // 2 primary AZs in one NS "ns-1"
    azConfig.put("OVERRIDES", serviceEndpoint);
    azConfig.put("KUBENAMESPACE", "ns-1");
    List<AvailabilityZone> zones = pair.getSecond();
    CloudInfoInterface.setCloudProviderInfoFromConfig(zones.get(0), azConfig);
    zones.get(0).save();
    CloudInfoInterface.setCloudProviderInfoFromConfig(zones.get(1), azConfig);
    zones.get(1).save();

    // Third primary AZ in separate NS
    azConfig.put("KUBENAMESPACE", "ns-2");
    CloudInfoInterface.setCloudProviderInfoFromConfig(zones.get(2), azConfig);
    zones.get(2).save();

    UniverseDefinitionTaskParams universeParams = pair.getFirst();
    UniverseDefinitionTaskParams taskParams =
        Json.fromJson(Json.toJson(universeParams), UniverseDefinitionTaskParams.class);
    // New placement info
    Map<UUID, Integer> azToNumNodesMap = new HashMap<>();
    azToNumNodesMap.put(zones.get(0).getUuid(), 2);
    azToNumNodesMap.put(zones.get(1).getUuid(), 1);
    PlacementInfo pi = ModelFactory.constructPlacementInfoObject(azToNumNodesMap);
    taskParams.getPrimaryCluster().placementInfo = pi;
    String instanceType = taskParams.nodeDetailsSet.iterator().next().cloudInfo.instance_type;

    // New node details
    NodeDetails node1 = new NodeDetails();
    node1.cloudInfo = new CloudSpecificInfo();
    node1.cloudInfo.instance_type = instanceType;
    node1.cloudInfo.az = zones.get(0).getName();
    node1.azUuid = zones.get(0).getUuid();
    node1.nodeName = "demo-pod-1";
    node1.placementUuid = taskParams.getPrimaryCluster().uuid;
    NodeDetails node2 = new NodeDetails();
    node2.cloudInfo = new CloudSpecificInfo();
    node2.cloudInfo.instance_type = instanceType;
    node2.cloudInfo.az = zones.get(0).getName();
    node2.azUuid = zones.get(0).getUuid();
    node2.nodeName = "demo-pod-2";
    node2.placementUuid = taskParams.getPrimaryCluster().uuid;
    NodeDetails node3 = new NodeDetails();
    node3.cloudInfo = new CloudSpecificInfo();
    node3.cloudInfo.instance_type = instanceType;
    node3.cloudInfo.az = zones.get(1).getName();
    node3.azUuid = zones.get(1).getUuid();
    node3.nodeName = "demo-pod-3";
    node3.placementUuid = taskParams.getPrimaryCluster().uuid;
    taskParams.nodeDetailsSet = Set.of(node1, node2, node3);

    Map<UUID, Set<String>> svcsToRemove =
        KubernetesUtil.getExtraNSScopeServicesToRemove(
            taskParams,
            universeParams,
            universeConfig,
            ClusterType.PRIMARY /* clusterType */,
            false /* deleteCluster */);
    assertEquals(1, svcsToRemove.size());
    Map.Entry<UUID, Set<String>> entry = svcsToRemove.entrySet().iterator().next();
    // delete all this NS has
    assertNull(entry.getValue());
  }

  @Test
  public void testExtraNSScopedServiceToRemoveDeleteAZSingleNS() throws IOException {
    Pair<UniverseDefinitionTaskParams, List<AvailabilityZone>> pair =
        addClusterAndNodeDetailsK8s("2024.2.0.0-b2", false /* createRR */);
    Map<String, String> universeConfig = universeConfig();
    String serviceEndpoint = TestUtils.readResource("kubernetes/service_endpoint_overrides.yaml");
    Map<String, String> azConfig = new HashMap<>();

    // 2 primary AZs in one NS "ns-1"
    azConfig.put("OVERRIDES", serviceEndpoint);
    azConfig.put("KUBENAMESPACE", "ns-1");
    List<AvailabilityZone> zones = pair.getSecond();
    CloudInfoInterface.setCloudProviderInfoFromConfig(zones.get(0), azConfig);
    zones.get(0).save();
    CloudInfoInterface.setCloudProviderInfoFromConfig(zones.get(1), azConfig);
    zones.get(1).save();
    CloudInfoInterface.setCloudProviderInfoFromConfig(zones.get(2), azConfig);
    zones.get(2).save();

    UniverseDefinitionTaskParams universeParams = pair.getFirst();
    UniverseDefinitionTaskParams taskParams =
        Json.fromJson(Json.toJson(universeParams), UniverseDefinitionTaskParams.class);
    // New placement info
    Map<UUID, Integer> azToNumNodesMap = new HashMap<>();
    azToNumNodesMap.put(zones.get(0).getUuid(), 2);
    azToNumNodesMap.put(zones.get(1).getUuid(), 1);
    PlacementInfo pi = ModelFactory.constructPlacementInfoObject(azToNumNodesMap);
    taskParams.getPrimaryCluster().placementInfo = pi;
    String instanceType = taskParams.nodeDetailsSet.iterator().next().cloudInfo.instance_type;

    // New node details
    NodeDetails node1 = new NodeDetails();
    node1.cloudInfo = new CloudSpecificInfo();
    node1.cloudInfo.instance_type = instanceType;
    node1.cloudInfo.az = zones.get(0).getName();
    node1.azUuid = zones.get(0).getUuid();
    node1.nodeName = "demo-pod-1";
    node1.placementUuid = taskParams.getPrimaryCluster().uuid;
    NodeDetails node2 = new NodeDetails();
    node2.cloudInfo = new CloudSpecificInfo();
    node2.cloudInfo.instance_type = instanceType;
    node2.cloudInfo.az = zones.get(0).getName();
    node2.azUuid = zones.get(0).getUuid();
    node2.nodeName = "demo-pod-2";
    node2.placementUuid = taskParams.getPrimaryCluster().uuid;
    NodeDetails node3 = new NodeDetails();
    node3.cloudInfo = new CloudSpecificInfo();
    node3.cloudInfo.instance_type = instanceType;
    node3.cloudInfo.az = zones.get(1).getName();
    node3.azUuid = zones.get(1).getUuid();
    node3.nodeName = "demo-pod-3";
    node3.placementUuid = taskParams.getPrimaryCluster().uuid;
    taskParams.nodeDetailsSet = Set.of(node1, node2, node3);

    Map<UUID, Set<String>> svcsToRemove =
        KubernetesUtil.getExtraNSScopeServicesToRemove(
            taskParams,
            universeParams,
            universeConfig,
            ClusterType.PRIMARY /* clusterType */,
            false /* deleteCluster */);
    // Nothing to be deleted
    assertEquals(0, svcsToRemove.size());
  }

  @Test
  public void testCoreCountForUniverseWithoutOverrides() throws IOException {
    Pair<UniverseDefinitionTaskParams, List<AvailabilityZone>> pair =
        addClusterAndNodeDetailsK8s("2024.2.0.0-b2", false /* createRR */);
    UniverseDefinitionTaskParams universeParams = pair.getFirst();
    double tserverCoreCount = 8.0;
    double masterCoreCount = 4.0;
    UserIntent.K8SNodeResourceSpec tserverSpec = new UserIntent.K8SNodeResourceSpec();
    tserverSpec.setCpuCoreCount(tserverCoreCount);
    UserIntent.K8SNodeResourceSpec masterSpec = new UserIntent.K8SNodeResourceSpec();
    masterSpec.setCpuCoreCount(masterCoreCount);
    UserIntent userIntent = universeParams.getPrimaryCluster().userIntent;
    userIntent.tserverK8SNodeResourceSpec = tserverSpec;
    userIntent.masterK8SNodeResourceSpec = masterSpec;
    for (NodeDetails node : universeParams.nodeDetailsSet) {
      double coreCount = KubernetesUtil.getCoreCountForUniverseForServer(userIntent, null, node);
      assertEquals(node.isTserver ? tserverCoreCount : masterCoreCount, coreCount, 0.1);
    }
  }

  @Test
  public void testCoreCountForUniverseWithOverrides() throws IOException {
    Pair<UniverseDefinitionTaskParams, List<AvailabilityZone>> pair =
        addClusterAndNodeDetailsK8s("2024.2.0.0-b2", false /* createRR */);
    UniverseDefinitionTaskParams universeParams = pair.getFirst();
    String overrides = TestUtils.readResource("kubernetes/universe_cpu_overrides.yaml");
    universeParams.getPrimaryCluster().userIntent.tserverK8SNodeResourceSpec =
        new UserIntent.K8SNodeResourceSpec();
    universeParams.getPrimaryCluster().userIntent.masterK8SNodeResourceSpec =
        new UserIntent.K8SNodeResourceSpec();
    Map<UUID, Map<String, Object>> azOverridesMap =
        KubernetesUtil.getFinalOverrides(
            universeParams.getPrimaryCluster(),
            overrides,
            universeParams.getPrimaryCluster().userIntent.azOverrides);
    for (NodeDetails node : universeParams.nodeDetailsSet) {
      double coreCount =
          KubernetesUtil.getCoreCountForUniverseForServer(
              universeParams.getPrimaryCluster().userIntent, azOverridesMap, node);
      assertEquals(node.isTserver ? 16 : 8, coreCount, 0.1);
    }
  }

  @Test
  public void testCoreCountForUniverseWithAZOverrides() throws IOException {
    Pair<UniverseDefinitionTaskParams, List<AvailabilityZone>> pair =
        addClusterAndNodeDetailsK8s("2024.2.0.0-b2", false /* createRR */);
    UniverseDefinitionTaskParams universeParams = pair.getFirst();
    Yaml yaml = new Yaml();
    Map<String, String> azOverridesMap =
        yaml.load(TestUtils.readResource("kubernetes/az_cpu_overrides.yaml"));
    universeParams.getPrimaryCluster().userIntent.tserverK8SNodeResourceSpec =
        new UserIntent.K8SNodeResourceSpec();
    universeParams.getPrimaryCluster().userIntent.masterK8SNodeResourceSpec =
        new UserIntent.K8SNodeResourceSpec();
    Map<UUID, Map<String, Object>> finalAzOverrides =
        KubernetesUtil.getFinalOverrides(universeParams.getPrimaryCluster(), "", azOverridesMap);

    Map<String, Map<String, Double>> expectedCpuValues =
        Map.of(
            "PlacementAZ 1", Map.of("master", 2.0, "tserver", 10.0),
            "PlacementAZ 2", Map.of("master", 6.0, "tserver", 2.0),
            "PlacementAZ 3", Map.of("master", 2.0, "tserver", 2.0));

    for (NodeDetails node : universeParams.nodeDetailsSet) {
      double expected =
          expectedCpuValues.get(node.getZone()).get(node.isTserver ? "tserver" : "master");
      double coreCount =
          KubernetesUtil.getCoreCountForUniverseForServer(
              universeParams.getPrimaryCluster().userIntent, finalAzOverrides, node);
      assertEquals(expected, coreCount, 0.1);
    }
  }
}
