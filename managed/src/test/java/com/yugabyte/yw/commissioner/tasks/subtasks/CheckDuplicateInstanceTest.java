// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static org.junit.Assert.assertThrows;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.spy;
import static org.mockito.Mockito.when;

import com.fasterxml.jackson.databind.node.ArrayNode;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.subtasks.CheckDuplicateInstance.Params;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.NodeManager.NodeCommandType;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.util.UUID;
import junitparams.JUnitParamsRunner;
import junitparams.Parameters;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import play.libs.Json;

@RunWith(JUnitParamsRunner.class)
public class CheckDuplicateInstanceTest extends FakeDBApplication {

  private BaseTaskDependencies mockBaseTaskDependencies;
  private CheckDuplicateInstance checkDuplicateInstance;

  @Before
  public void setUp() {
    mockBaseTaskDependencies = mock(BaseTaskDependencies.class);
    when(mockBaseTaskDependencies.getNodeManager()).thenReturn(mockNodeManager);
    checkDuplicateInstance = spy(new CheckDuplicateInstance(mockBaseTaskDependencies));
  }

  @Parameters({
    "1, Live, true",
    "0, ToBeAdded, true",
    "0, Decommissioned, true",
    "2, Live, false",
  })
  @Test
  public void testDuplicateInstance(int numInstances, NodeState nodeState, boolean mustSucceed) {
    ArrayNode array = Json.mapper().createArrayNode();
    for (int i = 0; i < numInstances; i++) {
      array.add(Json.newObject().put("instanceId", "i-" + i).put("state", "running"));
    }
    when(mockNodeManager.nodeCommand(eq(NodeCommandType.List), any()))
        .thenReturn(ShellResponse.create(0, array.toString()));
    Params params = new Params();
    params.nodeName = "blah";
    params.nodeState = nodeState;
    params.setUniverseUUID(UUID.randomUUID());
    doReturn(params).when(checkDuplicateInstance).taskParams();
    if (mustSucceed) {
      checkDuplicateInstance.run();
    } else {
      assertThrows(RuntimeException.class, () -> checkDuplicateInstance.run());
    }
  }
}
