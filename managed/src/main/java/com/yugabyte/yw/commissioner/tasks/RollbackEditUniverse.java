// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.CanRollback;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.StateTransitionDetails;
import java.util.Objects;
import java.util.Set;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

/**
 * Rolls back a failed {@link EditUniverse} within the safe window by reverting CSP instance tags on
 * nodes that were Live before and remain Live, destroying newly-added nodes (ground-truth),
 * deconfiguring shell-configured masters on existing survivors, clearing orphaned master
 * server_blacklist entries, verifying master membership against Live-before, releasing any
 * lingering capacity reservations, restoring {@code universe_details_json} from {@code
 * state_transition_details}, and regenerating swamper/Prometheus targets for the restored topology.
 *
 * <p>Primary {@code dedicatedNodes} flips are refused via {@link
 * StateTransitionDetails#requireRollbackable()} (Live tserver gflags rewritten before checkpoint).
 *
 * <p>Prechecks run {@link com.yugabyte.yw.commissioner.tasks.subtasks.CheckClusterConsistency} with
 * ADD node names in {@code skipMayBeRunning} so started {@code ToBeAdded} servers do not fail the
 * check. Full {@link #addBasicPrecheckTasks()} (including leaderless-tablet checks) is still
 * skipped. Post-destroy {@code ConfirmEditRollbackMembership} remains the Live-before membership
 * gate before restore.
 */
@Slf4j
@Abortable
@Retryable
@CanRollback(enabled = false)
public class RollbackEditUniverse extends EditUniverseTaskBase {

  private StateTransitionDetails stateTransitionDetails;
  private UniverseDefinitionTaskParams before;
  private UniverseDefinitionTaskParams target;

  @Inject
  protected RollbackEditUniverse(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected UniverseDefinitionTaskParams taskParams() {
    return (UniverseDefinitionTaskParams) taskParams;
  }

  /**
   * Preserve the failed {@link EditUniverse} delta. Re-capturing on freeze would diff the already
   * broken universe against the edit's target params and overwrite ADD markers with nested REPLACE.
   */
  @Override
  protected boolean shouldCaptureStateTransitionDelta() {
    return false;
  }

  /**
   * Authoritative rollback eligibility gate. Runs after the universe is locked (via {@link
   * #lockAndFreezeUniverseForUpdate}) and also for precheck-only submissions. Does not call {@link
   * #addBasicPrecheckTasks()} - those assume a healthy universe and would block rollback exactly
   * when it is needed (leaderless tablets, etc.). Feature flag is enforced by
   * EditUniverseRollbackComputer before submit. Dedicated-nodes flips are refused via {@link
   * StateTransitionDetails#requireRollbackable()}.
   *
   * <p>When {@code rollbackSafe}, also confirms master {@code server_blacklist} is readable so the
   * YBA flag is not trusted alone. Orphaned blacklist from the pre-checkpoint ADD blacklist step is
   * cleared after destroy, before restore.
   *
   * <p>Enqueues {@code CheckClusterConsistency} with names of nodes absent from {@code before}
   * skipped (started ADDs are still {@code ToBeAdded} in YBA but reported by master).
   */
  @Override
  protected void createPrecheckTasks(Universe universe) {
    super.createPrecheckTasks(universe);
    StateTransitionDetails details = universe.getStateTransitionDetails();
    if (details == null) {
      details = new StateTransitionDetails();
    }
    details.requireRollbackable();
    confirmMasterServerBlacklistReadable(universe);
    stateTransitionDetails = details;
    before = details.getBeforeUniverseDetails();
    target = details.getTargetUniverseDetails();
    // Started ADD nodes are excluded from expected IPs (ToBeAdded) but still report as live;
    // skip them so consistency does not block rollback of a mid-provision expand.
    Set<String> skipMaybeRunning =
        collectAddedNodesToDestroy(universe, before).stream()
            .map(NodeDetails::getNodeName)
            .filter(Objects::nonNull)
            .collect(Collectors.toSet());
    verifyClustersConsistency(skipMaybeRunning);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());
    Universe universe = null;
    String errorMessage = null;
    try {
      if (maybeRunOnlyPrechecks()) {
        return;
      }

      // Skip optimistic version check; universe is already in a failed edit state.
      taskParams().expectedUniverseVersion = -1;
      universe =
          lockAndFreezeUniverseForUpdate(
              taskParams().expectedUniverseVersion, null /* Txn callback */);

      Set<NodeDetails> nodesToDestroy = collectAddedNodesToDestroy(universe, before);
      // Revert CSP tags on Live-before AND Live-after survivors before destroy.
      createRevertInstanceTagsTasks(universe, before, target);

      // Destroy ADDs before clearing blacklist so leftover ADD blacklist entries are not needed for
      // placement safety; survivors may still be blacklisted from the pre-checkpoint ADD blacklist.
      log.info(
          "RollbackEditUniverse for {}: destroying {} added node(s)",
          universe.getUniverseUUID(),
          nodesToDestroy.size());
      if (!nodesToDestroy.isEmpty()) {
        createSetNodeStateTasks(nodesToDestroy, NodeDetails.NodeState.Terminating)
            .setSubTaskGroupType(SubTaskGroupType.RemovingUnusedServers);
        createDestroyServerTasks(
                universe,
                nodesToDestroy,
                node -> true /* isForceDelete */,
                true /* deleteNode */,
                true /* deleteRootVolumes */,
                true /* skipDestroyPrecheck */)
            .setSubTaskGroupType(SubTaskGroupType.RemovingUnusedServers);
      }

      // Delete leftover shell master conf on existing Live nodes the edit prepared as masters.
      createDeconfigureShellConfiguredMasterTasks(
          collectExistingNodesConfiguredAsMasters(universe, before, target));

      // Clear orphaned server_blacklist from the pre-checkpoint ADD blacklist step, including
      // destroyed ADD IPs so they can be reused (on-prem DECOMMISSIONED skip is in
      // ModifyBlackList).
      createClearOrphanedServerBlacklistTasks(universe, before, nodesToDestroy);

      // Master should only report Live-before (plus transient destroyed ADD IPs).
      createConfirmBeforeLiveMembershipTasks(before, nodesToDestroy);

      // Force-release before restore: DeleteCapacityReservation reads current details; restore
      // would drop failed-edit CapacityReservationState while cloud reservations may remain.
      if (universe.getUniverseDetails().getCapacityReservationState() != null
          && !universe.getUniverseDetails().getCapacityReservationState().isEmpty()) {
        createDeleteCapacityReservationTask(false /* deleteOnlyIfFullyUtilized */);
      }

      createRestoreUniverseDetailsFromDeltaTask(stateTransitionDetails)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      // Match EditUniverse post-topology cleanup: rewrite Prometheus targets from restored details
      // so scrapes drop destroyed ADD nodes (no-op when metrics management is disabled).
      createSwamperTargetUpdateTask(false /* removeFile */);
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      getRunnableTask().runSubTasks();
    } catch (Throwable t) {
      errorMessage = t.getMessage();
      log.error("Error executing task {} with error='{}'.", getName(), t.getMessage(), t);
      if (universe != null) {
        clearCapacityReservationOnError(
            t, Universe.getOrBadRequest(taskParams().getUniverseUUID()));
      }
      throw t;
    } finally {
      releaseReservedNodes();
      if (universe != null) {
        unlockUniverseForUpdate(taskParams().getUniverseUUID(), errorMessage);
      }
    }
    log.info("Finished {} task.", getName());
  }
}
