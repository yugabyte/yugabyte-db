// Copyright (c) YugabyteDB, Inc.

package db.migration.default_.postgres;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.AlertTemplate;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.AlertConfiguration;
import com.yugabyte.yw.models.AlertConfiguration.Severity;
import com.yugabyte.yw.models.AlertConfigurationThreshold;
import com.yugabyte.yw.models.AlertDefinition;
import com.yugabyte.yw.models.AlertTemplateSettings;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.common.Condition;
import com.yugabyte.yw.models.filters.AlertConfigurationFilter;
import com.yugabyte.yw.models.filters.AlertDefinitionFilter;
import com.yugabyte.yw.models.filters.AlertTemplateSettingsFilter;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import io.ebean.DB;
import java.nio.charset.StandardCharsets;
import java.sql.Connection;
import java.sql.Statement;
import java.util.Date;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.stream.Collectors;
import org.apache.commons.io.IOUtils;
import org.junit.Test;

public class V472__Kubernetes_Universe_AlertsTest extends FakeDBApplication {

  @Test
  public void testTwinsCopyVmSettingsAndVmDefinitionsLeaveKubernetes() throws Exception {
    Customer customer = ModelFactory.testCustomer();
    Universe k8s = ModelFactory.createUniverse("k8s", customer.getId(), CloudType.kubernetes);
    Universe vm = ModelFactory.createUniverse("vm", customer.getId());
    AlertConfiguration cpu =
        vmConfiguration(customer, AlertTemplate.NODE_CPU_USAGE, true)
            .setThresholds(
                Map.of(
                    Severity.SEVERE,
                    new AlertConfigurationThreshold()
                        .setCondition(Condition.GREATER_THAN)
                        .setThreshold(60D)))
            .setDurationSec(120);
    cpu.save();
    AlertConfiguration disk = vmConfiguration(customer, AlertTemplate.NODE_DISK_USAGE, false);
    AlertConfiguration memory =
        vmConfiguration(customer, AlertTemplate.MEMORY_CONSUMPTION, true).setDurationSec(300);
    memory.save();
    // Before the upgrade the VM alerts had definitions on every universe.
    for (AlertConfiguration configuration : List.of(cpu, disk, memory)) {
      ModelFactory.createAlertDefinition(customer, k8s, configuration);
      ModelFactory.createAlertDefinition(customer, vm, configuration);
    }

    runMigration();

    AlertConfiguration cpuTwin = only(customer, AlertTemplate.DB_K8S_CPU_USAGE);
    assertEquals(Integer.valueOf(120), cpuTwin.getDurationSec());
    assertEquals(60D, cpuTwin.getThresholds().get(Severity.SEVERE).getThreshold(), 0);
    assertTrue(cpuTwin.isActive());
    assertFalse(only(customer, AlertTemplate.DB_K8S_DATA_VOLUME_USAGE).isActive());
    assertEquals(
        Integer.valueOf(300), only(customer, AlertTemplate.DB_K8S_MEMORY_USAGE).getDurationSec());
    for (AlertTemplate twin :
        List.of(
            AlertTemplate.DB_K8S_CPU_USAGE,
            AlertTemplate.DB_K8S_DATA_VOLUME_USAGE,
            AlertTemplate.DB_K8S_MEMORY_USAGE)) {
      assertEquals(Set.of(k8s.getUniverseUUID().toString()), universes(only(customer, twin)));
    }
    for (AlertConfiguration configuration : List.of(cpu, disk, memory)) {
      assertEquals(Set.of(vm.getUniverseUUID().toString()), universes(configuration));
    }
  }

  @Test
  public void testDefaultsWithoutAnAllUniverseVmConfiguration() throws Exception {
    Customer customer = ModelFactory.testCustomer();

    runMigration();

    AlertConfiguration cpuTwin = only(customer, AlertTemplate.DB_K8S_CPU_USAGE);
    assertTrue(cpuTwin.isActive());
    assertEquals(95D, cpuTwin.getThresholds().get(Severity.SEVERE).getThreshold(), 0);
    assertEquals(90D, cpuTwin.getThresholds().get(Severity.WARNING).getThreshold(), 0);
    assertEquals(
        70D,
        only(customer, AlertTemplate.DB_K8S_DATA_VOLUME_USAGE)
            .getThresholds()
            .get(Severity.SEVERE)
            .getThreshold(),
        0);
    assertTrue(configurations(customer, AlertTemplate.DB_K8S_MEMORY_USAGE).isEmpty());
  }

  @Test
  public void testUnfinishedUniversesAndUuidTargets() throws Exception {
    Customer customer = ModelFactory.testCustomer();
    Universe k8s = ModelFactory.createUniverse("k8s", customer.getId(), CloudType.kubernetes);
    Universe creating =
        ModelFactory.createUniverse("creating", customer.getId(), CloudType.kubernetes);
    creating =
        Universe.saveDetails(
            creating.getUniverseUUID(), u -> u.getUniverseDetails().creationSucceeded = false);
    AlertConfiguration byUuid =
        ModelFactory.createAlertConfiguration(
            customer, k8s, c -> c.setTemplate(AlertTemplate.NODE_CPU_USAGE));
    ModelFactory.createAlertDefinition(customer, k8s, byUuid);

    runMigration();

    assertEquals(
        Set.of(k8s.getUniverseUUID().toString()),
        universes(only(customer, AlertTemplate.DB_K8S_CPU_USAGE)));
    assertTrue(universes(byUuid).isEmpty());
  }

