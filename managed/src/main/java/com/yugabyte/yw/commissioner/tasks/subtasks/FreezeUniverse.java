// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static com.google.common.base.Preconditions.checkState;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase;
import com.yugabyte.yw.common.Util;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdaterConfig;
import java.util.Objects;
import java.util.function.Consumer;
import javax.inject.Inject;
import lombok.Setter;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class FreezeUniverse extends UniverseDefinitionTaskBase {

  @Inject
  protected FreezeUniverse(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Setter
  public static class Params extends UniverseDefinitionTaskParams {
    @JsonIgnore public Consumer<Universe> callback;
    @JsonIgnore public ExecutionContext executionContext;
    @JsonIgnore public Universe universe;
    @JsonIgnore public UniverseDefinitionTaskParams stateTransitionCaptureTarget;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void initialize(ITaskParams params) {
    super.initialize(params);
    Objects.requireNonNull(taskParams().getUniverseUUID(), "Universe UUID cannot be null");
  }

  @Override
  public void run() {
    UniverseUpdaterConfig currentUpdaterConfig =
        taskParams().executionContext.getUniverseUpdaterConfig(taskParams().getUniverseUUID());
    checkState(currentUpdaterConfig != null, "Universe must already be locked");
    // Verify that the universe details in the DB has not changed after running pre-checks.
    // DB state can be changed only during freezing the universe.
    JsonNode deltaJsonNode = Util.findDiffJsonNode(taskParams().universe);
    if (deltaJsonNode != null && !deltaJsonNode.isEmpty()) {
      log.error(
          "Freezing universe {} with non-empty delta: {}",
          taskParams().getUniverseUUID(),
          deltaJsonNode.toPrettyString());
      throw new RuntimeException(
          "Some fields in the universe have changed since the lock was acquired before freezing");
    }

    UniverseDefinitionTaskParams beforeUniverseDetails = taskParams().universe.getUniverseDetails();
    UniverseDefinitionTaskParams captureTarget = taskParams().stateTransitionCaptureTarget;
    Consumer<Universe> freezeCallback = taskParams().callback;
    if (captureTarget != null) {
      freezeCallback =
          universe -> {
            if (taskParams().callback != null) {
              taskParams().callback.accept(universe);
            }
            captureStateTransitionDelta(universe, beforeUniverseDetails, captureTarget);
          };
    }

    UniverseUpdaterConfig updaterConfig =
        currentUpdaterConfig.toBuilder()
            .callback(freezeCallback)
            .checkSuccess(true)
            .freezeUniverse(true)
            .build();
    saveUniverseDetails(taskParams().getUniverseUUID(), getFreezeUniverseUpdater(updaterConfig));
    taskParams().executionContext.lockUniverse(taskParams().getUniverseUUID(), updaterConfig);
  }
}
