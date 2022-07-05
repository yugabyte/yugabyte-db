// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.HookInserter;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.HookScope.TriggerType;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.Collection;
import java.util.Collections;
import java.util.Set;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class RunApiTriggeredHooks extends UniverseTaskBase {

  @Inject
  protected RunApiTriggeredHooks(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends UniverseTaskParams {
    public boolean isRolling;
  }

  public Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    log.info("Started {} task", getName());

    try {
      Universe universe = lockUniverseForUpdate(-1); // Check this
      Collection<NodeDetails> nodes = universe.getNodes();

      if (taskParams().isRolling) {
        for (NodeDetails node : nodes) {
          Set<NodeDetails> singletonSet = Collections.singleton(node);
          HookInserter.addHookTrigger(TriggerType.ApiTriggered, this, taskParams(), singletonSet);
        }
      } else {
        HookInserter.addHookTrigger(TriggerType.ApiTriggered, this, taskParams(), nodes);
      }

      getRunnableTask().runSubTasks();
    } catch (Throwable t) {
      log.error("Error executing task {} with error='{}'", getName(), t.getMessage(), t);
      throw t;
    } finally {
      unlockUniverseForUpdate();
    }
    log.info("Finished {} task", getName());
  }
}
