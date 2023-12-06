// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.models.Hook;
import com.yugabyte.yw.models.HookScope.TriggerType;
import com.yugabyte.yw.models.Users;
import org.junit.Test;

public class RunHooksTest extends NodeTaskBaseTest {

  @Test
  public void testRunHooks() {
    when(mockNodeManager.nodeCommand(any(), any())).thenReturn(ShellResponse.create(0, ""));
    Users defaultUser = ModelFactory.testUser(defaultCustomer);
    Hook hook =
        Hook.create(
            defaultCustomer.getUuid(),
            "test.sh",
            Hook.ExecutionLang.Bash,
            "DEFAULT\nTEXT\n",
            false,
            null);

    RunHooks runHooks = AbstractTaskBase.createTask(RunHooks.class);
    RunHooks.Params params = new RunHooks.Params();
    params.creatingUser = defaultUser;
    params.hook = hook;
    params.trigger = TriggerType.PreNodeProvision;
    params.parentTask = "CreateUniverse";
    params.hookPath = "/tmp/test.sh";
    runHooks.initialize(params);
    runHooks.run();

    verify(mockNodeManager, times(1)).nodeCommand(NodeManager.NodeCommandType.RunHooks, params);
    assertAuditEntry(1, defaultCustomer.getUuid());
  }
}
