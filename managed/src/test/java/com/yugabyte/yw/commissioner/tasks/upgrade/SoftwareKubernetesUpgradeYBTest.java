// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import static com.yugabyte.yw.models.TaskInfo.State.Failure;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyInt;
import static org.mockito.ArgumentMatchers.anyList;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.isNull;
import static org.mockito.ArgumentMatchers.nullable;
import static org.mockito.Mockito.atLeastOnce;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.google.common.net.HostAndPort;
import com.yugabyte.yw.common.RegexMatcher;
import com.yugabyte.yw.common.TestHelper;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.forms.CreatePitrConfigParams;
import com.yugabyte.yw.forms.FinalizeUpgradeParams;
import com.yugabyte.yw.forms.SoftwareUpgradeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.PitrConfig;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import java.io.IOException;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.mockito.ArgumentCaptor;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import org.yb.CommonTypes;
import org.yb.client.DeleteSnapshotScheduleResponse;
import org.yb.client.GetYsqlMajorCatalogUpgradeStateResponse;
import org.yb.client.IsInitDbDoneResponse;
import org.yb.client.IsYsqlMajorCatalogUpgradeDoneResponse;
import org.yb.client.ListSnapshotSchedulesResponse;
import org.yb.client.PromoteAutoFlagsResponse;
import org.yb.client.SnapshotScheduleInfo;
import org.yb.client.StartYsqlMajorCatalogUpgradeResponse;
import org.yb.client.UpgradeYsqlResponse;
import org.yb.client.YBClientApi;
import org.yb.master.MasterAdminOuterClass.YsqlMajorCatalogUpgradeState;
import org.yb.master.MasterClusterOuterClass.PromoteAutoFlagsResponsePB;
import play.libs.Json;

public class SoftwareKubernetesUpgradeYBTest extends KubernetesUpgradeTaskTest {

  @Rule public MockitoRule rule = MockitoJUnit.rule();

  private SoftwareKubernetesUpgrade softwareKubernetesUpgrade;

  @Before
  public void setUp() {
    super.setUp();
    setFollowerLagMock();
    setUnderReplicatedTabletsMock();
    lenient()
        .when(mockYBClient.getServerVersion(any(), anyString(), anyInt()))
        .thenReturn(Optional.of(YB_SOFTWARE_VERSION_NEW));
    // Catalog-upgrade subtasks use getClient(timeout) -> getClientWithConfig.
    lenient().when(mockYBClient.getClientWithConfig(any())).thenReturn(mockClient);
    when(mockOperatorStatusUpdaterFactory.create()).thenReturn(mockOperatorStatusUpdater);
    try {
      when(mockClient.promoteAutoFlags(anyString(), anyBoolean(), anyBoolean()))
          .thenReturn(
              new PromoteAutoFlagsResponse(
                  0, "uuid", PromoteAutoFlagsResponsePB.getDefaultInstance()));
      when(mockGFlagsValidation.ysqlMajorVersionUpgrade(anyString(), anyString()))
          .thenReturn(false);
    } catch (Exception ignored) {
      fail();
    }
    this.softwareKubernetesUpgrade =
        new SoftwareKubernetesUpgrade(
            mockBaseTaskDependencies, null, mockOperatorStatusUpdaterFactory);
  }

  private TaskInfo submitTask(SoftwareUpgradeParams taskParams) {
    return submitTask(taskParams, TaskType.SoftwareKubernetesUpgradeYB, commissioner);
  }

  @Test
  public void testSoftwareUpgradeSingleAZ() throws IOException {
    softwareKubernetesUpgrade.setUserTaskUUID(UUID.randomUUID());
    setupUniverseSingleAZ(false, true);
    factory
        .forUniverse(defaultUniverse)
        .setValue(UniverseConfKeys.autoFlagUpdateSleepTimeInMilliSeconds.getKey(), "0ms");

    when(mockSoftwareUpgradeHelper.checkUpgradeRequireFinalize(anyString(), anyString()))
        .thenReturn(true);

    ArgumentCaptor<String> expectedYbSoftwareVersion = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNamespace = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedPodName = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<Map<String, String>> expectedConfig = ArgumentCaptor.forClass(Map.class);
    ArgumentCaptor<UUID> expectedUniverseUUID = ArgumentCaptor.forClass(UUID.class);

    String overrideFileRegex = "(.*)" + defaultUniverse.getUniverseUUID() + "(.*).yml";

    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = YB_SOFTWARE_VERSION_NEW;
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

    assertEquals(YB_SOFTWARE_VERSION_NEW, expectedYbSoftwareVersion.getValue());
    assertEquals(config, expectedConfig.getValue());
    assertEquals(NODE_PREFIX, expectedNodePrefix.getValue());
    assertEquals(NODE_PREFIX, expectedNamespace.getValue());
    assertThat(expectedOverrideFile.getValue(), RegexMatcher.matchesRegex(overrideFileRegex));

    assertEquals(Success, taskInfo.getTaskState());
    assertCheckSoftwareVersionRanForAllNodes(taskInfo);
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertTrue(defaultUniverse.getUniverseDetails().isSoftwareRollbackAllowed);
    assertEquals(
        YB_SOFTWARE_VERSION_OLD,
        defaultUniverse.getUniverseDetails().prevYBSoftwareConfig.getSoftwareVersion());
    assertEquals(
        UniverseDefinitionTaskParams.SoftwareUpgradeState.PreFinalize,
        defaultUniverse.getUniverseDetails().softwareUpgradeState);
  }

