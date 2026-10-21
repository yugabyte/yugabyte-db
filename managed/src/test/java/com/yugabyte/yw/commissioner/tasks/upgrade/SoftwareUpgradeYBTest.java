// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.hamcrest.CoreMatchers.containsString;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyInt;
import static org.mockito.ArgumentMatchers.anyList;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.ArgumentMatchers.isNull;
import static org.mockito.Mockito.lenient;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.MockUpgrade;
import com.yugabyte.yw.commissioner.UpgradeTaskBase;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.SaveSoftwareUpgradeProgress;
import com.yugabyte.yw.commissioner.tasks.subtasks.UpdateUniverseSoftwareUpgradeState;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ReleaseContainer;
import com.yugabyte.yw.common.ReleaseManager;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.TestHelper;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.forms.AZUpgradeState;
import com.yugabyte.yw.forms.AZUpgradeStatus;
import com.yugabyte.yw.forms.AZUpgradeStep;
import com.yugabyte.yw.forms.CanaryPauseState;
import com.yugabyte.yw.forms.CanaryUpgradeConfig;
import com.yugabyte.yw.forms.CreatePitrConfigParams;
import com.yugabyte.yw.forms.GFlagsUpgradeParams;
import com.yugabyte.yw.forms.RollbackUpgradeParams;
import com.yugabyte.yw.forms.SoftwareUpgradeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.PrevYBSoftwareConfig;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.SoftwareUpgradeState;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeOption;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.PitrConfig;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashSet;
import java.util.List;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
import java.util.function.Consumer;
import java.util.function.ToIntFunction;
import java.util.stream.Collectors;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import lombok.extern.slf4j.Slf4j;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.InjectMocks;
import org.mockito.Mockito;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import org.yb.CommonTypes;
import org.yb.client.DeleteSnapshotScheduleResponse;
import org.yb.client.GetYsqlMajorCatalogUpgradeStateResponse;
import org.yb.client.IsInitDbDoneResponse;
import org.yb.client.ListSnapshotSchedulesResponse;
import org.yb.client.RollbackYsqlMajorCatalogVersionResponse;
import org.yb.client.SnapshotScheduleInfo;
import org.yb.client.UpgradeYsqlResponse;
import org.yb.master.MasterAdminOuterClass.YsqlMajorCatalogUpgradeState;

@RunWith(JUnitParamsRunner.class)
@Slf4j
public class SoftwareUpgradeYBTest extends UpgradeTaskTest {

  @Rule public MockitoRule rule = MockitoJUnit.rule();

  @InjectMocks private SoftwareUpgrade softwareUpgrade;

  private boolean ysqlMajorUpgrade = false;

  @Override
  @Before
  public void setUp() {
    super.setUp();

    attachHooks("SoftwareUpgrade");
    lenient().when(mockYBClient.getClientWithConfig(any())).thenReturn(mockClient);

    softwareUpgrade.setUserTaskUUID(UUID.randomUUID());
    ShellResponse successResponse = new ShellResponse();
    successResponse.message = "YSQL successfully upgraded to the latest version";

    ShellResponse shellResponse = new ShellResponse();
    shellResponse.message = "Command output:\n2989898";
    shellResponse.code = 0;
    List<String> command = new ArrayList<>();
    command.add("awk");
    command.add(String.format("/%s/ {print$2}", Util.AVAILABLE_MEMORY));
    command.add("/proc/meminfo");
    when(mockNodeUniverseManager.runCommand(any(), any(), eq(command), any()))
        .thenReturn(shellResponse);

    mockLocaleCheckResponse(mockNodeUniverseManager);

    setCheckNodesAreSafeToTakeDown(mockClient);

    setUnderReplicatedTabletsMock();
    setFollowerLagMock();
    factory
        .forUniverse(defaultUniverse)
        .setValue(UniverseConfKeys.autoFlagUpdateSleepTimeInMilliSeconds.getKey(), "0ms");

    TestHelper.updateUniverseVersion(defaultUniverse, "2.21.0.0-b1");
  }

  private TaskInfo submitTask(SoftwareUpgradeParams requestParams) {
    return submitTask(requestParams, TaskType.SoftwareUpgradeYB, commissioner);
  }

  private TaskInfo submitTask(SoftwareUpgradeParams requestParams, int expectedVersion) {
    return submitTask(requestParams, TaskType.SoftwareUpgradeYB, commissioner, expectedVersion);
  }

  @Test
  public void testSoftwareUpgradeWithSameVersion() {
    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion =
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;
    taskParams.clusters.add(defaultUniverse.getUniverseDetails().getPrimaryCluster());

    assertThrows(RuntimeException.class, () -> submitTask(taskParams));
    verify(mockNodeManager, times(0)).nodeCommand(any(), any());
    defaultUniverse.refresh();
    assertEquals(2, defaultUniverse.getVersion());
  }

  @Test
  public void testSoftwareUpgradeWithoutVersion() {
    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.clusters.add(defaultUniverse.getUniverseDetails().getPrimaryCluster());
    assertThrows(RuntimeException.class, () -> submitTask(taskParams));
    verify(mockNodeManager, times(0)).nodeCommand(any(), any());
    defaultUniverse.refresh();
    assertEquals(2, defaultUniverse.getVersion());
  }

  @Test
  public void testSoftwareUpgrade() throws IOException {
    updateDefaultUniverseTo5Nodes(true);

    when(mockSoftwareUpgradeHelper.checkUpgradeRequireFinalize(anyString(), anyString()))
        .thenReturn(true);

    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = "2.21.0.0-b2";
    taskParams.clusters.add(defaultUniverse.getUniverseDetails().getPrimaryCluster());
    mockDBServerVersion(
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion,
        taskParams.ybSoftwareVersion,
        defaultUniverse.getMasters().size() + defaultUniverse.getTServers().size());
    TaskInfo taskInfo = submitTask(taskParams, defaultUniverse.getVersion());
    verify(mockNodeManager, times(45)).nodeCommand(any(), any());
    verify(mockNodeUniverseManager, times(15)).runCommand(any(), any(), anyList(), any());

    MockUpgrade mockUpgrade = initMockUpgrade();
    mockUpgrade
        .precheckTasks(getPrecheckTasks(true))
        .addTasks(TaskType.UpdateUniverseState)
        .addTasks(TaskType.DisablePitrConfig)
        .addSimultaneousTasks(TaskType.AnsibleConfigureServers, defaultUniverse.getMasters().size())
        .addSimultaneousTasks(
            TaskType.AnsibleConfigureServers, defaultUniverse.getTServers().size())
        .addTasks(TaskType.XClusterInfoPersist)
        .addTasks(TaskType.StoreAutoFlagConfigVersion)
        .addTasks(TaskType.SaveSoftwareUpgradeProgress)
        .addSimultaneousTasks(
            TaskType.AnsibleConfigureServers, defaultUniverse.getTServers().size())
        .rollingSoftwareUpgradeWithProgressSaves(
            UpgradeOption.ROLLING_UPGRADE,
            UpgradeTaskBase.UpgradeContext.builder()
                .reconfigureMaster(false)
                .runBeforeStopping(false)
                .processInactiveMaster(true)
                .targetSoftwareVersion("2.21.0.0-b2")
                .build(),
            TaskType.AnsibleConfigureServers,
            false)
        .addSimultaneousTasks(TaskType.CheckSoftwareVersion, defaultUniverse.getTServers().size())
        .addTasks(TaskType.PromoteAutoFlags)
        .addTasks(TaskType.UpdateSoftwareVersion)
        .verifyTasks(taskInfo.getSubTasks());

    assertEquals(100.0, taskInfo.getPercentCompleted(), 0);
    assertEquals(Success, taskInfo.getTaskState());
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertTrue(defaultUniverse.getUniverseDetails().isSoftwareRollbackAllowed);
    assertEquals(
        "2.21.0.0-b1",
        defaultUniverse.getUniverseDetails().prevYBSoftwareConfig.getSoftwareVersion());
    assertEquals(
        SoftwareUpgradeState.PreFinalize,
        defaultUniverse.getUniverseDetails().softwareUpgradeState);
  }

  @Test
  public void testRollingSoftwareUpgradeSkipsComprehensivePrechecksOnRetry() {
    updateDefaultUniverseTo5Nodes(true);
    factory
        .forUniverse(defaultUniverse)
        .setValue(UniverseConfKeys.enableComprehensivePrechecks.getKey(), "true");
    when(mockSoftwareUpgradeHelper.checkUpgradeRequireFinalize(anyString(), anyString()))
        .thenReturn(true);

    // Precheck-only runs: universe stays on the old DB version so rolling-upgrade prechecks still
    // apply. Second run sets previousTaskUUID so isFirstTry() is false.
    SoftwareUpgradeParams taskParams1 = new SoftwareUpgradeParams();
    taskParams1.ybSoftwareVersion = "2.21.0.0-b2";
    taskParams1.clusters.add(defaultUniverse.getUniverseDetails().getPrimaryCluster());
    taskParams1.setRunOnlyPrechecks(true);
    mockDBServerVersion(
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion,
        taskParams1.ybSoftwareVersion,
        defaultUniverse.getMasters().size() + defaultUniverse.getTServers().size());
    TaskInfo taskInfo1 = submitTask(taskParams1, defaultUniverse.getVersion());
    assertEquals(Success, taskInfo1.getTaskState());
    assertTrue(
        taskInfo1.getSubTasks().stream()
            .anyMatch(t -> t.getTaskType() == TaskType.CheckServiceLiveness));
    assertTrue(
        taskInfo1.getSubTasks().stream()
            .anyMatch(t -> t.getTaskType() == TaskType.CheckNodeCommandExecution));

    SoftwareUpgradeParams taskParams2 = new SoftwareUpgradeParams();
    taskParams2.ybSoftwareVersion = "2.21.0.0-b2";
    taskParams2.clusters.add(defaultUniverse.getUniverseDetails().getPrimaryCluster());
    taskParams2.setRunOnlyPrechecks(true);
    taskParams2.setPreviousTaskUUID(taskInfo1.getUuid());
    mockDBServerVersion(
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion,
        taskParams2.ybSoftwareVersion,
        defaultUniverse.getMasters().size() + defaultUniverse.getTServers().size());

    TaskInfo taskInfo2 = submitTask(taskParams2, defaultUniverse.getVersion());
    assertEquals(Success, taskInfo2.getTaskState());
    assertFalse(
        taskInfo2.getSubTasks().stream()
            .anyMatch(t -> t.getTaskType() == TaskType.CheckServiceLiveness));
    assertFalse(
        taskInfo2.getSubTasks().stream()
            .anyMatch(t -> t.getTaskType() == TaskType.CheckNodeCommandExecution));
  }

  @Test
  public void testSoftwareUpgradeWithAutoFinalize() {
    updateDefaultUniverseTo5Nodes(true);
    mockRunYsqlUpgrade();

    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = "2.21.0.0-b2";
    taskParams.rollbackSupport = false;
    taskParams.clusters.add(defaultUniverse.getUniverseDetails().getPrimaryCluster());
    mockDBServerVersion(
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion,
        taskParams.ybSoftwareVersion,
        defaultUniverse.getMasters().size() + defaultUniverse.getTServers().size());
    TaskInfo taskInfo = submitTask(taskParams, defaultUniverse.getVersion());
    verify(mockNodeManager, times(45)).nodeCommand(any(), any());
    verify(mockNodeUniverseManager, times(15)).runCommand(any(), any(), anyList(), any());

    MockUpgrade mockUpgrade = initMockUpgrade();
    mockUpgrade
        .precheckTasks(getPrecheckTasks(true))
        .addTasks(TaskType.UpdateUniverseState)
        .addSimultaneousTasks(TaskType.AnsibleConfigureServers, defaultUniverse.getMasters().size())
        .addSimultaneousTasks(
            TaskType.AnsibleConfigureServers, defaultUniverse.getTServers().size())
        .addTasks(TaskType.XClusterInfoPersist)
        .addTasks(TaskType.StoreAutoFlagConfigVersion)
        .addTasks(TaskType.SaveSoftwareUpgradeProgress)
        .addSimultaneousTasks(
            TaskType.AnsibleConfigureServers, defaultUniverse.getTServers().size())
        .rollingSoftwareUpgradeWithProgressSaves(
            UpgradeOption.ROLLING_UPGRADE,
            UpgradeTaskBase.UpgradeContext.builder()
                .reconfigureMaster(false)
                .runBeforeStopping(false)
                .processInactiveMaster(true)
                .targetSoftwareVersion("2.21.0.0-b2")
                .build(),
            TaskType.AnsibleConfigureServers,
            false)
        .addSimultaneousTasks(TaskType.CheckSoftwareVersion, defaultUniverse.getTServers().size())
        .addTasks(TaskType.PromoteAutoFlags)
        .addTasks(TaskType.SaveSoftwareUpgradeProgress)
        .addTasks(TaskType.UpdateUniverseState)
        .addTasks(TaskType.PromoteAutoFlags)
        .addTasks(TaskType.RunYsqlUpgrade)
        .addTasks(TaskType.EnablePitrConfig)
        .addTasks(TaskType.UpdateSoftwareVersion)
        .verifyTasks(taskInfo.getSubTasks());

    assertEquals(100.0, taskInfo.getPercentCompleted(), 0);
    assertEquals(Success, taskInfo.getTaskState());
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertFalse(defaultUniverse.getUniverseDetails().isSoftwareRollbackAllowed);
    assertNull(defaultUniverse.getUniverseDetails().prevYBSoftwareConfig);
    assertEquals(
        SoftwareUpgradeState.Ready, defaultUniverse.getUniverseDetails().softwareUpgradeState);
  }

  /** Mocks for the RunYsqlUpgrade subtask that auto-finalize (rollbackSupport=false) runs. */
  private void mockRunYsqlUpgrade() {
    try {
      UpgradeYsqlResponse mockUpgradeYsqlResponse = new UpgradeYsqlResponse(1000, "", null);
      when(mockYBClient.getClientWithConfig(any())).thenReturn(mockClient);
      when(mockClient.upgradeYsql(any(HostAndPort.class), anyBoolean()))
          .thenReturn(mockUpgradeYsqlResponse);
      IsInitDbDoneResponse mockIsInitDbDoneResponse =
          new IsInitDbDoneResponse(1000, "", true, true, null, null);
      when(mockClient.getIsInitDbDone()).thenReturn(mockIsInitDbDoneResponse);
    } catch (Exception ignored) {
      fail();
    }
  }

