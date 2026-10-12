// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.rollback;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.DeltaEvaluator;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.NodeDetailsArrayComparator;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.models.helpers.StateTransitionDetails;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class AddNodeToUniverseRollbackComputerTest extends FakeDBApplication {

  private AddNodeToUniverseRollbackComputer computer;
  private SettableRuntimeConfigFactory mutableConfigFactory;
  private Customer customer;
  private Universe universe;

  @Before
  public void setUp() {
    computer = app.injector().instanceOf(AddNodeToUniverseRollbackComputer.class);
    mutableConfigFactory = app.injector().instanceOf(SettableRuntimeConfigFactory.class);
    customer = ModelFactory.testCustomer();
    universe =
        Universe.saveDetails(
            ModelFactory.createUniverse(customer.getId()).getUniverseUUID(),
            ApiUtils.mockUniverseUpdater());
  }

  private void enableFlag() {
    mutableConfigFactory.globalRuntimeConf().setValue("yb.task.allow_add_node_rollback", "true");
  }

  private void seedRollbackableDelta(boolean rollbackSafe) {
    UniverseDefinitionTaskParams before =
        Json.fromJson(
            Json.toJson(universe.getUniverseDetails()), UniverseDefinitionTaskParams.class);
    UniverseDefinitionTaskParams target =
        Json.fromJson(Json.toJson(before), UniverseDefinitionTaskParams.class);
    NodeDetails node =
        target.nodeDetailsSet.stream()
            .findFirst()
            .orElseThrow(() -> new IllegalStateException("universe has no nodes"));
    NodeState beforeState = node.state;
    node.state = NodeState.Live;
    var delta = DeltaEvaluator.buildDeltaJsonTree(before, target, new NodeDetailsArrayComparator());
    // Put before back so restore target differs; seed details on universe.
    node.state = beforeState;
    Universe.saveDetails(
        universe.getUniverseUUID(),
        u -> u.setStateTransitionDetails(new StateTransitionDetails(rollbackSafe, delta)));
    universe = Universe.getOrBadRequest(universe.getUniverseUUID());
  }

  private RollbackContext contextForFailedAdd() {
    NodeTaskParams params = new NodeTaskParams();
    params.setUniverseUUID(universe.getUniverseUUID());
    params.clusters.addAll(universe.getUniverseDetails().clusters);
    params.nodeDetailsSet = universe.getUniverseDetails().nodeDetailsSet;
    params.nodeName = universe.getNodes().iterator().next().nodeName;
    TaskInfo taskInfo = new TaskInfo(TaskType.AddNodeToUniverse, null);
    taskInfo.setUuid(UUID.randomUUID());
    taskInfo.setTaskParams(Json.toJson(params));
    taskInfo.setOwner("");
    taskInfo.setTaskState(TaskInfo.State.Failure);
    taskInfo.save();
    CustomerTask customerTask =
        CustomerTask.create(
            customer,
            universe.getUniverseUUID(),
            taskInfo.getUuid(),
            CustomerTask.TargetType.Node,
            CustomerTask.TaskType.Add,
            params.nodeName);
    return new RollbackContext(customer, customerTask, taskInfo, Json.toJson(params));
  }

  @Test
  public void isEnabledFollowsRuntimeFlag() {
    assertFalse(computer.isEnabled());
    enableFlag();
    assertTrue(computer.isEnabled());
  }

  @Test
  public void testDisabledByRuntimeFlag() {
    seedRollbackableDelta(true);
    PlatformServiceException ex =
        assertThrows(PlatformServiceException.class, () -> computer.compute(contextForFailedAdd()));
    assertTrue(ex.getMessage().contains("not enabled"));
  }

  @Test
  public void testComputeWhenEnabled() {
    enableFlag();
    seedRollbackableDelta(true);
    RollbackSubmission submission = computer.compute(contextForFailedAdd());
    assertEquals(TaskType.RollbackAddNodeToUniverse, submission.getRollbackTaskType());
    assertEquals(CustomerTask.TaskType.RollbackAddNodeToUniverse, submission.getCustomerTaskType());
    assertFalse(submission.isSetPreviousTaskUUID());
    assertTrue(((NodeTaskParams) submission.getParams()).expectedUniverseVersion == -1);
  }

  @Test
  public void testRefuseWhenUnsafe() {
    enableFlag();
    seedRollbackableDelta(false);
    PlatformServiceException ex =
        assertThrows(PlatformServiceException.class, () -> computer.compute(contextForFailedAdd()));
    assertTrue(ex.getMessage().contains("rollbackSafe=false"));
  }
}