  @Test
  public void testSoftwareUpgradeWithAutoFinalize() throws IOException {
    softwareKubernetesUpgrade.setUserTaskUUID(UUID.randomUUID());
    setupUniverseSingleAZ(false, true);
    factory
        .forUniverse(defaultUniverse)
        .setValue(UniverseConfKeys.autoFlagUpdateSleepTimeInMilliSeconds.getKey(), "0ms");

    UpgradeYsqlResponse mockUpgradeYsqlResponse = new UpgradeYsqlResponse(1000, "", null);
    IsInitDbDoneResponse mockIsInitDbDoneResponse =
        new IsInitDbDoneResponse(1000, "", true, true, null, null);
    mockClient = mock(YBClientApi.class);
    when(mockYBClient.getClientWithConfig(any())).thenReturn(mockClient);

    try {
      when(mockClient.upgradeYsql(any(HostAndPort.class), anyBoolean()))
          .thenReturn(mockUpgradeYsqlResponse);
      when(mockClient.getIsInitDbDone()).thenReturn(mockIsInitDbDoneResponse);
    } catch (Exception ignored) {
      fail();
    }

    when(mockSoftwareUpgradeHelper.checkUpgradeRequireFinalize(anyString(), anyString()))
        .thenReturn(true);

    ArgumentCaptor<String> expectedYbSoftwareVersion = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNamespace = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedPodName = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<Map<String, String>> expectedConfig = ArgumentCaptor.forClass(Map.class);
    ArgumentCaptor<UUID> expectedUniverseUUID = ArgumentCaptor.forClass(UUID.class);

    String overrideFileRegex = "(.*)" + defaultUniverse.getUniverseUUID() + "(.*).yml";

    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = YB_SOFTWARE_VERSION_NEW;
    taskParams.rollbackSupport = false;
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

    assertEquals(YB_SOFTWARE_VERSION_NEW, expectedYbSoftwareVersion.getValue());
    assertEquals(config, expectedConfig.getValue());
    assertEquals(NODE_PREFIX, expectedNodePrefix.getValue());
    assertEquals(NODE_PREFIX, expectedNamespace.getValue());
    assertThat(expectedOverrideFile.getValue(), RegexMatcher.matchesRegex(overrideFileRegex));
    assertEquals(Success, taskInfo.getTaskState());
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertFalse(defaultUniverse.getUniverseDetails().isSoftwareRollbackAllowed);
    assertNull(defaultUniverse.getUniverseDetails().prevYBSoftwareConfig);
    assertEquals(
        UniverseDefinitionTaskParams.SoftwareUpgradeState.Ready,
        defaultUniverse.getUniverseDetails().softwareUpgradeState);
  }

  @Test
  public void testSoftwareUpgradeMultiAZ() throws IOException {
    softwareKubernetesUpgrade.setUserTaskUUID(UUID.randomUUID());
    setupUniverseMultiAZ(false, true);
    factory
        .forUniverse(defaultUniverse)
        .setValue(UniverseConfKeys.autoFlagUpdateSleepTimeInMilliSeconds.getKey(), "0ms");

    when(mockSoftwareUpgradeHelper.checkUpgradeRequireFinalize(anyString(), anyString()))
        .thenReturn(false);

    ArgumentCaptor<String> expectedYbSoftwareVersion = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNamespace = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedPodName = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<Map<String, String>> expectedConfig = ArgumentCaptor.forClass(Map.class);
    ArgumentCaptor<UUID> expectedUniverseUUID = ArgumentCaptor.forClass(UUID.class);
    String overrideFileRegex = "(.*)" + defaultUniverse.getUniverseUUID() + "(.*).yml";

    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = YB_SOFTWARE_VERSION_NEW;
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

    assertEquals(YB_SOFTWARE_VERSION_NEW, expectedYbSoftwareVersion.getValue());
    assertEquals(config, expectedConfig.getValue());
    assertTrue(expectedNodePrefix.getValue().contains(NODE_PREFIX));
    assertTrue(expectedNamespace.getValue().contains(NODE_PREFIX));
    assertThat(expectedOverrideFile.getValue(), RegexMatcher.matchesRegex(overrideFileRegex));

    assertEquals(Success, taskInfo.getTaskState());
    assertCheckSoftwareVersionRanForAllNodes(taskInfo);
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertTrue(defaultUniverse.getUniverseDetails().isSoftwareRollbackAllowed);
    assertEquals(
        YB_SOFTWARE_VERSION_OLD,
        defaultUniverse.getUniverseDetails().prevYBSoftwareConfig.getSoftwareVersion());
    assertEquals(
        UniverseDefinitionTaskParams.SoftwareUpgradeState.Ready,
        defaultUniverse.getUniverseDetails().softwareUpgradeState);
  }

