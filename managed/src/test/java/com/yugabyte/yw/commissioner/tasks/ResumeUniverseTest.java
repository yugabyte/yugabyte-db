// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static com.yugabyte.yw.common.ApiUtils.getTestUserIntent;
import static com.yugabyte.yw.common.AssertHelper.assertJsonEqual;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.hamcrest.CoreMatchers.containsString;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyLong;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
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
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.runners.MockitoJUnitRunner;
import org.yb.client.YBClient;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class ResumeUniverseTest extends CommissionerBaseTest {

  private Universe defaultUniverse;
  private EncryptionAtRestUtil encryptionUtil;
  private KmsConfig testKMSConfig;

  @Override
  @Before
  public void setUp() {
    super.setUp();
    YBClient mockClient = mock(YBClient.class);
    when(mockYBClient.getClient(any(), any())).thenReturn(mockClient);
    when(mockClient.waitForServer(any(), anyLong())).thenReturn(true);
    try {
      when(mockClient.waitForMaster(any(), anyLong())).thenReturn(true);
    } catch (Exception e) {
      fail();
    }
    ShellResponse dummyShellResponse = new ShellResponse();
    dummyShellResponse.message = "true";
    when(mockNodeManager.nodeCommand(any(), any())).thenReturn(dummyShellResponse);
    testKMSConfig =
        KmsConfig.createKMSConfig(
            defaultCustomer.uuid,
            KeyProvider.AWS,
            Json.newObject().put("test_key", "test_val"),
            "some config name");
  }

  private void setupUniverse(boolean updateInProgress) {
    Region r = Region.create(defaultProvider, "region-1", "PlacementRegion 1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ 1", "subnet-1");
    InstanceType i =
        InstanceType.upsert(
            defaultProvider.uuid, "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());
    UniverseDefinitionTaskParams.UserIntent userIntent =
        getTestUserIntent(r, defaultProvider, i, 1);
    userIntent.replicationFactor = 1;
    userIntent.masterGFlags = new HashMap<>();
    userIntent.tserverGFlags = new HashMap<>();
    userIntent.universeName = "demo-universe";

    defaultUniverse = createUniverse(defaultCustomer.getCustomerId());
    String nodePrefix = "demo-universe";
    Universe.saveDetails(
        defaultUniverse.universeUUID,
        ApiUtils.mockUniverseUpdater(
            userIntent, nodePrefix, true /* setMasters */, updateInProgress));
  }

  private static final List<TaskType> RESUME_UNIVERSE_TASKS =
      ImmutableList.of(
          TaskType.ResumeServer,
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForServer,
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForServer,
          TaskType.SwamperTargetsFileUpdate,
          TaskType.ManageAlertDefinitions,
          TaskType.UniverseUpdateSucceeded);

  private static final List<TaskType> RESUME_ENCRYPTION_AT_REST_UNIVERSE_TASKS =
      ImmutableList.of(
          TaskType.ResumeServer,
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForServer,
          TaskType.SetActiveUniverseKeys,
          TaskType.AnsibleClusterServerCtl,
          TaskType.WaitForServer,
          TaskType.SwamperTargetsFileUpdate,
          TaskType.ManageAlertDefinitions,
          TaskType.UniverseUpdateSucceeded);

  private static final List<JsonNode> RESUME_UNIVERSE_EXPECTED_RESULTS =
      ImmutableList.of(
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("process", "master", "command", "start")),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("process", "tserver", "command", "start")),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()));

  private static final List<JsonNode> RESUME_ENCRYPTION_AT_REST_UNIVERSE_EXPECTED_RESULTS =
      ImmutableList.of(
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("process", "master", "command", "start")),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of()),
          Json.toJson(ImmutableMap.of("process", "tserver", "command", "start")),
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
          tasks.stream().map(TaskInfo::getTaskDetails).collect(Collectors.toList());
      assertJsonEqual(expectedResults, taskDetails.get(0));
      position++;
    }
  }

  private TaskInfo submitTask(ResumeUniverse.Params taskParams) {
    taskParams.universeUUID = defaultUniverse.universeUUID;
    taskParams.expectedUniverseVersion = 2;
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
    setupUniverse(false);
    ResumeUniverse.Params taskParams = new ResumeUniverse.Params();
    taskParams.customerUUID = defaultCustomer.uuid;
    taskParams.universeUUID = defaultUniverse.universeUUID;
    TaskInfo taskInfo = submitTask(taskParams);
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertTaskSequence(subTasksByPosition, RESUME_UNIVERSE_TASKS, RESUME_UNIVERSE_EXPECTED_RESULTS);
    assertEquals(Success, taskInfo.getTaskState());
    assertFalse(defaultCustomer.getUniverseUUIDs().contains(defaultUniverse.universeUUID));
  }

  @Test
  public void testResumeUniverseWithUpdateInProgress() {
    setupUniverse(true);
    ResumeUniverse.Params taskParams = new ResumeUniverse.Params();
    taskParams.customerUUID = defaultCustomer.uuid;
    taskParams.universeUUID = defaultUniverse.universeUUID;
    PlatformServiceException thrown =
        assertThrows(PlatformServiceException.class, () -> submitTask(taskParams));
  }

  @Test
  public void testResumeUniverseWithEncyptionAtRestEnabled() {
    setupUniverse(false);
    encryptionUtil.addKeyRef(
        defaultUniverse.universeUUID, testKMSConfig.configUUID, "some_key_ref".getBytes());
    int numRotations =
        encryptionUtil.getNumKeyRotations(defaultUniverse.universeUUID, testKMSConfig.configUUID);
    assertEquals(1, numRotations);
    ResumeUniverse.Params taskParams = new ResumeUniverse.Params();
    taskParams.customerUUID = defaultCustomer.uuid;
    taskParams.universeUUID = defaultUniverse.universeUUID;
    TaskInfo taskInfo = submitTask(taskParams);
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertTaskSequence(
        subTasksByPosition,
        RESUME_ENCRYPTION_AT_REST_UNIVERSE_TASKS,
        RESUME_ENCRYPTION_AT_REST_UNIVERSE_EXPECTED_RESULTS);
    assertEquals(Success, taskInfo.getTaskState());
    assertFalse(defaultCustomer.getUniverseUUIDs().contains(defaultUniverse.universeUUID));
  }
}
