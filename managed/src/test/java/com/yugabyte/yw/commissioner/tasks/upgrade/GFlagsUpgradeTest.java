// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.upgrade;

import static com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType.MASTER;
import static com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType.TSERVER;
import static com.yugabyte.yw.models.TaskInfo.State.Failure;
import static com.yugabyte.yw.models.TaskInfo.State.Success;
import static org.hamcrest.CoreMatchers.containsString;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyMap;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doAnswer;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.google.common.collect.ArrayListMultimap;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.google.common.collect.Multimap;
import com.yugabyte.yw.commissioner.MockUpgrade;
import com.yugabyte.yw.commissioner.UpgradeTaskBase;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.commissioner.tasks.subtasks.AnsibleClusterServerCtl;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.NodeManager.NodeCommandType;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.TestHelper;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.gflags.GFlagsValidation;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.forms.GFlagsUpgradeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeOption;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.helpers.DeviceInfo;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.TaskType;
import java.io.IOException;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.stream.Collectors;
import lombok.extern.slf4j.Slf4j;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.InjectMocks;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
@Slf4j
public class GFlagsUpgradeTest extends UpgradeTaskTest {

  private int expectedUniverseVersion = 2;

  @InjectMocks GFlagsUpgrade gFlagsUpgrade;

  @Override
  @Before
  public void setUp() {
    super.setUp();
    gFlagsUpgrade.setUserTaskUUID(UUID.randomUUID());

    setUnderReplicatedTabletsMock();
    setFollowerLagMock();
    try {
      when(mockClient.setFlag(any(), anyString(), anyString(), anyBoolean())).thenReturn(true);
      setCheckNodesAreSafeToTakeDown(mockClient);
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }

  private UUID addReadReplica() {
    UserIntent curIntent = defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent;

    UniverseDefinitionTaskParams.UserIntent userIntent =
        new UniverseDefinitionTaskParams.UserIntent();
    userIntent.numNodes = 3;
    userIntent.ybSoftwareVersion = curIntent.ybSoftwareVersion;
    userIntent.accessKeyCode = curIntent.accessKeyCode;
    userIntent.regionList = ImmutableList.of(region.getUuid());
    userIntent.providerType = curIntent.providerType;
    userIntent.provider = curIntent.provider;
    userIntent.deviceInfo = new DeviceInfo();
    userIntent.deviceInfo.numVolumes = 2;

    PlacementInfo pi = new PlacementInfo();
    PlacementInfoUtil.addPlacementZone(az1.getUuid(), pi, 1, 1, false);
    PlacementInfoUtil.addPlacementZone(az2.getUuid(), pi, 1, 1, false);
    PlacementInfoUtil.addPlacementZone(az3.getUuid(), pi, 1, 1, true);

    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            ApiUtils.mockUniverseUpdaterWithReadReplica(userIntent, pi));
    expectedUniverseVersion++;

    return defaultUniverse.getUniverseDetails().getReadOnlyClusters().get(0).uuid;
  }

  private TaskInfo submitTask(GFlagsUpgradeParams requestParams) {
    return submitTask(requestParams, TaskType.GFlagsUpgrade, commissioner, expectedUniverseVersion);
  }

  private TaskInfo submitTask(GFlagsUpgradeParams requestParams, int expectedVersion) {
    return submitTask(requestParams, TaskType.GFlagsUpgrade, commissioner, expectedVersion);
  }

  private MockUpgrade initMockUpgrade() {
    MockUpgrade mockUpgrade = initMockUpgrade(GFlagsUpgrade.class);
    mockUpgrade.setUpgradeContext(UpgradeTaskBase.RUN_BEFORE_STOPPING);
    return mockUpgrade;
  }

  @Test
  public void testGFlagsNonRollingUpgrade() {
    GFlagsUpgradeParams taskParams = new GFlagsUpgradeParams();
    taskParams.masterGFlags = ImmutableMap.of("master-flag", "m1");
    taskParams.tserverGFlags = ImmutableMap.of("tserver-flag", "t1");
    taskParams.upgradeOption = UpgradeOption.NON_ROLLING_UPGRADE;

    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    initMockUpgrade()
        .precheckTasks(getPrecheckTasks(false))
        .upgradeRound(UpgradeOption.NON_ROLLING_UPGRADE)
        .masterTask(TaskType.AnsibleConfigureServers)
        .tserverTask(TaskType.AnsibleConfigureServers)
        .applyRound()
        .addTasks(TaskType.UpdateAndPersistGFlags)
        .verifyTasks(taskInfo.getSubTasks());

    verify(mockNodeManager, times(18)).nodeCommand(any(), any());
  }

