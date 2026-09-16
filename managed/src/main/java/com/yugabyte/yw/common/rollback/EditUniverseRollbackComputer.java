// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.rollback;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.helpers.StateTransitionDetails;
import com.yugabyte.yw.models.helpers.TaskType;
import javax.inject.Inject;
import javax.inject.Singleton;
import play.libs.Json;

/**
 * Builds a {@link com.yugabyte.yw.commissioner.tasks.RollbackEditUniverse} submission for a failed
 * {@link TaskType#EditUniverse}. Only registered for {@code EditUniverse} in {@link
 * TaskRollbackModule}; Kubernetes edit rollback remains unbound until implemented.
 *
 * <p>Edit-universe rollback is gated behind {@code yb.task.allow_edit_universe_rollback}. Edits
 * that flip primary {@code dedicatedNodes} are refused (Live tserver gflags rewritten before the
 * checkpoint; same as auto-rollback).
 */
@Singleton
public class EditUniverseRollbackComputer implements TaskRollbackComputer {

  private final RuntimeConfGetter confGetter;

  @Inject
  public EditUniverseRollbackComputer(RuntimeConfGetter confGetter) {
    this.confGetter = confGetter;
  }

  @Override
  public RollbackSubmission compute(RollbackContext context) {
    TaskType taskType = context.getTaskInfo().getTaskType();
    if (!confGetter.getGlobalConf(GlobalConfKeys.allowEditUniverseRollback)) {
      throw new PlatformServiceException(
          BAD_REQUEST,
          String.format(
              "Rollback of %s tasks is not enabled. Set yb.task.allow_edit_universe_rollback to"
                  + " enable it.",
              taskType));
    }
    UniverseDefinitionTaskParams params =
        Json.fromJson(context.getOldTaskParams(), UniverseDefinitionTaskParams.class);
    Universe universe = Universe.getOrBadRequest(params.getUniverseUUID());
    StateTransitionDetails details = universe.getStateTransitionDetails();
    if (details != null) {
      // Authoritative rejection also lives in RollbackEditUniverse / requireRollbackable.
      details.requireRollbackable();
    }
    // Skip optimistic version check for this programmatically-submitted task.
    params.expectedUniverseVersion = -1;
    // Fresh task: must not inherit EditUniverse runtimeInfo / retry semantics.
    return new RollbackSubmission(
        TaskType.RollbackEditUniverse,
        params,
        CustomerTask.TaskType.RollbackEditUniverse,
        false /* setPreviousTaskUUID */);
  }
}
