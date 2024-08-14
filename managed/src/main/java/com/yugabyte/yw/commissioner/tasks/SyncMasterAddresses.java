// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.MasterState;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Retryable
public class SyncMasterAddresses extends UniverseDefinitionTaskBase {

  private final Set<String> nodesToStop = ConcurrentHashMap.newKeySet();

  private boolean isLocalProvider = false;

  @Inject
  protected SyncMasterAddresses(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected void createPrecheckTasks(Universe universe) {
    super.createPrecheckTasks(universe);
    if (isFirstTry()) {
      Set<NodeDetails> nonMasterNodes =
          universe.getNodes().stream()
              .filter(n -> !n.isMaster && n.cloudInfo.private_ip != null)
              .filter(n -> n.autoSyncMasterAddrs)
              .collect(Collectors.toSet());
      Cluster primaryCluster = universe.getUniverseDetails().getPrimaryCluster();
      if (primaryCluster.userIntent.providerType == CloudType.local) {
        isLocalProvider = true;
        nonMasterNodes.stream().map(NodeDetails::getNodeName).forEach(n -> nodesToStop.add(n));
      } else {
        // Disable this on local provider as the check cannot be done properly as there are multiple
        // master processes.
        createCheckProcessStateTask(
                universe,
                nonMasterNodes,
                ServerType.MASTER,
                false /* ensureRunning */,
                n -> nodesToStop.add(n.nodeName))
            .setSubTaskGroupType(SubTaskGroupType.ValidateConfigurations);
      }
    }
  }

  private void freezeUniverseInTxn(Universe universe) {
    nodesToStop.stream().forEach(n -> universe.getNode(n).masterState = MasterState.ToStop);
  }

  @Override
  public void run() {
    log.info("SyncMasterAddresses on universe uuid={}", taskParams().getUniverseUUID());
    checkUniverseVersion();
    Universe universe =
        lockAndFreezeUniverseForUpdate(
            taskParams().expectedUniverseVersion, this::freezeUniverseInTxn);
    try {
      Set<NodeDetails> nonMasterActiveNodes =
          universe.getNodes().stream()
              .filter(n -> n.masterState == MasterState.ToStop)
              .collect(Collectors.toSet());
      createStopServerTasks(
              nonMasterActiveNodes,
              ServerType.MASTER,
              params -> {
                params.deconfigure = true;
                params.isIgnoreError = isLocalProvider;
              })
          .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);
      createMasterAddressUpdateTask(
          universe, universe.getMasters(), universe.getTServers(), false /* ignore error */);
      createUpdateUniverseFieldsTask(u -> u.getNodes().forEach(n -> n.autoSyncMasterAddrs = false));
      createMarkUniverseUpdateSuccessTasks(universe.getUniverseUUID());
      getRunnableTask().runSubTasks();
    } catch (Throwable t) {
      log.error("Error executing task {}, error='{}'", getName(), t.getMessage(), t);
      throw t;
    } finally {
      unlockUniverseForUpdate();
    }
  }
}
