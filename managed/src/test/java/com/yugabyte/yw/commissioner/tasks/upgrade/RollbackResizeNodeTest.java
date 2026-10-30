// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import static com.yugabyte.yw.models.TaskInfo.State.Aborted;
import static com.yugabyte.yw.models.TaskInfo.State.Failure;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.hamcrest.CoreMatchers.containsString;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.yugabyte.yw.cloud.CloudAPI;
import com.yugabyte.yw.cloud.PublicCloudConstants;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.tasks.CommissionerBaseTest;
import com.yugabyte.yw.common.DeltaEvaluator;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.forms.ResizeNodeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeOption;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.StateTransitionDetails;
import com.yugabyte.yw.models.helpers.TaskType;
import java.time.Duration;
import java.time.Instant;
import java.util.Collections;
import java.util.Date;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import org.apache.commons.lang3.time.DateUtils;
import org.junit.Before;
import org.junit.Test;
import org.yb.client.ListMasterRaftPeersResponse;
import play.libs.Json;

/**
 * Commissioner-level tests for {@link RollbackResizeNode}. Computer/API overlay coverage lives in
 * {@code ResizeNodeRollbackComputerTest}; this class runs the task against a universe that looks
 * like a failed ResizeNode (delta captured, instance/disk already mutated).
 */
public class RollbackResizeNodeTest extends UpgradeTaskTest {

  private static final String DEFAULT_INSTANCE_TYPE = "c3.medium";
  private static final String NEW_INSTANCE_TYPE = "c4.medium";
  private static final int DEFAULT_VOLUME_SIZE = 100;
  private static final int NEW_VOLUME_SIZE = 200;
  private static final int DEFAULT_DISK_IOPS = 3000;
  private static final int NEW_DISK_IOPS = 5000;
  private static final int DEFAULT_DISK_THROUGHPUT = 125;
  private static final int NEW_DISK_THROUGHPUT = 250;

  private UUID failedResizeTaskUuid;

