// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static org.junit.Assert.fail;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doAnswer;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.NodeManager.NodeCommandType;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.controllers.UniverseControllerRequestBinder;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.RuntimeConfigEntry;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.TaskType;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import org.yb.client.ListTabletServersResponse;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class ReplaceNodeInUniverseTest extends UniverseModifyBaseTest {

  @Override
  @Before
  public void setUp() {
    super.setUp();
    mockCommonForEditUniverseBasedTasks(defaultUniverse);
    doAnswer(
            invocation -> {
              ObjectNode obj = Json.newObject();
              obj.put("private_ip", "10.20.30.40");
              ShellResponse shellResponse = new ShellResponse();
              shellResponse.code = 0;
              shellResponse.message = obj.toString();
              return shellResponse;
            })
        .when(mockNodeManager)
        .nodeCommand(eq(NodeCommandType.List), any());
    try {
      ListTabletServersResponse mockListTabletServersResponse =
          mock(ListTabletServersResponse.class);
      when(mockListTabletServersResponse.getTabletServersCount()).thenReturn(10);
      when(mockClient.listTabletServers()).thenReturn(mockListTabletServersResponse);
    } catch (Exception e) {
      fail(e.getMessage());
    }
  }

  @Test
  public void testReplaceNodeInUniverseRetries() {
    RuntimeConfigEntry.upsertGlobal("yb.checks.change_master_config.enabled", "false");
    RuntimeConfigEntry.upsert(
        defaultUniverse, "yb.checks.node_disk_size.target_usage_percentage", "0");

    NodeTaskParams taskParams =
        UniverseControllerRequestBinder.deepCopy(
            defaultUniverse.getUniverseDetails(), NodeTaskParams.class);
    taskParams.setUniverseUUID(defaultUniverse.getUniverseUUID());
    taskParams.expectedUniverseVersion = -1;
    NodeDetails nodeDetails =
        defaultUniverse.getNodes().stream()
            .filter(n -> n.isMaster)
            .findFirst()
            .orElseThrow(() -> new RuntimeException("No master node found in universe"));
    taskParams.nodeName = nodeDetails.getNodeName();
    taskParams.creatingUser = defaultUser;
    super.verifyTaskRetries(
        defaultCustomer,
        CustomerTask.TaskType.Replace,
        CustomerTask.TargetType.Node,
        taskParams.getUniverseUUID(),
        TaskType.ReplaceNodeInUniverse,
        taskParams);
    checkUniverseNodesStates(taskParams.getUniverseUUID());
  }
}
