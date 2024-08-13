// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.KubernetesUpgradeTaskBase;
import com.yugabyte.yw.commissioner.UpgradeTaskBase.MastersAndTservers;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.commissioner.tasks.subtasks.KubernetesCommandExecutor.CommandType;
import com.yugabyte.yw.common.ScheduleUtil;
import com.yugabyte.yw.common.UniverseInProgressException;
import com.yugabyte.yw.common.config.UniverseConfKeys;
import com.yugabyte.yw.common.gflags.GFlagsUtil;
import com.yugabyte.yw.common.operator.OperatorStatusUpdaterFactory;
import com.yugabyte.yw.forms.BackupRequestParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.Cluster;
import com.yugabyte.yw.forms.backuprestore.BackupScheduleTaskParams;
import com.yugabyte.yw.models.TaskInfo;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.models.helpers.TaskType;
import java.time.Duration;
import java.util.List;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;

@Slf4j
public abstract class BackupScheduleBaseKubernetes extends KubernetesUpgradeTaskBase {

  protected BackupScheduleBaseKubernetes(
      BaseTaskDependencies baseTaskDependencies,
      OperatorStatusUpdaterFactory operatorStatusUpdatorFactory) {
    super(baseTaskDependencies, operatorStatusUpdatorFactory);
  }

  @Override
  protected void createPrecheckTasks(Universe universe) {
    if (taskParams().scheduleParams.enablePointInTimeRestore) {
      addBasicPrecheckTasks();
    }
  }

  @Override
  protected void validateUniverseState(Universe universe) {
    try {
      super.validateUniverseState(universe);
    } catch (UniverseInProgressException e) {
      if (taskParams().scheduleParams.enablePointInTimeRestore) {
        throw e;
      }
    }
  }

  // If PIT restore enabled and previous task was a volume resize, should not run this task
  @Override
  protected boolean checkSafeToRunOnRestriction(
      Universe universe, TaskInfo placementModificationTaskInfo) {
    if (taskParams().scheduleParams.enablePointInTimeRestore
        && placementModificationTaskInfo.getTaskType() == TaskType.EditKubernetesUniverse) {
      UniverseDefinitionTaskParams placementTaskParams =
          Json.fromJson(
              placementModificationTaskInfo.getTaskParams(), UniverseDefinitionTaskParams.class);
      for (Cluster newCluster : placementTaskParams.clusters) {
        Cluster currCluster = universe.getCluster(newCluster.uuid);
        // Cannot run this task if there was a volume change since Statefulset could be deleted
        if (currCluster.userIntent.deviceInfo.volumeSize
            != newCluster.userIntent.deviceInfo.volumeSize) {
          return false;
        }
      }
    }
    return true;
  }

  @Override
  protected MastersAndTservers calculateNodesToBeRestarted() {
    return null;
  }

  @Override
  protected BackupScheduleTaskParams taskParams() {
    return (BackupScheduleTaskParams) taskParams;
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
  public SubTaskGroupType getTaskSubGroupType() {
    return SubTaskGroupType.UpdatingGFlags;
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
      KubernetesGflagsUpgradeCommonParams upgradeParams =
          new KubernetesGflagsUpgradeCommonParams(universe, cluster);
      createSingleKubernetesExecutorTask(
          universe.getName(),
          CommandType.POD_INFO,
          cluster.placementInfo,
          false /*isReadOnlyCluster*/);
      // Helm upgrade to modify gflag secret
      upgradePodsNonRestart(
          universe.getName(),
          upgradeParams.getPlacement(),
          upgradeParams.getMasterAddresses(),
          ServerType.TSERVER,
          upgradeParams.getYbSoftwareVersion(),
          upgradeParams.getUniverseOverrides(),
          upgradeParams.getAzOverrides(),
          upgradeParams.isNewNamingStyle(),
          false /* isReadOnlyCluster */,
          upgradeParams.isEnableYbc(),
          upgradeParams.getYbcSoftwareVersion());

      // In-memory gflag update
      List<NodeDetails> tserverNodes = universe.getTserversInCluster(cluster.uuid);
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
                                    GFlagsUtil.DEFAULT_TIMESTAMP_HISTORY_RETENTION_INTERVAL_SEC))),
                        eventualRetentionTime.getSeconds());
                params.gflags.put(
                    GFlagsUtil.TIMESTAMP_HISTORY_RETENTION_INTERVAL_SEC, Long.toString(retention));
              })
          .setSubTaskGroupType(SubTaskGroupType.UpdatingGFlags);
    }
  }
}
