// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.local;

import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.Util.YUGABYTE_DB;
import static com.yugabyte.yw.forms.UniverseConfigureTaskParams.ClusterOperationType.CREATE;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.CreateTableSpaces;
import com.yugabyte.yw.common.FakeApiHelper;
import com.yugabyte.yw.common.LocalNodeManager;
import com.yugabyte.yw.common.NodeActionType;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.TableSpaceStructures;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.controllers.UniverseControllerRequestBinder;
import com.yugabyte.yw.forms.NodeActionFormData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import java.io.IOException;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.TimeUnit;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.hamcrest.CoreMatchers;
import org.junit.Before;
import org.junit.Test;
import org.yb.client.YBClient;
import play.libs.Json;
import play.mvc.Result;

@Slf4j
public class NodeOperationsLocalTest extends LocalProviderUniverseTestBase {

  private Result nodeOperationInUniverse(
      UUID universeUUID, String nodeName, NodeActionFormData formData) {
    return FakeApiHelper.doRequestWithAuthTokenAndBody(
        app,
        "PUT",
        "/api/customers/"
            + customer.getUuid()
            + "/universes/"
            + universeUUID
            + "/nodes/"
            + nodeName,
        user.createAuthToken(),
        Json.toJson(formData));
  }

  @Override
  protected Pair<Integer, Integer> getIpRange() {
    return new Pair<>(240, 270);
  }

  @Before
  public void setUpDNS() {
    provider.getDetails().getCloudInfo().local.setHostedZoneId("test");
    provider.update();
    localNodeManager.setCheckDNS(true);
  }

  @Test
  public void testStopStartNodeInUniverse() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.specificGFlags = getGFlags();
    Universe universe = createUniverse(userIntent);
    NodeDetails nodeDetails = universe.getUniverseDetails().nodeDetailsSet.iterator().next();
    String nodeName = nodeDetails.nodeName;

    NodeActionFormData formData = new NodeActionFormData();
    formData.nodeAction = NodeActionType.STOP;
    Result result = nodeOperationInUniverse(universe.getUniverseUUID(), nodeName, formData);
    checkAndWaitForTask(result);
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    for (NodeDetails details : universe.getUniverseDetails().nodeDetailsSet) {
      if (details.nodeName.equals(nodeName)) {
        assertEquals(NodeDetails.NodeState.Stopped, details.state);
      } else {
        assertEquals(NodeDetails.NodeState.Live, details.state);
      }
    }
    initYSQL(universe);
    verifyYSQL(universe);
    verifyUniverseState(universe);

