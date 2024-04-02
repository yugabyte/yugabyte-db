// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.local;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.containsString;
import static org.junit.Assert.assertEquals;

import com.yugabyte.yw.commissioner.tasks.CommissionerBaseTest;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.UniverseConfigureTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.RuntimeConfigEntry;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.HashMap;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;
import org.junit.Test;
import play.libs.Json;

@Slf4j
public class EditUniverseLocalTest extends LocalProviderUniverseTestBase {

  @Override
  protected Pair<Integer, Integer> getIpRange() {
    return new Pair(2, 30);
  }

  @Test
  public void testExpand() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    Universe universe = createUniverse(userIntent);
    initYSQL(universe);
    changeNumberOfNodesInPrimary(universe, 2);
    UUID taskID =
        universeCRUDHandler.update(
            customer,
            Universe.getOrBadRequest(universe.getUniverseUUID()),
            universe.getUniverseDetails());
    TaskInfo taskInfo = CommissionerBaseTest.waitForTask(taskID);
    verifyUniverseTaskSuccess(taskInfo);
    verifyUniverseState(Universe.getOrBadRequest(universe.getUniverseUUID()));
    verifyYSQL(universe);
  }

  @Test
  public void testFullMove() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    Universe universe = createUniverse(userIntent);
    initYSQL(universe);
    UniverseDefinitionTaskParams.Cluster cluster =
        universe.getUniverseDetails().getPrimaryCluster();
    cluster.userIntent.instanceType = INSTANCE_TYPE_CODE_2;
    PlacementInfoUtil.updateUniverseDefinition(
        universe.getUniverseDetails(),
        customer.getId(),
        cluster.uuid,
        UniverseConfigureTaskParams.ClusterOperationType.EDIT);
    verifyNodeModifications(universe, 3, 3);
    UUID taskID =
        universeCRUDHandler.update(
            customer,
            Universe.getOrBadRequest(universe.getUniverseUUID()),
            universe.getUniverseDetails());
    TaskInfo taskInfo = CommissionerBaseTest.waitForTask(taskID);
    verifyUniverseTaskSuccess(taskInfo);
    verifyUniverseState(Universe.getOrBadRequest(universe.getUniverseUUID()));
    verifyYSQL(universe);
  }

  @Test
  public void testAZMove() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    Universe universe = createUniverse(userIntent);
    initYSQL(universe);
    UniverseDefinitionTaskParams.Cluster cluster =
        universe.getUniverseDetails().getPrimaryCluster();
    cluster.placementInfo.azStream().limit(1).forEach(az -> az.uuid = az4.getUuid());
    PlacementInfoUtil.updateUniverseDefinition(
        universe.getUniverseDetails(),
        customer.getId(),
        cluster.uuid,
        UniverseConfigureTaskParams.ClusterOperationType.EDIT);
    verifyNodeModifications(universe, 1, 1);
    UUID taskID =
        universeCRUDHandler.update(
            customer,
            Universe.getOrBadRequest(universe.getUniverseUUID()),
            universe.getUniverseDetails());
    TaskInfo taskInfo = CommissionerBaseTest.waitForTask(taskID);
    verifyUniverseTaskSuccess(taskInfo);
    verifyUniverseState(Universe.getOrBadRequest(universe.getUniverseUUID()));
    verifyYSQL(universe);
  }

  @Test
  public void testTwoAZMoves() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.specificGFlags = getGFlags("follower_unavailable_considered_failed_sec", "5");
    Universe universe = createUniverse(userIntent);
    initYSQL(universe);
    UniverseDefinitionTaskParams.Cluster cluster =
        universe.getUniverseDetails().getPrimaryCluster();
    AtomicReference<UUID> removingAz = new AtomicReference<>();
    cluster
        .placementInfo
        .azStream()
        .limit(1)
        .forEach(
            az -> {
              removingAz.set(az.uuid);
              az.uuid = az4.getUuid();
            });
    PlacementInfoUtil.updateUniverseDefinition(
        universe.getUniverseDetails(),
        customer.getId(),
        cluster.uuid,
        UniverseConfigureTaskParams.ClusterOperationType.EDIT);
    verifyNodeModifications(universe, 1, 1);
    UUID taskID =
        universeCRUDHandler.update(
            customer,
            Universe.getOrBadRequest(universe.getUniverseUUID()),
            universe.getUniverseDetails());
    TaskInfo taskInfo = CommissionerBaseTest.waitForTask(taskID);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    verifyUniverseState(Universe.getOrBadRequest(universe.getUniverseUUID()));
    verifyYSQL(universe);
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    cluster = universe.getUniverseDetails().getPrimaryCluster();
    cluster
        .placementInfo
        .azStream()
        .filter(az -> az.uuid.equals(az4.getUuid()))
        .limit(1)
        .forEach(az -> az.uuid = removingAz.get()); // Going back to prev az.
    PlacementInfoUtil.updateUniverseDefinition(
        universe.getUniverseDetails(),
        customer.getId(),
        cluster.uuid,
        UniverseConfigureTaskParams.ClusterOperationType.EDIT);
    verifyNodeModifications(universe, 1, 1);
    taskID =
        universeCRUDHandler.update(
            customer,
            Universe.getOrBadRequest(universe.getUniverseUUID()),
            universe.getUniverseDetails());
    taskInfo = CommissionerBaseTest.waitForTask(taskID);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
  }

  @Test
  public void testTimeoutingChangeMasterConfig() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    RuntimeConfigEntry.upsertGlobal("yb.checks.change_master_config.timeout", "30s");
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    Universe universe = createUniverse(userIntent);
    initYSQL(universe);
    UniverseDefinitionTaskParams.Cluster cluster =
        universe.getUniverseDetails().getPrimaryCluster();
    cluster.placementInfo.azStream().limit(1).forEach(az -> az.uuid = az4.getUuid());
    PlacementInfoUtil.updateUniverseDefinition(
        universe.getUniverseDetails(),
        customer.getId(),
        cluster.uuid,
        UniverseConfigureTaskParams.ClusterOperationType.EDIT);
    assertEquals(
        1,
        universe.getUniverseDetails().nodeDetailsSet.stream()
            .filter(n -> n.state == NodeDetails.NodeState.ToBeAdded)
            .count());
    assertEquals(
        1,
        universe.getUniverseDetails().nodeDetailsSet.stream()
            .filter(n -> n.state == NodeDetails.NodeState.ToBeRemoved)
            .count());
    localNodeManager.setAdditionalGFlags(
        SpecificGFlags.construct(
            Map.of("TEST_inject_latency_during_remote_bootstrap_secs", "600"), new HashMap<>()));
    UUID taskID =
        universeCRUDHandler.update(
            customer,
            Universe.getOrBadRequest(universe.getUniverseUUID()),
            universe.getUniverseDetails());
    TaskInfo taskInfo = CommissionerBaseTest.waitForTask(taskID);
    assertEquals(TaskInfo.State.Failure, taskInfo.getTaskState());
    assertThat(
        taskInfo.getSubTasks().stream()
            .filter(t -> t.getTaskType() == TaskType.ChangeMasterConfig)
            .findFirst()
            .get()
            .getDetails()
            .get("errorString")
            .asText(),
        containsString("AddMaster operation has not completed within PT30S"));
  }

  @Test
  public void testExpandWithRR() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    Universe universe = createUniverse(userIntent);
    initYSQL(universe);
    doAddReadReplica(universe, getDefaultUserIntent());
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    verifyYSQL(universe, true);

    changeNumberOfNodesInPrimary(universe, 2);
    UUID taskID =
        universeCRUDHandler.update(
            customer,
            Universe.getOrBadRequest(universe.getUniverseUUID()),
            universe.getUniverseDetails());
    TaskInfo taskInfo = CommissionerBaseTest.waitForTask(taskID);
    verifyUniverseTaskSuccess(taskInfo);
    verifyUniverseState(Universe.getOrBadRequest(universe.getUniverseUUID()));
    verifyYSQL(universe);
    verifyYSQL(universe, true);
  }

  @Test
  public void testIncreaseRFInRR() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    Universe universe = createUniverse(userIntent);
    initYSQL(universe);
    UniverseDefinitionTaskParams.UserIntent rrIntent = getDefaultUserIntent();
    rrIntent.replicationFactor = 1;
    rrIntent.numNodes = 3;
    doAddReadReplica(universe, rrIntent);
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    verifyYSQL(universe, true);

    UniverseDefinitionTaskParams.Cluster cluster =
        universe.getUniverseDetails().getReadOnlyClusters().get(0);
    cluster.userIntent.replicationFactor++;
    PlacementInfoUtil.updateUniverseDefinition(
        universe.getUniverseDetails(),
        customer.getId(),
        cluster.uuid,
        UniverseConfigureTaskParams.ClusterOperationType.EDIT);
    assertEquals(
        0,
        universe.getUniverseDetails().nodeDetailsSet.stream()
            .filter(
                n ->
                    n.state == NodeDetails.NodeState.ToBeAdded
                        || n.state == NodeDetails.NodeState.ToBeRemoved)
            .count());

    UniverseDefinitionTaskParams taskParams = universe.getUniverseDetails();
    taskParams.currentClusterType = UniverseDefinitionTaskParams.ClusterType.ASYNC;

    UUID taskID =
        universeCRUDHandler.update(
            customer, Universe.getOrBadRequest(universe.getUniverseUUID()), taskParams);
    TaskInfo taskInfo = CommissionerBaseTest.waitForTask(taskID);
    verifyUniverseTaskSuccess(taskInfo);
    verifyUniverseState(Universe.getOrBadRequest(universe.getUniverseUUID()));
    verifyYSQL(universe);
    verifyYSQL(universe, true);
  }

  @Test
  public void testDecreaseRFInRR() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    Universe universe = createUniverse(userIntent);
    initYSQL(universe);
    UniverseDefinitionTaskParams.UserIntent rrIntent = getDefaultUserIntent();
    rrIntent.replicationFactor = 3;
    rrIntent.numNodes = 3;
    doAddReadReplica(universe, rrIntent);
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    verifyYSQL(universe, true);

    UniverseDefinitionTaskParams.Cluster cluster =
        universe.getUniverseDetails().getReadOnlyClusters().get(0);
    cluster.userIntent.replicationFactor--;
    PlacementInfoUtil.updateUniverseDefinition(
        universe.getUniverseDetails(),
        customer.getId(),
        cluster.uuid,
        UniverseConfigureTaskParams.ClusterOperationType.EDIT);
    UniverseDefinitionTaskParams taskParams = universe.getUniverseDetails();
    taskParams.currentClusterType = UniverseDefinitionTaskParams.ClusterType.ASYNC;

    UUID taskID =
        universeCRUDHandler.update(
            customer, Universe.getOrBadRequest(universe.getUniverseUUID()), taskParams);
    TaskInfo taskInfo = CommissionerBaseTest.waitForTask(taskID);
    verifyUniverseTaskSuccess(taskInfo);
    verifyUniverseState(Universe.getOrBadRequest(universe.getUniverseUUID()));
    verifyYSQL(universe);
    verifyYSQL(universe, true);
  }

  @Test
  public void testAddNodesInRR() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    Universe universe = createUniverse(userIntent);
    initYSQL(universe);
    UniverseDefinitionTaskParams.UserIntent rrIntent = getDefaultUserIntent();
    rrIntent.replicationFactor = 1;
    rrIntent.numNodes = 1;
    doAddReadReplica(universe, rrIntent);
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    verifyYSQL(universe, true);

    UniverseDefinitionTaskParams.Cluster cluster =
        universe.getUniverseDetails().getReadOnlyClusters().get(0);
    cluster.userIntent.numNodes += 2;
    PlacementInfoUtil.updateUniverseDefinition(
        universe.getUniverseDetails(),
        customer.getId(),
        cluster.uuid,
        UniverseConfigureTaskParams.ClusterOperationType.EDIT);
    assertEquals(
        2,
        universe.getUniverseDetails().nodeDetailsSet.stream()
            .filter(n -> n.state == NodeDetails.NodeState.ToBeAdded)
            .count());

    UniverseDefinitionTaskParams taskParams = universe.getUniverseDetails();
    taskParams.currentClusterType = UniverseDefinitionTaskParams.ClusterType.ASYNC;

    UUID taskID =
        universeCRUDHandler.update(
            customer, Universe.getOrBadRequest(universe.getUniverseUUID()), taskParams);
    TaskInfo taskInfo = CommissionerBaseTest.waitForTask(taskID);
    verifyUniverseTaskSuccess(taskInfo);
    verifyUniverseState(Universe.getOrBadRequest(universe.getUniverseUUID()));
    verifyYSQL(universe);
    verifyYSQL(universe, true);
  }

  @Test
  public void testUnknownMasterBeforeEdit() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.numNodes = 5;
    userIntent.replicationFactor = 5;
    Universe universe = createUniverse(userIntent);
    initYSQL(universe);
    String masterLeaderIP = getMasterLeader(universe);

    universe =
        Universe.saveDetails(
            universe.getUniverseUUID(),
            u -> {
              UniverseDefinitionTaskParams details = u.getUniverseDetails();
              details.nodeDetailsSet.stream()
                  .filter(n -> !n.cloudInfo.private_ip.equals(masterLeaderIP))
                  .limit(2)
                  .forEach(n -> n.isMaster = false);
              details.getPrimaryCluster().userIntent.replicationFactor =
                  3; // Pretending we have RF3
              u.setUniverseDetails(details);
            });
    log.debug("Universe {}", Json.toJson(universe.getUniverseDetails()));
    UniverseDefinitionTaskParams.Cluster cluster =
        universe.getUniverseDetails().getPrimaryCluster();
    cluster.userIntent.numNodes += 1;
    PlacementInfoUtil.updateUniverseDefinition(
        universe.getUniverseDetails(),
        customer.getId(),
        cluster.uuid,
        UniverseConfigureTaskParams.ClusterOperationType.EDIT);

    UUID taskID =
        universeCRUDHandler.update(
            customer,
            Universe.getOrBadRequest(universe.getUniverseUUID()),
            universe.getUniverseDetails());
    TaskInfo taskInfo = CommissionerBaseTest.waitForTask(taskID);
    assertEquals(TaskInfo.State.Failure, taskInfo.getTaskState());
    String error = getAllErrorsStr(taskInfo);
    assertThat(error, containsString("Unexpected MASTER: "));
  }

  @Test
  public void testUnknownTserverBeforeEdit() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.numNodes = 4;
    Universe universe = createUniverse(userIntent);
    initYSQL(universe);
    NodeDetails removed = silentlyRemoveNode(universe, false, true);
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
    changeNumberOfNodesInPrimary(universe, 1);

    UUID taskID =
        universeCRUDHandler.update(
            customer,
            Universe.getOrBadRequest(universe.getUniverseUUID()),
            universe.getUniverseDetails());
    TaskInfo taskInfo = CommissionerBaseTest.waitForTask(taskID);
    assertEquals(TaskInfo.State.Failure, taskInfo.getTaskState());
    String error = getAllErrorsStr(taskInfo);
    assertThat(error, containsString("Unexpected TSERVER: " + removed.cloudInfo.private_ip));
  }

  @Test
  public void testLeaderlessTabletsBeforeEdit() throws InterruptedException {
    RuntimeConfigEntry.upsertGlobal("yb.checks.leaderless_tablets.timeout", "10s");
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.numNodes = 3;
    userIntent.replicationFactor = 3;
    Universe universe = createUniverse(userIntent);
    initYSQL(universe);
    log.debug("Universe {}", Json.toJson(universe.getUniverseDetails()));
    universe.getNodes().stream()
        .limit(2)
        .forEach(
            n -> {
              try {
                localNodeManager.killProcess(n.getNodeName(), UniverseTaskBase.ServerType.TSERVER);
              } catch (Exception e) {
                throw new RuntimeException("Failed to kill process", e);
              }
            });
    Thread.sleep(TimeUnit.SECONDS.toMillis(65));
    UniverseDefinitionTaskParams.Cluster cluster =
        universe.getUniverseDetails().getPrimaryCluster();
    cluster.userIntent.numNodes += 1;
    PlacementInfoUtil.updateUniverseDefinition(
        universe.getUniverseDetails(),
        customer.getId(),
        cluster.uuid,
        UniverseConfigureTaskParams.ClusterOperationType.EDIT);
    UUID taskID =
        universeCRUDHandler.update(
            customer,
            Universe.getOrBadRequest(universe.getUniverseUUID()),
            universe.getUniverseDetails());
    TaskInfo taskInfo = CommissionerBaseTest.waitForTask(taskID);
    assertEquals(TaskInfo.State.Failure, taskInfo.getTaskState());
    String error = getAllErrorsStr(taskInfo);
    assertThat(error, containsString("There are leaderless tablets"));
  }

  private NodeDetails silentlyRemoveNode(Universe universe, boolean isMaster, boolean isTserver) {
    AtomicReference<NodeDetails> node = new AtomicReference<>();
    Universe.saveDetails(
        universe.getUniverseUUID(),
        u -> {
          UniverseDefinitionTaskParams details = u.getUniverseDetails();
          details.getPrimaryCluster().userIntent.numNodes--;
          NodeDetails nodeToRemove =
              details.nodeDetailsSet.stream()
                  .filter(n -> n.isTserver == isTserver && n.isMaster == isMaster)
                  .findFirst()
                  .get();
          node.set(nodeToRemove);
          details.nodeDetailsSet.remove(nodeToRemove);
          details
              .getPrimaryCluster()
              .placementInfo
              .azStream()
              .filter(az -> az.uuid.equals(nodeToRemove.azUuid))
              .forEach(az -> az.numNodesInAZ--);
          u.setUniverseDetails(details);
        });
    return node.get();
  }

  private void changeNumberOfNodesInPrimary(Universe universe, int increment) {
    changeNumberOfNodesInCluster(
        universe, universe.getUniverseDetails().getPrimaryCluster().uuid, increment);
  }

  private void changeNumberOfNodesInCluster(Universe universe, UUID clusterUUID, int increment) {
    UniverseDefinitionTaskParams.Cluster cluster =
        universe.getUniverseDetails().getClusterByUuid(clusterUUID);
    cluster.userIntent.numNodes += increment;
    PlacementInfoUtil.updateUniverseDefinition(
        universe.getUniverseDetails(),
        customer.getId(),
        cluster.uuid,
        UniverseConfigureTaskParams.ClusterOperationType.EDIT);
    verifyNodeModifications(
        universe, increment > 0 ? increment : 0, increment < 0 ? -increment : 0);
  }

  private SpecificGFlags getGFlags(String... additional) {
    Map<String, String> gflags = new HashMap<>(GFLAGS);
    for (int i = 0; i < additional.length / 2; i++) {
      gflags.put(additional[i], additional[i + 1]);
    }
    return SpecificGFlags.construct(gflags, gflags);
  }

  private String getAllErrorsStr(TaskInfo taskInfo) {
    StringBuilder sb = new StringBuilder(taskInfo.getErrorMessage());
    for (TaskInfo subTask : taskInfo.getSubTasks()) {
      if (!StringUtils.isEmpty(subTask.getErrorMessage())) {
        sb.append("\n").append(subTask.getErrorMessage());
      }
    }
    return sb.toString();
  }

  private void verifyNodeModifications(Universe universe, int added, int removed) {
    assertEquals(
        added,
        universe.getUniverseDetails().nodeDetailsSet.stream()
            .filter(n -> n.state == NodeDetails.NodeState.ToBeAdded)
            .count());
    assertEquals(
        removed,
        universe.getUniverseDetails().nodeDetailsSet.stream()
            .filter(n -> n.state == NodeDetails.NodeState.ToBeRemoved)
            .count());
  }
}
