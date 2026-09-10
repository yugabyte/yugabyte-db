// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.CanRollback;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.TaskExecutor.SubTaskGroup;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor.CommandType;
import com.yugabyte.yw.commissioner.tasks.subtasks.RestoreUniverseDetailsFromDelta;
import com.yugabyte.yw.common.KubernetesUtil;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.StateTransitionDetails;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;

/**
 * Rolls back a failed {@link EditKubernetesUniverse} within the safe window (before the {@code
 * MarkRollbackUnsafe} checkpoint, i.e. before any existing pod was mutated). The failed edit only
 * scaled new pods up via helm; rollback scales the StatefulSets back down to the pre-edit placement
 * - removing exactly the pods the edit added (plus PVC/namespace cleanup for any net-new AZ) - and
 * restores {@code universe_details_json} from {@code state_transition_details}.
 *
 * <p>K8s counterpart of {@link RollbackEditUniverse}. The K8s path is strictly simpler than the VM
 * one: there are no cloud instance tags, capacity reservations, master {@code server_blacklist}, or
 * shell-configured masters to reconcile - pod removal is placement-driven, and tserver blacklist /
 * data migration only run <em>after</em> the checkpoint (unsafe window), so a safe-window rollback
 * never has any to clear.
 *
 * <p>Primary {@code dedicatedNodes} flips are refused via {@link
 * StateTransitionDetails#requireRollbackable()}.
 */
@Slf4j
@Abortable
@Retryable
@CanRollback(enabled = false)
public class RollbackEditKubernetesUniverse extends KubernetesTaskBase {

