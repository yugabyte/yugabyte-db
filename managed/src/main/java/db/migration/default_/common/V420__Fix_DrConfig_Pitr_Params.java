// Copyright (c) YugaByte, Inc.

package db.migration.default_.common;

import com.typesafe.config.Config;
import com.typesafe.config.ConfigFactory;
import com.yugabyte.yw.models.migrations.V420.DrConfig;
import com.yugabyte.yw.models.migrations.V420.XClusterConfig;
import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.util.List;
import java.util.Optional;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.flywaydb.core.api.migration.BaseJavaMigration;
import org.flywaydb.core.api.migration.Context;

/**
 * This migration is used to fix the pitr_snapshot_interval_sec column in the dr_config table.
 * caused by migration V376__Add_DR_Config_Constraint.java where YBA mistakenly sets the
 * pitr_retention_period_sec and pitr_snapshot_interval_sec to the same value.
 *
 * <p>Here, we will update all DR configs which has both retention and snapshot interval set and are
 * the same and set the snapshot interval to the value from the runtime config.
 */
@Slf4j
public class V420__Fix_DrConfig_Pitr_Params extends BaseJavaMigration {

  private static final String SNAPSHOT_INTERVAL_PATH =
      "yb.xcluster.transactional.pitr.default_snapshot_interval";
  private static final UUID GLOBAL_SCOPE_UUID =
      UUID.fromString("00000000-0000-0000-0000-000000000000");

  @Override
  public void migrate(Context context) throws Exception {
    List<DrConfig> drConfigs = DrConfig.getAll();
    log.info("Found {} DR configs to fix", drConfigs.size());
    for (DrConfig drConfig : drConfigs) {
      if (drConfig.getPitrRetentionPeriodSec() != null
          && drConfig.getPitrSnapshotIntervalSec() != null
          && drConfig.getPitrRetentionPeriodSec().equals(drConfig.getPitrSnapshotIntervalSec())) {
        UUID targetUniverseUUID = null;
        List<XClusterConfig> xClusterConfigs = drConfig.getXClusterConfigs();
        if (xClusterConfigs != null && !xClusterConfigs.isEmpty()) {
          Optional<XClusterConfig> xClusterConfigResult =
              xClusterConfigs.stream()
                  .filter(xClusterConfig -> !xClusterConfig.isSecondary())
                  .findFirst();
          if (xClusterConfigResult.isPresent()) {
            targetUniverseUUID = xClusterConfigResult.get().getTargetUniverseUUID();
          }
        }

        if (targetUniverseUUID == null) {
          continue;
        }

        Connection connection = context.getConnection();

        Optional<Long> snapshotInterval =
            getPitrParamFromRuntimeConfigEntry(
                connection, targetUniverseUUID, SNAPSHOT_INTERVAL_PATH);

        if (!snapshotInterval.isPresent()) {
          snapshotInterval = Optional.of(drConfig.getPitrRetentionPeriodSec() / 2);
          log.info(
              "Snapshot interval is not present, setting to {} seconds", snapshotInterval.get());
        }

        log.info(
            "Setting pitr_snapshot_interval_sec to {} on Dr Config with UUID {}",
            snapshotInterval,
            drConfig.getUuid());
        drConfig.setPitrSnapshotIntervalSec(snapshotInterval.get());
        drConfig.save();
      }
    }
  }

  Optional<Long> getPitrParamFromRuntimeConfigEntry(
      Connection connection, UUID targetUniverseUUID, String path) throws SQLException {
    Optional<Long> universeScopeResult =
        getRuntimeConfigEntry(connection, path, targetUniverseUUID);
    if (universeScopeResult.isPresent()) {
      return universeScopeResult;
    } else {
      // Check on global scope
      Optional<Long> globalScopeResult = getRuntimeConfigEntry(connection, path, GLOBAL_SCOPE_UUID);
      if (globalScopeResult.isPresent()) {
        return globalScopeResult;
      }
    }
    return Optional.empty();
  }

  Optional<Long> getRuntimeConfigEntry(Connection connection, String path, UUID scopeUuid)
      throws SQLException {
    String findRetentionQueryString =
        String.format(
            "SELECT value from runtime_config_entry where path = '%s' and scope_uuid = '%s'",
            path, scopeUuid);
    ResultSet retentionResultSet =
        connection.createStatement().executeQuery(findRetentionQueryString);
    if (retentionResultSet.next()) {
      String value = new String(retentionResultSet.getBytes("value"));
      Config config = ConfigFactory.parseString("key = " + value);
      Long result = config.getDuration("key").getSeconds();
      return Optional.of(result);
    }
    return Optional.empty();
  }
}
