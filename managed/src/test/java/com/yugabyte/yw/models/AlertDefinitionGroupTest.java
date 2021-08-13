// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.models;

import static com.yugabyte.yw.common.ThrownMatcher.thrown;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.nullValue;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.hasSize;
import static org.hamcrest.Matchers.notNullValue;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableMap;
import com.google.common.collect.ImmutableSet;
import com.yugabyte.yw.common.AlertTemplate;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.YWServiceException;
import com.yugabyte.yw.common.alerts.AlertDefinitionGroupService;
import com.yugabyte.yw.common.alerts.AlertDefinitionService;
import com.yugabyte.yw.common.alerts.AlertService;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.models.AlertDefinitionGroup.Severity;
import com.yugabyte.yw.models.AlertDefinitionGroup.TargetType;
import com.yugabyte.yw.models.common.Unit;
import com.yugabyte.yw.models.filters.AlertDefinitionFilter;
import com.yugabyte.yw.models.filters.AlertDefinitionGroupFilter;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.Collections;
import java.util.Date;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.function.Consumer;
import java.util.function.Function;
import org.apache.commons.io.IOUtils;
import org.apache.commons.lang3.StringUtils;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class AlertDefinitionGroupTest extends FakeDBApplication {

  private Customer customer;
  private Universe universe;
  private AlertRoute alertRoute;

  private AlertService alertService = new AlertService();
  private AlertDefinitionService alertDefinitionService = new AlertDefinitionService(alertService);
  private AlertDefinitionGroupService alertDefinitionGroupService;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer("Customer");
    universe = ModelFactory.createUniverse();
    ModelFactory.createUniverse("some other");

    alertRoute =
        ModelFactory.createAlertRoute(
            customer.getUuid(),
            "My Route",
            Collections.singletonList(
                ModelFactory.createEmailReceiver(customer.getUuid(), "Test receiver")));
    alertDefinitionGroupService =
        new AlertDefinitionGroupService(
            alertDefinitionService, new SettableRuntimeConfigFactory(app.config()));
  }

  @Test
  public void testSerialization() throws IOException {
    String initial =
        IOUtils.toString(
            getClass().getClassLoader().getResourceAsStream("alert/alert_definition_group.json"),
            StandardCharsets.UTF_8);

    JsonNode initialJson = Json.parse(initial);

    AlertDefinitionGroup group = Json.fromJson(initialJson, AlertDefinitionGroup.class);

    JsonNode resultJson = Json.toJson(group);

    assertThat(resultJson, equalTo(initialJson));
  }

  @Test
  public void testAddAndQueryByUUID() {
    AlertDefinitionGroup group = createTestDefinitionGroup();

    AlertDefinitionGroup queriedDefinition = alertDefinitionGroupService.get(group.getUuid());

    assertTestDefinitionGroup(queriedDefinition);

    List<AlertDefinition> definitions =
        alertDefinitionService.list(
            AlertDefinitionFilter.builder().groupUuid(queriedDefinition.getUuid()).build());

    assertThat(definitions, hasSize(2));
  }

  @Test
  public void testUpdateAndQueryTarget() {
    AlertDefinitionGroup group = createTestDefinitionGroup();

    AlertDefinitionGroupThreshold severeThreshold =
        new AlertDefinitionGroupThreshold()
            .setCondition(AlertDefinitionGroupThreshold.Condition.GREATER_THAN)
            .setThreshold(90D);
    AlertDefinitionGroupThreshold warningThreshold =
        new AlertDefinitionGroupThreshold()
            .setCondition(AlertDefinitionGroupThreshold.Condition.GREATER_THAN)
            .setThreshold(80D);
    Map<AlertDefinitionGroup.Severity, AlertDefinitionGroupThreshold> thresholds =
        ImmutableMap.of(
            AlertDefinitionGroup.Severity.SEVERE, severeThreshold,
            AlertDefinitionGroup.Severity.WARNING, warningThreshold);
    AlertDefinitionGroupTarget target =
        new AlertDefinitionGroupTarget()
            .setAll(false)
            .setUuids(ImmutableSet.of(universe.getUniverseUUID()));
    group.setTarget(target);
    group.setThresholds(thresholds);
    alertDefinitionGroupService.save(group);

    AlertDefinitionGroup updated =
        alertDefinitionGroupService
            .list(
                AlertDefinitionGroupFilter.builder().targetUuid(universe.getUniverseUUID()).build())
            .get(0);

    assertThat(updated.getTarget(), equalTo(target));
    assertThat(
        updated.getThresholds().get(AlertDefinitionGroup.Severity.SEVERE),
        equalTo(severeThreshold));
    assertThat(
        updated.getThresholds().get(AlertDefinitionGroup.Severity.WARNING),
        equalTo(warningThreshold));

    List<AlertDefinition> definitions =
        alertDefinitionService.list(
            AlertDefinitionFilter.builder().groupUuid(updated.getUuid()).build());

    assertThat(definitions, hasSize(1));
  }

  @Test
  public void testDelete() {
    AlertDefinitionGroup group = createTestDefinitionGroup();

    alertDefinitionGroupService.delete(group.getUuid());

    AlertDefinitionGroup queriedGroup = alertDefinitionGroupService.get(group.getUuid());

    assertThat(queriedGroup, nullValue());

    List<AlertDefinition> definitions =
        alertDefinitionService.list(
            AlertDefinitionFilter.builder().groupUuid(group.getUuid()).build());

    assertThat(definitions, hasSize(0));
  }

  @Test
  public void testValidation() {
    UUID randomUUID = UUID.randomUUID();

    testValidationCreate(group -> group.setCustomerUUID(null), "Customer UUID field is mandatory");

    testValidationCreate(group -> group.setName(null), "Name field is mandatory");

    testValidationCreate(
        group -> group.setName(StringUtils.repeat("a", 1001)),
        "Name field can't be longer than 1000 characters");

    testValidationCreate(group -> group.setTargetType(null), "Target type field is mandatory");

    testValidationCreate(group -> group.setTarget(null), "Target field is mandatory");

    testValidationCreate(
        group ->
            group.setTarget(
                new AlertDefinitionGroupTarget()
                    .setAll(true)
                    .setUuids(ImmutableSet.of(randomUUID))),
        "Should select either all entries or particular UUIDs as target");

    testValidationCreate(
        group ->
            group.setTarget(new AlertDefinitionGroupTarget().setUuids(ImmutableSet.of(randomUUID))),
        "Universe(s) missing for uuid(s) " + randomUUID);

    testValidationCreate(
        group ->
            group
                .setTargetType(TargetType.CUSTOMER)
                .setTarget(new AlertDefinitionGroupTarget().setUuids(ImmutableSet.of(randomUUID))),
        "CUSTOMER group can't have target uuids");

    testValidationCreate(group -> group.setTemplate(null), "Template field is mandatory");

    testValidationCreate(
        group -> group.setTemplate(AlertTemplate.ALERT_CONFIG_WRITING_FAILED),
        "Target type should be consistent with template");

    testValidationCreate(group -> group.setThresholds(null), "Query thresholds are mandatory");

    testValidationCreate(
        group -> group.setRouteUUID(randomUUID), "Alert route " + randomUUID + " is missing");

    testValidationCreate(group -> group.setThresholdUnit(null), "Threshold unit is mandatory");

    testValidationCreate(
        group -> group.setThresholdUnit(Unit.STATUS),
        "Can't set threshold unit incompatible with alert definition template");

    testValidationCreate(
        group -> group.getThresholds().get(Severity.SEVERE).setCondition(null),
        "Threshold condition is mandatory");

    testValidationCreate(
        group -> group.getThresholds().get(Severity.SEVERE).setThreshold(null),
        "Threshold value is mandatory");

    testValidationCreate(
        group -> group.getThresholds().get(Severity.SEVERE).setThreshold(-100D),
        "Threshold value can't be less than 0");

    testValidationCreate(group -> group.setDurationSec(-1), "Duration can't be less than 0");

    testValidationUpdate(
        group -> group.setCustomerUUID(randomUUID).setRouteUUID(null),
        uuid -> "Can't change customer UUID for group " + uuid);

    testValidationUpdate(
        group -> group.setCreateTime(new Date()),
        uuid -> "Can't change create time for group " + uuid);
  }

  private void testValidationCreate(
      Consumer<AlertDefinitionGroup> modifier, String expectedMessage) {
    testValidation(modifier, uuid -> expectedMessage, true);
  }

  private void testValidationUpdate(
      Consumer<AlertDefinitionGroup> modifier, Function<UUID, String> expectedMessageGenerator) {
    testValidation(modifier, expectedMessageGenerator, false);
  }

  private void testValidation(
      Consumer<AlertDefinitionGroup> modifier,
      Function<UUID, String> expectedMessageGenerator,
      boolean create) {
    AlertDefinitionGroup group = createTestDefinitionGroup();
    if (create) {
      alertDefinitionGroupService.delete(group.getUuid());
      group.setUuid(null);
    }
    modifier.accept(group);

    assertThat(
        () -> alertDefinitionGroupService.save(group),
        thrown(YWServiceException.class, expectedMessageGenerator.apply(group.getUuid())));
  }

  private AlertDefinitionGroup createTestDefinitionGroup() {
    AlertDefinitionGroup group =
        alertDefinitionGroupService
            .createDefinitionTemplate(customer, AlertTemplate.MEMORY_CONSUMPTION)
            .getDefaultGroup();
    group.setRouteUUID(alertRoute.getUuid());
    return alertDefinitionGroupService.save(group);
  }

  private void assertTestDefinitionGroup(AlertDefinitionGroup group) {
    AlertTemplate template = AlertTemplate.MEMORY_CONSUMPTION;
    assertThat(group.getCustomerUUID(), equalTo(customer.uuid));
    assertThat(group.getName(), equalTo(template.getName()));
    assertThat(group.getDescription(), equalTo(template.getDescription()));
    assertThat(group.getTemplate(), equalTo(template));
    assertThat(group.getDurationSec(), equalTo(template.getDefaultDurationSec()));
    assertThat(group.getRouteUUID(), equalTo(alertRoute.getUuid()));
    assertThat(group.getTargetType(), equalTo(template.getTargetType()));
    assertThat(group.getTarget(), equalTo(new AlertDefinitionGroupTarget().setAll(true)));
    assertThat(
        group.getThresholds().get(AlertDefinitionGroup.Severity.SEVERE),
        equalTo(
            new AlertDefinitionGroupThreshold()
                .setThreshold(90D)
                .setCondition(AlertDefinitionGroupThreshold.Condition.GREATER_THAN)));
    assertThat(group.getThresholds().get(AlertDefinitionGroup.Severity.WARNING), nullValue());
    assertThat(group.getUuid(), notNullValue());
    assertThat(group.getCreateTime(), notNullValue());
  }
}
