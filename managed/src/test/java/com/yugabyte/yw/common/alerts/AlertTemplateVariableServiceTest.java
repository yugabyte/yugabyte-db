// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.containsInAnyOrder;
import static org.hamcrest.Matchers.equalTo;
import static org.hamcrest.Matchers.nullValue;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import com.google.common.collect.ImmutableMap;
import com.google.common.collect.ImmutableSet;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.AlertChannel.ChannelType;
import com.yugabyte.yw.models.AlertChannelTemplates;
import com.yugabyte.yw.models.AlertConfiguration;
import com.yugabyte.yw.models.AlertTemplateVariable;
import com.yugabyte.yw.models.Customer;
import java.util.List;
import java.util.UUID;
import junitparams.JUnitParamsRunner;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(JUnitParamsRunner.class)
public class AlertTemplateVariableServiceTest extends FakeDBApplication {

  private UUID defaultCustomerUuid;

  private AlertTemplateVariableService alertTemplateVariableService;
  private AlertConfigurationService alertConfigurationService;
  private AlertChannelTemplateService alertChannelTemplateService;

  @Before
  public void setUp() {
    defaultCustomerUuid = ModelFactory.testCustomer().getUuid();
    alertTemplateVariableService = app.injector().instanceOf(AlertTemplateVariableService.class);
    alertConfigurationService = app.injector().instanceOf(AlertConfigurationService.class);
    alertChannelTemplateService = app.injector().instanceOf(AlertChannelTemplateService.class);
  }

  @Test
  public void testCreateAndGet() {
    AlertTemplateVariable variable = createTestVariable("test");
    AlertTemplateVariable updated = alertTemplateVariableService.save(variable);

    assertThat(updated, equalTo(variable));

    AlertTemplateVariable fromDb = alertTemplateVariableService.get(variable.getUuid());
    assertThat(fromDb, equalTo(variable));
  }

