// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks;

import com.fasterxml.jackson.databind.DeserializationFeature;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.SerializationFeature;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.XClusterUniverseService;
import com.yugabyte.yw.forms.XClusterConfigTaskParams;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.XClusterConfig;
import java.io.IOException;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;

@Slf4j
public class ResumeXClusterUniverses extends XClusterConfigTaskBase {

  @Inject
  protected ResumeXClusterUniverses(
      BaseTaskDependencies baseTaskDependencies, XClusterUniverseService xClusterUniverseService) {
    super(baseTaskDependencies, xClusterUniverseService);
  }

  @Override
  public void run() {
    log.info("Running {}", getName());

    XClusterConfig xClusterConfig = getXClusterConfigFromTaskParams();
    Universe sourceUniverse = Universe.getOrBadRequest(xClusterConfig.getSourceUniverseUUID());
    Universe targetUniverse = Universe.getOrBadRequest(xClusterConfig.getTargetUniverseUUID());

    XClusterConfigTaskParams sourceUniverseParams = copyUniverseParams(sourceUniverse);
    sourceUniverseParams.editFormData = taskParams().getEditFormData();
    sourceUniverseParams.xClusterConfig = taskParams().getXClusterConfig();

    XClusterConfigTaskParams targetUniverseParams = copyUniverseParams(targetUniverse);
    targetUniverseParams.editFormData = taskParams().getEditFormData();
    targetUniverseParams.xClusterConfig = taskParams().getXClusterConfig();

    try {
      // Lock the source universe.
      lockAndFreezeUniverseForUpdate(
          sourceUniverse.getUniverseUUID(), sourceUniverse.getVersion(), null /* Txn callback */);
      try {
        // Lock the target universe.
        lockAndFreezeUniverseForUpdate(
            targetUniverse.getUniverseUUID(), targetUniverse.getVersion(), null /* Txn callback */);

        taskParams = sourceUniverseParams;
        createResumeUniverseTasks(
            sourceUniverse,
            Customer.get(sourceUniverse.getCustomerId()).getUuid(),
            true /*updateCerts*/,
            u -> {});

        taskParams = targetUniverseParams;
        createResumeUniverseTasks(
            targetUniverse,
            Customer.get(targetUniverse.getCustomerId()).getUuid(),
            true /*updateCerts*/,
            u -> {});

        createSetReplicationPausedTask(xClusterConfig, false /* pause */);
        createWaitForReplicationDrainTask(xClusterConfig);

        // Used in createUpdateWalRetentionTasks.
        taskParams = sourceUniverseParams;
        createUpdateWalRetentionTasks(sourceUniverse, XClusterUniverseAction.RESUME);

        createMarkUniverseUpdateSuccessTasks(sourceUniverse.getUniverseUUID())
            .setSubTaskGroupType(SubTaskGroupType.ResumeUniverse);

        taskParams = targetUniverseParams;
        createMarkUniverseUpdateSuccessTasks(targetUniverse.getUniverseUUID())
            .setSubTaskGroupType(SubTaskGroupType.ResumeUniverse);

        getRunnableTask().runSubTasks();
      } finally {
        // Unlock the target universe.
        unlockUniverseForUpdate(targetUniverse.getUniverseUUID());
      }
    } catch (Throwable t) {
      log.error("{} hit error : {}", getName(), t.getMessage());
      throw t;
    } finally {
      // Unlock the source universe.
      unlockUniverseForUpdate(sourceUniverse.getUniverseUUID());
    }
    log.info("Completed {}", getName());
  }

  private XClusterConfigTaskParams copyUniverseParams(Universe universe) {
    ObjectMapper mapper =
        Json.mapper()
            .copy()
            .configure(DeserializationFeature.FAIL_ON_UNKNOWN_PROPERTIES, false)
            .configure(SerializationFeature.FAIL_ON_EMPTY_BEANS, false);
    try {
      return mapper.readValue(
          mapper.writeValueAsString(universe.getUniverseDetails()), XClusterConfigTaskParams.class);
    } catch (IOException e) {
      throw new RuntimeException(e);
    }
  }
}