  @Test
  public void testGFlagsNonRollingMasterOnlyUpgrade() {
    GFlagsUpgradeParams taskParams = new GFlagsUpgradeParams();
    taskParams.masterGFlags = ImmutableMap.of("master-flag", "m1");
    taskParams.upgradeOption = UpgradeOption.NON_ROLLING_UPGRADE;

    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    verify(mockNodeManager, times(9)).nodeCommand(any(), any());

    initMockUpgrade()
        .precheckTasks(getPrecheckTasks(false))
        .upgradeRound(UpgradeOption.NON_ROLLING_UPGRADE)
        .masterTask(TaskType.AnsibleConfigureServers)
        .applyToMasters()
        .addTasks(TaskType.UpdateAndPersistGFlags)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testGFlagsNonRollingTServerOnlyUpgrade() {
    GFlagsUpgradeParams taskParams = new GFlagsUpgradeParams();
    taskParams.tserverGFlags = ImmutableMap.of("tserver-flag", "t1");
    taskParams.upgradeOption = UpgradeOption.NON_ROLLING_UPGRADE;

    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    verify(mockNodeManager, times(9)).nodeCommand(any(), any());

    initMockUpgrade()
        .precheckTasks(getPrecheckTasks(false))
        .upgradeRound(UpgradeOption.NON_ROLLING_UPGRADE)
        .tserverTask(TaskType.AnsibleConfigureServers)
        .applyToTservers()
        .addTasks(TaskType.UpdateAndPersistGFlags)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testGFlagsUpgradeWithMasterGFlags() {
    GFlagsUpgradeParams taskParams = new GFlagsUpgradeParams();
    taskParams.masterGFlags = ImmutableMap.of("master-flag", "m1");
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    verify(mockNodeManager, times(9)).nodeCommand(any(), any());

    initMockUpgrade()
        .precheckTasks(getPrecheckTasks(true))
        .upgradeRound(UpgradeOption.ROLLING_UPGRADE)
        .masterTask(TaskType.AnsibleConfigureServers)
        .applyToMasters()
        .addTasks(TaskType.UpdateAndPersistGFlags)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testGFlagsUpgradeWithTServerGFlags() {
    GFlagsUpgradeParams taskParams = new GFlagsUpgradeParams();
    taskParams.tserverGFlags = ImmutableMap.of("tserver-flag", "t1");
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    ArgumentCaptor<NodeTaskParams> commandParams = ArgumentCaptor.forClass(NodeTaskParams.class);
    verify(mockNodeManager, times(9)).nodeCommand(any(), commandParams.capture());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        subTasks.stream().collect(Collectors.groupingBy(TaskInfo::getPosition));

    initMockUpgrade()
        .precheckTasks(getPrecheckTasks(true))
        .upgradeRound(UpgradeOption.ROLLING_UPGRADE)
        .tserverTask(TaskType.AnsibleConfigureServers)
        .applyToTservers()
        .addTasks(TaskType.UpdateAndPersistGFlags)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testGFlagsUpgrade() {
    GFlagsUpgradeParams taskParams = new GFlagsUpgradeParams();
    taskParams.masterGFlags = ImmutableMap.of("master-flag", "m1");
    taskParams.tserverGFlags = ImmutableMap.of("tserver-flag", "t1");
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    verify(mockNodeManager, times(18)).nodeCommand(any(), any());
    initMockUpgrade()
        .precheckTasks(getPrecheckTasks(true))
        .upgradeRound(UpgradeOption.ROLLING_UPGRADE)
        .task(TaskType.AnsibleConfigureServers)
        .applyRound()
        .addTasks(TaskType.UpdateAndPersistGFlags)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testGFlagsUpgradeWithEmptyFlags() {
    GFlagsUpgradeParams taskParams = new GFlagsUpgradeParams();
    assertThrows(RuntimeException.class, () -> submitTask(taskParams));
    verify(mockNodeManager, times(0)).nodeCommand(any(), any());
    assertThrows(RuntimeException.class, () -> submitTask(taskParams));
    defaultUniverse.refresh();
    assertEquals(2, defaultUniverse.getVersion());
  }

  @Test
  public void testGFlagsUpgradeWithSameMasterFlags() {
    // Simulate universe created with master flags and tserver flags.
    final Map<String, String> masterFlags = ImmutableMap.of("master-flag", "m123");
    Universe.UniverseUpdater updater =
        universe -> {
          UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
          UserIntent userIntent = universeDetails.getPrimaryCluster().userIntent;
          userIntent.masterGFlags = masterFlags;
          userIntent.tserverGFlags = ImmutableMap.of("tserver-flag", "t1");
          universe.setUniverseDetails(universeDetails);
        };
    Universe.saveDetails(defaultUniverse.getUniverseUUID(), updater);

    // Upgrade with same master flags but different tserver flags should not run master tasks.
    GFlagsUpgradeParams taskParams = new GFlagsUpgradeParams();
    taskParams.masterGFlags = masterFlags;
    taskParams.tserverGFlags = ImmutableMap.of("tserver-flag", "t2");

    TaskInfo taskInfo = submitTask(taskParams, 3);
    assertEquals(Success, taskInfo.getTaskState());
    verify(mockNodeManager, times(9)).nodeCommand(any(), any());

    initMockUpgrade()
        .precheckTasks(getPrecheckTasks(true))
        .upgradeRound(UpgradeOption.ROLLING_UPGRADE)
        .tserverTask(TaskType.AnsibleConfigureServers)
        .applyToTservers()
        .addTasks(TaskType.UpdateAndPersistGFlags)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testGFlagsUpgradeWithSameTserverFlags() {
    // Simulate universe created with master flags and tserver flags.
    final Map<String, String> tserverFlags = ImmutableMap.of("tserver-flag", "m123");
    Universe.UniverseUpdater updater =
        universe -> {
          UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
          UserIntent userIntent = universeDetails.getPrimaryCluster().userIntent;
          userIntent.masterGFlags = ImmutableMap.of("master-flag", "m1");
          userIntent.tserverGFlags = tserverFlags;
          universe.setUniverseDetails(universeDetails);
        };
    Universe.saveDetails(defaultUniverse.getUniverseUUID(), updater);

    // Upgrade with same master flags but different tserver flags should not run master tasks.
    GFlagsUpgradeParams taskParams = new GFlagsUpgradeParams();
    taskParams.masterGFlags = ImmutableMap.of("master-flag", "m2");
    taskParams.tserverGFlags = tserverFlags;

    TaskInfo taskInfo = submitTask(taskParams, 3);
    assertEquals(Success, taskInfo.getTaskState());
    verify(mockNodeManager, times(9)).nodeCommand(any(), any());
    initMockUpgrade()
        .precheckTasks(getPrecheckTasks(true))
        .upgradeRound(UpgradeOption.ROLLING_UPGRADE)
        .masterTask(TaskType.AnsibleConfigureServers)
        .applyToMasters()
        .addTasks(TaskType.UpdateAndPersistGFlags)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testRemoveFlags() {
    for (ServerType serverType : ImmutableList.of(MASTER, TSERVER)) {
      // Simulate universe created with master flags and tserver flags.
      final Map<String, String> tserverFlags = ImmutableMap.of("tserver-flag", "t1");
      final Map<String, String> masterGFlags = ImmutableMap.of("master-flag", "m1");
      Universe.UniverseUpdater updater =
          universe -> {
            UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
            UserIntent userIntent = universeDetails.getPrimaryCluster().userIntent;
            userIntent.masterGFlags = masterGFlags;
            userIntent.tserverGFlags = tserverFlags;
            universe.setUniverseDetails(universeDetails);
          };
      Universe.saveDetails(defaultUniverse.getUniverseUUID(), updater);

      GFlagsUpgradeParams taskParams = new GFlagsUpgradeParams();
      // This is a delete operation on the master flags.
      if (serverType == MASTER) {
        taskParams.masterGFlags = new HashMap<>();
        taskParams.tserverGFlags = tserverFlags;
      } else {
        taskParams.masterGFlags = masterGFlags;
        taskParams.tserverGFlags = new HashMap<>();
      }

      int expectedVersion = serverType == MASTER ? 3 : 5;
      TaskInfo taskInfo = submitTask(taskParams, expectedVersion);
      assertEquals(Success, taskInfo.getTaskState());

      int numInvocations = serverType == MASTER ? 9 : 18;
      verify(mockNodeManager, times(numInvocations)).nodeCommand(any(), any());

      MockUpgrade.UpgradeRound upgradeRound =
          initMockUpgrade()
              .precheckTasks(getPrecheckTasks(true))
              .upgradeRound(UpgradeOption.ROLLING_UPGRADE)
              .task(TaskType.AnsibleConfigureServers);

      MockUpgrade mockUpgrade;
      if (serverType == MASTER) {
        mockUpgrade = upgradeRound.applyToMasters();
      } else {
        mockUpgrade = upgradeRound.applyToTservers();
      }

      mockUpgrade.addTasks(TaskType.UpdateAndPersistGFlags).verifyTasks(taskInfo.getSubTasks());
    }
  }

  @Test
  public void testGflagChangingIntent() {
    Universe universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    // Verify enabled by default.
    assertEquals(universe.getUniverseDetails().getPrimaryCluster().userIntent.enableYSQL, true);
    GFlagsUpgradeParams taskParams = new GFlagsUpgradeParams();
    Map<String, String> gflags = ImmutableMap.of(GFlagsUtil.ENABLE_YSQL, "false");

    // this will cause both processes to be updated
    taskParams.masterGFlags = gflags;
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    verify(mockNodeManager, times(18)).nodeCommand(any(), any());
    initMockUpgrade()
        .precheckTasks(getPrecheckTasks(true))
        .upgradeRound(UpgradeOption.ROLLING_UPGRADE)
        .task(TaskType.AnsibleConfigureServers)
        .applyRound()
        .addTasks(TaskType.UpdateAndPersistGFlags)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testGflagsConflictingWithDefault() {
    GFlagsUpgradeParams taskParams = new GFlagsUpgradeParams();
    Map<String, String> gflags = ImmutableMap.of(GFlagsUtil.MASTER_ADDRESSES, "1.1.33.666:74350");
    // this will cause both processes to be updated
    taskParams.masterGFlags = gflags;
    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Failure, taskInfo.getTaskState());
    assertThat(
        taskInfo.getErrorMessage(),
        containsString(
            "Node host-n1: value 1.1.33.666:74350 for master_addresses is conflicting"
                + " with autogenerated value 10.0.0.1:7100,10.0.0.2:7100,10.0.0.3:7100"));

    // No conflicts here as tserver does not have such gflag.
    taskParams.masterGFlags = new HashMap<>();
    taskParams.tserverGFlags = gflags;
    taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
  }

  @Test
  public void testContradictoryGFlags() {
    GFlagsUpgradeParams taskParams = new GFlagsUpgradeParams();
    // this will cause both processes to be updated
    taskParams.masterGFlags = ImmutableMap.of(GFlagsUtil.ENABLE_YSQL, "false");
    taskParams.tserverGFlags = ImmutableMap.of(GFlagsUtil.ENABLE_YSQL, "true");
    Exception thrown = assertThrows(RuntimeException.class, () -> submitTask(taskParams));
    assertThat(
        thrown.getMessage(),
        containsString(
            "G-Flag value for 'enable_ysql' is inconsistent "
                + "between master and tserver ('false' vs 'true')"));
  }

  @Test
  public void testGFlagsUpgradeForRR() {
    UUID clusterId = addReadReplica();

    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            (u) -> {
              u.getUniverseDetails().getPrimaryCluster().userIntent.specificGFlags =
                  new SpecificGFlags();
            });
    expectedUniverseVersion++;

    GFlagsUpgradeParams taskParams = new GFlagsUpgradeParams();
    SpecificGFlags roGFlags =
        SpecificGFlags.construct(
            ImmutableMap.of("master-flag", "m1"), ImmutableMap.of("tserver-flag", "t1"));
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.getReadOnlyClusters().get(0).userIntent.specificGFlags = roGFlags;

    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();

    Set<String> nodeNames = new HashSet<>();
    defaultUniverse
        .getUniverseDetails()
        .getNodesInCluster(clusterId)
        .forEach(node -> nodeNames.add(node.getNodeName()));

    Set<String> changedNodes = new HashSet<>();
    subTasks.stream()
        .filter(t -> t.getTaskType() == TaskType.AnsibleConfigureServers)
        .map(t -> t.getTaskParams().get("nodeName").asText())
        .forEach(changedNodes::add);
    assertEquals(nodeNames, changedNodes);

    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertEquals(
        new SpecificGFlags(),
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.specificGFlags);
    assertEquals(
        roGFlags,
        defaultUniverse
            .getUniverseDetails()
            .getReadOnlyClusters()
            .get(0)
            .userIntent
            .specificGFlags);
    assertEquals(
        roGFlags.getGFlags(null, MASTER),
        defaultUniverse.getUniverseDetails().getReadOnlyClusters().get(0).userIntent.masterGFlags);
    assertEquals(
        roGFlags.getGFlags(null, TSERVER),
        defaultUniverse.getUniverseDetails().getReadOnlyClusters().get(0).userIntent.tserverGFlags);

    initMockUpgrade()
        .precheckTasks(getPrecheckTasks(true))
        .upgradeRound(UpgradeOption.ROLLING_UPGRADE)
        .task(TaskType.AnsibleConfigureServers)
        .applyToCluster(clusterId)
        .addTasks(TaskType.UpdateAndPersistGFlags)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testGFlagsUpgradeForRRStartInheriting() throws Exception {
    UUID clusterId = addReadReplica();

    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            universe -> {
              universe.getUniverseDetails().getPrimaryCluster().userIntent.specificGFlags =
                  SpecificGFlags.construct(
                      Collections.emptyMap(), ImmutableMap.of("tserver-flag", "t1"));

              UniverseDefinitionTaskParams.Cluster rr =
                  universe.getUniverseDetails().getReadOnlyClusters().get(0);
              rr.userIntent.specificGFlags =
                  SpecificGFlags.construct(
                      ImmutableMap.of("master-flag", "rm1"),
                      ImmutableMap.of("tserver-flag", "rt1", "tserver-flag2", "t2"));
            });
    expectedUniverseVersion++;

    GFlagsUpgradeParams taskParams = new GFlagsUpgradeParams();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    SpecificGFlags roGFlags = new SpecificGFlags();
    roGFlags.setInheritFromPrimary(true);
    taskParams.getReadOnlyClusters().get(0).userIntent.specificGFlags = roGFlags;

    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();

    try {
      Set<String> nodeNames = new HashSet<>();
      defaultUniverse
          .getUniverseDetails()
          .getNodesInCluster(clusterId)
          .forEach(node -> nodeNames.add(node.getNodeName()));

      Set<String> changedNodes = new HashSet<>();
      subTasks.stream()
          .filter(t -> t.getTaskType() == TaskType.AnsibleConfigureServers)
          .map(t -> t.getTaskParams().get("nodeName").asText())
          .forEach(changedNodes::add);
      assertEquals(nodeNames, changedNodes);

      assertEquals(
          new HashMap<>(Collections.singletonMap("tserver-flag", "t1")),
          getGflagsForNode(subTasks, changedNodes.iterator().next(), TSERVER));

      assertEquals(
          Collections.singleton("tserver-flag2"),
          getGflagsToRemoveForNode(subTasks, changedNodes.iterator().next(), TSERVER));

      defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
      assertEquals(
          roGFlags,
          defaultUniverse
              .getUniverseDetails()
              .getReadOnlyClusters()
              .get(0)
              .userIntent
              .specificGFlags);
      SpecificGFlags primarySpecificGFlags =
          defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.specificGFlags;
      assertEquals(
          primarySpecificGFlags.getGFlags(null, MASTER),
          defaultUniverse
              .getUniverseDetails()
              .getReadOnlyClusters()
              .get(0)
              .userIntent
              .masterGFlags);
      assertEquals(
          primarySpecificGFlags.getGFlags(null, TSERVER),
          defaultUniverse
              .getUniverseDetails()
              .getReadOnlyClusters()
              .get(0)
              .userIntent
              .tserverGFlags);

      initMockUpgrade()
          .precheckTasks(getPrecheckTasks(true))
          .upgradeRound(UpgradeOption.ROLLING_UPGRADE)
          .task(TaskType.AnsibleConfigureServers)
          .applyToCluster(clusterId)
          .addTasks(TaskType.UpdateAndPersistGFlags)
          .verifyTasks(taskInfo.getSubTasks());
    } catch (Exception e) {
      log.error("", e);
      throw e;
    }
  }

  @Test
  public void testGFlagsUpgradePrimaryWithRR() {
    addReadReplica();

    GFlagsUpgradeParams taskParams = new GFlagsUpgradeParams();
    SpecificGFlags specificGFlags =
        SpecificGFlags.construct(
            ImmutableMap.of("master-flag", "m1"), ImmutableMap.of("tserver-flag", "t1"));
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.getPrimaryCluster().userIntent.specificGFlags = specificGFlags;

    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();

    Set<String> changedNodes = new HashSet<>();
    subTasks.stream()
        .filter(t -> t.getTaskType() == TaskType.AnsibleConfigureServers)
        .map(t -> t.getTaskParams().get("nodeName").asText())
        .forEach(changedNodes::add);
    assertEquals(6, changedNodes.size()); // all nodes are affected

    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertNull(
        defaultUniverse
            .getUniverseDetails()
            .getReadOnlyClusters()
            .get(0)
            .userIntent
            .specificGFlags);
    assertEquals(
        specificGFlags,
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.specificGFlags);
    assertEquals(
        specificGFlags.getGFlags(null, MASTER),
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.masterGFlags);
    assertEquals(
        specificGFlags.getGFlags(null, TSERVER),
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.tserverGFlags);

    initMockUpgrade()
        .precheckTasks(getPrecheckTasks(true))
        .upgradeRound(UpgradeOption.ROLLING_UPGRADE)
        .task(TaskType.AnsibleConfigureServers)
        .applyToCluster(defaultUniverse.getUniverseDetails().getPrimaryCluster().uuid)
        .upgradeRound(UpgradeOption.ROLLING_UPGRADE)
        .task(TaskType.AnsibleConfigureServers)
        .applyToCluster(defaultUniverse.getUniverseDetails().getReadOnlyClusters().get(0).uuid)
        .addTasks(TaskType.UpdateAndPersistGFlags)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testContradictoryGFlagsNewVersion() {
    GFlagsUpgradeParams taskParams = new GFlagsUpgradeParams();
    SpecificGFlags specificGFlags =
        SpecificGFlags.construct(
            ImmutableMap.of(GFlagsUtil.ENABLE_YSQL, "true"),
            ImmutableMap.of(GFlagsUtil.ENABLE_YSQL, "false"));
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.getPrimaryCluster().userIntent.specificGFlags = specificGFlags;
    Exception thrown = assertThrows(RuntimeException.class, () -> submitTask(taskParams));
    assertThat(
        thrown.getMessage(),
        containsString(
            "G-Flag value for 'enable_ysql' is inconsistent between master and tserver ('true' vs"
                + " 'false')"));
  }

  @Test
  public void testSpecificGFlagsNoChanges() {
    defaultUniverse =
        Universe.saveDetails(
            defaultUniverse.getUniverseUUID(),
            universe -> {
              SpecificGFlags specificGFlags =
                  SpecificGFlags.construct(
                      ImmutableMap.of("master-flag", "m"), ImmutableMap.of("tserver-flag", "t1"));
              universe.getUniverseDetails().getPrimaryCluster().userIntent.specificGFlags =
                  specificGFlags;
            });
    expectedUniverseVersion++;

    GFlagsUpgradeParams taskParams = new GFlagsUpgradeParams();
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    Exception thrown = assertThrows(RuntimeException.class, () -> submitTask(taskParams));

    assertThat(
        thrown.getMessage(), containsString(GFlagsUpgradeParams.SPECIFIC_GFLAGS_NO_CHANGES_ERROR));
  }

  @Test
  public void testGflagsDeletedErrorNewVersion() {
    GFlagsUpgradeParams taskParams = new GFlagsUpgradeParams();
    taskParams.upgradeOption = UpgradeOption.NON_RESTART_UPGRADE;
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(),
        universe -> {
          SpecificGFlags specificGFlags =
              SpecificGFlags.construct(
                  ImmutableMap.of("master-flag", "m"), ImmutableMap.of("tserver-flag", "t1"));
          universe.getUniverseDetails().getPrimaryCluster().userIntent.specificGFlags =
              specificGFlags;
        });
    expectedUniverseVersion++;

    SpecificGFlags specificGFlags =
        SpecificGFlags.construct(ImmutableMap.of("master-flag", "m2"), ImmutableMap.of("a", "b"));
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.getPrimaryCluster().userIntent.specificGFlags = specificGFlags;
    Exception thrown = assertThrows(RuntimeException.class, () -> submitTask(taskParams));
    assertThat(
        thrown.getMessage(),
        containsString("Cannot delete gFlags through non-restart upgrade option."));
  }

  @Test
  public void testPerAZGflags() {
    List<UUID> azList = Arrays.asList(az1.getUuid(), az2.getUuid(), az3.getUuid());
    AtomicInteger idx = new AtomicInteger();
    Region.create(defaultProvider, "region-1", "PlacementRegion 1", "default-image");
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(),
        universe -> {
          universe
              .getUniverseDetails()
              .nodeDetailsSet
              .forEach(
                  node -> {
                    node.azUuid = azList.get(idx.getAndIncrement());
                  });
        });
    addReadReplica();
    expectedUniverseVersion++;

    GFlagsUpgradeParams taskParams = new GFlagsUpgradeParams();
    SpecificGFlags specificGFlags =
        SpecificGFlags.construct(
            ImmutableMap.of("master-flag", "m1"), ImmutableMap.of("tserver-flag", "t1"));
    Map<UUID, SpecificGFlags.PerProcessFlags> perAZ = new HashMap<>();
    specificGFlags.setPerAZ(perAZ);

    // First -> redefining existing
    UUID firstAZ = azList.get(0);
    SpecificGFlags.PerProcessFlags firstPerProcessFlags = new SpecificGFlags.PerProcessFlags();
    firstPerProcessFlags.value =
        ImmutableMap.of(
            MASTER, ImmutableMap.of("master-flag", "m2"),
            TSERVER, ImmutableMap.of("tserver-flag", "t2"));
    perAZ.put(firstAZ, firstPerProcessFlags);

    // Second -> adding new
    UUID secondAZ = azList.get(1);
    SpecificGFlags.PerProcessFlags secondPerProcessFlags = new SpecificGFlags.PerProcessFlags();
    secondPerProcessFlags.value =
        ImmutableMap.of(
            MASTER, ImmutableMap.of("master-flag2", "m2"),
            TSERVER, ImmutableMap.of("tserver-flag2", "t2"));
    perAZ.put(secondAZ, secondPerProcessFlags);

    // Third -> no changes
    UUID thirdAZ = azList.get(2);
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.getPrimaryCluster().userIntent.specificGFlags = specificGFlags;
    taskParams.getReadOnlyClusters().get(0).userIntent.specificGFlags =
        SpecificGFlags.constructInherited();

    TaskInfo taskInfo = submitTask(taskParams);
    List<TaskInfo> subTasks = taskInfo.getSubTasks();
    assertEquals(Success, taskInfo.getTaskState());

    Multimap<UUID, String> nodeNamesByUUID = ArrayListMultimap.create();
    for (NodeDetails nodeDetails : defaultUniverse.getUniverseDetails().nodeDetailsSet) {
      nodeNamesByUUID.put(nodeDetails.getAzUuid(), nodeDetails.nodeName);
    }
    assertEquals(6, nodeNamesByUUID.size());
    assertEquals(3, nodeNamesByUUID.keySet().size());

    for (String first : nodeNamesByUUID.get(firstAZ)) {
      assertEquals(
          new HashMap<>(Collections.singletonMap("tserver-flag", "t2")),
          getGflagsForNode(subTasks, first, TSERVER));
      if (defaultUniverse.getNode(first).isMaster) {
        assertEquals(
            new HashMap<>(Collections.singletonMap("master-flag", "m2")),
            getGflagsForNode(subTasks, first, MASTER));
      }
    }
    for (String second : nodeNamesByUUID.get(secondAZ)) {
      assertEquals(
          new HashMap<>(ImmutableMap.of("tserver-flag", "t1", "tserver-flag2", "t2")),
          getGflagsForNode(subTasks, second, TSERVER));
      if (defaultUniverse.getNode(second).isMaster) {
        assertEquals(
            new HashMap<>(ImmutableMap.of("master-flag", "m1", "master-flag2", "m2")),
            getGflagsForNode(subTasks, second, MASTER));
      }
    }
    for (String third : nodeNamesByUUID.get(thirdAZ)) {
      assertEquals(
          new HashMap<>(Collections.singletonMap("tserver-flag", "t1")),
          getGflagsForNode(subTasks, third, TSERVER));
      if (defaultUniverse.getNode(third).isMaster) {
        assertEquals(
            new HashMap<>(Collections.singletonMap("master-flag", "m1")),
            getGflagsForNode(subTasks, third, MASTER));
      }
    }
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    SpecificGFlags readonlyGflags =
        defaultUniverse.getUniverseDetails().getReadOnlyClusters().get(0).userIntent.specificGFlags;
    assertEquals(specificGFlags.getPerProcessFlags(), readonlyGflags.getPerProcessFlags());
    assertEquals(specificGFlags.getPerAZ(), readonlyGflags.getPerAZ());
  }

  @Test
  public void testChangeSingleAZGflags() {
    List<UUID> azList = Arrays.asList(az1.getUuid(), az2.getUuid(), az3.getUuid());
    AtomicInteger idx = new AtomicInteger();
    Region.create(defaultProvider, "region-1", "PlacementRegion 1", "default-image");
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(),
        universe -> {
          universe
              .getUniverseDetails()
              .nodeDetailsSet
              .forEach(
                  node -> {
                    node.azUuid = azList.get(idx.getAndIncrement());
                  });
        });
    addReadReplica();
    expectedUniverseVersion++;

    GFlagsUpgradeParams taskParams = new GFlagsUpgradeParams();
    SpecificGFlags specificGFlags =
        SpecificGFlags.construct(Collections.emptyMap(), Collections.emptyMap());
    Map<UUID, SpecificGFlags.PerProcessFlags> perAZ = new HashMap<>();
    specificGFlags.setPerAZ(perAZ);

    UUID firstAZ = azList.get(0);
    SpecificGFlags.PerProcessFlags firstPerProcessFlags = new SpecificGFlags.PerProcessFlags();
    firstPerProcessFlags.value =
        ImmutableMap.of(
            MASTER, ImmutableMap.of("master-flag", "m2"),
            TSERVER, ImmutableMap.of("tserver-flag", "t2"));
    perAZ.put(firstAZ, firstPerProcessFlags);

    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.getPrimaryCluster().userIntent.specificGFlags = specificGFlags;

    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());

    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    UUID primaryUUID = defaultUniverse.getUniverseDetails().getPrimaryCluster().uuid;
    UUID readonlyUUID = defaultUniverse.getUniverseDetails().getReadOnlyClusters().get(0).uuid;
    Set<String> az1PrimaryNodeNames = new HashSet<>();
    Set<String> az1RRNodeNames = new HashSet<>();

    for (NodeDetails nodeDetails : defaultUniverse.getUniverseDetails().nodeDetailsSet) {
      if (nodeDetails.azUuid.equals(firstAZ)) {
        if (nodeDetails.isInPlacement(primaryUUID)) {
          az1PrimaryNodeNames.add(nodeDetails.nodeName);
        } else {
          az1RRNodeNames.add(nodeDetails.nodeName);
        }
      }
    }

    initMockUpgrade()
        .precheckTasks(getPrecheckTasks(true))
        .upgradeRound(UpgradeOption.ROLLING_UPGRADE)
        .task(TaskType.AnsibleConfigureServers)
        .applyToNodes(az1PrimaryNodeNames, az1PrimaryNodeNames)
        .upgradeRound(UpgradeOption.ROLLING_UPGRADE)
        .task(TaskType.AnsibleConfigureServers)
        .applyToNodes(Collections.emptySet(), az1RRNodeNames)
        .addTasks(TaskType.UpdateAndPersistGFlags)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testSpecificGflagChangingIntentYBM() {
    Universe universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    // Verify enabled by default.
    assertEquals(universe.getUniverseDetails().getPrimaryCluster().userIntent.enableYSQL, true);
    // But having 'false' in gflags.
    Universe.saveDetails(
        universe.getUniverseUUID(),
        univ -> {
          univ.getUniverseDetails().getPrimaryCluster().userIntent.specificGFlags =
              SpecificGFlags.construct(
                  ImmutableMap.of(GFlagsUtil.ENABLE_YSQL, "false"), Collections.emptyMap());
        });
    expectedUniverseVersion++;

    GFlagsUpgradeParams taskParams = new GFlagsUpgradeParams();
    SpecificGFlags specificGFlags =
        SpecificGFlags.construct(
            ImmutableMap.of(GFlagsUtil.ENABLE_YSQL, "false", "master_gflag", "m1"),
            Collections.emptyMap());
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.getPrimaryCluster().userIntent.specificGFlags = specificGFlags;

    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    // Only masters
    verify(mockNodeManager, times(9)).nodeCommand(any(), any());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    assertEquals(universe.getUniverseDetails().getPrimaryCluster().userIntent.enableYSQL, false);

    initMockUpgrade()
        .precheckTasks(getPrecheckTasks(true))
        .upgradeRound(UpgradeOption.ROLLING_UPGRADE)
        .masterTask(TaskType.AnsibleConfigureServers)
        .applyToMasters()
        .addTasks(TaskType.UpdateAndPersistGFlags)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testSpecificGflagChangingIntent() {
    Universe universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    // Verify enabled by default.
    assertEquals(universe.getUniverseDetails().getPrimaryCluster().userIntent.enableYSQL, true);
    GFlagsUpgradeParams taskParams = new GFlagsUpgradeParams();
    SpecificGFlags specificGFlags =
        SpecificGFlags.construct(
            ImmutableMap.of(GFlagsUtil.ENABLE_YSQL, "false"), Collections.emptyMap());
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.getPrimaryCluster().userIntent.specificGFlags = specificGFlags;

    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    verify(mockNodeManager, times(18)).nodeCommand(any(), any());

    initMockUpgrade()
        .precheckTasks(getPrecheckTasks(true))
        .upgradeRound(UpgradeOption.ROLLING_UPGRADE)
        .task(TaskType.AnsibleConfigureServers)
        .applyRound()
        .addTasks(TaskType.UpdateAndPersistGFlags)
        .verifyTasks(taskInfo.getSubTasks());

    universe = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    // Verify changed to false.
    assertEquals(universe.getUniverseDetails().getPrimaryCluster().userIntent.enableYSQL, false);
    assertEquals(
        universe.getUniverseDetails().getPrimaryCluster().userIntent.specificGFlags,
        specificGFlags);
  }

  @Test
  public void testUpgradeAutoFlags() {
    GFlagsUpgradeParams taskParams = new GFlagsUpgradeParams();
    Universe.saveDetails(
        defaultUniverse.getUniverseUUID(),
        universe -> {
          SpecificGFlags specificGFlags =
              SpecificGFlags.construct(ImmutableMap.of("master-flag", "m1"), ImmutableMap.of());
          universe.getUniverseDetails().getPrimaryCluster().userIntent.specificGFlags =
              specificGFlags;
          universe.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion =
              "2.18.2.0-b65";
        });
    expectedUniverseVersion++;

    SpecificGFlags specificGFlags =
        SpecificGFlags.construct(ImmutableMap.of("master-flag", "m2"), ImmutableMap.of());
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.getPrimaryCluster().userIntent.specificGFlags = specificGFlags;

    Universe xClusterUniverse = ModelFactory.createUniverse("univ-2");
    XClusterConfig xClusterConfig =
        XClusterConfig.create(
            "test-1", defaultUniverse.getUniverseUUID(), xClusterUniverse.getUniverseUUID());
    xClusterConfig.updateStatus(XClusterConfig.XClusterConfigStatusType.Running);
    TestHelper.updateUniverseVersion(xClusterUniverse, "2.14.0.0-b1");

    try {
      when(mockGFlagsValidation.getFilteredAutoFlagsWithNonInitialValue(
              anyMap(), anyString(), any()))
          .thenReturn(ImmutableMap.of("master-flag", "m2"));
      GFlagsValidation.AutoFlagsPerServer autoFlagsPerServer =
          new GFlagsValidation.AutoFlagsPerServer();
      GFlagsValidation.AutoFlagDetails flag = new GFlagsValidation.AutoFlagDetails();
      flag.name = "master-flag-2";
      autoFlagsPerServer.autoFlagDetails = Collections.singletonList(flag);
      GFlagsValidation.AutoFlagDetails flag2 = new GFlagsValidation.AutoFlagDetails();
      flag2.name = "master-flag";
      GFlagsValidation.AutoFlagsPerServer autoFlagsPerServer2 =
          new GFlagsValidation.AutoFlagsPerServer();
      autoFlagsPerServer2.autoFlagDetails = Collections.singletonList(flag2);
      when(mockGFlagsValidation.extractAutoFlags(anyString(), (ServerType) any()))
          .thenReturn(autoFlagsPerServer)
          .thenReturn(autoFlagsPerServer2);
    } catch (IOException e) {
      fail();
    }

    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Failure, taskInfo.getTaskState());
    assertThat(
        taskInfo.getErrorMessage(),
        containsString(
            "Cannot upgrade auto flags as an XCluster linked universe "
                + xClusterUniverse.getUniverseUUID()
                + " does not support auto flags"));

    TestHelper.updateUniverseVersion(xClusterUniverse, "2.17.0.0-b1");
    taskInfo = submitTask(taskParams);
    assertEquals(Failure, taskInfo.getTaskState());
    assertThat(
        taskInfo.getErrorMessage(),
        containsString(
            "master-flag is not present in the xCluster linked universe: "
                + xClusterUniverse.getUniverseUUID()));

    taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
  }

  @Test
  public void testGFlagsUpgradeNonRestart() {
    addReadReplica();

    GFlagsUpgradeParams taskParams = new GFlagsUpgradeParams();
    SpecificGFlags specificGFlags =
        SpecificGFlags.construct(
            ImmutableMap.of("master-flag", "m1111"), ImmutableMap.of("tserver-flag", "t1111"));
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.getPrimaryCluster().userIntent.specificGFlags = specificGFlags;
    taskParams.upgradeOption = UpgradeOption.NON_RESTART_UPGRADE;

    TaskInfo taskInfo = submitTask(taskParams);
    assertEquals(Success, taskInfo.getTaskState());
    List<TaskInfo> subTasks = taskInfo.getSubTasks();

    subTasks.stream()
        .filter(t -> t.getTaskType() == TaskType.SetFlagInMemory)
        .forEach(
            task -> {
              ServerType process =
                  ServerType.valueOf(task.getTaskParams().get("serverType").asText());
              Map<String, String> gflags =
                  Json.fromJson(task.getTaskParams().get("gflags"), Map.class);
              if (process == MASTER) {
                assertEquals(specificGFlags.getGFlags(null, MASTER), gflags);
              } else {
                assertEquals(specificGFlags.getGFlags(null, TSERVER), gflags);
              }
            });
    defaultUniverse = Universe.getOrBadRequest(defaultUniverse.getUniverseUUID());
    assertEquals(
        specificGFlags,
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.specificGFlags);
    assertEquals(
        specificGFlags.getGFlags(null, MASTER),
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.masterGFlags);
    assertEquals(
        specificGFlags.getGFlags(null, TSERVER),
        defaultUniverse.getUniverseDetails().getPrimaryCluster().userIntent.tserverGFlags);
    initMockUpgrade()
        .precheckTasks(getPrecheckTasks(false))
        .upgradeRound(UpgradeOption.NON_RESTART_UPGRADE, true)
        .task(TaskType.AnsibleConfigureServers)
        .task(TaskType.SetFlagInMemory)
        .applyToCluster(defaultUniverse.getUniverseDetails().getPrimaryCluster().uuid)
        .upgradeRound(UpgradeOption.NON_RESTART_UPGRADE, true)
        .tserverTask(TaskType.AnsibleConfigureServers)
        .tserverTask(TaskType.SetFlagInMemory)
        .applyToCluster(defaultUniverse.getUniverseDetails().getReadOnlyClusters().get(0).uuid)
        .addTasks(TaskType.UpdateAndPersistGFlags)
        .verifyTasks(taskInfo.getSubTasks());
  }

  @Test
  public void testGFlagsUpgradePrimaryWithRRRetries() {
    addReadReplica();

    GFlagsUpgradeParams taskParams = new GFlagsUpgradeParams();
    SpecificGFlags specificGFlags =
        SpecificGFlags.construct(
            ImmutableMap.of("master-flag", "m1"), ImmutableMap.of("tserver-flag", "t1"));
    taskParams.clusters = defaultUniverse.getUniverseDetails().clusters;
    taskParams.getPrimaryCluster().userIntent.specificGFlags = specificGFlags;
    taskParams.expectedUniverseVersion = -1;
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.sleepAfterMasterRestartMillis = 0;
    taskParams.sleepAfterTServerRestartMillis = 0;
    super.verifyTaskRetries(
        defaultCustomer,
        CustomerTask.TaskType.GFlagsUpgrade,
        CustomerTask.TargetType.Universe,
        defaultUniverse.getUniverseUUID(),
        TaskType.GFlagsUpgrade,
        taskParams,
        false);
  }

  @Test
  public void testGFlagsUpgradeRerun() {
    ShellResponse dummyShellResponse = new ShellResponse();
    // Make the task fail the first time.
    doAnswer(
            inv -> {
              AnsibleClusterServerCtl.Params params =
                  (AnsibleClusterServerCtl.Params) inv.getArgument(1);
              if (params.process.equalsIgnoreCase("tserver")
                  && params.command.equalsIgnoreCase("start")) {
                throw new RuntimeException("Startup failed");
              }
              return dummyShellResponse;
            })
        .when(mockNodeManager)
        .nodeCommand(eq(NodeCommandType.Control), any());
    GFlagsUpgradeParams taskParams = new GFlagsUpgradeParams();
    taskParams.masterGFlags = ImmutableMap.of("master-flag", "m1");
    taskParams.tserverGFlags = ImmutableMap.of("tserver-flag", "t1");
    TaskInfo taskInfo = submitTask(taskParams, -1);
    assertEquals(Failure, taskInfo.getTaskState());
    // Do not fail the task due to node command.
    when(mockNodeManager.nodeCommand(eq(NodeCommandType.Control), any()))
        .thenReturn(dummyShellResponse);
    taskParams.masterGFlags = null;
    taskParams.tserverGFlags = ImmutableMap.of("tserver-flag", "t2");
    // Validation must fail.
    PlatformServiceException exception =
        assertThrows(PlatformServiceException.class, () -> submitTask(taskParams, -1));
    assertEquals(
        "Gflags upgrade rerun must affect all server types and nodes changed by the previously"
            + " failed gflags operation",
        exception.getMessage());
    taskParams.masterGFlags = ImmutableMap.of("master-flag", "m2");
    taskParams.tserverGFlags = ImmutableMap.of("tserver-flag", "t2");
    taskInfo = submitTask(taskParams, -1);
    assertEquals(Success, taskInfo.getTaskState());
  }

  private Map<String, String> getGflagsForNode(
      List<TaskInfo> tasks, String nodeName, ServerType process) {
    TaskInfo taskInfo = findGflagsTask(tasks, nodeName, process);
    JsonNode gflags = taskInfo.getTaskParams().get("gflags");
    return Json.fromJson(gflags, Map.class);
  }

  private Set<String> getGflagsToRemoveForNode(
      List<TaskInfo> tasks, String nodeName, ServerType process) {
    TaskInfo taskInfo = findGflagsTask(tasks, nodeName, process);
    ArrayNode gflagsToRemove = (ArrayNode) taskInfo.getTaskParams().get("gflagsToRemove");
    return Json.fromJson(gflagsToRemove, Set.class);
  }

  private TaskInfo findGflagsTask(List<TaskInfo> tasks, String nodeName, ServerType process) {
    return tasks.stream()
        .filter(t -> t.getTaskType() == TaskType.AnsibleConfigureServers)
        .filter(t -> t.getTaskParams().get("nodeName").asText().equals(nodeName))
        .filter(
            t ->
                t.getTaskParams()
                    .get("properties")
                    .get("processType")
                    .asText()
                    .equals(process.toString()))
        .findFirst()
        .get();
  }
}
