// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.local;

import static org.junit.Assert.assertEquals;

import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.tasks.CommissionerBaseTest;
import com.yugabyte.yw.common.CustomerTaskManager;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.gflags.SpecificGFlags;
import com.yugabyte.yw.common.utils.Pair;
import com.yugabyte.yw.forms.UniverseConfigureTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.UUID;
import java.util.function.Function;
import java.util.stream.IntStream;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import lombok.extern.slf4j.Slf4j;
import org.junit.runner.RunWith;
import org.slf4j.MDC;

@RunWith(JUnitParamsRunner.class)
@Slf4j
public class RetryableLocalTests extends LocalProviderUniverseTestBase {
  private static final String ENV_VAR = "RUN_RETRYABLE_TESTS";

  private CustomerTaskManager customerTaskManager;

  @Override
  protected Pair<Integer, Integer> getIpRange() {
    return new Pair(60, 90);
  }

  @Override
  public void setUp() {
    org.junit.Assume.assumeTrue("true".equals(System.getenv(ENV_VAR)));
    super.setUp();
    customerTaskManager = app.injector().instanceOf(CustomerTaskManager.class);
  }

  @Parameters(method = "getEditAbortPositions")
  //  @Test
  public void testAZMoveWithRetries(String abortPosition) throws InterruptedException {
    testRetry(
        (universe) -> {
          UniverseDefinitionTaskParams.Cluster cluster =
              universe.getUniverseDetails().getPrimaryCluster();
          cluster
              .placementInfo
              .azStream()
              .limit(1)
              .forEach(
                  az -> {
                    az.uuid = az4.getUuid();
                  });
          cluster
              .placementInfo
              .azStream()
              .skip(1)
              .limit(1)
              .forEach(
                  az -> {
                    az.numNodesInAZ++;
                  });
          cluster.userIntent.numNodes++;
          // Fully replacing one az with another and
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
          try {
            return CommissionerBaseTest.waitForTask(taskID);
          } catch (InterruptedException e) {
            throw new RuntimeException();
          }
        },
        abortPosition);
  }

  public static Object[] getEditAbortPositions() {
    return IntStream.range(1, 36).boxed().toArray();
  }

  @Parameters(method = "getReadReplicaAbortPositions")
  //  @Test
  public void testAddReadReplicaWithRetries(String abortPosition) throws InterruptedException {
    testRetry(
        (universe) -> {
          try {
            return addReadReplica(
                universe, universe.getUniverseDetails().getPrimaryCluster().userIntent.clone());
          } catch (InterruptedException e) {
            throw new RuntimeException(e);
          }
        },
        abortPosition);
  }

  public static Object[] getReadReplicaAbortPositions() {
    return IntStream.range(1, 14).boxed().toArray();
  }

  private void testRetry(Function<Universe, TaskInfo> taskFunction, String abortPosition)
      throws InterruptedException {
    UniverseDefinitionTaskParams.UserIntent userIntent = getDefaultUserIntent();
    userIntent.enableNodeToNodeEncrypt = false;
    userIntent.enableClientToNodeEncrypt = false;
    userIntent.specificGFlags =
        SpecificGFlags.construct(EditUniverseLocalTest.GFLAGS, EditUniverseLocalTest.GFLAGS);
    Universe universe = createUniverse(userIntent);
    initAndStartPayload(universe);
    MDC.put(Commissioner.SUBTASK_ABORT_POSITION_PROPERTY, abortPosition);
    TaskInfo taskInfo = taskFunction.apply(universe);
    assertEquals(TaskInfo.State.Aborted, taskInfo.getTaskState());
    MDC.remove(Commissioner.SUBTASK_ABORT_POSITION_PROPERTY);
    CustomerTask customerTask =
        customerTaskManager.retryCustomerTask(customer.getUuid(), taskInfo.getTaskUUID());
    taskInfo = CommissionerBaseTest.waitForTask(customerTask.getTaskUUID());
    assertEquals(TaskInfo.State.Success, taskInfo.getTaskState());
    verifyUniverseState(Universe.getOrBadRequest(universe.getUniverseUUID()));
    verifyPayload();
    verifyYSQL(universe);
  }
}