  @Test
  public void testSoftwareUpgradeWithoutFinalizeKeepsPitr() throws Exception {
    // PLAT-23184: a no-finalize upgrade ends in Ready, which never re-creates PITR schedules, so it
    // must not delete them.
    PitrConfig pitrConfig = runUpgradeWithPitrConfig(false /* requireFinalize */);
    assertFalse(PitrConfig.getOrBadRequest(pitrConfig.getUuid()).isDisabled());
    verify(mockClient, never()).deleteSnapshotSchedule(any());
    assertEquals(
        UniverseDefinitionTaskParams.SoftwareUpgradeState.Ready,
        defaultUniverse.getUniverseDetails().softwareUpgradeState);
  }

  @Test
  public void testSoftwareUpgradeWithFinalizeDisablesPitr() throws Exception {
    // Finalize or rollback re-creates the schedules, so an upgrade that waits for finalize still
    // disables them.
    PitrConfig pitrConfig = runUpgradeWithPitrConfig(true /* requireFinalize */);
    assertTrue(PitrConfig.getOrBadRequest(pitrConfig.getUuid()).isDisabled());
    verify(mockClient, times(1)).deleteSnapshotSchedule(pitrConfig.getUuid());
    assertEquals(
        UniverseDefinitionTaskParams.SoftwareUpgradeState.PreFinalize,
        defaultUniverse.getUniverseDetails().softwareUpgradeState);
  }

  /** Runs a successful multi-AZ upgrade on a universe with one PITR config. */
  private PitrConfig runUpgradeWithPitrConfig(boolean requireFinalize) throws Exception {
    softwareKubernetesUpgrade.setUserTaskUUID(UUID.randomUUID());
    setupUniverseMultiAZ(false, true);
    factory
        .forUniverse(defaultUniverse)
        .setValue(UniverseConfKeys.autoFlagUpdateSleepTimeInMilliSeconds.getKey(), "0ms");
    when(mockSoftwareUpgradeHelper.checkUpgradeRequireFinalize(anyString(), anyString()))
        .thenReturn(requireFinalize);

    CreatePitrConfigParams params = new CreatePitrConfigParams();
    params.setUniverseUUID(defaultUniverse.getUniverseUUID());
    params.customerUUID = defaultCustomer.getUuid();
    params.name = "test-pitr";
    params.keyspaceName = "test-keyspace";
    params.tableType = CommonTypes.TableType.YQL_TABLE_TYPE;
    params.retentionPeriodInSeconds = 86400L;
    params.intervalInSeconds = 3600L;
    PitrConfig pitrConfig = PitrConfig.create(UUID.randomUUID(), params);
    SnapshotScheduleInfo scheduleInfo =
        new SnapshotScheduleInfo(
            pitrConfig.getUuid(),
            3600L,
            86400L,
            Collections.emptyList(),
            "test-keyspace",
            UUID.randomUUID(),
            CommonTypes.YQLDatabase.YQL_DATABASE_CQL);
    ListSnapshotSchedulesResponse listResponse = mock(ListSnapshotSchedulesResponse.class);
    lenient()
        .when(listResponse.getSnapshotScheduleInfoList())
        .thenReturn(Collections.singletonList(scheduleInfo));
    lenient().when(mockClient.listSnapshotSchedules(isNull())).thenReturn(listResponse);
    lenient()
        .when(mockClient.deleteSnapshotSchedule(pitrConfig.getUuid()))
        .thenReturn(mock(DeleteSnapshotScheduleResponse.class));

    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = YB_SOFTWARE_VERSION_NEW;
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(
        taskInfo.getSubTasks().stream()
            .filter(t -> t.getTaskState() == TaskInfo.State.Failure)
            .map(t -> t.getTaskType() + ": " + t.getErrorMessage())
            .collect(Collectors.toList())
            .toString(),
        Success,
        taskInfo.getTaskState());
    assertEquals(
        requireFinalize,
        taskInfo.getSubTasks().stream()
            .anyMatch(t -> t.getTaskType() == TaskType.DisablePitrConfig));
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    return pitrConfig;
  }

  @Test
  public void testAbortBeforeVersionUpdateFailsUpgradeAndRetryCompletes() throws Exception {
    abortBeforeVersionUpdateThenRetry(true /* requireFinalize */, true /* rollbackSupport */);
  }

  @Test
  public void testAbortBeforeVersionUpdateWithoutFinalizeFailsUpgradeAndRetryCompletes()
      throws Exception {
    abortBeforeVersionUpdateThenRetry(false /* requireFinalize */, true /* rollbackSupport */);
  }

  @Test
  public void testAutoFinalizeAbortBeforeVersionUpdateFailsUpgradeAndRetryCompletes()
      throws Exception {
    // With rollbackSupport=false the upgrade finalizes itself, then saves Ready with the version.
    abortBeforeVersionUpdateThenRetry(true /* requireFinalize */, false /* rollbackSupport */);
  }

