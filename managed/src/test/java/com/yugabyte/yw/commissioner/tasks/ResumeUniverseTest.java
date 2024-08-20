// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.common.ApiUtils.getTestUserIntent;
import static com.yugabyte.yw.common.AssertHelper.assertJsonEqual;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.hamcrest.CoreMatchers.containsString;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyLong;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.google.common.net.HostAndPort;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.kms.util.EncryptionAtRestUtil;
import com.yugabyte.yw.common.kms.util.KeyProvider;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.KmsConfig;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicReference;
import java.util.stream.Collectors;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.client.IsServerReadyResponse;
import org.yb.client.YBClient;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class ResumeUniverseTest extends CommissionerBaseTest {

  private Universe defaultUniverse;
  private EncryptionAtRestUtil encryptionUtil;
  private KmsConfig testKMSConfig;
  private int expectedUniverseVersion = 2;

  @Override
  @Before
  public void setUp() {
    super.setUp();
    YBClient mockClient = mock(YBClient.class);
    when(mockYBClient.getClient(any(), any())).thenReturn(mockClient);
    when(mockClient.waitForServer(any(), anyLong())).thenReturn(true);
    try {
      when(mockClient.waitForMaster(any(), anyLong())).thenReturn(true);
      mockClockSyncResponse(mockNodeUniverseManager);
      IsServerReadyResponse okReadyResp = new IsServerReadyResponse(0, "", null, 0, 0);
      when(mockClient.isServerReady(any(HostAndPort.class), anyBoolean())).thenReturn(okReadyResp);
    } catch (Exception e) {
      fail();
    }
    ShellResponse dummyShellResponse = new ShellResponse();
    dummyShellResponse.message = "true";
    when(mockNodeManager.nodeCommand(any(), any())).thenReturn(dummyShellResponse);
    testKMSConfig =
        KmsConfig.createKMSConfig(
            defaultCustomer.getUuid(),
            KeyProvider.AWS,
            Json.newObject().put("test_key", "test_val"),
            "some config name");
  }

  private void setupUniverse(boolean updateInProgress, int numOfNodes) {
    Region r = Region.create(defaultProvider, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    InstanceType i =
        InstanceType.upsert(
            defaultProvider.getUuid(),
            "c3.xlarge",
            10,
            5.5,
            new InstanceType.InstanceTypeDetails());
    UniverseDefinitionTaskParams.UserIntent userIntent =
        getTestUserIntent(r, defaultProvider, i, numOfNodes);
    userIntent.replicationFactor = numOfNodes;
    userIntent.masterGFlags = new HashMap<>();
    userIntent.tserverGFlags = new HashMap<>();
    userIntent.universeName = "demo-universe";

    defaultUniverse = createUniverse(defaultCustomer.getId());
    String nodePrefix = "demo-universe";
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(),
        ApiUtils.mockUniverseUpdater(
            userIntent, nodePrefix, true /* setMasters */, updateInProgress));
  }

  private static final List<TaskType> RESUME_UNIVERSE_TASKS =
      ImmutableList.of(
          TaskType.FreezeUniverse,
          TaskType.UpdateConsistencyCheck,
          TaskType.ResumeServer,
          TaskType.WaitForClockSync, // Ensure clock skew is low enough
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.WaitForClockSync, // Ensure clock skew is low enough
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.SetNodeState,
          TaskType.ManageAlertDefinitions,
          TaskType.SwamperTargetsFileUpdate,
          TaskType.UniverseUpdateSucceeded);

  private static final List<TaskType> RESUME_ENCRYPTION_AT_REST_UNIVERSE_TASKS =
      ImmutableList.of(
          TaskType.FreezeUniverse,
          TaskType.UpdateConsistencyCheck,
          TaskType.ResumeServer,
          TaskType.WaitForClockSync, // Ensure clock skew is low enough
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForServer,
          TaskType.SetActiveUniverseKeys,
          TaskType.WaitForServerReady,
          TaskType.WaitForClockSync, // Ensure clock skew is low enough
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForServer,
          TaskType.WaitForServerReady,
          TaskType.SetNodeState,
          TaskType.ManageAlertDefinitions,
          TaskType.SwamperTargetsFileUpdate,
          TaskType.UniverseUpdateSucceeded);

  private static final List<JsonNode> RESUME_UNIVERSE_EXPECTED_RESULTS =
      ImmutableList.of(
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("process", "master", "command", "start")),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("process", "tserver", "command", "start")),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()));

  private static final List<JsonNode> RESUME_ENCRYPTION_AT_REST_UNIVERSE_EXPECTED_RESULTS =
      ImmutableList.of(
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("process", "master", "command", "start")),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("process", "tserver", "command", "start")),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()));

  private void assertTaskSequence(
      Map<Integer, List<TaskInfo>> subTasksByPosition,
      List<TaskType> expectedTaskSequence,
      List<JsonNode> expectedResultsList) {
    int position = 0;
    for (TaskType taskType : expectedTaskSequence) {
      JsonNode expectedResults = expectedResultsList.get(position);
      List<TaskInfo> tasks = subTasksByPosition.get(position);
      assertEquals(taskType, tasks.get(0).getTaskType());
      List<JsonNode> taskDetails =
          tasks.stream().map(TaskInfo::getTaskParams).collect(Collectors.toList());
      assertJsonEqual(expectedResults, taskDetails.get(0));
      position++;
    }
  }

  private TaskInfo submitTask(ResumeUniverse.Params taskParams) {
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = expectedUniverseVersion;
    try {
      UUID taskUUID = commissioner.submit(TaskType.ResumeUniverse, taskParams);
      return waitForTask(taskUUID);
    } catch (InterruptedException e) {
      assertNull(e.getMessage());
    }
    return null;
  }

  @Test
  public void testResumeUniverseSuccess() {
    setupUniverse(false, 1);
    ResumeUniverse.Params taskParams = new ResumeUniverse.Params();
    taskParams.customerUUID = defaultCustomer.getUuid();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    TaskInfo taskInfo = submitTask(taskParams);
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertTaskSequence(subTasksByPosition, RESUME_UNIVERSE_TASKS, RESUME_UNIVERSE_EXPECTED_RESULTS);
    assertEquals(Success, taskInfo.getTaskState());
    assertTrue(defaultCustomer.getUniverseUUIDs().contains(defaultUniverse.getUniverseUUID()));
  }

  @Test
  public void testResumeNodeStates() {
    setupUniverse(false, 3);

    AtomicReference<String> nodeWithStoppedProcesses = new AtomicReference<>();
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(),
        universe -> {
          for (NodeDetails node : universe.getNodes()) {
            if (nodeWithStoppedProcesses.get() == null) {
              node.isMaster = false;
              node.isTserver = false;
              nodeWithStoppedProcesses.set(node.getNodeName());
            }
            node.state = NodeDetails.NodeState.Stopped;
          }
        });
    expectedUniverseVersion++;
    ResumeUniverse.Params taskParams = new ResumeUniverse.Params();
    taskParams.customerUUID = defaultCustomer.getUuid();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    Universe universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    Map<NodeDetails.NodeState, List<NodeDetails>> byStates =
        universe.getNodes().stream().collect(Collectors.groupingBy(node -> node.state));
    assertEquals(1, byStates.get(NodeDetails.NodeState.Stopped).size());
    assertEquals(2, byStates.get(NodeDetails.NodeState.Live).size());

    String stoppedNodeName = byStates.get(NodeDetails.NodeState.Stopped).get(0).nodeName;
    assertEquals(nodeWithStoppedProcesses.get(), stoppedNodeName);
  }

  @Test
  public void testResumeUniverseWithUpdateInProgress() {
    setupUniverse(true, 1);
    ResumeUniverse.Params taskParams = new ResumeUniverse.Params();
    taskParams.customerUUID = defaultCustomer.getUuid();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    PlatformServiceException thrown =
        assertThrows(PlatformServiceException.class, () -> submitTask(taskParams));
    assertThat(thrown.getMessage(), containsString("is already being updated"));
  }

  @Test
  public void testResumeUniverseWithEncyptionAtRestEnabled() {
    setupUniverse(false, 1);
    encryptionUtil.addKeyRef(
        defaultUniverse.getUniverseUUID(),
        testKMSConfig.getConfigUUID(),
        "some_key_ref".getBytes());
    int numRotations = encryptionUtil.getNumUniverseKeys(defaultUniverse.getUniverseUUID());
    assertEquals(1, numRotations);
    ResumeUniverse.Params taskParams = new ResumeUniverse.Params();
    taskParams.customerUUID = defaultCustomer.getUuid();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    TaskInfo taskInfo = submitTask(taskParams);
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertTaskSequence(
        subTasksByPosition,
        RESUME_ENCRYPTION_AT_REST_UNIVERSE_TASKS,
        RESUME_ENCRYPTION_AT_REST_UNIVERSE_EXPECTED_RESULTS);
    assertEquals(Success, taskInfo.getTaskState());
    assertTrue(defaultCustomer.getUniverseUUIDs().contains(defaultUniverse.getUniverseUUID()));
  }
}
