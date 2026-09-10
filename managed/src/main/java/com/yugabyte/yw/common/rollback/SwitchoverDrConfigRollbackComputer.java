// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.common.rollback;

import static play.mvc.Http.Status.BAD_REQUEST;

import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.DrConfigTaskParams;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.XClusterTableConfig;
import com.yugabyte.yw.models.helpers.TaskType;
import java.util.Objects;
import javax.inject.Singleton;
import lombok.extern.slf4j.Slf4j;
import play.libs.Json;

/** Builds a SwitchoverDrConfigRollback submission from a failed SwitchoverDrConfig task. */
@Singleton
@Slf4j
public class SwitchoverDrConfigRollbackComputer implements TaskRollbackComputer {

  @Override
  public RollbackSubmission compute(RollbackContext context) {
    DrConfigTaskParams taskParams =
        Json.fromJson(context.getOldTaskParams(), DrConfigTaskParams.class);
    taskParams.refreshIfExists();

    // Roll back cannot be done if the old xCluster config is partially or fully deleted.
    XClusterConfig currentXClusterConfig = taskParams.getOldXClusterConfig();
    if (Objects.isNull(currentXClusterConfig)
        || !currentXClusterConfig.getTables().stream()
            .allMatch(XClusterTableConfig::isReplicationSetupDone)) {
      // At this point, the replication group on the new primary is deleted and it is
      // possible that the user has written data to the new primary, so setting up
      // replication from the new primary to the old primary is not safe and might need
      // bootstrapping which cannot be done in the rollback of the switchover.
      throw new PlatformServiceException(
          BAD_REQUEST,
          "The old xCluster config or its associated replication group is deleted and cannot do a"
              + " roll back; At this point the user is able to write to the new primary universe."
              + " You may retry the switchover task. If your intention is make the new primary"
              + " universe the dr universe again, you can run another switchover task.");
    }
    log.debug("Rolling back switchover task with old xCluster config: {}", currentXClusterConfig);
    return new RollbackSubmission(
        TaskType.SwitchoverDrConfigRollback, taskParams, CustomerTask.TaskType.SwitchoverRollback);
  }
}