  /**
   * PLAT-23182: aborts just before UpdateSoftwareVersion, the last upgrade subtask, which also
   * saves the end state. The upgrade must be left UpgradeFailed on the old version, and a retry
   * must finish it.
   */
  private void abortBeforeVersionUpdateThenRetry(boolean requireFinalize, boolean rollbackSupport)
      throws Exception {
    softwareKubernetesUpgrade.setUserTaskUUID(UUID.randomUUID());
    setupUniverseSingleAZ(false, true);
    factory
        .forUniverse(defaultUniverse)
        .setValue(UniverseConfKeys.autoFlagUpdateSleepTimeInMilliSeconds.getKey(), "0ms");
    when(mockSoftwareUpgradeHelper.checkUpgradeRequireFinalize(anyString(), anyString()))
        .thenReturn(requireFinalize);
    if (!rollbackSupport) {
      // RunYsqlUpgrade, which the in-task finalize runs.
      when(mockClient.upgradeYsql(any(HostAndPort.class), anyBoolean()))
          .thenReturn(new UpgradeYsqlResponse(1000, "", null));
      when(mockClient.getIsInitDbDone())
          .thenReturn(new IsInitDbDoneResponse(1000, "", true, true, null, null));
    }
    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = YB_SOFTWARE_VERSION_NEW;
    taskParams.rollbackSupport = rollbackSupport;
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.nodePrefix = NODE_PREFIX;
    taskParams.expectedUniverseVersion = -1;
    taskParams.sleepAfterMasterRestartMillis = 0;
    taskParams.sleepAfterTServerRestartMillis = 0;
    TaskInfo taskInfo;
    try {
      setPausePosition(0);
      UUID taskUuid = commissioner.submit(TaskType.SoftwareKubernetesUpgradeYB, taskParams);
      waitForTaskPaused(taskUuid);
      // The upgrade subtasks are created only after the freeze runs.
      setPausePosition(firstSubTaskPosition(taskUuid, TaskType.FreezeUniverse) + 1);
      commissioner.resumeTask(taskUuid);
      waitForTaskPaused(taskUuid);
      setAbortPosition(firstSubTaskPosition(taskUuid, TaskType.UpdateSoftwareVersion));
      commissioner.resumeTask(taskUuid);
      taskInfo = waitForTask(taskUuid);
    } finally {
      clearAbortOrPausePositions();
    }

    assertEquals(TaskInfo.State.Aborted, taskInfo.getTaskState());
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertEquals(
        UniverseDefinitionTaskParams.SoftwareUpgradeState.UpgradeFailed,
        defaultUniverse.getUniverseDetails().softwareUpgradeState);
    assertEquals(
        YB_SOFTWARE_VERSION_OLD,
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion);

    CustomerTask.create(
        defaultCustomer,
        defaultUniverse.getUniverseUUID(),
        taskInfo.getUuid(),
        CustomerTask.TargetType.Universe,
        CustomerTask.TaskType.SoftwareUpgrade,
        defaultUniverse.getName());
    CustomerTask retry =
        customerTaskManager.retryCustomerTask(defaultCustomer.getUuid(), taskInfo.getUuid());
    TaskInfo retryTask = waitForTask(retry.getTaskUUID());

    assertEquals(Success, retryTask.getTaskState());
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertEquals(
        requireFinalize && rollbackSupport
            ? UniverseDefinitionTaskParams.SoftwareUpgradeState.PreFinalize
            : UniverseDefinitionTaskParams.SoftwareUpgradeState.Ready,
        defaultUniverse.getUniverseDetails().softwareUpgradeState);
    assertEquals(rollbackSupport, defaultUniverse.getUniverseDetails().isSoftwareRollbackAllowed);
    assertEquals(
        YB_SOFTWARE_VERSION_NEW,
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion);
  }

  @Test
  public void testFinalizeWithStaleUniverseVersionRecordsTarget() throws InterruptedException {
    // PLAT-23182: older builds could leave PreFinalize with the old version still in the user
    // intent. Finalize takes the target from prevYBSoftwareConfig and records it, since it clears
    // prevYBSoftwareConfig.
    setupUniverseSingleAZ(false, true);
    factory
        .forUniverse(defaultUniverse)
        .setValue(UniverseConfKeys.autoFlagUpdateSleepTimeInMilliSeconds.getKey(), "0ms");
    UniverseDefinitionTaskParams.PrevYBSoftwareConfig prev =
        new UniverseDefinitionTaskParams.PrevYBSoftwareConfig();
    prev.setAutoFlagConfigVersion(1);
    prev.setSoftwareVersion(YB_SOFTWARE_VERSION_OLD);
    prev.setTargetUpgradeSoftwareVersion(YB_SOFTWARE_VERSION_NEW);
    TestHelper.updateUniversePrevSoftwareConfig(defaultUniverse, prev);
    TestHelper.updateUniverseIsRollbackAllowed(defaultUniverse, true);
    TestHelper.updateUniverseVersion(defaultUniverse, YB_SOFTWARE_VERSION_OLD);
    TestHelper.updateUniverseSoftwareUpgradeState(
        defaultUniverse, UniverseDefinitionTaskParams.SoftwareUpgradeState.PreFinalize);
    FinalizeUpgradeParams params = new FinalizeUpgradeParams();
    params.setUniverseUUID(defaultUniverse.getUniverseUUID());
    params.expectedUniverseVersion = -1;
    params.upgradeSystemCatalog = false;

    TaskInfo taskInfo =
        waitForTask(commissioner.submit(TaskType.FinalizeKubernetesUpgrade, params));

    assertEquals(Success, taskInfo.getTaskState());
    UniverseDefinitionTaskParams details =
        Universe.getOrBadRequest(defaultUniverse.getUniverseUUID()).getUniverseDetails();
    assertEquals(
        UniverseDefinitionTaskParams.SoftwareUpgradeState.Ready, details.softwareUpgradeState);
    assertNull(details.prevYBSoftwareConfig);
    assertEquals(YB_SOFTWARE_VERSION_NEW, details.getPrimaryCluster().userIntent.ybSoftwareVersion);
  }

