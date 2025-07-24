// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.PitrConfig;
import com.yugabyte.yw.models.Universe;
import java.util.Date;
import java.util.List;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;

/**
 * Subtask to update intermittentMinRecoverTimeInMillis for all PITR configs associated with a
 * universe. This is used during software upgrade and rollback operations to ensure PITR configs are
 * only valid from the completion of these operations.
 */
@Slf4j
public class UpdatePitrConfigIntermittentMinRecoverTime extends UniverseTaskBase {

  @Inject
  protected UpdatePitrConfigIntermittentMinRecoverTime(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends UniverseDefinitionTaskParams {}

  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public void run() {
    try {
      log.info("Running {} for universe {}", getName(), taskParams().getUniverseUUID());

      Universe universe = getUniverse();
      List<PitrConfig> pitrConfigs = PitrConfig.getByUniverseUUID(universe.getUniverseUUID());

      if (pitrConfigs.isEmpty()) {
        log.info("No PITR configs found for universe {}", universe.getUniverseUUID());
        return;
      }

      long currentTimeInMillis = System.currentTimeMillis();
      log.info(
          "Updating {} PITR configs for universe {} with intermittentMinRecoverTimeInMillis={}",
          pitrConfigs.size(),
          universe.getUniverseUUID(),
          currentTimeInMillis);

      for (PitrConfig pitrConfig : pitrConfigs) {
        pitrConfig.setIntermittentMinRecoverTimeInMillis(currentTimeInMillis);
        pitrConfig.setUpdateTime(new Date());
        pitrConfig.update();
        log.debug(
            "Updated PITR config {} with intermittentMinRecoverTimeInMillis={}",
            pitrConfig.getUuid(),
            currentTimeInMillis);
      }

      log.info(
          "Successfully updated {} PITR configs for universe {}",
          pitrConfigs.size(),
          universe.getUniverseUUID());

    } catch (Exception e) {
      String msg =
          String.format(
              "Task %s failed for universe %s - %s",
              getName(), taskParams().getUniverseUUID(), e.getMessage());
      log.error(msg, e);
      throw new RuntimeException(msg, e);
    }
  }
}
