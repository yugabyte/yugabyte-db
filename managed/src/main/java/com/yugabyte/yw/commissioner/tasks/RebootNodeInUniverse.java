package com.yugabyte.yw.commissioner.tasks;

import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.NodeActionType;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.ProviderDetails;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.util.Arrays;
import java.util.Collections;
import java.util.HashSet;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import play.mvc.Http;

@Slf4j
@Retryable
public class RebootNodeInUniverse extends UniverseDefinitionTaskBase {

  @JsonDeserialize(converter = RebootNodeInUniverse.Converter.class)
  public static class Params extends NodeTaskParams {
    public boolean isHardReboot = false;
    public boolean skipWaitingForMasterLeader = false;
  }

  public static class Converter
      extends UniverseDefinitionTaskParams.BaseConverter<RebootNodeInUniverse.Params> {}

  @Inject
  protected RebootNodeInUniverse(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void validateParams(boolean isFirstTry) {
    super.validateParams(isFirstTry);
    Universe universe = getUniverse();
    NodeDetails currentNode = universe.getNode(taskParams().nodeName);

    if (currentNode == null) {
      String msg =
          String.format(
              "No node %s is found in universe %s", taskParams().nodeName, universe.getName());
      log.error(msg);
      throw new PlatformServiceException(Http.Status.BAD_REQUEST, msg);
    }
    currentNode.validateActionOnState(
        taskParams().isHardReboot ? NodeActionType.HARD_REBOOT : NodeActionType.REBOOT);

    UUID providerUuid =
        UUID.fromString(universe.getUniverseDetails().getPrimaryCluster().userIntent.provider);
    Provider provider = Provider.getOrBadRequest(providerUuid);
    ProviderDetails providerDetails = provider.getDetails();
    if (provider.getCloudCode() == CloudType.onprem && providerDetails.skipProvisioning == true) {
      throw new PlatformServiceException(
          Http.Status.BAD_REQUEST, "Cannot reboot manually provisioned nodes through YBA");
    }
  }

  @Override
  protected void createPrecheckTasks(Universe universe) {
    addBasicPrecheckTasks();
  }

  @Override
  public void run() {
    boolean isHardReboot = taskParams().isHardReboot;
    boolean skipWaitingForMasterLeader = taskParams().skipWaitingForMasterLeader;
    checkUniverseVersion();
    Universe universe =
        lockAndFreezeUniverseForUpdate(
            taskParams().expectedUniverseVersion, null /* Txn callback */);
    try {
      NodeDetails currentNode = universe.getNode(taskParams().nodeName);
      taskParams().azUuid = currentNode.azUuid;
      taskParams().placementUuid = currentNode.placementUuid;

      if (!instanceExists(taskParams())) {
        String msg = "No instance exists for " + taskParams().nodeName;
        log.error(msg);
        throw new RuntimeException(msg);
      }

      preTaskActions();

      createSetNodeStateTask(
              currentNode, isHardReboot ? NodeState.HardRebooting : NodeState.Rebooting)
          .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);

      // Stop the tserver.
      if (currentNode.isTserver) {
        boolean tserverAlive = isTserverAliveOnNode(currentNode, universe.getMasterAddresses());
        if (tserverAlive) {
          createTServerTaskForNode(currentNode, "stop")
              .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);
        }
      }

      // Stop Yb-controller on this node.
      if (universe.isYbcEnabled()) {
        createStopYbControllerTasks(Collections.singletonList(currentNode))
            .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);
      }

      // Stop the master process on this node.
      if (currentNode.isMaster) {
        boolean masterAlive = isMasterAliveOnNode(currentNode, universe.getMasterAddresses());
        if (masterAlive) {
          createStopMasterTasks(Collections.singleton(currentNode))
              .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);
          if (!skipWaitingForMasterLeader) {
            createWaitForMasterLeaderTask()
                .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);
          }
        }
      }

      // Reboot the node.
      createRebootTasks(Collections.singletonList(currentNode), isHardReboot)
          .setSubTaskGroupType(
              isHardReboot ? SubTaskGroupType.HardRebootingNode : SubTaskGroupType.RebootingNode);

      if (currentNode.isMaster) {
        // Start the master.
        createStartMasterProcessTasks(Collections.singleton(currentNode));

        createWaitForServerReady(currentNode, ServerType.MASTER)
            .setSubTaskGroupType(SubTaskGroupType.StartingMasterProcess);
      }

      // Start the tserver.
      if (currentNode.isTserver) {
        createTServerTaskForNode(currentNode, "start")
            .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);

        // Wait for the tablet server to be responsive.
        createWaitForServersTasks(Collections.singleton(currentNode), ServerType.TSERVER)
            .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);

        createWaitForServerReady(currentNode, ServerType.TSERVER)
            .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);
      }

      if (universe.isYbcEnabled()) {
        createStartYbcTasks(Arrays.asList(currentNode))
            .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);

        // Wait for yb-controller to be responsive on each node.
        createWaitForYbcServerTask(new HashSet<>(Arrays.asList(currentNode)))
            .setSubTaskGroupType(SubTaskGroupType.ConfigureUniverse);
      }

      // Update node state to running.
      createSetNodeStateTask(currentNode, NodeDetails.NodeState.Live)
          .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);

      // Marks the update of this universe as a success only if all the tasks before it
      // succeeded.
      createMarkUniverseUpdateSuccessTasks()
          .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);

      getRunnableTask().runSubTasks();
    } catch (Throwable t) {
      log.error("Error executing task {}, error='{}'", getName(), t.getMessage(), t);
      throw t;
    } finally {
      unlockUniverseForUpdate();
    }
  }
}