  @Test
  public void testSoftwareKubernetesUpgradeRetries() {
    softwareKubernetesUpgrade.setUserTaskUUID(UUID.randomUUID());
    setupUniverseSingleAZ(false, true);
    factory
        .forUniverse(defaultUniverse)
        .setValue(UniverseConfKeys.autoFlagUpdateSleepTimeInMilliSeconds.getKey(), "0ms");
    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = YB_SOFTWARE_VERSION_NEW;

    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = 2;
    taskParams.sleepAfterMasterRestartMillis = 0;
    taskParams.sleepAfterTServerRestartMillis = 0;
    super.verifyTaskRetries(
        defaultCustomer,
        CustomerTask.TaskType.SoftwareUpgrade,
        CustomerTask.TargetType.Universe,
        defaultUniverse.getUniverseUUID(),
        TaskType.SoftwareKubernetesUpgradeYB,
        taskParams);
    checkUniverseNodesStates(taskParams.getUniverseUUID());
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertTrue(defaultUniverse.getUniverseDetails().isSoftwareRollbackAllowed);
    assertEquals(
        UniverseDefinitionTaskParams.SoftwareUpgradeState.Ready,
        defaultUniverse.getUniverseDetails().softwareUpgradeState);
  }

  @Test
  public void testYsqlMajorUpgradeTreatsReadReplicaAsReadReplica() throws Exception {
    // PLAT-23200: the YSQL major compatibility-flag helm step runs POD_INFO and a non-restart helm
    // upgrade for every cluster. For the read replica it must pass isReadOnlyCluster=true;
    // otherwise POD_INFO looks up the primary release with the read replica's placement and
    // replaces the primary's nodes, and the helm upgrade resizes the primary release.
    softwareKubernetesUpgrade.setUserTaskUUID(UUID.randomUUID());
    setupUniverseK8sSingleAZWithYSQL(true, true);
    Cluster readReplica = addReadReplicaCluster();
    setCheckNodesAreSafeToTakeDown(mockClient);
    factory
        .forUniverse(defaultUniverse)
        .setValue(UniverseConfKeys.waitAttemptsForMajorCatalogUpgrade.getKey(), "0");
    when(mockSoftwareUpgradeHelper.isYsqlMajorVersionUpgradeRequired(
            any(), anyString(), anyString()))
        .thenReturn(true);
    when(mockSoftwareUpgradeHelper.isAllMasterUpgradedToYsqlMajorVersion(any(), anyString()))
        .thenReturn(false);
    when(mockSoftwareUpgradeHelper.checkUpgradeRequireFinalize(anyString(), anyString()))
        .thenReturn(true);
    // Same DB-side stubs as testYsqlMajorUpgradeRetryRollsMastersBeforeCreateUser.
    lenient()
        .when(mockClient.setFlag(any(), anyString(), anyString(), anyBoolean()))
        .thenReturn(true);
    lenient()
        .when(mockClient.getYsqlMajorCatalogUpgradeState())
        .thenReturn(
            new GetYsqlMajorCatalogUpgradeStateResponse(
                0L,
                null,
                null,
                YsqlMajorCatalogUpgradeState
                    .YSQL_MAJOR_CATALOG_UPGRADE_PENDING_FINALIZE_OR_ROLLBACK));
    lenient()
        .when(mockClient.isYsqlMajorCatalogUpgradeDone())
        .thenReturn(new IsYsqlMajorCatalogUpgradeDoneResponse(0L, null, null, true));
    lenient()
        .when(mockClient.startYsqlMajorCatalogUpgrade())
        .thenReturn(new StartYsqlMajorCatalogUpgradeResponse(0L, null, null));
    lenient()
        .when(mockYsqlQueryExecutor.runUserDbCommands(anyString(), anyString(), any()))
        .thenReturn(Json.newObject());
    lenient()
        .when(
            mockYsqlQueryExecutor.executeQueryInNodeShell(
                any(), any(), any(), anyBoolean(), anyBoolean()))
        .thenReturn(Json.newObject());
    lenient().when(mockNodeUniverseManager.getRemoteTmpDir(any(), any())).thenReturn("/tmp");
    lenient()
        .when(
            mockKubernetesManager.executeCommandInPodContainer(
                any(), nullable(String.class), nullable(String.class), anyString(), anyList()))
        .thenAnswer(
            invocation -> {
              List<String> cmd = invocation.getArgument(4);
              return Arrays.asList("uname", "-m").equals(cmd) ? "x86_64" : "";
            });

    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = YB_SOFTWARE_VERSION_NEW;
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.nodePrefix = NODE_PREFIX;
    // Adding the read replica bumped the universe version past what submitTask expects.
    taskParams.expectedUniverseVersion = -1;
    taskParams.sleepAfterMasterRestartMillis = 5;
    taskParams.sleepAfterTServerRestartMillis = 5;
    TaskInfo taskInfo =
        waitForTask(commissioner.submit(TaskType.SoftwareKubernetesUpgradeYB, taskParams));

    assertEquals(taskInfo.getErrorMessage(), Success, taskInfo.getTaskState());
    assertReadReplicaCommandsTargetReadReplica(taskInfo, readReplica);
  }

