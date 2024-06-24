// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.MasterState;
import java.util.HashSet;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Retryable
public class SyncMasterAddresses extends UniverseDefinitionTaskBase {

  private final Set<String> nodesToStop = ConcurrentHashMap.newKeySet();

  @Inject
  protected SyncMasterAddresses(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  protected void createPrecheckTasks(Universe universe) {
    super.createPrecheckTasks(universe);
    if (isFirstTry()) {
      Set<NodeDetails> masterNodes = new HashSet<>(universe.getMasters());
      Set<NodeDetails> nonMasterNodes =
          universe.getNodes().stream()
              .filter(n -> !masterNodes.contains(n))
              .filter(n -> n.autoSyncMasterAddrs)
              .collect(Collectors.toSet());
      createCheckProcessStateTask(
              universe,
              nonMasterNodes,
              ServerType.MASTER,
              false /* ensureRunning */,
              n -> nodesToStop.add(n.nodeName))
          .setSubTaskGroupType(SubTaskGroupType.ValidateConfigurations);
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
              })
          .setSubTaskGroupType(SubTaskGroupType.StoppingNodeProcesses);
      createMasterAddressUpdateTask(universe, universe.getMasters(), universe.getTServers());
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
