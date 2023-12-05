// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.local;

import static org.junit.Assert.assertEquals;

import com.yugabyte.yw.commissioner.tasks.CommissionerBaseTest;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.forms.UniverseConfigureTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.junit.Test;

@Slf4j
public class EditUniverseLocalTest extends LocalProviderUniverseTestBase {

  @Test
  public void testExpand() throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.specificGFlags = SpecificGFlags.construct(GFLAGS, GFLAGS);
    Universe universe = createUniverse(userIntent);
    initYSQL(universe);
    UniverseDefinitionTaskParams.Cluster cluster =
        universe.getUniverseDetails().getPrimaryCluster();
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
    UUID taskID =
        universeCRUDHandler.update(
            customer,
            Universe.getOrBadRequest(universe.getUniverseUUID()),
            universe.getUniverseDetails());
    TaskInfo taskInfo = CommissionerBaseTest.waitForTask(taskID);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
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
    assertEquals(
        3,
        universe.getUniverseDetails().nodeDetailsSet.stream()
            .filter(n -> n.state == NodeDetails.NodeState.ToBeAdded)
            .count());
    assertEquals(
        3,
        universe.getUniverseDetails().nodeDetailsSet.stream()
            .filter(n -> n.state == NodeDetails.NodeState.ToBeRemoved)
            .count());
    UUID taskID =
        universeCRUDHandler.update(
            customer,
            Universe.getOrBadRequest(universe.getUniverseUUID()),
            universe.getUniverseDetails());
    TaskInfo taskInfo = CommissionerBaseTest.waitForTask(taskID);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
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
    UUID taskID =
        universeCRUDHandler.update(
            customer,
            Universe.getOrBadRequest(universe.getUniverseUUID()),
            universe.getUniverseDetails());
    TaskInfo taskInfo = CommissionerBaseTest.waitForTask(taskID);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    verifyUniverseState(Universe.getOrBadRequest(universe.getUniverseUUID()));
    verifyYSQL(universe);
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

    UniverseDefinitionTaskParams.Cluster cluster =
        universe.getUniverseDetails().getPrimaryCluster();
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
    UUID taskID =
        universeCRUDHandler.update(
            customer,
            Universe.getOrBadRequest(universe.getUniverseUUID()),
            universe.getUniverseDetails());
    TaskInfo taskInfo = CommissionerBaseTest.waitForTask(taskID);
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    verifyUniverseState(Universe.getOrBadRequest(universe.getUniverseUUID()));
    verifyYSQL(universe);
    verifyYSQL(universe, true);
  }
}
