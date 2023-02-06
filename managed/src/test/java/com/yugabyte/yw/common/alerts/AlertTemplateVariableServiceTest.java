// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.containsInAnyOrder;
import static org.hamcrest.Matchers.equalTo;
import static org.hamcrest.Matchers.nullValue;
import static org.junit.Assert.assertThrows;

import com.google.common.collect.ImmutableSet;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.models.AlertTemplateVariable;
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

  @Before
  public void setUp() {
    defaultCustomerUuid = ModelFactory.testCustomer().getUuid();
    alertTemplateVariableService = app.injector().instanceOf(AlertTemplateVariableService.class);
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
    UUID newCustomerUUID = ModelFactory.testCustomer().uuid;
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
    AlertTemplateVariable updated = alertTemplateVariableService.save(variable);

    assertThat(updated, equalTo(variable));

    AlertTemplateVariable fromDb = alertTemplateVariableService.get(variable.getUuid());
    assertThat(fromDb, equalTo(variable));
  }

  @Test
  public void testDelete() {
    AlertTemplateVariable variable = createTestVariable("test");
    alertTemplateVariableService.save(variable);

    alertTemplateVariableService.delete(variable.getUuid());

    AlertTemplateVariable fromDb = alertTemplateVariableService.get(variable.getUuid());
    assertThat(fromDb, nullValue());
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
