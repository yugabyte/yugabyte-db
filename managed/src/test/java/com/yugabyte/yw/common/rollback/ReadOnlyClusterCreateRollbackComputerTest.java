// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.rollback;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.commissioner.tasks.ReadOnlyClusterDelete;
import com.yugabyte.yw.commissioner.tasks.ReadOnlyKubernetesClusterDelete;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.ClusterType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class ReadOnlyClusterCreateRollbackComputerTest extends FakeDBApplication {

  private ReadOnlyClusterCreateRollbackComputer computer;
  private Customer customer;
  private Universe universe;
  private UUID readOnlyClusterUUID;

  @Before
  public void setUp() {
    computer = app.injector().instanceOf(ReadOnlyClusterCreateRollbackComputer.class);
    customer = ModelFactory.testCustomer();
    universe = ModelFactory.createUniverse(customer.getId());
    readOnlyClusterUUID = UUID.randomUUID();
  }

  private Cluster readOnlyCluster() {
    Cluster cluster = new Cluster(ClusterType.ASYNC, new UserIntent());
    cluster.uuid = readOnlyClusterUUID;
    return cluster;
  }

  private TaskInfo failedCreate(TaskType taskType) {
    UniverseDefinitionTaskParams params = new UniverseDefinitionTaskParams();
    params.setUniverseUUID(universe.getUniverseUUID());
    params.clusters.add(readOnlyCluster());
    TaskInfo taskInfo = new TaskInfo(taskType, null);
    taskInfo.setUuid(UUID.randomUUID());
    taskInfo.setTaskState(TaskInfo.State.Failure);
    taskInfo.setTaskParams(Json.toJson(params));
    return taskInfo;
  }

  private void freezeUniverse(UUID owner, boolean withReadReplica) {
    Universe.saveDetails(
        universe.getUniverseUUID(),
        u -> {
          UniverseDefinitionTaskParams details = u.getUniverseDetails();
          details.updatingTaskUUID = owner;
          details.placementModificationTaskUuid = owner;
          details.clusters.removeIf(c -> c.clusterType == ClusterType.ASYNC);
          if (withReadReplica) {
            details.clusters.add(readOnlyCluster());
          }
          u.setUniverseDetails(details);
        });
  }

  @Test
  public void testAllowedWhenCreateOwnsUniverseWithItsCluster() {
    TaskInfo failed = failedCreate(TaskType.ReadOnlyKubernetesClusterCreate);
    freezeUniverse(failed.getUuid(), true /* withReadReplica */);
    assertTrue(computer.canRollback(failed, universe.getUniverseUUID()));
  }

  @Test
  public void testRejectedWhenCreateDoesNotOwnUniverse() {
    TaskInfo failed = failedCreate(TaskType.ReadOnlyClusterCreate);
    freezeUniverse(UUID.randomUUID(), true /* withReadReplica */);
    assertFalse(computer.canRollback(failed, universe.getUniverseUUID()));
    freezeUniverse(null, true /* withReadReplica */);
    assertFalse(computer.canRollback(failed, universe.getUniverseUUID()));
  }

  @Test
  public void testRejectedWhenClusterNotInUniverse() {
    // A create that failed before writing its cluster leaves nothing to delete; Retry instead.
    TaskInfo failed = failedCreate(TaskType.ReadOnlyClusterCreate);
    freezeUniverse(failed.getUuid(), false /* withReadReplica */);
    assertFalse(computer.canRollback(failed, universe.getUniverseUUID()));
  }

  @Test
  public void testRejectedWhenUniverseMissing() {
    TaskInfo failed = failedCreate(TaskType.ReadOnlyClusterCreate);
    assertFalse(computer.canRollback(failed, UUID.randomUUID()));
  }

  private RollbackSubmission compute(TaskInfo failed) {
    failed.setOwner("test");
    failed.save();
    CustomerTask customerTask =
        CustomerTask.create(
            customer,
            universe.getUniverseUUID(),
            failed.getUuid(),
            CustomerTask.TargetType.Cluster,
            CustomerTask.TaskType.Create,
            universe.getName());
    return computer.compute(
        new RollbackContext(customer, customerTask, failed, failed.getTaskParams()));
  }

  @Test
  public void testComputeKubernetesDelete() {
    RollbackSubmission submission = compute(failedCreate(TaskType.ReadOnlyKubernetesClusterCreate));
    assertEquals(TaskType.ReadOnlyKubernetesClusterDelete, submission.getRollbackTaskType());
    assertEquals(CustomerTask.TaskType.Delete, submission.getCustomerTaskType());
    assertFalse(submission.isSetPreviousTaskUUID());
    ReadOnlyKubernetesClusterDelete.Params params =
        (ReadOnlyKubernetesClusterDelete.Params) submission.getParams();
    assertEquals(universe.getUniverseUUID(), params.getUniverseUUID());
    assertEquals(readOnlyClusterUUID, params.clusterUUID);
    assertFalse(params.isForceDelete);
    assertEquals(-1, params.expectedUniverseVersion.intValue());
  }

  @Test
  public void testComputeVmDelete() {
    RollbackSubmission submission = compute(failedCreate(TaskType.ReadOnlyClusterCreate));
    assertEquals(TaskType.ReadOnlyClusterDelete, submission.getRollbackTaskType());
    ReadOnlyClusterDelete.Params params = (ReadOnlyClusterDelete.Params) submission.getParams();
    assertEquals(universe.getUniverseUUID(), params.getUniverseUUID());
    assertEquals(readOnlyClusterUUID, params.clusterUUID);
    assertFalse(params.isForceDelete);
    assertEquals(-1, params.expectedUniverseVersion.intValue());
  }
}
