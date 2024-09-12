package com.yugabyte.yw.commissioner.tasks.subtasks.xcluster;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.XClusterConfigTaskBase;
import com.yugabyte.yw.common.DrConfigStates;
import com.yugabyte.yw.common.DrConfigStates.SourceUniverseState;
import com.yugabyte.yw.common.DrConfigStates.TargetUniverseState;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.forms.XClusterConfigTaskParams;
import com.yugabyte.yw.models.DrConfig;
import com.yugabyte.yw.models.XClusterConfig;
import java.util.Objects;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class SetDrStates extends XClusterConfigTaskBase {

  @Inject
  protected SetDrStates(
      BaseTaskDependencies baseTaskDependencies, XClusterUniverseService xClusterUniverseService) {
    super(baseTaskDependencies, xClusterUniverseService);
  }

  public static class Params extends XClusterConfigTaskParams {
    // The parent xCluster config must be stored in xClusterConfig field.

    // The DR config new state.
    public DrConfigStates.State drConfigState;

    // The source universe new dr state.
    public SourceUniverseState sourceUniverseState;

    // The target universe new dr state.
    public TargetUniverseState targetUniverseState;

    // The keyspace name that is undergoing changes by the xCluster task.
    public String keyspacePending;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return String.format(
        "%s(xClusterConfig=%s,drConfigState=%s,sourceUniverseState=%s,"
            + "targetUniverseState=%s,keyspacePending=%s)",
        super.getName(),
        taskParams().getXClusterConfig(),
        taskParams().drConfigState,
        taskParams().sourceUniverseState,
        taskParams().targetUniverseState,
        taskParams().keyspacePending);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    XClusterConfig xClusterConfig = getXClusterConfigFromTaskParams();

    if (!xClusterConfig.isUsedForDr()) {
      throw new IllegalArgumentException(
          "SetDrStates subtask can only run for xCluster configs that are used for DR");
    }
    DrConfig drConfig = xClusterConfig.getDrConfig();

    // The parameter keyspacePending is not added intentionally because when it is null, the task
    // will set keyspacePending in the xCluster config object to null.
    if (Objects.isNull(taskParams().drConfigState)
        && Objects.isNull(taskParams().sourceUniverseState)
        && Objects.isNull(taskParams().targetUniverseState)) {
      throw new IllegalArgumentException("At least one state must not be null");
    }

    try {
      if (Objects.nonNull(taskParams().drConfigState)) {
        log.info(
            "Setting the dr config state of xCluster config {} to {} from {}",
            xClusterConfig.getUuid(),
            taskParams().drConfigState,
            drConfig.getState());
        drConfig.setState(taskParams().drConfigState);
      }
      if (Objects.nonNull(taskParams().sourceUniverseState)) {
        log.info(
            "Setting the source universe state of xCluster config {} to {} from {}",
            xClusterConfig.getUuid(),
            taskParams().sourceUniverseState,
            xClusterConfig.getSourceUniverseState());
        xClusterConfig.setSourceUniverseState(taskParams().sourceUniverseState);
      }
      if (Objects.nonNull(taskParams().targetUniverseState)) {
        log.info(
            "Setting the target universe state of xCluster config {} to {} from {}",
            xClusterConfig.getUuid(),
            taskParams().targetUniverseState,
            xClusterConfig.getTargetUniverseState());
        xClusterConfig.setTargetUniverseState(taskParams().targetUniverseState);
      }

      log.info(
          "Setting pending keyspace of xCluster config {} to {} from {}",
          xClusterConfig.getUuid(),
          taskParams().keyspacePending,
          xClusterConfig.getKeyspacePending());
      xClusterConfig.setKeyspacePending(taskParams().keyspacePending);

      drConfig.update();
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    }

    log.info("Completed {}", getName());
  }
}
