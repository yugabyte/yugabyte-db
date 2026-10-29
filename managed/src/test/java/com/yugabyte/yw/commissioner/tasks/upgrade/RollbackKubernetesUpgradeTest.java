// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.isNull;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.RegexMatcher;
import com.yugabyte.yw.common.TestHelper;
import com.yugabyte.yw.forms.RollbackUpgradeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.SoftwareUpgradeState;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.Map;
import java.util.UUID;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.mockito.ArgumentCaptor;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import play.libs.Json;

public class RollbackKubernetesUpgradeTest extends KubernetesUpgradeTaskTest {

  @Rule public MockitoRule rule = MockitoJUnit.rule();

  private RollbackKubernetesUpgrade rollbackKubernetesUpgrade;

  private TaskInfo submitTask(RollbackUpgradeParams taskParams) {
    return submitTask(taskParams, TaskType.RollbackKubernetesUpgrade, commissioner);
  }

  @Before
  public void setup() throws Exception {
    setFollowerLagMock();
    setUnderReplicatedTabletsMock();
    setCheckNodesAreSafeToTakeDown(mockClient);
    this.rollbackKubernetesUpgrade =
        new RollbackKubernetesUpgrade(
            mockBaseTaskDependencies, mockSoftwareUpgradeHelper, mockOperatorStatusUpdaterFactory);
    rollbackKubernetesUpgrade.setTaskUUID(UUID.randomUUID());
  }

  @Test
  public void testRollbackUpgradeRetries() {
    setupUniverseSingleAZ(false, true);
    UniverseDefinitionTaskParams.PrevYBSoftwareConfig ybSoftwareConfig =
        new UniverseDefinitionTaskParams.PrevYBSoftwareConfig();
    ybSoftwareConfig.setAutoFlagConfigVersion(1);
    ybSoftwareConfig.setSoftwareVersion("old-version");
    TestHelper.updateUniversePrevSoftwareConfig(defaultUniverse, ybSoftwareConfig);
    TestHelper.updateUniverseIsRollbackAllowed(defaultUniverse, true);
    TestHelper.updateUniverseVersion(defaultUniverse, "new-version");
    RollbackUpgradeParams taskParams = new RollbackUpgradeParams();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = -1;
    taskParams.sleepAfterMasterRestartMillis = 0;
    taskParams.sleepAfterTServerRestartMillis = 0;
    super.verifyTaskRetries(
        defaultCustomer,
        CustomerTask.TaskType.RollbackUpgrade,
        CustomerTask.TargetType.Universe,
        defaultUniverse.getUniverseUUID(),
        TaskType.RollbackKubernetesUpgrade,
        taskParams,
        false);
    checkUniverseNodesStates(taskParams.getUniverseUUID());
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertFalse(defaultUniverse.getUniverseDetails().isSoftwareRollbackAllowed);
    assertEquals(
        SoftwareUpgradeState.Ready, defaultUniverse.getUniverseDetails().softwareUpgradeState);
  }

