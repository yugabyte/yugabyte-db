// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.controllers.UniverseControllerRequestBinder;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.RuntimeConfigEntry;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.TaskType;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class DecommissionNodeTest extends UniverseModifyBaseTest {

  private Universe testUniverse;

  @Override
  @Before
  public void setUp() {
    super.setUp();
    testUniverse = createUniverseForProvider("test-decommission-universe", defaultProvider, 5);
    mockCommonForEditUniverseBasedTasks(testUniverse);
  }

  @Test
  public void testDecommissionNodeRetries() {
    RuntimeConfigEntry.upsertGlobal("yb.checks.change_master_config.enabled", "false");
    RuntimeConfigEntry.upsert(
        testUniverse, "yb.checks.node_disk_size.target_usage_percentage", "0");
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
