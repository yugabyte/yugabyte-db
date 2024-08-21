// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.local;

import static org.junit.Assert.assertEquals;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.LocalNodeManager;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.TaskType;
import java.io.IOException;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Optional;
import java.util.UUID;
import java.util.stream.Collectors;
import org.junit.Before;
import org.junit.Test;

public class AutoMasterFailoverLocalTest extends LocalProviderUniverseTestBase {
  @Inject LocalNodeManager localNodeManager;

  @Before
  @Override
  public void setUp() {
    super.setUp();
    jobScheduler.init();
    autoMasterFailoverScheduler.init();
  }

  @Override
  protected Pair<Integer, Integer> getIpRange() {
    return new Pair<>(30, 60);
  }

  private void enableMasterFailover(Universe universe) {
    settableRuntimeConfigFactory
        .globalRuntimeConf()
        .setValue(GlobalConfKeys.autoMasterFailoverPollerInterval.getKey(), "5s");
    settableRuntimeConfigFactory
        .forUniverse(universe)
        .setValue(UniverseConfKeys.autoMasterFailoverCooldown.getKey(), "3s");
    settableRuntimeConfigFactory
        .forUniverse(universe)
        .setValue(UniverseConfKeys.autoMasterFailoverDetectionInterval.getKey(), "3s");
    settableRuntimeConfigFactory
        .forUniverse(universe)
        .setValue(UniverseConfKeys.autoMasterFailoverFollowerLagSoftThreshold.getKey(), "3s");
    settableRuntimeConfigFactory
        .forUniverse(universe)
        .setValue(UniverseConfKeys.autoMasterFailoverFollowerLagHardThreshold.getKey(), "5s");
    settableRuntimeConfigFactory
        .forUniverse(universe)
        .setValue(UniverseConfKeys.enableAutoMasterFailover.getKey(), "true");
  }

  // Pick the tserver only nodes to stop the process.
  private List<NodeDetails> pickEligibleTservers(Universe universe, int count, UUID excludeAz) {
    List<NodeDetails> tservers = new ArrayList<>();
    Map<UUID, List<NodeDetails>> nodes =
        universe.getNodes().stream()
            .filter(n -> excludeAz == null || !excludeAz.equals(n.azUuid))
            .collect(Collectors.groupingBy(n -> n.azUuid));
    for (Map.Entry<UUID, List<NodeDetails>> entry : nodes.entrySet()) {
      if (tservers.size() >= count) {
        break;
      }
      if (entry.getValue().size() < 2) {
        continue;
      }
      List<NodeDetails> tserverOnlyNodes =
          entry.getValue().stream()
              .filter(n -> n.isTserver && !n.isMaster)
              .collect(Collectors.toList());
      if (tserverOnlyNodes.size() > 0) {
        tservers.add(tserverOnlyNodes.get(0));
      }
    }
    return tservers;
  }

  // Pick a master node to stop both tserver and master processes.
  private NodeDetails pickEligibleMasterNode(Universe universe)
      throws IOException, InterruptedException {
    Map<UUID, List<NodeDetails>> nodes =
        universe.getNodes().stream().collect(Collectors.groupingBy(n -> n.azUuid));
    String leaderHost = getMasterLeader(universe);
    for (Map.Entry<UUID, List<NodeDetails>> entry : nodes.entrySet()) {
      if (entry.getValue().size() < 2) {
        continue;
      }
      int numMasters = (int) entry.getValue().stream().filter(n -> n.isMaster).count();
      if (numMasters == 0 || numMasters == entry.getValue().size()) {
        // Ignore AZs without any master replacement node.
        continue;
      }
      // Ignore master leader to shorten time.
      Optional<NodeDetails> optional =
          entry.getValue().stream()
              .filter(n -> n.isMaster && !Objects.equals(leaderHost, n.cloudInfo.private_ip))
              .findFirst();
      if (!optional.isPresent()) {
        continue;
      }
      return optional.get();
    }
    throw new IllegalStateException("No eligible master found");
  }

  @Test
  public void testAutoMasterFailover() throws InterruptedException, IOException {
    UniverseDefinitionTaskParams.UserIntent userIntent =
        getDefaultUserIntent("test-universe-1", false, 3, 6);
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    Universe universe = createUniverse(userIntent);
    CustomerTask customerTask = CustomerTask.getLastTaskByTargetUuid(universe.getUniverseUUID());
    enableMasterFailover(universe);
    NodeDetails pickedMasterNode = pickEligibleMasterNode(universe);
    List<NodeDetails> pickedTserverNodes =
        pickEligibleTservers(universe, 1 /* count */, pickedMasterNode.azUuid /* exclude AZ */);
    assertEquals(1, pickedTserverNodes.size());
    // Kill tserver process on a different node along with master and tserver on another node.
    for (NodeDetails pickedTserverNode : pickedTserverNodes) {
      killProcessesOnNode(universe.getUniverseUUID(), pickedTserverNode.getNodeName());
    }
    killProcessesOnNode(universe.getUniverseUUID(), pickedMasterNode.getNodeName());
    TaskInfo taskInfo =
        waitForNextTask(
            universe.getUniverseUUID(), customerTask.getTaskUUID(), Duration.ofMinutes(5));
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    assertEquals(TaskType.MasterFailover, taskInfo.getTaskType());
    // Start all the killed processes.
    for (NodeDetails pickedTserverNode : pickedTserverNodes) {
      startProcessesOnNode(
          universe.getUniverseUUID(), pickedTserverNode, UniverseTaskBase.ServerType.TSERVER);
    }
    startProcessesOnNode(
        universe.getUniverseUUID(), pickedMasterNode, UniverseTaskBase.ServerType.TSERVER);
    startProcessesOnNode(
        universe.getUniverseUUID(), pickedMasterNode, UniverseTaskBase.ServerType.MASTER);
    taskInfo =
        waitForNextTask(universe.getUniverseUUID(), taskInfo.getTaskUUID(), Duration.ofMinutes(5));
    assertEquals(TaskType.SyncMasterAddresses, taskInfo.getTaskType());
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    assertEquals(false, isMasterProcessRunning(pickedMasterNode.getNodeName()));
  }
}
