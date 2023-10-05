// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseDefinitionTaskBase;
import com.yugabyte.yw.forms.ITaskParams;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
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
    public Consumer<Universe> callback;
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
    freezeUniverse(taskParams().callback);
  }
}
