// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.models;

import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.nullValue;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.hasSize;
import static org.hamcrest.Matchers.notNullValue;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableMap;
import com.google.common.collect.ImmutableSet;
import com.yugabyte.yw.common.AlertDefinitionTemplate;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.alerts.AlertDefinitionGroupService;
import com.yugabyte.yw.common.alerts.AlertDefinitionService;
import com.yugabyte.yw.common.alerts.AlertService;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.models.filters.AlertDefinitionFilter;
import com.yugabyte.yw.models.filters.AlertDefinitionGroupFilter;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import org.apache.commons.io.IOUtils;
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
            Collections.singletonList(ModelFactory.createEmailReceiver(customer, "Test receiver")));
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
            .setThreshold(90);
    AlertDefinitionGroupThreshold warningThreshold =
        new AlertDefinitionGroupThreshold()
            .setCondition(AlertDefinitionGroupThreshold.Condition.GREATER_THAN)
            .setThreshold(80);
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

  private AlertDefinitionGroup createTestDefinitionGroup() {
    AlertDefinitionGroup group =
        alertDefinitionGroupService.createGroupFromTemplate(
            customer, AlertDefinitionTemplate.MEMORY_CONSUMPTION);
    group.setRouteUUID(alertRoute.getUuid());
    return alertDefinitionGroupService.save(group);
  }

  private void assertTestDefinitionGroup(AlertDefinitionGroup group) {
    AlertDefinitionTemplate template = AlertDefinitionTemplate.MEMORY_CONSUMPTION;
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
                .setThreshold(90)
                .setCondition(AlertDefinitionGroupThreshold.Condition.GREATER_THAN)));
    assertThat(group.getThresholds().get(AlertDefinitionGroup.Severity.WARNING), nullValue());
    assertThat(group.getUuid(), notNullValue());
    assertThat(group.getCreateTime(), notNullValue());
  }
}