  @Inject
  protected RollbackEditKubernetesUniverse(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected UniverseDefinitionTaskParams taskParams() {
    return (UniverseDefinitionTaskParams) taskParams;
  }

  /**
   * Preserve the failed {@link EditKubernetesUniverse} delta. Re-capturing on freeze would diff the
   * already-broken universe against the edit's target params and overwrite the ADD markers.
   */
  @Override
  protected boolean shouldCaptureStateTransitionDelta() {
    return false;
  }

  /**
   * Authoritative rollback eligibility gate. Does NOT call {@link #addBasicPrecheckTasks()} - those
   * assume a healthy universe and would block rollback exactly when it is needed. The feature flag
   * is enforced by {@code EditKubernetesUniverseRollbackComputer} before submit.
   */
  @Override
  protected void createPrecheckTasks(Universe universe) {
    StateTransitionDetails details = universe.getStateTransitionDetails();
    if (details == null) {
      details = new StateTransitionDetails();
    }
    details.requireRollbackable();
  }

  @Override
  public void run() {
    log.info("Running {}", getName());
    if (maybeRunOnlyPrechecks()) {
      return;
    }
    Universe universe = null;
    try {
      // Universe is already in a failed-edit state; skip the optimistic version check.
      taskParams().expectedUniverseVersion = -1;
      universe =
          lockAndFreezeUniverseForUpdate(
              taskParams().expectedUniverseVersion,
              u -> setCommunicationPortsForNodes(false) /* Txn callback */);
      taskParams().useNewHelmNamingStyle = universe.getUniverseDetails().useNewHelmNamingStyle;

      StateTransitionDetails details = universe.getStateTransitionDetails();
      if (details == null) {
        throw new PlatformServiceException(
            BAD_REQUEST,
            "Cannot roll back edit Kubernetes universe: state_transition_details is missing. The"
                + " failed edit is not rollbackable; retry the edit to drive it forward instead.");
      }
      details.requireRollbackable();
      UniverseDefinitionTaskParams before = details.getBeforeUniverseDetails();
      UniverseDefinitionTaskParams target = details.getTargetUniverseDetails();
      log.info(
          "RollbackEditKubernetesUniverse for {}: restoring pre-edit topology (primary tservers"
              + " {} -> {}); pods added by the failed edit within the safe window will be removed.",
          universe.getUniverseUUID(),
          target.getPrimaryCluster().userIntent.numNodes,
          before.getPrimaryCluster().userIntent.numNodes);

      // Render every helm upgrade from the pre-edit intent so surviving pods reset to the pre-edit
      // template (overrides / gflags / software version); deletePodsTask reads overrides from
      // taskParams().getPrimaryCluster(). RestoreUniverseDetailsFromDelta re-derives before from
      // the delta independently, so this in-memory swap does not affect the persisted restore.
      taskParams().clusters = before.clusters;

      Cluster beforePrimary = before.getPrimaryCluster();
      Provider provider = Util.getSingleProvider(beforePrimary);
      boolean isMultiAz = PlacementInfoUtil.isMultiAZ(provider);

      PlacementInfo beforePrimaryPI = beforePrimary.placementInfo;
      selectNumMastersAZ(beforePrimaryPI);
      KubernetesPlacement beforePrimaryPlacement =
          new KubernetesPlacement(beforePrimaryPI, false /* isReadOnlyCluster */);
      String masterAddresses =
          KubernetesUtil.computeMasterAddresses(
              beforePrimaryPI,
              beforePrimaryPlacement.masters,
              taskParams().nodePrefix,
              universe.getName(),
              provider,
              universe.getUniverseDetails().communicationPorts.masterRpcPort,
              taskParams().useNewHelmNamingStyle);

      // Delete the pre-edit PDB so scale-down pod deletions are not blocked; recreated
      // post-restore.
      if (taskParams().useNewHelmNamingStyle) {
        createPodDisruptionBudgetPolicyTask(true /* deletePDB */);
      }

      rollbackClusterPods(
          universe,
          before,
          target,
          false /* isReadOnlyCluster */,
          masterAddresses,
          provider,
          isMultiAz);
      if (CollectionUtils.isNotEmpty(before.getReadOnlyClusters())
          && CollectionUtils.isNotEmpty(target.getReadOnlyClusters())) {
        rollbackClusterPods(
            universe,
            before,
            target,
            true /* isReadOnlyCluster */,
            masterAddresses,
            provider,
            isMultiAz);
      }

      // Restore universe_details_json to the pre-edit topology.
      createRestoreUniverseDetailsFromDeltaTask(details);

      // Recreate the pre-edit PDB from the restored topology.
      if (taskParams().useNewHelmNamingStyle) {
        createPodDisruptionBudgetPolicyTask(false /* deletePDB */);
      }

      createSwamperTargetUpdateTask(false /* removeFile */);
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      getRunnableTask().runSubTasks();
      log.info(
          "RollbackEditKubernetesUniverse for {} completed: universe restored to its pre-edit"
              + " configuration. Re-attempt the edit if it is still needed.",
          universe.getUniverseUUID());
    } catch (Throwable t) {
      log.error("Error executing task {} with error='{}'.", getName(), t.getMessage(), t);
      throw t;
    } finally {
      if (universe != null) {
        unlockUniverseForUpdate();
      }
    }
    log.info("Finished {} task.", getName());
  }

  /**
   * Scales one cluster's StatefulSets back down from the failed-edit target placement to the
   * pre-edit placement. {@code currPlacement} is the current (failed target) deployment; {@code
   * newPlacement} is the pre-edit target. {@link #deletePodsTask} reduces in-AZ replica counts
   * (partition-protected, so surviving pods are not rolled) and fully removes any net-new AZ
   * ({@code HELM_DELETE}/{@code VOLUME_DELETE}/{@code NAMESPACE_DELETE}).
   */
  private void rollbackClusterPods(
      Universe universe,
      UniverseDefinitionTaskParams before,
      UniverseDefinitionTaskParams target,
      boolean isReadOnlyCluster,
      String masterAddresses,
      Provider provider,
      boolean isMultiAz) {
    Cluster beforeCluster =
        isReadOnlyCluster ? before.getReadOnlyClusters().get(0) : before.getPrimaryCluster();
    Cluster targetCluster =
        isReadOnlyCluster ? target.getReadOnlyClusters().get(0) : target.getPrimaryCluster();
    PlacementInfo beforePI = beforeCluster.placementInfo;
    PlacementInfo targetPI = targetCluster.placementInfo;
    if (!isReadOnlyCluster) {
      selectNumMastersAZ(beforePI);
      selectNumMastersAZ(targetPI);
    }
    KubernetesPlacement preEditPlacement = new KubernetesPlacement(beforePI, isReadOnlyCluster);
    KubernetesPlacement targetPlacement = new KubernetesPlacement(targetPI, isReadOnlyCluster);

    deletePodsTask(
        universe.getName(),
        targetPlacement /* currPlacement = failed target */,
        masterAddresses,
        preEditPlacement /* newPlacement = pre-edit */,
        false /* instanceTypeChanged */,
        isMultiAz,
        provider,
        isReadOnlyCluster,
        taskParams().useNewHelmNamingStyle,
        universe.isYbcEnabled());

    // Resync YBA's view of the surviving pods.
    createSingleKubernetesExecutorTask(
        universe.getName(), CommandType.POD_INFO, beforePI, isReadOnlyCluster);
  }

  private void createRestoreUniverseDetailsFromDeltaTask(StateTransitionDetails details) {
    SubTaskGroup subTaskGroup =
        createSubTaskGroup("RestoreUniverseDetailsFromDelta", SubTaskGroupType.ConfigureUniverse);
    RestoreUniverseDetailsFromDelta.Params params = new RestoreUniverseDetailsFromDelta.Params();
    params.setUniverseUUID(taskParams().getUniverseUUID());
    params.stateTransitionDetails = details;
    RestoreUniverseDetailsFromDelta task = createTask(RestoreUniverseDetailsFromDelta.class);
    task.initialize(params);
    task.setUserTaskUUID(getUserTaskUUID());
    subTaskGroup.addSubTask(task);
    getRunnableTask().addSubTaskGroup(subTaskGroup);
  }
}
