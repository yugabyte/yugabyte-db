// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.models;

import static com.yugabyte.yw.common.AlertTemplate.ALERT_CONFIG_WRITING_FAILED;
import static com.yugabyte.yw.common.AlertTemplate.ALERT_QUERY_FAILED;
import static com.yugabyte.yw.common.AlertTemplate.HEALTH_CHECK_ERROR;
import static com.yugabyte.yw.common.AlertTemplate.MEMORY_CONSUMPTION;
import static com.yugabyte.yw.common.TestUtils.replaceFirstChar;
import static com.yugabyte.yw.common.ThrownMatcher.thrown;
import static org.hamcrest.CoreMatchers.containsString;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.nullValue;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.contains;
import static org.hamcrest.Matchers.containsInAnyOrder;
import static org.hamcrest.Matchers.empty;
import static org.hamcrest.Matchers.hasSize;
import static org.hamcrest.Matchers.notNullValue;
import static org.junit.Assert.fail;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.google.common.collect.ImmutableSet;
import com.yugabyte.yw.common.AlertTemplate;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.common.alerts.AlertTemplateVariableService;
import com.yugabyte.yw.common.alerts.AlertTemplateVariableServiceTest;
import com.yugabyte.yw.common.alerts.MaintenanceService;
import com.yugabyte.yw.common.alerts.impl.AlertTemplateService;
import com.yugabyte.yw.common.alerts.impl.AlertTemplateService.AlertTemplateDescription;
import com.yugabyte.yw.forms.filters.AlertConfigurationApiFilter;
import com.yugabyte.yw.models.AlertConfiguration.Severity;
import com.yugabyte.yw.models.AlertConfiguration.SortBy;
import com.yugabyte.yw.models.AlertConfiguration.TargetType;
import com.yugabyte.yw.models.common.Condition;
import com.yugabyte.yw.models.common.Unit;
import com.yugabyte.yw.models.filters.AlertConfigurationFilter;
import com.yugabyte.yw.models.filters.AlertConfigurationFilter.DestinationType;
import com.yugabyte.yw.models.filters.AlertDefinitionFilter;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import com.yugabyte.yw.models.paging.AlertConfigurationPagedQuery;
import com.yugabyte.yw.models.paging.PagedQuery.SortDirection;
import io.ebean.CallableSql;
import io.ebean.DB;
import java.io.IOException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Date;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.function.Consumer;
import java.util.function.Function;
import java.util.stream.Collectors;
import javax.persistence.PersistenceException;
import junitparams.JUnitParamsRunner;
import org.apache.commons.lang3.StringUtils;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;
import play.libs.Json;

@RunWith(JUnitParamsRunner.class)
public class AlertConfigurationTest extends FakeDBApplication {

  @Rule public MockitoRule mockitoRule = MockitoJUnit.rule();

  private Customer customer;
  private Universe universe;
  private Universe otherUniverse;
  private AlertDestination alertDestination;

  private MaintenanceService maintenanceService;
  private AlertTemplateService alertTemplateService;
  private AlertTemplateVariableService alertTemplateVariableService;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer("Customer");
    universe = ModelFactory.createUniverse();
    otherUniverse = ModelFactory.createUniverse("some other");

    maintenanceService = app.injector().instanceOf(MaintenanceService.class);
    alertTemplateService = app.injector().instanceOf(AlertTemplateService.class);
    alertTemplateVariableService = app.injector().instanceOf(AlertTemplateVariableService.class);

