package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import com.yugabyte.yw.models.helpers.NodeDetails;
import javax.inject.Inject;

public class UpdateNodeDetails extends NodeTaskBase {

  @Inject
  protected UpdateNodeDetails(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends NodeTaskParams {
    public NodeDetails details;
    // Update skipAnsibleTasks config for a VM image pgrade without updating the software.
    public boolean updateCustomImageUsage;
  }

  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String toString() {
    return super.getName() + "(" + taskParams().details + ")";
  }

  @Override
  public void run() {
    try {
      UniverseUpdater updater =
          new UniverseUpdater() {
            public void run(Universe universe) {
              UniverseDefinitionTaskParams universeDetails = universe.getUniverseDetails();
              // FIXME: this is ugly - no equals/hashCode contract in NodeDetails
              NodeDetails old = universe.getNode(taskParams().nodeName);
              if (old == null) {
                return;
              }
              universeDetails.nodeDetailsSet.remove(old);
              universeDetails.nodeDetailsSet.add(taskParams().details);
              universe.setUniverseDetails(universeDetails);
            }
          };
      saveUniverseDetails(updater);
      if (taskParams().updateCustomImageUsage) {
        Universe universe = getUniverse();
        universe.updateConfig(
            ImmutableMap.of(
                Universe.USE_CUSTOM_IMAGE,
                Boolean.toString(
                    getUniverse().getUniverseDetails().nodeDetailsSet.stream()
                        .allMatch(n -> n.ybPrebuiltAmi))));
        universe.save();
      }
    } catch (Exception e) {
      throw new RuntimeException(e);
    }
  }
}