  @Test
  public void testSoftwareUpgradeWithNoFinalize() {
    updateDefaultUniverseTo5Nodes(true);

    when(mockSoftwareUpgradeHelper.checkUpgradeRequireFinalize(anyString(), anyString()))
        .thenReturn(false);

    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = "2.21.0.0-b2";
    taskParams.rollbackSupport = true;
    taskParams.clusters.add(defaultUniverse.getUniverseDetails().getPrimaryCluster());
    mockDBServerVersion(
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion,
        taskParams.ybSoftwareVersion,
        defaultUniverse.getMasters().size() + defaultUniverse.getTServers().size());
    TaskInfo taskInfo = submitTask(taskParams, defaultUniverse.getVersion());
    verify(mockNodeManager, times(45)).nodeCommand(any(), any());
    verify(mockNodeUniverseManager, times(15)).runCommand(any(), any(), anyList(), any());

    MockUpgrade mockUpgrade = initMockUpgrade();
    mockUpgrade
        .precheckTasks(getPrecheckTasks(true))
        .addTasks(TaskType.UpdateUniverseState)
        .addSimultaneousTasks(TaskType.AnsibleConfigureServers, defaultUniverse.getMasters().size())
        .addSimultaneousTasks(
            TaskType.AnsibleConfigureServers, defaultUniverse.getTServers().size())
        .addTasks(TaskType.XClusterInfoPersist)
        .addTasks(TaskType.StoreAutoFlagConfigVersion)
        .addTasks(TaskType.SaveSoftwareUpgradeProgress)
        .addSimultaneousTasks(
            TaskType.AnsibleConfigureServers, defaultUniverse.getTServers().size())
        .rollingSoftwareUpgradeWithProgressSaves(
            UpgradeOption.ROLLING_UPGRADE,
            UpgradeTaskBase.UpgradeContext.builder()
                .reconfigureMaster(false)
                .runBeforeStopping(false)
                .processInactiveMaster(true)
                .targetSoftwareVersion("2.21.0.0-b2")
                .build(),
            TaskType.AnsibleConfigureServers,
            false)
        .addSimultaneousTasks(TaskType.CheckSoftwareVersion, defaultUniverse.getTServers().size())
        .addTasks(TaskType.PromoteAutoFlags)
        .addTasks(TaskType.SaveSoftwareUpgradeProgress)
        .addTasks(TaskType.UpdateSoftwareVersion)
        .verifyTasks(taskInfo.getSubTasks());

    assertEquals(100.0, taskInfo.getPercentCompleted(), 0);
    assertEquals(Success, taskInfo.getTaskState());
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertTrue(defaultUniverse.getUniverseDetails().isSoftwareRollbackAllowed);
    assertEquals(
        SoftwareUpgradeState.Ready, defaultUniverse.getUniverseDetails().softwareUpgradeState);
  }

  @Test
  @Parameters({"false", "true"})
  public void testSoftwareUpgradeWithReadReplica(boolean enableYSQL) {
    updateDefaultUniverseTo5Nodes(enableYSQL);
    addReadReplica(
        enableYSQL,
        AvailabilityZone.createOrThrow(region, "az-4", "AZ 4", "subnet-1"),
        AvailabilityZone.createOrThrow(region, "az-5", "AZ 5", "subnet-2"),
        AvailabilityZone.createOrThrow(region, "az-6", "AZ 6", "subnet-3"));

    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = "2.21.0.0-b2";
    taskParams.clusters.add(defaultUniverse.getUniverseDetails().getPrimaryCluster());
    mockDBServerVersion(
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion,
        taskParams.ybSoftwareVersion,
        defaultUniverse.getMasters().size() + defaultUniverse.getTServers().size());
    TaskInfo taskInfo = submitTask(taskParams, defaultUniverse.getVersion());
    verify(mockNodeManager, times(63)).nodeCommand(any(), any());
    verify(mockNodeUniverseManager, times(24)).runCommand(any(), any(), anyList(), any());

    MockUpgrade mockUpgrade = initMockUpgrade();
    mockUpgrade
        .precheckTasks(enableYSQL, getPrecheckTasks(true))
        .addTasks(TaskType.UpdateUniverseState)
        .addSimultaneousTasks(TaskType.AnsibleConfigureServers, defaultUniverse.getMasters().size())
        .addSimultaneousTasks(
            TaskType.AnsibleConfigureServers, defaultUniverse.getTServersInPrimaryCluster().size())
        .addTasks(TaskType.XClusterInfoPersist)
        .addTasks(TaskType.StoreAutoFlagConfigVersion)
        .addTasks(TaskType.SaveSoftwareUpgradeProgress)
        .addSimultaneousTasks(
            TaskType.AnsibleConfigureServers, defaultUniverse.getTServers().size())
        .rollingSoftwareUpgradeWithProgressSaves(
            UpgradeOption.ROLLING_UPGRADE,
            UpgradeTaskBase.UpgradeContext.builder()
                .reconfigureMaster(false)
                .runBeforeStopping(false)
                .processInactiveMaster(true)
                .targetSoftwareVersion("2.21.0.0-b2")
                .build(),
            TaskType.AnsibleConfigureServers,
            false)
        .addSimultaneousTasks(TaskType.CheckSoftwareVersion, defaultUniverse.getTServers().size())
        .addTasks(TaskType.PromoteAutoFlags)
        .addTasks(TaskType.SaveSoftwareUpgradeProgress)
        .addTasks(TaskType.UpdateSoftwareVersion)
        .verifyTasks(taskInfo.getSubTasks());

    assertEquals(100.0, taskInfo.getPercentCompleted(), 0);
    assertEquals(Success, taskInfo.getTaskState());
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertTrue(defaultUniverse.getUniverseDetails().isSoftwareRollbackAllowed);
    assertEquals(
        "2.21.0.0-b1",
        defaultUniverse.getUniverseDetails().prevYBSoftwareConfig.getSoftwareVersion());
  }

