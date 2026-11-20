// Copyright (c) YugabyteDB, Inc.

package db.migration.default_.postgres;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.AlertTemplate;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.AlertConfiguration;
import com.yugabyte.yw.models.AlertConfiguration.Severity;
import com.yugabyte.yw.models.AlertDefinition;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.common.Condition;
import com.yugabyte.yw.models.filters.AlertConfigurationFilter;
import com.yugabyte.yw.models.filters.AlertDefinitionFilter;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import io.ebean.DB;
import java.nio.charset.StandardCharsets;
import java.sql.Connection;
import java.sql.Statement;
import java.util.List;
import java.util.Set;
import java.util.stream.Collectors;
import org.apache.commons.io.IOUtils;
import org.junit.Test;

public class V480__Health_Check_And_Scheduler_Not_Running_AlertsTest extends FakeDBApplication {

  @Test
  public void testHealthCheckAlertOnAllUniverses() throws Exception {
    Customer customer = ModelFactory.testCustomer();
    Universe vm = ModelFactory.createUniverse("vm", customer.getId());
    Universe k8s = ModelFactory.createUniverse("k8s", customer.getId(), CloudType.kubernetes);

    runMigration();

    AlertConfiguration configuration =
        severeConfiguration(customer, AlertTemplate.HEALTH_CHECK_NOT_RUNNING);
    assertEquals(
        Set.of(vm.getUniverseUUID().toString(), k8s.getUniverseUUID().toString()),
        definitionLabels(configuration, KnownAlertLabels.UNIVERSE_UUID));
  }

  @Test
  public void testSchedulerAlertForCustomer() throws Exception {
    Customer customer = ModelFactory.testCustomer();

    runMigration();

    AlertConfiguration configuration =
        severeConfiguration(customer, AlertTemplate.YBA_SCHEDULER_NOT_RUNNING);
    assertEquals(
        Set.of(customer.getUuid().toString()),
        definitionLabels(configuration, KnownAlertLabels.CUSTOMER_UUID));
  }

  private static AlertConfiguration severeConfiguration(Customer customer, AlertTemplate template) {
    List<AlertConfiguration> configurations =
        AlertConfiguration.createQueryByFilter(
                AlertConfigurationFilter.builder()
                    .customerUuid(customer.getUuid())
                    .template(template)
                    .build())
            .findList();
    assertEquals(1, configurations.size());
    AlertConfiguration configuration = configurations.get(0);
    assertTrue(configuration.isActive());
    assertEquals(Integer.valueOf(0), configuration.getDurationSec());
    assertEquals(Set.of(Severity.SEVERE), configuration.getThresholds().keySet());
    assertEquals(
        Condition.GREATER_THAN, configuration.getThresholds().get(Severity.SEVERE).getCondition());
    assertEquals(3D, configuration.getThresholds().get(Severity.SEVERE).getThreshold(), 0);
    return configuration;
  }

  private static Set<String> definitionLabels(
      AlertConfiguration configuration, KnownAlertLabels label) {
    return AlertDefinition.createQueryByFilter(
            AlertDefinitionFilter.builder().configurationUuid(configuration.getUuid()).build())
        .findList()
        .stream()
        .map(d -> d.getLabelValue(label))
        .collect(Collectors.toSet());
  }

  private static void runMigration() throws Exception {
    String sql =
        IOUtils.toString(
            V480__Health_Check_And_Scheduler_Not_Running_AlertsTest.class
                .getClassLoader()
                .getResourceAsStream(
                    "db/migration/default_/postgres/V480__Health_Check_And_Scheduler_Not_Running_Alerts.sql"),
            StandardCharsets.UTF_8);
    try (Connection connection = DB.getDefault().dataSource().getConnection();
        Statement statement = connection.createStatement()) {
      statement.execute(sql);
      if (!connection.getAutoCommit()) {
        connection.commit();
      }
    }
  }
}
