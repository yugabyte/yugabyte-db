// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UpgradeTaskBase;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.ScheduleUtil;
import com.yugabyte.yw.common.UniverseInProgressException;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.backuprestore.BackupScheduleTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.NodeDetails.NodeState;
import java.time.Duration;
import java.util.Collections;
import java.util.List;
import lombok.extern.slf4j.Slf4j;
import org.apache.commons.collections4.CollectionUtils;

@Slf4j
public abstract class BackupScheduleBase extends UpgradeTaskBase {

  protected BackupScheduleBase(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Override
  public void validateParams(boolean isFirstTry) {
    super.validateParams(isFirstTry);
    Universe universe = getUniverse();
    taskParams().verifyParams(universe, isFirstTry);
    if (taskParams().scheduleParams.enablePointInTimeRestore
        && !universe.verifyTserverRunningOnNodes()) {
      throw new RuntimeException("All nodes not in running state, cannot perform task");
    }
  }

  @Override
  protected void validateUniverseState(Universe universe) {
    try {
      super.validateUniverseState(universe);
    } catch (UniverseInProgressException e) {
      if (taskParams().scheduleParams.enablePointInTimeRestore) {
        log.error(e.getMessage());
        throw e;
      }
    }
  }

  @Override
  protected BackupScheduleTaskParams taskParams() {
    return (BackupScheduleTaskParams) taskParams;
  }

  @Override
  protected void createPrecheckTasks(Universe universe) {
    if (taskParams().scheduleParams.enablePointInTimeRestore) {
      addBasicPrecheckTasks();
    }
  }

  @Override
  protected MastersAndTservers calculateNodesToBeRestarted() {
    return null;
  }

  @Override
  public SubTaskGroupType getTaskSubGroupType() {
    return SubTaskGroupType.UpdatingGFlags;
  }

  @Override
  public NodeState getNodeState() {
    return NodeState.Live;
  }

  protected Runnable getBackupScheduleUniverseSubtasks(
      Universe universe, BackupRequestParams scheduledBackupParams, boolean isDelete) {
    return () -> {
      // Run subtasks to configure history retention if required
      maybeConfigureHistoryRetention(scheduledBackupParams, universe, isDelete);
    };
  }

  // Configure timstamp_history_retention_sec gflag on the universe to accommodate
  // the backup interval
  private void maybeConfigureHistoryRetention(
      BackupRequestParams scheduledBackupParams, Universe universe, boolean isDelete) {
    Duration bufferHistoryRetention =
        Duration.ofSeconds(
            confGetter.getConfForScope(
                universe, UniverseConfKeys.pitEnabledBackupsRetentionBufferTimeSecs));
    Duration eventualRetentionTime =
        ScheduleUtil.getFinalHistoryRetentionUniverseForPITRestore(
            universe.getUniverseUUID(), scheduledBackupParams, isDelete, bufferHistoryRetention);

    if (eventualRetentionTime.isZero()) {
      log.info("No change in timestamp_history_retention_sec gflag required!");
      return;
    }

    List<Cluster> clusters = universe.getUniverseDetails().clusters;
    for (Cluster cluster : clusters) {
      // Filter out nodes to apply the gflag on.
      List<NodeDetails> tserverNodes = universe.getTserversInCluster(cluster.uuid);
      if (CollectionUtils.isNotEmpty(tserverNodes)) {
        // Add subtasks to change gflag in non-restart manner.
        createServerConfFileUpdateTasks(
            cluster.userIntent,
            tserverNodes,
            Collections.singleton(ServerType.TSERVER),
            cluster,
            clusters,
            cluster,
            clusters);
        createSetFlagInMemoryTasks(
                tserverNodes,
                ServerType.TSERVER,
                (node, params) -> {
                  params.force = true;
                  params.gflags =
                      GFlagsUtil.getGFlagsForNode(node, ServerType.TSERVER, cluster, clusters);
                  // Final retention is max of user set value and current max frequency.
                  long retention =
                      Math.max(
                          Long.parseLong(
                              params.gflags.getOrDefault(
                                  GFlagsUtil.TIMESTAMP_HISTORY_RETENTION_INTERVAL_SEC,
                                  Long.toString(
                                      GFlagsUtil
                                          .DEFAULT_TIMESTAMP_HISTORY_RETENTION_INTERVAL_SEC))),
                          eventualRetentionTime.getSeconds());
                  params.gflags.put(
                      GFlagsUtil.TIMESTAMP_HISTORY_RETENTION_INTERVAL_SEC,
                      Long.toString(retention));
                })
            .setSubTaskGroupType(SubTaskGroupType.UpdatingGFlags);
      }
    }
  }
}
