// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.YbcUpgrade;
import com.yugabyte.yw.commissioner.tasks.KubernetesTaskBase;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.time.Duration;
import java.util.HashSet;
import java.util.Set;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

@Slf4j
@Abortable
@Retryable
public class InstallYbcSoftwareOnK8s extends KubernetesTaskBase {

  private final YbcUpgrade ybcUpgrade;

  @Inject
  protected InstallYbcSoftwareOnK8s(
      BaseTaskDependencies baseTaskDependencies, YbcUpgrade ybcUpgrade) {
    super(baseTaskDependencies);
    this.ybcUpgrade = ybcUpgrade;
  }

  protected UniverseDefinitionTaskParams taskParams() {
    return (UniverseDefinitionTaskParams) taskParams;
  }

  @Override
  public void run() {
    try {
      // The background YbcUpgrade scheduler copies and restarts YBC on these same pods. Let an
      // upgrade it already started finish first, so the two do not bounce YBC underneath each other
      // and fail this task's ping checks (PLAT-21957).
      waitForScheduledYbcUpgradeOnK8s(taskParams().getUniverseUUID());

      Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
      Set<NodeDetails> allTservers = new HashSet<>();
      Set<NodeDetails> primaryTservers =
          new HashSet<NodeDetails>(universe.getTServersInPrimaryCluster());
      Set<NodeDetails> replicaTservers = new HashSet<>();
      allTservers.addAll(primaryTservers);

      // Read after the wait: the scheduler stamps the version once it converges, so a universe that
      // was stale when this subtask was queued may already be up to date.
      boolean ybcUpToDate =
          StringUtils.equals(
              universe.getUniverseDetails().getYbcSoftwareVersion(),
              taskParams().getYbcSoftwareVersion());
      if (ybcUpToDate) {
        log.info(
            "Skipping YBC install on universe {} as it already runs version {}",
            taskParams().getUniverseUUID(),
            taskParams().getYbcSoftwareVersion());
      }

      if (!ybcUpToDate
          && !universe.getUniverseDetails().getPrimaryCluster().userIntent.isUseYbdbInbuiltYbc()) {
        /* This calls kubectl cp, it is idempotent */
        installYbcOnThePods(
            primaryTservers,
            false,
            taskParams().getYbcSoftwareVersion(),
            universe.getUniverseDetails().getPrimaryCluster().userIntent.ybcFlags);
        performYbcAction(primaryTservers, false, "stop");

        if (universe.getUniverseDetails().getReadOnlyClusters().size() != 0) {
          replicaTservers =
              new HashSet<NodeDetails>(
                  universe.getNodesInCluster(
                      universe.getUniverseDetails().getReadOnlyClusters().get(0).uuid));
          installYbcOnThePods(
              replicaTservers,
              true,
              taskParams().getYbcSoftwareVersion(),
              universe.getUniverseDetails().getReadOnlyClusters().get(0).userIntent.ybcFlags);
          performYbcAction(replicaTservers, true, "stop");
        }
      }
      allTservers.addAll(replicaTservers);

      createWaitForYbcServerTask(allTservers);
      createUpdateYbcTask(taskParams().getYbcSoftwareVersion())
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      // Marks update of this universe as a success only if all the tasks before it succeeded.
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
    } catch (Throwable t) {
      log.error("Error executing task {}, error='{}'", getName(), t.getMessage(), t);
      throw t;
    }
    log.info("Finished {} task.", getName());
  }

  /**
   * Waits while the background {@link YbcUpgrade} scheduler has an upgrade in flight for this
   * universe. Unlike the VM path this never claims the in-flight flag: {@link #run()} only queues
   * the copy/restart subtask groups, so the work outlives this method and a release could not be
   * bracketed around it. Claiming is unnecessary anyway: this subtask always runs under a frozen
   * universe lock, and {@link YbcUpgrade#canUpgradeYBCOnK8s} refuses any locked universe.
   *
   * <p>Returns without failing once the budget is spent, matching the VM path. A redundant copy is
   * recoverable by the WaitForYbcServer subtask queued afterwards, whereas failing here would abort
   * the backup this subtask exists to serve.
   */
  private void waitForScheduledYbcUpgradeOnK8s(UUID universeUUID) {
    int maxRetries = ybcUpgrade.getMaxYBCUpgradePollResultTries();
    for (int numRetries = 0; numRetries < maxRetries; numRetries++) {
      if (!ybcUpgrade.checkYBCUpgradeProcessExistsOnK8s(universeUUID)) {
        return;
      }
      log.info(
          "Waiting for an in-flight YBC upgrade on universe {} to finish ({}/{})",
          universeUUID,
          numRetries + 1,
          maxRetries);
      waitFor(Duration.ofMillis(ybcUpgrade.getYBCUpgradePollResultSleepMs()));
    }
    log.warn(
        "Proceeding with YBC install on universe {} while a scheduled YBC upgrade is still in"
            + " flight.",
        universeUUID);
  }
}