  @Test
  public void testYsqlMajorUpgradeRetryRollsMastersBeforeCreateUser() throws Exception {
    softwareKubernetesUpgrade.setUserTaskUUID(UUID.randomUUID());
    // K8s-style node names so POD_INFO during master revert does not rename host-n* nodes out
    // from under SetFlagInMemory tasks scheduled with universe.getMasters()/getTServers().
    // Keep finishSetupUniverse's getLeaderMasterHostAndPort mock (FQDN form) so HELM_UPGRADE
    // with YsqlMajorVersionUpgradeState.IN_PROGRESS can resolve getMasterLeaderNode() for PGPASS.
    setupUniverseK8sSingleAZWithYSQL(true, true);
    setCheckNodesAreSafeToTakeDown(mockClient);
    factory
        .forUniverse(defaultUniverse)
        .setValue(UniverseConfKeys.autoFlagUpdateSleepTimeInMilliSeconds.getKey(), "0ms");
    // Avoid multi-minute catalog poll loops if isUpgradeAlreadyCompleted is skipped.
    factory
        .forUniverse(defaultUniverse)
        .setValue(UniverseConfKeys.waitAttemptsForMajorCatalogUpgrade.getKey(), "0");

    when(mockSoftwareUpgradeHelper.isYsqlMajorVersionUpgradeRequired(
            any(), anyString(), anyString()))
        .thenReturn(true);
    when(mockSoftwareUpgradeHelper.isSuperUserRequiredForCatalogUpgrade(
            any(), anyString(), anyString()))
        .thenReturn(true);
    when(mockSoftwareUpgradeHelper.isAllMasterUpgradedToYsqlMajorVersion(any(), anyString()))
        .thenReturn(false);
    when(mockSoftwareUpgradeHelper.isAnyMasterUpgradedOrInProgressForYsqlMajorVersion(
            any(), anyString()))
        .thenReturn(true);
    when(mockSoftwareUpgradeHelper.checkUpgradeRequireFinalize(anyString(), anyString()))
        .thenReturn(true);

    when(mockClient.setFlag(any(), anyString(), anyString(), anyBoolean())).thenReturn(true);
    when(mockClient.getYsqlMajorCatalogUpgradeState())
        .thenReturn(
            new GetYsqlMajorCatalogUpgradeStateResponse(
                0L,
                null,
                null,
                YsqlMajorCatalogUpgradeState
                    .YSQL_MAJOR_CATALOG_UPGRADE_PENDING_FINALIZE_OR_ROLLBACK));
    when(mockClient.isYsqlMajorCatalogUpgradeDone())
        .thenReturn(new IsYsqlMajorCatalogUpgradeDoneResponse(0L, null, null, true));
    when(mockClient.startYsqlMajorCatalogUpgrade())
        .thenReturn(new StartYsqlMajorCatalogUpgradeResponse(0L, null, null));
    when(mockYsqlQueryExecutor.runUserDbCommands(anyString(), anyString(), any()))
        .thenReturn(Json.newObject());
    // WaitForServer(YSQLSERVER) after each rolling pod uses executeQueryInNodeShell; without a
    // success stub it polls until serverWaitTimeoutMs (multi-minute hang in unit tests).
    when(mockYsqlQueryExecutor.executeQueryInNodeShell(
            any(), any(), any(), anyBoolean(), anyBoolean()))
        .thenReturn(Json.newObject());
    when(mockNodeUniverseManager.getRemoteTmpDir(any(), any())).thenReturn("/tmp");
    when(mockKubernetesManager.executeCommandInPodContainer(
            any(), nullable(String.class), nullable(String.class), anyString(), anyList()))
        .thenAnswer(
            invocation -> {
              @SuppressWarnings("unchecked")
              List<String> cmd = invocation.getArgument(4);
              if (cmd != null && cmd.equals(Arrays.asList("uname", "-m"))) {
                return "x86_64";
              }
              return "";
            });

    ArgumentCaptor<String> expectedYbSoftwareVersion = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNamespace = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<Map<String, String>> expectedConfig = ArgumentCaptor.forClass(Map.class);
    ArgumentCaptor<UUID> expectedUniverseUUID = ArgumentCaptor.forClass(UUID.class);

    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = YB_SOFTWARE_VERSION_NEW;
    TaskInfo taskInfo = submitTask(taskParams);

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    int createUserIdx = -1;
    for (int i = 0; i < subTasks.size(); i++) {
      if (subTasks.get(i).getTaskType() == TaskType.ManageCatalogUpgradeSuperUser) {
        createUserIdx = i;
        break;
      }
    }
    assertTrue(
        "ManageCatalogUpgradeSuperUser should run for YSQL major upgrade with superuser",
        createUserIdx >= 0);

    boolean waitForPodBeforeCreateUser = false;
    for (int i = 0; i < createUserIdx; i++) {
      if (subTasks.get(i).getTaskType() == TaskType.KubernetesWaitForPod) {
        waitForPodBeforeCreateUser = true;
        break;
      }
    }
    assertTrue(
        "Rolling master revert (KubernetesWaitForPod) must run before"
            + " ManageCatalogUpgradeSuperUser",
        waitForPodBeforeCreateUser);

    verify(mockKubernetesManager, atLeastOnce())
        .helmUpgrade(
            expectedUniverseUUID.capture(),
            expectedYbSoftwareVersion.capture(),
            expectedConfig.capture(),
            expectedNodePrefix.capture(),
            expectedNamespace.capture(),
            expectedOverrideFile.capture(),
            isNull());
    List<String> helmVersions = expectedYbSoftwareVersion.getAllValues();
    assertTrue(
        "Retry recovery must helm-upgrade masters with the previous software version",
        helmVersions.contains(YB_SOFTWARE_VERSION_OLD));
    assertTrue(
        "Forward upgrade must still helm-upgrade to the target software version",
        helmVersions.contains(YB_SOFTWARE_VERSION_NEW));
  }

