// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.NodeUniverseContext;
import com.yugabyte.yw.common.ShellProcessContext;
import com.yugabyte.yw.common.ShellResponse;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.List;
import java.util.function.Consumer;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

/** Generic command runner on the DB node. */
@Slf4j
public class RunNodeCommand extends UniverseTaskBase {
  @Inject
  protected RunNodeCommand(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends NodeTaskParams {
    public List<String> command;

    @JsonIgnore public Consumer<ShellResponse> responseConsumer;
    @JsonIgnore public ShellProcessContext shellContext;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    NodeDetails node = universe.getNode(taskParams().nodeName);
    if (node == null) {
      log.warn(
          "Node {} is not found in the universe {}",
          taskParams().nodeName,
          taskParams().getUniverseUUID());
      return;
    }
    ShellResponse response = null;
    if (taskParams().shellContext == null) {
      response =
          nodeUniverseManager.runCommand(
              NodeUniverseContext.of(universe, node), taskParams().command);
    } else {
      response =
          nodeUniverseManager.runCommand(
              NodeUniverseContext.of(universe, node, taskParams().shellContext),
              taskParams().command);
    }
    taskParams().responseConsumer.accept(response);
  }
}
