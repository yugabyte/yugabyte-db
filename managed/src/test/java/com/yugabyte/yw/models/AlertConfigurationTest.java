// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.models;

import static com.yugabyte.yw.common.ThrownMatcher.thrown;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.nullValue;
import static org.hamcrest.MatcherAssert.assertThat;
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
import com.yugabyte.yw.common.alerts.AlertConfigurationService;
import com.yugabyte.yw.common.alerts.AlertDefinitionService;
import com.yugabyte.yw.common.alerts.AlertService;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.models.AlertConfiguration.Severity;
import com.yugabyte.yw.models.AlertConfiguration.TargetType;
import com.yugabyte.yw.models.common.Condition;
import com.yugabyte.yw.models.common.Unit;
import com.yugabyte.yw.models.filters.AlertConfigurationFilter;
import com.yugabyte.yw.models.filters.AlertDefinitionFilter;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.function.Consumer;
import java.util.stream.Collectors;
import org.apache.commons.io.IOUtils;
import org.apache.commons.lang3.StringUtils;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class AlertConfigurationTest extends FakeDBApplication {

  private Customer customer;
  private Universe universe;
  private AlertDestination alertDestination;

  private final AlertService alertService = new AlertService();
  private final AlertDefinitionService alertDefinitionService =
      new AlertDefinitionService(alertService);
  private AlertConfigurationService alertConfigurationService;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer("Customer");
    universe = ModelFactory.createUniverse();
    ModelFactory.createUniverse("some other");

    alertDestination =
        ModelFactory.createAlertDestination(
            customer.getUuid(),
            "My Route",
            Collections.singletonList(
                ModelFactory.createEmailChannel(customer.getUuid(), "Test channel")));
    alertConfigurationService =
        new AlertConfigurationService(
            alertDefinitionService, new SettableRuntimeConfigFactory(app.config()));
  }

  @Test
  public void testSerialization() throws IOException {
    String initial =
        IOUtils.toString(
            getClass().getClassLoader().getResourceAsStream("alert/alert_configuration.json"),
            StandardCharsets.UTF_8);

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
            .list(AlertConfigurationFilter.builder().targetUuid(universe.getUniverseUUID()).build())
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
            .label(KnownAlertLabels.SOURCE_UUID, universe.universeUUID.toString())
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
            .label(KnownAlertLabels.SOURCE_UUID, universe.universeUUID.toString())
            .build();

    List<AlertDefinition> universeDefinitions = alertDefinitionService.list(definitionFilter);

    assertThat(universeDefinitions, hasSize(1));

    AlertDefinition definition = universeDefinitions.get(0);
    AlertDefinition duplicate =
        new AlertDefinition()
            .setCustomerUUID(customer.getUuid())
            .setQuery(definition.getQuery())
            .setConfigurationUUID(definition.getConfigurationUUID())
            .setLabels(
                definition
                    .getLabels()
                    .stream()
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

    Universe universe3 = ModelFactory.createUniverse("one more", customer.getCustomerId());
    Universe universe4 = ModelFactory.createUniverse("another more", customer.getCustomerId());

    ExecutorService executor = Executors.newFixedThreadPool(2);
    List<Future<Void>> futures = new ArrayList<>();
    for (int i = 0; i < 2; i++) {
      futures.add(
          executor.submit(
              () -> {
                alertConfigurationService.save(ImmutableList.of(configuration, configuration2));
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
        configuration -> configuration.setCustomerUUID(null), "Customer UUID field is mandatory");

    testValidationCreate(configuration -> configuration.setName(null), "Name field is mandatory");

    testValidationCreate(
        configuration -> configuration.setName(StringUtils.repeat("a", 1001)),
        "Name field can't be longer than 1000 characters");

    testValidationCreate(
        configuration -> configuration.setTargetType(null), "Target type field is mandatory");

    testValidationCreate(
        configuration -> configuration.setTarget(null), "Target field is mandatory");

    testValidationCreate(
        configuration ->
            configuration.setTarget(
                new AlertConfigurationTarget().setAll(true).setUuids(ImmutableSet.of(randomUUID))),
        "Should select either all entries or particular UUIDs as target");

    testValidationCreate(
        configuration ->
            configuration.setTarget(
                new AlertConfigurationTarget().setUuids(ImmutableSet.of(randomUUID))),
        "Universe(s) missing for uuid(s) " + randomUUID);

    testValidationCreate(
        configuration ->
            configuration.setTarget(
                new AlertConfigurationTarget().setUuids(Collections.singleton(null))),
        "Target UUIDs can't have null values");

    testValidationCreate(
        configuration ->
            configuration
                .setTargetType(TargetType.PLATFORM)
                .setTarget(new AlertConfigurationTarget().setUuids(ImmutableSet.of(randomUUID))),
        "PLATFORM configuration can't have target uuids");

    testValidationCreate(
        configuration -> configuration.setTemplate(null), "Template field is mandatory");

    testValidationCreate(
        configuration -> configuration.setTemplate(AlertTemplate.ALERT_CONFIG_WRITING_FAILED),
        "Target type should be consistent with template");

    testValidationCreate(
        configuration -> configuration.setThresholds(null), "Query thresholds are mandatory");

    testValidationCreate(
        configuration -> configuration.setDestinationUUID(randomUUID),
        "Alert destination " + randomUUID + " is missing");

    testValidationCreate(
        configuration ->
            configuration
                .setDestinationUUID(alertDestination.getUuid())
                .setDefaultDestination(true),
        "Destination can't be filled in case default destination is selected");

    testValidationCreate(
        configuration -> configuration.setThresholdUnit(null), "Threshold unit is mandatory");

    testValidationCreate(
        configuration -> configuration.setThresholdUnit(Unit.STATUS),
        "Can't set threshold unit incompatible with alert definition template");

    testValidationCreate(
        configuration -> configuration.getThresholds().get(Severity.SEVERE).setCondition(null),
        "Threshold condition is mandatory");

    testValidationCreate(
        configuration -> configuration.getThresholds().get(Severity.SEVERE).setThreshold(null),
        "Threshold value is mandatory");

    testValidationCreate(
        configuration -> configuration.getThresholds().get(Severity.SEVERE).setThreshold(-100D),
        "Threshold value can't be less than 0");

    testValidationCreate(
        configuration -> configuration.setDurationSec(-1), "Duration can't be less than 0");

    testValidationUpdate(
        configuration -> configuration.setCustomerUUID(randomUUID).setDestinationUUID(null),
        "Can't change customer UUID for configuration 'Memory Consumption'");
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
            .createConfigurationTemplate(customer, AlertTemplate.MEMORY_CONSUMPTION)
            .getDefaultConfiguration();
    configuration.setDestinationUUID(alertDestination.getUuid());
    configuration.setDefaultDestination(false);
    return alertConfigurationService.save(configuration);
  }

  private void assertTestConfiguration(AlertConfiguration configuration) {
    AlertTemplate template = AlertTemplate.MEMORY_CONSUMPTION;
    assertThat(configuration.getCustomerUUID(), equalTo(customer.uuid));
    assertThat(configuration.getName(), equalTo(template.getName()));
    assertThat(configuration.getDescription(), equalTo(template.getDescription()));
    assertThat(configuration.getTemplate(), equalTo(template));
    assertThat(configuration.getDurationSec(), equalTo(template.getDefaultDurationSec()));
    assertThat(configuration.getDestinationUUID(), equalTo(alertDestination.getUuid()));
    assertThat(configuration.getTargetType(), equalTo(template.getTargetType()));
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
