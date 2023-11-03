// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.google.common.collect.Lists;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.List;
import java.util.Set;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;

@Slf4j
@Abortable
@Retryable
public class ReinstallNodeAgent extends UniverseDefinitionTaskBase {

  @Inject
  protected ReinstallNodeAgent(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends NodeTaskParams {
    public Set<String> nodeNames;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    Universe universe = lockUniverseForUpdate(-1, u -> preTaskActions(u));
    try {
      List<NodeDetails> nodeDetailsList =
          filterNodesForInstallNodeAgent(universe, universe.getNodes()).stream()
              .filter(
                  n ->
                      CollectionUtils.isEmpty(taskParams().nodeNames)
                          || taskParams().nodeNames.contains(n.getNodeName()))
              .collect(Collectors.toList());
      if (CollectionUtils.isEmpty(nodeDetailsList)) {
        log.warn("No matching nodes found for universe {}", taskParams().getUniverseUUID());
        return;
      }
      Integer parallelism =
          confGetter.getConfForScope(universe, UniverseConfKeys.nodeAgentReinstallParallelism);
      Lists.partition(nodeDetailsList, parallelism)
          .forEach(
              list -> {
                createInstallNodeAgentTasks(list, true)
                    .setSubTaskGroupType(SubTaskGroupType.InstallingSoftware);
                createWaitForNodeAgentTasks(list)
                    .setSubTaskGroupType(SubTaskGroupType.InstallingSoftware);
              });

      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(SubTaskGroupType.InstallingSoftware);
      getRunnableTask().runSubTasks();
    } finally {
      unlockUniverseForUpdate();
      log.info("Finished {} task.", getName());
    }
  }
}
