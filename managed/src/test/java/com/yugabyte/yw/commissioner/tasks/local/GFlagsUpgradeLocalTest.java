// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.local;

import static com.yugabyte.yw.common.Util.YUGABYTE_DB;
import static com.yugabyte.yw.forms.UniverseConfigureTaskParams.ClusterOperationType.CREATE;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.containsString;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.commissioner.tasks.CommissionerBaseTest;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.LocalNodeManager;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.gflags.GFlagGroup;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.controllers.UniverseControllerRequestBinder;
import com.yugabyte.yw.forms.GFlagsUpgradeParams;
import com.yugabyte.yw.forms.ResizeNodeParams;
import com.yugabyte.yw.forms.RollMaxBatchSize;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseResp;
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
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.function.Consumer;
import java.util.stream.Collectors;
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
  public void testResizeWithRR() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.specificGFlags.setGflagGroups(
        Collections.singletonList(GFlagGroup.GroupName.ENHANCED_POSTGRES_COMPATIBILITY));
    Universe universe = createUniverse(userIntent);
    UniverseDefinitionTaskParams.UserIntent rrIntent = getDefaultUserIntent();
    rrIntent.numNodes = 1;
    rrIntent.replicationFactor = 1;
    rrIntent.specificGFlags =
        SpecificGFlags.construct(
            Collections.singletonMap("max_log_size", "1805"),
            Collections.singletonMap("log_max_seconds_to_retain", "86333"));
    addReadReplica(universe, rrIntent);
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    UniverseDefinitionTaskParams.Cluster rrCluster =
        universe.getUniverseDetails().getReadOnlyClusters().get(0);
    NodeDetails primary =
        universe.getNodesByCluster(universe.getUniverseDetails().getPrimaryCluster().uuid).get(0);
    NodeDetails rrNode = universe.getNodesByCluster(rrCluster.uuid).get(0);

    assertTrue(
        getVarz(primary, universe, UniverseTaskBase.ServerType.TSERVER)
            .containsKey("yb_enable_read_committed_isolation"));
    assertTrue(
        getVarz(rrNode, universe, UniverseTaskBase.ServerType.TSERVER)
            .containsKey("yb_enable_read_committed_isolation"));

    ResizeNodeParams resizeParams =
        getUpgradeParams(
            universe, UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE, ResizeNodeParams.class);
    resizeParams.clusters = Collections.singletonList(rrCluster);
    rrCluster.userIntent.instanceType = instanceType2.getInstanceTypeCode();
    GFlagsUtil.removeGFlag(
        rrCluster.userIntent, "log_max_seconds_to_retain", UniverseTaskBase.ServerType.TSERVER);

    TaskInfo taskInfo =
        waitForTask(
            upgradeUniverseHandler.resizeNode(
                resizeParams, customer, Universe.getOrBadRequest(universe.getUniverseUUID())),
            universe);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    Map<String, String> newValues =
        getDiskFlags(rrNode, universe, UniverseTaskBase.ServerType.TSERVER);
    assertTrue(newValues.containsKey("yb_enable_read_committed_isolation"));
    assertFalse(newValues.containsKey("log_max_seconds_to_retain"));
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

  @Test
  // Roll a 6 node cluster in batches of 2 per AZ
  public void testStopMultipleNodesInAZ() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.numNodes = 6;
    UniverseDefinitionTaskParams taskParams = new UniverseDefinitionTaskParams();
    taskParams.nodePrefix = "univConfCreate";
    taskParams.upsertPrimaryCluster(userIntent, null);
    PlacementInfoUtil.updateUniverseDefinition(
        taskParams, customer.getId(), taskParams.getPrimaryCluster().uuid, CREATE);

    taskParams.expectedUniverseVersion = -1;
    UniverseResp universeResp = universeCRUDHandler.createUniverse(customer, taskParams);
    TaskInfo taskInfo = waitForTask(universeResp.taskUUID);
    Universe universe = Universe.getOrBadRequest(universeResp.universeUUID);
    verifyUniverseTaskSuccess(taskInfo);

    RuntimeConfigEntry.upsertGlobal(UniverseConfKeys.upgradeBatchRollEnabled.getKey(), "true");
    RuntimeConfigEntry.upsertGlobal(
        UniverseConfKeys.nodesAreSafeToTakeDownCheckTimeout.getKey(), "30s");

    SpecificGFlags specificGFlags =
        SpecificGFlags.construct(
            Collections.singletonMap("max_log_size", "1805"),
            Collections.singletonMap("log_max_seconds_to_retain", "86333"));
    RollMaxBatchSize rollMaxBatchSize = new RollMaxBatchSize();
    rollMaxBatchSize.setPrimaryBatchSize(2);

    taskInfo =
        doGflagsUpgrade(
            universe,
            UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE,
            specificGFlags,
            null,
            TaskInfo.State.Success,
            null,
            params -> params.rollMaxBatchSize = rollMaxBatchSize);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        taskInfo.getSubTasks().stream().collect(Collectors.groupingBy(TaskInfo::getPosition));
    boolean foundTasks = false;
    for (List<TaskInfo> group : subTasksByPosition.values()) {
      if (group.get(0).getTaskType() == TaskType.AnsibleClusterServerCtl) {
        String process = group.get(0).getTaskParams().get("process").asText();
        if (process.equals("tserver")) {
          // Verifying that there are 2 simultaneous stops/starts for tservers.
          assertEquals(2, group.size());
          foundTasks = true;
        }
      }
    }
    assertTrue(foundTasks);
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    compareGFlags(universe);
  }

  @Test
  public void testStopMultipleNodesInAZFallback() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.numNodes = 6;
    Universe universe = createUniverse(userIntent);

    NodeDetails nodeDetails = universe.getNodes().iterator().next();

    String createTblSpace =
        "CREATE TABLESPACE two_zone_tablespace\n"
            + "  WITH (replica_placement='{ \"num_replicas\": 3, \"placement_blocks\": [\n"
            + "    {\"cloud\":\"local\",\"region\":\"region-1\","
            + "\"zone\":\"az-1\",\"min_num_replicas\":1},\n"
            + "    {\"cloud\":\"local\",\"region\":\"region-1\","
            + "\"zone\":\"az-2\",\"min_num_replicas\":2}\n"
            + "]}')";

    ShellResponse response =
        localNodeUniverseManager.runYsqlCommand(
            nodeDetails,
            universe,
            YUGABYTE_DB,
            createTblSpace,
            20,
            userIntent.isYSQLAuthEnabled(),
            true);
    assertTrue("Message is " + response.getMessage(), response.isSuccess());

    response =
        localNodeUniverseManager.runYsqlCommand(
            nodeDetails,
            universe,
            YUGABYTE_DB,
            "CREATE TABLE two_zone_table (id INTEGER, field text)\n"
                + "  TABLESPACE two_zone_tablespace",
            20,
            userIntent.isYSQLAuthEnabled());
    assertTrue(response.isSuccess());

    response =
        localNodeUniverseManager.runYsqlCommand(
            nodeDetails,
            universe,
            YUGABYTE_DB,
            "INSERT INTO two_zone_table \n" + " values (1, 'some_value')",
            20,
            userIntent.isYSQLAuthEnabled());
    assertTrue(response.isSuccess());

    RuntimeConfigEntry.upsertGlobal(UniverseConfKeys.upgradeBatchRollEnabled.getKey(), "true");
    RuntimeConfigEntry.upsertGlobal(
        UniverseConfKeys.nodesAreSafeToTakeDownCheckTimeout.getKey(), "30s");

    SpecificGFlags specificGFlags =
        SpecificGFlags.construct(
            Collections.singletonMap("max_log_size", "1805"),
            Collections.singletonMap("log_max_seconds_to_retain", "86333"));

    UniverseDefinitionTaskParams.Cluster cluster =
        universe.getUniverseDetails().getPrimaryCluster();

    RollMaxBatchSize rollMaxBatchSize = new RollMaxBatchSize();
    // Since we have az-2 with 2 replicas, we cannot stop 2 nodes -> will have fallback.
    rollMaxBatchSize.setPrimaryBatchSize(2);

    TaskInfo taskInfo =
        doGflagsUpgrade(
            universe,
            UpgradeTaskParams.UpgradeOption.ROLLING_UPGRADE,
            specificGFlags,
            null,
            TaskInfo.State.Success,
            null,
            params -> params.rollMaxBatchSize = rollMaxBatchSize);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    Map<Integer, List<TaskInfo>> subTasksByPosition =
        taskInfo.getSubTasks().stream().collect(Collectors.groupingBy(TaskInfo::getPosition));

    boolean foundTasks = false;
    for (List<TaskInfo> group : subTasksByPosition.values()) {
      if (group.get(0).getTaskType() == TaskType.AnsibleClusterServerCtl) {
        String process = group.get(0).getTaskParams().get("process").asText();
        if (process.equals("tserver")) {
          // Verifying that there are 1 simultaneous stops/starts for tservers.
          assertEquals(1, group.size());
          foundTasks = true;
        }
      }
    }
    assertTrue(foundTasks);

    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    compareGFlags(universe);
  }

  @Test
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
        "Aborting because this operation can potentially "
            + "take down a majority of copies of some tablets",
        null);
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
        "Aborting because this operation can potentially "
            + "take down a majority of copies of some tablets",
        null);
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
        "Aborting because this operation can potentially "
            + "take down a majority of copies of some tablets",
        null);
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

  @Test
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

    CommissionerBaseTest.setPausePosition(20);
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

    localNodeManager.killProcess(processedNodeName, UniverseTaskBase.ServerType.TSERVER);
    CommissionerBaseTest.clearAbortOrPausePositions();

    commissioner.resumeTask(taskID);
    taskInfo = waitForTask(taskID);
    assertEquals(TaskInfo.State.Failure, taskInfo.getTaskState());
    assertThat(
        getAllErrorsStr(taskInfo),
        containsString("this operation can potentially take down a majority"));
  }

  protected TaskInfo doGflagsUpgrade(
      Universe universe,
      UpgradeTaskParams.UpgradeOption upgradeOption,
      SpecificGFlags specificGFlags,
      SpecificGFlags rrGflags)
      throws InterruptedException {
    return doGflagsUpgrade(
        universe, upgradeOption, specificGFlags, rrGflags, TaskInfo.State.Success, null, null);
  }

  protected TaskInfo doGflagsUpgrade(
      Universe universe,
      UpgradeTaskParams.UpgradeOption upgradeOption,
      SpecificGFlags specificGFlags,
      SpecificGFlags rrGflags,
      TaskInfo.State expectedState,
      String expectedFailMessage,
      Consumer<GFlagsUpgradeParams> upgradeParamsCustomizer)
      throws InterruptedException {
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    GFlagsUpgradeParams gFlagsUpgradeParams =
        getUpgradeParams(universe, upgradeOption, GFlagsUpgradeParams.class);
    gFlagsUpgradeParams.getPrimaryCluster().userIntent.specificGFlags = specificGFlags;
    if (rrGflags != null) {
      gFlagsUpgradeParams.getReadOnlyClusters().get(0).userIntent.specificGFlags = rrGflags;
    }
    if (upgradeParamsCustomizer != null) {
      upgradeParamsCustomizer.accept(gFlagsUpgradeParams);
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
