// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.models;

import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.alerts.AlertDefinitionService;
import com.yugabyte.yw.models.filters.AlertDefinitionFilter;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.junit.MockitoJUnitRunner;

import javax.persistence.OptimisticLockException;
import java.util.List;

import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.nullValue;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.*;
import static org.junit.Assert.*;

@RunWith(MockitoJUnitRunner.class)
public class AlertDefinitionTest extends FakeDBApplication {

  private static final String TEST_DEFINITION_NAME = "Alert Definition";
  private static final String TEST_DEFINITION_QUERY = "some_metric > 100";

  private static final String TEST_LABEL = "test_label";
  private static final String TEST_LABEL_VALUE = "test_value";

  private static final String TEST_LABEL_2 = "test_label_2";
  private static final String TEST_LABEL_VALUE_2 = "test_value2";

  private Customer customer;

  private Universe universe;

  @InjectMocks private AlertDefinitionService alertDefinitionService;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer("Customer");
    universe = ModelFactory.createUniverse();
  }

  @Test
  public void testAddAndQueryByUUID() {
    AlertDefinition definition = createTestDefinition1();
    createTestDefinition2();

    AlertDefinition queriedDefinition = alertDefinitionService.get(definition.getUuid());

    assertTestDefinition1(queriedDefinition);
  }

  @Test
  public void testQueryByCustomerUniverse() {
    createTestDefinition1();

    List<AlertDefinition> queriedDefinitions =
        alertDefinitionService.list(
            new AlertDefinitionFilter()
                .setCustomerUuid(customer.uuid)
                .setName(TEST_DEFINITION_NAME)
                .setLabel(KnownAlertLabels.UNIVERSE_UUID, universe.universeUUID.toString()));

    assertThat(queriedDefinitions, hasSize(1));
    assertTestDefinition1(queriedDefinitions.get(0));
  }

  @Test
  public void testQueryByCustomerLabel() {
    createTestDefinition1();
    createTestDefinition2();

    AlertDefinitionLabel label1 = new AlertDefinitionLabel(TEST_LABEL, TEST_LABEL_VALUE);
    List<AlertDefinition> queriedDefinitions =
        alertDefinitionService.list(
            new AlertDefinitionFilter().setCustomerUuid(customer.uuid).setLabel(label1));

    assertThat(queriedDefinitions, hasSize(1));
    assertTestDefinition1(queriedDefinitions.get(0));
  }

  @Test
  public void testUpdateAndQueryByLabel() {
    AlertDefinition definition = createTestDefinition2();

    AlertDefinitionLabel label2 = new AlertDefinitionLabel(TEST_LABEL_2, TEST_LABEL_VALUE_2);

    String newQuery = "qwewqewqe";
    definition.setQuery(newQuery);
    definition.setActive(false);
    definition.setLabels(ImmutableList.of(label2));
    alertDefinitionService.update(definition);

    AlertDefinitionLabel label1 = new AlertDefinitionLabel(TEST_LABEL, TEST_LABEL_VALUE);
    List<AlertDefinition> queriedDefinitions =
        alertDefinitionService.list(
            new AlertDefinitionFilter().setCustomerUuid(customer.uuid).setLabel(label2));

    List<AlertDefinition> queriedByOldLabelDefinitions =
        alertDefinitionService.list(
            new AlertDefinitionFilter().setCustomerUuid(customer.uuid).setLabel(label1));

    assertThat(queriedDefinitions, hasSize(1));
    assertThat(queriedByOldLabelDefinitions, empty());

    AlertDefinition queriedDefinition = queriedDefinitions.get(0);
    assertThat(queriedDefinition.getCustomerUUID(), equalTo(customer.uuid));
    assertThat(queriedDefinition.getName(), equalTo(TEST_DEFINITION_NAME));
    assertThat(queriedDefinition.getQuery(), equalTo(newQuery));
    assertFalse(queriedDefinition.isActive());
    assertThat(queriedDefinition.getLabels(), containsInAnyOrder(label2));
  }

  @Test
  public void testOptimisticLocking() {
    AlertDefinition definition = createTestDefinition1();

    AlertDefinition createdDefinition = alertDefinitionService.get(definition.getUuid());

    String newQuery = "qwewqewqe";
    definition.setQuery(newQuery);
    definition.setActive(false);
    alertDefinitionService.update(definition);

    createdDefinition.setConfigWritten(true);
    assertThrows(
        OptimisticLockException.class, () -> alertDefinitionService.update(createdDefinition));
  }

  @Test
  public void testDelete() {
    AlertDefinition definition = createTestDefinition1();

    definition.delete();

    AlertDefinition queriedDefinition = alertDefinitionService.get(definition.getUuid());

    assertThat(queriedDefinition, nullValue());
  }

  private AlertDefinition createTestDefinition1() {
    AlertDefinitionLabel label1 = new AlertDefinitionLabel(TEST_LABEL, TEST_LABEL_VALUE);
    AlertDefinitionLabel knownLabel =
        new AlertDefinitionLabel(KnownAlertLabels.UNIVERSE_UUID, universe.universeUUID.toString());
    AlertDefinition definition = new AlertDefinition();
    definition.setCustomerUUID(customer.uuid);
    definition.setTargetType(AlertDefinition.TargetType.Universe);
    definition.setName(TEST_DEFINITION_NAME);
    definition.setQuery(TEST_DEFINITION_QUERY);
    definition.setLabels(ImmutableList.of(label1, knownLabel));
    return alertDefinitionService.create(definition);
  }

  private AlertDefinition createTestDefinition2() {
    AlertDefinitionLabel label2 = new AlertDefinitionLabel(TEST_LABEL_2, TEST_LABEL_VALUE_2);
    AlertDefinition definition = new AlertDefinition();
    definition.setCustomerUUID(customer.uuid);
    definition.setTargetType(AlertDefinition.TargetType.Universe);
    definition.setName(TEST_DEFINITION_NAME);
    definition.setQuery(TEST_DEFINITION_QUERY);
    definition.setLabels(ImmutableList.of(label2));
    return alertDefinitionService.create(definition);
  }

  private void assertTestDefinition1(AlertDefinition definition) {
    AlertDefinitionLabel label1 =
        new AlertDefinitionLabel(definition, TEST_LABEL, TEST_LABEL_VALUE);
    label1.setDefinition(definition);
    AlertDefinitionLabel knownLabel =
        new AlertDefinitionLabel(
            definition, KnownAlertLabels.UNIVERSE_UUID, universe.universeUUID.toString());
    assertThat(definition.getCustomerUUID(), equalTo(customer.uuid));
    assertThat(definition.getName(), equalTo(TEST_DEFINITION_NAME));
    assertThat(definition.getQuery(), equalTo(TEST_DEFINITION_QUERY));
    assertTrue(definition.isActive());
    assertFalse(definition.isConfigWritten());
    assertThat(definition.getLabels(), containsInAnyOrder(label1, knownLabel));
  }
}
