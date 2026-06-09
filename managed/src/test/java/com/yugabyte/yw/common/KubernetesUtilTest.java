// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.AZOverrides;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.PerProcessDetails;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntentOverrides;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.CloudInfoInterface;
import com.yugabyte.yw.models.helpers.CloudSpecificInfo;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import java.io.IOException;
import java.util.HashMap;
import java.util.HashSet;
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

  @Test
  public void testNeedsFullMoveSameDeviceInfo() throws IOException {
    Pair<UniverseDefinitionTaskParams, List<AvailabilityZone>> pair =
        addClusterAndNodeDetailsK8s("2024.2.0.0-b2", false);
    UniverseDefinitionTaskParams params = pair.getFirst();
    Cluster currCluster = params.getPrimaryCluster();
    Cluster newCluster = Json.fromJson(Json.toJson(currCluster), Cluster.class);
    assertFalse(KubernetesUtil.needsFullMove(currCluster, newCluster));
  }

  @Test
  public void testNeedsFullMoveOnlyVolumeSizeIncreased() throws IOException {
    Pair<UniverseDefinitionTaskParams, List<AvailabilityZone>> pair =
        addClusterAndNodeDetailsK8s("2024.2.0.0-b2", false);
    UniverseDefinitionTaskParams params = pair.getFirst();
    Cluster currCluster = params.getPrimaryCluster();
    if (currCluster.userIntent.deviceInfo.volumeSize == null) {
      currCluster.userIntent.deviceInfo.volumeSize = 100;
      currCluster.userIntent.deviceInfo.numVolumes = 1;
    }
    Cluster newCluster = Json.fromJson(Json.toJson(currCluster), Cluster.class);
    // Only increase volume size - allowed without full move
    newCluster.userIntent.deviceInfo.volumeSize = currCluster.userIntent.deviceInfo.volumeSize + 10;
    assertFalse(KubernetesUtil.needsFullMove(currCluster, newCluster));
  }

  @Test
  public void testNeedsFullMoveStorageClassChanged() throws IOException {
    Pair<UniverseDefinitionTaskParams, List<AvailabilityZone>> pair =
        addClusterAndNodeDetailsK8s("2024.2.0.0-b2", false);
    UniverseDefinitionTaskParams params = pair.getFirst();
    Cluster currCluster = params.getPrimaryCluster();
    currCluster.userIntent.deviceInfo.volumeSize = 100;
    Cluster newCluster = Json.fromJson(Json.toJson(currCluster), Cluster.class);
    newCluster.userIntent.deviceInfo.storageClass = "fast";
    assertTrue(KubernetesUtil.needsFullMove(currCluster, newCluster));
  }

  @Test
  public void testNeedsFullMoveNumVolumesChanged() throws IOException {
    Pair<UniverseDefinitionTaskParams, List<AvailabilityZone>> pair =
        addClusterAndNodeDetailsK8s("2024.2.0.0-b2", false);
    UniverseDefinitionTaskParams params = pair.getFirst();
    Cluster currCluster = params.getPrimaryCluster();
    currCluster.userIntent.deviceInfo.volumeSize = 100;
    Cluster newCluster = Json.fromJson(Json.toJson(currCluster), Cluster.class);
    if (newCluster.userIntent.deviceInfo.numVolumes == null) {
      newCluster.userIntent.deviceInfo.numVolumes = 1;
    }
    currCluster.userIntent.deviceInfo.numVolumes = 1;
    newCluster.userIntent.deviceInfo.numVolumes = 2;
    assertTrue(KubernetesUtil.needsFullMove(currCluster, newCluster));
  }

  @Test
  public void testNeedsFullMoveVolumeSizeDecreased() throws IOException {
    Pair<UniverseDefinitionTaskParams, List<AvailabilityZone>> pair =
        addClusterAndNodeDetailsK8s("2024.2.0.0-b2", false);
    UniverseDefinitionTaskParams params = pair.getFirst();
    Cluster currCluster = params.getPrimaryCluster();
    if (currCluster.userIntent.deviceInfo.volumeSize == null) {
      currCluster.userIntent.deviceInfo.volumeSize = 100;
      currCluster.userIntent.deviceInfo.numVolumes = 1;
    }
    Cluster newCluster = Json.fromJson(Json.toJson(currCluster), Cluster.class);
    newCluster.userIntent.deviceInfo.volumeSize = 50;
    // Decreasing volume size is a device change (not only volume size increase)
    assertTrue(KubernetesUtil.needsFullMove(currCluster, newCluster));
  }

  /*--- Tests for KubernetesUtil.generateVolumeOverridesForUserIntent ---*/

  /**
   * Helper: creates a kubernetes AZ and sets {@code STORAGE_CLASS} on its provider config so that
   * {@code CloudInfoInterface.fetchEnvVars(zone)} surfaces it - this is what the fallback path of
   * {@code generateVolumeOverridesForUserIntent} reads from.
   */
  private AvailabilityZone createK8sAZWithStorageClass(
      Region region, String code, String storageClass) {
    AvailabilityZone az =
        AvailabilityZone.createOrThrow(region, code, code.toUpperCase(), "subnet-" + code);
    if (storageClass != null) {
      Map<String, String> azConfig = new HashMap<>();
      azConfig.put("STORAGE_CLASS", storageClass);
      CloudInfoInterface.setCloudProviderInfoFromConfig(az, azConfig);
      az.save();
    }
    return az;
  }

  /**
   * Helper: builds a UserIntentOverrides with a single tserver per-AZ deviceInfo entry for the
   * given {@code azUuid}.
   */
  private UserIntentOverrides buildExistingTserverOverride(
      UUID azUuid, int volumeSize, String storageClass) {
    UserIntentOverrides overrides = new UserIntentOverrides();
    Map<UUID, AZOverrides> azMap = new HashMap<>();
    AZOverrides azOv = new AZOverrides();
    Map<ServerType, PerProcessDetails> perProcess = new HashMap<>();
    PerProcessDetails ppd = new PerProcessDetails();
    DeviceInfo di = new DeviceInfo();
    di.volumeSize = volumeSize;
    di.numVolumes = 1;
    di.storageClass = storageClass;
    ppd.setDeviceInfo(di);
    perProcess.put(ServerType.TSERVER, ppd);
    azOv.setPerProcess(perProcess);
    azMap.put(azUuid, azOv);
    overrides.setAzOverrides(azMap);
    return overrides;
  }

  @Test
  public void testGenerateVolumeOverridesPopulatedAZOverrideIsPreserved() {
    Provider provider = ModelFactory.kubernetesProvider(customer);
    Region region = Region.create(provider, "region-1", "Region 1", "default-image");
    AvailabilityZone az1 = createK8sAZWithStorageClass(region, "az-1", "provider-az1-sc");
    AvailabilityZone az2 = createK8sAZWithStorageClass(region, "az-2", "provider-az2-sc");

    // Existing override for az1 with a custom storageClass and volumeSize. Even though
    // provider STORAGE_CLASS and a conflicting azOverrides string-map entry are configured for
    // az1, the per-AZ deviceInfo on userIntentOverrides should remain authoritative on a
    // per-field basis - neither source should overwrite values that are already set.
    UserIntentOverrides existing =
        buildExistingTserverOverride(az1.getUuid(), 500, "custom-az1-sc");

    Map<String, String> azOverridesMap = new HashMap<>();
    azOverridesMap.put("az-1", "storage:\n  tserver:\n    storageClass: should-not-overwrite\n");

    UserIntentOverrides result =
        KubernetesUtil.generateVolumeOverridesForUserIntent(
            existing,
            new HashSet<>(Set.of(az1.getUuid(), az2.getUuid())),
            null /* universeOverridesStr */,
            azOverridesMap,
            null /* skipAZs */);

    assertNotNull(result);
    Map<UUID, AZOverrides> azOverrides = result.getAzOverrides();
    assertNotNull(azOverrides);

    // az1 TSERVER: existing override values must be preserved. Provider STORAGE_CLASS and the
    // azOverrides string-map entry for az-1 must NOT override the populated fields.
    AZOverrides az1Ov = azOverrides.get(az1.getUuid());
    assertNotNull(az1Ov);
    assertNotNull(az1Ov.getPerProcess());
    PerProcessDetails az1Tserver = az1Ov.getPerProcess().get(ServerType.TSERVER);
    assertNotNull(az1Tserver);
    DeviceInfo az1Di = az1Tserver.getDeviceInfo();
    assertNotNull(az1Di);
    assertEquals("custom-az1-sc", az1Di.storageClass);
    assertEquals(500, az1Di.volumeSize.intValue());
    assertEquals(1, az1Di.numVolumes.intValue());

    // az2 had no existing override, so it gets a freshly built override from the provider
    // STORAGE_CLASS for both server types.
    AZOverrides az2Ov = azOverrides.get(az2.getUuid());
    assertNotNull(az2Ov);
    assertNotNull(az2Ov.getPerProcess());
    PerProcessDetails az2Tserver = az2Ov.getPerProcess().get(ServerType.TSERVER);
    assertNotNull(az2Tserver);
    assertEquals("provider-az2-sc", az2Tserver.getDeviceInfo().storageClass);
    PerProcessDetails az2Master = az2Ov.getPerProcess().get(ServerType.MASTER);
    assertNotNull(az2Master);
    assertEquals("provider-az2-sc", az2Master.getDeviceInfo().storageClass);
  }

  @Test
  public void testGenerateVolumeOverridesUsesProviderStorageClassAndAzOverridesMap() {
    Provider provider = ModelFactory.kubernetesProvider(customer);
    Region region = Region.create(provider, "region-1", "Region 1", "default-image");
    AvailabilityZone az1 = createK8sAZWithStorageClass(region, "az-1", "provider-az1-sc");
    AvailabilityZone az2 = createK8sAZWithStorageClass(region, "az-2", "provider-az2-sc");

    // The userIntent.azOverrides map is keyed by AZ code (e.g. "az-1"). For az-1 we override
    // the tserver storageClass via a YAML snippet matching the storage.<server>.storageClass
    // structure that fetchDeviceInfo expects. az-2 has no entry, so it should fall through to
    // just the provider STORAGE_CLASS.
    Map<String, String> azOverridesMap = new HashMap<>();
    azOverridesMap.put("az-1", "storage:\n  tserver:\n    storageClass: az1-tserver-special\n");

    UserIntentOverrides result =
        KubernetesUtil.generateVolumeOverridesForUserIntent(
            null /* userIntentOverrides */,
            new HashSet<>(Set.of(az1.getUuid(), az2.getUuid())),
            null /* universeOverridesStr */,
            azOverridesMap,
            null /* skipAZs */);

    assertNotNull(result);
    Map<UUID, AZOverrides> azOverrides = result.getAzOverrides();
    assertNotNull(azOverrides);

    // az1 tserver: azOverrides string-map entry wins over provider STORAGE_CLASS.
    AZOverrides az1Ov = azOverrides.get(az1.getUuid());
    assertNotNull(az1Ov);
    assertNotNull(az1Ov.getPerProcess());
    PerProcessDetails az1Tserver = az1Ov.getPerProcess().get(ServerType.TSERVER);
    assertNotNull(az1Tserver);
    assertEquals("az1-tserver-special", az1Tserver.getDeviceInfo().storageClass);
    // az1 master: no master entry in azOverrides, so provider STORAGE_CLASS is used.
    PerProcessDetails az1Master = az1Ov.getPerProcess().get(ServerType.MASTER);
    assertNotNull(az1Master);
    assertEquals("provider-az1-sc", az1Master.getDeviceInfo().storageClass);

    // az2: not in azOverrides at all - both server types pick up the provider STORAGE_CLASS.
    AZOverrides az2Ov = azOverrides.get(az2.getUuid());
    assertNotNull(az2Ov);
    assertNotNull(az2Ov.getPerProcess());
    PerProcessDetails az2Tserver = az2Ov.getPerProcess().get(ServerType.TSERVER);
    assertNotNull(az2Tserver);
    assertEquals("provider-az2-sc", az2Tserver.getDeviceInfo().storageClass);
    PerProcessDetails az2Master = az2Ov.getPerProcess().get(ServerType.MASTER);
    assertNotNull(az2Master);
    assertEquals("provider-az2-sc", az2Master.getDeviceInfo().storageClass);
  }
}
