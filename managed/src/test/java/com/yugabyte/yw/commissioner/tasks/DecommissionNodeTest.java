// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase.ServerType;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.controllers.UniverseControllerRequestBinder;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.TaskType;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(JUnitParamsRunner.class)
public class DecommissionNodeTest extends UniverseModifyBaseTest {

  private Universe testUniverse;

  @Override
  @Before
  public void setUp() {
    super.setUp();
    testUniverse = createUniverseForProvider("test-decommission-universe", defaultProvider, 6);
    mockCommonForEditUniverseBasedTasks(testUniverse);
  }

  @Parameters({"MASTER", "TSERVER"})
  @Test
  public void testDecommissionDedicatedNodes(ServerType serverType) throws InterruptedException {
    Universe universe =
        Universe.saveDetails(
            testUniverse.getUniverseUUID(),
            u -> {
              u.getUniverseDetails().getPrimaryCluster().userIntent.ybSoftwareVersion =
                  "2.31.0.0-b1";
              u.getUniverseDetails().nodeDetailsSet.stream()
                  .forEach(
                      n -> {
                        n.dedicatedTo = n.isMaster ? ServerType.MASTER : ServerType.TSERVER;
                        n.isTserver = !n.isMaster;
                      });
            },
            false);
    NodeTaskParams taskParams =
        UniverseControllerRequestBinder.deepCopy(
            universe.getUniverseDetails(), NodeTaskParams.class);
    taskParams.setUniverseUUID(universe.getUniverseUUID());
    taskParams.expectedUniverseVersion = -1;
    NodeDetails nodeDetails =
        universe.getNodes().stream()
            .filter(n -> serverType == ServerType.MASTER ? n.isMaster : n.isTserver)
            .findFirst()
            .orElseThrow(
                () -> new RuntimeException("No " + serverType + " node found in universe"));
    taskParams.nodeName = nodeDetails.getNodeName();
    taskParams.creatingUser = defaultUser;
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () -> commissioner.submit(TaskType.DecommissionNode, taskParams));
    assertTrue(
        exception
            .getMessage()
            .contains(
                serverType == ServerType.MASTER
                    ? "Cannot decommission a dedicated master as it causes under-replicated masters"
                    : "Cannot decommission a dedicated tserver as it causes under-replicated"
                        + " tablets"));
  }

  @Test
  public void testDecommissionNodeRetries() {
    factory.globalRuntimeConf().setValue("yb.checks.change_master_config.enabled", "false");
    factory
        .forUniverse(testUniverse)
        .setValue("yb.checks.node_disk_size.target_usage_percentage", "0");
    NodeTaskParams taskParams =
        UniverseControllerRequestBinder.deepCopy(
            testUniverse.getUniverseDetails(), NodeTaskParams.class);
    taskParams.setUniverseUUID(testUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = -1;
    NodeDetails nodeDetails =
        testUniverse.getNodes().stream()
            .filter(n -> n.isMaster)
            .findFirst()
            .orElseThrow(() -> new RuntimeException("No master node found in universe"));
    taskParams.nodeName = nodeDetails.getNodeName();
    taskParams.creatingUser = defaultUser;
    super.verifyTaskRetries(
        defaultCustomer,
        CustomerTask.TaskType.Decommission,
        CustomerTask.TargetType.Universe,
        taskParams.getUniverseUUID(),
        TaskType.DecommissionNode,
        taskParams);
    checkUniverseNodesStates(taskParams.getUniverseUUID());
  }
}