  @Test
  public void testRollbackAbortedBeforeReadyFailsRollbackAndRetryCompletes() throws Exception {
    // Rollback saves the old version before the Ready state, so an abort between the two leaves
    // RollingBack, which the failure handler turns into RollbackFailed.
    setupUniverseSingleAZ(false, true);
    UniverseDefinitionTaskParams.PrevYBSoftwareConfig ybSoftwareConfig =
        new UniverseDefinitionTaskParams.PrevYBSoftwareConfig();
    ybSoftwareConfig.setAutoFlagConfigVersion(1);
    ybSoftwareConfig.setSoftwareVersion(YB_SOFTWARE_VERSION_OLD);
    ybSoftwareConfig.setTargetUpgradeSoftwareVersion(YB_SOFTWARE_VERSION_NEW);
    TestHelper.updateUniversePrevSoftwareConfig(defaultUniverse, ybSoftwareConfig);
    TestHelper.updateUniverseIsRollbackAllowed(defaultUniverse, true);
    TestHelper.updateUniverseVersion(defaultUniverse, YB_SOFTWARE_VERSION_NEW);
    TestHelper.updateUniverseSoftwareUpgradeState(
        defaultUniverse, SoftwareUpgradeState.PreFinalize);
    RollbackUpgradeParams taskParams = new RollbackUpgradeParams();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.nodePrefix = NODE_PREFIX;
    taskParams.expectedUniverseVersion = -1;
    taskParams.sleepAfterMasterRestartMillis = 0;
    taskParams.sleepAfterTServerRestartMillis = 0;

    UUID taskUuid;
    TaskInfo taskInfo;
    try {
      setPausePosition(0);
      taskUuid = commissioner.submit(TaskType.RollbackKubernetesUpgrade, taskParams);
      CustomerTask.create(
          defaultCustomer,
          taskParams.getUniverseUUID(),
          taskUuid,
          CustomerTask.TargetType.Universe,
          CustomerTask.TaskType.RollbackUpgrade,
          defaultUniverse.getName());
      waitForTaskPaused(taskUuid);
      // The rollback subtasks are created only after the freeze runs.
      setPausePosition(firstSubTaskPosition(taskUuid, TaskType.FreezeUniverse) + 1);
      commissioner.resumeTask(taskUuid);
      waitForTaskPaused(taskUuid);
      setAbortPosition(lastSubTaskPosition(taskUuid, TaskType.UpdateUniverseState));
      commissioner.resumeTask(taskUuid);
      taskInfo = waitForTask(taskUuid);
    } finally {
      clearAbortOrPausePositions();
    }

    assertEquals(TaskInfo.State.Aborted, taskInfo.getTaskState());
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertEquals(
        SoftwareUpgradeState.RollbackFailed,
        defaultUniverse.getUniverseDetails().softwareUpgradeState);
    assertEquals(
        YB_SOFTWARE_VERSION_OLD,
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion);

    CustomerTask retry = customerTaskManager.retryCustomerTask(defaultCustomer.getUuid(), taskUuid);
    taskInfo = waitForTask(retry.getTaskUUID());

    assertEquals(Success, taskInfo.getTaskState());
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertEquals(
        SoftwareUpgradeState.Ready, defaultUniverse.getUniverseDetails().softwareUpgradeState);
    assertFalse(defaultUniverse.getUniverseDetails().isSoftwareRollbackAllowed);
  }

  @Test
  public void testRollbackUpgradeSingleAZ() {
    setupUniverseSingleAZ(false, true);
    UniverseDefinitionTaskParams.PrevYBSoftwareConfig ybSoftwareConfig =
        new UniverseDefinitionTaskParams.PrevYBSoftwareConfig();
    ybSoftwareConfig.setAutoFlagConfigVersion(1);
    ybSoftwareConfig.setSoftwareVersion("old-version");
    TestHelper.updateUniversePrevSoftwareConfig(defaultUniverse, ybSoftwareConfig);
    TestHelper.updateUniverseIsRollbackAllowed(defaultUniverse, true);
    TestHelper.updateUniverseVersion(defaultUniverse, "new-version");

    ArgumentCaptor<String> expectedYbSoftwareVersion = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNamespace = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedPodName = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<Map<String, String>> expectedConfig = ArgumentCaptor.forClass(Map.class);
    ArgumentCaptor<UUID> expectedUniverseUUID = ArgumentCaptor.forClass(UUID.class);

    String overrideFileRegex = "(.*)" + defaultUniverse.getUniverseUUID() + "(.*).yml";

    RollbackUpgradeParams taskParams = new RollbackUpgradeParams();
    TaskInfo taskInfo = submitTask(taskParams);

    verify(mockKubernetesManager, times(9))
        .helmUpgrade(
            expectedUniverseUUID.capture(),
            expectedYbSoftwareVersion.capture(),
            expectedConfig.capture(),
            expectedNodePrefix.capture(),
            expectedNamespace.capture(),
            expectedOverrideFile.capture(),
            isNull());
    verify(mockKubernetesManager, times(9))
        .getPodObject(
            expectedConfig.capture(), expectedNodePrefix.capture(), expectedPodName.capture());
    verify(mockKubernetesManager, times(2))
        .getPodInfos(
            expectedConfig.capture(), expectedNodePrefix.capture(), expectedNamespace.capture());

    assertEquals("old-version", expectedYbSoftwareVersion.getValue());
    assertEquals(config, expectedConfig.getValue());
    assertEquals(NODE_PREFIX, expectedNodePrefix.getValue());
    assertEquals(NODE_PREFIX, expectedNamespace.getValue());
    assertThat(expectedOverrideFile.getValue(), RegexMatcher.matchesRegex(overrideFileRegex));

    assertEquals(Success, taskInfo.getTaskState());
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertFalse(defaultUniverse.getUniverseDetails().isSoftwareRollbackAllowed);
    assertNull(defaultUniverse.getUniverseDetails().prevYBSoftwareConfig);
    assertEquals(
        SoftwareUpgradeState.Ready, defaultUniverse.getUniverseDetails().softwareUpgradeState);
  }

