// Copyright (c) YugabyteDB, Inc.

package db.migration.default_.postgres;

import com.yugabyte.yw.common.operator.utils.KubernetesEnvironmentVariables;
import java.sql.Connection;
import java.sql.PreparedStatement;
import java.sql.SQLException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import lombok.AllArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.flywaydb.core.api.migration.BaseJavaMigration;
import org.flywaydb.core.api.migration.Context;

/**
 * Seeds YBA-node resource alert configurations. We seed the VM-based variants ({@code YBA_NODE_*})
 * when YBA runs on a VM, and the K8s-based variants ({@code YBA_K8S_*}) when YBA runs in Kubernetes
 * - templates depend on metrics that are only available in one of the two environments so seeding
 * the wrong set would leave the customer with always-firing or never-firing alerts.
 */
@Slf4j
public class V452__YBA_Node_Resource_Alerts extends BaseJavaMigration {

  @AllArgsConstructor
  private static class SeedEntry {
    String name;
    String description;
    String thresholdsJson;
    String template;
  }

  private static final List<SeedEntry> VM_ENTRIES =
      Arrays.asList(
          new SeedEntry(
              "YBA node CPU usage",
              "Average YBA node CPU usage percentage for 30 minutes is above threshold",
              "{\"WARNING\":{\"condition\":\"GREATER_THAN\", \"threshold\":90.0},"
                  + "\"SEVERE\":{\"condition\":\"GREATER_THAN\", \"threshold\":95.0}}",
              "YBA_NODE_CPU_USAGE"),
          new SeedEntry(
              "YBA node memory usage",
              "Average YBA node memory usage percentage for 10 minutes is above threshold",
              "{\"SEVERE\":{\"condition\":\"GREATER_THAN\", \"threshold\":90.0}}",
              "YBA_NODE_MEMORY_USAGE"),
          new SeedEntry(
              "YBA node disk usage",
              "YBA node disk usage percentage is above threshold",
              "{\"SEVERE\":{\"condition\":\"GREATER_THAN\", \"threshold\":95.0}}",
              "YBA_NODE_DISK_USAGE"));

  private static final List<SeedEntry> K8S_ENTRIES =
      Arrays.asList(
          new SeedEntry(
              "YBA container CPU usage",
              "Average YBA container CPU usage percentage for 30 minutes is above threshold",
              "{\"WARNING\":{\"condition\":\"GREATER_THAN\", \"threshold\":90.0},"
                  + "\"SEVERE\":{\"condition\":\"GREATER_THAN\", \"threshold\":95.0}}",
              "YBA_K8S_CPU_USAGE"),
          new SeedEntry(
              "YBA container memory usage",
              "Average YBA container memory usage percentage for 10 minutes is above threshold",
              "{\"SEVERE\":{\"condition\":\"GREATER_THAN\", \"threshold\":90.0}}",
              "YBA_K8S_MEMORY_USAGE"),
          new SeedEntry(
              "YBA container disk usage",
              "YBA container persistent volume usage percentage is above threshold",
              "{\"SEVERE\":{\"condition\":\"GREATER_THAN\", \"threshold\":95.0}}",
              "YBA_K8S_DISK_USAGE"));

  private static final String INSERT_CONFIG_SQL =
      "INSERT INTO alert_configuration"
          + " (uuid, customer_uuid, name, description, create_time, target_type, target,"
          + " thresholds, threshold_unit, template, active, default_destination)"
          + " SELECT gen_random_uuid(), uuid, ?, ?, current_timestamp, 'PLATFORM',"
          + " '{\"all\":true}', ?::jsonb, 'PERCENT', ?, true, true FROM customer";

  @Override
  public void migrate(Context context) throws SQLException {
    Connection connection = context.getConnection();
    boolean ybaInK8s = KubernetesEnvironmentVariables.isYbaRunningInKubernetes();
    List<SeedEntry> entries = ybaInK8s ? K8S_ENTRIES : VM_ENTRIES;
    log.info(
        "Seeding YBA node resource alerts for {} environment: {} templates",
        ybaInK8s ? "K8s" : "VM",
        entries.size());

    List<String> insertedConfigNames = new ArrayList<>();
    try (PreparedStatement insertConfig = connection.prepareStatement(INSERT_CONFIG_SQL)) {
      for (SeedEntry entry : entries) {
        insertConfig.setString(1, entry.name);
        insertConfig.setString(2, entry.description);
        insertConfig.setString(3, entry.thresholdsJson);
        insertConfig.setString(4, entry.template);
        insertConfig.executeUpdate();
        insertedConfigNames.add(entry.name);
      }
    }

    // create_customer_alert_definitions(configurationName, skipTargetLabels) fans out
    // alert_definition rows for every customer that just got a matching alert_configuration.
    try (PreparedStatement createDefs =
        connection.prepareStatement("SELECT create_customer_alert_definitions(?, false)")) {
      for (String name : insertedConfigNames) {
        createDefs.setString(1, name);
        createDefs.executeQuery();
      }
    }
  }
}