  @Test
  public void testYsqlMajorUpgradeRetryAllMastersPendingRollsMastersBeforeCreateUser()
      throws Exception {
    softwareKubernetesUpgrade.setUserTaskUUID(UUID.randomUUID());
    // Abort after last master: all masters already on PG15, catalog still PENDING. Retry must
    // roll masters back before CREATE USER so DDLs are allowed (password was lost on abort).
    setupUniverseK8sSingleAZWithYSQL(true, true);
    setCheckNodesAreSafeToTakeDown(mockClient);
    factory
        .forUniverse(defaultUniverse)
        .setValue(UniverseConfKeys.autoFlagUpdateSleepTimeInMilliSeconds.getKey(), "0ms");
    factory
        .forUniverse(defaultUniverse)
        .setValue(UniverseConfKeys.waitAttemptsForMajorCatalogUpgrade.getKey(), "0");

    when(mockSoftwareUpgradeHelper.isYsqlMajorVersionUpgradeRequired(
            any(), anyString(), anyString()))
        .thenReturn(true);
    when(mockSoftwareUpgradeHelper.isSuperUserRequiredForCatalogUpgrade(
            any(), anyString(), anyString()))
        .thenReturn(true);
    when(mockSoftwareUpgradeHelper.isAllMasterUpgradedToYsqlMajorVersion(any(), anyString()))
        .thenReturn(true);
    when(mockSoftwareUpgradeHelper.getYsqlMajorCatalogUpgradeState(any()))
        .thenReturn(YsqlMajorCatalogUpgradeState.YSQL_MAJOR_CATALOG_UPGRADE_PENDING);
    when(mockSoftwareUpgradeHelper.checkUpgradeRequireFinalize(anyString(), anyString()))
        .thenReturn(true);

    when(mockClient.setFlag(any(), anyString(), anyString(), anyBoolean())).thenReturn(true);
    when(mockClient.getYsqlMajorCatalogUpgradeState())
        .thenReturn(
            new GetYsqlMajorCatalogUpgradeStateResponse(
                0L,
                null,
                null,
                YsqlMajorCatalogUpgradeState
                    .YSQL_MAJOR_CATALOG_UPGRADE_PENDING_FINALIZE_OR_ROLLBACK));
    when(mockClient.isYsqlMajorCatalogUpgradeDone())
        .thenReturn(new IsYsqlMajorCatalogUpgradeDoneResponse(0L, null, null, true));
    when(mockClient.startYsqlMajorCatalogUpgrade())
        .thenReturn(new StartYsqlMajorCatalogUpgradeResponse(0L, null, null));
    when(mockYsqlQueryExecutor.runUserDbCommands(anyString(), anyString(), any()))
        .thenReturn(Json.newObject());
    when(mockYsqlQueryExecutor.executeQueryInNodeShell(
            any(), any(), any(), anyBoolean(), anyBoolean()))
        .thenReturn(Json.newObject());
    when(mockNodeUniverseManager.getRemoteTmpDir(any(), any())).thenReturn("/tmp");
    when(mockKubernetesManager.executeCommandInPodContainer(
            any(), nullable(String.class), nullable(String.class), anyString(), anyList()))
        .thenAnswer(
            invocation -> {
              @SuppressWarnings("unchecked")
              List<String> cmd = invocation.getArgument(4);
              if (cmd != null && cmd.equals(Arrays.asList("uname", "-m"))) {
                return "x86_64";
              }
              return "";
            });

    ArgumentCaptor<String> expectedYbSoftwareVersion = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNodePrefix = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedNamespace = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<String> expectedOverrideFile = ArgumentCaptor.forClass(String.class);
    ArgumentCaptor<Map<String, String>> expectedConfig = ArgumentCaptor.forClass(Map.class);
    ArgumentCaptor<UUID> expectedUniverseUUID = ArgumentCaptor.forClass(UUID.class);

    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = YB_SOFTWARE_VERSION_NEW;
    TaskInfo taskInfo = submitTask(taskParams);

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    int createUserIdx = -1;
    for (int i = 0; i < subTasks.size(); i++) {
      if (subTasks.get(i).getTaskType() == TaskType.ManageCatalogUpgradeSuperUser) {
        createUserIdx = i;
        break;
      }
    }
    assertTrue(
        "ManageCatalogUpgradeSuperUser should run for YSQL major upgrade with superuser",
        createUserIdx >= 0);

    boolean waitForPodBeforeCreateUser = false;
    for (int i = 0; i < createUserIdx; i++) {
      if (subTasks.get(i).getTaskType() == TaskType.KubernetesWaitForPod) {
        waitForPodBeforeCreateUser = true;
        break;
      }
    }
    assertTrue(
        "Rolling master revert (KubernetesWaitForPod) must run before"
            + " ManageCatalogUpgradeSuperUser",
        waitForPodBeforeCreateUser);

    verify(mockKubernetesManager, atLeastOnce())
        .helmUpgrade(
            expectedUniverseUUID.capture(),
            expectedYbSoftwareVersion.capture(),
            expectedConfig.capture(),
            expectedNodePrefix.capture(),
            expectedNamespace.capture(),
            expectedOverrideFile.capture(),
            isNull());
    List<String> helmVersions = expectedYbSoftwareVersion.getAllValues();
    assertTrue(
        "Retry recovery must helm-upgrade masters with the previous software version",
        helmVersions.contains(YB_SOFTWARE_VERSION_OLD));
    assertTrue(
        "Forward upgrade must still helm-upgrade to the target software version",
        helmVersions.contains(YB_SOFTWARE_VERSION_NEW));
  }

