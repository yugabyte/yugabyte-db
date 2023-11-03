// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.common.alerts;

import static com.yugabyte.yw.common.AlertTemplate.MEMORY_CONSUMPTION;
import static com.yugabyte.yw.common.ThrownMatcher.thrown;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.nullValue;
import static org.hamcrest.MatcherAssert.*;
import static org.hamcrest.Matchers.*;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.TestUtils;
import com.yugabyte.yw.models.AlertConfiguration;
import com.yugabyte.yw.models.AlertDefinition;
import com.yugabyte.yw.models.AlertTemplateSettings;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.filters.AlertDefinitionFilter;
import com.yugabyte.yw.models.filters.AlertTemplateSettingsFilter;
import java.io.IOException;
import java.util.Collections;
import java.util.List;
import java.util.function.Consumer;
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
public class AlertTemplateSettingsServiceTest extends FakeDBApplication {

  @Rule public MockitoRule mockitoRule = MockitoJUnit.rule();

  private Customer customer;

  public AlertTemplateSettingsService alertTemplateSettingsService;

  private AlertConfiguration configuration;
  private AlertConfiguration configuration2;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer("Customer");
    alertTemplateSettingsService = app.injector().instanceOf(AlertTemplateSettingsService.class);

    Universe universe1 = ModelFactory.createUniverse("U1", customer.getId());
    Universe universe2 = ModelFactory.createUniverse("U2", customer.getId());
    configuration = ModelFactory.createAlertConfiguration(customer, universe1);
    ModelFactory.createAlertDefinition(customer, universe1, configuration);
    ModelFactory.createAlertDefinition(customer, universe2, configuration);
    configuration2 = ModelFactory.createAlertConfiguration(customer, null);
    ModelFactory.createAlertDefinition(customer, null, configuration2);

    setDefinitionConfigWritten();
  }

  @Test
  public void testSerialization() throws IOException {
    String initial = TestUtils.readResource("alert/alert_template_settings.json");

    JsonNode initialJson = Json.parse(initial);

    AlertTemplateSettings settings = Json.fromJson(initialJson, AlertTemplateSettings.class);

    JsonNode resultJson = Json.toJson(settings);

    assertThat(resultJson, equalTo(initialJson));
  }

  @Test
  public void testAddAndQueryByUUID() {
    AlertTemplateSettings settings = ModelFactory.createTemplateSettings(customer);

    AlertTemplateSettings queriedSettings = alertTemplateSettingsService.get(settings.getUuid());

    assertTestSettings(queriedSettings);
  }

  @Test
  public void testUpdateAndQuery() {
    AlertTemplateSettings settings = ModelFactory.createTemplateSettings(customer);

    settings.setLabels(ImmutableMap.of("bar", "baz"));

    alertTemplateSettingsService.save(settings);

    AlertTemplateSettings updated =
        alertTemplateSettingsService
            .list(AlertTemplateSettingsFilter.builder().template(MEMORY_CONSUMPTION.name()).build())
            .get(0);

    assertThat(
        updated.getLabels().entrySet(),
        everyItem(is(in(ImmutableMap.of("bar", "baz").entrySet()))));

    assertDefinitionConfigWritten();
    setDefinitionConfigWritten();

    updated.setLabels(Collections.emptyMap());

    alertTemplateSettingsService.save(updated);

    updated =
        alertTemplateSettingsService
            .list(AlertTemplateSettingsFilter.builder().template(MEMORY_CONSUMPTION.name()).build())
            .get(0);

    assertDefinitionConfigWritten();
    assertThat(updated.getLabels(), anEmptyMap());
  }

  @Test
  public void testDelete() {
    AlertTemplateSettings settings = ModelFactory.createTemplateSettings(customer);

    alertTemplateSettingsService.delete(settings.getUuid());

    AlertTemplateSettings queriedSettings = alertTemplateSettingsService.get(settings.getUuid());

    assertThat(queriedSettings, nullValue());
    assertDefinitionConfigWritten();
  }

  @Test
  public void testValidation() {
    testValidation(
        settings -> settings.setCustomerUUID(null),
        "errorJson: {\"customerUUID\":[\"must not be null\"]}");

    testValidation(
        settings -> settings.setTemplate(null), "errorJson: {\"template\":[\"must not be null\"]}");

    testValidation(
        settings -> settings.setTemplate(StringUtils.repeat("a", 1001)),
        "errorJson: {\"template\":[\"size must be between 1 and 50\"]}");
  }

  private void testValidation(Consumer<AlertTemplateSettings> modifier, String expectedMessage) {
    AlertTemplateSettings settings = ModelFactory.createTemplateSettings(customer);

    alertTemplateSettingsService.delete(settings.getUuid());
    settings.setUuid(null);
    modifier.accept(settings);

    assertThat(
        () -> alertTemplateSettingsService.save(settings),
        thrown(PlatformServiceException.class, expectedMessage));
  }

  private void assertTestSettings(AlertTemplateSettings settings) {
    assertThat(settings.getCustomerUUID(), equalTo(customer.getUuid()));
    assertThat(settings.getTemplate(), equalTo(MEMORY_CONSUMPTION.name()));
    assertThat(
        settings.getLabels().entrySet(),
        everyItem(is(in(ImmutableMap.of("foo", "bar", "one", "two").entrySet()))));
    assertThat(settings.getUuid(), notNullValue());
    assertThat(settings.getCreateTime(), notNullValue());
  }

  private void setDefinitionConfigWritten() {
    List<AlertDefinition> definitionList =
        alertDefinitionService.list(AlertDefinitionFilter.builder().build());
    definitionList.forEach(d -> d.setConfigWritten(true));
    alertDefinitionService.save(definitionList);
  }

  private void assertDefinitionConfigWritten() {
    List<AlertDefinition> shouldBeOverwritten =
        alertDefinitionService.list(
            AlertDefinitionFilter.builder().configurationUuid(configuration.getUuid()).build());

    assertThat(shouldBeOverwritten, hasSize(2));
    shouldBeOverwritten.forEach(d -> assertThat(d.isConfigWritten(), equalTo(false)));

    List<AlertDefinition> shouldNotBeOverwritten =
        alertDefinitionService.list(
            AlertDefinitionFilter.builder().configurationUuid(configuration2.getUuid()).build());

    assertThat(shouldNotBeOverwritten, hasSize(1));
    shouldNotBeOverwritten.forEach(d -> assertThat(d.isConfigWritten(), equalTo(true)));
  }
}