  /** Adds a read replica cluster with one node in each of {@code zones}. */
  private void addReadReplica(boolean enableYSQL, AvailabilityZone... zones) {
    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.numNodes = zones.length;
    userIntent.replicationFactor = 3;
    userIntent.ybSoftwareVersion = "2.21.0.0-b1";
    userIntent.regionList = ImmutableList.of(region.getUuid());
    userIntent.enableYSQL = enableYSQL;

    TestUtils.initUserIntent(
        userIntent,
        defaultProvider,
        ApiUtils.UTIL_INST_TYPE,
        ApiUtils.getDummyDeviceInfo(1, 100),
        "demo-access");

    PlacementInfo pi = new PlacementInfo();
    for (int i = 0; i < zones.length; i++) {
      PlacementInfoUtil.addPlacementZone(zones[i].getUuid(), pi, 1, 1, i == 1);
    }

    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            ApiUtils.mockUniverseUpdaterWithReadReplica(userIntent, pi));
  }

  @Test
  public void testSoftwareNonRollingUpgrade() {
    updateDefaultUniverseTo5Nodes(true);

    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = "2.21.0.0-b2";
    taskParams.upgradeOption = UpgradeOption.NON_ROLLING_UPGRADE;
    taskParams.clusters.add(defaultUniverse.getUniverseDetails().getPrimaryCluster());

    mockDBServerVersion(
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion,
        taskParams.ybSoftwareVersion,
        defaultUniverse.getMasters().size() + defaultUniverse.getTServers().size());

    TaskInfo taskInfo = submitTask(taskParams, defaultUniverse.getVersion());
    ArgumentCaptor<NodeTaskParams> commandParams = ArgumentCaptor.forClass(NodeTaskParams.class);
    verify(mockNodeManager, times(25)).nodeCommand(any(), commandParams.capture());
    verify(mockNodeUniverseManager, times(10)).runCommand(any(), any(), anyList(), any());

    MockUpgrade mockUpgrade = initMockUpgrade();
    mockUpgrade
        .precheckTasks(getPrecheckTasks(false))
        .addTasks(TaskType.UpdateUniverseState)
        .addSimultaneousTasks(TaskType.AnsibleConfigureServers, defaultUniverse.getMasters().size())
        .addSimultaneousTasks(
            TaskType.AnsibleConfigureServers, defaultUniverse.getTServers().size())
        .addTasks(TaskType.XClusterInfoPersist)
        .addTasks(TaskType.StoreAutoFlagConfigVersion)
        .addTasks(TaskType.SaveSoftwareUpgradeProgress)
        .addSimultaneousTasks(
            TaskType.AnsibleConfigureServers, defaultUniverse.getTServers().size())
        .nonRollingSoftwareUpgradeWithProgressSaves(
            UpgradeTaskBase.UpgradeContext.builder()
                .reconfigureMaster(false)
                .runBeforeStopping(false)
                .processInactiveMaster(true)
                .targetSoftwareVersion("2.21.0.0-b2")
                .build(),
            TaskType.AnsibleConfigureServers,
            false)
        .addSimultaneousTasks(TaskType.CheckSoftwareVersion, defaultUniverse.getTServers().size())
        .addTasks(TaskType.PromoteAutoFlags)
        .addTasks(TaskType.SaveSoftwareUpgradeProgress)
        .addTasks(TaskType.UpdateSoftwareVersion)
        .verifyTasks(taskInfo.getSubTasks());

    assertEquals(100.0, taskInfo.getPercentCompleted(), 0);
    assertEquals(Success, taskInfo.getTaskState());
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertTrue(defaultUniverse.getUniverseDetails().isSoftwareRollbackAllowed);
    assertEquals(
        "2.21.0.0-b1",
        defaultUniverse.getUniverseDetails().prevYBSoftwareConfig.getSoftwareVersion());
  }

  @Test
  public void testYsqlMajorVersionSoftwareUpgrade() throws Exception {

    String baseVersion = "2024.2.2.0-b1";
    String targetVersion = "2025.1.0.0-b1";
    this.ysqlMajorUpgrade = true;

    TestHelper.updateUniverseVersion(defaultUniverse, baseVersion);
    when(mockSoftwareUpgradeHelper.isYsqlMajorVersionUpgradeRequired(
            any(), anyString(), anyString()))
        .thenReturn(true);

    when(mockSoftwareUpgradeHelper.checkUpgradeRequireFinalize(anyString(), anyString()))
        .thenReturn(true);

    ReleaseManager.ReleaseMetadata rm =
        ReleaseManager.ReleaseMetadata.create(targetVersion)
            .withFilePath("yugabyte-" + targetVersion + "-centos-x86_64" + ".tar.gz");
    when(mockReleaseManager.getReleaseByVersion(anyString()))
        .thenReturn(new ReleaseContainer(rm, mockCloudUtilFactory, mockConfig, mockReleasesUtils));
    when(mockClient.setFlag(any(), anyString(), anyString(), anyBoolean())).thenReturn(true);
    when(mockClient.getYsqlMajorCatalogUpgradeState())
        .thenReturn(
            new GetYsqlMajorCatalogUpgradeStateResponse(
                0L,
                null,
                null,
                YsqlMajorCatalogUpgradeState
                    .YSQL_MAJOR_CATALOG_UPGRADE_PENDING_FINALIZE_OR_ROLLBACK));
    mockDBServerVersion(
        baseVersion,
        targetVersion,
        defaultUniverse.getMasters().size() + defaultUniverse.getTServers().size());

    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = targetVersion;
    taskParams.upgradeOption = UpgradeOption.NON_ROLLING_UPGRADE;
    taskParams.clusters.add(defaultUniverse.getUniverseDetails().getPrimaryCluster());

    TaskInfo taskInfo = submitTask(taskParams, defaultUniverse.getVersion());

    MockUpgrade mockUpgrade = initMockUpgrade();
    mockUpgrade
        .precheckTasks(getPrecheckTasks(false))
        .addTasks(TaskType.UpdateUniverseState)
        .addTasks(TaskType.DisablePitrConfig)
        .addSimultaneousTasks(TaskType.AnsibleConfigureServers, defaultUniverse.getMasters().size())
        .addSimultaneousTasks(
            TaskType.AnsibleConfigureServers, defaultUniverse.getTServers().size())
        .addTasks(TaskType.XClusterInfoPersist)
        .addTasks(TaskType.StoreAutoFlagConfigVersion)
        .addTasks(TaskType.SaveSoftwareUpgradeProgress)
        .addSimultaneousTasks(
            TaskType.AnsibleConfigureServers, defaultUniverse.getTServers().size())
        .addSimultaneousTasks(TaskType.SetFlagInMemory, defaultUniverse.getMasters().size())
        .addSimultaneousTasks(TaskType.SetFlagInMemory, defaultUniverse.getTServers().size())
        .addSimultaneousTasks(TaskType.AnsibleConfigureServers, defaultUniverse.getMasters().size())
        .addSimultaneousTasks(
            TaskType.AnsibleConfigureServers, defaultUniverse.getTServers().size())
        .addTasks(TaskType.SaveSoftwareUpgradeProgress)
        .upgradeRound(UpgradeOption.NON_ROLLING_UPGRADE)
        .withContext(
            UpgradeTaskBase.UpgradeContext.builder()
                .reconfigureMaster(false)
                .runBeforeStopping(false)
                .processInactiveMaster(true)
                .targetSoftwareVersion(targetVersion)
                .build())
        .task(TaskType.AnsibleConfigureServers)
        .applyToMasters()
        .addTasks(TaskType.SaveSoftwareUpgradeProgress)
        .addTasks(TaskType.UpdateSoftwareUpdatePrevConfig)
        .addTasks(TaskType.RunYsqlMajorVersionCatalogUpgrade)
        .addTasks(TaskType.SaveSoftwareUpgradeProgress)
        .upgradeRound(UpgradeOption.NON_ROLLING_UPGRADE)
        .withContext(
            UpgradeTaskBase.UpgradeContext.builder()
                .reconfigureMaster(false)
                .runBeforeStopping(false)
                .processInactiveMaster(true)
                .targetSoftwareVersion(targetVersion)
                .build())
        .task(TaskType.AnsibleConfigureServers)
        .applyToTservers()
        .addTasks(TaskType.SaveSoftwareUpgradeProgress)
        .addTasks(TaskType.UpdateSoftwareUpdatePrevConfig)
        .addSimultaneousTasks(TaskType.SetFlagInMemory, defaultUniverse.getMasters().size())
        .addSimultaneousTasks(TaskType.SetFlagInMemory, defaultUniverse.getTServers().size())
        .addSimultaneousTasks(TaskType.AnsibleConfigureServers, defaultUniverse.getMasters().size())
        .addSimultaneousTasks(
            TaskType.AnsibleConfigureServers, defaultUniverse.getTServers().size())
        .addSimultaneousTasks(TaskType.CheckSoftwareVersion, defaultUniverse.getTServers().size())
        .addTasks(TaskType.PromoteAutoFlags)
        .addTasks(TaskType.UpdateSoftwareVersion)
        .verifyTasks(taskInfo.getSubTasks());

    assertEquals(100.0, taskInfo.getPercentCompleted(), 0);
    assertEquals(Success, taskInfo.getTaskState());
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertTrue(defaultUniverse.getUniverseDetails().isSoftwareRollbackAllowed);
    assertEquals(
        baseVersion,
        defaultUniverse.getUniverseDetails().prevYBSoftwareConfig.getSoftwareVersion());
  }

  @Test
  public void testYsqlMajorVersionSoftwareUpgradeOnDedicatedNodeUniverse() throws Exception {
    String baseVersion = "2024.2.2.0-b1";
    String targetVersion = "2025.1.0.0-b1";
    this.ysqlMajorUpgrade = true;

    // Update universe details to set dedicated node, version and auth settings.
    UniverseDefinitionTaskParams details = defaultUniverse.getUniverseDetails();
    UniverseDefinitionTaskParams.UserIntent userIntent = details.getPrimaryCluster().userIntent;
    userIntent.ybSoftwareVersion = baseVersion;
    userIntent.enableYSQLAuth = true;
    userIntent.dedicatedNodes = true;
    details.upsertPrimaryCluster(userIntent, null, null);
    defaultUniverse.setUniverseDetails(details);
    defaultUniverse.save();

    when(mockSoftwareUpgradeHelper.checkUpgradeRequireFinalize(anyString(), anyString()))
        .thenReturn(true);
    when(mockSoftwareUpgradeHelper.isSuperUserRequiredForCatalogUpgrade(
            any(), anyString(), anyString()))
        .thenReturn(true);
    when(mockSoftwareUpgradeHelper.isYsqlMajorVersionUpgradeRequired(
            any(), anyString(), anyString()))
        .thenReturn(true);
    ReleaseManager.ReleaseMetadata rm =
        ReleaseManager.ReleaseMetadata.create(targetVersion)
            .withFilePath("yugabyte-" + targetVersion + "-centos-x86_64" + ".tar.gz");
    when(mockReleaseManager.getReleaseByVersion(anyString()))
        .thenReturn(new ReleaseContainer(rm, mockCloudUtilFactory, mockConfig, mockReleasesUtils));
    when(mockClient.setFlag(any(), anyString(), anyString(), anyBoolean())).thenReturn(true);
    when(mockClient.getYsqlMajorCatalogUpgradeState())
        .thenReturn(
            new GetYsqlMajorCatalogUpgradeStateResponse(
                0L,
                null,
                null,
                YsqlMajorCatalogUpgradeState
                    .YSQL_MAJOR_CATALOG_UPGRADE_PENDING_FINALIZE_OR_ROLLBACK));
    mockDBServerVersion(
        baseVersion,
        targetVersion,
        defaultUniverse.getMasters().size() + defaultUniverse.getTServers().size());

    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = targetVersion;
    taskParams.upgradeOption = UpgradeOption.NON_ROLLING_UPGRADE;
    taskParams.clusters.add(defaultUniverse.getUniverseDetails().getPrimaryCluster());

    TaskInfo taskInfo = submitTask(taskParams, defaultUniverse.getVersion());

    MockUpgrade mockUpgrade = initMockUpgrade();
    mockUpgrade
        .precheckTasks(getPrecheckTasks(false))
        .addTasks(TaskType.UpdateUniverseState)
        .addTasks(TaskType.DisablePitrConfig)
        .addSimultaneousTasks(TaskType.AnsibleConfigureServers, defaultUniverse.getMasters().size())
        .addSimultaneousTasks(
            TaskType.AnsibleConfigureServers, defaultUniverse.getTServers().size())
        .addTasks(TaskType.XClusterInfoPersist)
        .addTasks(TaskType.StoreAutoFlagConfigVersion)
        .addTasks(TaskType.SaveSoftwareUpgradeProgress)
        .addSimultaneousTasks(
            TaskType.AnsibleConfigureServers, defaultUniverse.getTServers().size())
        .addSimultaneousTasks(TaskType.SetFlagInMemory, defaultUniverse.getMasters().size())
        .addSimultaneousTasks(TaskType.SetFlagInMemory, defaultUniverse.getTServers().size())
        .addSimultaneousTasks(TaskType.AnsibleConfigureServers, defaultUniverse.getMasters().size())
        .addSimultaneousTasks(
            TaskType.AnsibleConfigureServers, defaultUniverse.getTServers().size())
        .addTasks(TaskType.ManageCatalogUpgradeSuperUser)
        .addTasks(TaskType.SaveSoftwareUpgradeProgress)
        .upgradeRound(UpgradeOption.NON_ROLLING_UPGRADE)
        .withContext(
            UpgradeTaskBase.UpgradeContext.builder()
                .reconfigureMaster(false)
                .runBeforeStopping(false)
                .processInactiveMaster(true)
                .targetSoftwareVersion(targetVersion)
                .build())
        .task(TaskType.AnsibleConfigureServers)
        .applyToMasters()
        .addTasks(TaskType.SaveSoftwareUpgradeProgress)
        .addTasks(TaskType.UpdateSoftwareUpdatePrevConfig)
        .addTasks(TaskType.ManageCatalogUpgradeSuperUser)
        .addTasks(TaskType.RunYsqlMajorVersionCatalogUpgrade)
        .addTasks(TaskType.ManageCatalogUpgradeSuperUser)
        .addTasks(TaskType.SaveSoftwareUpgradeProgress)
        .upgradeRound(UpgradeOption.NON_ROLLING_UPGRADE)
        .withContext(
            UpgradeTaskBase.UpgradeContext.builder()
                .reconfigureMaster(false)
                .runBeforeStopping(false)
                .processInactiveMaster(true)
                .targetSoftwareVersion(targetVersion)
                .build())
        .task(TaskType.AnsibleConfigureServers)
        .applyToTservers()
        .addTasks(TaskType.SaveSoftwareUpgradeProgress)
        .addTasks(TaskType.UpdateSoftwareUpdatePrevConfig)
        .addSimultaneousTasks(TaskType.SetFlagInMemory, defaultUniverse.getMasters().size())
        .addSimultaneousTasks(TaskType.SetFlagInMemory, defaultUniverse.getTServers().size())
        .addSimultaneousTasks(TaskType.AnsibleConfigureServers, defaultUniverse.getMasters().size())
        .addSimultaneousTasks(
            TaskType.AnsibleConfigureServers, defaultUniverse.getTServers().size())
        .addSimultaneousTasks(TaskType.CheckSoftwareVersion, defaultUniverse.getTServers().size())
        .addTasks(TaskType.PromoteAutoFlags)
        .addTasks(TaskType.UpdateSoftwareVersion)
        .verifyTasks(taskInfo.getSubTasks());

    assertEquals(100.0, taskInfo.getPercentCompleted(), 0);
    assertEquals(Success, taskInfo.getTaskState());
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertTrue(defaultUniverse.getUniverseDetails().isSoftwareRollbackAllowed);
    assertEquals(
        baseVersion,
        defaultUniverse.getUniverseDetails().prevYBSoftwareConfig.getSoftwareVersion());
  }

  @Test
  public void testYsqlMajorVersionSoftwareUpgradeFailureRetry() throws Exception {
    String baseVersion = "2024.2.2.0-b1";
    String targetVersion = "2025.1.0.0-b1";
    this.ysqlMajorUpgrade = true;

    // Update universe details to set dedicated node, version and auth settings.
    UniverseDefinitionTaskParams details = defaultUniverse.getUniverseDetails();
    UniverseDefinitionTaskParams.UserIntent userIntent = details.getPrimaryCluster().userIntent;
    userIntent.ybSoftwareVersion = baseVersion;
    userIntent.enableYSQLAuth = true;
    userIntent.dedicatedNodes = true;
    details.upsertPrimaryCluster(userIntent, null, null);
    defaultUniverse.setUniverseDetails(details);
    defaultUniverse.save();

    when(mockSoftwareUpgradeHelper.checkUpgradeRequireFinalize(anyString(), anyString()))
        .thenReturn(true);
    when(mockSoftwareUpgradeHelper.isSuperUserRequiredForCatalogUpgrade(
            any(), anyString(), anyString()))
        .thenReturn(true);
    when(mockSoftwareUpgradeHelper.isAllMasterUpgradedToYsqlMajorVersion(any(), anyString()))
        .thenReturn(true);
    when(mockSoftwareUpgradeHelper.getYsqlMajorCatalogUpgradeState(any()))
        .thenReturn(YsqlMajorCatalogUpgradeState.YSQL_MAJOR_CATALOG_UPGRADE_PENDING_ROLLBACK);
    when(mockSoftwareUpgradeHelper.isYsqlMajorVersionUpgradeRequired(
            any(), anyString(), anyString()))
        .thenReturn(true);

    ReleaseManager.ReleaseMetadata rm =
        ReleaseManager.ReleaseMetadata.create(targetVersion)
            .withFilePath("yugabyte-" + targetVersion + "-centos-x86_64" + ".tar.gz");
    when(mockReleaseManager.getReleaseByVersion(anyString()))
        .thenReturn(new ReleaseContainer(rm, mockCloudUtilFactory, mockConfig, mockReleasesUtils));
    when(mockClient.setFlag(any(), anyString(), anyString(), anyBoolean())).thenReturn(true);
    when(mockClient.getYsqlMajorCatalogUpgradeState())
        .thenReturn(
            new GetYsqlMajorCatalogUpgradeStateResponse(
                0L, null, null, YsqlMajorCatalogUpgradeState.YSQL_MAJOR_CATALOG_UPGRADE_PENDING))
        .thenReturn(
            new GetYsqlMajorCatalogUpgradeStateResponse(
                0L, null, null, YsqlMajorCatalogUpgradeState.YSQL_MAJOR_CATALOG_UPGRADE_PENDING))
        .thenReturn(
            new GetYsqlMajorCatalogUpgradeStateResponse(
                0L,
                null,
                null,
                YsqlMajorCatalogUpgradeState
                    .YSQL_MAJOR_CATALOG_UPGRADE_PENDING_FINALIZE_OR_ROLLBACK));
    when(mockClient.rollbackYsqlMajorCatalogVersion())
        .thenReturn(new RollbackYsqlMajorCatalogVersionResponse(0L, null, null));
    mockDBServerVersion(
        baseVersion,
        targetVersion,
        defaultUniverse.getMasters().size() + defaultUniverse.getTServers().size());

    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = targetVersion;
    taskParams.upgradeOption = UpgradeOption.NON_ROLLING_UPGRADE;
    taskParams.clusters.add(defaultUniverse.getUniverseDetails().getPrimaryCluster());

    TaskInfo taskInfo = submitTask(taskParams, defaultUniverse.getVersion());

    MockUpgrade mockUpgrade = initMockUpgrade();
    mockUpgrade
        .precheckTasks(getPrecheckTasks(false))
        .addTasks(TaskType.UpdateUniverseState)
        .addTasks(TaskType.DisablePitrConfig)
        .addSimultaneousTasks(TaskType.AnsibleConfigureServers, defaultUniverse.getMasters().size())
        .addSimultaneousTasks(
            TaskType.AnsibleConfigureServers, defaultUniverse.getTServers().size())
        .addTasks(TaskType.XClusterInfoPersist)
        .addTasks(TaskType.StoreAutoFlagConfigVersion)
        .addTasks(TaskType.SaveSoftwareUpgradeProgress)
        .addTasks(TaskType.RollbackYsqlMajorVersionCatalogUpgrade)
        .addSimultaneousTasks(TaskType.AnsibleConfigureServers, defaultUniverse.getMasters().size())
        .upgradeRound(UpgradeOption.NON_ROLLING_UPGRADE)
        .withContext(
            UpgradeTaskBase.UpgradeContext.builder()
                .reconfigureMaster(false)
                .runBeforeStopping(false)
                .processInactiveMaster(true)
                .targetSoftwareVersion(targetVersion)
                .build())
        .task(TaskType.AnsibleConfigureServers)
        .applyToMasters()
        .addSimultaneousTasks(
            TaskType.AnsibleConfigureServers, defaultUniverse.getTServers().size())
        .addSimultaneousTasks(TaskType.SetFlagInMemory, defaultUniverse.getMasters().size())
        .addSimultaneousTasks(TaskType.SetFlagInMemory, defaultUniverse.getTServers().size())
        .addSimultaneousTasks(TaskType.AnsibleConfigureServers, defaultUniverse.getMasters().size())
        .addSimultaneousTasks(
            TaskType.AnsibleConfigureServers, defaultUniverse.getTServers().size())
        .addTasks(TaskType.ManageCatalogUpgradeSuperUser)
        .addTasks(TaskType.SaveSoftwareUpgradeProgress)
        .upgradeRound(UpgradeOption.NON_ROLLING_UPGRADE)
        .withContext(
            UpgradeTaskBase.UpgradeContext.builder()
                .reconfigureMaster(false)
                .runBeforeStopping(false)
                .processInactiveMaster(true)
                .targetSoftwareVersion(targetVersion)
                .build())
        .task(TaskType.AnsibleConfigureServers)
        .applyToMasters()
        .addTasks(TaskType.SaveSoftwareUpgradeProgress)
        .addTasks(TaskType.UpdateSoftwareUpdatePrevConfig)
        .addTasks(TaskType.ManageCatalogUpgradeSuperUser)
        .addTasks(TaskType.RunYsqlMajorVersionCatalogUpgrade)
        .addTasks(TaskType.ManageCatalogUpgradeSuperUser)
        .addTasks(TaskType.SaveSoftwareUpgradeProgress)
        .upgradeRound(UpgradeOption.NON_ROLLING_UPGRADE)
        .withContext(
            UpgradeTaskBase.UpgradeContext.builder()
                .reconfigureMaster(false)
                .runBeforeStopping(false)
                .processInactiveMaster(true)
                .targetSoftwareVersion(targetVersion)
                .build())
        .task(TaskType.AnsibleConfigureServers)
        .applyToTservers()
        .addTasks(TaskType.SaveSoftwareUpgradeProgress)
        .addTasks(TaskType.UpdateSoftwareUpdatePrevConfig)
        .addSimultaneousTasks(TaskType.SetFlagInMemory, defaultUniverse.getMasters().size())
        .addSimultaneousTasks(TaskType.SetFlagInMemory, defaultUniverse.getTServers().size())
        .addSimultaneousTasks(TaskType.AnsibleConfigureServers, defaultUniverse.getMasters().size())
        .addSimultaneousTasks(
            TaskType.AnsibleConfigureServers, defaultUniverse.getTServers().size())
        .addSimultaneousTasks(TaskType.CheckSoftwareVersion, defaultUniverse.getTServers().size())
        .addTasks(TaskType.PromoteAutoFlags)
        .addTasks(TaskType.UpdateSoftwareVersion)
        .verifyTasks(taskInfo.getSubTasks());

    assertEquals(100.0, taskInfo.getPercentCompleted(), 0);
    assertEquals(Success, taskInfo.getTaskState());
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertTrue(defaultUniverse.getUniverseDetails().isSoftwareRollbackAllowed);
    assertEquals(
        baseVersion,
        defaultUniverse.getUniverseDetails().prevYBSoftwareConfig.getSoftwareVersion());
  }

  @Test
  public void testYsqlMajorVersionSoftwareUpgradeAllMastersPendingRetry() throws Exception {
    String baseVersion = "2024.2.2.0-b1";
    String targetVersion = "2025.1.0.0-b1";
    this.ysqlMajorUpgrade = true;

    // Dedicated node + auth: superuser/.pgpass path applies. Abort after last master leaves all
    // masters on PG15 with catalog still PENDING - retry must roll masters back before CREATE USER.
    UniverseDefinitionTaskParams details = defaultUniverse.getUniverseDetails();
    UniverseDefinitionTaskParams.UserIntent userIntent = details.getPrimaryCluster().userIntent;
    userIntent.ybSoftwareVersion = baseVersion;
    userIntent.enableYSQLAuth = true;
    userIntent.dedicatedNodes = true;
    details.upsertPrimaryCluster(userIntent, null, null);
    defaultUniverse.setUniverseDetails(details);
    defaultUniverse.save();

    when(mockSoftwareUpgradeHelper.checkUpgradeRequireFinalize(anyString(), anyString()))
        .thenReturn(true);
    when(mockSoftwareUpgradeHelper.isSuperUserRequiredForCatalogUpgrade(
            any(), anyString(), anyString()))
        .thenReturn(true);
    when(mockSoftwareUpgradeHelper.isAllMasterUpgradedToYsqlMajorVersion(any(), anyString()))
        .thenReturn(true);
    when(mockSoftwareUpgradeHelper.getYsqlMajorCatalogUpgradeState(any()))
        .thenReturn(YsqlMajorCatalogUpgradeState.YSQL_MAJOR_CATALOG_UPGRADE_PENDING);
    when(mockSoftwareUpgradeHelper.isYsqlMajorVersionUpgradeRequired(
            any(), anyString(), anyString()))
        .thenReturn(true);

    ReleaseManager.ReleaseMetadata rm =
        ReleaseManager.ReleaseMetadata.create(targetVersion)
            .withFilePath("yugabyte-" + targetVersion + "-centos-x86_64" + ".tar.gz");
    when(mockReleaseManager.getReleaseByVersion(anyString()))
        .thenReturn(new ReleaseContainer(rm, mockCloudUtilFactory, mockConfig, mockReleasesUtils));
    when(mockClient.setFlag(any(), anyString(), anyString(), anyBoolean())).thenReturn(true);
    when(mockClient.getYsqlMajorCatalogUpgradeState())
        .thenReturn(
            new GetYsqlMajorCatalogUpgradeStateResponse(
                0L,
                null,
                null,
                YsqlMajorCatalogUpgradeState
                    .YSQL_MAJOR_CATALOG_UPGRADE_PENDING_FINALIZE_OR_ROLLBACK));
    mockDBServerVersion(
        baseVersion,
        targetVersion,
        defaultUniverse.getMasters().size() + defaultUniverse.getTServers().size());

    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = targetVersion;
    taskParams.upgradeOption = UpgradeOption.NON_ROLLING_UPGRADE;
    taskParams.clusters.add(defaultUniverse.getUniverseDetails().getPrimaryCluster());

    TaskInfo taskInfo = submitTask(taskParams, defaultUniverse.getVersion());

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    int createUserIdx = -1;
    for (int i = 0; i < subTasks.size(); i++) {
      if (subTasks.get(i).getTaskType() == TaskType.ManageCatalogUpgradeSuperUser) {
        createUserIdx = i;
        break;
      }
    }
    assertTrue(
        "ManageCatalogUpgradeSuperUser should run for dedicated-node YSQL major upgrade",
        createUserIdx >= 0);

    boolean masterConfigureBeforeCreateUser = false;
    for (int i = 0; i < createUserIdx; i++) {
      if (subTasks.get(i).getTaskType() == TaskType.AnsibleConfigureServers) {
        masterConfigureBeforeCreateUser = true;
        break;
      }
    }
    assertTrue(
        "Master revert (AnsibleConfigureServers) must run before ManageCatalogUpgradeSuperUser",
        masterConfigureBeforeCreateUser);

    for (int i = 0; i < createUserIdx; i++) {
      assertFalse(
          "Catalog rollback must not run when catalog state is PENDING (not PENDING_ROLLBACK)",
          subTasks.get(i).getTaskType() == TaskType.RollbackYsqlMajorVersionCatalogUpgrade);
    }

    MockUpgrade mockUpgrade = initMockUpgrade();
    mockUpgrade
        .precheckTasks(getPrecheckTasks(false))
        .addTasks(TaskType.UpdateUniverseState)
        .addTasks(TaskType.DisablePitrConfig)
        .addSimultaneousTasks(TaskType.AnsibleConfigureServers, defaultUniverse.getMasters().size())
        .addSimultaneousTasks(
            TaskType.AnsibleConfigureServers, defaultUniverse.getTServers().size())
        .addTasks(TaskType.XClusterInfoPersist)
        .addTasks(TaskType.StoreAutoFlagConfigVersion)
        .addTasks(TaskType.SaveSoftwareUpgradeProgress)
        .addSimultaneousTasks(TaskType.AnsibleConfigureServers, defaultUniverse.getMasters().size())
        .upgradeRound(UpgradeOption.NON_ROLLING_UPGRADE)
        .withContext(
            UpgradeTaskBase.UpgradeContext.builder()
                .reconfigureMaster(false)
                .runBeforeStopping(false)
                .processInactiveMaster(true)
                .targetSoftwareVersion(baseVersion)
                .build())
        .task(TaskType.AnsibleConfigureServers)
        .applyToMasters()
        .addSimultaneousTasks(
            TaskType.AnsibleConfigureServers, defaultUniverse.getTServers().size())
        .addSimultaneousTasks(TaskType.SetFlagInMemory, defaultUniverse.getMasters().size())
        .addSimultaneousTasks(TaskType.SetFlagInMemory, defaultUniverse.getTServers().size())
        .addSimultaneousTasks(TaskType.AnsibleConfigureServers, defaultUniverse.getMasters().size())
        .addSimultaneousTasks(
            TaskType.AnsibleConfigureServers, defaultUniverse.getTServers().size())
        .addTasks(TaskType.ManageCatalogUpgradeSuperUser)
        .addTasks(TaskType.SaveSoftwareUpgradeProgress)
        .upgradeRound(UpgradeOption.NON_ROLLING_UPGRADE)
        .withContext(
            UpgradeTaskBase.UpgradeContext.builder()
                .reconfigureMaster(false)
                .runBeforeStopping(false)
                .processInactiveMaster(true)
                .targetSoftwareVersion(targetVersion)
                .build())
        .task(TaskType.AnsibleConfigureServers)
        .applyToMasters()
        .addTasks(TaskType.SaveSoftwareUpgradeProgress)
        .addTasks(TaskType.UpdateSoftwareUpdatePrevConfig)
        .addTasks(TaskType.ManageCatalogUpgradeSuperUser)
        .addTasks(TaskType.RunYsqlMajorVersionCatalogUpgrade)
        .addTasks(TaskType.ManageCatalogUpgradeSuperUser)
        .addTasks(TaskType.SaveSoftwareUpgradeProgress)
        .upgradeRound(UpgradeOption.NON_ROLLING_UPGRADE)
        .withContext(
            UpgradeTaskBase.UpgradeContext.builder()
                .reconfigureMaster(false)
                .runBeforeStopping(false)
                .processInactiveMaster(true)
                .targetSoftwareVersion(targetVersion)
                .build())
        .task(TaskType.AnsibleConfigureServers)
        .applyToTservers()
        .addTasks(TaskType.SaveSoftwareUpgradeProgress)
        .addTasks(TaskType.UpdateSoftwareUpdatePrevConfig)
        .addSimultaneousTasks(TaskType.SetFlagInMemory, defaultUniverse.getMasters().size())
        .addSimultaneousTasks(TaskType.SetFlagInMemory, defaultUniverse.getTServers().size())
        .addSimultaneousTasks(TaskType.AnsibleConfigureServers, defaultUniverse.getMasters().size())
        .addSimultaneousTasks(
            TaskType.AnsibleConfigureServers, defaultUniverse.getTServers().size())
        .addSimultaneousTasks(TaskType.CheckSoftwareVersion, defaultUniverse.getTServers().size())
        .addTasks(TaskType.PromoteAutoFlags)
        .addTasks(TaskType.UpdateSoftwareVersion)
        .verifyTasks(taskInfo.getSubTasks());

    assertEquals(100.0, taskInfo.getPercentCompleted(), 0);
    assertEquals(Success, taskInfo.getTaskState());
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertTrue(defaultUniverse.getUniverseDetails().isSoftwareRollbackAllowed);
    assertEquals(
        baseVersion,
        defaultUniverse.getUniverseDetails().prevYBSoftwareConfig.getSoftwareVersion());
  }

  @Test
  public void testSoftwareUpgradeRetries() {
    factory.globalRuntimeConf().setValue("yb.checks.leaderless_tablets.enabled", "false");
    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = "2.21.0.0-b2";
    taskParams.expectedUniverseVersion = -1;
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.clusters.add(defaultUniverse.getUniverseDetails().getPrimaryCluster());
    taskParams.creatingUser = defaultUser;
    taskParams.sleepAfterMasterRestartMillis = 0;
    taskParams.sleepAfterTServerRestartMillis = 0;
    TestUtils.setFakeHttpContext(defaultUser);
    super.verifyTaskRetries(
        defaultCustomer,
        CustomerTask.TaskType.SoftwareUpgrade,
        CustomerTask.TargetType.Universe,
        defaultUniverse.getUniverseUUID(),
        TaskType.SoftwareUpgradeYB,
        taskParams,
        false);
    checkUniverseNodesStates(taskParams.getUniverseUUID());
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertTrue(defaultUniverse.getUniverseDetails().isSoftwareRollbackAllowed);
    assertEquals(
        SoftwareUpgradeState.Ready, defaultUniverse.getUniverseDetails().softwareUpgradeState);
  }

  @Test
  @Parameters({"false", "true"})
  public void testRollbackRejectedWhenPrecheckFails(boolean previousUpgradeRollbackAllowed) {
    if (previousUpgradeRollbackAllowed) {
      // Left by an earlier upgrade that did not need finalize; rolling back the failed task would
      // downgrade that upgrade.
      PrevYBSoftwareConfig prev = new PrevYBSoftwareConfig();
      prev.setSoftwareVersion("2.20.0.0-b1");
      defaultUniverse =
          Universe.saveDetails(
              defaultUniverse.getUniverseUUID(),
              u -> {
                UniverseDefinitionTaskParams details = u.getUniverseDetails();
                details.isSoftwareRollbackAllowed = true;
                details.prevYBSoftwareConfig = prev;
                u.setUniverseDetails(details);
              });
    }
    // Above the mocked available memory, so CheckMemory fails before FreezeUniverse.
    factory
        .forUniverse(defaultUniverse)
        .setValue(UniverseConfKeys.dbMemAvailableLimit.getKey(), "4000000");
    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = "2.21.0.0-b2";
    taskParams.clusters.add(defaultUniverse.getUniverseDetails().getPrimaryCluster());
    TaskInfo taskInfo = submitTask(taskParams, defaultUniverse.getVersion());

    assertEquals(TaskInfo.State.Failure, taskInfo.getTaskState());
    assertTrue(
        taskInfo.getSubTasks().stream()
            .anyMatch(
                t ->
                    t.getTaskType() == TaskType.CheckMemory
                        && t.getTaskState() == TaskInfo.State.Failure));
    assertTaskNeverOwnedUniverse(taskInfo);
    assertRollbackRejected(createCustomerTask(taskInfo));
  }

  @Test
  public void testRollbackRejectedWhenAbortedBeforeFreeze() {
    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = "2.21.0.0-b2";
    taskParams.clusters.add(defaultUniverse.getUniverseDetails().getPrimaryCluster());
    TaskInfo taskInfo;
    try {
      setAbortPosition(0);
      taskInfo = submitTask(taskParams, defaultUniverse.getVersion());
    } finally {
      clearAbortOrPausePositions();
    }

    assertEquals(TaskInfo.State.Aborted, taskInfo.getTaskState());
    assertTaskNeverOwnedUniverse(taskInfo);
    assertRollbackRejected(createCustomerTask(taskInfo));
  }

  @Test
  public void testRollbackOfferedWhenAbortedBeforeUpgradeStarts() throws InterruptedException {
    TaskInfo taskInfo = abortUpgradeAroundUpgradingState(false);

    assertEquals(TaskInfo.State.Aborted, taskInfo.getTaskState());
    UniverseDefinitionTaskParams details =
        Universe.getOrBadRequest(defaultUniverse.getUniverseUUID()).getUniverseDetails();
    assertEquals(taskInfo.getUuid(), details.updatingTaskUUID);
    assertEquals(taskInfo.getUuid(), details.placementModificationTaskUuid);
    assertEquals(SoftwareUpgradeState.Ready, details.softwareUpgradeState);
    assertFalse(details.isSoftwareRollbackAllowed);
    CustomerTask customerTask = createCustomerTask(taskInfo);
    assertTrue(customerTaskManager.isTaskRetryable(customerTask, taskInfo));
    // The task owns the universe, so rollback is offered; the rollback's precheck refuses it
    // because this upgrade never set isSoftwareRollbackAllowed.
    assertTrue(listingCanRollback(customerTask));
    assertTrue(commissioner.canTaskRollbackDetailed(taskInfo));
    PlatformServiceException ex =
        assertThrows(
            PlatformServiceException.class,
            () ->
                customerTaskManager.rollbackCustomerTask(
                    defaultCustomer.getUuid(), taskInfo.getUuid()));
    assertThat(ex.getMessage(), containsString("previous upgrade was finalized"));
  }

  @Test
  public void testRollbackAllowedWhenAbortedAfterUpgradeStarts() throws InterruptedException {
    String oldVersion = primaryVersion();
    TaskInfo taskInfo = abortUpgradeAroundUpgradingState(true);

    assertEquals(TaskInfo.State.Aborted, taskInfo.getTaskState());
    UniverseDefinitionTaskParams details =
        Universe.getOrBadRequest(defaultUniverse.getUniverseUUID()).getUniverseDetails();
    assertEquals(SoftwareUpgradeState.UpgradeFailed, details.softwareUpgradeState);
    assertTrue(details.isSoftwareRollbackAllowed);
    CustomerTask customerTask = createCustomerTask(taskInfo);
    assertTrue(listingCanRollback(customerTask));
    assertTrue(commissioner.canTaskRollbackDetailed(taskInfo));
    assertRollbackSubmitted(customerTask, oldVersion);
  }

  @Test
  @Parameters({"true, false", "false, false", "true, true", "false, true"})
  public void testAbortBeforeVersionUpdateFailsUpgradeAndRetryCompletes(
      boolean requireFinalize, boolean canary) throws InterruptedException {
    if (canary) {
      updateDefaultUniverseTo5Nodes(true);
    }
    when(mockSoftwareUpgradeHelper.checkUpgradeRequireFinalize(anyString(), anyString()))
        .thenReturn(requireFinalize);
    // PLAT-23182: abort just before UpdateSoftwareVersion, the last upgrade subtask, which also
    // saves the end state (PreFinalize, or Ready with no finalize). A canary retry resumes from
    // the saved AZ progress when finalize is required; otherwise that progress is already cleared.
    String oldVersion = primaryVersion();
    TaskInfo taskInfo =
        abortUpgradeAfterSubTask(
            taskUuid -> firstSubTaskPosition(taskUuid, TaskType.UpdateSoftwareVersion),
            canary ? this::setNoPauseCanaryConfig : params -> {});

    assertEquals(TaskInfo.State.Aborted, taskInfo.getTaskState());
    UniverseDefinitionTaskParams details =
        Universe.getOrBadRequest(defaultUniverse.getUniverseUUID()).getUniverseDetails();
    assertEquals(SoftwareUpgradeState.UpgradeFailed, details.softwareUpgradeState);
    assertEquals(oldVersion, primaryVersion());
    assertTrue(details.isSoftwareRollbackAllowed);
    CustomerTask customerTask = createCustomerTask(taskInfo);
    assertTrue(listingCanRollback(customerTask));
    assertTrue(commissioner.canTaskRollbackDetailed(taskInfo));

    // Every node already runs the new version, so the retry only has to finish the upgrade.
    String newVersion = "2.21.0.0-b2";
    mockDBServerVersion(
        newVersion, defaultUniverse.getMasters().size() + defaultUniverse.getTServers().size());
    CustomerTask retry =
        customerTaskManager.retryCustomerTask(defaultCustomer.getUuid(), taskInfo.getUuid());
    TaskInfo retryTask = waitForTask(retry.getTaskUUID());

    assertEquals(Success, retryTask.getTaskState());
    details = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID()).getUniverseDetails();
    assertEquals(
        requireFinalize ? SoftwareUpgradeState.PreFinalize : SoftwareUpgradeState.Ready,
        details.softwareUpgradeState);
    assertEquals(newVersion, primaryVersion());
    if (requireFinalize) {
      // PLAT-23183: the retry upgrades no node, yet the progress kept for PreFinalize must still
      // show every AZ as upgraded.
      assertAllAzsCompleted(details.prevYBSoftwareConfig);
    }
  }

  @Test
  @Parameters({"false, false", "true, false", "false, true", "true, true"})
  public void testRetryKeepsUpgradedAzsCompleted(boolean readReplica, boolean canary)
      throws InterruptedException {
    updateDefaultUniverseTo5Nodes(true);
    if (readReplica) {
      // az1 is shared with the primary cluster, and its AZ progress is kept per cluster.
      addReadReplica(
          true,
          az1,
          AvailabilityZone.createOrThrow(region, "az-4", "AZ 4", "subnet-1"),
          AvailabilityZone.createOrThrow(region, "az-5", "AZ 5", "subnet-2"));
    }
    when(mockSoftwareUpgradeHelper.checkUpgradeRequireFinalize(anyString(), anyString()))
        .thenReturn(true);
    // PLAT-23183: abort the first run as the t-servers start. Both a standard upgrade and a canary
    // upgrade without AZ steps upgrade all t-servers in one step, between the last two progress
    // saves.
    TaskInfo taskInfo =
        abortUpgradeAfterSubTask(
            taskUuid -> {
              List<Integer> savePositions =
                  TaskInfo.getOrBadRequest(taskUuid).getSubTasks().stream()
                      .filter(t -> t.getTaskType() == TaskType.SaveSoftwareUpgradeProgress)
                      .map(TaskInfo::getPosition)
                      .sorted()
                      .collect(Collectors.toList());
              return savePositions.get(savePositions.size() - 2) + 1;
            },
            canary
                ? params -> params.canaryUpgradeConfig = new CanaryUpgradeConfig()
                : params -> {});
    assertEquals(TaskInfo.State.Aborted, taskInfo.getTaskState());
    createCustomerTask(taskInfo);

    // The retry finds every master and the az1 t-servers of each cluster on the new version, as
    // if the abort had come after them.
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    String oldVersion = primaryVersion();
    String newVersion = "2.21.0.0-b2";
    Set<String> upgradedTservers =
        defaultUniverse.getTServers().stream()
            .filter(n -> n.azUuid.equals(az1.getUuid()))
            .map(n -> n.cloudInfo.private_ip)
            .collect(Collectors.toSet());
    Set<String> checkedTservers = new HashSet<>();
    when(mockYBClient.getServerVersion(any(), anyString(), anyInt()))
        .thenAnswer(
            invocation -> {
              String ip = invocation.getArgument(1);
              int port = invocation.getArgument(2);
              boolean isMaster = port == 7100;
              // A pending t-server reports the old version only on the first check, before it is
              // upgraded.
              boolean pending =
                  !isMaster && !upgradedTservers.contains(ip) && checkedTservers.add(ip);
              return Optional.of(pending ? oldVersion : newVersion);
            });
    CustomerTask retry =
        customerTaskManager.retryCustomerTask(defaultCustomer.getUuid(), taskInfo.getUuid());
    TaskInfo retryTask = waitForTask(retry.getTaskUUID());
    assertEquals(Success, retryTask.getTaskState());

    Set<UUID> az1Clusters =
        defaultUniverse.getTServers().stream()
            .filter(n -> n.azUuid.equals(az1.getUuid()))
            .map(n -> n.placementUuid)
            .collect(Collectors.toSet());
    assertEquals(readReplica ? 2 : 1, az1Clusters.size());
    boolean sawAz2InProgress = false;
    for (TaskInfo subTask : retryTask.getSubTasks()) {
      if (subTask.getTaskType() != TaskType.SaveSoftwareUpgradeProgress) {
        continue;
      }
      SaveSoftwareUpgradeProgress.Params params =
          play.libs.Json.fromJson(
              subTask.getTaskParams(), SaveSoftwareUpgradeProgress.Params.class);
      assertTrue(
          "Masters were upgraded before the retry",
          params.masterAZUpgradeStatesList.stream()
              .allMatch(s -> s.getStatus() == AZUpgradeStatus.COMPLETED));
      for (AZUpgradeState s : params.tserverAZUpgradeStatesList) {
        if (s.getAzUUID().equals(az1.getUuid())) {
          assertEquals(
              "az1 t-servers were upgraded before the retry",
              AZUpgradeStatus.COMPLETED,
              s.getStatus());
        } else if (s.getAzUUID().equals(az2.getUuid())
            && s.getStatus() == AZUpgradeStatus.IN_PROGRESS) {
          sawAz2InProgress = true;
        }
      }
    }
    assertTrue("The retry upgrades the az2 t-servers", sawAz2InProgress);

    PrevYBSoftwareConfig prev =
        Universe.getOrBadRequest(defaultUniverse.getUniverseUUID())
            .getUniverseDetails()
            .prevYBSoftwareConfig;
    assertAllAzsCompleted(prev);
    assertEquals(
        az1Clusters,
        prev.getTserverAZUpgradeStatesList().stream()
            .filter(s -> s.getAzUUID().equals(az1.getUuid()))
            .map(AZUpgradeState::getClusterUUID)
            .collect(Collectors.toSet()));
  }

  /** Every master and t-server AZ in the saved progress is COMPLETED. */
  private static void assertAllAzsCompleted(PrevYBSoftwareConfig prev) {
    assertTrue("Masters not all completed", allMasterAzsCompleted(prev));
    assertFalse(prev.getTserverAZUpgradeStatesList().isEmpty());
    for (AZUpgradeState s : prev.getTserverAZUpgradeStatesList()) {
      assertEquals(
          "T-server AZ " + s.getAzName() + " of cluster " + s.getClusterUUID(),
          AZUpgradeStatus.COMPLETED,
          s.getStatus());
    }
  }

  @Test
  public void testAutoFinalizeAbortBeforeVersionUpdateFailsUpgradeAndRetryCompletes()
      throws InterruptedException {
    // PLAT-23182: with rollbackSupport=false the upgrade finalizes itself, then saves Ready, with
    // rollback disallowed, together with the version. An abort just before that must still leave
    // UpgradeFailed.
    mockRunYsqlUpgrade();
    String oldVersion = primaryVersion();
    TaskInfo taskInfo =
        abortUpgradeAfterSubTask(
            taskUuid -> firstSubTaskPosition(taskUuid, TaskType.UpdateSoftwareVersion),
            params -> params.rollbackSupport = false);

    assertEquals(TaskInfo.State.Aborted, taskInfo.getTaskState());
    UniverseDefinitionTaskParams details =
        Universe.getOrBadRequest(defaultUniverse.getUniverseUUID()).getUniverseDetails();
    assertEquals(SoftwareUpgradeState.UpgradeFailed, details.softwareUpgradeState);
    assertEquals(oldVersion, primaryVersion());
    assertNotNull(details.prevYBSoftwareConfig);

    String newVersion = "2.21.0.0-b2";
    mockDBServerVersion(
        newVersion, defaultUniverse.getMasters().size() + defaultUniverse.getTServers().size());
    createCustomerTask(taskInfo);
    CustomerTask retry =
        customerTaskManager.retryCustomerTask(defaultCustomer.getUuid(), taskInfo.getUuid());
    TaskInfo retryTask = waitForTask(retry.getTaskUUID());

    assertEquals(Success, retryTask.getTaskState());
    details = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID()).getUniverseDetails();
    assertEquals(SoftwareUpgradeState.Ready, details.softwareUpgradeState);
    assertFalse(details.isSoftwareRollbackAllowed);
    assertNull(details.prevYBSoftwareConfig);
    assertEquals(newVersion, primaryVersion());
  }

  /** Canary upgrade of az1-az3, one AZ per step, with no pause points. */
  private void setNoPauseCanaryConfig(SoftwareUpgradeParams taskParams) {
    taskParams.canaryUpgradeConfig = new CanaryUpgradeConfig();
    taskParams.canaryUpgradeConfig.pauseAfterMasters = false;
    taskParams.canaryUpgradeConfig.primaryClusterAZSteps = new ArrayList<>();
    for (UUID azUUID : Arrays.asList(az1.getUuid(), az2.getUuid(), az3.getUuid())) {
      AZUpgradeStep step = new AZUpgradeStep();
      step.azUUID = azUUID;
      step.pauseAfterTserverUpgrade = false;
      taskParams.canaryUpgradeConfig.primaryClusterAZSteps.add(step);
    }
  }

  /**
   * Runs an upgrade past FreezeUniverse and aborts it just before the Upgrading state update, or
   * just after it when {@code afterStateUpdate}.
   */
  private TaskInfo abortUpgradeAroundUpgradingState(boolean afterStateUpdate)
      throws InterruptedException {
    return abortUpgradeAfterSubTask(
        taskUuid ->
            firstSubTaskPosition(taskUuid, TaskType.UpdateUniverseState)
                + (afterStateUpdate ? 1 : 0));
  }

  /**
   * Runs an upgrade past FreezeUniverse, then aborts it at the subtask position {@code
   * abortPosition} computes from the created subtasks.
   */
  private TaskInfo abortUpgradeAfterSubTask(ToIntFunction<UUID> abortPosition)
      throws InterruptedException {
    return abortUpgradeAfterSubTask(abortPosition, params -> {});
  }

  /**
   * Same as {@link #abortUpgradeAfterSubTask(ToIntFunction)}, with {@code customizeParams} applied
   * to the upgrade params before submitting.
   */
  private TaskInfo abortUpgradeAfterSubTask(
      ToIntFunction<UUID> abortPosition, Consumer<SoftwareUpgradeParams> customizeParams)
      throws InterruptedException {
    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = "2.21.0.0-b2";
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = defaultUniverse.getVersion();
    taskParams.creatingUser = defaultUser;
    taskParams.clusters.add(defaultUniverse.getUniverseDetails().getPrimaryCluster());
    customizeParams.accept(taskParams);
    mockDBServerVersion(
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion,
        taskParams.ybSoftwareVersion,
        defaultUniverse.getMasters().size() + defaultUniverse.getTServers().size());
    try {
      setPausePosition(0);
      UUID taskUuid = commissioner.submit(TaskType.SoftwareUpgradeYB, taskParams);
      waitForTaskPaused(taskUuid);
      // The upgrade subtasks are created only after the freeze runs.
      setPausePosition(firstSubTaskPosition(taskUuid, TaskType.FreezeUniverse) + 1);
      commissioner.resumeTask(taskUuid);
      waitForTaskPaused(taskUuid);
      setAbortPosition(abortPosition.applyAsInt(taskUuid));
      commissioner.resumeTask(taskUuid);
      return waitForTask(taskUuid);
    } finally {
      clearAbortOrPausePositions();
    }
  }

  private void assertTaskNeverOwnedUniverse(TaskInfo taskInfo) {
    UniverseDefinitionTaskParams details =
        Universe.getOrBadRequest(defaultUniverse.getUniverseUUID()).getUniverseDetails();
    assertNotEquals(taskInfo.getUuid(), details.updatingTaskUUID);
    assertNotEquals(taskInfo.getUuid(), details.placementModificationTaskUuid);
    assertEquals(SoftwareUpgradeState.Ready, details.softwareUpgradeState);
  }

  private CustomerTask createCustomerTask(TaskInfo taskInfo) {
    CustomerTask.create(
        defaultCustomer,
        defaultUniverse.getUniverseUUID(),
        taskInfo.getUuid(),
        CustomerTask.TargetType.Universe,
        CustomerTask.TaskType.SoftwareUpgrade,
        defaultUniverse.getName());
    // Re-fetch so the TaskInfo join is loaded.
    return CustomerTask.getOrBadRequest(defaultCustomer.getUuid(), taskInfo.getUuid());
  }

  /** Listing, the submit gate, and the rollback API must all refuse. */
  private void assertRollbackRejected(CustomerTask customerTask) {
    TaskInfo taskInfo = customerTask.getTaskInfo();
    assertFalse(listingCanRollback(customerTask));
    assertFalse(commissioner.canTaskRollbackDetailed(taskInfo));
    PlatformServiceException ex =
        assertThrows(
            PlatformServiceException.class,
            () ->
                customerTaskManager.rollbackCustomerTask(
                    defaultCustomer.getUuid(), taskInfo.getUuid()));
    assertThat(ex.getMessage(), containsString("cannot be rolled back"));
  }

  /**
   * The rollback API submits a downgrade to {@code oldVersion}. UpdateSoftwareVersion has not run
   * yet, so the universe's user intent still holds the old version when the rollback is computed.
   */
  private void assertRollbackSubmitted(CustomerTask customerTask, String oldVersion)
      throws InterruptedException {
    CustomerTask rollback =
        customerTaskManager.rollbackCustomerTask(
            defaultCustomer.getUuid(), customerTask.getTaskUUID());
    assertEquals(CustomerTask.TaskType.RollbackUpgrade, rollback.getType());
    TaskInfo rollbackTask = waitForTask(rollback.getTaskUUID());
    assertEquals(TaskType.RollbackUpgrade, rollbackTask.getTaskType());
    RollbackUpgradeParams params =
        play.libs.Json.fromJson(rollbackTask.getTaskParams(), RollbackUpgradeParams.class);
    assertEquals(oldVersion, params.ybSoftwareVersion);
  }

  private String primaryVersion() {
    return Universe.getOrBadRequest(defaultUniverse.getUniverseUUID())
        .getUniverseDetails()
        .getPrimaryCluster()
        .userIntent
        .ybSoftwareVersion;
  }

  /**
   * Listing {@code canRollback}, with the ownership map built from the universe in memory because
   * H2 cannot run the JSONB query the task list uses.
   */
  private boolean listingCanRollback(CustomerTask customerTask) {
    UniverseDefinitionTaskParams details =
        Universe.getOrBadRequest(customerTask.getTargetUUID()).getUniverseDetails();
    Set<String> owners = new HashSet<>();
    Set<String> placementLockOwners = new HashSet<>();
    if (details.updatingTaskUUID != null) {
      owners.add(details.updatingTaskUUID.toString());
    }
    if (details.placementModificationTaskUuid != null) {
      owners.add(details.placementModificationTaskUuid.toString());
      placementLockOwners.add(details.placementModificationTaskUuid.toString());
    }
    return commissioner
        .buildTaskStatus(
            customerTask,
            customerTask.getTaskInfo().getSubTasks(),
            ImmutableMap.of(customerTask.getTargetUUID(), owners),
            ImmutableMap.of(customerTask.getTargetUUID(), placementLockOwners),
            ImmutableMap.of(customerTask.getTargetUUID(), customerTask))
        .map(status -> status.get("canRollback").asBoolean())
        .orElseThrow();
  }

  @Test
  public void testPartialSoftwareUpgrade() {
    updateDefaultUniverseTo5Nodes(true);

    Set<String> mastersOriginallyUpdated = new HashSet<>();
    Set<String> tserversOriginallyUpdated = new HashSet<>();

    List<NodeDetails> masters = defaultUniverse.getMasters();
    NodeDetails onlyMasterUpdated = masters.get(0);
    mastersOriginallyUpdated.add(onlyMasterUpdated.cloudInfo.private_ip);
    NodeDetails bothUpdated = masters.get(1);
    mastersOriginallyUpdated.add(bothUpdated.cloudInfo.private_ip);
    tserversOriginallyUpdated.add(bothUpdated.cloudInfo.private_ip);

    List<NodeDetails> otherTservers =
        defaultUniverse.getTServers().stream()
            .filter(n -> !masters.contains(n))
            .collect(Collectors.toList());

    NodeDetails tserverUpdated = otherTservers.get(0);
    tserversOriginallyUpdated.add(tserverUpdated.cloudInfo.private_ip);

    NodeDetails tserverUpdatedButNotLive = otherTservers.get(1);
    tserversOriginallyUpdated.add(tserverUpdatedButNotLive.cloudInfo.private_ip);

    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            u -> {
              UniverseDefinitionTaskParams details = u.getUniverseDetails();
              u.getNode(tserverUpdatedButNotLive.getNodeName()).state =
                  NodeDetails.NodeState.UpgradeSoftware;
              u.setUniverseDetails(details);
            });

    Set<String> mastersUpdated = new HashSet<>(mastersOriginallyUpdated);
    Set<String> tserversUpdated = new HashSet<>(tserversOriginallyUpdated);

    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = "2.21.0.0-b2";
    taskParams.clusters.add(defaultUniverse.getUniverseDetails().getPrimaryCluster());

    String oldVersion =
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion;

    when(mockYBClient.getServerVersion(any(), anyString(), anyInt()))
        .thenAnswer(
            invocation -> {
              String ip = invocation.getArgument(1);
              int port = invocation.getArgument(2);
              boolean isMaster = port == 7100;
              Set<String> serversUpdated = isMaster ? mastersUpdated : tserversUpdated;
              Optional<String> result =
                  serversUpdated.add(ip)
                      ? Optional.of(oldVersion)
                      : Optional.of(taskParams.ybSoftwareVersion);
              return result;
            });

    TaskInfo taskInfo = submitTask(taskParams, defaultUniverse.getVersion());
    assertEquals(100.0, taskInfo.getPercentCompleted(), 0);
    assertEquals(Success, taskInfo.getTaskState());

    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());

    Set<String> configuredMasters =
        taskInfo.getSubTasks().stream()
            .filter(t -> t.getTaskType() == TaskType.AnsibleConfigureServers)
            .filter(t -> t.getTaskParams().get("type").asText().equals("Software"))
            .filter(
                t ->
                    t.getTaskParams()
                        .get("properties")
                        .get("processType")
                        .asText()
                        .equals("MASTER"))
            .map(t -> t.getTaskParams().get("nodeName").asText())
            .collect(Collectors.toSet());

    Set<String> configuredTservers =
        taskInfo.getSubTasks().stream()
            .filter(t -> t.getTaskType() == TaskType.AnsibleConfigureServers)
            .filter(t -> t.getTaskParams().get("type").asText().equals("Software"))
            .filter(
                t ->
                    t.getTaskParams()
                        .get("properties")
                        .get("processType")
                        .asText()
                        .equals("TSERVER"))
            .map(t -> t.getTaskParams().get("nodeName").asText())
            .collect(Collectors.toSet());

    Set<String> masterNames =
        defaultUniverse.getMasters().stream()
            .filter(n -> !mastersOriginallyUpdated.contains(n.cloudInfo.private_ip))
            .map(n -> n.nodeName)
            .collect(Collectors.toSet());
    Set<String> tserverNames =
        defaultUniverse.getTServers().stream()
            .filter(
                n ->
                    !tserversOriginallyUpdated.contains(n.cloudInfo.private_ip)
                        || n.nodeName.equals(tserverUpdatedButNotLive.nodeName))
            .map(n -> n.nodeName)
            .collect(Collectors.toSet());

    Set<String> expectedMasters = new HashSet<>(masterNames);
    // We do process inactive masters, so for each tserver we also process masters
    // (but not for "onlyMasterUpdated" node)
    expectedMasters.addAll(tserverNames);
    expectedMasters.remove(onlyMasterUpdated.getNodeName());

    assertEquals("Upgraded masters", expectedMasters, configuredMasters);
    assertEquals("Upgraded tservers", tserverNames, configuredTservers);

    // Re-mark node as upgrading to ensure order is correct.
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            u -> {
              UniverseDefinitionTaskParams details = u.getUniverseDetails();
              u.getNode(tserverUpdatedButNotLive.getNodeName()).state =
                  NodeDetails.NodeState.UpgradeSoftware;
              u.setUniverseDetails(details);
            });

    MockUpgrade mockUpgrade = initMockUpgrade();
    mockUpgrade
        .precheckTasks(
            TaskType.CheckUpgrade, TaskType.CheckMemory, TaskType.CheckLocale, TaskType.CheckGlibc)
        .addTasks(TaskType.UpdateUniverseState)
        .addSimultaneousTasks(TaskType.AnsibleConfigureServers, defaultUniverse.getMasters().size())
        .addSimultaneousTasks(
            TaskType.AnsibleConfigureServers, defaultUniverse.getTServers().size())
        .addTasks(TaskType.XClusterInfoPersist)
        .addTasks(TaskType.StoreAutoFlagConfigVersion)
        .addTasks(TaskType.SaveSoftwareUpgradeProgress)
        .addSimultaneousTasks(TaskType.AnsibleConfigureServers, tserverNames.size())
        .rollingSoftwareUpgradeWithProgressSaves(
            UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE,
            UpgradeTaskBase.UpgradeContext.builder()
                .reconfigureMaster(false)
                .runBeforeStopping(false)
                .processInactiveMaster(true)
                .targetSoftwareVersion("2.21.0.0-b2")
                .build(),
            TaskType.AnsibleConfigureServers,
            false,
            masterNames,
            tserverNames)
        .addSimultaneousTasks(TaskType.CheckSoftwareVersion, defaultUniverse.getTServers().size())
        .addTasks(TaskType.PromoteAutoFlags)
        .addTasks(TaskType.SaveSoftwareUpgradeProgress)
        .addTasks(TaskType.UpdateSoftwareVersion)
        .verifyTasks(taskInfo.getSubTasks());
    assertTrue(defaultUniverse.getUniverseDetails().isSoftwareRollbackAllowed);
    assertEquals(
        "2.21.0.0-b1",
        defaultUniverse.getUniverseDetails().prevYBSoftwareConfig.getSoftwareVersion());
    assertEquals(
        SoftwareUpgradeState.Ready, defaultUniverse.getUniverseDetails().softwareUpgradeState);
  }

  @Test
  @Parameters({"true, false", "false, false", "true, true", "false, true"})
  public void testSoftwareUpgradeDisablesPitrOnlyWhenFinalizeRequired(
      boolean requireFinalize, boolean canary) throws Exception {
    // Finalize or rollback re-creates PITR schedules; a no-finalize upgrade ends in Ready and
    // never would, so it must leave them alone (PLAT-18619).
    updateDefaultUniverseTo5Nodes(true);
    when(mockSoftwareUpgradeHelper.checkUpgradeRequireFinalize(anyString(), anyString()))
        .thenReturn(requireFinalize);
    PitrConfig pitrConfig = createPitrConfigWithSchedule();

    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = "2.21.0.0-b2";
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = defaultUniverse.getVersion();
    taskParams.sleepAfterMasterRestartMillis = 5;
    taskParams.sleepAfterTServerRestartMillis = 5;
    taskParams.creatingUser = defaultUser;
    taskParams.clusters.add(defaultUniverse.getUniverseDetails().getPrimaryCluster());
    if (canary) {
      List<AZUpgradeStep> steps = new ArrayList<>();
      for (UUID azUUID : Arrays.asList(az1.getUuid(), az2.getUuid(), az3.getUuid())) {
        AZUpgradeStep step = new AZUpgradeStep();
        step.azUUID = azUUID;
        step.pauseAfterTserverUpgrade = false;
        steps.add(step);
      }
      taskParams.canaryUpgradeConfig = new CanaryUpgradeConfig();
      taskParams.canaryUpgradeConfig.pauseAfterMasters = false;
      taskParams.canaryUpgradeConfig.primaryClusterAZSteps = steps;
    }
    mockDBServerVersion(
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion,
        taskParams.ybSoftwareVersion,
        defaultUniverse.getMasters().size() + defaultUniverse.getTServers().size());

    TaskInfo taskInfo = waitForTask(commissioner.submit(TaskType.SoftwareUpgradeYB, taskParams));

    assertEquals(Success, taskInfo.getTaskState());
    assertEquals(
        requireFinalize,
        taskInfo.getSubTasks().stream()
            .anyMatch(t -> t.getTaskType() == TaskType.DisablePitrConfig));
    assertEquals(requireFinalize, PitrConfig.getOrBadRequest(pitrConfig.getUuid()).isDisabled());
    verify(mockClient, times(requireFinalize ? 1 : 0)).deleteSnapshotSchedule(pitrConfig.getUuid());
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertEquals(
        requireFinalize ? SoftwareUpgradeState.PreFinalize : SoftwareUpgradeState.Ready,
        defaultUniverse.getUniverseDetails().softwareUpgradeState);
  }

  @Test
  public void testCanaryConfigNoPausePoints() throws InterruptedException {
    updateDefaultUniverseTo5Nodes(true);
    when(mockSoftwareUpgradeHelper.checkUpgradeRequireFinalize(anyString(), anyString()))
        .thenReturn(true);

    List<AZUpgradeStep> steps = new ArrayList<>();
    for (UUID azUUID : Arrays.asList(az1.getUuid(), az2.getUuid(), az3.getUuid())) {
      AZUpgradeStep step = new AZUpgradeStep();
      step.azUUID = azUUID;
      step.pauseAfterTserverUpgrade = false;
      steps.add(step);
    }

    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = "2.21.0.0-b2";
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = defaultUniverse.getVersion();
    taskParams.sleepAfterMasterRestartMillis = 5;
    taskParams.sleepAfterTServerRestartMillis = 5;
    taskParams.creatingUser = defaultUser;
    taskParams.clusters.add(defaultUniverse.getUniverseDetails().getPrimaryCluster());
    taskParams.canaryUpgradeConfig = new CanaryUpgradeConfig();
    taskParams.canaryUpgradeConfig.pauseAfterMasters = false;
    taskParams.canaryUpgradeConfig.primaryClusterAZSteps = steps;

    mockDBServerVersion(
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion,
        taskParams.ybSoftwareVersion,
        defaultUniverse.getMasters().size() + defaultUniverse.getTServers().size());

    UUID taskUUID = commissioner.submit(TaskType.SoftwareUpgradeYB, taskParams);
    TaskInfo taskInfo = waitForTask(taskUUID);

    assertEquals(Success, taskInfo.getTaskState());
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertEquals(
        SoftwareUpgradeState.PreFinalize,
        defaultUniverse.getUniverseDetails().softwareUpgradeState);
  }

  @Test
  public void testCanaryUpgradeRejectedWhenStagePauseDurationNonZero() throws InterruptedException {
    updateDefaultUniverseTo5Nodes(true);
    when(mockSoftwareUpgradeHelper.checkUpgradeRequireFinalize(anyString(), anyString()))
        .thenReturn(true);

    List<AZUpgradeStep> steps = new ArrayList<>();
    for (UUID azUUID : Arrays.asList(az1.getUuid(), az2.getUuid(), az3.getUuid())) {
      AZUpgradeStep step = new AZUpgradeStep();
      step.azUUID = azUUID;
      step.pauseAfterTserverUpgrade = false;
      steps.add(step);
    }

    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = "2.21.0.0-b2";
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.sleepAfterMasterRestartMillis = 5;
    taskParams.sleepAfterTServerRestartMillis = 5;
    taskParams.creatingUser = defaultUser;
    taskParams.clusters.add(defaultUniverse.getUniverseDetails().getPrimaryCluster());
    taskParams.canaryUpgradeConfig = new CanaryUpgradeConfig();
    taskParams.canaryUpgradeConfig.pauseAfterMasters = false;
    taskParams.canaryUpgradeConfig.primaryClusterAZSteps = steps;

    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    try {
      factory
          .forUniverse(defaultUniverse)
          .setValue(UniverseConfKeys.upgradeMasterStagePauseDurationMs.getKey(), "5000");
      defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
      taskParams.expectedUniverseVersion = defaultUniverse.getVersion();

      PlatformServiceException ex =
          assertThrows(
              PlatformServiceException.class,
              () -> commissioner.submit(TaskType.SoftwareUpgradeYB, taskParams));
      assertThat(ex.getMessage(), containsString("per-AZ stage pause durations are non-zero"));

      factory
          .forUniverse(defaultUniverse)
          .setValue(UniverseConfKeys.upgradeMasterStagePauseDurationMs.getKey(), "0");
      factory
          .forUniverse(defaultUniverse)
          .setValue(UniverseConfKeys.upgradeTServerStagePauseDurationMs.getKey(), "5000");
      defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
      taskParams.expectedUniverseVersion = defaultUniverse.getVersion();

      ex =
          assertThrows(
              PlatformServiceException.class,
              () -> commissioner.submit(TaskType.SoftwareUpgradeYB, taskParams));
      assertThat(ex.getMessage(), containsString("per-AZ stage pause durations are non-zero"));

      factory
          .forUniverse(defaultUniverse)
          .setValue(UniverseConfKeys.upgradeTServerStagePauseDurationMs.getKey(), "0");
      defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
      taskParams.expectedUniverseVersion = defaultUniverse.getVersion();

      mockDBServerVersion(
          defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion,
          taskParams.ybSoftwareVersion,
          defaultUniverse.getMasters().size() + defaultUniverse.getTServers().size());

      UUID taskUUID = commissioner.submit(TaskType.SoftwareUpgradeYB, taskParams);
      assertNotNull(taskUUID);
      TaskInfo taskInfo = waitForTask(taskUUID);
      assertEquals(Success, taskInfo.getTaskState());
    } finally {
      defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
      factory
          .forUniverse(defaultUniverse)
          .setValue(UniverseConfKeys.upgradeMasterStagePauseDurationMs.getKey(), "0");
      factory
          .forUniverse(defaultUniverse)
          .setValue(UniverseConfKeys.upgradeTServerStagePauseDurationMs.getKey(), "0");
    }
  }

  @Test
  public void testCanaryResumeAfterMasters() throws InterruptedException {
    updateDefaultUniverseTo5Nodes(true);
    when(mockSoftwareUpgradeHelper.checkUpgradeRequireFinalize(anyString(), anyString()))
        .thenReturn(true);

    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = "2.21.0.0-b2";
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = defaultUniverse.getVersion();
    taskParams.sleepAfterMasterRestartMillis = 5;
    taskParams.sleepAfterTServerRestartMillis = 5;
    taskParams.creatingUser = defaultUser;
    taskParams.clusters.add(defaultUniverse.getUniverseDetails().getPrimaryCluster());
    taskParams.canaryUpgradeConfig = new CanaryUpgradeConfig();
    taskParams.canaryUpgradeConfig.pauseAfterMasters = true;

    mockDBServerVersion(
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion,
        taskParams.ybSoftwareVersion,
        defaultUniverse.getMasters().size() + defaultUniverse.getTServers().size());

    UUID taskUUID = commissioner.submit(TaskType.SoftwareUpgradeYB, taskParams);
    TaskInfo taskInfo = waitForTask(taskUUID);
    assertEquals(TaskInfo.State.Paused, taskInfo.getTaskState());

    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertEquals(
        SoftwareUpgradeState.Paused, defaultUniverse.getUniverseDetails().softwareUpgradeState);
    assertCanaryPauseUniverseTaskFields(defaultUniverse, taskUUID);
    PrevYBSoftwareConfig prev = defaultUniverse.getUniverseDetails().prevYBSoftwareConfig;
    assertNotNull(prev);
    assertTrue("Masters phase should be completed before pause", allMasterAzsCompleted(prev));

    // Re-mock server versions: first run consumed mock responses.
    mockDBServerVersion(
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion,
        taskParams.ybSoftwareVersion,
        defaultUniverse.getMasters().size() + defaultUniverse.getTServers().size());

    UUID resumedUUID = resumeCanaryTask(taskUUID);
    taskInfo = waitForTask(resumedUUID);

    assertEquals(Success, taskInfo.getTaskState());
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertEquals(
        SoftwareUpgradeState.PreFinalize,
        defaultUniverse.getUniverseDetails().softwareUpgradeState);
    assertPreFinalizeClearsSoftwareUpgradeTaskTracking(defaultUniverse);
  }

  @Test
  public void testCanaryResumeClearsPausedAfterMastersImmediately() throws Exception {
    String baseVersion = "2024.2.2.0-b1";
    String targetVersion = "2025.1.0.0-b1";
    this.ysqlMajorUpgrade = true;
    updateDefaultUniverseTo5Nodes(true);
    TestHelper.updateUniverseVersion(defaultUniverse, baseVersion);
    when(mockSoftwareUpgradeHelper.isYsqlMajorVersionUpgradeRequired(
            any(), anyString(), anyString()))
        .thenReturn(true);
    when(mockSoftwareUpgradeHelper.checkUpgradeRequireFinalize(anyString(), anyString()))
        .thenReturn(true);

    ReleaseManager.ReleaseMetadata rm =
        ReleaseManager.ReleaseMetadata.create(targetVersion)
            .withFilePath("yugabyte-" + targetVersion + "-centos-x86_64" + ".tar.gz");
    when(mockReleaseManager.getReleaseByVersion(anyString()))
        .thenReturn(new ReleaseContainer(rm, mockCloudUtilFactory, mockConfig, mockReleasesUtils));
    when(mockClient.setFlag(any(), anyString(), anyString(), anyBoolean())).thenReturn(true);
    when(mockClient.getYsqlMajorCatalogUpgradeState())
        .thenReturn(
            new GetYsqlMajorCatalogUpgradeStateResponse(
                0L,
                null,
                null,
                YsqlMajorCatalogUpgradeState
                    .YSQL_MAJOR_CATALOG_UPGRADE_PENDING_FINALIZE_OR_ROLLBACK));

    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = targetVersion;
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = defaultUniverse.getVersion();
    taskParams.sleepAfterMasterRestartMillis = 5;
    taskParams.sleepAfterTServerRestartMillis = 5;
    taskParams.creatingUser = defaultUser;
    taskParams.clusters.add(defaultUniverse.getUniverseDetails().getPrimaryCluster());
    taskParams.canaryUpgradeConfig = new CanaryUpgradeConfig();
    taskParams.canaryUpgradeConfig.pauseAfterMasters = true;

    mockDBServerVersion(
        baseVersion,
        targetVersion,
        defaultUniverse.getMasters().size() + defaultUniverse.getTServers().size());

    UUID taskUUID = commissioner.submit(TaskType.SoftwareUpgradeYB, taskParams);
    TaskInfo taskInfo = waitForTask(taskUUID);
    assertEquals(TaskInfo.State.Paused, taskInfo.getTaskState());

    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    PrevYBSoftwareConfig prev = defaultUniverse.getUniverseDetails().prevYBSoftwareConfig;
    assertNotNull(prev);
    assertEquals(CanaryPauseState.PAUSED_AFTER_MASTERS, prev.getCanaryPauseState());
    assertTrue("Masters phase should be completed before pause", allMasterAzsCompleted(prev));

    mockDBServerVersion(
        baseVersion,
        targetVersion,
        defaultUniverse.getMasters().size() + defaultUniverse.getTServers().size());

    TaskInfo.deleteChildrenAfterMaxSuccessPosition(taskUUID);
    Set<UUID> preResumeSubTaskUUIDs =
        TaskInfo.getOrBadRequest(taskUUID).getSubTasks().stream()
            .map(TaskInfo::getUuid)
            .collect(Collectors.toSet());
    UUID resumedUUID = resumeCanaryTask(taskUUID);
    taskInfo = waitForTask(resumedUUID);
    assertEquals(Success, taskInfo.getTaskState());

    List<TaskInfo> subTasks = TaskInfo.getOrBadRequest(resumedUUID).getSubTasks();
    int upgradingStatePosition = Integer.MAX_VALUE;
    int firstNotPausedSavePosition = Integer.MAX_VALUE;
    int catalogPosition = Integer.MAX_VALUE;
    int firstAnsiblePosition = Integer.MAX_VALUE;
    for (TaskInfo subTask : subTasks) {
      if (preResumeSubTaskUUIDs.contains(subTask.getUuid())) {
        continue;
      }
      int position = subTask.getPosition();
      if (subTask.getTaskType() == TaskType.UpdateUniverseState) {
        UpdateUniverseSoftwareUpgradeState.Params stateParams =
            play.libs.Json.fromJson(
                subTask.getTaskParams(), UpdateUniverseSoftwareUpgradeState.Params.class);
        if (stateParams.state == SoftwareUpgradeState.Upgrading
            && position < upgradingStatePosition) {
          upgradingStatePosition = position;
        }
      } else if (subTask.getTaskType() == TaskType.SaveSoftwareUpgradeProgress) {
        SaveSoftwareUpgradeProgress.Params progressParams =
            play.libs.Json.fromJson(
                subTask.getTaskParams(), SaveSoftwareUpgradeProgress.Params.class);
        if (progressParams.canaryPauseState == CanaryPauseState.NOT_PAUSED
            && position < firstNotPausedSavePosition) {
          firstNotPausedSavePosition = position;
        }
      } else if (subTask.getTaskType() == TaskType.RunYsqlMajorVersionCatalogUpgrade) {
        catalogPosition = Math.min(catalogPosition, position);
      } else if (subTask.getTaskType() == TaskType.AnsibleConfigureServers) {
        NodeTaskParams nodeParams =
            play.libs.Json.fromJson(subTask.getTaskParams(), NodeTaskParams.class);
        if (nodeParams.isTserver) {
          firstAnsiblePosition = Math.min(firstAnsiblePosition, position);
        }
      }
    }
    assertTrue(
        "Resume should transition universe to Upgrading before catalog/tserver work",
        upgradingStatePosition < Integer.MAX_VALUE);
    assertTrue(
        "Upgrading state update should run before YSQL catalog upgrade",
        upgradingStatePosition < catalogPosition);
    assertTrue(
        "Upgrading state update should run before first tserver AnsibleConfigureServers",
        upgradingStatePosition < firstAnsiblePosition);
    assertTrue(
        "Resume should enqueue NOT_PAUSED progress before catalog/tserver work",
        firstNotPausedSavePosition < Integer.MAX_VALUE);
    assertTrue(
        "Upgrading state update should run before NOT_PAUSED progress save",
        upgradingStatePosition < firstNotPausedSavePosition);
    assertTrue(
        "NOT_PAUSED save should run before YSQL catalog upgrade",
        firstNotPausedSavePosition < catalogPosition);
    assertTrue(
        "NOT_PAUSED save should run before first tserver AnsibleConfigureServers",
        firstNotPausedSavePosition < firstAnsiblePosition);

    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertEquals(
        SoftwareUpgradeState.PreFinalize,
        defaultUniverse.getUniverseDetails().softwareUpgradeState);
    assertPreFinalizeClearsSoftwareUpgradeTaskTracking(defaultUniverse);
  }

  /**
   * Simulates UI retry after a canary upgrade failure: universe is left in UpgradeFailed with
   * placementModificationTaskUuid pointing at the original task, but prevYBSoftwareConfig still
   * records completed AZs. Retry submits a new parent task UUID with previousTaskUUID set to the
   * original task; progress must not reset completed tserver AZs to NOT_STARTED.
   */
  @Test
  public void testCanaryFailedRetryPreservesCompletedTserverAzProgress()
      throws InterruptedException {
    updateDefaultUniverseTo5Nodes(true);
    when(mockSoftwareUpgradeHelper.checkUpgradeRequireFinalize(anyString(), anyString()))
        .thenReturn(true);

    List<UUID> azOrder = Arrays.asList(az1.getUuid(), az2.getUuid(), az3.getUuid());
    List<AZUpgradeStep> steps = new ArrayList<>();
    for (UUID azUUID : azOrder) {
      AZUpgradeStep step = new AZUpgradeStep();
      step.azUUID = azUUID;
      step.pauseAfterTserverUpgrade = azUUID.equals(az1.getUuid());
      steps.add(step);
    }

    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = "2.21.0.0-b2";
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = defaultUniverse.getVersion();
    taskParams.sleepAfterMasterRestartMillis = 5;
    taskParams.sleepAfterTServerRestartMillis = 5;
    taskParams.creatingUser = defaultUser;
    taskParams.clusters.add(defaultUniverse.getUniverseDetails().getPrimaryCluster());
    taskParams.canaryUpgradeConfig = new CanaryUpgradeConfig();
    taskParams.canaryUpgradeConfig.pauseAfterMasters = false;
    taskParams.canaryUpgradeConfig.primaryClusterAZSteps = steps;

    mockDBServerVersion(
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion,
        taskParams.ybSoftwareVersion,
        defaultUniverse.getMasters().size() + defaultUniverse.getTServers().size());

    UUID failedTaskUuid = commissioner.submit(TaskType.SoftwareUpgradeYB, taskParams);
    TaskInfo taskInfo = waitForTask(failedTaskUuid);
    assertEquals(TaskInfo.State.Paused, taskInfo.getTaskState());

    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    PrevYBSoftwareConfig prevAfterPause = defaultUniverse.getUniverseDetails().prevYBSoftwareConfig;
    assertNotNull(prevAfterPause);
    assertTrue(
        "First primary AZ should be completed before simulated failure",
        primaryTserverAzCompleted(prevAfterPause, defaultUniverse, az1.getUuid()));

    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(),
        u -> {
          u.getUniverseDetails().softwareUpgradeState = SoftwareUpgradeState.UpgradeFailed;
          u.setUniverseDetails(u.getUniverseDetails());
        });

    SoftwareUpgradeParams retryParams =
        play.libs.Json.fromJson(
            TaskInfo.getOrBadRequest(failedTaskUuid).getTaskParams(), SoftwareUpgradeParams.class);
    retryParams.setPreviousTaskUUID(failedTaskUuid);
    retryParams.expectedUniverseVersion = -1;
    retryParams.creatingUser = defaultUser;

    mockDBServerVersion(
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion,
        retryParams.ybSoftwareVersion,
        defaultUniverse.getMasters().size() + defaultUniverse.getTServers().size());

    UUID retryTaskUuid = commissioner.submit(TaskType.SoftwareUpgradeYB, retryParams);

    long deadline = System.currentTimeMillis() + 120_000;
    boolean sawAz1StillCompleted = false;
    while (System.currentTimeMillis() < deadline) {
      defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
      PrevYBSoftwareConfig p = defaultUniverse.getUniverseDetails().prevYBSoftwareConfig;
      if (p != null && primaryTserverAzCompleted(p, defaultUniverse, az1.getUuid())) {
        sawAz1StillCompleted = true;
        break;
      }
      Thread.sleep(200);
    }
    assertTrue(
        "Retry must not wipe completed AZ progress; az1 should stay COMPLETED in prev",
        sawAz1StillCompleted);

    taskInfo = waitForTask(retryTaskUuid);
    assertEquals(Success, taskInfo.getTaskState());
  }

  /**
   * After a successful canary run that ends in PreFinalize, unlock should clear task tracking
   * fields (including on resume when FreezeUniverse subtask is skipped).
   */
  private void assertPreFinalizeClearsSoftwareUpgradeTaskTracking(Universe universe) {
    UniverseDefinitionTaskParams d = universe.getUniverseDetails();
    assertNull(d.placementModificationTaskUuid);
    assertNull(d.updatingTaskUUID);
    assertNull(d.updatingTask);
  }

  /** Universe fields expected while a canary software upgrade is paused (between pause points). */
  private void assertCanaryPauseUniverseTaskFields(Universe universe, UUID expectedTaskUuid) {
    UniverseDefinitionTaskParams d = universe.getUniverseDetails();
    assertTrue("Canary pause should not surface as updateSucceeded=false", d.updateSucceeded);
    assertNull("updatingTaskUUID should be cleared at canary pause", d.updatingTaskUUID);
    assertNull("updatingTask should be cleared at canary pause", d.updatingTask);
    assertEquals(
        "placementModificationTaskUuid is the marker for the paused upgrade",
        expectedTaskUuid,
        d.placementModificationTaskUuid);
    assertEquals(SoftwareUpgradeState.Paused, d.softwareUpgradeState);
  }

  /**
   * Simulates UpgradeUniverseHandler.resumeCanarySoftwareUpgrade: deserializes params from the
   * stored TaskInfo, sets previousTaskUUID and expectedUniverseVersion=-1, then submits with the
   * same task UUID.
   */
  private UUID resumeCanaryTask(UUID taskUUID) {
    TaskInfo taskInfo = TaskInfo.getOrBadRequest(taskUUID);
    SoftwareUpgradeParams params =
        play.libs.Json.fromJson(taskInfo.getTaskParams(), SoftwareUpgradeParams.class);
    params.setPreviousTaskUUID(taskUUID);
    params.expectedUniverseVersion = -1;
    return commissioner.submit(TaskType.SoftwareUpgradeYB, params, taskUUID);
  }

  /**
   * Verifies canary tserver AZ pause fires when upgradeTServerStagePauseDurationMs is at default
   * (0). Ensures the sleepTime gating fix: AZ-level loop and SaveSoftwareUpgradeProgress run even
   * without explicit sleep config.
   */
  @Test
  public void testCanaryPausesWithoutSleepConfig() throws InterruptedException {
    updateDefaultUniverseTo5Nodes(true);
    when(mockSoftwareUpgradeHelper.checkUpgradeRequireFinalize(anyString(), anyString()))
        .thenReturn(true);

    List<AZUpgradeStep> steps = new ArrayList<>();
    AZUpgradeStep step1 = new AZUpgradeStep();
    step1.azUUID = az1.getUuid();
    step1.pauseAfterTserverUpgrade = true;
    steps.add(step1);
    for (UUID azUUID : Arrays.asList(az2.getUuid(), az3.getUuid())) {
      AZUpgradeStep step = new AZUpgradeStep();
      step.azUUID = azUUID;
      step.pauseAfterTserverUpgrade = false;
      steps.add(step);
    }

    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = "2.21.0.0-b2";
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = defaultUniverse.getVersion();
    taskParams.sleepAfterMasterRestartMillis = 0;
    taskParams.sleepAfterTServerRestartMillis = 0;
    taskParams.creatingUser = defaultUser;
    taskParams.clusters.add(defaultUniverse.getUniverseDetails().getPrimaryCluster());
    taskParams.canaryUpgradeConfig = new CanaryUpgradeConfig();
    taskParams.canaryUpgradeConfig.pauseAfterMasters = false;
    taskParams.canaryUpgradeConfig.primaryClusterAZSteps = steps;

    mockDBServerVersion(
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion,
        taskParams.ybSoftwareVersion,
        defaultUniverse.getMasters().size() + defaultUniverse.getTServers().size());

    UUID taskUUID = commissioner.submit(TaskType.SoftwareUpgradeYB, taskParams);
    TaskInfo taskInfo = waitForTask(taskUUID);
    assertEquals(TaskInfo.State.Paused, taskInfo.getTaskState());

    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertEquals(
        SoftwareUpgradeState.Paused, defaultUniverse.getUniverseDetails().softwareUpgradeState);
    assertCanaryPauseUniverseTaskFields(defaultUniverse, taskUUID);
    PrevYBSoftwareConfig prev = defaultUniverse.getUniverseDetails().prevYBSoftwareConfig;
    assertNotNull(prev);
    assertTrue(allMasterAzsCompleted(prev));
    assertTrue(
        "az1 should be in completed AZs when sleepTime=0",
        primaryTserverAzCompleted(prev, defaultUniverse, az1.getUuid()));
  }

  @Test
  public void testCanaryTserverAZPauseFirstAZ() throws InterruptedException {
    updateDefaultUniverseTo5Nodes(true);

    List<UUID> azOrder = Arrays.asList(az1.getUuid(), az2.getUuid(), az3.getUuid());
    List<AZUpgradeStep> steps = new ArrayList<>();
    for (UUID azUUID : azOrder) {
      AZUpgradeStep step = new AZUpgradeStep();
      step.azUUID = azUUID;
      step.pauseAfterTserverUpgrade = azUUID.equals(az1.getUuid());
      steps.add(step);
    }

    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = "2.21.0.0-b2";
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = defaultUniverse.getVersion();
    taskParams.sleepAfterMasterRestartMillis = 5;
    taskParams.sleepAfterTServerRestartMillis = 5;
    taskParams.creatingUser = defaultUser;
    taskParams.clusters.add(defaultUniverse.getUniverseDetails().getPrimaryCluster());
    taskParams.canaryUpgradeConfig = new CanaryUpgradeConfig();
    taskParams.canaryUpgradeConfig.pauseAfterMasters = false;
    taskParams.canaryUpgradeConfig.primaryClusterAZSteps = steps;

    mockDBServerVersion(
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion,
        taskParams.ybSoftwareVersion,
        defaultUniverse.getMasters().size() + defaultUniverse.getTServers().size());

    UUID taskUUID = commissioner.submit(TaskType.SoftwareUpgradeYB, taskParams);
    TaskInfo taskInfo = waitForTask(taskUUID);
    assertEquals(TaskInfo.State.Paused, taskInfo.getTaskState());

    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertEquals(
        SoftwareUpgradeState.Paused, defaultUniverse.getUniverseDetails().softwareUpgradeState);
    assertCanaryPauseUniverseTaskFields(defaultUniverse, taskUUID);
    PrevYBSoftwareConfig prev = defaultUniverse.getUniverseDetails().prevYBSoftwareConfig;
    assertNotNull(prev);
    assertTrue(allMasterAzsCompleted(prev));
    assertTrue(
        "az1 should be in completed AZs",
        primaryTserverAzCompleted(prev, defaultUniverse, az1.getUuid()));
    assertFalse(
        "az2 should not yet be completed",
        primaryTserverAzCompleted(prev, defaultUniverse, az2.getUuid()));
  }

  @Test
  public void testCanaryTserverAZPauseResumeAndPauseNextAZ() throws InterruptedException {
    updateDefaultUniverseTo5Nodes(true);
    when(mockSoftwareUpgradeHelper.checkUpgradeRequireFinalize(anyString(), anyString()))
        .thenReturn(true);

    List<UUID> azOrder = Arrays.asList(az1.getUuid(), az2.getUuid(), az3.getUuid());
    List<AZUpgradeStep> steps = new ArrayList<>();
    for (UUID azUUID : azOrder) {
      AZUpgradeStep step = new AZUpgradeStep();
      step.azUUID = azUUID;
      step.pauseAfterTserverUpgrade = azUUID.equals(az1.getUuid()) || azUUID.equals(az2.getUuid());
      steps.add(step);
    }

    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = "2.21.0.0-b2";
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = defaultUniverse.getVersion();
    taskParams.sleepAfterMasterRestartMillis = 5;
    taskParams.sleepAfterTServerRestartMillis = 5;
    taskParams.creatingUser = defaultUser;
    taskParams.clusters.add(defaultUniverse.getUniverseDetails().getPrimaryCluster());
    taskParams.canaryUpgradeConfig = new CanaryUpgradeConfig();
    taskParams.canaryUpgradeConfig.pauseAfterMasters = false;
    taskParams.canaryUpgradeConfig.primaryClusterAZSteps = steps;

    mockDBServerVersion(
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion,
        taskParams.ybSoftwareVersion,
        defaultUniverse.getMasters().size() + defaultUniverse.getTServers().size());

    // First run: should pause after az1 tservers.
    UUID taskUUID = commissioner.submit(TaskType.SoftwareUpgradeYB, taskParams);
    TaskInfo taskInfo = waitForTask(taskUUID);
    assertEquals(TaskInfo.State.Paused, taskInfo.getTaskState());

    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertCanaryPauseUniverseTaskFields(defaultUniverse, taskUUID);
    PrevYBSoftwareConfig prev = defaultUniverse.getUniverseDetails().prevYBSoftwareConfig;
    assertNotNull(prev);
    assertTrue(primaryTserverAzCompleted(prev, defaultUniverse, az1.getUuid()));
    assertEquals(1, countPrimaryCompletedTserverAzs(prev, defaultUniverse));

    // Re-mock server versions: first run consumed mock responses.
    mockDBServerVersion(
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion,
        taskParams.ybSoftwareVersion,
        defaultUniverse.getMasters().size() + defaultUniverse.getTServers().size());

    // Resume: should pause after az2 tservers.
    UUID resumedUUID = resumeCanaryTask(taskUUID);
    taskInfo = waitForTask(resumedUUID);
    assertEquals(TaskInfo.State.Paused, taskInfo.getTaskState());

    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertCanaryPauseUniverseTaskFields(defaultUniverse, resumedUUID);
    prev = defaultUniverse.getUniverseDetails().prevYBSoftwareConfig;
    assertNotNull(prev);
    assertTrue(
        "az1 should still be completed",
        primaryTserverAzCompleted(prev, defaultUniverse, az1.getUuid()));
    assertTrue(
        "az2 should now be completed",
        primaryTserverAzCompleted(prev, defaultUniverse, az2.getUuid()));
    assertEquals(2, countPrimaryCompletedTserverAzs(prev, defaultUniverse));

    // Re-mock server versions for the second resume.
    mockDBServerVersion(
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion,
        taskParams.ybSoftwareVersion,
        defaultUniverse.getMasters().size() + defaultUniverse.getTServers().size());

    // Second resume: should complete (az3 has no pause).
    UUID resumed2UUID = resumeCanaryTask(resumedUUID);
    TaskInfo finalTaskInfo = waitForTask(resumed2UUID);
    assertEquals(Success, finalTaskInfo.getTaskState());

    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertEquals(
        SoftwareUpgradeState.PreFinalize,
        defaultUniverse.getUniverseDetails().softwareUpgradeState);
    assertPreFinalizeClearsSoftwareUpgradeTaskTracking(defaultUniverse);
  }

  @Test
  public void testCanaryMasterPauseThenTserverAZPause() throws InterruptedException {
    updateDefaultUniverseTo5Nodes(true);
    when(mockSoftwareUpgradeHelper.checkUpgradeRequireFinalize(anyString(), anyString()))
        .thenReturn(true);

    List<AZUpgradeStep> steps = new ArrayList<>();
    AZUpgradeStep step1 = new AZUpgradeStep();
    step1.azUUID = az1.getUuid();
    step1.pauseAfterTserverUpgrade = true;
    steps.add(step1);
    AZUpgradeStep step2 = new AZUpgradeStep();
    step2.azUUID = az2.getUuid();
    step2.pauseAfterTserverUpgrade = false;
    steps.add(step2);
    AZUpgradeStep step3 = new AZUpgradeStep();
    step3.azUUID = az3.getUuid();
    step3.pauseAfterTserverUpgrade = false;
    steps.add(step3);

    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = "2.21.0.0-b2";
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = defaultUniverse.getVersion();
    taskParams.sleepAfterMasterRestartMillis = 5;
    taskParams.sleepAfterTServerRestartMillis = 5;
    taskParams.creatingUser = defaultUser;
    taskParams.clusters.add(defaultUniverse.getUniverseDetails().getPrimaryCluster());
    taskParams.canaryUpgradeConfig = new CanaryUpgradeConfig();
    taskParams.canaryUpgradeConfig.pauseAfterMasters = true;
    taskParams.canaryUpgradeConfig.primaryClusterAZSteps = steps;

    mockDBServerVersion(
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion,
        taskParams.ybSoftwareVersion,
        defaultUniverse.getMasters().size() + defaultUniverse.getTServers().size());

    // First run: should pause after masters.
    UUID taskUUID = commissioner.submit(TaskType.SoftwareUpgradeYB, taskParams);
    TaskInfo taskInfo = waitForTask(taskUUID);
    assertEquals(TaskInfo.State.Paused, taskInfo.getTaskState());

    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertCanaryPauseUniverseTaskFields(defaultUniverse, taskUUID);
    PrevYBSoftwareConfig prev = defaultUniverse.getUniverseDetails().prevYBSoftwareConfig;
    assertNotNull(prev);
    assertTrue("Masters should be completed", allMasterAzsCompleted(prev));
    assertTrue(
        "No tserver AZs completed yet",
        countPrimaryCompletedTserverAzs(prev, defaultUniverse) == 0);

    // Re-mock server versions: first run consumed mock responses.
    mockDBServerVersion(
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion,
        taskParams.ybSoftwareVersion,
        defaultUniverse.getMasters().size() + defaultUniverse.getTServers().size());

    // Resume after masters: should pause after az1 tservers.
    UUID resumedUUID = resumeCanaryTask(taskUUID);
    taskInfo = waitForTask(resumedUUID);
    assertEquals(TaskInfo.State.Paused, taskInfo.getTaskState());

    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertCanaryPauseUniverseTaskFields(defaultUniverse, resumedUUID);
    prev = defaultUniverse.getUniverseDetails().prevYBSoftwareConfig;
    assertNotNull(prev);
    assertTrue(primaryTserverAzCompleted(prev, defaultUniverse, az1.getUuid()));

    // Re-mock server versions for the final resume.
    mockDBServerVersion(
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion,
        taskParams.ybSoftwareVersion,
        defaultUniverse.getMasters().size() + defaultUniverse.getTServers().size());

    // Final resume: should complete.
    UUID resumed2UUID = resumeCanaryTask(resumedUUID);
    TaskInfo finalTaskInfo = waitForTask(resumed2UUID);
    assertEquals(Success, finalTaskInfo.getTaskState());

    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertEquals(
        SoftwareUpgradeState.PreFinalize,
        defaultUniverse.getUniverseDetails().softwareUpgradeState);
    assertPreFinalizeClearsSoftwareUpgradeTaskTracking(defaultUniverse);
  }

  @Test
  public void testCanaryPauseBlocksDisallowedTasksAndAllowsRollback() throws InterruptedException {
    updateDefaultUniverseTo5Nodes(true);
    PrevYBSoftwareConfig prev = new PrevYBSoftwareConfig();
    prev.setSoftwareVersion("2.21.0.0-b1");
    prev.setTargetUpgradeSoftwareVersion("2.21.0.0-b2");
    prev.setCanRollbackCatalogUpgrade(true);
    prev.setAllTserversUpgradedToYsqlMajorVersion(true);
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            universe -> {
              UniverseDefinitionTaskParams d = universe.getUniverseDetails();
              d.softwareUpgradeState = SoftwareUpgradeState.Paused;
              d.isSoftwareRollbackAllowed = true;
              d.prevYBSoftwareConfig = prev;
              d.updateInProgress = false;
              universe.setUniverseDetails(d);
            });

    GFlagsUpgradeParams gflagsParams = new GFlagsUpgradeParams();
    gflagsParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    gflagsParams.expectedUniverseVersion = defaultUniverse.getVersion();
    gflagsParams.creatingUser = defaultUser;
    gflagsParams.masterGFlags = ImmutableMap.of("master-flag", "m1");
    gflagsParams.tserverGFlags = ImmutableMap.of("tserver-flag", "t1");
    gflagsParams.upgradeOption = UpgradeOption.ROLLING_UPGRADE;

    PlatformServiceException gflagsEx =
        assertThrows(
            PlatformServiceException.class,
            () -> commissioner.submit(TaskType.GFlagsUpgrade, gflagsParams));
    assertThat(gflagsEx.getMessage(), containsString("paused canary software upgrade"));

    RollbackUpgradeParams rollbackParams = new RollbackUpgradeParams();
    rollbackParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    rollbackParams.upgradeOption = UpgradeOption.ROLLING_UPGRADE;
    rollbackParams.creatingUser = defaultUser;
    rollbackParams.sleepAfterMasterRestartMillis = 5;
    rollbackParams.sleepAfterTServerRestartMillis = 5;

    mockDBServerVersion(
        "2.21.0.0-b2",
        "2.21.0.0-b1",
        defaultUniverse.getMasters().size() + defaultUniverse.getTServers().size());
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    TaskInfo rollbackTaskInfo =
        submitTask(
            rollbackParams, TaskType.RollbackUpgrade, commissioner, defaultUniverse.getVersion());
    assertEquals(Success, rollbackTaskInfo.getTaskState());
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertEquals(
        SoftwareUpgradeState.Ready, defaultUniverse.getUniverseDetails().softwareUpgradeState);
  }

  private static boolean allMasterAzsCompleted(PrevYBSoftwareConfig prev) {
    if (prev.getMasterAZUpgradeStatesList() == null
        || prev.getMasterAZUpgradeStatesList().isEmpty()) {
      return false;
    }
    return prev.getMasterAZUpgradeStatesList().stream()
        .allMatch(s -> s.getStatus() == AZUpgradeStatus.COMPLETED);
  }

  private static boolean primaryTserverAzCompleted(
      PrevYBSoftwareConfig prev, Universe universe, UUID azUuid) {
    UUID primaryUuid = universe.getUniverseDetails().getPrimaryCluster().uuid;
    if (prev.getTserverAZUpgradeStatesList() == null) {
      return false;
    }
    return prev.getTserverAZUpgradeStatesList().stream()
        .anyMatch(
            s ->
                primaryUuid.equals(s.getClusterUUID())
                    && azUuid.equals(s.getAzUUID())
                    && s.getStatus() == AZUpgradeStatus.COMPLETED);
  }

  private static long countPrimaryCompletedTserverAzs(PrevYBSoftwareConfig prev, Universe u) {
    UUID primaryUuid = u.getUniverseDetails().getPrimaryCluster().uuid;
    if (prev.getTserverAZUpgradeStatesList() == null) {
      return 0;
    }
    return prev.getTserverAZUpgradeStatesList().stream()
        .filter(
            s ->
                primaryUuid.equals(s.getClusterUUID())
                    && s.getStatus() == AZUpgradeStatus.COMPLETED)
        .map(AZUpgradeState::getAzUUID)
        .distinct()
        .count();
  }

  private MockUpgrade initMockUpgrade() {
    return initMockUpgrade(SoftwareUpgrade.class);
  }

  @Override
  protected TaskType[] getPrecheckTasks(boolean hasRollingRestarts) {
    List<TaskType> prechecks = new ArrayList<>();
    if (hasRollingRestarts) {
      prechecks.add(TaskType.CheckServiceLiveness);
      prechecks.add(TaskType.CheckNodeCommandExecution);
      prechecks.add(TaskType.CheckNodesAreSafeToTakeDown);
    }
    prechecks.addAll(
        Arrays.asList(
            TaskType.CheckUpgrade,
            TaskType.CheckMemory,
            TaskType.CheckLocale,
            TaskType.CheckGlibc));
    if (ysqlMajorUpgrade) {
      prechecks.add(TaskType.PGUpgradeTServerCheck);
    }
    return prechecks.toArray(new TaskType[0]);
  }

  /** A PITR config whose snapshot schedule the mocked client lists and can delete. */
  private PitrConfig createPitrConfigWithSchedule() throws Exception {
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
    ListSnapshotSchedulesResponse listResponse = Mockito.mock(ListSnapshotSchedulesResponse.class);
    lenient()
        .when(listResponse.getSnapshotScheduleInfoList())
        .thenReturn(Collections.singletonList(scheduleInfo));
    lenient().when(mockClient.listSnapshotSchedules(isNull())).thenReturn(listResponse);
    DeleteSnapshotScheduleResponse deleteResponse =
        Mockito.mock(DeleteSnapshotScheduleResponse.class);
    lenient()
        .when(mockClient.deleteSnapshotSchedule(pitrConfig.getUuid()))
        .thenReturn(deleteResponse);
    return pitrConfig;
  }
}
