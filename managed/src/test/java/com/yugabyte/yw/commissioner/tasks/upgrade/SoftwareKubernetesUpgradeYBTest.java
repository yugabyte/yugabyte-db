// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyList;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.isNull;
import static org.mockito.ArgumentMatchers.nullable;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.never;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.google.common.net.HostAndPort;
import com.yugabyte.yw.common.RegexMatcher;
import com.yugabyte.yw.common.ReleaseContainer;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.forms.CreatePitrConfigParams;
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
import org.yb.client.ListSnapshotSchedulesResponse;
import org.yb.client.PromoteAutoFlagsResponse;
import org.yb.client.SnapshotScheduleInfo;
import org.yb.client.UpgradeYsqlResponse;
import org.yb.client.YBClient;
import org.yb.master.MasterAdminOuterClass.YsqlMajorCatalogUpgradeState;
import org.yb.master.MasterClusterOuterClass.PromoteAutoFlagsResponsePB;

public class SoftwareKubernetesUpgradeYBTest extends KubernetesUpgradeTaskTest {

  @Rule public MockitoRule rule = MockitoJUnit.rule();

  private SoftwareKubernetesUpgrade softwareKubernetesUpgrade;

  @Before
  public void setUp() {
    super.setUp();
    setFollowerLagMock();
    setUnderReplicatedTabletsMock();
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
            expectedOverrideFile.capture());
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
    mockClient = mock(YBClient.class);
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
            expectedOverrideFile.capture());
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
            expectedOverrideFile.capture());
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
        new SnapshotScheduleInfo(pitrConfig.getUuid(), 3600L, 86400L, Collections.emptyList());
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
    setupUniverseSingleAZ(true, true);
    useKubernetesNodeNames();
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
    // Catalog-upgrade subtasks use getClient(timeout) -> getClientWithConfig.
    lenient().when(mockYBClient.getClientWithConfig(any())).thenReturn(mockClient);
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
    ReleaseManager.ReleaseMetadata rm =
        ReleaseManager.ReleaseMetadata.create(YB_SOFTWARE_VERSION_NEW)
            .withFilePath("yugabyte-" + YB_SOFTWARE_VERSION_NEW + "-centos-x86_64.tar.gz");
    when(mockReleaseManager.getReleaseByVersion(anyString()))
        .thenReturn(new ReleaseContainer(rm, mockCloudUtilFactory, mockConfig, mockReleasesUtils));
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
}
