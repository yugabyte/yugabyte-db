// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.Universe;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

/**
 * Enables or disables cross-cloud federated IAM on an EXISTING universe: fans the per-node
 * configuration ({@code enabled=true}) or teardown ({@code enabled=false}) across every current
 * node, and persists {@code userIntent.federationConfigured}. This is the bridge for when the
 * provider's federation config is set (or cleared) after the universe already exists.
 */
@Slf4j
public class ManageCrossCloudFederationUniverse extends UniverseDefinitionTaskBase {

  @Inject
  protected ManageCrossCloudFederationUniverse(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends UniverseDefinitionTaskParams {
    // true = configure federation on every node; false = tear it down on every node.
    public boolean enabled = true;
  }

  public Params params() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    log.info(
        "Started {} (enabled={}) for universe {}",
        getName(),
        params().enabled,
        taskParams().getUniverseUUID());
    // lockUniverse(-1): reconciles node-side creds only; no metadata mutation, no freeze.
    Universe universe = lockUniverse(-1);
    try {
      boolean enabled = params().enabled;
      for (Cluster cluster : universe.getUniverseDetails().clusters) {
        createConfigureCloudFederationTasks(
            cluster.userIntent, universe.getNodesInCluster(cluster.uuid), enabled);
      }
      // Persist the flag only after every node subtask succeeds; a failed node subtask aborts the
      // task before this runs, so the universe is never left in a partial state.
      createPersistFederationConfiguredTask(enabled);
      createMarkUniverseUpdateSuccessTasks(universe.getUniverseUUID())
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      getRunnableTask().runSubTasks();
    } catch (RuntimeException e) {
      log.error("Error executing task {} with error='{}'.", getName(), e.getMessage(), e);
      throw e;
    } finally {
      unlockUniverseForUpdate(universe.getUniverseUUID());
    }
  }
}
