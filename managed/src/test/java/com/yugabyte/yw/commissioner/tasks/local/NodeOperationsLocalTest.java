// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.local;

import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static org.junit.Assert.assertEquals;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.commissioner.tasks.CommissionerBaseTest;
import com.yugabyte.yw.common.FakeApiHelper;
import com.yugabyte.yw.common.NodeActionType;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.NodeActionFormData;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.junit.Test;
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

  @Test
  public void testStopStartNodeInUniverse() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    Universe universe = createUniverse(userIntent);
    NodeDetails nodeDetails = universe.getUniverseDetails().nodeDetailsSet.iterator().next();
    String nodeName = nodeDetails.nodeName;

    NodeActionFormData formData = new NodeActionFormData();
    formData.nodeAction = NodeActionType.STOP;
    Result result = nodeOperationInUniverse(universe.getUniverseUUID(), nodeName, formData);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    TaskInfo taskInfo =
        CommissionerBaseTest.waitForTask(UUID.fromString(json.get("taskUUID").asText()));
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
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
    assertOk(result);
    json = Json.parse(contentAsString(result));
    taskInfo = CommissionerBaseTest.waitForTask(UUID.fromString(json.get("taskUUID").asText()));
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
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
    Result result = nodeOperationInUniverse(universe.getUniverseUUID(), nodeName, formData);
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    TaskInfo taskInfo =
        CommissionerBaseTest.waitForTask(UUID.fromString(json.get("taskUUID").asText()));

    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
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
    assertOk(result);
    JsonNode json = Json.parse(contentAsString(result));
    TaskInfo taskInfo =
        CommissionerBaseTest.waitForTask(UUID.fromString(json.get("taskUUID").asText()), 500);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
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
    assertOk(result);
    json = Json.parse(contentAsString(result));
    taskInfo = CommissionerBaseTest.waitForTask(UUID.fromString(json.get("taskUUID").asText()));
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
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
    assertOk(result);
    json = Json.parse(contentAsString(result));
    taskInfo = CommissionerBaseTest.waitForTask(UUID.fromString(json.get("taskUUID").asText()));
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());

    for (NodeDetails details : universe.getUniverseDetails().nodeDetailsSet) {
      assertEquals(NodeDetails.NodeState.Live, details.state);
    }
    verifyUniverseState(universe);
  }
}
