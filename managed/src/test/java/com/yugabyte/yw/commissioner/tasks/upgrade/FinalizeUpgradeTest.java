// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.Mockito.when;

import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.tasks.subtasks.RunYsqlUpgrade;
import com.yugabyte.yw.common.TestHelper;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.forms.FinalizeUpgradeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import junitparams.JUnitParamsRunner;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import org.yb.client.GetYsqlMajorCatalogUpgradeStateResponse;
import org.yb.client.IsInitDbDoneResponse;
import org.yb.client.PromoteAutoFlagsResponse;
import org.yb.client.UpgradeYsqlResponse;
import org.yb.master.MasterAdminOuterClass.YsqlMajorCatalogUpgradeState;
import org.yb.master.MasterClusterOuterClass.PromoteAutoFlagsResponsePB;

@RunWith(JUnitParamsRunner.class)
public class FinalizeUpgradeTest extends UpgradeTaskTest {

  @Rule public MockitoRule rule = MockitoJUnit.rule();

  @InjectMocks private FinalizeUpgrade finalizeUpgrade;

  @Before
  public void setup() {
    finalizeUpgrade.setTaskUUID(UUID.randomUUID());
    try {
      UpgradeYsqlResponse mockUpgradeYsqlResponse = new UpgradeYsqlResponse(1000, "", null);
      when(mockYBClient.getClientWithConfig(any())).thenReturn(mockClient);
      when(mockClient.upgradeYsql(any(HostAndPort.class), anyBoolean()))
          .thenReturn(mockUpgradeYsqlResponse);
      IsInitDbDoneResponse mockIsInitDbDoneResponse =
          new IsInitDbDoneResponse(1000, "", true, true, null, null);
      when(mockClient.getIsInitDbDone()).thenReturn(mockIsInitDbDoneResponse);
      when(mockClient.promoteAutoFlags(anyString(), anyBoolean(), anyBoolean()))
          .thenReturn(
              new PromoteAutoFlagsResponse(
                  0, "uuid", PromoteAutoFlagsResponsePB.getDefaultInstance()));
      when(mockGFlagsValidation.ysqlMajorVersionUpgrade(anyString(), anyString()))
          .thenReturn(false);
    } catch (Exception ignored) {
      fail();
    }

    factory
        .forUniverse(defaultUniverse)
        .setValue(RunYsqlUpgrade.USE_SINGLE_CONNECTION_PARAM, "true");
    factory
        .forUniverse(defaultUniverse)
        .setValue(UniverseConfKeys.autoFlagUpdateSleepTimeInMilliSeconds.getKey(), "0ms");
    UniverseDefinitionTaskParams.PrevYBSoftwareConfig ybSoftwareConfig =
        new UniverseDefinitionTaskParams.PrevYBSoftwareConfig();
    ybSoftwareConfig.setAutoFlagConfigVersion(1);
    ybSoftwareConfig.setSoftwareVersion("2.21.0.0-b1");
    TestHelper.updateUniversePrevSoftwareConfig(defaultUniverse, ybSoftwareConfig);
    TestHelper.updateUniverseIsRollbackAllowed(defaultUniverse, true);
    TestHelper.updateUniverseVersion(defaultUniverse, "2.21.0.0-b2");
    TestHelper.updateUniverseSoftwareUpgradeState(
        defaultUniverse, UniverseDefinitionTaskParams.SoftwareUpgradeState.PreFinalize);
  }

  private TaskInfo submitTask(FinalizeUpgradeParams requestParams) {
    return submitTask(requestParams, TaskType.FinalizeUpgrade, commissioner);
  }

  @Test
  public void testFinalizeWithUpgradeSystemCatalog() throws Exception {
    FinalizeUpgradeParams params = new FinalizeUpgradeParams();
    params.setUniverseUUID(defaultUniverse.getUniverseUUID());
    params.upgradeSystemCatalog = true;
    TaskInfo taskInfo = submitTask(params);
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertEquals(8, subTasks.size());
    int position = 0;
    assertTaskType(subTasksByPosition.get(position++), TaskType.UpdateConsistencyCheck);
    assertTaskType(subTasksByPosition.get(position++), TaskType.FreezeUniverse);
    assertTaskType(subTasksByPosition.get(position++), TaskType.UpdateUniverseState);
    assertTaskType(subTasksByPosition.get(position++), TaskType.PromoteAutoFlags);
    assertTaskType(subTasksByPosition.get(position++), TaskType.RunYsqlUpgrade);
    assertTaskType(subTasksByPosition.get(position++), TaskType.EnablePitrConfig);
    assertTaskType(subTasksByPosition.get(position++), TaskType.UpdateUniverseState);
    assertTaskType(subTasksByPosition.get(position++), TaskType.UniverseUpdateSucceeded);
    assertEquals(100.0, taskInfo.getPercentCompleted(), 0);
    assertEquals(Success, taskInfo.getTaskState());
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertFalse(defaultUniverse.getUniverseDetails().isSoftwareRollbackAllowed);
    assertNull(defaultUniverse.getUniverseDetails().prevYBSoftwareConfig);
    assertEquals(
        UniverseDefinitionTaskParams.SoftwareUpgradeState.Ready,
        defaultUniverse.getUniverseDetails().softwareUpgradeState);
  }

