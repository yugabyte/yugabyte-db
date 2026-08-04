// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import static com.google.common.base.Preconditions.checkState;

import com.fasterxml.jackson.annotation.JsonIgnore;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Universe.UniverseUpdaterConfig;
import java.util.Objects;
import java.util.function.Consumer;
import javax.inject.Inject;
import lombok.Setter;

public class FreezeUniverse extends UniverseDefinitionTaskBase {

  @Inject
  protected FreezeUniverse(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  @Setter
  public static class Params extends UniverseDefinitionTaskParams {
    @JsonIgnore public Consumer<Universe> callback;
    @JsonIgnore public ExecutionContext executionContext;
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
    UniverseUpdaterConfig updaterConfig =
        currentUpdaterConfig.toBuilder()
            .callback(taskParams().callback)
            .checkSuccess(true)
            .freezeUniverse(true)
            .build();
    saveUniverseDetails(taskParams().getUniverseUUID(), getFreezeUniverseUpdater(updaterConfig));
    taskParams().executionContext.lockUniverse(taskParams().getUniverseUUID(), updaterConfig);
  }
}
