// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.models;

import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import junitparams.JUnitParamsRunner;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

import java.util.List;

import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.nullValue;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.*;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

@RunWith(JUnitParamsRunner.class)
public class AlertDefinitionTest extends FakeDBApplication {

  private static final String TEST_DEFINITION_NAME = "Alert Definition";
  private static final String TEST_DEFINITION_QUERY = "some_metric > 100";

  private static final String TEST_LABEL = "test_label";
  private static final String TEST_LABEL_VALUE = "test_value";

  private static final String TEST_LABEL_2 = "test_label_2";
  private static final String TEST_LABEL_VALUE_2 = "test_value2";

  private Customer customer;

  private Universe universe;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer("Customer");
    universe = ModelFactory.createUniverse();
  }

  @Test
  public void testAddAndQueryByUUID() {
    AlertDefinition definition = createTestDefinition1();
    createTestDefinition2();

    AlertDefinition queriedDefinition = AlertDefinition.get(definition.uuid);

    assertTestDefinition1(queriedDefinition);
  }

  @Test
  public void testQueryByCustomerUniverse() {
    createTestDefinition1();

    AlertDefinition queriedDefinition = AlertDefinition
      .get(customer.uuid, universe.universeUUID, TEST_DEFINITION_NAME);

    assertTestDefinition1(queriedDefinition);
  }

  @Test
  public void testQueryByCustomerLabel() {
    createTestDefinition1();
    createTestDefinition2();

    AlertDefinitionLabel label1 = new AlertDefinitionLabel(TEST_LABEL, TEST_LABEL_VALUE);
    List<AlertDefinition> queriedDefinitions = AlertDefinition
      .get(customer.uuid, label1);

    assertThat(queriedDefinitions, hasSize(1));
    assertTestDefinition1(queriedDefinitions.get(0));
  }

  @Test
  public void testUpdateAndQueryByLabel() {
    AlertDefinition definition = createTestDefinition2();

    AlertDefinitionLabel label2 = new AlertDefinitionLabel(TEST_LABEL_2, TEST_LABEL_VALUE_2);

    String newQuery = "qwewqewqe";
    AlertDefinition.update(definition.uuid,
      newQuery,
      false,
      ImmutableList.of(label2));

    AlertDefinitionLabel label1 = new AlertDefinitionLabel(TEST_LABEL, TEST_LABEL_VALUE);
    List<AlertDefinition> queriedDefinitions = AlertDefinition
      .get(customer.uuid, label2);

    List<AlertDefinition> queriedByOldLabelDefinitions = AlertDefinition
      .get(customer.uuid, label1);

    assertThat(queriedDefinitions, hasSize(1));
    assertThat(queriedByOldLabelDefinitions, empty());

    AlertDefinition queriedDefinition = queriedDefinitions.get(0);
    assertThat(queriedDefinition.customerUUID, equalTo(customer.uuid));
    assertThat(queriedDefinition.name, equalTo(TEST_DEFINITION_NAME));
    assertThat(queriedDefinition.query, equalTo(newQuery));
    assertFalse(queriedDefinition.isActive);
    assertThat(queriedDefinition.getLabels(), containsInAnyOrder(label2));
  }

  @Test
  public void testDelete() {
    AlertDefinition definition = createTestDefinition1();

    definition.delete();

    AlertDefinition queriedDefinition = AlertDefinition.get(definition.uuid);

    assertThat(queriedDefinition, nullValue());
  }

  private AlertDefinition createTestDefinition1() {
    AlertDefinitionLabel label1 = new AlertDefinitionLabel(TEST_LABEL, TEST_LABEL_VALUE);
    AlertDefinitionLabel knownLabel = new AlertDefinitionLabel(
      KnownAlertLabels.UNIVERSE_UUID, universe.universeUUID.toString());
    return AlertDefinition.create(
      customer.uuid,
      AlertDefinition.TargetType.Universe,
      TEST_DEFINITION_NAME,
      TEST_DEFINITION_QUERY,
      true,
      ImmutableList.of(label1, knownLabel));
  }

  private AlertDefinition createTestDefinition2() {
    AlertDefinitionLabel label2 = new AlertDefinitionLabel(TEST_LABEL_2, TEST_LABEL_VALUE_2);
    return AlertDefinition.create(
      customer.uuid,
      AlertDefinition.TargetType.Universe,
      TEST_DEFINITION_NAME,
      TEST_DEFINITION_QUERY,
      true,
      ImmutableList.of(label2));
  }

  private void assertTestDefinition1(AlertDefinition definition) {
    AlertDefinitionLabel label1 = new AlertDefinitionLabel(
      definition, TEST_LABEL, TEST_LABEL_VALUE);
    label1.setDefinition(definition);
    AlertDefinitionLabel knownLabel = new AlertDefinitionLabel(
      definition, KnownAlertLabels.UNIVERSE_UUID, universe.universeUUID.toString());
    assertThat(definition.customerUUID, equalTo(customer.uuid));
    assertThat(definition.name, equalTo(TEST_DEFINITION_NAME));
    assertThat(definition.query, equalTo(TEST_DEFINITION_QUERY));
    assertTrue(definition.isActive);
    assertThat(definition.getLabels(), containsInAnyOrder(label1, knownLabel));
  }
}