  @Test
  public void testGetOrBadRequest() {
    // Should raise an exception for random UUID.
    final UUID uuid = UUID.randomUUID();
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () -> {
              alertTemplateVariableService.getOrBadRequest(uuid);
            });
    assertThat(exception.getMessage(), equalTo("Invalid Alert Template Variable UUID: " + uuid));
  }

  @Test
  public void testListByCustomerUuid() {
    AlertTemplateVariable variable = createTestVariable("test");
    alertTemplateVariableService.save(variable);

    AlertTemplateVariable variable2 = createTestVariable("test2");
    alertTemplateVariableService.save(variable2);

    // Second customer with one channel.
    UUID newCustomerUUID = ModelFactory.testCustomer().getUuid();
    AlertTemplateVariable otherCustomerVariable = createTestVariable(newCustomerUUID, "test2");

    List<AlertTemplateVariable> variables = alertTemplateVariableService.list(defaultCustomerUuid);
    assertThat(variables, containsInAnyOrder(variable, variable2));
  }

  @Test
  public void testValidateDuplicateName() {
    AlertTemplateVariable variable = createTestVariable("test");
    alertTemplateVariableService.save(variable);

    AlertTemplateVariable duplicate = createTestVariable("test");
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () -> {
              alertTemplateVariableService.save(duplicate);
            });
    assertThat(
        exception.getMessage(), equalTo("errorJson: {\"name\":[\"duplicate variable 'test'\"]}"));
  }

  @Test
  public void testValidateChangeName() {
    AlertTemplateVariable variable = createTestVariable("test");
    alertTemplateVariableService.save(variable);

    variable.setName("other");
    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () -> {
              alertTemplateVariableService.save(variable);
            });
    assertThat(
        exception.getMessage(),
        equalTo("errorJson: {\"name\":[\"can't change for variable 'test'\"]}"));
  }

  @Test
  public void testUpdate() {
    AlertTemplateVariable variable = createTestVariable("test");
    alertTemplateVariableService.save(variable);

    variable.setPossibleValues(ImmutableSet.of("one", "two", "three"));
    variable.setDefaultValue("one");
    AlertTemplateVariable updated = alertTemplateVariableService.save(variable);

    assertThat(updated, equalTo(variable));

    AlertTemplateVariable fromDb = alertTemplateVariableService.get(variable.getUuid());
    assertThat(fromDb, equalTo(variable));
  }

  @Test
  public void testUpdateDeleteUsedValues() {
    AlertTemplateVariable variable = createTestVariable("test");
    alertTemplateVariableService.save(variable);
    AlertTemplateVariable otherVariable = createTestVariable("test1");
    alertTemplateVariableService.save(otherVariable);

    Customer defaultCustomer = Customer.get(defaultCustomerUuid);
    AlertConfiguration configuration =
        ModelFactory.createAlertConfiguration(
            defaultCustomer, ModelFactory.createUniverse(defaultCustomer.getId()));
    configuration.setLabels(ImmutableMap.of("test", "foo", "test1", "bar"));
    alertConfigurationService.save(configuration);

    variable.setPossibleValues(ImmutableSet.of("other", "any"));
    variable.setDefaultValue("other");
    alertTemplateVariableService.save(variable);

    AlertTemplateVariable fromDb = alertTemplateVariableService.get(variable.getUuid());
    assertThat(fromDb, equalTo(variable));

    AlertConfiguration updatedConfiguration =
        alertConfigurationService.get(configuration.getUuid());
    assertTrue(updatedConfiguration.getLabels().containsKey("test1"));
    assertFalse(updatedConfiguration.getLabels().containsKey("test"));
  }

  @Test
  public void testDelete() {
    AlertTemplateVariable variable = createTestVariable("test");
    alertTemplateVariableService.save(variable);

    alertTemplateVariableService.delete(variable.getUuid());

    AlertTemplateVariable fromDb = alertTemplateVariableService.get(variable.getUuid());
    assertThat(fromDb, nullValue());
  }

  @Test
  public void testDeleteVariableUsedInConfig() {
    AlertTemplateVariable variable = createTestVariable("test");
    alertTemplateVariableService.save(variable);
    AlertTemplateVariable otherVariable = createTestVariable("test1");
    alertTemplateVariableService.save(otherVariable);

    Customer defaultCustomer = Customer.get(defaultCustomerUuid);
    AlertConfiguration configuration =
        ModelFactory.createAlertConfiguration(
            defaultCustomer, ModelFactory.createUniverse(defaultCustomer.getId()));
    configuration.setLabels(ImmutableMap.of("test", "foo", "test1", "bar"));
    alertConfigurationService.save(configuration);

    alertTemplateVariableService.delete(variable.getUuid());

    AlertTemplateVariable fromDb = alertTemplateVariableService.get(variable.getUuid());
    assertThat(fromDb, nullValue());

    AlertConfiguration updatedConfiguration =
        alertConfigurationService.get(configuration.getUuid());
    assertTrue(updatedConfiguration.getLabels().containsKey("test1"));
    assertFalse(updatedConfiguration.getLabels().containsKey("test"));
  }

  @Test
  public void testDeleteVariableUsedInTemplate() {
    AlertTemplateVariable variable = createTestVariable("test");
    alertTemplateVariableService.save(variable);
    AlertTemplateVariable otherVariable = createTestVariable("test1");
    alertTemplateVariableService.save(otherVariable);

    AlertChannelTemplates alertChannelTemplates =
        AlertChannelTemplateServiceTest.createTemplates(defaultCustomerUuid, ChannelType.Email);
    alertChannelTemplates.setTextTemplate("Bla {{ test }} and {{ test1 }} bla bla");
    alertChannelTemplateService.save(alertChannelTemplates);

    PlatformServiceException exception =
        assertThrows(
            PlatformServiceException.class,
            () -> {
              alertTemplateVariableService.delete(variable.getUuid());
            });
    assertThat(
        exception.getMessage(),
        equalTo("errorJson: {\"\":[\"variable 'test' is used in 'Email' template\"]}"));
  }

  private AlertTemplateVariable createTestVariable(String name) {
    return createTestVariable(defaultCustomerUuid, name);
  }

  public static AlertTemplateVariable createTestVariable(UUID customerUuid, String name) {
    return new AlertTemplateVariable()
        .setName(name)
        .setCustomerUUID(customerUuid)
        .setPossibleValues(ImmutableSet.of("foo", "bar"))
        .setDefaultValue("foo");
  }
}
