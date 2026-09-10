// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.api.v2.mappers;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import api.v2.mappers.UserIntentMapper;
import api.v2.models.AvailabilityZoneNodeSpec;
import api.v2.models.ClusterEditSpec;
import api.v2.models.ClusterNodeSpec;
import api.v2.models.ClusterPerProcessNodeSpec;
import api.v2.models.ClusterSpec;
import api.v2.models.UniverseResizeNodesCluster;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.AZOverrides;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.PerProcessDetails;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntentOverrides;
import com.yugabyte.yw.models.helpers.DeviceInfo;
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
    userIntent.instanceType = "c5.xlarge";
    userIntent.deviceInfo = ApiUtils.getDummyDeviceInfo(1, 100);

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
    userIntent.instanceType = "c5.xlarge";
    userIntent.deviceInfo = ApiUtils.getDummyDeviceInfo(1, 100);
    userIntent.masterInstanceType = "m5.2xlarge";
    userIntent.masterDeviceInfo = ApiUtils.getDummyDeviceInfo(1, 75);

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
    userIntent.instanceType = "c5.xlarge";
    userIntent.deviceInfo = ApiUtils.getDummyDeviceInfo(1, 100);

    PerProcessDetails masterDetails = new PerProcessDetails();
    masterDetails.setInstanceType("c5.4xlarge");
    masterDetails.setDeviceInfo(ApiUtils.getDummyDeviceInfo(1, 50));

    Map<ServerType, PerProcessDetails> perProcess = new HashMap<>();
    perProcess.put(ServerType.MASTER, masterDetails);

    UserIntentOverrides overrides = new UserIntentOverrides();
    overrides.setPerProcess(perProcess);
    userIntent.setUserIntentOverrides(overrides);

    userIntent.masterInstanceType = "m5.2xlarge";
    DeviceInfo masterDeviceInfo = ApiUtils.getDummyDeviceInfo(1, 75);
    userIntent.masterDeviceInfo = masterDeviceInfo;

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
    userIntent.instanceType = "c5.xlarge";

    UniverseResizeNodesCluster resizeCluster = new UniverseResizeNodesCluster();
    resizeCluster.setUuid(UUID.randomUUID());
    // gflags-only: no node_spec / provider_nodes_specs

    UserIntent mapped =
        UserIntentMapper.INSTANCE.toV1UserIntentFromUniverseResizeNodesCluster(
            resizeCluster, userIntent);

    assertEquals("c5.xlarge", mapped.instanceType);
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