  @Test
  public void testRollbackUpgradeMultiAZ() {
    setupUniverseMultiAZ(false, true);
    UniverseDefinitionTaskParams.PrevYBSoftwareConfig ybSoftwareConfig =
        new UniverseDefinitionTaskParams.PrevYBSoftwareConfig();
    ybSoftwareConfig.setAutoFlagConfigVersion(1);
    ybSoftwareConfig.setSoftwareVersion("old-version");
    TestHelper.updateUniversePrevSoftwareConfig(defaultUniverse, ybSoftwareConfig);
    TestHelper.updateUniverseIsRollbackAllowed(defaultUniverse, true);
    TestHelper.updateUniverseVersion(defaultUniverse, "new-version");

    ArgumentCaptor<String> expectedYbSoftwareVersion = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNamespace = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedPodName = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<Map<String, String>> expectedConfig = ArgumentCaptor.forClass(Map.class);
    ArgumentCaptor<UUID> expectedUniverseUUID = ArgumentCaptor.forClass(UUID.class);
    String overrideFileRegex = "(.*)" + defaultUniverse.getUniverseUUID() + "(.*).yml";

    RollbackUpgradeParams taskParams = new RollbackUpgradeParams();
    TaskInfo taskInfo = submitTask(taskParams);

    verify(mockKubernetesManager, times(9))
        .helmUpgrade(
            expectedUniverseUUID.capture(),
            expectedYbSoftwareVersion.capture(),
            expectedConfig.capture(),
            expectedNodePrefix.capture(),
            expectedNamespace.capture(),
            expectedOverrideFile.capture(),
            isNull());
    verify(mockKubernetesManager, times(9))
        .getPodObject(
            expectedConfig.capture(), expectedNodePrefix.capture(), expectedPodName.capture());
    verify(mockKubernetesManager, times(6))
        .getPodInfos(
            expectedConfig.capture(), expectedNodePrefix.capture(), expectedNamespace.capture());

    assertEquals("old-version", expectedYbSoftwareVersion.getValue());
    assertEquals(config, expectedConfig.getValue());
    assertTrue(expectedNodePrefix.getValue().contains(NODE_PREFIX));
    assertTrue(expectedNamespace.getValue().contains(NODE_PREFIX));
    assertThat(expectedOverrideFile.getValue(), RegexMatcher.matchesRegex(overrideFileRegex));

    assertEquals(Success, taskInfo.getTaskState());
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertFalse(defaultUniverse.getUniverseDetails().isSoftwareRollbackAllowed);
    assertNull(defaultUniverse.getUniverseDetails().prevYBSoftwareConfig);
    assertEquals(
        SoftwareUpgradeState.Ready, defaultUniverse.getUniverseDetails().softwareUpgradeState);
  }

