// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
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
import com.yugabyte.yw.commissioner.tasks.subtasks.RunYsqlUpgrade;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.TestHelper;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.forms.SoftwareUpgradeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeOption;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.RuntimeConfigEntry;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.TaskType;
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
import org.mockito.InjectMocks;
import org.mockito.Mockito;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import org.yb.client.IsInitDbDoneResponse;
import org.yb.client.UpgradeYsqlResponse;

@RunWith(JUnitParamsRunner.class)
@Slf4j
public class SoftwareUpgradeTest extends UpgradeTaskTest {

  @Rule public MockitoRule rule = MockitoJUnit.rule();

  @InjectMocks private SoftwareUpgrade softwareUpgrade;

  private static final String OLD_VERSION = "2.15.0.0-b1";
  private static final String NEW_VERSION = "2.17.0.0-b1";

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

    List<String> command2 = new ArrayList<>();
    command2.add("locale");
    command2.add("-a");
    command2.add("|");
    command2.add("grep");
    command2.add("-E");
    command2.add("-q");
    command2.add("\"en_US.utf8|en_US.UTF-8\"");
    command2.add("&&");
    command2.add("echo");
    command2.add("\"Locale is present\"");
    command2.add("||");
    command2.add("echo");
    command2.add("\"Locale is not present\"");
    ShellResponse shellResponse2 = new ShellResponse();
    shellResponse2.message = "Command output:\\nLocale is present";
    shellResponse2.code = 0;
    when(mockNodeUniverseManager.runCommand(any(), any(), eq(command2), any()))
        .thenReturn(shellResponse2);

    try {
      UpgradeYsqlResponse mockUpgradeYsqlResponse = new UpgradeYsqlResponse(1000, "", null);
      when(mockYBClient.getClientWithConfig(any())).thenReturn(mockClient);
      when(mockClient.upgradeYsql(any(HostAndPort.class), anyBoolean()))
          .thenReturn(mockUpgradeYsqlResponse);
      IsInitDbDoneResponse mockIsInitDbDoneResponse =
          new IsInitDbDoneResponse(1000, "", true, true, null, null);
      when(mockClient.getIsInitDbDone()).thenReturn(mockIsInitDbDoneResponse);
      setCheckNodesAreSafeToTakeDown(mockClient);
    } catch (Exception ignored) {
      fail();
    }

    factory
        .forUniverse(defaultUniverse)
        .setValue(RunYsqlUpgrade.USE_SINGLE_CONNECTION_PARAM, "true");

