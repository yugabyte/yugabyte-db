/*
 * Copyright 2021 YugabyteDB, Inc. and Contributors
 *
 * Licensed under the Polyform Free Trial License 1.0.0 (the "License"); you
 * may not use this file except in compliance with the License. You
 * may obtain a copy of the License at
 *
 *     https://github.com/YugaByte/yugabyte-db/blob/master/licenses/POLYFORM-FREE-TRIAL-LICENSE-1.0.0.txt
 */

package com.yugabyte.yw.commissioner.tasks;

import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import com.fasterxml.jackson.annotation.JsonProperty;
import com.fasterxml.jackson.databind.annotation.JsonDeserialize;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.ITask.Abortable;
import com.yugabyte.yw.commissioner.ITask.Retryable;
import com.yugabyte.yw.commissioner.UserTaskDetails.SubTaskGroupType;
import com.yugabyte.yw.common.utils.CapacityReservationUtil;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
@Abortable
@Retryable
public class ResumeUniverse extends UniverseDefinitionTaskBase {
  private volatile RuntimeInfo runtimeInfo;

  @Inject
  protected ResumeUniverse(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @JsonIgnoreProperties(ignoreUnknown = true)
  @JsonDeserialize(converter = Params.Converter.class)
  public static class Params extends UniverseDefinitionTaskParams {
    public UUID customerUUID;

    public static class Converter
        extends UniverseDefinitionTaskParams.BaseConverter<ResumeUniverse.Params> {}
  }

  /** Task runtime progress info. */
  public static class RuntimeInfo {
    @JsonProperty("certsUpdated")
    boolean certsUpdated;
  }

  public Params params() {
    return (Params) taskParams;
  }

  @Override
  protected void createPrecheckTasks(Universe universe) {
    super.createPrecheckTasks(universe);
    runtimeInfo = getRuntimeInfo(RuntimeInfo.class);
  }

  @Override
  public void run() {
    try {
      // Update the universe DB with the update to be performed and set the 'updateInProgress' flag
      // to prevent other updates from happening.
      Universe universe = lockAndFreezeUniverseForUpdate(-1, null /* Txn callback */);
      boolean deleteCapacityReservation =
          createCapacityReservationsIfNeeded(
              universe.getUniverseDetails().nodeDetailsSet,
              CapacityReservationUtil.OperationType.RESUME,
              node ->
                  node.state == NodeDetails.NodeState.Stopped
                      || node.state == NodeDetails.NodeState.InstanceStopped);
      createResumeUniverseTasks(
          universe,
          params().customerUUID,
          !runtimeInfo.certsUpdated,
          u -> updateRuntimeInfo(RuntimeInfo.class, info -> info.certsUpdated = true));

      if (deleteCapacityReservation) {
        createDeleteCapacityReservationTask();
      }

      createMarkUniverseUpdateSuccessTasks().setSubTaskGroupType(SubTaskGroupType.ResumeUniverse);

      // Run all the tasks.
      getRunnableTask().runSubTasks();
    } catch (Throwable t) {
      log.error("Error executing task {} with error='{}'.", getName(), t.getMessage(), t);
      clearCapacityReservationOnError(t, Universe.getOrBadRequest(taskParams().getUniverseUUID()));
      throw t;
    } finally {
      unlockUniverseForUpdate();
    }
    log.info("Finished {} task.", getName());
  }
}