  @Test
  public void testYsqlMajorRollbackTreatsReadReplicaAsReadReplica() throws Exception {
    // PLAT-23200: same compatibility-flag helm step as the upgrade, run on rollback.
    setupUniverseK8sSingleAZWithYSQL(true, true);
    Cluster readReplica = addReadReplicaCluster();
    UniverseDefinitionTaskParams.PrevYBSoftwareConfig ybSoftwareConfig =
        new UniverseDefinitionTaskParams.PrevYBSoftwareConfig();
    ybSoftwareConfig.setAutoFlagConfigVersion(1);
    ybSoftwareConfig.setSoftwareVersion(YB_SOFTWARE_VERSION_OLD);
    ybSoftwareConfig.setTargetUpgradeSoftwareVersion(YB_SOFTWARE_VERSION_NEW);
    ybSoftwareConfig.setAllTserversUpgradedToYsqlMajorVersion(true);
    TestHelper.updateUniversePrevSoftwareConfig(defaultUniverse, ybSoftwareConfig);
    TestHelper.updateUniverseIsRollbackAllowed(defaultUniverse, true);
    TestHelper.updateUniverseVersion(defaultUniverse, YB_SOFTWARE_VERSION_NEW);
    when(mockSoftwareUpgradeHelper.isYsqlMajorVersionUpgradeRequired(
            any(), anyString(), anyString()))
        .thenReturn(true);
    lenient()
        .when(mockClient.setFlag(any(), anyString(), anyString(), anyBoolean()))
        .thenReturn(true);
    lenient()
        .when(
            mockYsqlQueryExecutor.executeQueryInNodeShell(
                any(), any(), any(), anyBoolean(), anyBoolean()))
        .thenReturn(Json.newObject());

    RollbackUpgradeParams taskParams = new RollbackUpgradeParams();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.nodePrefix = NODE_PREFIX;
    // Adding the read replica bumped the universe version past what submitTask expects.
    taskParams.expectedUniverseVersion = -1;
    taskParams.sleepAfterMasterRestartMillis = 0;
    taskParams.sleepAfterTServerRestartMillis = 0;
    TaskInfo taskInfo =
        waitForTask(commissioner.submit(TaskType.RollbackKubernetesUpgrade, taskParams));

    assertEquals(taskInfo.getErrorMessage(), Success, taskInfo.getTaskState());
    assertReadReplicaCommandsTargetReadReplica(taskInfo, readReplica);
  }

  private RollbackUpgradeParams setupImmutableYbcRollback(
      String currentVersion, String prevVersion, SoftwareUpgradeState state) {
    setupUniverseSingleAZ(false, true);
    setImmutableYbcUniverseState(true, currentVersion, state);
    UniverseDefinitionTaskParams.PrevYBSoftwareConfig ybSoftwareConfig =
        new UniverseDefinitionTaskParams.PrevYBSoftwareConfig();
    ybSoftwareConfig.setSoftwareVersion(prevVersion);
    TestHelper.updateUniversePrevSoftwareConfig(defaultUniverse, ybSoftwareConfig);
    TestHelper.updateUniverseIsRollbackAllowed(defaultUniverse, true);
    return new RollbackUpgradeParams();
  }

  @Test
  public void testRollbackRejectedWhenPrevVersionLacksInbuiltYbc() {
    RollbackUpgradeParams taskParams =
        setupImmutableYbcRollback(
            "2025.2.0.0-b1", "2025.1.0.0-b1", SoftwareUpgradeState.PreFinalize);
    PlatformServiceException e =
        assertThrows(
            PlatformServiceException.class, () -> taskParams.verifyParams(defaultUniverse, true));
    assertTrue(e.getMessage(), e.getMessage().contains("does not support ybdb inbuilt ybc"));
    assertTrue(e.getMessage(), e.getMessage().contains("Disable ybdb inbuilt ybc"));
  }

  @Test
  public void testRollbackRejectedWhenFrozenDoesNotSuggestToggle() {
    // The toggle task is not admitted on a frozen universe, so the error must not suggest it.
    RollbackUpgradeParams taskParams =
        setupImmutableYbcRollback(
            "2025.2.0.0-b1", "2025.1.0.0-b1", SoftwareUpgradeState.RollbackFailed);
    PlatformServiceException e =
        assertThrows(
            PlatformServiceException.class, () -> taskParams.verifyParams(defaultUniverse, true));
    assertTrue(e.getMessage(), e.getMessage().contains("cannot be disabled"));
    assertFalse(e.getMessage(), e.getMessage().contains("Disable ybdb inbuilt ybc"));
  }

  @Test
  public void testRollbackAllowedWhenPrevVersionSupportsInbuiltYbc() {
    RollbackUpgradeParams taskParams =
        setupImmutableYbcRollback(
            "2025.2.1.0-b1", "2025.2.0.0-b1", SoftwareUpgradeState.PreFinalize);
    taskParams.verifyParams(defaultUniverse, true);
  }
}
