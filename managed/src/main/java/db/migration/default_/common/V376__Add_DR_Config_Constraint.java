// Copyright (c) YugaByte, Inc.

package db.migration.default_.common;

import com.typesafe.config.Config;
import com.typesafe.config.ConfigFactory;
import com.yugabyte.yw.models.migrations.V376.DrConfig;
import com.yugabyte.yw.models.migrations.V376.XClusterConfig;
import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.util.List;
import java.util.Optional;
import java.util.UUID;
import lombok.extern.slf4j.Slf4j;
import org.flywaydb.core.api.migration.BaseJavaMigration;
import org.flywaydb.core.api.migration.Context;

@Slf4j
public class V376__Add_DR_Config_Constraint extends BaseJavaMigration {

  private static final String RETENTION_PERIOD_PATH =
      "yb.xcluster.transactional.pitr.default_retention_period";
  private static final String SNAPSHOT_INTERVAL_PATH =
      "yb.xcluster.transactional.pitr.default_snapshot_interval";
  private static final UUID GLOBAL_SCOPE_UUID =
      UUID.fromString("00000000-0000-0000-0000-000000000000");

  @Override
  public void migrate(Context context) throws Exception {
    List<DrConfig> drConfigs = DrConfig.getAll();
    for (DrConfig drConfig : drConfigs) {
      if (drConfig.getPitrRetentionPeriodSec() == null
          && drConfig.getPitrSnapshotIntervalSec() == null) {
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
        Long pitrRetentionPeriodSec = 259200L;
        Long pitrSnapshotIntervalSec = 3600L;
        if (targetUniverseUUID != null) {
          Connection connection = context.getConnection();

          Optional<Long> retentionPeriod =
              getPitrParamFromRuntimeConfigEntry(
                  connection, targetUniverseUUID, RETENTION_PERIOD_PATH);

          if (retentionPeriod.isPresent()) {
            pitrRetentionPeriodSec = retentionPeriod.get();
          }

          Optional<Long> snapshotInterval =
              getPitrParamFromRuntimeConfigEntry(
                  connection, targetUniverseUUID, SNAPSHOT_INTERVAL_PATH);
          if (snapshotInterval.isPresent()) {
            pitrSnapshotIntervalSec = snapshotInterval.get();
          }
        }
        log.info(
            "Setting pitr_retention_period_sec to {} and pitr_snapshot_interval_sec to {} on Dr"
                + " Config with UUID {}",
            pitrRetentionPeriodSec,
            pitrSnapshotIntervalSec,
            drConfig.getUuid());
        drConfig.setPitrRetentionPeriodSec(pitrRetentionPeriodSec);
        drConfig.setPitrSnapshotIntervalSec(pitrSnapshotIntervalSec);
        drConfig.save();
      }
    }

    // Add NOT NULL constraint to pitr_retention_period_sec and pitr_snapshot_interval_sec
    addNotNullConstraint(context);
  }

  void addNotNullConstraint(Context context) throws SQLException {
    Connection connection = context.getConnection();
    connection
        .createStatement()
        .execute("ALTER TABLE dr_config ALTER COLUMN pitr_retention_period_sec SET NOT NULL");
    connection
        .createStatement()
        .execute("ALTER TABLE dr_config ALTER COLUMN pitr_snapshot_interval_sec SET NOT NULL");
  }

  Optional<Long> getPitrParamFromRuntimeConfigEntry(
      Connection connection, UUID targetUniverseUUID, String path) throws SQLException {
    Optional<Long> universeScopeResult =
        getRuntimeConfigEntry(connection, path, targetUniverseUUID);
    if (universeScopeResult.isPresent()) {
      return universeScopeResult;
    } else {
      // Check on global scope
      Optional<Long> globalScopeResult =
          getRuntimeConfigEntry(connection, RETENTION_PERIOD_PATH, GLOBAL_SCOPE_UUID);
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
