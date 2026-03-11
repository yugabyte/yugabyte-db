// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.PrevYBSoftwareConfig;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.Universe.UniverseUpdater;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

/**
 * Persists software upgrade progress to the universe (masters completion, per-AZ tserver progress
 * in {@code prevYBSoftwareConfig}, and {@code softwareUpgradeState = Paused}). The caller marks
 * this subtask group with {@code SubTaskGroup.setPausedAfter(true)} so TaskExecutor pauses cleanly
 * after this group finishes. Used at pause points during canary-style upgrades (e.g. after masters,
 * after an AZ's tservers). On resume, the upgrade task loads this state and skips completed work.
 */
@Slf4j
public class SaveSoftwareUpgradeProgress extends UniverseTaskBase {

  @Inject
  protected SaveSoftwareUpgradeProgress(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends UniverseTaskParams {
    /** True if masters phase completed before this pause. */
    public boolean mastersUpgradeCompleted = false;

    /** Primary cluster AZ UUIDs completed so far (in order). */
    public List<UUID> primaryClusterAZsCompleted = new ArrayList<>();

    /** Read replica cluster UUID -> list of completed AZ UUIDs. */
    public Map<UUID, List<UUID>> readReplicaClusterAZsCompleted = new HashMap<>();
  }

  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return super.getName() + "(" + taskParams().getUniverseUUID() + ")";
  }

  @Override
  public void run() {
    UniverseUpdater updater =
        universe -> {
          UniverseDefinitionTaskParams details = universe.getUniverseDetails();
          if (!details.updateInProgress) {
            throw new RuntimeException(
                "Universe " + taskParams().getUniverseUUID() + " is not being updated.");
          }
          details.softwareUpgradeState = UniverseDefinitionTaskParams.SoftwareUpgradeState.Paused;
          PrevYBSoftwareConfig prev = details.prevYBSoftwareConfig;
          if (prev == null) {
            prev = new PrevYBSoftwareConfig();
            details.prevYBSoftwareConfig = prev;
          }
          prev.setMastersUpgradeCompleted(taskParams().mastersUpgradeCompleted);
          prev.setPrimaryClusterAZsCompleted(
              taskParams().primaryClusterAZsCompleted != null
                  ? new ArrayList<>(taskParams().primaryClusterAZsCompleted)
                  : new ArrayList<>());
          prev.setReadReplicaClusterAZsCompleted(
              taskParams().readReplicaClusterAZsCompleted != null
                  ? new HashMap<>(taskParams().readReplicaClusterAZsCompleted)
                  : new HashMap<>());
          universe.setUniverseDetails(details);
        };
    saveUniverseDetails(updater);
    log.info("Software upgrade progress saved for universe {}.", taskParams().getUniverseUUID());
  }
}