    formData.nodeAction = NodeActionType.START;
    result = nodeOperationInUniverse(universe.getUniverseUUID(), nodeName, formData);
    checkAndWaitForTask(result);
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    for (NodeDetails details : universe.getUniverseDetails().nodeDetailsSet) {
      assertEquals(NodeDetails.NodeState.Live, details.state);
    }
    verifyUniverseState(universe);
  }

  @Test
  public void testMasterNodeRemoval() throws InterruptedException {
    Universe universe = createUniverse(4, 3);
    Map<UUID, List<NodeDetails>> nodesGroupedByAzUuid =
        universe.getUniverseDetails().nodeDetailsSet.stream()
            .collect(Collectors.groupingBy(node -> node.azUuid));

    NodeDetails nodeDetails = null;
    for (Map.Entry<UUID, List<NodeDetails>> entry : nodesGroupedByAzUuid.entrySet()) {
      List<NodeDetails> nodesInAz = entry.getValue();
      if (nodesInAz.size() > 1) {
        nodeDetails = nodesInAz.stream().filter(n -> n.isMaster && n.isTserver).findFirst().get();
        break;
      }
    }

    initYSQL(universe);
    verifyYSQL(universe);
    verifyUniverseState(universe);

    String nodeName = nodeDetails.nodeName;
    NodeActionFormData formData = new NodeActionFormData();
    formData.nodeAction = NodeActionType.STOP;
    NodeActionFormData.startMasterOnStopNode = true;
    Result result = nodeOperationInUniverse(universe.getUniverseUUID(), nodeName, formData);
    checkAndWaitForTask(result);
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());

    // Verify that we spwan up a new master.
    assertEquals(
        3,
        universe.getUniverseDetails().nodeDetailsSet.stream()
            .filter(n -> n.isMaster && n.state.equals(NodeDetails.NodeState.Live))
            .count());
    verifyUniverseState(universe);
  }

  @Test
  public void testRemoveReleaseNodeFromUniverse() throws InterruptedException {
    Universe universe = createUniverse(3, 1);
    NodeDetails nodeDetails =
        universe.getUniverseDetails().nodeDetailsSet.stream()
            .filter(n -> !n.isMaster && n.isTserver)
            .findFirst()
            .get();
    initYSQL(universe);
    verifyYSQL(universe);
    verifyUniverseState(universe);

    String nodeName = nodeDetails.nodeName;
    NodeActionFormData formData = new NodeActionFormData();
    formData.nodeAction = NodeActionType.REMOVE;
    Result result = nodeOperationInUniverse(universe.getUniverseUUID(), nodeName, formData);
    checkAndWaitForTask(result);
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());

    for (NodeDetails details : universe.getUniverseDetails().nodeDetailsSet) {
      if (details.nodeName.equals(nodeName)) {
        assertEquals(NodeDetails.NodeState.Removed, details.state);
      } else {
        assertEquals(NodeDetails.NodeState.Live, details.state);
      }
    }
    verifyUniverseState(universe);

    formData.nodeAction = NodeActionType.RELEASE;
    result = nodeOperationInUniverse(universe.getUniverseUUID(), nodeName, formData);
    checkAndWaitForTask(result);
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());

    for (NodeDetails details : universe.getUniverseDetails().nodeDetailsSet) {
      if (details.nodeName.equals(nodeName)) {
        assertEquals(NodeDetails.NodeState.Decommissioned, details.state);
      } else {
        assertEquals(NodeDetails.NodeState.Live, details.state);
      }
    }

    formData.nodeAction = NodeActionType.DELETE;
    result = nodeOperationInUniverse(universe.getUniverseUUID(), nodeName, formData);
    checkAndWaitForTask(result);
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());

    for (NodeDetails details : universe.getUniverseDetails().nodeDetailsSet) {
      assertEquals(NodeDetails.NodeState.Live, details.state);
    }
    verifyUniverseState(universe);
  }

  @Test
  public void testRemoveNodeFromUniverseGeoFAIL() throws InterruptedException {
    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();

    taskParams.nodePrefix = "univConfCreate";
    PlacementInfo placementInfo = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(az1.getUuid(), placementInfo, 2, 3);
    PlacementInfoUtil.addPlacementZone(az2.getUuid(), placementInfo, 1, 1);

    UniverseDefinitionTaskParams.PartitionInfo partitionInfo =
        new UniverseDefinitionTaskParams.PartitionInfo();
    partitionInfo.setDefaultPartition(true);
    partitionInfo.setName("geo1");
    partitionInfo.setTablespaceName("geo1TableSpace");
    partitionInfo.setReplicationFactor(3);
    partitionInfo.setPlacement(placementInfo);

    PlacementInfo placementInfo2 = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(az3.getUuid(), placementInfo2, 1, 1);
    UniverseDefinitionTaskParams.PartitionInfo partitionInfo2 =
        new UniverseDefinitionTaskParams.PartitionInfo();
    partitionInfo2.setName("geo2");
    partitionInfo2.setPlacement(placementInfo2);
    partitionInfo2.setReplicationFactor(1);
    partitionInfo2.setTablespaceName("geo2TableSpace");

    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.numNodes = 5;
    taskParams.upsertPrimaryCluster(userIntent, Arrays.asList(partitionInfo, partitionInfo2), null);
    assertTrue(taskParams.getPrimaryCluster().isGeoPartitioned());
    PlacementInfoUtil.updateUniverseDefinition(
        taskParams, customer.getId(), taskParams.getPrimaryCluster().uuid, CREATE);
    Universe universe = createUniverse(taskParams);
    verifyUniverseState(universe);
    initYSQL(universe, "table_in_geo1", partitionInfo.getTablespaceName());

    NodeDetails nodeDetails =
        universe.getUniverseDetails().nodeDetailsSet.stream()
            .filter(n -> n.azUuid.equals(az1.getUuid()))
            .findFirst()
            .get();

    TableSpaceStructures.TableSpaceInfo az1Tablespace =
        initTablespace("az1_tablespace", az1.getUuid(), 3);
    String createTblSpace = CreateTableSpaces.getTablespaceCreationQuery(az1Tablespace);

    ShellResponse response =
        localNodeUniverseManager.runYsqlCommand(
            nodeDetails,
            universe,
            YUGABYTE_DB,
            createTblSpace,
            20,
            userIntent.isYSQLAuthEnabled(),
            false);
    assertTrue("Message is " + response.getMessage(), response.isSuccess());
    initYSQL(universe, "table_in_az_geo", "az1_tablespace");

    String nodeName = nodeDetails.nodeName;
    NodeActionFormData formData = new NodeActionFormData();
    formData.nodeAction = NodeActionType.REMOVE;
    Result result = nodeOperationInUniverse(universe.getUniverseUUID(), nodeName, formData);
    // Although there are excessive nodes in az1 (3 nodes 2 replicas) for normal tables,
    // this should fail as tablespace az1_tablespace  occupies the whole az.
    TaskInfo taskInfo = checkAndWaitForTask(result, TaskInfo.State.Failure);
    TaskInfo subTaskInfo =
        taskInfo.getSubTasks().stream()
            .filter(st -> st.getTaskType() == TaskType.CheckTabletsMovementAvailableForNode)
            .findFirst()
            .get();
    String expectedMsg =
        "Non-empty tablespace az1_tablespace has 3 replicas in zone az-1,"
            + " but there will be only 2 nodes after operation";
    assertThat(subTaskInfo.getErrorMessage(), CoreMatchers.containsString(expectedMsg));
  }

  @Test
  public void testRemoveNodeFromUniverseFAIL() throws InterruptedException, IOException {
    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();

    taskParams.nodePrefix = "univConfCreate";
    PlacementInfo placementInfo = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(az1.getUuid(), placementInfo, 2, 3);
    PlacementInfoUtil.addPlacementZone(az2.getUuid(), placementInfo, 1, 1);

    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.numNodes = 4;
    userIntent.specificGFlags = getGFlags("follower_unavailable_considered_failed_sec", "5");
    taskParams.upsertPrimaryCluster(userIntent, null, placementInfo);
    taskParams.userAZSelected = true;
    PlacementInfoUtil.updateUniverseDefinition(
        taskParams, customer.getId(), taskParams.getPrimaryCluster().uuid, CREATE);
    taskParams
        .getPrimaryCluster()
        .placementInfo
        .azStream()
        .filter(az -> az.uuid.equals(az1.getUuid()))
        .forEach(az -> az.replicationFactor = 2);
    Universe universe = createUniverse(taskParams);
    verifyUniverseState(universe);

    List<NodeDetails> az1Nodes =
        universe.getUniverseDetails().nodeDetailsSet.stream()
            .filter(n -> n.azUuid.equals(az1.getUuid()))
            .collect(Collectors.toList());
    NodeDetails toRemove = az1Nodes.get(0);
    // Killing tserver on the other node.
    killProcessOnNode(
        universe.getUniverseUUID(), az1Nodes.get(1).nodeName, UniverseTaskBase.ServerType.TSERVER);
    log.debug("Killed tserver on {}", az1Nodes.get(1).cloudInfo.private_ip);
    Thread.sleep(TimeUnit.SECONDS.toMillis(60));
    String nodeName = toRemove.nodeName;
    NodeActionFormData formData = new NodeActionFormData();

    formData.nodeAction = NodeActionType.REMOVE;
    Result result = nodeOperationInUniverse(universe.getUniverseUUID(), nodeName, formData);
    TaskInfo taskInfo = checkAndWaitForTask(result, TaskInfo.State.Failure);
    TaskInfo subTaskInfo =
        taskInfo.getSubTasks().stream()
            .filter(st -> st.getTaskType() == TaskType.CheckTabletsMovementAvailableForNode)
            .findFirst()
            .get();
    String expectedMsg = "Expected to have 0 tablets on the node to remove it";
    assertThat(subTaskInfo.getErrorMessage(), CoreMatchers.containsString(expectedMsg));
  }

  @Test
  public void testDecommissionNode() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.specificGFlags = getGFlags();
    userIntent.replicationFactor = 1;
    userIntent.numNodes = 3;
    Universe universe = createUniverse(userIntent);
    NodeDetails nodeDetails =
        universe.getUniverseDetails().nodeDetailsSet.stream()
            .filter(n -> !n.isMaster && n.isTserver)
            .findFirst()
            .get();
    initYSQL(universe);
    verifyYSQL(universe);
    verifyUniverseState(universe);

    String nodeName = nodeDetails.nodeName;
    NodeActionFormData formData = new NodeActionFormData();
    formData.nodeAction = NodeActionType.DECOMMISSION;
    Result result = nodeOperationInUniverse(universe.getUniverseUUID(), nodeName, formData);
    checkAndWaitForTask(result);
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());

    for (NodeDetails details : universe.getUniverseDetails().nodeDetailsSet) {
      assertEquals(NodeDetails.NodeState.Live, details.state);
    }
    verifyUniverseState(universe);
  }

  @Test
  public void testDecommissionNodeMasterReplace() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.specificGFlags = getGFlags();
    userIntent.replicationFactor = 3;
    userIntent.numNodes = 4;
    Universe universe = createUniverse(userIntent);

    Map<UUID, List<NodeDetails>> nodesGroupedByAzUuid =
        universe.getUniverseDetails().nodeDetailsSet.stream()
            .collect(Collectors.groupingBy(node -> node.azUuid));
    String nodeToRemove = null;
    for (Map.Entry<UUID, List<NodeDetails>> entry : nodesGroupedByAzUuid.entrySet()) {
      List<NodeDetails> nodesInAz = entry.getValue();
      if (nodesInAz.size() > 1) {
        nodeToRemove =
            nodesInAz.stream().filter(n -> n.isMaster && n.isTserver).findFirst().get().nodeName;
        break;
      }
    }
    initYSQL(universe);
    verifyYSQL(universe);
    verifyUniverseState(universe);

    NodeActionFormData formData = new NodeActionFormData();
    formData.nodeAction = NodeActionType.DECOMMISSION;
    Result result = nodeOperationInUniverse(universe.getUniverseUUID(), nodeToRemove, formData);
    checkAndWaitForTask(result);
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());

    // Verify that we spwan up a new master.
    assertEquals(
        3,
        universe.getUniverseDetails().nodeDetailsSet.stream()
            .filter(n -> n.isMaster && n.state.equals(NodeDetails.NodeState.Live))
            .count());
    for (NodeDetails details : universe.getUniverseDetails().nodeDetailsSet) {
      assertEquals(NodeDetails.NodeState.Live, details.state);
    }
    verifyUniverseState(universe);
  }

  @Test
  public void testDecommissionNodeReadReplica() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.specificGFlags = getGFlags();
    userIntent.replicationFactor = 1;
    userIntent.numNodes = 2;
    Universe universe = createUniverse(userIntent);
    initYSQL(universe);

    UniverseDefinitionTaskParams.UserIntent rrIntent = getDefaultUserIntent();
    rrIntent.replicationFactor = 1;
    rrIntent.numNodes = 2;
    doAddReadReplica(universe, rrIntent);

    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    verifyYSQL(universe);
    verifyUniverseState(universe);
    Pair<Long, Long> primaryCounts =
        verifyNodeDetails(universe, universe.getUniverseDetails().getPrimaryCluster());
    Pair<Long, Long> asyncCounts =
        verifyNodeDetails(universe, universe.getUniverseDetails().getReadOnlyClusters().get(0));

    UniverseDefinitionTaskParams.Cluster asyncCluster =
        universe.getUniverseDetails().getReadOnlyClusters().get(0);
    String nodeToRemove =
        universe.getNodes().stream()
            .filter(n -> n.placementUuid.equals(asyncCluster.uuid))
            .findFirst()
            .orElseThrow()
            .getNodeName();

    NodeActionFormData formData = new NodeActionFormData();
    formData.nodeAction = NodeActionType.DECOMMISSION;
    Result result = nodeOperationInUniverse(universe.getUniverseUUID(), nodeToRemove, formData);
    checkAndWaitForTask(result);
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    log.info("universe definition json {}", universe.getUniverseDetailsJson());

    verifyUniverseState(universe);

    Pair<Long, Long> primaryCountsFinal =
        verifyNodeDetails(universe, universe.getUniverseDetails().getPrimaryCluster());
    Pair<Long, Long> asyncCountsFinal =
        verifyNodeDetails(universe, universe.getUniverseDetails().getReadOnlyClusters().get(0));
    assertTrue(
        "Primary cluster unchanged by this decommission", primaryCountsFinal.equals(primaryCounts));
    assertTrue(
        "Async cluster decreased by this decommission",
        asyncCountsFinal.getSecond() == asyncCounts.getSecond() - 1);

    verifyUniverseState(universe);
  }

  @Test
  public void testStartMasterRetries() throws InterruptedException {
    Universe universe = createUniverse(4, 3);
    Map<UUID, Integer> zoneToCount =
        PlacementInfoUtil.getAzUuidToNumNodes(
            universe.getUniverseDetails().getPrimaryCluster().placementInfo);

    NodeDetails nodeWithMaster =
        universe.getUniverseDetails().nodeDetailsSet.stream()
            .filter(n -> n.isMaster && zoneToCount.get(n.azUuid) == 2)
            .findFirst()
            .get();
    NodeActionFormData.startMasterOnStopNode = false;
    NodeTaskParams taskParams =
        UniverseControllerRequestBinder.deepCopy(
            universe.getUniverseDetails(), NodeTaskParams.class);
    NodeActionType nodeActionType = NodeActionType.STOP;
    taskParams.nodeName = nodeWithMaster.getNodeName();
    UUID taskUUID = commissioner.submit(nodeActionType.getCommissionerTask(), taskParams);
    TaskInfo taskInfo = waitForTask(taskUUID, 500);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertEquals(2, universe.getMasters().size());
    NodeDetails nodeWithoutMaster =
        universe.getUniverseDetails().nodeDetailsSet.stream()
            .filter(n -> !n.isMaster && !n.getNodeName().equals(nodeWithMaster.getNodeName()))
            .findFirst()
            .get();

    taskParams =
        UniverseControllerRequestBinder.deepCopy(
            universe.getUniverseDetails(), NodeTaskParams.class);
    taskParams.nodeName = nodeWithoutMaster.getNodeName();

    super.verifyTaskRetries(
        customer,
        CustomerTask.TaskType.StartMaster,
        CustomerTask.TargetType.Universe,
        universe.getUniverseUUID(),
        TaskType.StartMasterOnNode,
        taskParams,
        false);
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    verifyUniverseState(universe);
  }

  @Test
  public void testStartAlreadyStarted() throws InterruptedException {
    Universe universe = createUniverse(3, 1);
    NodeDetails nodeDetails =
        universe.getUniverseDetails().nodeDetailsSet.stream()
            .filter(n -> !n.isMaster && n.isTserver)
            .findFirst()
            .get();
    LocalNodeManager.NodeInfo nodeInfo = localNodeManager.getNodeInfo(nodeDetails);
    verifyUniverseState(universe);

    String nodeName = nodeDetails.nodeName;
    NodeActionFormData formData = new NodeActionFormData();
    formData.nodeAction = NodeActionType.STOP;
    Result result = nodeOperationInUniverse(universe.getUniverseUUID(), nodeName, formData);
    checkAndWaitForTask(result);
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    try (YBClient client =
        ybClientService.getClient(
            universe.getMasterAddresses(), universe.getCertificateNodetoNode())) {
      assertFalse(universe.getNode(nodeName).isTserver);
      localNodeManager.startProcessForNode(
          universe.getUniverseDetails().getPrimaryCluster().userIntent,
          UniverseTaskBase.ServerType.TSERVER,
          nodeInfo);
      waitTillNumOfTservers(client, 3);
    } catch (Exception e) {
      throw new RuntimeException(e);
    }

    formData.nodeAction = NodeActionType.START;
    result = nodeOperationInUniverse(universe.getUniverseUUID(), nodeName, formData);
    checkAndWaitForTask(result);
  }

  private TaskInfo checkAndWaitForTask(Result result) throws InterruptedException {
    return checkAndWaitForTask(result, TaskInfo.State.Success);
  }

  private TaskInfo checkAndWaitForTask(Result result, TaskInfo.State state)
      throws InterruptedException {
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    TaskInfo taskInfo = waitForTask(UUID.fromString(json.get("taskUUID").asText()), 500);
    assertEquals(state, taskInfo.getTaskState());
    return taskInfo;
  }
}
