// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common;

import static org.junit.Assert.assertEquals;

import com.yugabyte.yw.cloud.PublicCloudConstants;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.ProxyConfig;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.function.Consumer;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.junit.runners.BlockJUnit4ClassRunner;

@RunWith(BlockJUnit4ClassRunner.class)
public class ProviderSpecificationTest {

  private UUID providerUUID = UUID.randomUUID();
  private Common.CloudType providerType = Common.CloudType.gcp;
  private UUID imageBundleUUID = UUID.randomUUID();
  private Map<String, String> tags = Map.of("a", "b");
  private DeviceInfo deviceInfo;
  private ProxyConfig proxyConfig;

  @Before
  public void init() {
    deviceInfo = new DeviceInfo();
    deviceInfo.storageType = PublicCloudConstants.StorageType.GP2;
    deviceInfo.diskIops = 1000;
    deviceInfo.numVolumes = 3;
    deviceInfo.throughput = 5;
    deviceInfo.volumeSize = 1500;

    proxyConfig = new ProxyConfig();
    proxyConfig.setHttpsProxy("https proxy");
  }

  @Test
  public void testSingleProviderSpec() {
    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    UniverseDefinitionTaskParams.ProviderSpecification providerSpecification =
        new UniverseDefinitionTaskParams.ProviderSpecification();
    providerSpecification.setProviderUUID(providerUUID);
    providerSpecification.setProviderType(providerType);
    providerSpecification.setInstanceTags(tags);
    providerSpecification.setImageBundleUUID(imageBundleUUID);
    providerSpecification.setEnableLoadBalancer(true);
    providerSpecification.setHelmOverrides("Helm overrides");
    providerSpecification.setAccessKeyCode("accessKey");
    providerSpecification.setEnableLoadBalancer(true);
    providerSpecification.setExposingServiceState(
        UniverseDefinitionTaskParams.ExposingServiceState.UNEXPOSED);
    providerSpecification.setAwsInstanceProfile("aws profile");
    UniverseDefinitionTaskParams.NodesSpecification nodesSpecification =
        new UniverseDefinitionTaskParams.NodesSpecification();
    UniverseDefinitionTaskParams.NodeSpecification tserverSpec =
        new UniverseDefinitionTaskParams.NodeSpecification();
    tserverSpec.setInstanceType("tserverType");
    tserverSpec.setCgroupSize(10);
    tserverSpec.setDeviceInfo(deviceInfo);
    tserverSpec.setProxyConfig(proxyConfig);
    nodesSpecification.setTserverSpecification(tserverSpec);
    providerSpecification.setBaseNodesSpecification(nodesSpecification);
    userIntent.providerSpecifications = Collections.singletonList(providerSpecification);

    NodeDetails nodeDetails = new NodeDetails();
    nodeDetails.azUuid = UUID.randomUUID();

    assertEquals(Set.of(providerUUID), userIntent.getAllProviderUUIDs());
    assertEquals(Arrays.asList(Common.CloudType.gcp), userIntent.getAllCloudTypes());
    assertEquals(providerSpecification, userIntent.getProviderSpecification(providerUUID));
    assertEquals(List.of(imageBundleUUID), userIntent.getAllImageBundles());
    assertEquals(imageBundleUUID, userIntent.getImageBundleUUIDForProvider(providerUUID));
    assertEquals("accessKey", userIntent.getAccessKeyCodeForProvider(providerUUID));
    assertEquals(10, userIntent.getCGroupSize(nodeDetails).intValue());
    assertEquals("tserverType", userIntent.getInstanceTypeForNode(nodeDetails));
    assertEquals(deviceInfo, userIntent.getDeviceInfoForNode(nodeDetails));
    assertEquals(proxyConfig, userIntent.getProxyConfig(nodeDetails.azUuid));
  }