    alertDestination =
        ModelFactory.createAlertDestination(
            customer.getUuid(),
            "My Route",
            Collections.singletonList(
                ModelFactory.createEmailChannel(customer.getUuid(), "Test channel")));
  }

  @Test
  public void testSerialization() throws IOException {
    String initial = TestUtils.readResource("alert/alert_configuration.json");

    JsonNode initialJson = Json.parse(initial);

    AlertConfiguration configuration = Json.fromJson(initialJson, AlertConfiguration.class);

    JsonNode resultJson = Json.toJson(configuration);

    assertThat(resultJson, equalTo(initialJson));
  }

  @Test
  public void testAddAndQueryByUUID() {
    AlertConfiguration configuration = createTestConfiguration();

    AlertConfiguration queriedDefinition = alertConfigurationService.get(configuration.getUuid());

    assertTestConfiguration(queriedDefinition);

    List<AlertDefinition> definitions =
        alertDefinitionService.list(
            AlertDefinitionFilter.builder().configurationUuid(queriedDefinition.getUuid()).build());

    assertThat(definitions, hasSize(2));
  }

  @Test
  public void testConfigurationUnderMaintenanceWindow() {
    AlertConfigurationApiFilter alertConfigurationApiFilter = new AlertConfigurationApiFilter();
    alertConfigurationApiFilter.setTarget(
        new AlertConfigurationTarget()
            .setAll(false)
            .setUuids(ImmutableSet.of(universe.getUniverseUUID())));
    AlertConfigurationApiFilter alertConfigurationApiFilter2 = new AlertConfigurationApiFilter();
    alertConfigurationApiFilter2.setTarget(
        new AlertConfigurationTarget()
            .setAll(false)
            .setUuids(ImmutableSet.of(otherUniverse.getUniverseUUID())));
    MaintenanceWindow maintenanceWindow =
        ModelFactory.createMaintenanceWindow(
            customer.getUuid(),
            window ->
                window
                    .setUuid(replaceFirstChar(window.getUuid(), 'a'))
                    .setAlertConfigurationFilter(alertConfigurationApiFilter));
    MaintenanceWindow maintenanceWindow2 =
        ModelFactory.createMaintenanceWindow(
            customer.getUuid(),
            window ->
                window
                    .setUuid(replaceFirstChar(window.getUuid(), 'b'))
                    .setAlertConfigurationFilter(alertConfigurationApiFilter2));

    AlertConfiguration configuration = createTestConfiguration();
    configuration.addMaintenanceWindowUuid(maintenanceWindow2.getUuid());
    configuration.addMaintenanceWindowUuid(maintenanceWindow.getUuid());
    alertConfigurationService.save(configuration);

    AlertConfiguration queriedConfiguration =
        alertConfigurationService.get(configuration.getUuid());

    assertThat(
        queriedConfiguration.getMaintenanceWindowUuidsSet(),
        contains(maintenanceWindow.getUuid(), maintenanceWindow2.getUuid()));

    List<AlertDefinition> definitions =
        alertDefinitionService.list(
            AlertDefinitionFilter.builder()
                .configurationUuid(queriedConfiguration.getUuid())
                .build());

    assertThat(definitions, hasSize(2));
    Map<UUID, AlertDefinition> definitionMap =
        definitions.stream()
            .collect(
                Collectors.toMap(
                    definition ->
                        UUID.fromString(definition.getLabelValue(KnownAlertLabels.SOURCE_UUID)),
                    Function.identity()));
    assertThat(
        definitionMap
            .get(universe.getUniverseUUID())
            .getLabelValue(KnownAlertLabels.MAINTENANCE_WINDOW_UUIDS),
        equalTo(maintenanceWindow.getUuid().toString()));
    assertThat(
        definitionMap
            .get(otherUniverse.getUniverseUUID())
            .getLabelValue(KnownAlertLabels.MAINTENANCE_WINDOW_UUIDS),
        equalTo(maintenanceWindow2.getUuid().toString()));
  }

  @Test
  public void testUpdateAndQueryTarget() {
    AlertConfiguration configuration = createTestConfiguration();

    AlertConfigurationThreshold severeThreshold =
        new AlertConfigurationThreshold().setCondition(Condition.GREATER_THAN).setThreshold(90D);
    AlertConfigurationThreshold warningThreshold =
        new AlertConfigurationThreshold().setCondition(Condition.GREATER_THAN).setThreshold(80D);
    Map<AlertConfiguration.Severity, AlertConfigurationThreshold> thresholds =
        ImmutableMap.of(
            AlertConfiguration.Severity.SEVERE, severeThreshold,
            AlertConfiguration.Severity.WARNING, warningThreshold);
    AlertConfigurationTarget target =
        new AlertConfigurationTarget()
            .setAll(false)
            .setUuids(ImmutableSet.of(universe.getUniverseUUID()));
    configuration.setTarget(target);
    configuration.setThresholds(thresholds);
    alertConfigurationService.save(configuration);

    AlertConfiguration updated =
        alertConfigurationService
            .list(
                AlertConfigurationFilter.builder()
                    .target(
                        new AlertConfigurationTarget()
                            .setAll(false)
                            .setUuids(Collections.singleton(universe.getUniverseUUID())))
                    .build())
            .get(0);

    assertThat(updated.getTarget(), equalTo(target));
    assertThat(
        updated.getThresholds().get(AlertConfiguration.Severity.SEVERE), equalTo(severeThreshold));
    assertThat(
        updated.getThresholds().get(AlertConfiguration.Severity.WARNING),
        equalTo(warningThreshold));

    List<AlertDefinition> definitions =
        alertDefinitionService.list(
            AlertDefinitionFilter.builder().configurationUuid(updated.getUuid()).build());

    assertThat(definitions, hasSize(1));
  }

  @Test
  public void testFilter() {
    AlertConfiguration configuration = createTestConfiguration();

    AlertConfiguration configuration2 =
        alertConfigurationService
            .createConfigurationTemplate(customer, HEALTH_CHECK_ERROR)
            .getDefaultConfiguration();
    AlertConfigurationThreshold warningThreshold =
        new AlertConfigurationThreshold().setCondition(Condition.GREATER_THAN).setThreshold(1D);
    configuration2.setThresholds(ImmutableMap.of(Severity.WARNING, warningThreshold));
    AlertConfigurationTarget target =
        new AlertConfigurationTarget()
            .setAll(false)
            .setUuids(ImmutableSet.of(universe.getUniverseUUID()));
    configuration2.setTarget(target);
    configuration2.setActive(false);
    alertConfigurationService.save(configuration2);

    AlertConfiguration platformConfiguration =
        alertConfigurationService
            .createConfigurationTemplate(customer, ALERT_QUERY_FAILED)
            .getDefaultConfiguration();
    platformConfiguration.setDefaultDestination(false);
    alertConfigurationService.save(platformConfiguration);

    // Empty filter
    AlertConfigurationFilter filter = AlertConfigurationFilter.builder().build();
    assertFind(filter, configuration, configuration2, platformConfiguration);

    // Name filter
    filter = AlertConfigurationFilter.builder().name("Memory Consumption").build();
    assertFind(filter, configuration);

    // Name starts with
    filter = AlertConfigurationFilter.builder().name("Memory").build();
    assertFind(filter, configuration);

    // Name contains case insensitive
    filter = AlertConfigurationFilter.builder().name("cons").build();
    assertFind(filter, configuration);

    // Active filter
    filter = AlertConfigurationFilter.builder().active(true).build();
    assertFind(filter, configuration, platformConfiguration);

    // Target type filter
    filter = AlertConfigurationFilter.builder().targetType(TargetType.UNIVERSE).build();
    assertFind(filter, configuration, configuration2);

    // Target filter
    filter =
        AlertConfigurationFilter.builder()
            .targetType(TargetType.UNIVERSE)
            .target(
                new AlertConfigurationTarget()
                    .setAll(false)
                    .setUuids(ImmutableSet.of(universe.getUniverseUUID())))
            .build();
    assertFind(filter, configuration, configuration2);

    filter =
        AlertConfigurationFilter.builder()
            .targetType(TargetType.UNIVERSE)
            .target(
                new AlertConfigurationTarget()
                    .setAll(false)
                    .setUuids(ImmutableSet.of(UUID.randomUUID())))
            .build();
    assertFind(filter, configuration);

    filter =
        AlertConfigurationFilter.builder()
            .target(new AlertConfigurationTarget().setAll(true))
            .build();
    assertFind(filter, configuration, platformConfiguration);

    // Template filter
    filter = AlertConfigurationFilter.builder().template(MEMORY_CONSUMPTION).build();
    assertFind(filter, configuration);

    // Severity filter
    filter = AlertConfigurationFilter.builder().severity(Severity.WARNING).build();
    assertFind(filter, configuration2);

    // Destination Type filter
    filter =
        AlertConfigurationFilter.builder()
            .destinationType(DestinationType.SELECTED_DESTINATION)
            .build();
    assertFind(filter, configuration);

    filter =
        AlertConfigurationFilter.builder()
            .destinationType(DestinationType.DEFAULT_DESTINATION)
            .build();
    assertFind(filter, configuration2);

    filter =
        AlertConfigurationFilter.builder().destinationType(DestinationType.NO_DESTINATION).build();
    assertFind(filter, platformConfiguration);

    // Destination UUID filter
    filter = AlertConfigurationFilter.builder().destinationUuid(alertDestination.getUuid()).build();
    assertFind(filter, configuration);
  }

  private void assertFind(AlertConfigurationFilter filter, AlertConfiguration... configurations) {
    List<AlertConfiguration> queried = alertConfigurationService.list(filter);
    assertThat(queried, hasSize(configurations.length));
    assertThat(queried, containsInAnyOrder(configurations));
  }

  @Test
  public void testSort() {
    Date now = new Date();
    AlertConfiguration configuration =
        alertConfigurationService
            .createConfigurationTemplate(customer, MEMORY_CONSUMPTION)
            .getDefaultConfiguration();
    configuration.setDestinationUUID(alertDestination.getUuid());
    configuration.setDefaultDestination(false);
    configuration.setCreateTime(Date.from(now.toInstant().minusSeconds(5)));
    configuration.generateUUID();
    configuration.setUuid(replaceFirstChar(configuration.getUuid(), 'a'));
    configuration.save();

    AlertConfiguration configuration2 =
        alertConfigurationService
            .createConfigurationTemplate(customer, HEALTH_CHECK_ERROR)
            .getDefaultConfiguration();
    AlertConfigurationThreshold warningThreshold =
        new AlertConfigurationThreshold().setCondition(Condition.GREATER_THAN).setThreshold(1D);
    configuration2.setThresholds(ImmutableMap.of(Severity.WARNING, warningThreshold));
    AlertConfigurationTarget target =
        new AlertConfigurationTarget()
            .setAll(false)
            .setUuids(ImmutableSet.of(universe.getUniverseUUID()));
    configuration2.setTarget(target);
    configuration2.setActive(false);
    configuration2.setCreateTime(Date.from(now.toInstant().minusSeconds(2)));
    configuration2.generateUUID();
    configuration2.setUuid(replaceFirstChar(configuration2.getUuid(), 'b'));
    configuration2.save();

    AlertConfiguration platformConfiguration =
        alertConfigurationService
            .createConfigurationTemplate(customer, ALERT_QUERY_FAILED)
            .getDefaultConfiguration();
    platformConfiguration.setDefaultDestination(false);
    platformConfiguration.setCreateTime(now);
    platformConfiguration.generateUUID();
    platformConfiguration.setUuid(replaceFirstChar(platformConfiguration.getUuid(), 'c'));
    platformConfiguration.save();

    AlertDefinition definition =
        ModelFactory.createAlertDefinition(customer, universe, configuration);
    ModelFactory.createAlert(customer, universe, definition);

    assertSort(
        SortBy.uuid, SortDirection.ASC, configuration, configuration2, platformConfiguration);
    assertSort(
        SortBy.name, SortDirection.DESC, configuration, configuration2, platformConfiguration);
    assertSort(
        SortBy.active, SortDirection.ASC, configuration2, configuration, platformConfiguration);
    assertSort(
        SortBy.targetType,
        SortDirection.DESC,
        configuration,
        configuration2,
        platformConfiguration);
    assertSort(
        SortBy.createTime,
        SortDirection.DESC,
        platformConfiguration,
        configuration2,
        configuration);
    assertSort(
        SortBy.template, SortDirection.ASC, platformConfiguration, configuration2, configuration);
    assertSort(
        SortBy.severity, SortDirection.ASC, configuration2, configuration, platformConfiguration);
    assertSort(
        SortBy.destination,
        SortDirection.DESC,
        configuration2,
        platformConfiguration,
        configuration);
    assertSort(
        SortBy.alertCount, SortDirection.ASC, configuration2, platformConfiguration, configuration);
  }

  private void assertSort(
      SortBy sortBy, SortDirection direction, AlertConfiguration... configurations) {
    AlertConfigurationFilter filter = AlertConfigurationFilter.builder().build();
    AlertConfigurationPagedQuery query = new AlertConfigurationPagedQuery();
    query.setFilter(filter);
    query.setSortBy(sortBy);
    query.setDirection(direction);
    query.setLimit(3);
    List<AlertConfiguration> queried = alertConfigurationService.pagedList(query).getEntities();
    assertThat(queried, hasSize(configurations.length));
    assertThat(queried, contains(configurations));
  }

  @Test
  public void testDelete() {
    AlertConfiguration configuration = createTestConfiguration();

    alertConfigurationService.delete(configuration.getUuid());

    AlertConfiguration queriedConfiguration =
        alertConfigurationService.get(configuration.getUuid());

    assertThat(queriedConfiguration, nullValue());

    List<AlertDefinition> definitions =
        alertDefinitionService.list(
            AlertDefinitionFilter.builder().configurationUuid(configuration.getUuid()).build());

    assertThat(definitions, hasSize(0));
  }

  @Test
  public void testHandleUniverseRemoval() {
    AlertConfiguration configuration = createTestConfiguration();

    AlertConfiguration configuration2 = createTestConfiguration();
    configuration2.setTarget(
        new AlertConfigurationTarget()
            .setAll(false)
            .setUuids(ImmutableSet.of(universe.getUniverseUUID())));

    alertConfigurationService.save(configuration2);

    AlertDefinitionFilter definitionFilter =
        AlertDefinitionFilter.builder()
            .label(KnownAlertLabels.SOURCE_UUID, universe.getUniverseUUID().toString())
            .build();

    List<AlertDefinition> universeDefinitions = alertDefinitionService.list(definitionFilter);

    assertThat(universeDefinitions, hasSize(2));

    universe.delete();
    alertConfigurationService.handleSourceRemoval(
        customer.getUuid(), TargetType.UNIVERSE, universe.getUniverseUUID());

    AlertConfiguration updatedConfiguration2 =
        alertConfigurationService.get(configuration2.getUuid());

    assertThat(updatedConfiguration2, nullValue());
    universeDefinitions = alertDefinitionService.list(definitionFilter);

    assertThat(universeDefinitions, empty());
  }

  @Test
  public void testManageDefinitionsHandlesDuplicates() {
    AlertConfiguration configuration = createTestConfiguration();

    AlertDefinitionFilter definitionFilter =
        AlertDefinitionFilter.builder()
            .label(KnownAlertLabels.SOURCE_UUID, universe.getUniverseUUID().toString())
            .build();

    List<AlertDefinition> universeDefinitions = alertDefinitionService.list(definitionFilter);

    assertThat(universeDefinitions, hasSize(1));

    AlertDefinition definition = universeDefinitions.get(0);
    AlertDefinition duplicate =
        new AlertDefinition()
            .setCustomerUUID(customer.getUuid())
            .setConfigurationUUID(definition.getConfigurationUUID())
            .setLabels(
                definition.getLabels().stream()
                    .map(label -> new AlertDefinitionLabel(label.getName(), label.getValue()))
                    .collect(Collectors.toList()));
    alertDefinitionService.save(duplicate);

    universeDefinitions = alertDefinitionService.list(definitionFilter);

    assertThat(universeDefinitions, hasSize(2));

    alertConfigurationService.save(configuration);

    universeDefinitions = alertDefinitionService.list(definitionFilter);

    // Duplicate definition was removed
    assertThat(universeDefinitions, hasSize(1));
  }

  @Test
  public void testUpdateFromMultipleThreads() throws InterruptedException {
    AlertConfiguration configuration = createTestConfiguration();

    AlertConfiguration configuration2 = createTestConfiguration();
    configuration2.setTarget(
        new AlertConfigurationTarget()
            .setAll(false)
            .setUuids(ImmutableSet.of(universe.getUniverseUUID())));

    alertConfigurationService.save(configuration2);

    AlertDefinitionFilter definitionFilter =
        AlertDefinitionFilter.builder()
            .configurationUuid(configuration.getUuid())
            .configurationUuid(configuration2.getUuid())
            .build();

    List<AlertDefinition> universeDefinitions = alertDefinitionService.list(definitionFilter);

    assertThat(universeDefinitions, hasSize(3));

    Universe universe3 = ModelFactory.createUniverse("one more", customer.getId());
    Universe universe4 = ModelFactory.createUniverse("another more", customer.getId());

    ExecutorService executor = Executors.newFixedThreadPool(2);
    List<Future<Void>> futures = new ArrayList<>();
    for (int i = 0; i < 2; i++) {
      futures.add(
          executor.submit(
              () -> {
                alertConfigurationService.save(
                    customer.getUuid(), ImmutableList.of(configuration, configuration2));
                return null;
              }));
    }
    for (Future<Void> future : futures) {
      try {
        future.get();
      } catch (ExecutionException e) {
        fail("Exception occurred in worker: " + e);
      }
    }
    executor.shutdown();

    universeDefinitions = alertDefinitionService.list(definitionFilter);

    assertThat(universeDefinitions, hasSize(5));
  }

  @Test
  public void testValidation() {
    UUID randomUUID = UUID.randomUUID();

    testValidationCreate(
        configuration -> configuration.setCustomerUUID(null),
        "errorJson: {\"customerUUID\":[\"must not be null\"]}");

    testValidationCreate(
        configuration -> configuration.setName(null),
        "errorJson: {\"name\":[\"must not be null\"]}");

    testValidationCreate(
        configuration -> configuration.setName(StringUtils.repeat("a", 1001)),
        "errorJson: {\"name\":[\"size must be between 1 and 1000\"]}");

    testValidationCreate(
        configuration -> configuration.setTargetType(null),
        "errorJson: {\"targetType\":[\"must not be null\"]}");

    testValidationCreate(
        configuration -> configuration.setTarget(null),
        "errorJson: {\"target\":[\"must not be null\"]}");

    testValidationCreate(
        configuration ->
            configuration.setTarget(
                new AlertConfigurationTarget().setAll(true).setUuids(ImmutableSet.of(randomUUID))),
        "errorJson: {\"target\":[\"should select either all entries or particular UUIDs\"]}");

    testValidationCreate(
        configuration ->
            configuration.setTarget(
                new AlertConfigurationTarget().setUuids(ImmutableSet.of(randomUUID))),
        "errorJson: {\"target.uuids\":[\"universe(s) missing: " + randomUUID + "\"]}");

    testValidationCreate(
        configuration ->
            configuration.setTarget(
                new AlertConfigurationTarget().setUuids(Collections.singleton(null))),
        "errorJson: {\"target.uuids\":[\"can't have null entries\"]}");

    testValidationCreate(
        configuration ->
            configuration
                .setTargetType(TargetType.PLATFORM)
                .setTarget(new AlertConfigurationTarget().setUuids(ImmutableSet.of(randomUUID))),
        "errorJson: {\"target.uuids\":[\"PLATFORM configuration can't have target uuids\"]}");

    testValidationCreate(
        configuration -> configuration.setTemplate(null),
        "errorJson: {\"template\":[\"must not be null\"]}");

    testValidationCreate(
        configuration -> configuration.setTemplate(ALERT_CONFIG_WRITING_FAILED),
        "errorJson: {\"\":[\"target type should be consistent with template\"]}");

    testValidationCreate(
        configuration -> configuration.setThresholds(null),
        "errorJson: {\"thresholds\":[\"must not be null\"]}");

    testValidationCreate(
        configuration -> configuration.setDestinationUUID(randomUUID),
        "errorJson: {\"destinationUUID\":[\"alert destination " + randomUUID + " is missing\"]}");

    testValidationCreate(
        configuration ->
            configuration
                .setDestinationUUID(alertDestination.getUuid())
                .setDefaultDestination(true),
        "errorJson: {\"\":[\"destination can't be filled "
            + "in case default destination is selected\"]}");

    testValidationCreate(
        configuration -> configuration.setThresholdUnit(null),
        "errorJson: {\"thresholdUnit\":[\"must not be null\"]}");

    testValidationCreate(
        configuration -> configuration.setThresholdUnit(Unit.STATUS),
        "errorJson: {\"thresholdUnit\":[\"incompatible with alert definition template\"]}");

    testValidationCreate(
        configuration -> configuration.getThresholds().get(Severity.SEVERE).setCondition(null),
        "errorJson: {\"thresholds[SEVERE].condition\":[\"must not be null\"]}");

    testValidationCreate(
        configuration -> configuration.getThresholds().get(Severity.SEVERE).setThreshold(null),
        "errorJson: {\"thresholds[SEVERE].threshold\":[\"must not be null\"]}");

    testValidationCreate(
        configuration -> configuration.getThresholds().get(Severity.SEVERE).setThreshold(-100D),
        "errorJson: {\"thresholds[SEVERE].threshold\":[\"can't be less than 0\"]}");

    testValidationCreate(
        configuration -> configuration.setDurationSec(-1),
        "errorJson: {\"durationSec\":[\"must be greater than or equal to 0\"]}");

    testValidationCreate(
        configuration -> configuration.setLabels(ImmutableMap.of("test", "some_value")),
        "errorJson: {\"labels\":[\"variable 'test' does not exist\"]}");

    AlertTemplateVariable variable =
        AlertTemplateVariableServiceTest.createTestVariable(customer.getUuid(), "test");
    alertTemplateVariableService.save(variable);
    testValidationCreate(
        configuration -> configuration.setLabels(ImmutableMap.of("test", "some_value")),
        "errorJson: {\"labels\":[\"variable 'test' does not have value 'some_value'\"]}");

    testValidationUpdate(
        configuration -> configuration.setCustomerUUID(randomUUID).setDestinationUUID(null),
        "errorJson: {\"customerUUID\":[\"can't change for configuration 'Memory Consumption'\"]}");
  }

  @Test
  public void testTransactions() {
    AlertConfiguration configuration = createTestConfiguration();
    CallableSql dropTable = DB.createCallableSql("drop table maintenance_window");
    DB.getDefault().execute(dropTable);

    configuration.setMaintenanceWindowUuids(ImmutableSet.of(UUID.randomUUID()));

    assertThat(
        () -> alertConfigurationService.save(configuration),
        thrown(
            PersistenceException.class, containsString("Table \"MAINTENANCE_WINDOW\" not found")));

    AlertConfiguration updated = alertConfigurationService.get(configuration.getUuid());
    assertThat(updated.getMaintenanceWindowUuids(), nullValue());
  }

  private void testValidationCreate(Consumer<AlertConfiguration> modifier, String expectedMessage) {
    testValidation(modifier, expectedMessage, true);
  }

  private void testValidationUpdate(Consumer<AlertConfiguration> modifier, String expectedMessage) {
    testValidation(modifier, expectedMessage, false);
  }

  private void testValidation(
      Consumer<AlertConfiguration> modifier, String expectedMessage, boolean create) {
    AlertConfiguration configuration = createTestConfiguration();
    if (create) {
      alertConfigurationService.delete(configuration.getUuid());
      configuration.setUuid(null);
    }
    modifier.accept(configuration);

    assertThat(
        () -> alertConfigurationService.save(configuration),
        thrown(PlatformServiceException.class, expectedMessage));
  }

  private AlertConfiguration createTestConfiguration() {
    AlertConfiguration configuration =
        alertConfigurationService
            .createConfigurationTemplate(customer, MEMORY_CONSUMPTION)
            .getDefaultConfiguration();
    configuration.setDestinationUUID(alertDestination.getUuid());
    configuration.setDefaultDestination(false);
    return alertConfigurationService.save(configuration);
  }

  private void assertTestConfiguration(AlertConfiguration configuration) {
    AlertTemplate template = MEMORY_CONSUMPTION;
    AlertTemplateDescription templateDescription =
        alertTemplateService.getTemplateDescription(template);
    assertThat(configuration.getCustomerUUID(), equalTo(customer.getUuid()));
    assertThat(configuration.getName(), equalTo(templateDescription.getName()));
    assertThat(configuration.getDescription(), equalTo(templateDescription.getDescription()));
    assertThat(configuration.getTemplate(), equalTo(template));
    assertThat(
        configuration.getDurationSec(), equalTo(templateDescription.getDefaultDurationSec()));
    assertThat(configuration.getDestinationUUID(), equalTo(alertDestination.getUuid()));
    assertThat(configuration.getTargetType(), equalTo(templateDescription.getTargetType()));
    assertThat(configuration.getTarget(), equalTo(new AlertConfigurationTarget().setAll(true)));
    assertThat(
        configuration.getThresholds().get(AlertConfiguration.Severity.SEVERE),
        equalTo(
            new AlertConfigurationThreshold()
                .setThreshold(90D)
                .setCondition(Condition.GREATER_THAN)));
    assertThat(configuration.getThresholds().get(AlertConfiguration.Severity.WARNING), nullValue());
    assertThat(configuration.getUuid(), notNullValue());
    assertThat(configuration.getCreateTime(), notNullValue());
  }
}
