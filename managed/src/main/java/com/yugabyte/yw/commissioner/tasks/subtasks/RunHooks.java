// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.fasterxml.jackson.databind.node.JsonNodeFactory;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.NodeManager;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Hook;
import com.yugabyte.yw.models.HookScope.TriggerType;
import java.io.File;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.io.FileUtils;

@Slf4j
public class RunHooks extends NodeTaskBase {

  @Inject
  protected RunHooks(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends NodeTaskParams {
    // The hook to run
    public Hook hook;

    // The path to the hook that should be run
    public String hookPath;

    // The event that triggered this hook run
    public TriggerType trigger;

    // Parent class name
    public String parentTask;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    log.info("Running hook {} on node {}", taskParams().hook.getName(), taskParams().nodeName);

    // Create the hook script to run
    Hook hook = taskParams().hook;
    File hookFile = new File(taskParams().hookPath);
    try {
      FileUtils.write(hookFile, hook.getHookText(), StandardCharsets.UTF_8);
    } catch (IOException e) {
      throw new RuntimeException(
          "Error creating hook file " + taskParams().hookPath + " for hook " + hook.getUuid());
    }

    ShellResponse response =
        getNodeManager().nodeCommand(NodeManager.NodeCommandType.RunHooks, taskParams());

    // Audit with the return code and error if present
    ObjectNode body = JsonNodeFactory.instance.objectNode();
    body.put("returnCode", response.code);
    if (response.message != null && response.message.length() != 0)
      body.put("error", response.message);
    Audit.create(
        taskParams().creatingUser,
        "/run/custom_hook/" + taskParams().trigger.name(),
        "DUMMY",
        Audit.TargetType.Hook,
        hook.getUuid().toString(),
        Audit.ActionType.RunHook,
        body,
        getUserTaskUUID(),
        null,
        null);

    if (!hookFile.delete()) log.warn("Failed to delete hook file " + taskParams().hookPath);
    response.processErrors();
  }
}
