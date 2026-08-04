// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.UpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.ProvisionUniverseNodesParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.UpgradeTaskParams.UpgradeOption;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.util.Collections;
import java.util.HashSet;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Abortable
@Retryable
public class ProvisionUniverseNodes extends UpgradeTaskBase {

  @Inject
  protected ProvisionUniverseNodes(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected ProvisionUniverseNodesParams taskParams() {
    return (ProvisionUniverseNodesParams) taskParams;
  }

  @Override
  public SubTaskGroupType getTaskSubGroupType() {
    return SubTaskGroupType.Provisioning;
  }

  @Override
  public NodeState getNodeState() {
    return NodeState.Reprovisioning;
  }

  @Override
  public void validateParams(boolean isFirstTry) {
    super.validateParams(isFirstTry);
    if (isFirstTry) {
      taskParams().verifyParams(getUniverse(), getNodeState(), isFirstTry);
      Universe universe = getUniverse();
      for (Cluster cluster : universe.getUniverseDetails().clusters) {
        if (cluster.userIntent.providerType == CloudType.onprem) {
          Provider provider =
              Provider.getOrBadRequest(UUID.fromString(cluster.userIntent.provider));
          if (provider.getDetails().skipProvisioning) {
            throw new PlatformServiceException(
                BAD_REQUEST,
                "ProvisionUniverseNodes is not supported for on-prem universes with"
                    + " manual provisioning (skip_provisioning enabled).");
          }
        }
      }
    }
  }

  @Override
  protected MastersAndTservers calculateNodesToBeRestarted() {
    return fetchNodes(UpgradeOption.ROLLING_UPGRADE);
  }

  @Override
  protected void createPrecheckTasks(Universe universe) {
    super.createPrecheckTasks(universe);
    addBasicPrecheckTasks();
  }

  @Override
  public void run() {
    runUpgrade(
        () -> {
          Universe universe = getUniverse();
          MastersAndTservers nodes = getNodesToBeRestarted();
          Set<NodeDetails> nodeSet = toOrderedSet(nodes.asPair());

          taskParams().clusters = universe.getUniverseDetails().clusters;

          for (NodeDetails node : nodeSet) {
            Set<ServerType> processTypes = new LinkedHashSet<>();
            if (node.isMaster) {
              processTypes.add(ServerType.MASTER);
            }
            if (node.isTserver) {
              processTypes.add(ServerType.TSERVER);
            }
            if (universe.isYbcEnabled()) {
              processTypes.add(ServerType.CONTROLLER);
            }

            List<NodeDetails> singleNode = Collections.singletonList(node);

            createSetNodeStateTasks(singleNode, getNodeState())
                .setSubTaskGroupType(getTaskSubGroupType());

            createCheckNodesAreSafeToTakeDownTask(
                Collections.singletonList(MastersAndTservers.from(node, processTypes)),
                getTargetSoftwareVersion(),
                false);

            for (ServerType processType : processTypes) {
              createServerControlTask(node, processType, "stop")
                  .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);
            }

            createSetupYNPTask(universe, singleNode)
                .setSubTaskGroupType(SubTaskGroupType.Provisioning);
            createYNPProvisioningTask(universe, singleNode, false)
                .setSubTaskGroupType(SubTaskGroupType.Provisioning);
            createInstallNodeAgentTasks(universe, singleNode)
                .setSubTaskGroupType(SubTaskGroupType.InstallingSoftware);
            createWaitForNodeAgentTasks(singleNode)
                .setSubTaskGroupType(SubTaskGroupType.InstallingSoftware);

            for (ServerType processType : processTypes) {
              if (processType.equals(ServerType.CONTROLLER)) {
                createStartYbcTasks(singleNode)
                    .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);
                createWaitForYbcServerTask(new HashSet<>(singleNode))
                    .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);
              } else {
                createServerControlTask(node, processType, "start")
                    .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);
                createWaitForServersTasks(singleNode, processType)
                    .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);
                if (processType.equals(ServerType.TSERVER) && node.isYsqlServer) {
                  createWaitForServersTasks(singleNode, ServerType.YSQLSERVER)
                      .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);
                }
                createWaitForServerReady(node, processType)
                    .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);
              }
            }

            createWaitForKeyInMemoryTasks(singleNode)
                .setSubTaskGroupType(SubTaskGroupType.StartingNodeProcesses);

            createSetNodeStateTasks(singleNode, NodeState.Live)
                .setSubTaskGroupType(getTaskSubGroupType());
          }

          createUpdateUniverseFieldsTask(u -> u.getUniverseDetails().installNodeAgent = false)
              .setSubTaskGroupType(SubTaskGroupType.InstallingSoftware);
        });
  }
}
