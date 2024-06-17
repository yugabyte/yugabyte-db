// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import static com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType.MASTER;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.verifyNoMoreInteractions;

import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.MockUpgrade;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.forms.ThirdpartySoftwareUpgradeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.models.AccessKey;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.RuntimeConfigEntry;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.ArrayList;
import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
@Slf4j
public class ThirdpartySoftwareUpgradeTest extends UpgradeTaskTest {

  @InjectMocks private ThirdpartySoftwareUpgrade thirdpartySoftwareUpgrade;

  private int expectedUniverseVersion = 2;

  private List<Integer> nodesToFilter = new ArrayList<>();

  @Override
  @Before
  public void setUp() {
    super.setUp();
    attachHooks("ThirdpartySoftwareUpgrade");
    thirdpartySoftwareUpgrade.setUserTaskUUID(UUID.randomUUID());
    setCheckNodesAreSafeToTakeDown(mockClient);
    setUnderReplicatedTabletsMock();
    setFollowerLagMock();
  }

  private TaskInfo submitTask(ThirdpartySoftwareUpgradeParams requestParams, int version) {
    return submitTask(requestParams, TaskType.ThirdpartySoftwareUpgrade, commissioner, version);
  }

  @Test
  public void testInstanceReprovision() {
    testInstanceReprovision(false);
  }

  @Test
  public void testOnpremManualProvisionException() {
    AccessKey.KeyInfo keyInfo = new AccessKey.KeyInfo();
    onPremProvider.getDetails().skipProvisioning = true;
    onPremProvider.save();
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(),
        details -> {
          UniverseDefinitionTaskParams.UserIntent userIntent =
              details.getUniverseDetails().getPrimaryCluster().userIntent;
          userIntent.provider = onPremProvider.getUuid().toString();
          userIntent.providerType = Common.CloudType.onprem;
          userIntent.accessKeyCode = ApiUtils.DEFAULT_ACCESS_KEY_CODE;
        });
    expectedUniverseVersion++;
    ThirdpartySoftwareUpgradeParams taskParams = new ThirdpartySoftwareUpgradeParams();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    assertThrows(RuntimeException.class, () -> submitTask(taskParams, expectedUniverseVersion));
    verifyNoMoreInteractions(mockNodeManager);
  }

  @Test
  public void testOnpremOK() {
    AccessKey.create(
        gcpProvider.getUuid(), ApiUtils.DEFAULT_ACCESS_KEY_CODE, new AccessKey.KeyInfo());
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(),
        details -> {
          UniverseDefinitionTaskParams.UserIntent userIntent =
              details.getUniverseDetails().getPrimaryCluster().userIntent;
          userIntent.provider = onPremProvider.getUuid().toString();
          userIntent.providerType = Common.CloudType.onprem;
          userIntent.accessKeyCode = ApiUtils.DEFAULT_ACCESS_KEY_CODE;
        });
    expectedUniverseVersion++;
    testInstanceReprovision(true);
  }

  @Override
  protected List<Integer> getRollingUpgradeNodeOrder(UniverseTaskBase.ServerType serverType) {
    return super.getRollingUpgradeNodeOrder(serverType).stream()
        .filter(idx -> !nodesToFilter.contains(idx))
        .collect(Collectors.toList());
  }

  private void testInstanceReprovision(boolean forceAll) {
    ThirdpartySoftwareUpgradeParams taskParams = new ThirdpartySoftwareUpgradeParams();
    taskParams.setForceAll(forceAll);
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;

    TaskInfo taskInfo = submitTask(taskParams, expectedUniverseVersion);

    int nodeCnt = getRollingUpgradeNodeOrder(MASTER).size();
    verify(mockNodeManager, times(13 * nodeCnt)).nodeCommand(any(), any());

    MockUpgrade mockUpgrade = initMockUpgrade();
    mockUpgrade
        .precheckTasks(getPrecheckTasks(true))
        .upgradeRound(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, true)
        .tserverTask(TaskType.AnsibleSetupServer) // reprovision
        .tserverTask(TaskType.AnsibleConfigureServers) // reprovision
        .tserverTask(TaskType.AnsibleConfigureServers) // gflags tserver
        .tserverTask(TaskType.AnsibleConfigureServers) // gflags master
        .applyRound()
        .verifyTasks(taskInfo.getSubTasks());

    assertEquals(100.0, taskInfo.getPercentCompleted(), 0);
    assertEquals(Success, taskInfo.getTaskState());
  }

  @Test
  public void testInstanceReprovisionRetries() {
    RuntimeConfigEntry.upsertGlobal("yb.checks.leaderless_tablets.enabled", "false");
    ThirdpartySoftwareUpgradeParams taskParams = new ThirdpartySoftwareUpgradeParams();
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.expectedUniverseVersion = -1;
    taskParams.creatingUser = defaultUser;
    TestUtils.setFakeHttpContext(defaultUser);
    super.verifyTaskRetries(
        defaultCustomer,
        CustomerTask.TaskType.ThirdpartySoftwareUpgrade,
        CustomerTask.TargetType.Universe,
        defaultUniverse.getUniverseUUID(),
        TaskType.ThirdpartySoftwareUpgrade,
        taskParams,
        false);
  }

  private MockUpgrade initMockUpgrade() {
    return initMockUpgrade(ThirdpartySoftwareUpgrade.class);
  }
}
