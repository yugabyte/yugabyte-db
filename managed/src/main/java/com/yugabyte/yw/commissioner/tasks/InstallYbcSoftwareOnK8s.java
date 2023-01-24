// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.HashSet;
import java.util.Set;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class InstallYbcSoftwareOnK8s extends KubernetesTaskBase {

  @Inject
  protected InstallYbcSoftwareOnK8s(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  protected UniverseDefinitionTaskParams taskParams() {
    return (UniverseDefinitionTaskParams) taskParams;
  }

  @Override
  public void run() {
    try {
      lockUniverse(-1 /* expectedUniverseVersion */);

      Universe universe = Universe.getOrBadRequest(taskParams().universeUUID);

      Set<NodeDetails> allTservers = new HashSet<>();
      Set<NodeDetails> primaryTservers =
          new HashSet<NodeDetails>(universe.getTServersInPrimaryCluster());
      allTservers.addAll(primaryTservers);
      installYbcOnThePods(universe.name, primaryTservers, false, taskParams().ybcSoftwareVersion);

      if (universe.getUniverseDetails().getReadOnlyClusters().size() != 0) {
        Set<NodeDetails> replicaTservers =
            new HashSet<NodeDetails>(
                universe.getNodesInCluster(
                    universe.getUniverseDetails().getReadOnlyClusters().get(0).uuid));
        allTservers.addAll(replicaTservers);
        installYbcOnThePods(universe.name, replicaTservers, true, taskParams().ybcSoftwareVersion);
        performYbcAction(replicaTservers, true, "stop");
      }

      performYbcAction(primaryTservers, false, "stop");
      createWaitForYbcServerTask(allTservers);
      createUpdateYbcTask(taskParams().ybcSoftwareVersion)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
    } catch (Throwable t) {
      log.error("Error executing task {}, error='{}'", getName(), t.getMessage(), t);
      throw t;
    } finally {
      unlockUniverseForUpdate();
    }
    log.info("Finished {} task.", getName());
  }
}
