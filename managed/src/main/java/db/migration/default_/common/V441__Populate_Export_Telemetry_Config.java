// Copyright (c) YugabyteDB, Inc.

package db.migration.default_.common;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.yugabyte.yw.models.migrations.V441.ExportTelemetryConfig;
import com.yugabyte.yw.models.migrations.V441.TelemetryConfig;
import com.yugabyte.yw.models.migrations.V441.Universe;
import io.ebean.DB;
import lombok.extern.slf4j.Slf4j;
import org.flywaydb.core.api.migration.BaseJavaMigration;
import org.flywaydb.core.api.migration.Context;

/**
 * Populates the export_telemetry_config table for all existing universes. Extracts auditLogConfig,
 * queryLogConfig, and metricsExportConfig from the primary cluster's userIntent and combines them
 * into a single TelemetryConfig.
 */
@Slf4j
public class V441__Populate_Export_Telemetry_Config extends BaseJavaMigration {

  private static final ObjectMapper MAPPER = new ObjectMapper();

  @Override
  public void migrate(Context context) {
    log.info("Populating export_telemetry_config table for all existing universes");
    DB.execute(V441__Populate_Export_Telemetry_Config::populateExportTelemetryConfigs);
    log.info("Finished populating export_telemetry_config table for all existing universes");
  }

  public static void populateExportTelemetryConfigs() {
    for (Universe universe : Universe.getAll()) {
      try {
        TelemetryConfig telemetryConfig = buildTelemetryConfig(universe.universeDetailsJson);

        if (telemetryConfig == null) {
          log.debug(
              "No auditLogConfig, queryLogConfig, or metricsExportConfig found for universe {}.",
              universe.universeUUID);
          continue;
        }

        ExportTelemetryConfig exportTelemetryConfig = new ExportTelemetryConfig();
        exportTelemetryConfig.setUniverseUuid(universe.universeUUID);
        exportTelemetryConfig.setTelemetryConfig(telemetryConfig);
        exportTelemetryConfig.save();

        log.debug("Created export_telemetry_config for universe {}", universe.universeUUID);
      } catch (Exception e) {
        log.warn(
            "Failed to create export_telemetry_config for universe {}: {}",
            universe.universeUUID,
            e.getMessage());
      }
    }
  }

  /**
   * Extracts telemetry configs from universe details JSON and builds a TelemetryConfig object.
   * Returns null if no telemetry configs are configured.
   */
  private static TelemetryConfig buildTelemetryConfig(String universeDetailsJson) {
    try {
      JsonNode universeDetails = MAPPER.readTree(universeDetailsJson);
      JsonNode clusters = universeDetails.get("clusters");

      if (clusters == null || !clusters.isArray()) {
        return null;
      }

      // Find the PRIMARY cluster's userIntent
      JsonNode primaryUserIntent = null;
      for (JsonNode cluster : clusters) {
        JsonNode clusterType = cluster.get("clusterType");
        if (clusterType != null && "PRIMARY".equals(clusterType.asText())) {
          primaryUserIntent = cluster.get("userIntent");
          break;
        }
      }

      if (primaryUserIntent == null) {
        return null;
      }

      // Extract the three telemetry configs as JsonNodes
      JsonNode auditLogConfig = getIfPresent(primaryUserIntent, "auditLogConfig");
      JsonNode queryLogConfig = getIfPresent(primaryUserIntent, "queryLogConfig");
      JsonNode metricsExportConfig = getIfPresent(primaryUserIntent, "metricsExportConfig");

      // If all are null, return null
      if (auditLogConfig == null && queryLogConfig == null && metricsExportConfig == null) {
        return null;
      }

      // Build the TelemetryConfig with JsonNodes
      TelemetryConfig telemetryConfig = new TelemetryConfig();
      telemetryConfig.setAuditLogConfig(auditLogConfig);
      telemetryConfig.setQueryLogConfig(queryLogConfig);
      telemetryConfig.setMetricsExportConfig(metricsExportConfig);
      return telemetryConfig;
    } catch (Exception e) {
      log.warn("Failed to parse universe details: {}", e.getMessage());
      return null;
    }
  }

  /** Get a JsonNode field if it exists and is not null. */
  private static JsonNode getIfPresent(JsonNode parent, String fieldName) {
    JsonNode node = parent.get(fieldName);
    if (node == null || node.isNull()) {
      return null;
    }
    return node;
  }
}