  @Test
  public void testNodeSpecOverrides() {
    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.dedicatedNodes = true;
    UniverseDefinitionTaskParams.ProviderSpecification providerSpecification =
        new UniverseDefinitionTaskParams.ProviderSpecification();
    providerSpecification.setProviderUUID(providerUUID);
    UniverseDefinitionTaskParams.NodeSpecification tserverSpec =
        new UniverseDefinitionTaskParams.NodeSpecification();
    tserverSpec.setInstanceType("tserverType");
    tserverSpec.setCgroupSize(10);
    tserverSpec.setDeviceInfo(deviceInfo);
    tserverSpec.setProxyConfig(proxyConfig);

    DeviceInfo masterDeviceInfo = new DeviceInfo();
    masterDeviceInfo.storageType = PublicCloudConstants.StorageType.GP3;
    masterDeviceInfo.diskIops = 2000;
    masterDeviceInfo.numVolumes = 2;
    masterDeviceInfo.throughput = 100;
    masterDeviceInfo.volumeSize = 500;

    UniverseDefinitionTaskParams.NodeSpecification masterSpec =
        new UniverseDefinitionTaskParams.NodeSpecification();
    masterSpec.setInstanceType("masterType");
    masterSpec.setCgroupSize(20);
    masterSpec.setDeviceInfo(masterDeviceInfo);

    providerSpecification.setBaseNodesSpecification(
        UniverseDefinitionTaskParams.NodesSpecification.of(tserverSpec, masterSpec));

    userIntent.providerSpecifications = Collections.singletonList(providerSpecification);

    NodeDetails nodeDetails = new NodeDetails();
    nodeDetails.azUuid = UUID.randomUUID();

    NodeDetails masterNodeDetails = new NodeDetails();
    nodeDetails.azUuid = UUID.randomUUID();
    masterNodeDetails.dedicatedTo = UniverseTaskBase.ServerType.MASTER;

    UUID tserverOverridenAZ = UUID.randomUUID();
    UUID masterOverridenAZ = UUID.randomUUID();
    UUID bothOverridenAZ = UUID.randomUUID();

    UniverseDefinitionTaskParams.NodeSpecification overridenTserver =
        new UniverseDefinitionTaskParams.NodeSpecification();
    overridenTserver.setInstanceType("overridenTserver");
    overridenTserver.setCgroupSize(100);
    DeviceInfo overridenTserverDevice = new DeviceInfo();
    overridenTserverDevice.volumeSize = deviceInfo.volumeSize + 1;
    overridenTserver.setDeviceInfo(overridenTserverDevice);

    UniverseDefinitionTaskParams.NodeSpecification overridenMaster =
        new UniverseDefinitionTaskParams.NodeSpecification();
    overridenMaster.setInstanceType("overridenMaster");
    overridenMaster.setCgroupSize(200);
    DeviceInfo overridenMasterDevice = new DeviceInfo();
    overridenMasterDevice.numVolumes = 10;
    overridenMaster.setDeviceInfo(overridenMasterDevice);

    providerSpecification.setPerAZOverrides(
        Map.of(
            tserverOverridenAZ,
                UniverseDefinitionTaskParams.NodesSpecification.of(overridenTserver, null),
            masterOverridenAZ,
                UniverseDefinitionTaskParams.NodesSpecification.of(null, overridenMaster),
            bothOverridenAZ,
                UniverseDefinitionTaskParams.NodesSpecification.of(
                    overridenTserver, overridenMaster)));

    assertEquals("tserverType", userIntent.getInstanceTypeForNode(nodeDetails));
    assertEquals(deviceInfo, userIntent.getDeviceInfoForNode(nodeDetails));
    assertEquals(proxyConfig, userIntent.getProxyConfig(nodeDetails.azUuid));

    assertEquals("masterType", userIntent.getInstanceTypeForNode(masterNodeDetails));
    assertEquals(masterDeviceInfo, userIntent.getDeviceInfoForNode(masterNodeDetails));
    assertEquals(proxyConfig, userIntent.getProxyConfig(masterNodeDetails.azUuid));

    NodeDetails overridenTsNode = new NodeDetails();
    overridenTsNode.azUuid = tserverOverridenAZ;

    NodeDetails overridenMsNode = new NodeDetails();
    overridenMsNode.azUuid = masterOverridenAZ;
    overridenMsNode.dedicatedTo = UniverseTaskBase.ServerType.MASTER;

    assertEquals("overridenTserver", userIntent.getInstanceTypeForNode(overridenTsNode));
    assertEquals(
        modify(deviceInfo, d -> d.volumeSize = overridenTserverDevice.volumeSize),
        userIntent.getDeviceInfoForNode(overridenTsNode));
    assertEquals(100, userIntent.getCGroupSize(overridenTsNode).intValue());

    assertEquals("overridenMaster", userIntent.getInstanceTypeForNode(overridenMsNode));
    assertEquals(
        modify(masterDeviceInfo, d -> d.numVolumes = 10),
        userIntent.getDeviceInfoForNode(overridenMsNode));
    assertEquals(200, userIntent.getCGroupSize(overridenMsNode).intValue());

    nodeDetails.azUuid = bothOverridenAZ;
    assertEquals("overridenTserver", userIntent.getInstanceTypeForNode(nodeDetails));
    assertEquals(
        modify(deviceInfo, d -> d.volumeSize = overridenTserverDevice.volumeSize),
        userIntent.getDeviceInfoForNode(nodeDetails));
    assertEquals(100, userIntent.getCGroupSize(nodeDetails).intValue());
    nodeDetails.dedicatedTo = UniverseTaskBase.ServerType.MASTER;
    assertEquals("overridenMaster", userIntent.getInstanceTypeForNode(nodeDetails));
    assertEquals(
        modify(masterDeviceInfo, d -> d.numVolumes = 10),
        userIntent.getDeviceInfoForNode(nodeDetails));
    assertEquals(200, userIntent.getCGroupSize(nodeDetails).intValue());
  }

