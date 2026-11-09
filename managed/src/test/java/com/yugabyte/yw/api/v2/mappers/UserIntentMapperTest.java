// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.api.v2.mappers;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import api.v2.mappers.UserIntentMapper;
import api.v2.models.AvailabilityZoneNodeSpec;
import api.v2.models.ClusterAddSpec;
import api.v2.models.ClusterEditSpec;
import api.v2.models.ClusterGFlags;
import api.v2.models.ClusterNodeSpec;
import api.v2.models.ClusterPerProcessNodeSpec;
import api.v2.models.ClusterResizeNodeSpec;
import api.v2.models.ClusterSpec;
import api.v2.models.K8SNodeResourceSpec;
import api.v2.models.UniverseResizeNodesCluster;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.AZOverrides;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.PerProcessDetails;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntentOverrides;
import java.util.Collections;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class UserIntentMapperTest {

  @Test
  public void testNullUserIntentReturnsNull() {
    assertNull(UserIntentMapper.INSTANCE.userIntentToClusterNodeSpec(null));
  }

  @Test
  public void testPerProcessOverridesMapToMasterAndTserverNodeSpec() {
    UserIntent userIntent = new UserIntent();
    userIntent.dedicatedNodes = true;
    TestUtils.getProviderInitializerForTests(userIntent, UUID.randomUUID())
        .setInstanceType("c5.xlarge")
        .setDeviceInfo(ApiUtils.getDummyDeviceInfo(1, 100));

    PerProcessDetails masterDetails = new PerProcessDetails();
    masterDetails.setInstanceType("c5.4xlarge");
    masterDetails.setDeviceInfo(ApiUtils.getDummyDeviceInfo(1, 50));

    PerProcessDetails tserverDetails = new PerProcessDetails();
    tserverDetails.setInstanceType("c5.2xlarge");
    tserverDetails.setDeviceInfo(ApiUtils.getDummyDeviceInfo(2, 200));

    Map<ServerType, PerProcessDetails> perProcess = new HashMap<>();
    perProcess.put(ServerType.MASTER, masterDetails);
    perProcess.put(ServerType.TSERVER, tserverDetails);

    UserIntentOverrides overrides = new UserIntentOverrides();
    overrides.setPerProcess(perProcess);
    userIntent.setUserIntentOverrides(overrides);

    ClusterNodeSpec clusterNodeSpec =
        UserIntentMapper.INSTANCE.userIntentToClusterNodeSpec(userIntent);

    assertNotNull(clusterNodeSpec.getMaster());
    assertEquals("c5.4xlarge", clusterNodeSpec.getMaster().getInstanceType());
    assertEquals(Integer.valueOf(50), clusterNodeSpec.getMaster().getStorageSpec().getVolumeSize());

    assertNotNull(clusterNodeSpec.getTserver());
    assertEquals("c5.2xlarge", clusterNodeSpec.getTserver().getInstanceType());
    assertEquals(
        Integer.valueOf(200), clusterNodeSpec.getTserver().getStorageSpec().getVolumeSize());
    assertEquals(Integer.valueOf(2), clusterNodeSpec.getTserver().getStorageSpec().getNumVolumes());
  }

  @Test
  public void testLegacyMasterFieldsMapToMasterNodeSpec() {
    UserIntent userIntent = new UserIntent();
    userIntent.dedicatedNodes = true;

    TestUtils.getProviderInitializerForTests(userIntent, UUID.randomUUID())
        .setInstanceType("c5.xlarge")
        .setDeviceInfo(ApiUtils.getDummyDeviceInfo(1, 100))
        .setMasterInstanceType("m5.2xlarge")
        .setMasterDeviceInfo(ApiUtils.getDummyDeviceInfo(1, 75));

    ClusterNodeSpec clusterNodeSpec =
        UserIntentMapper.INSTANCE.userIntentToClusterNodeSpec(userIntent);

    ClusterPerProcessNodeSpec masterNodeSpec = clusterNodeSpec.getMaster();
    assertNotNull(masterNodeSpec);
    assertEquals("m5.2xlarge", masterNodeSpec.getInstanceType());
    assertEquals(Integer.valueOf(75), masterNodeSpec.getStorageSpec().getVolumeSize());
    assertNull(clusterNodeSpec.getTserver());
  }

  @Test
  public void testLegacyMasterFieldsOverridePerProcessMasterSpec() {
    UserIntent userIntent = new UserIntent();
    userIntent.dedicatedNodes = true;
    TestUtils.getProviderInitializerForTests(userIntent, UUID.randomUUID())
        .setInstanceType("c5.xlarge")
        .setDeviceInfo(ApiUtils.getDummyDeviceInfo(1, 100))
        .setMasterInstanceType("m5.2xlarge")
        .setMasterDeviceInfo(ApiUtils.getDummyDeviceInfo(1, 75));

    PerProcessDetails masterDetails = new PerProcessDetails();
    masterDetails.setInstanceType("c5.4xlarge");
    masterDetails.setDeviceInfo(ApiUtils.getDummyDeviceInfo(1, 50));

    Map<ServerType, PerProcessDetails> perProcess = new HashMap<>();
    perProcess.put(ServerType.MASTER, masterDetails);

    UserIntentOverrides overrides = new UserIntentOverrides();
    overrides.setPerProcess(perProcess);
    userIntent.setUserIntentOverrides(overrides);

    ClusterNodeSpec clusterNodeSpec =
        UserIntentMapper.INSTANCE.userIntentToClusterNodeSpec(userIntent);

    ClusterPerProcessNodeSpec masterNodeSpec = clusterNodeSpec.getMaster();
    assertNotNull(masterNodeSpec);
    assertEquals("m5.2xlarge", masterNodeSpec.getInstanceType());
    assertEquals(Integer.valueOf(75), masterNodeSpec.getStorageSpec().getVolumeSize());
  }

  @Test
  public void testEditOmittingDedicatedNodesPreservesExisting() {
    UserIntent userIntent = new UserIntent();
    userIntent.dedicatedNodes = true;

    ClusterEditSpec editSpec = new ClusterEditSpec().uuid(UUID.randomUUID()).numNodes(5);
    UserIntentMapper.INSTANCE.toV1UserIntentFromClusterEditSpec(editSpec, userIntent);

    assertTrue(userIntent.dedicatedNodes);
    assertEquals(5, userIntent.numNodes);
  }

  @Test
  public void testClusterLevelDedicatedNodesPreferredOverNodeSpec() {
    ClusterSpec clusterSpec = new ClusterSpec();
    clusterSpec.setDedicatedNodes(true);
    clusterSpec.setNodeSpec(new ClusterNodeSpec().dedicatedNodes(false));

    UserIntent userIntent = UserIntentMapper.INSTANCE.toV1UserIntent(clusterSpec);

    assertTrue(userIntent.dedicatedNodes);
  }

  @Test
  public void testDeprecatedNodeSpecDedicatedNodesStillHonored() {
    ClusterSpec clusterSpec = new ClusterSpec();
    clusterSpec.setNodeSpec(new ClusterNodeSpec().dedicatedNodes(true));

    UserIntent userIntent = UserIntentMapper.INSTANCE.toV1UserIntent(clusterSpec);

    assertTrue(userIntent.dedicatedNodes);
  }

  @Test
  public void testCreateOmittingDedicatedNodesDefaultsFalse() {
    ClusterSpec clusterSpec = new ClusterSpec();
    UserIntent userIntent = UserIntentMapper.INSTANCE.toV1UserIntent(clusterSpec);

    assertFalse(userIntent.dedicatedNodes);
  }

  @Test
  public void testGflagsOnlyResizeDoesNotRequireNodeSpec() {
    UserIntent userIntent = new UserIntent();
    UUID providerUUID = UUID.randomUUID();
    TestUtils.getProviderInitializerForTests(userIntent, providerUUID).setInstanceType("c5.xlarge");

    Map<String, String> masterFlags = Map.of("master-flag", "some");
    Map<String, String> tserverFlags = Map.of("tserver-flag", "other");
    UniverseResizeNodesCluster resizeCluster = new UniverseResizeNodesCluster();
    resizeCluster.setUuid(UUID.randomUUID());
    resizeCluster.gflags(new ClusterGFlags().tserver(tserverFlags).master(masterFlags));
    // gflags-only: no node_spec / provider_nodes_specs

    UserIntent mapped =
        UserIntentMapper.INSTANCE.toV1UserIntentFromUniverseResizeNodesCluster(
            resizeCluster, userIntent);

    assertEquals("c5.xlarge", mapped.getBaseInstanceType(providerUUID));
    assertEquals(
        tserverFlags, mapped.specificGFlags.getPerProcessFlags().value.get(ServerType.TSERVER));
    assertEquals(
        masterFlags, mapped.specificGFlags.getPerProcessFlags().value.get(ServerType.MASTER));
  }

  @Test
  public void testNoGflagsResizeDoesNotEraseFlags() {
    SpecificGFlags specificGFlags =
        SpecificGFlags.construct(Map.of("tserver-flag", "foo"), Map.of("master-flag", "bar"));
    UserIntent userIntent = new UserIntent();
    userIntent.instanceType = "c5.xlarge";
    userIntent.specificGFlags = specificGFlags;

    UniverseResizeNodesCluster resizeCluster = new UniverseResizeNodesCluster();
    resizeCluster.setUuid(UUID.randomUUID());
    resizeCluster.setNodeSpec(new ClusterResizeNodeSpec().instanceType("c6.large"));
    UserIntent mapped =
        UserIntentMapper.INSTANCE.toV1UserIntentFromUniverseResizeNodesCluster(
            resizeCluster, userIntent);

    assertEquals("c6.large", mapped.instanceType);
    assertEquals(specificGFlags, mapped.specificGFlags);
  }

  @Test
  public void testEditAzNodeSpecReplacesExistingAzOverrides() {
    UUID az1 = UUID.randomUUID();
    UUID az2 = UUID.randomUUID();
    UserIntent userIntent = userIntentWithAzOverrides(az1, "c5.2xlarge", az2, "c5.4xlarge");

    AvailabilityZoneNodeSpec az1Spec = new AvailabilityZoneNodeSpec();
    az1Spec.setInstanceType("c6.2xlarge");
    ClusterNodeSpec nodeSpec =
        new ClusterNodeSpec()
            .instanceType("c5.xlarge")
            .azNodeSpec(Collections.singletonMap(az1.toString(), az1Spec));

    UserIntentMapper.INSTANCE.toV1UserIntentFromClusterEditSpec(
        new ClusterEditSpec().uuid(UUID.randomUUID()).nodeSpec(nodeSpec), userIntent);

    Map<UUID, AZOverrides> result = userIntent.getUserIntentOverrides().getAzOverrides();
    // Az2 is removed and Az1
    assertEquals(1, result.size());
    assertTrue(result.containsKey(az1));
    assertFalse(result.containsKey(az2));
    assertEquals("c6.2xlarge", result.get(az1).getInstanceType());
  }

  @Test
  public void testEditRemoveAzNodeSpec() {
    UUID az1 = UUID.randomUUID();
    UUID az2 = UUID.randomUUID();
    UserIntent userIntent = userIntentWithAzOverrides(az1, "c5.2xlarge", az2, "c5.4xlarge");

    ClusterNodeSpec nodeSpec =
        new ClusterNodeSpec().instanceType("c5.xlarge").azNodeSpec(Collections.emptyMap());

    UserIntentMapper.INSTANCE.toV1UserIntentFromClusterEditSpec(
        new ClusterEditSpec().uuid(UUID.randomUUID()).nodeSpec(nodeSpec), userIntent);

    Map<UUID, AZOverrides> result = userIntent.getUserIntentOverrides().getAzOverrides();
    assertNotNull(result);
    assertTrue(result.isEmpty());
  }

  @Test
  public void testEditOmittingAzNodeSpecPreservesExistingAzOverrides() {
    UUID az1 = UUID.randomUUID();
    UUID az2 = UUID.randomUUID();
    UserIntent userIntent = userIntentWithAzOverrides(az1, "c5.2xlarge", az2, "c5.4xlarge");

    // node_spec present but az_node_spec omitted - AZ overrides remain the same.
    ClusterNodeSpec nodeSpec = new ClusterNodeSpec().instanceType("c5.large");

    UserIntentMapper.INSTANCE.toV1UserIntentFromClusterEditSpec(
        new ClusterEditSpec().uuid(UUID.randomUUID()).nodeSpec(nodeSpec), userIntent);

    Map<UUID, AZOverrides> result = userIntent.getUserIntentOverrides().getAzOverrides();
    assertEquals(2, result.size());
    assertEquals("c5.2xlarge", result.get(az1).getInstanceType());
    assertEquals("c5.4xlarge", result.get(az2).getInstanceType());
    assertEquals("c5.large", userIntent.instanceType);
  }

  @Test
  public void testEditOmittingK8sResourceSpecsClearsThemForReinitialization() {
    UserIntent userIntent = new UserIntent();
    userIntent.instanceType = "large";
    userIntent.masterK8SNodeResourceSpec = resourceSpec(2.5, 8.0);
    userIntent.tserverK8SNodeResourceSpec = resourceSpec(8.0, 32.0);

    ClusterNodeSpec nodeSpec = new ClusterNodeSpec().instanceType("xlarge");
    UserIntentMapper.INSTANCE.toV1UserIntentFromClusterEditSpec(
        new ClusterEditSpec().uuid(UUID.randomUUID()).nodeSpec(nodeSpec), userIntent);

    // Left null so the handler derives them from the new instance type; keeping the old ones
    // would hide the instance type change from EditKubernetesUniverse.
    assertEquals("xlarge", userIntent.instanceType);
    assertNull(userIntent.masterK8SNodeResourceSpec);
    assertNull(userIntent.tserverK8SNodeResourceSpec);
  }

  @Test
  public void testEditK8sResourceSpecsReplaceExisting() {
    UserIntent userIntent = new UserIntent();
    userIntent.masterK8SNodeResourceSpec = resourceSpec(2.5, 8.0);
    userIntent.tserverK8SNodeResourceSpec = resourceSpec(8.0, 32.0);

    K8SNodeResourceSpec master = new K8SNodeResourceSpec().cpuCoreCount(3.0).memoryGib(6.0);
    K8SNodeResourceSpec tserver = new K8SNodeResourceSpec().cpuCoreCount(1.0).memoryGib(2.0);
    ClusterNodeSpec nodeSpec =
        new ClusterNodeSpec()
            .instanceType("xlarge")
            .k8sMasterResourceSpec(master)
            .k8sTserverResourceSpec(tserver);
    UserIntentMapper.INSTANCE.toV1UserIntentFromClusterEditSpec(
        new ClusterEditSpec().uuid(UUID.randomUUID()).nodeSpec(nodeSpec), userIntent);

    assertEquals(3.0, userIntent.masterK8SNodeResourceSpec.cpuCoreCount, 0.001);
    assertEquals(6.0, userIntent.masterK8SNodeResourceSpec.memoryGib, 0.001);
    assertEquals(1.0, userIntent.tserverK8SNodeResourceSpec.cpuCoreCount, 0.001);
    assertEquals(2.0, userIntent.tserverK8SNodeResourceSpec.memoryGib, 0.001);
  }

  @Test
  public void testAddReadReplicaOmittingK8sResourceSpecsClearsPrimaryOnes() {
    UserIntent userIntent = new UserIntent();
    userIntent.instanceType = "large";
    userIntent.masterK8SNodeResourceSpec = resourceSpec(2.5, 8.0);
    userIntent.tserverK8SNodeResourceSpec = resourceSpec(8.0, 32.0);

    ClusterAddSpec addSpec =
        new ClusterAddSpec().nodeSpec(new ClusterNodeSpec().instanceType("xlarge"));
    UserIntentMapper.INSTANCE.overwriteUserIntentFromClusterAddSpec(addSpec, userIntent);

    // The read replica must not inherit the primary's specs; the handler derives the tserver one
    // from the replica's instance type.
    assertEquals("xlarge", userIntent.instanceType);
    assertNull(userIntent.masterK8SNodeResourceSpec);
    assertNull(userIntent.tserverK8SNodeResourceSpec);
  }

  @Test
  public void testAddReadReplicaK8sResourceSpecsReplacePrimary() {
    UserIntent userIntent = new UserIntent();
    userIntent.masterK8SNodeResourceSpec = resourceSpec(2.5, 8.0);
    userIntent.tserverK8SNodeResourceSpec = resourceSpec(8.0, 32.0);

    K8SNodeResourceSpec tserver = new K8SNodeResourceSpec().cpuCoreCount(1.0).memoryGib(2.0);
    ClusterAddSpec addSpec =
        new ClusterAddSpec()
            .nodeSpec(new ClusterNodeSpec().instanceType("xlarge").k8sTserverResourceSpec(tserver));
    UserIntentMapper.INSTANCE.overwriteUserIntentFromClusterAddSpec(addSpec, userIntent);

    assertNull(userIntent.masterK8SNodeResourceSpec);
    assertEquals(1.0, userIntent.tserverK8SNodeResourceSpec.cpuCoreCount, 0.001);
    assertEquals(2.0, userIntent.tserverK8SNodeResourceSpec.memoryGib, 0.001);
  }

  private static UserIntent.K8SNodeResourceSpec resourceSpec(double cpu, double memoryGib) {
    UserIntent.K8SNodeResourceSpec spec = new UserIntent.K8SNodeResourceSpec();
    spec.cpuCoreCount = cpu;
    spec.memoryGib = memoryGib;
    return spec;
  }

  private static UserIntent userIntentWithAzOverrides(
      UUID az1, String az1InstanceType, UUID az2, String az2InstanceType) {
    UserIntent userIntent = new UserIntent();
    userIntent.instanceType = "c5.xlarge";
    AZOverrides az1Override = new AZOverrides();
    az1Override.setInstanceType(az1InstanceType);
    AZOverrides az2Override = new AZOverrides();
    az2Override.setInstanceType(az2InstanceType);
    Map<UUID, AZOverrides> existing = new HashMap<>();
    existing.put(az1, az1Override);
    existing.put(az2, az2Override);
    UserIntentOverrides overrides = new UserIntentOverrides();
    overrides.setAzOverrides(existing);
    userIntent.setUserIntentOverrides(overrides);
    return userIntent;
  }
}