  @Test
  public void testFinalizeWithNoSystemCatalog() {
    FinalizeUpgradeParams params = new FinalizeUpgradeParams();
    params.setUniverseUUID(defaultUniverse.getUniverseUUID());
    params.upgradeSystemCatalog = false;
    TaskInfo taskInfo = submitTask(params);
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertEquals(7, subTasks.size());
    int position = 0;
    assertTaskType(subTasksByPosition.get(position++), TaskType.UpdateConsistencyCheck);
    assertTaskType(subTasksByPosition.get(position++), TaskType.FreezeUniverse);
    assertTaskType(subTasksByPosition.get(position++), TaskType.UpdateUniverseState);
    assertTaskType(subTasksByPosition.get(position++), TaskType.PromoteAutoFlags);
    assertTaskType(subTasksByPosition.get(position++), TaskType.EnablePitrConfig);
    assertTaskType(subTasksByPosition.get(position++), TaskType.UpdateUniverseState);
    assertTaskType(subTasksByPosition.get(position++), TaskType.UniverseUpdateSucceeded);
    assertEquals(100.0, taskInfo.getPercentCompleted(), 0);
    assertEquals(Success, taskInfo.getTaskState());
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertFalse(defaultUniverse.getUniverseDetails().isSoftwareRollbackAllowed);
    assertNull(defaultUniverse.getUniverseDetails().prevYBSoftwareConfig);
    assertEquals(
        UniverseDefinitionTaskParams.SoftwareUpgradeState.Ready,
        defaultUniverse.getUniverseDetails().softwareUpgradeState);
  }

  @Test
  public void testFinalizeYsqlMajorUpgrade() throws Exception {
    when(mockSoftwareUpgradeHelper.isYsqlMajorVersionUpgradeRequired(any(), any(), any()))
        .thenReturn(true);
    when(mockSoftwareUpgradeHelper.isSuperUserRequiredForCatalogUpgrade(any(), any(), any()))
        .thenReturn(true);
    when(mockClient.getYsqlMajorCatalogUpgradeState())
        .thenReturn(
            new GetYsqlMajorCatalogUpgradeStateResponse(
                0L, null, null, YsqlMajorCatalogUpgradeState.YSQL_MAJOR_CATALOG_UPGRADE_DONE));
    FinalizeUpgradeParams params = new FinalizeUpgradeParams();
    params.setUniverseUUID(defaultUniverse.getUniverseUUID());
    params.upgradeSystemCatalog = true;
    TaskInfo taskInfo = submitTask(params);
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    assertEquals(10, subTasks.size());
    int position = 0;
    assertTaskType(subTasksByPosition.get(position++), TaskType.UpdateConsistencyCheck);
    assertTaskType(subTasksByPosition.get(position++), TaskType.FreezeUniverse);
    assertTaskType(subTasksByPosition.get(position++), TaskType.UpdateUniverseState);
    assertTaskType(subTasksByPosition.get(position++), TaskType.FinalizeYsqlMajorCatalogUpgrade);
    assertTaskType(subTasksByPosition.get(position++), TaskType.PromoteAutoFlags);
    assertTaskType(subTasksByPosition.get(position++), TaskType.RunYsqlUpgrade);
    assertTaskType(subTasksByPosition.get(position++), TaskType.ManageCatalogUpgradeSuperUser);
    assertTaskType(subTasksByPosition.get(position++), TaskType.EnablePitrConfig);
    assertTaskType(subTasksByPosition.get(position++), TaskType.UpdateUniverseState);
    assertTaskType(subTasksByPosition.get(position++), TaskType.UniverseUpdateSucceeded);
    assertEquals(100.0, taskInfo.getPercentCompleted(), 0);
    assertEquals(Success, taskInfo.getTaskState());
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertFalse(defaultUniverse.getUniverseDetails().isSoftwareRollbackAllowed);
    assertNull(defaultUniverse.getUniverseDetails().prevYBSoftwareConfig);
    assertEquals(
        UniverseDefinitionTaskParams.SoftwareUpgradeState.Ready,
        defaultUniverse.getUniverseDetails().softwareUpgradeState);
  }

  @Test
  public void testFinalizeRetries() {
    FinalizeUpgradeParams taskParams = new FinalizeUpgradeParams();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = -1;
    taskParams.creatingUser = defaultUser;
    TestUtils.setFakeHttpContext(defaultUser);
    super.verifyTaskRetries(
        defaultCustomer,
        CustomerTask.TaskType.FinalizeUpgrade,
        CustomerTask.TargetType.Universe,
        defaultUniverse.getUniverseUUID(),
        TaskType.FinalizeUpgrade,
        taskParams,
        false);
    checkUniverseNodesStates(taskParams.getUniverseUUID());
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertFalse(defaultUniverse.getUniverseDetails().isSoftwareRollbackAllowed);
    assertNull(defaultUniverse.getUniverseDetails().prevYBSoftwareConfig);
    assertEquals(
        UniverseDefinitionTaskParams.SoftwareUpgradeState.Ready,
        defaultUniverse.getUniverseDetails().softwareUpgradeState);
  }
}