  @Override
  @Before
  public void setUp() {
    super.setUp();
    factory.globalRuntimeConf().setValue("yb.checks.change_master_config.enabled", "false");
    factory.globalRuntimeConf().setValue("yb.internal.allow_unsupported_instances", "true");
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            universe -> {
              UserIntent userIntent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
              userIntent.deviceInfo = new DeviceInfo();
              userIntent.deviceInfo.numVolumes = 1;
              userIntent.deviceInfo.volumeSize = DEFAULT_VOLUME_SIZE;
              userIntent.deviceInfo.diskIops = DEFAULT_DISK_IOPS;
              userIntent.deviceInfo.throughput = DEFAULT_DISK_THROUGHPUT;
              userIntent.deviceInfo.storageType = PublicCloudConstants.StorageType.GP3;
              userIntent.instanceType = DEFAULT_INSTANCE_TYPE;
              userIntent.provider = defaultProvider.getUuid().toString();
              universe
                  .getNodes()
                  .forEach(node -> node.cloudInfo.instance_type = DEFAULT_INSTANCE_TYPE);
            });
    try {
      when(mockYBClient.getClientWithConfig(any())).thenReturn(mockClient);
      ListMasterRaftPeersResponse listMastersResponse = mock(ListMasterRaftPeersResponse.class);
      when(listMastersResponse.getPeersList()).thenReturn(Collections.emptyList());
      when(mockClient.listMasterRaftPeers()).thenReturn(listMastersResponse);
      setCheckNodesAreSafeToTakeDown(mockClient);
    } catch (Exception ignored) {
    }
    createInstanceType(defaultProvider.getUuid(), DEFAULT_INSTANCE_TYPE);
    createInstanceType(defaultProvider.getUuid(), NEW_INSTANCE_TYPE);
    setUnderReplicatedTabletsMock();
    setFollowerLagMock();
    setLeaderlessTabletsMock();
    // Precheck always describes AWS/GCP/Azure. Default: cloud instance still at the failed
    // target, IOPS already at before (no invented Disk_Update), modify outside the AWS window.
    stubCloudState(
        NEW_INSTANCE_TYPE,
        DEFAULT_DISK_IOPS,
        DEFAULT_DISK_THROUGHPUT,
        DEFAULT_VOLUME_SIZE,
        Instant.now().minus(Duration.ofHours(48)));
  }

  private void stubCloudState(
      String instanceType,
      Integer iops,
      Integer throughput,
      Integer volumeSizeGb,
      Instant lastModificationStart) {
    lenient()
        .when(cloudAPI.describeNodeDataDiskSpec(any(), any()))
        .thenReturn(
            Optional.of(
                new CloudAPI.NodeDiskSpec(
                    instanceType, iops, throughput, volumeSizeGb, lastModificationStart)));
  }

  @Test
  public void testRollbackResizeNodeNotRollbackable() {
    assertFalse(Commissioner.canTaskTypeRollback(TaskType.RollbackResizeNode));
    assertTrue(Commissioner.canTaskTypeRollback(TaskType.ResizeNode));
  }

  @Test
  public void testRollbackReversesInstanceAndKeepsVolumeSize() {
    seedFailedResize(/* changeInstance */ true, /* growVolume */ true, /* changeIops */ false);
    ResizeNodeParams rollbackParams = createRollbackParams();
    TaskInfo taskInfo = submitRollback(rollbackParams);
    assertEquals(Success, taskInfo.getTaskState());

    List<TaskType> types =
        taskInfo.getSubTasks().stream().map(TaskInfo::getTaskType).collect(Collectors.toList());
    assertTrue(types.contains(TaskType.ChangeInstanceType));
    assertTrue(types.contains(TaskType.PersistResizeNode));
    assertFalse(types.contains(TaskType.RestoreUniverseDetailsFromDelta));

    Universe universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    UserIntent intent = universe.getUniverseDetails().getPrimaryCluster().userIntent;
    assertEquals(DEFAULT_INSTANCE_TYPE, intent.instanceType);
    assertEquals(NEW_VOLUME_SIZE, intent.deviceInfo.volumeSize.intValue());
    for (NodeDetails node : universe.getNodes()) {
      assertEquals(DEFAULT_INSTANCE_TYPE, node.cloudInfo.instance_type);
      assertEquals(NodeDetails.NodeState.Live, node.state);
    }
    assertNull(universe.getStateTransitionDetails());
    assertNull(universe.getUniverseDetails().placementModificationTaskUuid);
  }

  @Test
  public void testRollbackRevertsSpecificGFlagsWithoutVolumeChange() {
    SpecificGFlags beforeFlags =
        SpecificGFlags.construct(Map.of("master-before", "1"), Map.of("tserver-before", "2"));
    SpecificGFlags targetFlags =
        SpecificGFlags.construct(
            Map.of("master-before", "5", "added-master", "x"), Map.of("added-tserver", "y"));
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            u ->
                u.getUniverseDetails().getPrimaryCluster().userIntent.specificGFlags = beforeFlags);

    seedFailedResize(/* changeInstance */ true, /* growVolume */ false, /* changeIops */ false);
    persistFailedResizeForward(NEW_INSTANCE_TYPE, targetFlags);

    ResizeNodeParams rollbackParams = createRollbackParams();
    rollbackParams.getPrimaryCluster().userIntent.specificGFlags = beforeFlags;
    rollbackParams.masterGFlags = new HashMap<>();
    rollbackParams.tserverGFlags = new HashMap<>();
    TaskInfo taskInfo = submitRollback(rollbackParams);
    assertEquals(Success, taskInfo.getTaskState());

    List<TaskInfo> confUpdates = gflagConfSubtasks(taskInfo);
    assertFalse(confUpdates.isEmpty());
    Set<String> confNodes =
        confUpdates.stream()
            .map(t -> t.getTaskParams().get("nodeName").asText())
            .collect(Collectors.toSet());
    Set<String> universeNodes =
        Universe.getOrBadRequest(defaultUniverse.getUniverseUUID()).getNodes().stream()
            .map(n -> n.nodeName)
            .collect(Collectors.toSet());
    assertEquals(universeNodes, confNodes);
    assertGFlagRevertConfTasks(confUpdates);

    Universe universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertEquals(
        beforeFlags, universe.getUniverseDetails().getPrimaryCluster().userIntent.specificGFlags);
  }

  @Test
  public void testRollbackInstanceOnlyHasNoGFlagsConfUpdates() {
    seedFailedResize(/* changeInstance */ true, /* growVolume */ false, /* changeIops */ false);
    persistFailedResizeForward(NEW_INSTANCE_TYPE, null);
    ResizeNodeParams rollbackParams = createRollbackParams();
    rollbackParams.masterGFlags = new HashMap<>();
    rollbackParams.tserverGFlags = new HashMap<>();
    TaskInfo taskInfo = submitRollback(rollbackParams);
    assertEquals(Success, taskInfo.getTaskState());
    assertEquals(0, gflagConfSubtasks(taskInfo).size());
  }

  @Test
  public void testRollbackRevertsGFlagsOnNodeResizeNeverReached() {
    SpecificGFlags beforeFlags =
        SpecificGFlags.construct(Map.of("master-before", "1"), Map.of("tserver-before", "2"));
    SpecificGFlags targetFlags =
        SpecificGFlags.construct(
            Map.of("master-before", "5", "added-master", "x"), Map.of("added-tserver", "y"));
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            u ->
                u.getUniverseDetails().getPrimaryCluster().userIntent.specificGFlags = beforeFlags);

    seedFailedResize(/* changeInstance */ true, /* growVolume */ false, /* changeIops */ false);
    persistFailedResizeForward(NEW_INSTANCE_TYPE, targetFlags);
    String alreadyBefore =
        Universe.getOrBadRequest(defaultUniverse.getUniverseUUID()).getNodes().stream()
            .findFirst()
            .orElseThrow()
            .nodeName;
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            u -> {
              NodeDetails node = u.getNode(alreadyBefore);
              node.cloudInfo.instance_type = DEFAULT_INSTANCE_TYPE;
            });
    lenient()
        .when(cloudAPI.describeNodeDataDiskSpec(any(), any()))
        .thenAnswer(
            invocation -> {
              NodeDetails node = invocation.getArgument(1);
              return Optional.of(
                  new CloudAPI.NodeDiskSpec(
                      node.cloudInfo.instance_type,
                      DEFAULT_DISK_IOPS,
                      DEFAULT_DISK_THROUGHPUT,
                      DEFAULT_VOLUME_SIZE,
                      Instant.now().minus(Duration.ofHours(48))));
            });

    ResizeNodeParams rollbackParams = createRollbackParams();
    rollbackParams.getPrimaryCluster().userIntent.specificGFlags = beforeFlags;
    rollbackParams.masterGFlags = new HashMap<>();
    rollbackParams.tserverGFlags = new HashMap<>();
    TaskInfo taskInfo = submitRollback(rollbackParams);
    assertEquals(Success, taskInfo.getTaskState());

    long changeOnUnreached =
        taskInfo.getSubTasks().stream()
            .filter(t -> t.getTaskType() == TaskType.ChangeInstanceType)
            .filter(t -> alreadyBefore.equals(t.getTaskParams().get("nodeName").asText()))
            .count();
    assertEquals(0, changeOnUnreached);
    List<TaskInfo> gflagsOnUnreached =
        gflagConfSubtasks(taskInfo).stream()
            .filter(t -> alreadyBefore.equals(t.getTaskParams().get("nodeName").asText()))
            .collect(Collectors.toList());
    assertGFlagRevertConfTasks(gflagsOnUnreached);
  }

  @Test
  public void testRollbackSkipsNodesAlreadyAtBeforeInstanceType() {
    seedFailedResize(/* changeInstance */ true, /* growVolume */ false, /* changeIops */ false);
    String alreadyBefore =
        Universe.getOrBadRequest(defaultUniverse.getUniverseUUID()).getNodes().stream()
            .findFirst()
            .orElseThrow()
            .nodeName;
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            u -> {
              NodeDetails node = u.getNode(alreadyBefore);
              node.cloudInfo.instance_type = DEFAULT_INSTANCE_TYPE;
            });
    // Cloud mirrors YBA instance types so the already-before node is not re-driven by persist-abort
    // logic.
    lenient()
        .when(cloudAPI.describeNodeDataDiskSpec(any(), any()))
        .thenAnswer(
            invocation -> {
              NodeDetails node = invocation.getArgument(1);
              return Optional.of(
                  new CloudAPI.NodeDiskSpec(
                      node.cloudInfo.instance_type,
                      DEFAULT_DISK_IOPS,
                      DEFAULT_DISK_THROUGHPUT,
                      DEFAULT_VOLUME_SIZE,
                      Instant.now().minus(Duration.ofHours(48))));
            });

    TaskInfo taskInfo = submitRollback(createRollbackParams());
    assertEquals(Success, taskInfo.getTaskState());

    long changeInstanceCount =
        taskInfo.getSubTasks().stream()
            .filter(t -> t.getTaskType() == TaskType.ChangeInstanceType)
            .count();
    int totalNodes = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID()).getNodes().size();
    assertEquals(totalNodes - 1, changeInstanceCount);

    Universe universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    for (NodeDetails node : universe.getNodes()) {
      assertEquals(DEFAULT_INSTANCE_TYPE, node.cloudInfo.instance_type);
    }
  }

  @Test
  public void testRollbackRejectedWhenDeltaMissing() {
    ResizeNodeParams rollbackParams = createRollbackParams();
    PlatformServiceException ex =
        assertThrows(PlatformServiceException.class, () -> submitRollback(rollbackParams));
    assertThat(ex.getMessage(), containsString("state_transition_details"));
  }

  @Test
  public void testRollbackRejectedDuringIopsCooldown() {
    factory.globalRuntimeConf().setValue("yb.aws.disk_resize_cooldown_hours", "6");
    seedFailedResize(/* changeInstance */ true, /* growVolume */ false, /* changeIops */ true);
    Date oneHourAgo = DateUtils.addHours(new Date(), -1);
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            u -> u.getNodes().forEach(n -> n.lastVolumeUpdateTime = oneHourAgo));
    when(cloudAPI.describeNodeDataDiskSpec(any(), any()))
        .thenReturn(
            Optional.of(
                new CloudAPI.NodeDiskSpec(
                    NEW_INSTANCE_TYPE,
                    NEW_DISK_IOPS,
                    NEW_DISK_THROUGHPUT,
                    DEFAULT_VOLUME_SIZE,
                    Instant.now().minus(Duration.ofHours(1)))));

    ResizeNodeParams rollbackParams = createRollbackParams();
    rollbackParams.getPrimaryCluster().userIntent.deviceInfo.diskIops = DEFAULT_DISK_IOPS;
    rollbackParams.getPrimaryCluster().userIntent.deviceInfo.throughput = DEFAULT_DISK_THROUGHPUT;
    TaskInfo taskInfo = submitRollback(rollbackParams);
    assertEquals(Failure, taskInfo.getTaskState());
    assertThat(taskInfo.getErrorMessage(), containsString("cooldown"));
  }

  @Test
  public void testRollbackAllowsInstanceOnlyDuringCooldown() {
    factory.globalRuntimeConf().setValue("yb.aws.disk_resize_cooldown_hours", "6");
    seedFailedResize(/* changeInstance */ true, /* growVolume */ false, /* changeIops */ false);
    Date oneHourAgo = DateUtils.addHours(new Date(), -1);
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            u -> u.getNodes().forEach(n -> n.lastVolumeUpdateTime = oneHourAgo));
    // Cloud IOPS already at before so always-describe does not invent a disk-modify cooldown.
    stubCloudState(
        NEW_INSTANCE_TYPE,
        DEFAULT_DISK_IOPS,
        DEFAULT_DISK_THROUGHPUT,
        DEFAULT_VOLUME_SIZE,
        Instant.now().minus(Duration.ofHours(1)));

    TaskInfo taskInfo = submitRollback(createRollbackParams());
    assertEquals(Success, taskInfo.getTaskState());
    Universe universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertEquals(
        DEFAULT_INSTANCE_TYPE,
        universe.getUniverseDetails().getPrimaryCluster().userIntent.instanceType);
  }

  @Test
  public void testRollbackResizeNodeRetries() {
    seedFailedResize(/* changeInstance */ true, /* growVolume */ false, /* changeIops */ false);
    ResizeNodeParams rollbackParams = createRollbackParams();
    rollbackParams.expectedUniverseVersion = -1;
    rollbackParams.creatingUser = defaultUser;
    TestUtils.setFakeHttpContext(defaultUser);
    super.verifyTaskRetries(
        defaultCustomer,
        CustomerTask.TaskType.RollbackResizeNode,
        CustomerTask.TargetType.Universe,
        defaultUniverse.getUniverseUUID(),
        TaskType.RollbackResizeNode,
        rollbackParams,
        false);
    checkUniverseNodesStates(rollbackParams.getUniverseUUID());
    Universe universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertEquals(
        DEFAULT_INSTANCE_TYPE,
        universe.getUniverseDetails().getPrimaryCluster().userIntent.instanceType);
  }

  @Test
  public void testRollbackResizeNodeRetryLastNodeState() throws InterruptedException {
    seedFailedResize(/* changeInstance */ true, /* growVolume */ false, /* changeIops */ false);
    ResizeNodeParams rollbackParams = createRollbackParams();
    rollbackParams.expectedUniverseVersion = -1;
    rollbackParams.creatingUser = defaultUser;
    rollbackParams.sleepAfterMasterRestartMillis = 5;
    rollbackParams.sleepAfterTServerRestartMillis = 5;
    TestUtils.setFakeHttpContext(defaultUser);
    setPausePosition(5);
    UUID taskUUID = commissioner.submit(TaskType.RollbackResizeNode, rollbackParams);
    CustomerTask.create(
        defaultCustomer,
        defaultUniverse.getUniverseUUID(),
        taskUUID,
        CustomerTask.TargetType.Universe,
        CustomerTask.TaskType.RollbackResizeNode,
        "fake-name");
    TaskInfo taskInfo = TaskInfo.getOrBadRequest(taskUUID);
    CommissionerBaseTest.waitForTaskPaused(taskInfo.getUuid(), commissioner);
    taskInfo = TaskInfo.getOrBadRequest(taskInfo.getUuid());
    int i = 0;
    int lastNodeUpdatePosition = 0;
    for (TaskInfo subTask : taskInfo.getSubTasks()) {
      if (subTask.getTaskType() == TaskType.ChangeInstanceType) {
        lastNodeUpdatePosition = i;
      }
      i++;
    }
    setAbortPosition(lastNodeUpdatePosition);
    commissioner.resumeTask(taskInfo.getUuid());
    taskInfo = waitForTask(taskInfo.getUuid());
    assertEquals(Aborted, taskInfo.getTaskState());
    clearAbortOrPausePositions();

    CustomerTask retryTask =
        customerTaskManager.retryCustomerTask(defaultCustomer.getUuid(), taskInfo.getUuid());
    taskInfo = waitForTask(retryTask.getTaskUUID());
    assertEquals(Success, taskInfo.getTaskState());
    Universe universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    for (NodeDetails node : universe.getNodes()) {
      assertEquals(NodeDetails.NodeState.Live, node.state);
      assertEquals(DEFAULT_INSTANCE_TYPE, node.cloudInfo.instance_type);
    }
    assertNull(universe.getStateTransitionDetails());
  }

  @Test
  public void testRollbackRejectedWhenCheckpointCrossed() {
    seedFailedResize(/* changeInstance */ true, /* growVolume */ true, /* changeIops */ false);
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            u -> {
              StateTransitionDetails details = u.getStateTransitionDetails();
              details.setRollbackSafe(false);
              u.setStateTransitionDetails(details);
            });
    PlatformServiceException ex =
        assertThrows(PlatformServiceException.class, () -> submitRollback(createRollbackParams()));
    assertThat(ex.getMessage(), containsString("rollback checkpoint was crossed"));
  }

  @Test
  public void testRollbackRevertsInstanceWhenCloudChangedButYbaPersistAborted() {
    // ChangeInstanceType succeeded in cloud; UpdateUniverseFields never ran. YBA still at before.
    seedFailedResize(/* changeInstance */ false, /* growVolume */ false, /* changeIops */ false);
    // Delta still captured an instance change intent on freeze.
    UniverseDefinitionTaskParams before =
        Json.fromJson(
            Json.toJson(
                Universe.getOrBadRequest(defaultUniverse.getUniverseUUID()).getUniverseDetails()),
            UniverseDefinitionTaskParams.class);
    // Rebuild delta as if freeze saw an instance-type target while universe never persisted it.
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            u -> {
              UniverseDefinitionTaskParams target =
                  Json.fromJson(
                      Json.toJson(u.getUniverseDetails()), UniverseDefinitionTaskParams.class);
              target.getPrimaryCluster().userIntent.instanceType = NEW_INSTANCE_TYPE;
              JsonNode delta = DeltaEvaluator.buildDeltaJsonTree(before, target);
              u.setStateTransitionDetails(new StateTransitionDetails(true, delta));
            });
    stubCloudState(
        NEW_INSTANCE_TYPE,
        DEFAULT_DISK_IOPS,
        DEFAULT_DISK_THROUGHPUT,
        DEFAULT_VOLUME_SIZE,
        Instant.now().minus(Duration.ofHours(48)));

    TaskInfo taskInfo = submitRollback(createRollbackParams());
    assertEquals(Success, taskInfo.getTaskState());
    long changeInstanceCount =
        taskInfo.getSubTasks().stream()
            .filter(t -> t.getTaskType() == TaskType.ChangeInstanceType)
            .count();
    assertTrue(changeInstanceCount > 0);
  }

  @Test
  public void testRollbackIssuesDiskUpdateWhenCloudIopsDifferButYbaMatchesBefore() {
    // Disk_Update succeeded; PersistResizeNode aborted. YBA IOPS still at before.
    seedFailedResize(/* changeInstance */ false, /* growVolume */ false, /* changeIops */ false);
    UniverseDefinitionTaskParams before =
        Json.fromJson(
            Json.toJson(
                Universe.getOrBadRequest(defaultUniverse.getUniverseUUID()).getUniverseDetails()),
            UniverseDefinitionTaskParams.class);
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            u -> {
              UniverseDefinitionTaskParams target =
                  Json.fromJson(
                      Json.toJson(u.getUniverseDetails()), UniverseDefinitionTaskParams.class);
              target.getPrimaryCluster().userIntent.deviceInfo.diskIops = NEW_DISK_IOPS;
              target.getPrimaryCluster().userIntent.deviceInfo.throughput = NEW_DISK_THROUGHPUT;
              JsonNode delta = DeltaEvaluator.buildDeltaJsonTree(before, target);
              u.setStateTransitionDetails(new StateTransitionDetails(true, delta));
            });
    stubCloudState(
        DEFAULT_INSTANCE_TYPE,
        NEW_DISK_IOPS,
        NEW_DISK_THROUGHPUT,
        DEFAULT_VOLUME_SIZE,
        Instant.now().minus(Duration.ofHours(48)));

    ResizeNodeParams rollbackParams = createRollbackParams();
    rollbackParams.getPrimaryCluster().userIntent.deviceInfo.diskIops = DEFAULT_DISK_IOPS;
    rollbackParams.getPrimaryCluster().userIntent.deviceInfo.throughput = DEFAULT_DISK_THROUGHPUT;
    TaskInfo taskInfo = submitRollback(rollbackParams);
    assertEquals(Success, taskInfo.getTaskState());
    long diskUpdates =
        taskInfo.getSubTasks().stream()
            .filter(t -> t.getTaskType() == TaskType.InstanceActions)
            .filter(t -> "Disk_Update".equals(t.getTaskParams().path("type").asText()))
            .count();
    assertTrue(diskUpdates > 0);
  }

  /**
   * Snapshot current details as {@code before}, move the universe to the failed-resize target, and
   * write {@code state_transition_details} plus a failed ResizeNode {@code TaskInfo} as the
   * placement-modification owner.
   */
  private void seedFailedResize(boolean changeInstance, boolean growVolume, boolean changeIops) {
    UniverseDefinitionTaskParams before =
        Json.fromJson(
            Json.toJson(
                Universe.getOrBadRequest(defaultUniverse.getUniverseUUID()).getUniverseDetails()),
            UniverseDefinitionTaskParams.class);
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            u -> {
              UserIntent intent = u.getUniverseDetails().getPrimaryCluster().userIntent;
              if (changeInstance) {
                intent.instanceType = NEW_INSTANCE_TYPE;
                u.getNodes().forEach(n -> n.cloudInfo.instance_type = NEW_INSTANCE_TYPE);
              }
              if (growVolume) {
                intent.deviceInfo.volumeSize = NEW_VOLUME_SIZE;
              }
              if (changeIops) {
                intent.deviceInfo.diskIops = NEW_DISK_IOPS;
                intent.deviceInfo.throughput = NEW_DISK_THROUGHPUT;
              }
            });
    UniverseDefinitionTaskParams target =
        Json.fromJson(
            Json.toJson(
                Universe.getOrBadRequest(defaultUniverse.getUniverseUUID()).getUniverseDetails()),
            UniverseDefinitionTaskParams.class);
    JsonNode delta = DeltaEvaluator.buildDeltaJsonTree(before, target);
    failedResizeTaskUuid = persistFailedResizeTask();
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            u -> {
              u.setStateTransitionDetails(new StateTransitionDetails(true, delta));
              u.getUniverseDetails().placementModificationTaskUuid = failedResizeTaskUuid;
              u.getUniverseDetails().updateSucceeded = false;
              u.getUniverseDetails().updateInProgress = false;
            });
  }

  private UUID persistFailedResizeTask() {
    TaskInfo taskInfo = new TaskInfo(TaskType.ResizeNode, null);
    taskInfo.setTaskParams(Json.newObject());
    taskInfo.setOwner("");
    taskInfo.setYbaVersion(Util.getYbaVersion());
    taskInfo.setTaskState(TaskInfo.State.Failure);
    taskInfo.save();
    return taskInfo.getUuid();
  }

  private void persistFailedResizeForward(String instanceType, SpecificGFlags specificGFlags) {
    ResizeNodeParams forward = new ResizeNodeParams();
    forward.setUniverseUUID(defaultUniverse.getUniverseUUID());
    forward.upgradeOption = UpgradeOption.ROLLING_UPGRADE;
    forward.clusters =
        Json.fromJson(
                Json.toJson(
                    Universe.getOrBadRequest(defaultUniverse.getUniverseUUID())
                        .getUniverseDetails()),
                UniverseDefinitionTaskParams.class)
            .clusters;
    forward.getPrimaryCluster().userIntent.instanceType = instanceType;
    if (specificGFlags != null) {
      forward.getPrimaryCluster().userIntent.specificGFlags = specificGFlags;
    }
    forward.masterGFlags = new HashMap<>();
    forward.tserverGFlags = new HashMap<>();
    TaskInfo failedInfo = TaskInfo.getOrBadRequest(failedResizeTaskUuid);
    failedInfo.setTaskParams(Json.toJson(forward));
    failedInfo.save();
  }

  private List<TaskInfo> gflagConfSubtasks(TaskInfo taskInfo) {
    return taskInfo.getSubTasks().stream()
        .filter(t -> t.getTaskType() == TaskType.AnsibleConfigureServers)
        .filter(t -> "GFlags".equals(t.getTaskParams().path("type").asText()))
        .collect(Collectors.toList());
  }

  private void assertGFlagRevertConfTasks(List<TaskInfo> confUpdates) {
    assertFalse(confUpdates.isEmpty());
    for (TaskInfo conf : confUpdates) {
      @SuppressWarnings("unchecked")
      Map<String, String> gflags = Json.fromJson(conf.getTaskParams().get("gflags"), Map.class);
      String process = conf.getTaskParams().path("properties").path("processType").asText();
      ArrayNode remove = (ArrayNode) conf.getTaskParams().get("gflagsToRemove");
      assertTrue(remove != null && remove.size() > 0);
      if ("MASTER".equals(process)) {
        assertEquals("1", gflags.get("master-before"));
        assertTrue(remove.toString().contains("added-master"));
      } else {
        assertEquals("TSERVER", process);
        assertEquals("2", gflags.get("tserver-before"));
        assertTrue(remove.toString().contains("added-tserver"));
      }
    }
  }

  private ResizeNodeParams createRollbackParams() {
    Universe universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    ResizeNodeParams params = new ResizeNodeParams();
    params.setUniverseUUID(universe.getUniverseUUID());
    params.upgradeOption = UpgradeOption.ROLLING_UPGRADE;
    params.clusters =
        Json.fromJson(
                Json.toJson(universe.getUniverseDetails()), UniverseDefinitionTaskParams.class)
            .clusters;
    UserIntent intent = params.getPrimaryCluster().userIntent;
    intent.instanceType = DEFAULT_INSTANCE_TYPE;
    params.setOriginalTaskUUID(failedResizeTaskUuid);
    return params;
  }

  private TaskInfo submitRollback(ResizeNodeParams requestParams) {
    factory.globalRuntimeConf().setValue("yb.checks.change_master_config.enabled", "false");
    return submitTask(requestParams, TaskType.RollbackResizeNode, commissioner, -1);
  }

  private void createInstanceType(UUID providerId, String type) {
    InstanceType.InstanceTypeDetails instanceTypeDetails = new InstanceType.InstanceTypeDetails();
    InstanceType.VolumeDetails volumeDetails = new InstanceType.VolumeDetails();
    volumeDetails.volumeType = InstanceType.VolumeType.SSD;
    volumeDetails.volumeSizeGB = 100;
    volumeDetails.mountPath = "/";
    instanceTypeDetails.volumeDetailsList = Collections.singletonList(volumeDetails);
    InstanceType.upsert(providerId, type, 1, 100d, instanceTypeDetails);
  }
}