  @Test
  public void testCustomersStayIndependent() throws Exception {
    Customer first = ModelFactory.testCustomer("first", "first@customer.com");
    Customer second = ModelFactory.testCustomer("second", "second@customer.com");
    Universe firstK8s =
        ModelFactory.createUniverse("first-k8s", first.getId(), CloudType.kubernetes);
    Universe secondK8s =
        ModelFactory.createUniverse("second-k8s", second.getId(), CloudType.kubernetes);
    vmConfiguration(first, AlertTemplate.NODE_DISK_USAGE, false);

    runMigration();

    assertFalse(only(first, AlertTemplate.DB_K8S_DATA_VOLUME_USAGE).isActive());
    assertTrue(only(second, AlertTemplate.DB_K8S_DATA_VOLUME_USAGE).isActive());
    assertEquals(
        Set.of(firstK8s.getUniverseUUID().toString()),
        universes(only(first, AlertTemplate.DB_K8S_DATA_VOLUME_USAGE)));
    assertEquals(
        Set.of(secondK8s.getUniverseUUID().toString()),
        universes(only(second, AlertTemplate.DB_K8S_DATA_VOLUME_USAGE)));
  }

  @Test
  public void testTemplateSettingsAreCopied() throws Exception {
    Customer customer = ModelFactory.testCustomer();
    templateSettings(customer, AlertTemplate.NODE_CPU_USAGE, Map.of("team", "db"));
    templateSettings(customer, AlertTemplate.NODE_DISK_USAGE, Map.of("team", "storage"));
    templateSettings(customer, AlertTemplate.DB_K8S_DATA_VOLUME_USAGE, Map.of("keep", "me"));
    templateSettings(customer, AlertTemplate.MEMORY_CONSUMPTION, Map.of("team", "memory"));

    runMigration();

    Map<String, Map<String, String>> labels =
        AlertTemplateSettings.createQueryByFilter(
                AlertTemplateSettingsFilter.builder().customerUuid(customer.getUuid()).build())
            .findList()
            .stream()
            .collect(
                Collectors.toMap(
                    AlertTemplateSettings::getTemplate, AlertTemplateSettings::getLabels));
    assertEquals(Map.of("team", "db"), labels.get(AlertTemplate.DB_K8S_CPU_USAGE.name()));
    assertEquals(Map.of("keep", "me"), labels.get(AlertTemplate.DB_K8S_DATA_VOLUME_USAGE.name()));
    // No Memory Consumption configuration, so no memory twin and no copied settings.
    assertFalse(labels.containsKey(AlertTemplate.DB_K8S_MEMORY_USAGE.name()));
  }

  @Test
  public void testActiveConfigurationAndItsLabelsWin() throws Exception {
    Customer customer = ModelFactory.testCustomer();
    AlertConfiguration olderInactive =
        vmConfiguration(customer, AlertTemplate.NODE_CPU_USAGE, false)
            .setCreateTime(new Date(System.currentTimeMillis() - 60_000))
            .setDurationSec(30)
            .setLabels(Map.of("source", "inactive"));
    olderInactive.save();
    AlertConfiguration active =
        vmConfiguration(customer, AlertTemplate.NODE_CPU_USAGE, true)
            .setDurationSec(90)
            .setLabels(Map.of("source", "active"));
    active.save();

    runMigration();

    AlertConfiguration cpuTwin = only(customer, AlertTemplate.DB_K8S_CPU_USAGE);
    assertTrue(cpuTwin.isActive());
    assertEquals(Integer.valueOf(90), cpuTwin.getDurationSec());
    assertEquals(Map.of("source", "active"), cpuTwin.getLabels());
  }

  private static void templateSettings(
      Customer customer, AlertTemplate template, Map<String, String> labels) {
    new AlertTemplateSettings()
        .generateUUID()
        .setCustomerUUID(customer.getUuid())
        .setTemplate(template.name())
        .setCreateTime(new Date())
        .setLabels(labels)
        .save();
  }

  private static AlertConfiguration vmConfiguration(
      Customer customer, AlertTemplate template, boolean active) {
    return ModelFactory.createAlertConfiguration(
        customer, null, c -> c.setTemplate(template).setActive(active));
  }

  private static void runMigration() throws Exception {
    String sql =
        IOUtils.toString(
            V472__Kubernetes_Universe_AlertsTest.class
                .getClassLoader()
                .getResourceAsStream(
                    "db/migration/default_/postgres/V472__Kubernetes_Universe_Alerts.sql"),
            StandardCharsets.UTF_8);
    try (Connection connection = DB.getDefault().dataSource().getConnection();
        Statement statement = connection.createStatement()) {
      statement.execute(sql);
      if (!connection.getAutoCommit()) {
        connection.commit();
      }
    }
  }

  private static List<AlertConfiguration> configurations(
      Customer customer, AlertTemplate template) {
    return AlertConfiguration.createQueryByFilter(
            AlertConfigurationFilter.builder()
                .customerUuid(customer.getUuid())
                .template(template)
                .build())
        .findList();
  }

  private static AlertConfiguration only(Customer customer, AlertTemplate template) {
    List<AlertConfiguration> found = configurations(customer, template);
    assertEquals(template.name(), 1, found.size());
    return found.get(0);
  }

  private static Set<String> universes(AlertConfiguration configuration) {
    return AlertDefinition.createQueryByFilter(
            AlertDefinitionFilter.builder().configurationUuid(configuration.getUuid()).build())
        .findList()
        .stream()
        .map(d -> d.getLabelValue(KnownAlertLabels.UNIVERSE_UUID))
        .collect(Collectors.toSet());
  }
}
