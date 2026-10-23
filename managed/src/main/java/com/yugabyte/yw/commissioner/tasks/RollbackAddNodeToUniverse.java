// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.CanRollback;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import com.yugabyte.yw.models.helpers.StateTransitionDetails;
import java.util.Collections;
import java.util.Objects;
import java.util.Set;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.lang3.StringUtils;

/**
 * Rolls back a failed {@link AddNodeToUniverse} within the safe window by stopping/deconfiguring
 * processes on the re-added node, destroying the VM only when the pre-add state was {@code
 * Decommissioned}, restoring {@code universe_details_json} from {@code state_transition_details},
 * and regenerating swamper targets.
 *
 * <p>Prechecks run {@code CheckClusterConsistency} with the adding node name in {@code
 * skipMayBeRunning}. Full {@link #addBasicPrecheckTasks()} is skipped. Feature flag is enforced by
 * {@code AddNodeToUniverseRollbackComputer} before submit.
 */
@Slf4j
@Abortable
@Retryable
@CanRollback(enabled = false)
public class RollbackAddNodeToUniverse extends UniverseDefinitionTaskBase {

  private StateTransitionDetails stateTransitionDetails;
  private UniverseDefinitionTaskParams before;

  @Inject
  protected RollbackAddNodeToUniverse(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected NodeTaskParams taskParams() {
    return (NodeTaskParams) taskParams;
  }

  /**
   * Preserve the failed {@link AddNodeToUniverse} delta. Re-capturing on freeze would overwrite it.
   */
  @Override
  protected boolean shouldCaptureStateTransitionDelta() {
    return false;
  }

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
    Set<String> skipMaybeRunning =
        taskParams().nodeName == null ? Set.of() : Collections.singleton(taskParams().nodeName);
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

      taskParams().expectedUniverseVersion = -1;
      universe =
          lockAndFreezeUniverseForUpdate(
              taskParams().expectedUniverseVersion, null /* Txn callback */);

      NodeDetails currentNode = universe.getNode(taskParams().nodeName);
      if (currentNode == null) {
        throw new IllegalStateException(
            "Node " + taskParams().nodeName + " not found in universe " + universe.getName());
      }
      NodeDetails beforeNode =
          before.nodeDetailsSet == null
              ? null
              : before.nodeDetailsSet.stream()
                  .filter(n -> Objects.equals(taskParams().nodeName, n.getNodeName()))
                  .findFirst()
                  .orElse(null);
      if (beforeNode == null) {
        throw new IllegalStateException(
            "Node " + taskParams().nodeName + " not found in before universe details");
      }
      boolean wasDecommissioned = beforeNode.state == NodeState.Decommissioned;
      Set<NodeDetails> nodeSet = Collections.singleton(currentNode);

      // Process cleanup: stop with deconfigure so leftover conf cannot be started by hand.
      if (universe.isYbcEnabled()) {
        createStopYbControllerTasks(nodeSet, true /* isIgnoreError */)
            .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);
      }
      createStopServerTasks(
              nodeSet,
              ServerType.TSERVER,
              params -> {
                params.isIgnoreError = true;
                params.deconfigure = true;
              })
          .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);
      createStopServerTasks(
              nodeSet,
              ServerType.MASTER,
              params -> {
                params.isIgnoreError = true;
                params.deconfigure = true;
              })
          .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);

      if (wasDecommissioned) {
        log.info(
            "RollbackAddNodeToUniverse for {}: destroying recreated instance for decommissioned"
                + " node {}",
            universe.getUniverseUUID(),
            taskParams().nodeName);
        createSetNodeStateTask(currentNode, NodeState.Terminating)
            .setSubTaskGroupType(SubTaskGroupType.ReleasingInstance);
        createDestroyServerTasks(
                universe,
                nodeSet,
                node -> true /* isForceDelete */,
                false /* deleteNode */,
                true /* deleteRootVolumes */,
                true /* skipDestroyPrecheck */)
            .setSubTaskGroupType(SubTaskGroupType.ReleasingInstance);
        // Match Release: clear blacklist for the IP that Add may have registered.
        if (currentNode.cloudInfo != null
            && StringUtils.isNotBlank(currentNode.cloudInfo.private_ip)) {
          createModifyBlackListTask(
                  null /* addNodes */, nodeSet /* removeNodes */, false /* isLeaderBlacklist */)
              .setSubTaskGroupType(SubTaskGroupType.ReleasingInstance);
        }
      } else {
        log.info(
            "RollbackAddNodeToUniverse for {}: keeping instance for removed node {} (stop +"
                + " deconfigure only)",
            universe.getUniverseUUID(),
            taskParams().nodeName);
      }

      createRestoreUniverseDetailsFromDeltaTask(stateTransitionDetails)
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      createSwamperTargetUpdateTask(false /* removeFile */);
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      getRunnableTask().runSubTasks();
    } catch (Throwable t) {
      errorMessage = t.getMessage();
      log.error("Error executing task {} with error='{}'.", getName(), t.getMessage(), t);
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
