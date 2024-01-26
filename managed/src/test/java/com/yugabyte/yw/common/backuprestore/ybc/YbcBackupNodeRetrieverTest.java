// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.backuprestore.ybc;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.anyInt;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.ArgumentMatchers.nullable;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.ObjectMapper;
import com.google.common.net.HostAndPort;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.forms.BackupTableParams.ParallelBackupState;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import java.io.IOException;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import org.apache.commons.lang3.StringUtils;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class YbcBackupNodeRetrieverTest extends FakeDBApplication {

  private Universe mockUniverse;
  private Customer mockCustomer;
  private final String multiRegionPlacementInfo =
      "{ "
          + "    \"cloudList\": ["
          + "        {"
          + "            \"uuid\": \"6c8da989-a988-4d5e-ac2c-82e944873808\","
          + "            \"code\": \"gcp\","
          + "            \"regionList\": ["
          + "                {"
          + "                    \"uuid\": \"869cf266-45e0-4fb5-9390-8698f573235b\","
          + "                    \"code\": \"asia-southeast1\","
          + "                    \"name\": \"Singapore\","
          + "                    \"azList\": ["
          + "                        {"
          + "                            \"uuid\": \"7fa30bc1-5b7a-4d8f-beea-36dd23af8841\","
          + "                            \"name\": \"asia-southeast1-a\","
          + "                            \"subnet\": \"yb-subnet-asia-southeast1\","
          + "                            \"numNodesInAZ\": 1,"
          + "                            \"isAffinitized\": true"
          + "                        }"
          + "                    ]"
          + "                },"
          + "                {"
          + "                    \"uuid\": \"bd362dd3-b332-4d27-bb11-a461f9be6076\","
          + "                    \"code\": \"us-west1\","
          + "                    \"name\": \"Oregon\","
          + "                    \"azList\": ["
          + "                        {"
          + "                            \"uuid\": \"78b7614c-a612-4919-94e3-915baf7a91b8\","
          + "                            \"name\": \"us-west1-a\","
          + "                            \"subnet\": \"yb-subnet-us-west1\","
          + "                            \"numNodesInAZ\": 1,"
          + "                            \"isAffinitized\": true"
          + "                        },"
          + "                        {"
          + "                            \"uuid\": \"d8fa2062-4641-405e-9b3c-59c8802fff40\","
          + "                            \"name\": \"us-west1-b\","
          + "                            \"subnet\": \"yb-subnet-us-west1\","
          + "                            \"numNodesInAZ\": 1,"
          + "                            \"isAffinitized\": true"
          + "                        }"
          + "                    ]"
          + "                }"
          + "            ]"
          + "        }"
          + "    ]"
          + "}";

  private final String multiRegionUserIntent =
      "{"
          + "    \"universeName\": \"test\","
          + "    \"provider\": \"6c8da989-a988-4d5e-ac2c-82e944873808\","
          + "    \"providerType\": \"gcp\","
          + "    \"regionList\": ["
          + "        \"bd362dd3-b332-4d27-bb11-a461f9be6076\","
          + "        \"869cf266-45e0-4fb5-9390-8698f573235b\""
          + "    ],"
          + "    \"numNodes\": 3,"
          + "    \"replicationFactor\": 3,"
          + "    \"dedicatedNodes\": false,"
          + "    \"instanceType\": \"n1-standard-1\","
          + "    \"deviceInfo\": {"
          + "        \"numVolumes\": 1,"
          + "        \"volumeSize\": \"375\","
          + "        \"storageClass\": \"standard\","
          + "        \"storageType\": \"Persistent\""
          + "    },"
          + "    \"accessKeyCode\": \"foo\""
          + "}";

  private final String multiRegionNodeDetails =
      "["
          + "    {"
          + "        \"nodeIdx\": 2,"
          + "        \"cloudInfo\": {"
          + "            \"az\": \"us-west1-b\","
          + "            \"region\": \"us-west1\","
          + "            \"cloud\": \"gcp\","
          + "             \"private_ip\": \"127.0.0.3\""
          + "        },"
          + "        \"azUuid\": \"d8fa2062-4641-405e-9b3c-59c8802fff40\","
          + "        \"placementUuid\": \"d3a191fa-48be-4a74-a787-1472bc736c92\","
          + "        \"state\": \"Live\","
          + "        \"isMaster\": true,"
          + "        \"isTserver\": true"
          + "    },"
          + "    {"
          + "        \"nodeIdx\": 1,"
          + "        \"cloudInfo\": {"
          + "            \"az\": \"us-west1-a\","
          + "            \"region\": \"us-west1\","
          + "            \"cloud\": \"gcp\","
          + "             \"private_ip\": \"127.0.0.2\""
          + "        },"
          + "        \"azUuid\": \"78b7614c-a612-4919-94e3-915baf7a91b8\","
          + "        \"placementUuid\": \"d3a191fa-48be-4a74-a787-1472bc736c92\","
          + "        \"state\": \"Live\","
          + "        \"isMaster\": true,"
          + "        \"isTserver\": true"
          + "    },"
          + "    {"
          + "        \"nodeIdx\": 3,"
          + "        \"cloudInfo\": {"
          + "            \"az\": \"asia-southeast1-a\","
          + "            \"region\": \"asia-southeast1\","
          + "            \"cloud\": \"gcp\","
          + "             \"private_ip\": \"127.0.0.1\""
          + "        },"
          + "        \"azUuid\": \"7fa30bc1-5b7a-4d8f-beea-36dd23af8841\","
          + "        \"placementUuid\": \"d3a191fa-48be-4a74-a787-1472bc736c92\","
          + "        \"state\": \"Live\","
          + "        \"isMaster\": true,"
          + "        \"isTserver\": true"
          + "    }"
          + "]";

  @Before
  public void setup() throws IOException {
    mockCustomer = ModelFactory.testCustomer();
    UniverseDefinitionTaskParams params = new UniverseDefinitionTaskParams();
    UUID universeUUID = UUID.randomUUID();
    params.setUniverseUUID(universeUUID);
    ObjectMapper mapper = new ObjectMapper();
    params.nodeDetailsSet =
        mapper.readValue(
            Json.parse(multiRegionNodeDetails).traverse(),
            mapper.getTypeFactory().constructCollectionType(Set.class, NodeDetails.class));
    params.setEnableYbc(true);
    params.setYbcInstalled(true);
    params.upsertPrimaryCluster(
        mapper.readValue(multiRegionUserIntent, UserIntent.class),
        mapper.readValue(multiRegionPlacementInfo, PlacementInfo.class));
    params.getPrimaryCluster().setUuid(params.nodeDetailsSet.iterator().next().placementUuid);
    mockUniverse = Universe.create(params, mockCustomer.getId());
    HostAndPort leaderHP = HostAndPort.fromParts("127.0.0.2", 7100);
    when(mockService.getClient(anyString(), nullable(String.class))).thenReturn(mockYBClient);
    when(mockYBClient.getLeaderMasterHostAndPort()).thenReturn(leaderHP);
  }

  @Test
  public void testNodesInPoolNewTask() throws InterruptedException {
    Map<UUID, ParallelBackupState> subTasksMap = new HashMap<>();
    subTasksMap.put(UUID.randomUUID(), new ParallelBackupState());
    subTasksMap.put(UUID.randomUUID(), new ParallelBackupState());
    subTasksMap.put(UUID.randomUUID(), new ParallelBackupState());
    when(mockYbcManager.ybcPingCheck(anyString(), eq(null), anyInt())).thenReturn(true);
    YbcBackupNodeRetriever ybcBackupNodeRetriever =
        new YbcBackupNodeRetriever(mockUniverse.getUniverseUUID(), 3);
    ybcBackupNodeRetriever.initializeNodePoolForBackups(subTasksMap);
    // Verify 3 polls are successful.
    String node_ip1 = ybcBackupNodeRetriever.getNodeIpForBackup();
    String node_ip2 = ybcBackupNodeRetriever.getNodeIpForBackup();
    String node_ip3 = ybcBackupNodeRetriever.getNodeIpForBackup();
    assertTrue(StringUtils.isNotBlank(node_ip1));
    assertTrue(StringUtils.isNotBlank(node_ip2));
    assertTrue(StringUtils.isNotBlank(node_ip3));

    // Verify node-ips retrieved are not equal among themselves.
    assertNotEquals(node_ip1, node_ip2);
    assertNotEquals(node_ip2, node_ip3);
    assertNotEquals(node_ip1, node_ip3);

    // Verify next poll returns null.
    assertTrue(StringUtils.isBlank(ybcBackupNodeRetriever.peekNodeIpForBackup()));
  }

  @Test
  public void testNodesInPoolResumedTaskWithRunningBackup() throws InterruptedException {
    Map<UUID, ParallelBackupState> subTasksMap = new HashMap<>();
    ParallelBackupState bS1 = new ParallelBackupState();
    bS1.nodeIp = "127.0.0.1";
    subTasksMap.put(UUID.randomUUID(), bS1);
    subTasksMap.put(UUID.randomUUID(), new ParallelBackupState());
    subTasksMap.put(UUID.randomUUID(), new ParallelBackupState());
    when(mockYbcManager.ybcPingCheck(anyString(), eq(null), anyInt())).thenReturn(true);
    YbcBackupNodeRetriever ybcBackupNodeRetriever =
        new YbcBackupNodeRetriever(mockUniverse.getUniverseUUID(), 3);
    ybcBackupNodeRetriever.initializeNodePoolForBackups(subTasksMap);
    // Verify 2 polls are successful and node-ips are not equal to 127.0.0.1
    String node_ip2 = ybcBackupNodeRetriever.getNodeIpForBackup();
    assertTrue(StringUtils.isNotBlank(node_ip2));
    assertNotEquals(bS1.nodeIp, node_ip2);

    String node_ip3 = ybcBackupNodeRetriever.getNodeIpForBackup();
    assertTrue(StringUtils.isNotBlank(node_ip3));
    assertNotEquals(bS1.nodeIp, node_ip3);

    // Verify node-ips retrieved are not equal among themselves.
    assertNotEquals(node_ip2, node_ip3);

    // Verify next poll returns null.
    assertTrue(StringUtils.isBlank(ybcBackupNodeRetriever.peekNodeIpForBackup()));

    // Add "127.0.0.1" back to pool.
    ybcBackupNodeRetriever.putNodeIPBackToPool(bS1.nodeIp);

    // Verify poll succeeds and ip equal to "127.0.0.1"
    assertTrue(StringUtils.equals(ybcBackupNodeRetriever.getNodeIpForBackup(), "127.0.0.1"));
  }

  @Test
  public void testNodesInPoolResumedTaskWithoutRunningBackup() throws InterruptedException {
    Map<UUID, ParallelBackupState> subTasksMap = new HashMap<>();
    ParallelBackupState bS1 = new ParallelBackupState();
    bS1.alreadyScheduled = true;
    subTasksMap.put(UUID.randomUUID(), bS1);
    subTasksMap.put(UUID.randomUUID(), new ParallelBackupState());
    subTasksMap.put(UUID.randomUUID(), new ParallelBackupState());
    when(mockYbcManager.ybcPingCheck(anyString(), eq(null), anyInt())).thenReturn(true);
    YbcBackupNodeRetriever ybcBackupNodeRetriever =
        new YbcBackupNodeRetriever(mockUniverse.getUniverseUUID(), 3);
    ybcBackupNodeRetriever.initializeNodePoolForBackups(subTasksMap);
    // Verify 3 polls are successful.
    String node_ip1 = ybcBackupNodeRetriever.getNodeIpForBackup();
    String node_ip2 = ybcBackupNodeRetriever.getNodeIpForBackup();
    String node_ip3 = ybcBackupNodeRetriever.getNodeIpForBackup();
    assertTrue(StringUtils.isNotBlank(node_ip1));
    assertTrue(StringUtils.isNotBlank(node_ip2));
    assertTrue(StringUtils.isNotBlank(node_ip3));

    // Verify next poll returns null.
    assertTrue(StringUtils.isBlank(ybcBackupNodeRetriever.peekNodeIpForBackup()));
  }

  @Test
  public void testPoolSizeOne() throws InterruptedException {
    Map<UUID, ParallelBackupState> subTasksMap = new HashMap<>();
    YbcBackupNodeRetriever ybcBackupNodeRetriever =
        new YbcBackupNodeRetriever(mockUniverse.getUniverseUUID(), 1);
    when(mockYbcManager.ybcPingCheck(anyString(), eq(null), anyInt())).thenReturn(true);
    ybcBackupNodeRetriever.initializeNodePoolForBackups(subTasksMap);
    // Verify 1 poll is successful.
    String node_ip1 = ybcBackupNodeRetriever.getNodeIpForBackup();
    assertTrue(StringUtils.isNotBlank(node_ip1));

    // Verify next poll returns null.
    assertTrue(StringUtils.isBlank(ybcBackupNodeRetriever.peekNodeIpForBackup()));
  }

  @Test
  public void testPoolWithFewUnhealthyNodes() throws InterruptedException {
    Map<UUID, ParallelBackupState> subTasksMap = new HashMap<>();
    subTasksMap.put(UUID.randomUUID(), new ParallelBackupState());
    subTasksMap.put(UUID.randomUUID(), new ParallelBackupState());
    subTasksMap.put(UUID.randomUUID(), new ParallelBackupState());

    when(mockYbcManager.ybcPingCheck(anyString(), eq(null), anyInt()))
        .thenReturn(false)
        .thenReturn(true);
    YbcBackupNodeRetriever ybcBackupNodeRetriever =
        new YbcBackupNodeRetriever(mockUniverse.getUniverseUUID(), 3);
    ybcBackupNodeRetriever.initializeNodePoolForBackups(subTasksMap);

    // Verify 2 polls are successful.
    String node_ip1 = ybcBackupNodeRetriever.getNodeIpForBackup();
    String node_ip2 = ybcBackupNodeRetriever.getNodeIpForBackup();

    assertTrue(StringUtils.isNotBlank(node_ip1));
    assertTrue(StringUtils.isNotBlank(node_ip2));

    // Verify next poll returns null.
    assertTrue(StringUtils.isBlank(ybcBackupNodeRetriever.peekNodeIpForBackup()));
  }

  @Test
  public void testAllUnhealthyNodes() {
    Map<UUID, ParallelBackupState> subTasksMap = new HashMap<>();
    subTasksMap.put(UUID.randomUUID(), new ParallelBackupState());
    subTasksMap.put(UUID.randomUUID(), new ParallelBackupState());
    subTasksMap.put(UUID.randomUUID(), new ParallelBackupState());

    when(mockYbcManager.ybcPingCheck(anyString(), eq(null), anyInt())).thenReturn(false);
    YbcBackupNodeRetriever ybcBackupNodeRetriever =
        new YbcBackupNodeRetriever(mockUniverse.getUniverseUUID(), 3);
    assertThrows(
        RuntimeException.class,
        () -> ybcBackupNodeRetriever.initializeNodePoolForBackups(subTasksMap));
  }

  @Test
  public void testNodePreferenceOneNodeMasterLeader() {
    when(mockYbcManager.ybcPingCheck(anyString(), eq(null), anyInt())).thenReturn(true);
    // Verify parallelism 1 get master leader node
    YbcBackupNodeRetriever ybcBackupNodeRetriever =
        new YbcBackupNodeRetriever(mockUniverse.getUniverseUUID(), 1);
    Map<UUID, ParallelBackupState> subTasksMap = new HashMap<>();
    subTasksMap.put(UUID.randomUUID(), new ParallelBackupState());
    ybcBackupNodeRetriever.initializeNodePoolForBackups(subTasksMap);
    String node_ip1 = ybcBackupNodeRetriever.getNodeIpForBackup();
    String leaderIP = mockUniverse.getMasterLeaderNode().cloudInfo.private_ip;
    assertTrue(node_ip1.equals(leaderIP));
  }

  @Test
  public void testNodePreferenceOrderMultipleIPs() {
    when(mockYbcManager.ybcPingCheck(anyString(), eq(null), anyInt())).thenReturn(true);
    ArgumentCaptor<String> nodeIPCaptor = ArgumentCaptor.forClass(String.class);
    YbcBackupNodeRetriever ybcBackupNodeRetriever =
        new YbcBackupNodeRetriever(mockUniverse.getUniverseUUID(), 3);
    Map<UUID, ParallelBackupState> subTasksMap = new HashMap<>();
    subTasksMap.put(UUID.randomUUID(), new ParallelBackupState());
    subTasksMap.put(UUID.randomUUID(), new ParallelBackupState());
    subTasksMap.put(UUID.randomUUID(), new ParallelBackupState());
    ybcBackupNodeRetriever.initializeNodePoolForBackups(subTasksMap);
    verify(mockYbcManager, times(3)).ybcPingCheck(nodeIPCaptor.capture(), eq(null), anyInt());
    List<String> capturedIPs = nodeIPCaptor.getAllValues();
    NodeDetails masterLeaderNode = mockUniverse.getMasterLeaderNode();
    List<NodeDetails> nodeDetails = mockUniverse.getRunningTserversInPrimaryCluster();
    Set<String> leaderRegionNodeIPs =
        nodeDetails.stream()
            .filter(nD -> nD.getRegion().equals(masterLeaderNode.getRegion()))
            .map(nD -> nD.cloudInfo.private_ip)
            .collect(Collectors.toSet());
    // First is leader.
    assertTrue(capturedIPs.get(0).equals(masterLeaderNode.cloudInfo.private_ip));
    // Second is same region node as leader.
    assertTrue(leaderRegionNodeIPs.contains(capturedIPs.get(1)));
    // Third is different region( least preferred ).
    assertFalse(leaderRegionNodeIPs.contains(capturedIPs.get(2)));
  }
}