    setUnderReplicatedTabletsMock();
    setFollowerLagMock();
    RuntimeConfigEntry.upsertGlobal("yb.checks.leaderless_tablets.enabled", "false");
  }

  private TaskInfo submitTask(SoftwareUpgradeParams requestParams) {
    return submitTask(requestParams, TaskType.SoftwareUpgrade, commissioner);
  }

  private TaskInfo submitTask(SoftwareUpgradeParams requestParams, int expectedVersion) {
    return submitTask(requestParams, TaskType.SoftwareUpgrade, commissioner, expectedVersion);
  }

  @Test
  public void testSoftwareUpgradeWithSameVersion() {
    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = "2.14.12.0-b1";
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
  public void testSoftwareUpgrade() {
    updateDefaultUniverseTo5Nodes(true, OLD_VERSION);

    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = NEW_VERSION;
    taskParams.clusters.add(defaultUniverse.getUniverseDetails().getPrimaryCluster());
    mockDBServerVersion(
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion,
        taskParams.ybSoftwareVersion,
        defaultUniverse.getMasters().size() + defaultUniverse.getTServers().size());
    TaskInfo taskInfo = submitTask(taskParams, defaultUniverse.getVersion());
    verify(mockNodeUniverseManager, times(10)).runCommand(any(), any(), anyList(), any());

    assertEquals(100.0, taskInfo.getPercentCompleted(), 0);
    assertEquals(Success, taskInfo.getTaskState());

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
                .targetSoftwareVersion(NEW_VERSION)
                .build())
        .task(TaskType.AnsibleConfigureServers)
        .applyRound()
        .addSimultaneousTasks(TaskType.CheckSoftwareVersion, defaultUniverse.getTServers().size())
        .addTasks(TaskType.PromoteAutoFlags)
        .addTasks(TaskType.RunYsqlUpgrade)
        .addTasks(TaskType.UpdateSoftwareVersion)
        .addTasks(TaskType.UpdateUniverseState)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testSoftwareUpgradeAndInstallYbc() {
    updateDefaultUniverseTo5Nodes(true, OLD_VERSION);
    TestHelper.updateUniverseSystemdDetails(defaultUniverse);

    Mockito.doNothing().when(mockYbcManager).waitForYbc(any(), any());

    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = NEW_VERSION;
    taskParams.installYbc = true;
    taskParams.clusters.add(defaultUniverse.getUniverseDetails().getPrimaryCluster());
    mockDBServerVersion(
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion,
        taskParams.ybSoftwareVersion,
        defaultUniverse.getMasters().size() + defaultUniverse.getTServers().size());
    TaskInfo taskInfo = submitTask(taskParams, defaultUniverse.getVersion());
    verify(mockNodeUniverseManager, times(10)).runCommand(any(), any(), anyList(), any());

    assertEquals(100.0, taskInfo.getPercentCompleted(), 0);
    assertEquals(Success, taskInfo.getTaskState());

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
                .targetSoftwareVersion(NEW_VERSION)
                .build())
        .task(TaskType.AnsibleSetupServer)
        .task(TaskType.AnsibleConfigureServers)
        .applyRound()
        // Install ybc
        .addSimultaneousTasks(
            TaskType.AnsibleConfigureServers, defaultUniverse.getTServers().size())
        .addTasks(TaskType.WaitForYbcServer)
        .addTasks(TaskType.UpdateUniverseYbcDetails)
        .addSimultaneousTasks(TaskType.CheckSoftwareVersion, defaultUniverse.getTServers().size())
        .addTasks(TaskType.PromoteAutoFlags)
        .addTasks(TaskType.RunYsqlUpgrade)
        .addTasks(TaskType.UpdateSoftwareVersion)
        .addTasks(TaskType.UpdateUniverseState)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testSoftwareUpgradeAndPromoteAutoFlagsOnOthers() {
    updateDefaultUniverseTo5Nodes(true, OLD_VERSION);

    Universe xClusterUniv = ModelFactory.createUniverse("univ-2");
    XClusterConfig.create(
        "test-2", defaultUniverse.getUniverseUUID(), xClusterUniv.getUniverseUUID());
    Universe xClusterUniv2 = ModelFactory.createUniverse("univ-3");
    XClusterConfig.create(
        "test-3", xClusterUniv.getUniverseUUID(), xClusterUniv2.getUniverseUUID());

    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = NEW_VERSION;
    taskParams.clusters.add(defaultUniverse.getUniverseDetails().getPrimaryCluster());
    mockDBServerVersion(
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion,
        taskParams.ybSoftwareVersion,
        defaultUniverse.getMasters().size() + defaultUniverse.getTServers().size());
    TaskInfo taskInfo = submitTask(taskParams, defaultUniverse.getVersion());
    verify(mockNodeManager, times(71)).nodeCommand(any(), any());
    verify(mockNodeUniverseManager, times(10)).runCommand(any(), any(), anyList(), any());

    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    assertEquals(
        3,
        subTasks.stream()
            .filter(task -> task.getTaskType().equals(TaskType.PromoteAutoFlags))
            .count());
    assertEquals(Success, taskInfo.getTaskState());
  }

  @Test
  public void testSoftwareUpgradeNoSystemCatalogUpgrade() {
    updateDefaultUniverseTo5Nodes(true, OLD_VERSION);

    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = NEW_VERSION;
    taskParams.clusters.add(defaultUniverse.getUniverseDetails().getPrimaryCluster());
    taskParams.upgradeSystemCatalog = false;
    mockDBServerVersion(
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion,
        taskParams.ybSoftwareVersion,
        defaultUniverse.getMasters().size() + defaultUniverse.getTServers().size());
    TaskInfo taskInfo = submitTask(taskParams, defaultUniverse.getVersion());
    verify(mockNodeUniverseManager, times(10)).runCommand(any(), any(), anyList(), any());

    assertEquals(100.0, taskInfo.getPercentCompleted(), 0);
    assertEquals(Success, taskInfo.getTaskState());

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
                .targetSoftwareVersion(NEW_VERSION)
                .build())
        .task(TaskType.AnsibleConfigureServers)
        .applyRound()
        .addSimultaneousTasks(TaskType.CheckSoftwareVersion, defaultUniverse.getTServers().size())
        .addTasks(TaskType.PromoteAutoFlags)
        .addTasks(TaskType.UpdateSoftwareVersion)
        .addTasks(TaskType.UpdateUniverseState)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  @Parameters({"false", "true"})
  public void testSoftwareUpgradeWithReadReplica(boolean enableYSQL) {
    updateDefaultUniverseTo5Nodes(enableYSQL, OLD_VERSION);

    // Adding Read Replica cluster.
    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.numNodes = 3;
    userIntent.replicationFactor = 3;
    userIntent.ybSoftwareVersion = OLD_VERSION;
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
    taskParams.ybSoftwareVersion = NEW_VERSION;
    taskParams.clusters.add(defaultUniverse.getUniverseDetails().getPrimaryCluster());
    mockDBServerVersion(
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion,
        taskParams.ybSoftwareVersion,
        defaultUniverse.getMasters().size() + defaultUniverse.getTServers().size());
    TaskInfo taskInfo = submitTask(taskParams, defaultUniverse.getVersion());
    verify(mockNodeUniverseManager, times(16)).runCommand(any(), any(), anyList(), any());

    assertEquals(100.0, taskInfo.getPercentCompleted(), 0);
    assertEquals(Success, taskInfo.getTaskState());

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
                .targetSoftwareVersion(NEW_VERSION)
                .build())
        .task(TaskType.AnsibleConfigureServers)
        .applyRound()
        .addSimultaneousTasks(TaskType.CheckSoftwareVersion, defaultUniverse.getTServers().size())
        .addTasks(TaskType.PromoteAutoFlags)
        .addTasks(TaskType.RunYsqlUpgrade)
        .addTasks(TaskType.UpdateSoftwareVersion)
        .addTasks(TaskType.UpdateUniverseState)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testSoftwareNonRollingUpgrade() {
    updateDefaultUniverseTo5Nodes(true, OLD_VERSION);

    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = NEW_VERSION;
    taskParams.upgradeOption = UpgradeOption.NON_ROLLING_UPGRADE;
    taskParams.clusters.add(defaultUniverse.getUniverseDetails().getPrimaryCluster());

    mockDBServerVersion(
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion,
        taskParams.ybSoftwareVersion,
        defaultUniverse.getMasters().size() + defaultUniverse.getTServers().size());

    TaskInfo taskInfo = submitTask(taskParams, defaultUniverse.getVersion());
    verify(mockNodeUniverseManager, times(10)).runCommand(any(), any(), anyList(), any());

    assertEquals(100.0, taskInfo.getPercentCompleted(), 0);
    assertEquals(Success, taskInfo.getTaskState());

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
                .targetSoftwareVersion(NEW_VERSION)
                .build())
        .task(TaskType.AnsibleConfigureServers)
        .applyRound()
        .addSimultaneousTasks(TaskType.CheckSoftwareVersion, defaultUniverse.getTServers().size())
        .addTasks(TaskType.PromoteAutoFlags)
        .addTasks(TaskType.RunYsqlUpgrade)
        .addTasks(TaskType.UpdateSoftwareVersion)
        .addTasks(TaskType.UpdateUniverseState)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testSoftwareUpgradeRetries() {
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(),
        univ -> {
          UniverseDefinitionTaskParams details = univ.getUniverseDetails();
          details.getPrimaryCluster().userIntent.ybSoftwareVersion = OLD_VERSION;
          univ.setUniverseDetails(details);
        });
    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = NEW_VERSION;
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
        TaskType.SoftwareUpgrade,
        taskParams,
        false);
  }

  @Test
  public void testPartialSoftwareUpgrade() {
    updateDefaultUniverseTo5Nodes(true, OLD_VERSION);

    Set<String> mastersOriginallyUpdated = new HashSet<>();
    Set<String> tserversOriginallyUpdated = new HashSet<>();

    List<NodeDetails> masters = defaultUniverse.getMasters();
    NodeDetails onlyMasterUpdated = masters.get(0);
    mastersOriginallyUpdated.add(onlyMasterUpdated.cloudInfo.private_ip);
    NodeDetails bothUpdated = masters.get(1);
    mastersOriginallyUpdated.add(bothUpdated.cloudInfo.private_ip);
    tserversOriginallyUpdated.add(bothUpdated.cloudInfo.private_ip);

    List<NodeDetails> otherTservers =
        defaultUniverse.getTServers().stream().filter(n -> !masters.contains(n)).toList();

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

    when(mockYBClient.getServerVersion(any(), anyString(), anyInt()))
        .thenAnswer(
            invocation -> {
              String ip = invocation.getArgument(1);
              int port = invocation.getArgument(2);
              boolean isMaster = port == 7100;
              Set<String> serversUpdated = isMaster ? mastersUpdated : tserversUpdated;
              Optional<String> result =
                  serversUpdated.add(ip) ? Optional.of(OLD_VERSION) : Optional.of(NEW_VERSION);
              return result;
            });

    defaultUniverse.refresh();
    SoftwareUpgradeParams taskParams = new SoftwareUpgradeParams();
    taskParams.ybSoftwareVersion = NEW_VERSION;
    taskParams.clusters.add(defaultUniverse.getUniverseDetails().getPrimaryCluster());

    TaskInfo taskInfo = submitTask(taskParams, defaultUniverse.getVersion());
    assertEquals(100.0, taskInfo.getPercentCompleted(), 0);
    assertEquals(Success, taskInfo.getTaskState());

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
                .targetSoftwareVersion(NEW_VERSION)
                .build())
        .task(TaskType.AnsibleConfigureServers)
        .applyToNodes(masterNames, tserverNames)
        .addSimultaneousTasks(TaskType.CheckSoftwareVersion, defaultUniverse.getTServers().size())
        .addTasks(TaskType.PromoteAutoFlags)
        .addTasks(TaskType.RunYsqlUpgrade)
        .addTasks(TaskType.UpdateSoftwareVersion)
        .addTasks(TaskType.UpdateUniverseState)
        .verifyTasks(taskInfo.getSubTasks());
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