  @Test
  public void testNodeSpecOverridesUpdate() {
    UniverseDefinitionTaskParams.ProviderSpecification providerSpecification =
        new UniverseDefinitionTaskParams.ProviderSpecification();
    providerSpecification.setProviderUUID(providerUUID);
    UniverseDefinitionTaskParams.NodeSpecification tserverSpec =
        new UniverseDefinitionTaskParams.NodeSpecification();
    tserverSpec.setInstanceType("tserverType");
    tserverSpec.setCgroupSize(10);
    providerSpecification.setBaseNodesSpecification(
        UniverseDefinitionTaskParams.NodesSpecification.of(tserverSpec, null));

    UniverseDefinitionTaskParams.ProviderSpecification providerSpecification2 =
        new UniverseDefinitionTaskParams.ProviderSpecification();
    UniverseDefinitionTaskParams.NodeSpecification masterSpec =
        new UniverseDefinitionTaskParams.NodeSpecification();
    masterSpec.setInstanceType("masterType");
    masterSpec.setCgroupSize(20);

    providerSpecification2.setBaseNodesSpecification(
        UniverseDefinitionTaskParams.NodesSpecification.of(tserverSpec, masterSpec));
    UUID overridenAZ = UUID.randomUUID();
    UniverseDefinitionTaskParams.NodeSpecification overridenTSpec =
        new UniverseDefinitionTaskParams.NodeSpecification();
    overridenTSpec.setInstanceType("tserverType2");
    overridenTSpec.setCgroupSize(5);
    providerSpecification2.setPerAZOverrides(
        Map.of(
            overridenAZ, UniverseDefinitionTaskParams.NodesSpecification.of(overridenTSpec, null)));

    providerSpecification.mergeNodesSpecification(
        providerSpecification2,
        ctx -> {
          ctx.getCurrent().setInstanceType(ctx.getTarget().getInstanceType());
        });

    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.dedicatedNodes = true;
    userIntent.providerSpecifications = Collections.singletonList(providerSpecification);

    NodeDetails basicNode = new NodeDetails();
    basicNode.azUuid = UUID.randomUUID();
    assertEquals("tserverType", userIntent.getInstanceTypeForNode(basicNode));
    basicNode.dedicatedTo = UniverseTaskBase.ServerType.MASTER;
    assertEquals("masterType", userIntent.getInstanceTypeForNode(basicNode));

    NodeDetails overridenNode = new NodeDetails();
    overridenNode.azUuid = overridenAZ;
    assertEquals("tserverType2", userIntent.getInstanceTypeForNode(overridenNode));
  }

  private DeviceInfo modify(DeviceInfo original, Consumer<DeviceInfo> mutator) {
    DeviceInfo clone = original.clone();
    mutator.accept(clone);
    return clone;
  }
}
