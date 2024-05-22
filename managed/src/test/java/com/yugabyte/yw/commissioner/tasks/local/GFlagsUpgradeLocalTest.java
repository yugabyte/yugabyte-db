// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.local;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.containsString;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;

import com.yugabyte.yw.commissioner.tasks.CommissionerBaseTest;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.LocalNodeManager;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.controllers.UniverseControllerRequestBinder;
import com.yugabyte.yw.forms.GFlagsUpgradeParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UpgradeTaskParams;
import com.yugabyte.yw.models.RuntimeConfigEntry;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.TaskType;
import java.io.BufferedReader;
import java.io.FileReader;
import java.io.IOException;
import java.util.Collections;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.junit.Test;

@Slf4j
public class GFlagsUpgradeLocalTest extends LocalProviderUniverseTestBase {

  @Override
  protected Pair<Integer, Integer> getIpRange() {
    return new Pair(30, 60);
  }

  @Test
  public void testNonRestartAndNonRollingUpgrade() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.specificGFlags = new SpecificGFlags();
    Universe universe = createUniverse(userIntent);
    initYSQL(universe);
    initAndStartPayload(universe);
    SpecificGFlags specificGFlags =
        SpecificGFlags.construct(
            Collections.singletonMap("max_log_size", "1805"),
            Collections.singletonMap("log_max_seconds_to_retain", "86333"));
    universe.getUniverseDetails().getPrimaryCluster().userIntent.specificGFlags = specificGFlags;
    TaskInfo taskInfo =
        doGflagsUpgrade(
            universe, UpgradeTaskParams.UpgradeOption.NON_RESTART_UPGRADE, specificGFlags, null);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    compareGFlags(universe);
    verifyYSQL(universe);
    verifyPayload();
    specificGFlags =
        SpecificGFlags.construct(
            Collections.singletonMap("max_log_size", "1806"),
            Collections.singletonMap("log_max_seconds_to_retain", "86313"));
    universe.getUniverseDetails().getPrimaryCluster().userIntent.specificGFlags = specificGFlags;
    taskInfo =
        doGflagsUpgrade(
            universe, UpgradeTaskParams.UpgradeOption.NON_ROLLING_UPGRADE, specificGFlags, null);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    compareGFlags(universe);
    verifyYSQL(universe);
  }

  @Test
  public void testRollingUpgradeWithRRInherited() throws InterruptedException {
    Universe universe = createUniverse(getDefaultUserIntent());
    doAddReadReplica(universe, getDefaultUserIntent());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    initYSQL(universe);
    initAndStartPayload(universe);
    SpecificGFlags specificGFlags =
        SpecificGFlags.construct(
            Collections.singletonMap("max_log_size", "1805"),
            Collections.singletonMap("log_max_seconds_to_retain", "86333"));
    SpecificGFlags.PerProcessFlags perProcessFlags = new SpecificGFlags.PerProcessFlags();
    perProcessFlags.value =
        Collections.singletonMap(
            UniverseTaskBase.ServerType.MASTER, Collections.singletonMap("max_log_size", "2205"));
    specificGFlags.setPerAZ(Collections.singletonMap(az2.getUuid(), perProcessFlags));

    TaskInfo taskInfo =
        doGflagsUpgrade(
            universe,
            UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE,
            specificGFlags,
            SpecificGFlags.constructInherited());
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    compareGFlags(universe);
    verifyYSQL(universe);
    verifyPayload();
  }

  @Test
  public void testRollingUpgradeWithRR() throws InterruptedException {
    Universe universe = createUniverse(getDefaultUserIntent());
    addReadReplica(universe, getDefaultUserIntent());
    initYSQL(universe);
    initAndStartPayload(universe);
    SpecificGFlags specificGFlags =
        SpecificGFlags.construct(
            Collections.singletonMap("max_log_size", "1805"),
            Collections.singletonMap("log_max_seconds_to_retain", "86333"));
    SpecificGFlags.PerProcessFlags perProcessFlags = new SpecificGFlags.PerProcessFlags();
    perProcessFlags.value =
        Collections.singletonMap(
            UniverseTaskBase.ServerType.MASTER, Collections.singletonMap("max_log_size", "2205"));
    specificGFlags.setPerAZ(Collections.singletonMap(az2.getUuid(), perProcessFlags));

    TaskInfo taskInfo =
        doGflagsUpgrade(
            universe,
            UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE,
            specificGFlags,
            SpecificGFlags.constructInherited());
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    compareGFlags(universe);
    verifyYSQL(universe);
    verifyPayload();
  }

  // @Test
  // Current db version doesn't support that api, so will uncomment later
  public void testNodesAreSafeToTakeDownFails() throws InterruptedException, IOException {
    // So that we will do only one check.
    RuntimeConfigEntry.upsertGlobal(
        UniverseConfKeys.nodesAreSafeToTakeDownCheckTimeout.getKey(), "5s");
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.specificGFlags = getGFlags("TEST_set_tablet_follower_lag_ms", "20000");
    Universe universe = createUniverse(userIntent);
    initYSQL(universe);
    SpecificGFlags specificGFlags =
        SpecificGFlags.construct(
            Collections.singletonMap("max_log_size", "1805"),
            Collections.singletonMap("log_max_seconds_to_retain", "86333"));
    universe.getUniverseDetails().getPrimaryCluster().userIntent.specificGFlags = specificGFlags;
    NodeDetails node = universe.getNodes().iterator().next();
    LocalNodeManager.NodeInfo nodeInfo = localNodeManager.getNodeInfo(node);
    localNodeManager.killProcess(node.getNodeName(), UniverseTaskBase.ServerType.MASTER);
    // Check that it fails (one master is absent)
    doGflagsUpgrade(
        universe,
        UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE,
        specificGFlags,
        null,
        TaskInfo.State.Failure,
        "Nodes are not safe to take down");
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    // Verify that it failed before locking
    assertNull(universe.getUniverseDetails().updatingTaskUUID);
    assertNull(universe.getUniverseDetails().placementModificationTaskUuid);
    localNodeManager.startProcessForNode(userIntent, UniverseTaskBase.ServerType.MASTER, nodeInfo);

    // Now kill tserver
    localNodeManager.killProcess(node.getNodeName(), UniverseTaskBase.ServerType.TSERVER);
    // Check that it fails
    doGflagsUpgrade(
        universe,
        UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE,
        specificGFlags,
        null,
        TaskInfo.State.Failure,
        "Nodes are not safe to take down");
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    // Verify that it failed before locking
    assertNull(universe.getUniverseDetails().updatingTaskUUID);
    assertNull(universe.getUniverseDetails().placementModificationTaskUuid);
    localNodeManager.startProcessForNode(userIntent, UniverseTaskBase.ServerType.TSERVER, nodeInfo);

    // Too small max follower lag - might fail
    RuntimeConfigEntry.upsertGlobal(UniverseConfKeys.followerLagMaxThreshold.getKey(), "10s");
    doGflagsUpgrade(
        universe,
        UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE,
        specificGFlags,
        null,
        TaskInfo.State.Failure,
        "Nodes are not safe to take down");
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    // Verify that it failed before locking
    assertNull(universe.getUniverseDetails().updatingTaskUUID);
    assertNull(universe.getUniverseDetails().placementModificationTaskUuid);

    // Revert setting
    RuntimeConfigEntry.upsertGlobal(UniverseConfKeys.followerLagMaxThreshold.getKey(), "30s");

    // Now it should be successful
    doGflagsUpgrade(
        universe, UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, specificGFlags, null);
    compareGFlags(universe);

    verifyYSQL(universe);
  }

  // @Test
  // Current db version doesn't support that api, so will uncomment later
  public void testUpgradeFailsDuringExecution() throws InterruptedException, IOException {
    // So that we will do only one check.
    RuntimeConfigEntry.upsertGlobal(
        UniverseConfKeys.nodesAreSafeToTakeDownCheckTimeout.getKey(), "5s");
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.specificGFlags = SpecificGFlags.construct(new HashMap<>(), new HashMap<>());
    Universe universe = createUniverse(userIntent);
    initYSQL(universe);
    // Upgrading only tservers
    SpecificGFlags specificGFlags =
        SpecificGFlags.construct(
            new HashMap<>(), Collections.singletonMap("log_max_seconds_to_retain", "86333"));
    universe.getUniverseDetails().getPrimaryCluster().userIntent.specificGFlags = specificGFlags;

    CommissionerBaseTest.setPausePosition(11);
    GFlagsUpgradeParams gFlagsUpgradeParams =
        getUpgradeParams(
            universe, UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, GFlagsUpgradeParams.class);
    gFlagsUpgradeParams.getPrimaryCluster().userIntent.specificGFlags = specificGFlags;
    UUID taskID =
        upgradeUniverseHandler.upgradeGFlags(
            gFlagsUpgradeParams, customer, Universe.getOrBadRequest(universe.getUniverseUUID()));
    CommissionerBaseTest.waitForTaskPaused(taskID, commissioner);
    TaskInfo taskInfo = TaskInfo.getOrBadRequest(taskID);
    String processedNodeName =
        taskInfo.getSubTasks().stream()
            .filter(t -> t.getTaskState() == TaskInfo.State.Success)
            .filter(t -> t.getTaskType() == TaskType.SetNodeState)
            .map(t -> t.getTaskParams().get("nodeName").asText())
            .findFirst()
            .get();

    NodeDetails anotherNode =
        universe.getUniverseDetails().nodeDetailsSet.stream()
            .filter(n -> !n.getNodeName().equals(processedNodeName))
            .findFirst()
            .get();

    localNodeManager.killProcess(anotherNode.getNodeName(), UniverseTaskBase.ServerType.TSERVER);
    CommissionerBaseTest.clearAbortOrPausePositions();

    commissioner.resumeTask(taskID);
    taskInfo = waitForTask(taskID);
    assertEquals(TaskInfo.State.Failure, taskInfo.getTaskState());
    assertThat(getAllErrorsStr(taskInfo), containsString("Nodes are not safe to take down"));
  }

  protected TaskInfo doGflagsUpgrade(
      Universe universe,
      UpgradeTaskParams.UpgradeOption upgradeOption,
      SpecificGFlags specificGFlags,
      SpecificGFlags rrGflags)
      throws InterruptedException {
    return doGflagsUpgrade(
        universe, upgradeOption, specificGFlags, rrGflags, TaskInfo.State.Success, null);
  }

  protected TaskInfo doGflagsUpgrade(
      Universe universe,
      UpgradeTaskParams.UpgradeOption upgradeOption,
      SpecificGFlags specificGFlags,
      SpecificGFlags rrGflags,
      TaskInfo.State expectedState,
      String expectedFailMessage)
      throws InterruptedException {
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    GFlagsUpgradeParams gFlagsUpgradeParams =
        getUpgradeParams(universe, upgradeOption, GFlagsUpgradeParams.class);
    gFlagsUpgradeParams.getPrimaryCluster().userIntent.specificGFlags = specificGFlags;
    if (rrGflags != null) {
      gFlagsUpgradeParams.getReadOnlyClusters().get(0).userIntent.specificGFlags = rrGflags;
    }
    TaskInfo taskInfo =
        waitForTask(
            upgradeUniverseHandler.upgradeGFlags(
                gFlagsUpgradeParams,
                customer,
                Universe.getOrBadRequest(universe.getUniverseUUID())),
            universe);
    assertEquals(expectedState, taskInfo.getTaskState());
    if (expectedFailMessage != null) {
      assertThat(getAllErrorsStr(taskInfo), containsString(expectedFailMessage));
    }
    return taskInfo;
  }

  private <T extends UpgradeTaskParams> T getUpgradeParams(
      Universe universe, UpgradeTaskParams.UpgradeOption upgradeOption, Class<T> clazz) {
    T upgadeParams = UniverseControllerRequestBinder.deepCopy(universe.getUniverseDetails(), clazz);
    upgadeParams.setUniverseUUID(universe.getUniverseUUID());
    upgadeParams.upgradeOption = upgradeOption;
    upgadeParams.expectedUniverseVersion = universe.getVersion();
    upgadeParams.clusters = universe.getUniverseDetails().clusters;
    return upgadeParams;
  }

  private void compareGFlags(Universe universe) {
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    for (NodeDetails node : universe.getNodes()) {
      UniverseDefinitionTaskParams.Cluster cluster = universe.getCluster(node.placementUuid);
      for (UniverseTaskBase.ServerType serverType : node.getAllProcesses()) {
        Map<String, String> varz = getVarz(node, universe, serverType);
        Map<String, String> gflags =
            GFlagsUtil.getGFlagsForNode(
                node, serverType, cluster, universe.getUniverseDetails().clusters);
        Map<String, String> gflagsOnDisk = getDiskFlags(node, universe, serverType);
        gflags.forEach(
            (k, v) -> {
              String actual = varz.getOrDefault(k, "?????");
              assertEquals("Compare in memory gflag " + k, v, actual);
              String onDisk = gflagsOnDisk.getOrDefault(k, "?????");
              assertEquals("Compare on disk gflag " + k, v, onDisk);
            });
      }
    }
  }

  private Map<String, String> getDiskFlags(
      NodeDetails nodeDetails, Universe universe, UniverseTaskBase.ServerType serverType) {
    Map<String, String> results = new HashMap<>();
    UniverseDefinitionTaskParams.UserIntent userIntent =
        universe.getCluster(nodeDetails.placementUuid).userIntent;
    String gflagsFile =
        localNodeManager.getNodeGFlagsFile(
            userIntent, serverType, localNodeManager.getNodeInfo(nodeDetails));
    try (FileReader fr = new FileReader(gflagsFile);
        BufferedReader br = new BufferedReader(fr)) {
      String line;
      while ((line = br.readLine()) != null) {
        String[] split = line.split("=");
        String key = split[0].substring(2);
        String val = split.length == 1 ? "" : split[1];
        results.put(key, val);
      }
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
    return results;
  }
}
