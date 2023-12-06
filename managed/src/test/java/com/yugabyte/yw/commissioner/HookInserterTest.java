// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.commissioner.tasks.upgrade.RestartUniverse;
import com.yugabyte.yw.commissioner.tasks.upgrade.UpgradeTaskTest;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.forms.RestartTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Hook;
import com.yugabyte.yw.models.HookScope;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.stream.Collectors;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.junit.MockitoJUnitRunner;

/*
 * Reuses the test code for universe upgrade to test triggering.
 */
@RunWith(MockitoJUnitRunner.class)
public class HookInserterTest extends UpgradeTaskTest {

  private Hook providerHook, universeHook;
  private HookScope universeScope, providerScope;

  // Use an upgrade universe task to test triggering
  @InjectMocks private RestartUniverse restartUniverse;

  @Override
  @Before
  public void setUp() {
    super.setUp();

    // Create universe and provider level scopes
    providerHook =
        Hook.create(
            defaultCustomer.getUuid(),
            "providerHook",
            Hook.ExecutionLang.Bash,
            "providerHook\nTEXT\n",
            true,
            null);
    universeHook =
        Hook.create(
            defaultCustomer.getUuid(),
            "universeHook",
            Hook.ExecutionLang.Bash,
            "universeHook\nTEXT\n",
            false,
            null);
    attachHooks("RestartUniverse");
    universeScope =
        HookScope.create(
            defaultCustomer.getUuid(),
            HookScope.TriggerType.PreRestartUniverse,
            defaultUniverse,
            null);
    providerScope =
        HookScope.create(
            defaultCustomer.getUuid(), HookScope.TriggerType.PreRestartUniverse, defaultProvider);
    providerScope.addHook(providerHook);
    universeScope.addHook(universeHook);

    restartUniverse.setUserTaskUUID(UUID.randomUUID());

    setUnderReplicatedTabletsMock();
    setFollowerLagMock();
  }

  @Test
  public void testHookInserterTrigger() {
    RestartTaskParams taskParams = new RestartTaskParams();
    TaskInfo taskInfo = submitTask(taskParams, TaskType.RestartUniverse, commissioner);
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));

    // Assert that hook preUpgradeHook has been created
    List<TaskInfo> hookTasks = subTasksByPosition.get(0);
    assertTaskType(hookTasks, TaskType.RunHooks);
    assertEquals(hookTasks.size(), 3);

    // Assert that hook providerHook has been created
    hookTasks = subTasksByPosition.get(1);
    assertTaskType(hookTasks, TaskType.RunHooks);
    assertEquals(hookTasks.size(), 3);

    // Assert that hook universeHook has been created
    hookTasks = subTasksByPosition.get(2);
    assertTaskType(hookTasks, TaskType.RunHooks);
    assertEquals(hookTasks.size(), 3);

    // Assert that no more hooks were added
    hookTasks = subTasksByPosition.get(3);
    assertTrue(hookTasks.get(0).getTaskType() != TaskType.RunHooks);
  }

  @Test
  public void testHookInserterTriggerWithReadReplica() {
    // Create read replica
    UniverseDefinitionTaskParams.UserIntent curIntent =
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent;
    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.numNodes = 3;
    userIntent.ybSoftwareVersion = curIntent.ybSoftwareVersion;
    userIntent.accessKeyCode = curIntent.accessKeyCode;
    userIntent.regionList = ImmutableList.of(region.getUuid());
    userIntent.deviceInfo = new DeviceInfo();
    userIntent.deviceInfo.numVolumes = 1;
    userIntent.provider = gcpProvider.getUuid().toString();
    PlacementInfo pi = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(az1.getUuid(), pi, 1, 1, false);
    PlacementInfoUtil.addPlacementZone(az2.getUuid(), pi, 1, 1, false);
    PlacementInfoUtil.addPlacementZone(az3.getUuid(), pi, 1, 1, true);
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            ApiUtils.mockUniverseUpdaterWithReadReplica(userIntent, pi));

    // Create hooks for provider
    Hook gcpProviderHook =
        Hook.create(
            defaultCustomer.getUuid(),
            "gcpProviderHook",
            Hook.ExecutionLang.Bash,
            "gcpProviderHook\nTEXT\n",
            true,
            null);
    HookScope gcpProviderScope =
        HookScope.create(
            defaultCustomer.getUuid(), HookScope.TriggerType.PreRestartUniverse, gcpProvider);
    gcpProviderScope.addHook(gcpProviderHook);

    RestartTaskParams taskParams = new RestartTaskParams();
    TaskInfo taskInfo = submitTask(taskParams, TaskType.RestartUniverse, commissioner, 3);
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));

    // Assert that hook gcpProviderHook has been created
    List<TaskInfo> hookTasks = subTasksByPosition.get(0);
    assertTaskType(hookTasks, TaskType.RunHooks);
    assertEquals(hookTasks.size(), 3);

    // Assert that hook preUpgradeHook has been created
    hookTasks = subTasksByPosition.get(1);
    assertTaskType(hookTasks, TaskType.RunHooks);
    assertEquals(hookTasks.size(), 6);

    // Assert that hook providerHook has been created
    hookTasks = subTasksByPosition.get(2);
    assertTaskType(hookTasks, TaskType.RunHooks);
    assertEquals(hookTasks.size(), 3);

    // Assert that hook universeHook has been created
    hookTasks = subTasksByPosition.get(3);
    assertTaskType(hookTasks, TaskType.RunHooks);
    assertEquals(hookTasks.size(), 6);

    // Assert that no more hooks were added
    hookTasks = subTasksByPosition.get(4);
    assertTrue(hookTasks.get(0).getTaskType() != TaskType.RunHooks);
  }

  @Test
  public void testHookInserterTriggerWithSudoDisabled() {
    factory.globalRuntimeConf().setValue(ENABLE_SUDO_PATH, "false");
    RestartTaskParams taskParams = new RestartTaskParams();
    TaskInfo taskInfo = submitTask(taskParams, TaskType.RestartUniverse, commissioner);
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));

    // Assert that hook universeHook has been created, since it is not sudo
    List<TaskInfo> hookTasks = subTasksByPosition.get(0);
    assertTaskType(hookTasks, TaskType.RunHooks);
    assertEquals(hookTasks.size(), 3);

    // Assert that no more hooks were added, since the rest are sudo
    hookTasks = subTasksByPosition.get(3);
    assertTrue(hookTasks.get(0).getTaskType() != TaskType.RunHooks);
  }

  @Test
  public void testHookInserterTriggerWithCustomHooksDisabled() {
    RestartTaskParams taskParams = new RestartTaskParams();
    TaskInfo taskInfo = submitTask(taskParams, TaskType.RestartUniverse, commissioner);
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));

    // Assert that the hook has not been created
    List<TaskInfo> hookTasks = subTasksByPosition.get(0);
    assert (hookTasks.get(0).getTaskType() != TaskType.RunHooks);
  }
}