  @Test
  public void testSoftwareUpgradeFailsWhenServerVersionMismatch() throws Exception {
    softwareKubernetesUpgrade.setUserTaskUUID(UUID.randomUUID());
    setupUniverseMultiAZ(false, true);
    factory
        .forUniverse(defaultUniverse)
        .setValue(UniverseConfKeys.autoFlagUpdateSleepTimeInMilliSeconds.getKey(), "0ms");

    UUID staleAz = defaultUniverse.getNodes().iterator().next().azUuid;
    Set<String> staleNodeIps =
        defaultUniverse.getNodes().stream()
            .filter(node -> staleAz.equals(node.azUuid))
            .map(node -> node.cloudInfo.private_ip)
            .collect(Collectors.toSet());
    when(mockYBClient.getServerVersion(any(), anyString(), anyInt()))
        .thenAnswer(
            invocation -> {
              String ip = invocation.getArgument(1);
              if (staleNodeIps.contains(ip)) {
                return Optional.of(YB_SOFTWARE_VERSION_OLD);
              }
              return Optional.of(YB_SOFTWARE_VERSION_NEW);
            });

    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = YB_SOFTWARE_VERSION_NEW;
    TaskInfo taskInfo = submitTask(taskParams);

    assertEquals(Failure, taskInfo.getTaskState());
    assertTrue(
        taskInfo.getSubTasks().stream()
            .anyMatch(
                task ->
                    task.getTaskType() == TaskType.CheckSoftwareVersion
                        && task.getTaskState() == Failure
                        && task.getErrorMessage() != null
                        && task.getErrorMessage().contains("Expected version")));
    verify(mockClient, never()).promoteAutoFlags(anyString(), anyBoolean(), anyBoolean());
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertEquals(
        YB_SOFTWARE_VERSION_OLD,
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion);
    assertEquals(
        UniverseDefinitionTaskParams.SoftwareUpgradeState.UpgradeFailed,
        defaultUniverse.getUniverseDetails().softwareUpgradeState);
  }

  private void assertCheckSoftwareVersionRanForAllNodes(TaskInfo taskInfo) {
    assertEquals(
        defaultUniverse.getNodes().size(),
        taskInfo.getSubTasks().stream()
            .filter(task -> task.getTaskType() == TaskType.CheckSoftwareVersion)
            .count());
  }
}
