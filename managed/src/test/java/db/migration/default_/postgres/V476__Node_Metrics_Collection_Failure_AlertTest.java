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

public class V476__Node_Metrics_Collection_Failure_AlertTest extends FakeDBApplication {

  @Test
  public void testSevereConfigurationWithDefinitionsOnVmUniversesOnly() throws Exception {
    Customer customer = ModelFactory.testCustomer();
    Universe vm = ModelFactory.createUniverse("vm", customer.getId());
    ModelFactory.createUniverse("k8s", customer.getId(), CloudType.kubernetes);

    runMigration();

    List<AlertConfiguration> configurations =
        AlertConfiguration.createQueryByFilter(
                AlertConfigurationFilter.builder()
                    .customerUuid(customer.getUuid())
                    .template(AlertTemplate.NODE_METRICS_COLLECTION_FAILURE)
                    .build())
            .findList();
    assertEquals(1, configurations.size());
    AlertConfiguration configuration = configurations.get(0);
    assertTrue(configuration.isActive());
    assertEquals(Integer.valueOf(600), configuration.getDurationSec());
    assertEquals(Set.of(Severity.SEVERE), configuration.getThresholds().keySet());
    assertEquals(
        Condition.GREATER_THAN, configuration.getThresholds().get(Severity.SEVERE).getCondition());
    assertEquals(0D, configuration.getThresholds().get(Severity.SEVERE).getThreshold(), 0);

    Set<String> universes =
        AlertDefinition.createQueryByFilter(
                AlertDefinitionFilter.builder().configurationUuid(configuration.getUuid()).build())
            .findList()
            .stream()
            .map(d -> d.getLabelValue(KnownAlertLabels.UNIVERSE_UUID))
            .collect(Collectors.toSet());
    assertEquals(Set.of(vm.getUniverseUUID().toString()), universes);
  }

  private static void runMigration() throws Exception {
    String sql =
        IOUtils.toString(
            V476__Node_Metrics_Collection_Failure_AlertTest.class
                .getClassLoader()
                .getResourceAsStream(
                    "db/migration/default_/postgres/"
                        + "V476__Node_Metrics_Collection_Failure_Alert.sql"),
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
