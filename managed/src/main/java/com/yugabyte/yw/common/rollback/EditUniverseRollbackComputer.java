// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.rollback;

import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.NOT_IMPLEMENTED;

import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.GlobalConfKeys;
import com.yugabyte.yw.common.config.RuntimeConfGetter;
import com.yugabyte.yw.models.helpers.TaskType;
import javax.inject.Inject;
import javax.inject.Singleton;

/**
 * Placeholder for EditUniverse / EditKubernetesUniverse rollback until PLAT-21484 lands.
 *
 * <p>Currently unreachable in production (those tasks are not yet annotated {@code @CanRollback});
 * registered so a forced eligibility path fails explicitly rather than attempting a rollback.
 *
 * <p>Edit-universe rollback is gated behind a runtime flag while the feature is built out. The
 * eligibility gate/annotation should also consult this flag once the rollback path lands.
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
    throw new PlatformServiceException(
        NOT_IMPLEMENTED,
        String.format(
            "Rollback for task type %s is not yet supported; edit-universe rollback is under"
                + " development (tracked by PLAT-21484).",
            taskType));
  }
}
