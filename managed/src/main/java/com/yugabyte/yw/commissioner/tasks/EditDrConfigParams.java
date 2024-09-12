// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.google.inject.Inject;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UserTaskDetails;
import com.yugabyte.yw.common.DrConfigStates.State;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.forms.DrConfigTaskParams;
import com.yugabyte.yw.models.DrConfig;
import com.yugabyte.yw.models.PitrConfig;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterConfig.XClusterConfigStatusType;
import java.util.List;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class EditDrConfigParams extends XClusterConfigTaskBase {

  @Inject
  protected EditDrConfigParams(
      BaseTaskDependencies baseTaskDependencies, XClusterUniverseService xClusterUniverseService) {
    super(baseTaskDependencies, xClusterUniverseService);
  }

  @Override
  protected DrConfigTaskParams taskParams() {
    return (DrConfigTaskParams) taskParams;
  }

  @Override
  public String getName() {
    return String.format(
        "%s(uuid=%s,universe=%s)",
        this.getClass().getSimpleName(),
        taskParams().getDrConfig().getUuid(),
        taskParams().getUniverseUUID());
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    DrConfig drConfig = getDrConfigFromTaskParams();
    XClusterConfig xClusterConfig = drConfig.getActiveXClusterConfig();
    Universe sourceUniverse = Universe.getOrBadRequest(xClusterConfig.getSourceUniverseUUID());
    Universe targetUniverse = Universe.getOrBadRequest(xClusterConfig.getTargetUniverseUUID());

    try {
      // Lock the source universe.
      lockAndFreezeUniverseForUpdate(
          sourceUniverse.getUniverseUUID(), sourceUniverse.getVersion(), null /* Txn callback */);
      try {
        // Lock the target universe.
        lockAndFreezeUniverseForUpdate(
            targetUniverse.getUniverseUUID(), targetUniverse.getVersion(), null /* Txn callback */);
        {
          try {

            createXClusterConfigSetStatusTask(xClusterConfig, XClusterConfigStatusType.Updating);

            if (taskParams().getPitrParams() != null) {
              createUpdateDrConfigParamsTask(
                  drConfig.getUuid(), null /* bootstrapParams */, taskParams().getPitrParams());

              long pitrRetentionPeriodSec = taskParams().getPitrParams().retentionPeriodSec;
              long pitrSnapshotIntervalSec = taskParams().getPitrParams().snapshotIntervalSec;

              List<PitrConfig> pitrConfigs = xClusterConfig.getPitrConfigs();
              pitrConfigs.forEach(
                  pitrConfig -> {
                    createDeletePitrConfigTask(
                        pitrConfig.getUuid(),
                        targetUniverse.getUniverseUUID(),
                        false /* ignoreErrors */);
                    createCreatePitrConfigTask(
                        targetUniverse,
                        pitrConfig.getDbName(),
                        pitrConfig.getTableType(),
                        pitrRetentionPeriodSec,
                        pitrSnapshotIntervalSec,
                        xClusterConfig,
                        true /* createdForDr */);
                  });
            }

            if (taskParams().getBootstrapParams() != null) {
              createUpdateDrConfigParamsTask(
                  drConfig.getUuid(), taskParams().getBootstrapParams(), null /* pitrParams */);
            }

            createSetDrStatesTask(
                    xClusterConfig,
                    State.Replicating,
                    null /* sourceUniverseState */,
                    null /* targetUniverseState */,
                    null /* keyspacePending */)
                .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

            createXClusterConfigSetStatusTask(xClusterConfig, XClusterConfigStatusType.Running)
                .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

            createMarkUniverseUpdateSuccessTasks(targetUniverse.getUniverseUUID())
                .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

            createMarkUniverseUpdateSuccessTasks(sourceUniverse.getUniverseUUID())
                .setSubTaskGroupType(UserTaskDetails.SubTaskGroupType.ConfigureUniverse);

            getRunnableTask().runSubTasks();

          } catch (Exception e) {
            throw e;
          }
        }
      } finally {
        // Unlock the target universe.
        unlockUniverseForUpdate(targetUniverse.getUniverseUUID());
      }
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      xClusterConfig.updateStatus(XClusterConfigStatusType.Failed);
      throw new RuntimeException(e);
    } finally {
      // Unlock the source universe.
      unlockUniverseForUpdate(sourceUniverse.getUniverseUUID());
    }
  }
}
