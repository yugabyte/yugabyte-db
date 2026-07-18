package com.yugabyte.yw.api.v2.mappers;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;

import api.v2.mappers.UniverseResizeNodeParamsMapper;
import api.v2.models.AvailabilityZoneGFlags;
import api.v2.models.AvailabilityZoneResizeNodeSpec;
import api.v2.models.ClusterGFlags;
import api.v2.models.ClusterResizeNodeSpec;
import api.v2.models.ClusterResizeStorageSpec;
import api.v2.models.K8SNodeResourceSpec;
import api.v2.models.PerProcessResizeNodeSpec;
import api.v2.models.UniverseResizeNodes;
import api.v2.models.UniverseResizeNodesCluster;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.forms.ResizeNodeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.PerProcessDetails;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class UniverseResizeNodesMapperTest {

  private UniverseResizeNodesCluster createResizeCluster(UUID clusterUUID) {
    UniverseResizeNodesCluster cluster = new UniverseResizeNodesCluster();
    cluster.setUuid(clusterUUID);
    return cluster;
  }

  private Cluster createV1Cluster(UUID clusterUUID, ClusterType type) {
    Cluster c = new Cluster(type, new UserIntent());
    c.setUuid(clusterUUID);
    return c;
  }

  @Test
  public void testSleepTimingsAndInstanceTypeWithStorageSpec() {
    UUID cUUID = UUID.randomUUID();
    UniverseResizeNodes req = new UniverseResizeNodes();
    req.setSleepAfterMasterRestartMillis(123);
    req.setSleepAfterTserverRestartMillis(456);

    ClusterResizeNodeSpec nodeSpec = new ClusterResizeNodeSpec();
    nodeSpec.setInstanceType("c5.2xlarge");
    nodeSpec.setStorageSpec(
        new ClusterResizeStorageSpec().volumeSize(500).diskIops(3000).throughput(125));

    UniverseResizeNodesCluster resizeCluster = createResizeCluster(cUUID);
    resizeCluster.setNodeSpec(nodeSpec);
    req.addClustersItem(resizeCluster);

    ResizeNodeParams v1Params = new ResizeNodeParams();
    v1Params.clusters.add(createV1Cluster(cUUID, ClusterType.PRIMARY));

    UniverseResizeNodeParamsMapper.INSTANCE.copyToV1ResizeNodeParams(req, v1Params);

    assertEquals((Object) 123, v1Params.sleepAfterMasterRestartMillis);
    assertEquals((Object) 456, v1Params.sleepAfterTServerRestartMillis);

    Cluster cluster = v1Params.getClusterByUuid(cUUID);
    assertEquals("c5.2xlarge", cluster.userIntent.instanceType);

    DeviceInfo device = cluster.userIntent.deviceInfo;
    assertNotNull(device);
    assertEquals((Object) 500, device.volumeSize);
    assertEquals((Object) 3000, device.diskIops);
    assertEquals((Object) 125, device.throughput);
  }

  @Test
  public void testPerProcessDedicatedNodesWithStorageSpecs() {
    UUID cUUID = UUID.randomUUID();
    UniverseResizeNodes req = new UniverseResizeNodes();

    ClusterResizeNodeSpec nodeSpec = new ClusterResizeNodeSpec();
    nodeSpec.setInstanceType("c5.xlarge");
    nodeSpec.setStorageSpec(new ClusterResizeStorageSpec().volumeSize(200));

    PerProcessResizeNodeSpec masterSpec = new PerProcessResizeNodeSpec();
    masterSpec.setInstanceType("c5.4xlarge");
    masterSpec.setStorageSpec(new ClusterResizeStorageSpec().volumeSize(100).diskIops(5000));
    nodeSpec.setMaster(masterSpec);

    PerProcessResizeNodeSpec tserverSpec = new PerProcessResizeNodeSpec();
    tserverSpec.setInstanceType("c5.2xlarge");
    tserverSpec.setStorageSpec(
        new ClusterResizeStorageSpec().volumeSize(800).diskIops(6000).throughput(250));
    nodeSpec.setTserver(tserverSpec);

    UniverseResizeNodesCluster resizeCluster = createResizeCluster(cUUID);
    resizeCluster.setNodeSpec(nodeSpec);
    req.addClustersItem(resizeCluster);

    ResizeNodeParams v1Params = new ResizeNodeParams();
    Cluster v1c = createV1Cluster(cUUID, ClusterType.PRIMARY);
    v1c.userIntent.dedicatedNodes = true;
    v1Params.clusters.add(v1c);

    UniverseResizeNodeParamsMapper.INSTANCE.copyToV1ResizeNodeParams(req, v1Params);

    Cluster cluster = v1Params.getClusterByUuid(cUUID);
    assertEquals("c5.xlarge", cluster.userIntent.instanceType);
    assertEquals((Object) 200, cluster.userIntent.deviceInfo.volumeSize);

    // Instance type resolution works correctly via UserIntentOverrides
    NodeDetails masterNode = new NodeDetails();
    masterNode.dedicatedTo = ServerType.MASTER;
    assertEquals("c5.4xlarge", cluster.userIntent.getInstanceTypeForNode(masterNode));

    NodeDetails tserverNode = new NodeDetails();
    tserverNode.dedicatedTo = ServerType.TSERVER;
    assertEquals("c5.2xlarge", cluster.userIntent.getInstanceTypeForNode(tserverNode));

    // Verify per-process overrides are stored correctly in UserIntentOverrides
    assertNotNull(cluster.userIntent.getUserIntentOverrides());
    assertNotNull(cluster.userIntent.getUserIntentOverrides().getPerProcess());

    PerProcessDetails masterOverrides =
        cluster.userIntent.getUserIntentOverrides().getPerProcess().get(ServerType.MASTER);
    assertNotNull(masterOverrides);
    assertEquals("c5.4xlarge", masterOverrides.getInstanceType());
    assertEquals((Object) 100, masterOverrides.getDeviceInfo().volumeSize);
    assertEquals((Object) 5000, masterOverrides.getDeviceInfo().diskIops);

    PerProcessDetails tserverOverrides =
        cluster.userIntent.getUserIntentOverrides().getPerProcess().get(ServerType.TSERVER);
    assertNotNull(tserverOverrides);
    assertEquals("c5.2xlarge", tserverOverrides.getInstanceType());
    assertEquals((Object) 800, tserverOverrides.getDeviceInfo().volumeSize);
    assertEquals((Object) 6000, tserverOverrides.getDeviceInfo().diskIops);
    assertEquals((Object) 250, tserverOverrides.getDeviceInfo().throughput);

    // tserver path via getDeviceInfoForNode works for non-dedicated nodes
    DeviceInfo tserverDevice = cluster.userIntent.getDeviceInfoForNode(tserverNode);
    assertEquals((Object) 800, tserverDevice.volumeSize);
    assertEquals((Object) 6000, tserverDevice.diskIops);
    assertEquals((Object) 250, tserverDevice.throughput);
  }

  @Test
  public void testAzOverridesWithInstanceTypeAndStorage() {
    UUID cUUID = UUID.randomUUID();
    UUID azUUID1 = UUID.randomUUID();
    UUID azUUID2 = UUID.randomUUID();

    UniverseResizeNodes req = new UniverseResizeNodes();

    ClusterResizeNodeSpec nodeSpec = new ClusterResizeNodeSpec();
    nodeSpec.setInstanceType("default-instance");
    nodeSpec.setStorageSpec(new ClusterResizeStorageSpec().volumeSize(100).diskIops(1000));
    nodeSpec.setCgroupSize(512);

    Map<String, AvailabilityZoneResizeNodeSpec> azMap = new HashMap<>();

    AvailabilityZoneResizeNodeSpec az1Spec = new AvailabilityZoneResizeNodeSpec();
    az1Spec.setInstanceType("az1-instance");
    az1Spec.setStorageSpec(new ClusterResizeStorageSpec().volumeSize(300).diskIops(4000));
    az1Spec.setCgroupSize(1024);
    azMap.put(azUUID1.toString(), az1Spec);

    AvailabilityZoneResizeNodeSpec az2Spec = new AvailabilityZoneResizeNodeSpec();
    az2Spec.setInstanceType("az2-instance");
    az2Spec.setStorageSpec(
        new ClusterResizeStorageSpec().volumeSize(600).diskIops(8000).throughput(500));
    azMap.put(azUUID2.toString(), az2Spec);

    nodeSpec.setAzNodeSpec(azMap);
    UniverseResizeNodesCluster resizeCluster = createResizeCluster(cUUID);
    resizeCluster.setNodeSpec(nodeSpec);
    req.addClustersItem(resizeCluster);

    ResizeNodeParams v1Params = new ResizeNodeParams();
    v1Params.clusters.add(createV1Cluster(cUUID, ClusterType.PRIMARY));

    UniverseResizeNodeParamsMapper.INSTANCE.copyToV1ResizeNodeParams(req, v1Params);

    Cluster cluster = v1Params.getClusterByUuid(cUUID);
    assertEquals("default-instance", cluster.userIntent.instanceType);
    assertEquals((Object) 100, cluster.userIntent.deviceInfo.volumeSize);
    assertEquals((Object) 1000, cluster.userIntent.deviceInfo.diskIops);

    // Unknown AZ falls back to cluster defaults
    NodeDetails defaultNode = new NodeDetails();
    defaultNode.azUuid = UUID.randomUUID();
    assertEquals("default-instance", cluster.userIntent.getInstanceTypeForNode(defaultNode));
    assertEquals((Object) 100, cluster.userIntent.getDeviceInfoForNode(defaultNode).volumeSize);

    // AZ1: instance type, storage overrides, and cgroup
    NodeDetails az1Node = new NodeDetails();
    az1Node.azUuid = azUUID1;
    assertEquals("az1-instance", cluster.userIntent.getInstanceTypeForNode(az1Node));
    DeviceInfo az1Device = cluster.userIntent.getDeviceInfoForNode(az1Node);
    assertEquals((Object) 300, az1Device.volumeSize);
    assertEquals((Object) 4000, az1Device.diskIops);

    // AZ2: instance type and full storage spec (volumeSize + diskIops + throughput)
    NodeDetails az2Node = new NodeDetails();
    az2Node.azUuid = azUUID2;
    assertEquals("az2-instance", cluster.userIntent.getInstanceTypeForNode(az2Node));
    DeviceInfo az2Device = cluster.userIntent.getDeviceInfoForNode(az2Node);
    assertEquals((Object) 600, az2Device.volumeSize);
    assertEquals((Object) 8000, az2Device.diskIops);
    assertEquals((Object) 500, az2Device.throughput);
  }

  @Test
  public void testGflagsWithAzOverrides() {
    UUID cUUID = UUID.randomUUID();
    UUID azUUID1 = UUID.randomUUID();
    UUID azUUID2 = UUID.randomUUID();

    UniverseResizeNodes req = new UniverseResizeNodes();
    UniverseResizeNodesCluster resizeCluster = createResizeCluster(cUUID);

    ClusterResizeNodeSpec nodeSpec = new ClusterResizeNodeSpec();
    nodeSpec.setInstanceType("c5.xlarge");
    resizeCluster.setNodeSpec(nodeSpec);

    ClusterGFlags gflagSpec = new ClusterGFlags();
    gflagSpec.putMasterItem("master_flag", "master_val");
    gflagSpec.putTserverItem("tserver_flag", "tserver_val");
    gflagSpec.putAzGflagsItem(
        azUUID1.toString(), new AvailabilityZoneGFlags().putMasterItem("az1_master", "az1_val"));
    gflagSpec.putAzGflagsItem(
        azUUID2.toString(), new AvailabilityZoneGFlags().putTserverItem("az2_tserver", "az2_val"));
    resizeCluster.setGflags(gflagSpec);
    req.addClustersItem(resizeCluster);

    ResizeNodeParams v1Params = new ResizeNodeParams();
    v1Params.clusters.add(createV1Cluster(cUUID, ClusterType.PRIMARY));

    UniverseResizeNodeParamsMapper.INSTANCE.copyToV1ResizeNodeParams(req, v1Params);

    Cluster cluster = v1Params.getClusterByUuid(cUUID);
    assertEquals("c5.xlarge", cluster.userIntent.instanceType);

    assertEquals(
        "master_val",
        cluster.userIntent.specificGFlags.getGFlags(null, ServerType.MASTER).get("master_flag"));
    assertEquals(
        "tserver_val",
        cluster.userIntent.specificGFlags.getGFlags(null, ServerType.TSERVER).get("tserver_flag"));
    assertEquals(
        "az1_val",
        cluster.userIntent.specificGFlags.getGFlags(azUUID1, ServerType.MASTER).get("az1_master"));
    assertEquals(
        "az2_val",
        cluster
            .userIntent
            .specificGFlags
            .getGFlags(azUUID2, ServerType.TSERVER)
            .get("az2_tserver"));
  }

  @Test
  public void testK8sResourceSpecs() {
    UUID cUUID = UUID.randomUUID();
    UniverseResizeNodes req = new UniverseResizeNodes();

    ClusterResizeNodeSpec nodeSpec = new ClusterResizeNodeSpec();

    K8SNodeResourceSpec masterK8sSpec = new K8SNodeResourceSpec();
    masterK8sSpec.setCpuCoreCount(4.0);
    masterK8sSpec.setMemoryGib(16.0);
    nodeSpec.setK8sMasterResourceSpec(masterK8sSpec);

    K8SNodeResourceSpec tserverK8sSpec = new K8SNodeResourceSpec();
    tserverK8sSpec.setCpuCoreCount(8.0);
    tserverK8sSpec.setMemoryGib(32.0);
    nodeSpec.setK8sTserverResourceSpec(tserverK8sSpec);

    nodeSpec.setStorageSpec(new ClusterResizeStorageSpec().volumeSize(250));

    UniverseResizeNodesCluster resizeCluster = createResizeCluster(cUUID);
    resizeCluster.setNodeSpec(nodeSpec);
    req.addClustersItem(resizeCluster);

    ResizeNodeParams v1Params = new ResizeNodeParams();
    v1Params.clusters.add(createV1Cluster(cUUID, ClusterType.PRIMARY));

    UniverseResizeNodeParamsMapper.INSTANCE.copyToV1ResizeNodeParams(req, v1Params);

    Cluster cluster = v1Params.getClusterByUuid(cUUID);

    assertNotNull(cluster.userIntent.masterK8SNodeResourceSpec);
    assertEquals(4.0, cluster.userIntent.masterK8SNodeResourceSpec.getCpuCoreCount(), 0.001);
    assertEquals(16.0, cluster.userIntent.masterK8SNodeResourceSpec.getMemoryGib(), 0.001);

    assertNotNull(cluster.userIntent.tserverK8SNodeResourceSpec);
    assertEquals(8.0, cluster.userIntent.tserverK8SNodeResourceSpec.getCpuCoreCount(), 0.001);
    assertEquals(32.0, cluster.userIntent.tserverK8SNodeResourceSpec.getMemoryGib(), 0.001);

    assertEquals((Object) 250, cluster.userIntent.deviceInfo.volumeSize);
  }

  @Test
  public void testK8sResourceSpecsWithInstanceTypeUnset() {
    UUID cUUID = UUID.randomUUID();
    UniverseResizeNodes req = new UniverseResizeNodes();

    ClusterResizeNodeSpec nodeSpec = new ClusterResizeNodeSpec();

    K8SNodeResourceSpec masterK8sSpec = new K8SNodeResourceSpec();
    masterK8sSpec.setCpuCoreCount(2.0);
    masterK8sSpec.setMemoryGib(8.0);
    nodeSpec.setK8sMasterResourceSpec(masterK8sSpec);

    K8SNodeResourceSpec tserverK8sSpec = new K8SNodeResourceSpec();
    tserverK8sSpec.setCpuCoreCount(4.0);
    tserverK8sSpec.setMemoryGib(16.0);
    nodeSpec.setK8sTserverResourceSpec(tserverK8sSpec);

    UniverseResizeNodesCluster resizeCluster = createResizeCluster(cUUID);
    resizeCluster.setNodeSpec(nodeSpec);
    req.addClustersItem(resizeCluster);

    ResizeNodeParams v1Params = new ResizeNodeParams();
    v1Params.clusters.add(createV1Cluster(cUUID, ClusterType.PRIMARY));

    UniverseResizeNodeParamsMapper.INSTANCE.copyToV1ResizeNodeParams(req, v1Params);

    Cluster cluster = v1Params.getClusterByUuid(cUUID);
    assertNull(cluster.userIntent.instanceType);

    assertNotNull(cluster.userIntent.masterK8SNodeResourceSpec);
    assertEquals(2.0, cluster.userIntent.masterK8SNodeResourceSpec.getCpuCoreCount(), 0.001);
    assertEquals(8.0, cluster.userIntent.masterK8SNodeResourceSpec.getMemoryGib(), 0.001);

    assertNotNull(cluster.userIntent.tserverK8SNodeResourceSpec);
    assertEquals(4.0, cluster.userIntent.tserverK8SNodeResourceSpec.getCpuCoreCount(), 0.001);
    assertEquals(16.0, cluster.userIntent.tserverK8SNodeResourceSpec.getMemoryGib(), 0.001);
  }

  @Test
  public void testMultiClusterPrimaryAndReadReplica() {
    UUID primaryUUID = UUID.randomUUID();
    UUID rrUUID = UUID.randomUUID();

    UniverseResizeNodes req = new UniverseResizeNodes();
    req.setSleepAfterTserverRestartMillis(300);

    ClusterResizeNodeSpec primaryNodeSpec = new ClusterResizeNodeSpec();
    primaryNodeSpec.setInstanceType("c5.4xlarge");
    primaryNodeSpec.setStorageSpec(
        new ClusterResizeStorageSpec().volumeSize(1000).diskIops(10000).throughput(500));
    UniverseResizeNodesCluster primaryCluster = createResizeCluster(primaryUUID);
    primaryCluster.setNodeSpec(primaryNodeSpec);
    req.addClustersItem(primaryCluster);

    ClusterResizeNodeSpec rrNodeSpec = new ClusterResizeNodeSpec();
    rrNodeSpec.setInstanceType("c5.xlarge");
    rrNodeSpec.setStorageSpec(new ClusterResizeStorageSpec().volumeSize(500).diskIops(3000));
    UniverseResizeNodesCluster rrCluster = createResizeCluster(rrUUID);
    rrCluster.setNodeSpec(rrNodeSpec);
    req.addClustersItem(rrCluster);

    ResizeNodeParams v1Params = new ResizeNodeParams();
    v1Params.clusters.add(createV1Cluster(primaryUUID, ClusterType.PRIMARY));
    v1Params.clusters.add(createV1Cluster(rrUUID, ClusterType.ASYNC));

    UniverseResizeNodeParamsMapper.INSTANCE.copyToV1ResizeNodeParams(req, v1Params);

    assertEquals((Object) 300, v1Params.sleepAfterTServerRestartMillis);

    Cluster primary = v1Params.getClusterByUuid(primaryUUID);
    assertEquals("c5.4xlarge", primary.userIntent.instanceType);
    assertEquals((Object) 1000, primary.userIntent.deviceInfo.volumeSize);
    assertEquals((Object) 10000, primary.userIntent.deviceInfo.diskIops);
    assertEquals((Object) 500, primary.userIntent.deviceInfo.throughput);

    Cluster rr = v1Params.getClusterByUuid(rrUUID);
    assertEquals("c5.xlarge", rr.userIntent.instanceType);
    assertEquals((Object) 500, rr.userIntent.deviceInfo.volumeSize);
    assertEquals((Object) 3000, rr.userIntent.deviceInfo.diskIops);
    assertNull(rr.userIntent.deviceInfo.throughput);
  }

  @Test
  public void testStorageSpecPartialFields() {
    UUID cUUID = UUID.randomUUID();
    UniverseResizeNodes req = new UniverseResizeNodes();

    ClusterResizeNodeSpec nodeSpec = new ClusterResizeNodeSpec();
    nodeSpec.setStorageSpec(new ClusterResizeStorageSpec().volumeSize(750));

    UniverseResizeNodesCluster resizeCluster = createResizeCluster(cUUID);
    resizeCluster.setNodeSpec(nodeSpec);
    req.addClustersItem(resizeCluster);

    ResizeNodeParams v1Params = new ResizeNodeParams();
    v1Params.clusters.add(createV1Cluster(cUUID, ClusterType.PRIMARY));

    UniverseResizeNodeParamsMapper.INSTANCE.copyToV1ResizeNodeParams(req, v1Params);

    Cluster cluster = v1Params.getClusterByUuid(cUUID);
    DeviceInfo device = cluster.userIntent.deviceInfo;
    assertNotNull(device);
    assertEquals((Object) 750, device.volumeSize);
    assertNull(device.diskIops);
    assertNull(device.throughput);
  }

  @Test
  public void testAzStorageSpecOverridesClusterDefault() {
    UUID cUUID = UUID.randomUUID();
    UUID azUUID1 = UUID.randomUUID();
    UUID azUUID2 = UUID.randomUUID();

    UniverseResizeNodes req = new UniverseResizeNodes();
    ClusterResizeNodeSpec nodeSpec = new ClusterResizeNodeSpec();
    nodeSpec.setInstanceType("default-type");
    nodeSpec.setStorageSpec(
        new ClusterResizeStorageSpec().volumeSize(200).diskIops(1000).throughput(100));

    Map<String, AvailabilityZoneResizeNodeSpec> azMap = new HashMap<>();

    AvailabilityZoneResizeNodeSpec az1Spec = new AvailabilityZoneResizeNodeSpec();
    az1Spec.setStorageSpec(new ClusterResizeStorageSpec().volumeSize(400).diskIops(2000));
    azMap.put(azUUID1.toString(), az1Spec);

    AvailabilityZoneResizeNodeSpec az2Spec = new AvailabilityZoneResizeNodeSpec();
    az2Spec.setStorageSpec(
        new ClusterResizeStorageSpec().volumeSize(600).diskIops(5000).throughput(300));
    azMap.put(azUUID2.toString(), az2Spec);

    nodeSpec.setAzNodeSpec(azMap);
    UniverseResizeNodesCluster resizeCluster = createResizeCluster(cUUID);
    resizeCluster.setNodeSpec(nodeSpec);
    req.addClustersItem(resizeCluster);

    ResizeNodeParams v1Params = new ResizeNodeParams();
    v1Params.clusters.add(createV1Cluster(cUUID, ClusterType.PRIMARY));

    UniverseResizeNodeParamsMapper.INSTANCE.copyToV1ResizeNodeParams(req, v1Params);

    Cluster cluster = v1Params.getClusterByUuid(cUUID);

    // Unrecognized AZ falls back to cluster default
    NodeDetails defaultNode = new NodeDetails();
    defaultNode.azUuid = UUID.randomUUID();
    assertEquals("default-type", cluster.userIntent.getInstanceTypeForNode(defaultNode));
    assertEquals((Object) 200, cluster.userIntent.getDeviceInfoForNode(defaultNode).volumeSize);
    assertEquals((Object) 1000, cluster.userIntent.getDeviceInfoForNode(defaultNode).diskIops);
    assertEquals((Object) 100, cluster.userIntent.getDeviceInfoForNode(defaultNode).throughput);

    // AZ1 overrides
    NodeDetails az1Node = new NodeDetails();
    az1Node.azUuid = azUUID1;
    assertEquals((Object) 400, cluster.userIntent.getDeviceInfoForNode(az1Node).volumeSize);
    assertEquals((Object) 2000, cluster.userIntent.getDeviceInfoForNode(az1Node).diskIops);

    // AZ2 overrides with all storage fields
    NodeDetails az2Node = new NodeDetails();
    az2Node.azUuid = azUUID2;
    assertEquals((Object) 600, cluster.userIntent.getDeviceInfoForNode(az2Node).volumeSize);
    assertEquals((Object) 5000, cluster.userIntent.getDeviceInfoForNode(az2Node).diskIops);
    assertEquals((Object) 300, cluster.userIntent.getDeviceInfoForNode(az2Node).throughput);
  }
}
