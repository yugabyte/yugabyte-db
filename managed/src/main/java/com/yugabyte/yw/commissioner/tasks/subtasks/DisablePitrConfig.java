// Copyright (c) YugabyteDB, Inc.
package com.yugabyte.yw.commissioner.tasks.subtasks;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.PitrConfig;
import com.yugabyte.yw.models.Universe;
import java.util.List;
import java.util.stream.Collectors;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.DeleteSnapshotScheduleResponse;
import org.yb.client.ListSnapshotSchedulesResponse;
import org.yb.client.SnapshotScheduleInfo;
import org.yb.client.YBClient;

@Slf4j
public class DisablePitrConfig extends UniverseTaskBase {

  @Inject
  protected DisablePitrConfig(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends UniverseTaskParams {
    // The universe UUID must be stored in universeUUID field.

    public boolean ignoreErrors;
  }

  @Override
  protected Params taskParams() {
    return (Params) taskParams;
  }

  @Override
  public String getName() {
    return String.format("%s(Universe=%s)", super.getName(), taskParams().getUniverseUUID());
  }

  @Override
  public void run() {
    Universe universe = Universe.getOrBadRequest(taskParams().getUniverseUUID());
    List<PitrConfig> pitrConfigs = PitrConfig.getByUniverseUUID(taskParams().getUniverseUUID());

    // Filter out disabled PITR configs
    pitrConfigs =
        pitrConfigs.stream()
            .filter(pitrConfig -> !pitrConfig.isDisabled())
            .collect(Collectors.toList());

    if (pitrConfigs.isEmpty()) {
      log.info("No PITR configs found for universe {}", taskParams().getUniverseUUID());
      return;
    }

    log.info(
        "Found {} PITR configs to disable for universe {}",
        pitrConfigs.size(),
        taskParams().getUniverseUUID());

    try (YBClient client = ybService.getUniverseClient(universe)) {
      ListSnapshotSchedulesResponse scheduleListResp = client.listSnapshotSchedules(null);

      for (PitrConfig pitrConfig : pitrConfigs) {
        try {
          // Find and delete the snapshot schedule for this PITR config
          for (SnapshotScheduleInfo scheduleInfo : scheduleListResp.getSnapshotScheduleInfoList()) {
            if (scheduleInfo.getSnapshotScheduleUUID().equals(pitrConfig.getUuid())) {
              DeleteSnapshotScheduleResponse resp =
                  client.deleteSnapshotSchedule(pitrConfig.getUuid());
              if (resp.hasError()) {
                String errorMsg =
                    "Failed to delete snapshot schedule for PITR config "
                        + pitrConfig.getUuid()
                        + " due to error: "
                        + resp.errorMessage();
                log.error(errorMsg);
                if (!taskParams().ignoreErrors) {
                  throw new RuntimeException(errorMsg);
                }
              } else {
                log.debug(
                    "Successfully deleted snapshot schedule for PITR config {}",
                    pitrConfig.getUuid());
              }
              break;
            }
          }

          // Mark the PITR config as disabled
          pitrConfig.updateDisabledStatus(true);
          log.info(
              "Successfully disabled PITR config {} for universe {}",
              pitrConfig.getUuid(),
              taskParams().getUniverseUUID());
        } catch (Exception e) {
          log.error("Error disabling PITR config {}: {}", pitrConfig.getUuid(), e.getMessage());
          if (!taskParams().ignoreErrors) {
            throw new RuntimeException("Failed to disable PITR config " + pitrConfig.getUuid(), e);
          }
        }
      }
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      if (taskParams().ignoreErrors) {
        log.debug(
            "Ignoring error disabling PITR configs for universe: {} as ignoreErrors is set to true."
                + " Error: {}",
            taskParams().getUniverseUUID(),
            e.getMessage());
        return;
      }
      throw new RuntimeException(e);
    }
  }
}
