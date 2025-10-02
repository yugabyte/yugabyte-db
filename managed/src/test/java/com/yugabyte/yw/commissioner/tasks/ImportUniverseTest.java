// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.models.TaskInfo.State.Failure;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.containsString;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.argThat;
import static org.mockito.ArgumentMatchers.contains;
import static org.mockito.Mockito.doAnswer;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableList;
import com.google.common.net.HostAndPort;
import com.google.protobuf.ByteString;
import com.yugabyte.yw.cloud.PublicCloudConstants.Architecture;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.forms.ImportUniverseTaskParams;
import com.yugabyte.yw.forms.ImportUniverseTaskParams.ServerConfig;
import com.yugabyte.yw.forms.NodeInstanceFormData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseMigrationConfig;
import com.yugabyte.yw.forms.UniverseMigrationConfig.ServerSpecificConfig;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.InstanceType.InstanceTypeDetails;
import com.yugabyte.yw.models.NodeAgent;
import com.yugabyte.yw.models.NodeAgent.ArchType;
import com.yugabyte.yw.models.NodeAgent.OSType;
import com.yugabyte.yw.models.NodeAgent.State;
import com.yugabyte.yw.models.NodeInstance;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Release;
import com.yugabyte.yw.models.ReleaseArtifact;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.function.BiConsumer;
import java.util.stream.Collectors;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.CommonNet.PlacementBlockPB;
import org.yb.CommonNet.PlacementInfoPB;
import org.yb.CommonNet.ReplicationInfoPB;
import org.yb.client.GetMasterClusterConfigResponse;
import org.yb.client.ListMasterRaftPeersResponse;
import org.yb.client.ListTabletServersResponse;
import org.yb.client.YBClient;
import org.yb.master.CatalogEntityInfo.SysClusterConfigEntryPB;
import org.yb.util.PeerInfo;
import org.yb.util.ServerInfo;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class ImportUniverseTest extends CommissionerBaseTest {

  private final List<String> masterIps = ImmutableList.of("10.10.10.1", "10.10.10.2", "10.10.10.3");
  private final List<String> tserverIps =
      ImmutableList.of("10.10.11.1", "10.10.11.2", "10.10.11.3");
  private final String dbVersion = "2024.2.2.0-b1";
  private final String masterService = "my-master.service";
  private final String tserverService = "my-tserver.service";
  private final String masterInstanceTypeCode = "master-instance1";
  private final String tserverInstanceTypeCode = "tserver-instance1";

  private final String masterServiceUnit =
      "# /etc/systemd/system/my-master.service\n"
          + "ExecStart=/master/bin/yb-master --flagfile /master/conf/master.conf";
  private final String tserverServiceUnit =
      "# /etc/systemd/system/my-tserver.service\n"
          + "ExecStart=/master/bin/yb-tserver --flagfile /tserver/conf/tserver.conf";

  private final int masterVolumeMountCount = 1;
  private final int tserverVolumeMountCount = 5;

  // IP to NodeInstance object.
  private final Map<String, NodeInstance> nodeInstances = new ConcurrentHashMap<>();

  private String masterMountPaths;
  private String tserverMountPaths;

  @Before
  public void setUp() {
    setupInfra();
    try {
      YBClient mockClient = mock(YBClient.class);
      when(mockYBClient.getClient(any(), any())).thenReturn(mockClient);

      GetMasterClusterConfigResponse mockClusterConfigResponse =
          mock(GetMasterClusterConfigResponse.class);

      SysClusterConfigEntryPB mockClusterConfigEntry = mock(SysClusterConfigEntryPB.class);
      ReplicationInfoPB mockReplicationInfo = mock(ReplicationInfoPB.class);
      PlacementInfoPB mockPlacementInfo = mock(PlacementInfoPB.class);
      when(mockClusterConfigResponse.getConfig()).thenReturn(mockClusterConfigEntry);
      when(mockClusterConfigResponse.getConfig()).thenReturn(mockClusterConfigEntry);
      when(mockClusterConfigEntry.getReplicationInfo()).thenReturn(mockReplicationInfo);
      when(mockReplicationInfo.getLiveReplicas()).thenReturn(mockPlacementInfo);
      when(mockPlacementInfo.getPlacementUuid()).thenReturn(ByteString.EMPTY);
      List<PlacementBlockPB> placementBlocks = new ArrayList<>();
      when(mockPlacementInfo.getPlacementBlocksList()).thenReturn(placementBlocks);
      when(mockClient.getMasterClusterConfig()).thenReturn(mockClusterConfigResponse);

      ListMasterRaftPeersResponse mockMasterRaftPeers = mock(ListMasterRaftPeersResponse.class);
      when(mockClient.listMasterRaftPeers()).thenReturn(mockMasterRaftPeers);
      List<PeerInfo> peerInfos = new ArrayList<>();
      masterIps.forEach(
          ip -> {
            PeerInfo mockPeerInfo = mock(PeerInfo.class);
            HostAndPort mockHostAndPort = mock(HostAndPort.class);
            when(mockHostAndPort.getHost()).thenReturn(ip);
            when(mockPeerInfo.getLastKnownPrivateIps()).thenReturn(List.of(mockHostAndPort));
            peerInfos.add(mockPeerInfo);
          });
      when(mockMasterRaftPeers.getPeersList()).thenReturn(peerInfos);

      ListTabletServersResponse mockTabletServersResponse = mock(ListTabletServersResponse.class);
      when(mockClient.listTabletServers()).thenReturn(mockTabletServersResponse);
      List<ServerInfo> serverInfos = new ArrayList<>();
      tserverIps.forEach(
          ip -> {
            ServerInfo mockServerInfo = mock(ServerInfo.class);
            when(mockServerInfo.getHost()).thenReturn(ip);
            serverInfos.add(mockServerInfo);
          });
      when(mockTabletServersResponse.getTabletServersList()).thenReturn(serverInfos);

      HostAndPort mockMasterLeaderHostAndPort = mock(HostAndPort.class);
      when(mockMasterLeaderHostAndPort.getHost()).thenReturn(masterIps.get(0));
      when(mockClient.getLeaderMasterHostAndPort()).thenReturn(mockMasterLeaderHostAndPort);

      String[] versionParts = dbVersion.split("-b");
      ObjectNode versionInfoNode = Json.mapper().createObjectNode();
      versionInfoNode.put("version_number", versionParts[0]);
      versionInfoNode.put("build_number", versionParts[1]);
      when(mockNodeUIApiHelper.getRequest(contains("v1/version"))).thenReturn(versionInfoNode);
      when(mockNodeAgentClient.executeCommand(any(), argThat(cmd -> cmd.contains("uname"))))
          .thenReturn(ShellResponse.create(0, "Linux x86_64"));
      doReturn(ShellResponse.create(0, masterServiceUnit))
          .when(mockNodeAgentClient)
          .executeCommand(
              any(), argThat(cmd -> cmd.containsAll(List.of("systemctl", "cat", masterService))));
      doReturn(ShellResponse.create(0, tserverServiceUnit))
          .when(mockNodeAgentClient)
          .executeCommand(
              any(), argThat(cmd -> cmd.containsAll(List.of("systemctl", "cat", tserverService))));

      masterMountPaths =
          InstanceType.get(onPremProvider.getUuid(), masterInstanceTypeCode)
              .getInstanceTypeDetails()
              .volumeDetailsList
              .stream()
              .map(v -> v.mountPath)
              .collect(Collectors.joining(","));
      doAnswer(
              i -> {
                NodeAgent nodeAgent = i.getArgument(0);
                NodeInstance nodeInstance = nodeInstances.get(nodeAgent.getIp());
                List<String> masterGflagsList = new ArrayList<>();
                masterGflagsList.add("--fs_data_dirs=" + masterMountPaths);
                masterGflagsList.add("--replication_factor=3");
                masterGflagsList.add("--placement_region=" + nodeInstance.getDetails().region);
                masterGflagsList.add("--placement_zone=" + nodeInstance.getDetails().zone);
                return ShellResponse.create(0, String.join("\n", masterGflagsList));
              })
          .when(mockNodeAgentClient)
          .executeCommand(any(), argThat(cmd -> cmd.contains("cat /master/conf/master.conf")));

      tserverMountPaths =
          InstanceType.get(onPremProvider.getUuid(), tserverInstanceTypeCode)
              .getInstanceTypeDetails()
              .volumeDetailsList
              .stream()
              .map(v -> v.mountPath)
              .collect(Collectors.joining(","));
      doAnswer(
              i -> {
                NodeAgent nodeAgent = i.getArgument(0);
                NodeInstance nodeInstance = nodeInstances.get(nodeAgent.getIp());
                List<String> tserverGflagsList = new ArrayList<>();
                tserverGflagsList.add("--fs_data_dirs=" + tserverMountPaths);
                tserverGflagsList.add("--placement_region=" + nodeInstance.getDetails().region);
                tserverGflagsList.add("--placement_zone=" + nodeInstance.getDetails().zone);
                return ShellResponse.create(0, String.join("\n", tserverGflagsList));
              })
          .when(mockNodeAgentClient)
          .executeCommand(any(), argThat(cmd -> cmd.contains("cat /tserver/conf/tserver.conf")));

    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  private void setupInfra() {
    Release release = Release.create(UUID.randomUUID(), dbVersion, "LTS");
    ReleaseArtifact artifact =
        ReleaseArtifact.create(
            "sha256", ReleaseArtifact.Platform.LINUX, Architecture.x86_64, "file_url");
    release.addArtifact(artifact);
    Region r1 = Region.create(onPremProvider, "region-1", "PlacementRegion 1", "default-image");
    Region r2 = Region.create(onPremProvider, "region-2", "PlacementRegion 2", "default-image");
    List<AvailabilityZone> zones = new ArrayList<>();
    zones.add(AvailabilityZone.createOrThrow(r1, "r1-az-1", "Region1-AZ1", "subnet-1"));
    zones.add(AvailabilityZone.createOrThrow(r1, "r1-az-2", "Region1-AZ2", "subnet-2"));
    zones.add(AvailabilityZone.createOrThrow(r2, "r2-az-3", "Region1-AZ3", "subnet-3"));
    List<String> allIps = new ArrayList<>(masterIps);
    allIps.addAll(tserverIps);
    allIps.forEach(
        ip -> {
          NodeAgent nodeAgent = new NodeAgent();
          nodeAgent.setIp(ip);
          nodeAgent.setName(ip);
          nodeAgent.setPort(9070);
          nodeAgent.setCustomerUuid(defaultCustomer.getUuid());
          nodeAgent.setOsType(OSType.LINUX);
          nodeAgent.setArchType(ArchType.AMD64);
          nodeAgent.setVersion("2024.2.4.0");
          nodeAgent.setHome("/home/yugabyte");
          nodeAgent.setState(State.READY);
          nodeAgent.save();
        });
    InstanceTypeDetails masterInstanceDetails = new InstanceTypeDetails();
    for (int i = 0; i < masterVolumeMountCount; i++) {
      InstanceType.VolumeDetails volDetails = new InstanceType.VolumeDetails();
      volDetails.volumeSizeGB = 100;
      volDetails.volumeType = InstanceType.VolumeType.EBS;
      volDetails.mountPath = "/mnt/master-data" + i;
      masterInstanceDetails.volumeDetailsList.add(volDetails);
    }
    InstanceTypeDetails tserverInstanceDetails = new InstanceTypeDetails();
    for (int i = 0; i < tserverVolumeMountCount; i++) {
      InstanceType.VolumeDetails volDetails = new InstanceType.VolumeDetails();
      volDetails.volumeSizeGB = 100;
      volDetails.volumeType = InstanceType.VolumeType.EBS;
      volDetails.mountPath = "/mnt/tserver-data" + i;
      tserverInstanceDetails.volumeDetailsList.add(volDetails);
    }
    InstanceType masterInstanceType =
        InstanceType.upsert(
            onPremProvider.getUuid(), masterInstanceTypeCode, 4, 16.0, masterInstanceDetails);
    InstanceType tserverInstanceType =
        InstanceType.upsert(
            onPremProvider.getUuid(), tserverInstanceTypeCode, 4, 16.0, tserverInstanceDetails);
    AtomicInteger azIdx = new AtomicInteger();
    BiConsumer<String, String> nodeInstanceCreator =
        (ip, instanceTypeCode) -> {
          AvailabilityZone zone = zones.get(azIdx.get());
          azIdx.getAndUpdate(i -> (i++) % zones.size());
          Region region = zone.getRegion();
          NodeInstanceFormData.NodeInstanceData nodeData =
              new NodeInstanceFormData.NodeInstanceData();
          nodeData.ip = ip;
          nodeData.region = region.getCode();
          nodeData.zone = zone.getCode();
          nodeData.instanceType = instanceTypeCode;
          nodeInstances.put(ip, NodeInstance.create(zone.getUuid(), nodeData));
        };
    masterIps.forEach(
        ip -> nodeInstanceCreator.accept(ip, masterInstanceType.getInstanceTypeCode()));
    azIdx.set(0);
    tserverIps.forEach(
        ip -> nodeInstanceCreator.accept(ip, tserverInstanceType.getInstanceTypeCode()));
  }

  private ImportUniverseTaskParams createTaskParams() {
    ImportUniverseTaskParams taskParams = new ImportUniverseTaskParams();
    taskParams.customerUuid = defaultCustomer.getUuid();
    taskParams.providerUuid = onPremProvider.getUuid();
    taskParams.universeName = "my-imported-universe";
    taskParams.masterAddrs = new HashSet<>(masterIps);
    taskParams.dedicatedNodes = true;
    taskParams.serverConfigs = new HashMap<>();
    ServerConfig masterServerConfig = new ServerConfig();
    masterServerConfig.specificGflags = new HashMap<>();
    masterServerConfig.serviceName = masterService;
    masterServerConfig.specificGflags.put("master-specific-gflag1", "m1");
    ServerConfig tserverServerConfig = new ServerConfig();
    tserverServerConfig.specificGflags = new HashMap<>();
    tserverServerConfig.serviceName = tserverService;
    tserverServerConfig.specificGflags.put("tserver-specific-gflag1", "t1");
    taskParams.serverConfigs.put(ServerType.MASTER, masterServerConfig);
    taskParams.serverConfigs.put(ServerType.TSERVER, tserverServerConfig);
    return taskParams;
  }

  private TaskInfo submitTask(ImportUniverseTaskParams taskParams) {
    try {
      UUID taskUUID = commissioner.submit(TaskType.ImportUniverse, taskParams);
      return waitForTask(taskUUID);
    } catch (InterruptedException e) {
      assertNull(e.getMessage());
    }
    return null;
  }

  private void verifyUniverse(Universe universe) {
    assertEquals(masterIps.size() + tserverIps.size(), universe.getNodes().size());
    Cluster cluster = universe.getUniverseDetails().getPrimaryCluster();
    Map<String, String> masterSpecificGFlags =
        cluster.userIntent.specificGFlags.getPerProcessFlags().value.get(ServerType.MASTER);
    Map<String, String> tserverSpecificGFlags =
        cluster.userIntent.specificGFlags.getPerProcessFlags().value.get(ServerType.TSERVER);
    assertEquals(Map.of("master-specific-gflag1", "m1"), masterSpecificGFlags);
    assertEquals(Map.of("tserver-specific-gflag1", "t1"), tserverSpecificGFlags);
    UniverseMigrationConfig migrationConfig = cluster.userIntent.getMigrationConfig();
    assertNotNull(migrationConfig);
    ServerSpecificConfig masterConfig = migrationConfig.getServerConfigs().get(ServerType.MASTER);
    assertEquals(masterService, masterConfig.systemdService);
    ServerSpecificConfig tserverConfig = migrationConfig.getServerConfigs().get(ServerType.TSERVER);
    assertEquals(tserverService, tserverConfig.systemdService);
    for (NodeDetails node : universe.getNodes()) {
      String ip = node.cloudInfo.private_ip;
      assertEquals(NodeState.Live, node.state);
      NodeInstance nodeInstance = nodeInstances.get(ip);
      nodeInstance.refresh();
      assertEquals(NodeInstance.State.USED, nodeInstance.getState());
      assertEquals(nodeInstance.getNodeUuid(), node.getNodeUuid());
      assertEquals(nodeInstance.getDetails().region, node.cloudInfo.region);
      assertEquals(nodeInstance.getDetails().zone, node.cloudInfo.az);
      assertEquals(cluster.uuid, node.placementUuid);
      DeviceInfo deviceInfo = cluster.userIntent.getDeviceInfoForNode(node);
      if (masterIps.contains(ip)) {
        assertTrue(node.isMaster);
        assertEquals(masterMountPaths, deviceInfo.mountPoints);
      }
      if (tserverIps.contains(ip)) {
        assertTrue(node.isTserver);
        assertEquals(tserverMountPaths, deviceInfo.mountPoints);
      }
      assertTrue(node.migrationPending);
    }
  }

  // TODO More tests can be added easily as all the mocks are available.
  @Test
  public void testImportUniverseSuccess() {
    ImportUniverseTaskParams taskParams = createTaskParams();
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    Universe universe =
        Universe.maybeGetUniverseByName(defaultCustomer.getId(), taskParams.universeName)
            .orElseThrow();
    verifyUniverse(universe);
  }

  @Test
  public void testImportUniverseNonUniformFsDirs() {
    doAnswer(
            i -> {
              NodeAgent nodeAgent = i.getArgument(0);
              NodeInstance nodeInstance = nodeInstances.get(nodeAgent.getIp());
              List<String> tserverGflagsList = new ArrayList<>();
              if (nodeAgent.getIp().equals(tserverIps.get(0))) {
                // Set a different one.
                tserverGflagsList.add("--fs_data_dirs=/mnt/diff1");
              } else {
                tserverGflagsList.add("--fs_data_dirs=" + tserverMountPaths);
              }
              tserverGflagsList.add("--placement_region=" + nodeInstance.getDetails().region);
              tserverGflagsList.add("--placement_zone=" + nodeInstance.getDetails().zone);
              return ShellResponse.create(0, String.join("\n", tserverGflagsList));
            })
        .when(mockNodeAgentClient)
        .executeCommand(any(), argThat(cmd -> cmd.contains("cat /tserver/conf/tserver.conf")));
    ImportUniverseTaskParams taskParams = createTaskParams();
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Failure, taskInfo.getTaskState());
    assertThat(
        taskInfo.getErrorMessage(),
        containsString("Some servers have different common gflags for TSERVER"));
    assertFalse(
        Universe.maybeGetUniverseByName(defaultCustomer.getId(), taskParams.universeName)
            .isPresent());
  }
}
