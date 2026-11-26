// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.rollback;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.commissioner.tasks.params.NodeTaskParams;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.StateTransitionDetails;
import com.yugabyte.yw.models.helpers.TaskType;
import javax.inject.Inject;
import javax.inject.Singleton;
import play.libs.Json;

/**
 * Builds a {@link com.yugabyte.yw.commissioner.tasks.RollbackAddNodeToUniverse} submission for a
 * failed {@link TaskType#AddNodeToUniverse}. Gated behind {@code yb.task.allow_add_node_rollback}.
 */
@Singleton
public class AddNodeToUniverseRollbackComputer implements TaskRollbackComputer {

  public static final TaskType ROLLBACK_TASK_TYPE = TaskType.RollbackAddNodeToUniverse;

  private final RuntimeConfGetter confGetter;

  @Inject
  public AddNodeToUniverseRollbackComputer(RuntimeConfGetter confGetter) {
    this.confGetter = confGetter;
  }

  @Override
  public boolean isEnabled() {
    return confGetter.getGlobalConf(GlobalConfKeys.allowAddNodeRollback);
  }

  @Override
  public TaskType rollbackTaskType() {
    return ROLLBACK_TASK_TYPE;
  }

  @Override
  public boolean requiresStateTransitionDetails() {
    return true;
  }

  @Override
  public RollbackSubmission compute(RollbackContext context) {
    TaskType taskType = context.getTaskInfo().getTaskType();
    // Second gate for direct API calls; listing already uses {@link #isEnabled()}.
    if (!isEnabled()) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Rollback of %s tasks is not enabled. Set yb.task.allow_add_node_rollback to"
                  + " enable it.",
              taskType));
    }
    NodeTaskParams params = Json.fromJson(context.getOldTaskParams(), NodeTaskParams.class);
    Universe universe = Universe.getOrBadRequest(params.getUniverseUUID());
    StateTransitionDetails details = universe.getStateTransitionDetails();
    if (details != null) {
      details.requireRollbackable();
    }
    params.expectedUniverseVersion = -1;
    return new RollbackSubmission(
        rollbackTaskType(),
        params,
        CustomerTask.TaskType.RollbackAddNodeToUniverse,
        false /* setPreviousTaskUUID */);
  }
}
