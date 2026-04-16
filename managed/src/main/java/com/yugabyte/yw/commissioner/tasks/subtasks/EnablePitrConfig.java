// Copyright (c) YugabyteDB, Inc.
package com.yugabyte.yw.commissioner.tasks.subtasks;

import static com.yugabyte.yw.common.backuprestore.BackupUtil.TABLE_TYPE_TO_YQL_DATABASE_MAP;

import com.yugabyte.yw.commissioner.BaseTaskDependencies;
import com.yugabyte.yw.commissioner.tasks.UniverseTaskBase;
import com.yugabyte.yw.forms.CreatePitrConfigParams;
import com.yugabyte.yw.forms.UniverseTaskParams;
import com.yugabyte.yw.models.PitrConfig;
import com.yugabyte.yw.models.Universe;
import io.ebean.annotation.Transactional;
import java.util.List;
import java.util.Objects;
import java.util.UUID;
import javax.inject.Inject;
import lombok.extern.slf4j.Slf4j;
import org.yb.client.CreateSnapshotScheduleResponse;
import org.yb.client.YBClient;

@Slf4j
public class EnablePitrConfig extends UniverseTaskBase {

  @Inject
  protected EnablePitrConfig(BaseTaskDependencies baseTaskDependencies) {
    super(baseTaskDependencies);
  }

  public static class Params extends UniverseTaskParams {
    // The universe UUID must be stored in universeUUID field.
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
    List<PitrConfig> disabledPitrConfigs =
        PitrConfig.getDisabledByUniverseUUID(taskParams().getUniverseUUID());

    if (disabledPitrConfigs.isEmpty()) {
      log.info("No disabled PITR configs found for universe {}", taskParams().getUniverseUUID());
      return;
    }

    log.info(
        "Found {} disabled PITR configs to enable for universe {}",
        disabledPitrConfigs.size(),
        taskParams().getUniverseUUID());

    try (YBClient client = ybService.getUniverseClient(universe)) {
      for (PitrConfig pitrConfig : disabledPitrConfigs) {
        try {
          // Find the keyspace id of the keyspace name specified in the pitr config.
          String keyspaceId =
              getKeyspaceNameKeyspaceIdMap(client, pitrConfig.getTableType())
                  .get(pitrConfig.getDbName());
          if (Objects.isNull(keyspaceId)) {
            String errorMsg =
                String.format(
                    "A keyspace with name %s and table type %s could not be found for PITR config"
                        + " %s",
                    pitrConfig.getDbName(), pitrConfig.getTableType(), pitrConfig.getUuid());
            log.error(errorMsg);
            throw new IllegalArgumentException(errorMsg);
          }
          log.debug(
              "Found keyspace id {} for keyspace name {} for PITR config {}",
              keyspaceId,
              pitrConfig.getDbName(),
              pitrConfig.getUuid());

          // Create the PITR config on DB with existing parameters.
          CreateSnapshotScheduleResponse resp =
              client.createSnapshotSchedule(
                  TABLE_TYPE_TO_YQL_DATABASE_MAP.get(pitrConfig.getTableType()),
                  pitrConfig.getDbName(),
                  keyspaceId,
                  pitrConfig.getRetentionPeriod(),
                  pitrConfig.getScheduleInterval());
          if (resp.hasError()) {
            String errorMsg =
                "Failed to create snapshot schedule for PITR config "
                    + pitrConfig.getUuid()
                    + " due to error: "
                    + resp.errorMessage();
            log.error(errorMsg);
            throw new RuntimeException(errorMsg);
          }

          reCreatePitrConfig(pitrConfig, resp.getSnapshotScheduleUUID());
        } catch (Exception e) {
          log.error("Error enabling PITR config {}: {}", pitrConfig.getUuid(), e.getMessage());
          throw new RuntimeException("Failed to enable PITR config " + pitrConfig.getUuid(), e);
        }
      }
    } catch (Exception e) {
      log.error("{} hit error : {}", getName(), e.getMessage());
      throw new RuntimeException(e);
    }
  }

  @Transactional
  private void reCreatePitrConfig(PitrConfig pitrConfig, UUID scheduleUUID) {
    CreatePitrConfigParams params = pitrConfig.toCreatePitrConfigParams();
    pitrConfig.delete();

    PitrConfig newPitrConfig = PitrConfig.create(scheduleUUID, params);

    log.info(
        "Successfully enabled new PITR config {} for universe {}",
        newPitrConfig.getUuid(),
        taskParams().getUniverseUUID());
  }
}
