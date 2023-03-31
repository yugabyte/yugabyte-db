// Copyright (c) Yugabyte, Inc.
package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.AbstractTaskBase;
import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.SetUniverseKey;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.Universe;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

@Slf4j
public class SetActiveUniverseKeys extends AbstractTaskBase {

  @Inject private SetUniverseKey setUniverseKeys;

  private final boolean SET_ACTIVE_KEYS_FORCEFULLY = true;

  @Inject
  protected SetActiveUniverseKeys(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends UniverseDefinitionTaskParams {}

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    try {
      Universe u = Universe.getOrBadRequest(taskParams().getUniverseUUID());
      setUniverseKeys.setUniverseKey(u, SET_ACTIVE_KEYS_FORCEFULLY);
    } catch (Exception e) {
      throw e;
    }
  }
}
