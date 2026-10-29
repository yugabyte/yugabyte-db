// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.CanRollback;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor.CommandType;
import com.yugabyte.yw.common.KubernetesUtil;
import com.yugabyte.yw.common.PlacementInfoUtil;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.PlacementInfo;
import com.yugabyte.yw.models.helpers.StateTransitionDetails;
import java.util.Set;
import java.util.stream.Collectors;
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
 * <p>K8s counterpart of {@link RollbackEditUniverse}. The K8s path is simpler than the VM one:
 * there are no cloud instance tags, capacity reservations, or shell-configured masters to reconcile
 * - pod removal is placement-driven. Unlike the VM edit, the K8s edit does not blacklist new
 * tservers before starting them, so in an existing AZ the load balancer can already have moved
 * data onto them within the safe window. The rollback therefore drains them (blacklist, wait for
 * data move, safe-to-delete check) before removing them, and clears their blacklist entries after,
 * since later edits reuse the pod names. Like the edit's own removal path, the drain needs the
 * cluster's surviving tservers to be up, so the rollback first waits for them with the usual
 * bounded timeout. If one is down, the rollback fails there, before that cluster's tservers are
 * blacklisted, and can be retried once it is back. Clusters are rolled back one after the other,
 * so a failure in the read replica comes after the primary was already rolled back. If the rollback
 * fails after the blacklist was set (for example in the data move or the safe-to-delete check), the
 * entries stay until a retry, which sets and clears them again. This task is a placement
 * modification task, so its failed run stays the universe's {@code placementModificationTaskUuid}
 * (unlock only clears it on success). Until a retry succeeds, only that retry and the {@code
 * SAFE_TO_RUN_IF_UNIVERSE_BROKEN} tasks can run, and none of them create pods, so no later edit
 * can reuse these pod names while the entries are set.
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
      // A read-replica-only edit checkpointed by an older build has no primary in its target.
      addPrimaryClusterIfMissing(target, before);
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
      selectNumMastersAZ(beforePrimaryPI, beforePrimary);
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
      selectNumMastersAZ(beforePI, beforeCluster);
      selectNumMastersAZ(targetPI, targetCluster);
    }
    KubernetesPlacement preEditPlacement = new KubernetesPlacement(beforePI, isReadOnlyCluster);
    KubernetesPlacement targetPlacement = new KubernetesPlacement(targetPI, isReadOnlyCluster);

    // The tservers the failed edit added, as recorded by its POD_INFO. Move their data off first,
    // as the edit's own removal path does.
    Set<NodeDetails> addedTservers =
        getPodsToRemove(
                preEditPlacement,
                targetPlacement,
                ServerType.TSERVER,
                universe,
                isMultiAz,
                isReadOnlyCluster)
            .values()
            .stream()
            .flatMap(Set::stream)
            .collect(Collectors.toSet());
    if (!addedTservers.isEmpty()) {
      // The data move needs the surviving tservers. If one is down it may never finish, so fail
      // here, with a bounded wait and before this cluster's tservers are blacklisted, rather than
      // hang in WaitForDataMove. The rollback can be retried once they are back.
      Set<NodeDetails> survivingTservers =
          universe.getNodesInCluster(targetCluster.uuid).stream()
              .filter(node -> node.isTserver && !addedTservers.contains(node))
              .collect(Collectors.toSet());
      createWaitForServersTasks(survivingTservers, ServerType.TSERVER)
          .setSubTaskGroupType(SubTaskGroupType.ValidateConfigurations);
      createModifyBlackListTask(addedTservers, null /* removeNodes */, false /* isLeaderBlacklist */)
          .setSubTaskGroupType(SubTaskGroupType.WaitForDataMigration);
      createWaitForDataMoveTask().setSubTaskGroupType(SubTaskGroupType.WaitForDataMigration);
      createCheckNodeSafeToDeleteTasks(universe, addedTservers);
    }

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

    if (!addedTservers.isEmpty()) {
      // Clear the entries so a later edit can reuse these pod names.
      createClearTserverBlacklistTasks(universe, addedTservers);
    }

    // Resync YBA's view of the surviving pods.
    createSingleKubernetesExecutorTask(
        universe.getName(), CommandType.POD_INFO, beforePI, isReadOnlyCluster);
  }
}
