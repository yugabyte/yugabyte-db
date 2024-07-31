// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
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
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.google.common.collect.ImmutableList;
import com.google.common.net.HostAndPort;
import com.yugabyte.yw.commissioner.MockUpgrade;
import com.yugabyte.yw.commissioner.UpgradeTaskBase;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.TestHelper;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.forms.SoftwareUpgradeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.SoftwareUpgradeState;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeOption;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.RuntimeConfigEntry;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Optional;
import java.util.Set;
import java.util.UUID;
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
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import org.yb.client.IsInitDbDoneResponse;
import org.yb.client.UpgradeYsqlResponse;

@RunWith(JUnitParamsRunner.class)
@Slf4j
public class SoftwareUpgradeYBTest extends UpgradeTaskTest {

  @Rule public MockitoRule rule = MockitoJUnit.rule();

  @InjectMocks private SoftwareUpgrade softwareUpgrade;

  @Override
  @Before
  public void setUp() {
    super.setUp();

    attachHooks("SoftwareUpgrade");

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

    when(mockAutoFlagUtil.upgradeRequireFinalize(anyString(), anyString())).thenReturn(true);

    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = "2.21.0.0-b2";
    taskParams.clusters.add(defaultUniverse.getUniverseDetails().getPrimaryCluster());
    mockDBServerVersion(
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion,
        taskParams.ybSoftwareVersion,
        defaultUniverse.getMasters().size() + defaultUniverse.getTServers().size());
    TaskInfo taskInfo = submitTask(taskParams, defaultUniverse.getVersion());
    verify(mockNodeManager, times(71)).nodeCommand(any(), any());
    verify(mockNodeUniverseManager, times(10)).runCommand(any(), any(), anyList(), any());

    MockUpgrade mockUpgrade = initMockUpgrade();
    mockUpgrade
        .precheckTasks(getPrecheckTasks(true))
        .addTasks(TaskType.UpdateUniverseState)
        .addSimultaneousTasks(TaskType.AnsibleConfigureServers, defaultUniverse.getMasters().size())
        .addSimultaneousTasks(
            TaskType.AnsibleConfigureServers, defaultUniverse.getTServers().size())
        .addTasks(TaskType.XClusterInfoPersist)
        .addSimultaneousTasks(
            TaskType.AnsibleConfigureServers, defaultUniverse.getTServers().size())
        .upgradeRound(UpgradeOption.ROLLING_UPGRADE)
        .withContext(
            UpgradeTaskBase.UpgradeContext.builder()
                .reconfigureMaster(false)
                .runBeforeStopping(false)
                .processInactiveMaster(true)
                .targetSoftwareVersion("2.21.0.0-b2")
                .build())
        .task(TaskType.AnsibleConfigureServers)
        .applyRound()
        .addSimultaneousTasks(TaskType.CheckSoftwareVersion, defaultUniverse.getTServers().size())
        .addTasks(TaskType.StoreAutoFlagConfigVersion)
        .addTasks(TaskType.PromoteAutoFlags)
        .addTasks(TaskType.UpdateSoftwareVersion)
        .addTasks(TaskType.UpdateUniverseState)
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
  public void testSoftwareUpgradeWithAutoFinalize() {
    updateDefaultUniverseTo5Nodes(true);

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

    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = "2.21.0.0-b2";
    taskParams.rollbackSupport = false;
    taskParams.clusters.add(defaultUniverse.getUniverseDetails().getPrimaryCluster());
    mockDBServerVersion(
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion,
        taskParams.ybSoftwareVersion,
        defaultUniverse.getMasters().size() + defaultUniverse.getTServers().size());
    TaskInfo taskInfo = submitTask(taskParams, defaultUniverse.getVersion());
    verify(mockNodeManager, times(71)).nodeCommand(any(), any());
    verify(mockNodeUniverseManager, times(10)).runCommand(any(), any(), anyList(), any());

    MockUpgrade mockUpgrade = initMockUpgrade();
    mockUpgrade
        .precheckTasks(getPrecheckTasks(true))
        .addTasks(TaskType.UpdateUniverseState)
        .addSimultaneousTasks(TaskType.AnsibleConfigureServers, defaultUniverse.getMasters().size())
        .addSimultaneousTasks(
            TaskType.AnsibleConfigureServers, defaultUniverse.getTServers().size())
        .addTasks(TaskType.XClusterInfoPersist)
        .addSimultaneousTasks(
            TaskType.AnsibleConfigureServers, defaultUniverse.getTServers().size())
        .upgradeRound(UpgradeOption.ROLLING_UPGRADE)
        .withContext(
            UpgradeTaskBase.UpgradeContext.builder()
                .reconfigureMaster(false)
                .runBeforeStopping(false)
                .processInactiveMaster(true)
                .targetSoftwareVersion("2.21.0.0-b2")
                .build())
        .task(TaskType.AnsibleConfigureServers)
        .applyRound()
        .addSimultaneousTasks(TaskType.CheckSoftwareVersion, defaultUniverse.getTServers().size())
        .addTasks(TaskType.StoreAutoFlagConfigVersion)
        .addTasks(TaskType.PromoteAutoFlags)
        .addTasks(TaskType.UpdateSoftwareVersion)
        .addTasks(TaskType.UpdateUniverseState)
        .addTasks(TaskType.RunYsqlUpgrade)
        .addTasks(TaskType.PromoteAutoFlags)
        .addTasks(TaskType.UpdateUniverseState)
        .verifyTasks(taskInfo.getSubTasks());

    assertEquals(100.0, taskInfo.getPercentCompleted(), 0);
    assertEquals(Success, taskInfo.getTaskState());
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertFalse(defaultUniverse.getUniverseDetails().isSoftwareRollbackAllowed);
    assertNull(defaultUniverse.getUniverseDetails().prevYBSoftwareConfig);
    assertEquals(
        SoftwareUpgradeState.Ready, defaultUniverse.getUniverseDetails().softwareUpgradeState);
  }

  @Test
  @Parameters({"false", "true"})
  public void testSoftwareUpgradeWithReadReplica(boolean enableYSQL) {
    updateDefaultUniverseTo5Nodes(enableYSQL);

    // Adding Read Replica cluster.
    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.numNodes = 3;
    userIntent.replicationFactor = 3;
    userIntent.ybSoftwareVersion = "2.21.0.0-b1";
    userIntent.accessKeyCode = "demo-access";
    userIntent.regionList = ImmutableList.of(region.getUuid());
    userIntent.enableYSQL = enableYSQL;
    userIntent.provider = defaultProvider.getUuid().toString();

    PlacementInfo pi = new PlacementInfo();
    AvailabilityZone az4 = AvailabilityZone.createOrThrow(region, "az-4", "AZ 4", "subnet-1");
    AvailabilityZone az5 = AvailabilityZone.createOrThrow(region, "az-5", "AZ 5", "subnet-2");
    AvailabilityZone az6 = AvailabilityZone.createOrThrow(region, "az-6", "AZ 6", "subnet-3");

    // Currently read replica zones are always affinitized.
    PlacementInfoUtil.addPlacementZone(az4.getUuid(), pi, 1, 1, false);
    PlacementInfoUtil.addPlacementZone(az5.getUuid(), pi, 1, 1, true);
    PlacementInfoUtil.addPlacementZone(az6.getUuid(), pi, 1, 1, false);

    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            ApiUtils.mockUniverseUpdaterWithReadReplica(userIntent, pi));

    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = "2.21.0.0-b2";
    taskParams.clusters.add(defaultUniverse.getUniverseDetails().getPrimaryCluster());
    mockDBServerVersion(
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion,
        taskParams.ybSoftwareVersion,
        defaultUniverse.getMasters().size() + defaultUniverse.getTServers().size());
    TaskInfo taskInfo = submitTask(taskParams, defaultUniverse.getVersion());
    verify(mockNodeManager, times(95)).nodeCommand(any(), any());
    verify(mockNodeUniverseManager, times(16)).runCommand(any(), any(), anyList(), any());

    MockUpgrade mockUpgrade = initMockUpgrade();
    mockUpgrade
        .precheckTasks(getPrecheckTasks(true))
        .addTasks(TaskType.UpdateUniverseState)
        .addSimultaneousTasks(TaskType.AnsibleConfigureServers, defaultUniverse.getMasters().size())
        .addSimultaneousTasks(
            TaskType.AnsibleConfigureServers, defaultUniverse.getTServersInPrimaryCluster().size())
        .addTasks(TaskType.XClusterInfoPersist)
        .addSimultaneousTasks(
            TaskType.AnsibleConfigureServers, defaultUniverse.getTServers().size())
        .upgradeRound(UpgradeOption.ROLLING_UPGRADE)
        .withContext(
            UpgradeTaskBase.UpgradeContext.builder()
                .reconfigureMaster(false)
                .runBeforeStopping(false)
                .processInactiveMaster(true)
                .targetSoftwareVersion("2.21.0.0-b2")
                .build())
        .task(TaskType.AnsibleConfigureServers)
        .applyRound()
        .addSimultaneousTasks(TaskType.CheckSoftwareVersion, defaultUniverse.getTServers().size())
        .addTasks(TaskType.StoreAutoFlagConfigVersion)
        .addTasks(TaskType.PromoteAutoFlags)
        .addTasks(TaskType.UpdateSoftwareVersion)
        .addTasks(TaskType.UpdateUniverseState)
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
    verify(mockNodeManager, times(51)).nodeCommand(any(), commandParams.capture());
    verify(mockNodeUniverseManager, times(10)).runCommand(any(), any(), anyList(), any());

    MockUpgrade mockUpgrade = initMockUpgrade();
    mockUpgrade
        .precheckTasks(getPrecheckTasks(false))
        .addTasks(TaskType.UpdateUniverseState)
        .addSimultaneousTasks(TaskType.AnsibleConfigureServers, defaultUniverse.getMasters().size())
        .addSimultaneousTasks(
            TaskType.AnsibleConfigureServers, defaultUniverse.getTServers().size())
        .addTasks(TaskType.XClusterInfoPersist)
        .addSimultaneousTasks(
            TaskType.AnsibleConfigureServers, defaultUniverse.getTServers().size())
        .upgradeRound(UpgradeOption.NON_ROLLING_UPGRADE)
        .withContext(
            UpgradeTaskBase.UpgradeContext.builder()
                .reconfigureMaster(false)
                .runBeforeStopping(false)
                .processInactiveMaster(true)
                .targetSoftwareVersion("2.21.0.0-b2")
                .build())
        .task(TaskType.AnsibleConfigureServers)
        .applyRound()
        .addSimultaneousTasks(TaskType.CheckSoftwareVersion, defaultUniverse.getTServers().size())
        .addTasks(TaskType.StoreAutoFlagConfigVersion)
        .addTasks(TaskType.PromoteAutoFlags)
        .addTasks(TaskType.UpdateSoftwareVersion)
        .addTasks(TaskType.UpdateUniverseState)
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
  public void testSoftwareUpgradeRetries() {
    RuntimeConfigEntry.upsertGlobal("yb.checks.leaderless_tablets.enabled", "false");
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
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertTrue(defaultUniverse.getUniverseDetails().isSoftwareRollbackAllowed);
    assertEquals(
        SoftwareUpgradeState.Ready, defaultUniverse.getUniverseDetails().softwareUpgradeState);
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
              NodeDetails node = defaultUniverse.getNodeByPrivateIP(ip);
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

    MockUpgrade mockUpgrade = initMockUpgrade();
    mockUpgrade
        .precheckTasks(
            TaskType.CheckUpgrade, TaskType.CheckMemory, TaskType.CheckLocale, TaskType.CheckGlibc)
        .addTasks(TaskType.UpdateUniverseState)
        .addSimultaneousTasks(TaskType.AnsibleConfigureServers, defaultUniverse.getMasters().size())
        .addSimultaneousTasks(
            TaskType.AnsibleConfigureServers, defaultUniverse.getTServers().size())
        .addTasks(TaskType.XClusterInfoPersist)
        .addSimultaneousTasks(TaskType.AnsibleConfigureServers, tserverNames.size())
        .upgradeRound(UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE)
        .withContext(
            UpgradeTaskBase.UpgradeContext.builder()
                .reconfigureMaster(false)
                .runBeforeStopping(false)
                .processInactiveMaster(true)
                .targetSoftwareVersion("2.21.0.0-b2")
                .build())
        .task(TaskType.AnsibleConfigureServers)
        .applyToNodes(masterNames, tserverNames)
        .addSimultaneousTasks(TaskType.CheckSoftwareVersion, defaultUniverse.getTServers().size())
        .addTasks(TaskType.StoreAutoFlagConfigVersion)
        .addTasks(TaskType.PromoteAutoFlags)
        .addTasks(TaskType.UpdateSoftwareVersion)
        .addTasks(TaskType.UpdateUniverseState)
        .verifyTasks(taskInfo.getSubTasks());
    assertTrue(defaultUniverse.getUniverseDetails().isSoftwareRollbackAllowed);
    assertEquals(
        "2.21.0.0-b1",
        defaultUniverse.getUniverseDetails().prevYBSoftwareConfig.getSoftwareVersion());
    assertEquals(
        SoftwareUpgradeState.Ready, defaultUniverse.getUniverseDetails().softwareUpgradeState);
  }

  private MockUpgrade initMockUpgrade() {
    return initMockUpgrade(SoftwareUpgrade.class);
  }

  @Override
  protected TaskType[] getPrecheckTasks(boolean hasRollingRestarts) {
    List<TaskType> lst =
        new ArrayList<>(
            Arrays.asList(
                TaskType.CheckUpgrade,
                TaskType.CheckMemory,
                TaskType.CheckLocale,
                TaskType.CheckGlibc));
    if (hasRollingRestarts) {
      lst.add(0, TaskType.CheckNodesAreSafeToTakeDown);
    }
    return lst.toArray(new TaskType[0]);
  }
}
