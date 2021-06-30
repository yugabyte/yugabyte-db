// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.models;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.alerts.AlertService;
import com.yugabyte.yw.models.filters.AlertFilter;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.KnownAlertCodes;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import com.yugabyte.yw.models.helpers.KnownAlertTypes;
import junitparams.JUnitParamsRunner;
import org.hamcrest.Matchers;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;

import java.util.List;
import java.util.stream.Collectors;
import java.util.stream.Stream;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.*;
import static org.junit.Assert.assertTrue;

@RunWith(JUnitParamsRunner.class)
public class AlertTest extends FakeDBApplication {

  @Rule public MockitoRule rule = MockitoJUnit.rule();

  private Customer cust1;

  @InjectMocks private AlertService alertService;

  @Before
  public void setUp() {
    cust1 = ModelFactory.testCustomer("Customer 1");
  }

  @Test
  public void testAddAndQueryByUuid() {
    AlertDefinition definition = createDefinition();
    Alert alert = createAlert(definition);

    alert = alertService.create(alert);

    Alert queriedAlert = alertService.get(alert.getUuid());

    assertTestAlert(queriedAlert, definition);
  }

  @Test
  public void testQueryByLotUuids() {
    AlertDefinition definition = createDefinition();
    List<Alert> alerts =
        Stream.generate(() -> createAlert(definition))
            .limit(CommonUtils.DB_MAX_IN_CLAUSE_ITEMS + 1)
            .collect(Collectors.toList());

    List<Alert> created = alertService.save(alerts);

    AlertFilter alertFilter =
        AlertFilter.builder()
            .uuids(created.stream().map(Alert::getUuid).collect(Collectors.toList()))
            .build();
    List<Alert> queriedAlerts = alertService.list(alertFilter);

    assertThat(queriedAlerts, hasSize(CommonUtils.DB_MAX_IN_CLAUSE_ITEMS + 1));

    alertFilter =
        AlertFilter.builder()
            .excludeUuids(created.stream().map(Alert::getUuid).collect(Collectors.toList()))
            .build();
    queriedAlerts = alertService.list(alertFilter);

    assertThat(queriedAlerts, empty());
  }

  @Test
  public void testUpdateAndQueryByLabel() {
    AlertDefinition definition = createDefinition();
    Alert alert = createAlert(definition);

    alert.setMessage("New Message");
    alert = alertService.create(alert);

    AlertLabel oldLabel1 =
        new AlertLabel(
            alert,
            KnownAlertLabels.UNIVERSE_UUID.labelName(),
            definition.getLabelValue(KnownAlertLabels.UNIVERSE_UUID));
    AlertLabel oldLabel2 =
        new AlertLabel(
            alert,
            KnownAlertLabels.UNIVERSE_NAME.labelName(),
            definition.getLabelValue(KnownAlertLabels.UNIVERSE_NAME));
    AlertFilter filter = AlertFilter.builder().customerUuid(cust1.uuid).label(oldLabel1).build();
    List<Alert> queriedAlerts = alertService.list(filter);

    assertThat(queriedAlerts, hasSize(1));
    Alert queriedAlert = queriedAlerts.get(0);
    assertThat(queriedAlert.getMessage(), is("New Message"));
    assertThat(queriedAlert.getLabels(), hasItems(oldLabel1, oldLabel2));
  }

  @Test
  public void testDelete() {
    AlertDefinition definition = createDefinition();
    Alert alert = createAlert(definition);
    alert = alertService.create(alert);
    alert.delete();

    Alert queriedAlert = alertService.get(alert.getUuid());

    assertThat(queriedAlert, Matchers.nullValue());
  }

  @Test
  public void testAlertsDiffCustomer() {
    AlertDefinition definition = createDefinition();
    Alert alert1 = ModelFactory.createAlert(cust1, definition);

    Customer cust2 = ModelFactory.testCustomer();
    Universe universe2 = ModelFactory.createUniverse(cust2.getCustomerId());
    AlertDefinition definition2 = ModelFactory.createAlertDefinition(cust2, universe2);
    Alert alert2 = ModelFactory.createAlert(cust2, definition2);

    AlertFilter filter1 = AlertFilter.builder().customerUuid(cust1.getUuid()).build();
    List<Alert> cust1Alerts = alertService.list(filter1);

    AlertFilter filter2 = AlertFilter.builder().customerUuid(cust2.getUuid()).build();
    List<Alert> cust2Alerts = alertService.list(filter2);

    assertThat(cust1Alerts, hasItem(alert1));
    assertThat(cust2Alerts, hasItem(alert2));

    assertThat(cust2Alerts, not(hasItem(alert1)));
    assertThat(cust1Alerts, not(hasItem(alert2)));
  }

  @Test
  public void testQueryByVariousFilters() {
    AlertDefinition definition = createDefinition();
    Alert alert1 = ModelFactory.createAlert(cust1, definition);

    Customer cust2 = ModelFactory.testCustomer();
    Universe universe2 = ModelFactory.createUniverse(cust2.getCustomerId());
    AlertDefinition definition2 = ModelFactory.createAlertDefinition(cust2, universe2);
    Alert alert2 = ModelFactory.createAlert(cust2, definition2);
    alert2.setState(Alert.State.RESOLVED);
    alert2.setTargetState(Alert.State.RESOLVED);
    alert2.setErrCode(KnownAlertCodes.ALERT_MANAGER_FAILURE.name());

    alertService.save(alert2);

    AlertFilter filter = AlertFilter.builder().customerUuid(cust1.getUuid()).build();
    queryAndAssertByFilter(filter, definition);

    filter = AlertFilter.builder().states(Alert.State.CREATED).build();
    queryAndAssertByFilter(filter, definition);

    filter = AlertFilter.builder().targetStates(Alert.State.ACTIVE).build();
    queryAndAssertByFilter(filter, definition);

    filter =
        AlertFilter.builder()
            .label(
                KnownAlertLabels.UNIVERSE_UUID,
                definition.getLabelValue(KnownAlertLabels.UNIVERSE_UUID))
            .build();
    queryAndAssertByFilter(filter, definition);

    filter = AlertFilter.builder().errorCode(KnownAlertCodes.CUSTOMER_ALERT).build();
    queryAndAssertByFilter(filter, definition);

    filter = AlertFilter.builder().definitionUuids(definition.getUuid()).build();
    queryAndAssertByFilter(filter, definition);

    filter = AlertFilter.builder().excludeUuids(alert2.getUuid()).build();
    queryAndAssertByFilter(filter, definition);
  }

  private void queryAndAssertByFilter(AlertFilter filter, AlertDefinition definition) {
    List<Alert> alerts = alertService.list(filter);
    assertThat(alerts, hasSize(1));
    Alert alert = alerts.get(0);
    assertTestAlert(alert, definition);
  }

  public AlertDefinition createDefinition() {
    Universe universe = ModelFactory.createUniverse(cust1.getCustomerId());
    return ModelFactory.createAlertDefinition(cust1, universe);
  }

  private static Alert createAlert(AlertDefinition definition) {
    List<AlertLabel> labels =
        definition
            .getEffectiveLabels()
            .stream()
            .map(l -> new AlertLabel(l.getName(), l.getValue()))
            .collect(Collectors.toList());
    return new Alert()
        .setCustomerUUID(definition.getCustomerUUID())
        .setErrCode(KnownAlertCodes.CUSTOMER_ALERT)
        .setType(KnownAlertTypes.Error)
        .setMessage("Universe on fire!")
        .setSendEmail(true)
        .setDefinitionUUID(definition.getUuid())
        .setLabels(labels);
  }

  private void assertTestAlert(Alert alert, AlertDefinition definition) {
    AlertLabel label =
        new AlertLabel(
            alert,
            KnownAlertLabels.UNIVERSE_UUID.labelName(),
            definition.getLabelValue(KnownAlertLabels.UNIVERSE_UUID));
    AlertLabel label2 =
        new AlertLabel(
            alert,
            KnownAlertLabels.UNIVERSE_NAME.labelName(),
            definition.getLabelValue(KnownAlertLabels.UNIVERSE_NAME));
    AlertLabel label3 =
        new AlertLabel(
            alert,
            KnownAlertLabels.TARGET_UUID.labelName(),
            definition.getLabelValue(KnownAlertLabels.TARGET_UUID));
    AlertLabel label4 =
        new AlertLabel(
            alert,
            KnownAlertLabels.TARGET_NAME.labelName(),
            definition.getLabelValue(KnownAlertLabels.TARGET_NAME));
    AlertLabel label5 =
        new AlertLabel(
            alert,
            KnownAlertLabels.TARGET_TYPE.labelName(),
            definition.getLabelValue(KnownAlertLabels.TARGET_TYPE));
    assertThat(alert.getCustomerUUID(), is(cust1.uuid));
    assertThat(alert.getErrCode(), is(KnownAlertCodes.CUSTOMER_ALERT.name()));
    assertThat(alert.getType(), is(KnownAlertTypes.Error.name()));
    assertThat(alert.getMessage(), is("Universe on fire!"));
    assertThat(alert.getDefinitionUUID(), equalTo(definition.getUuid()));
    assertTrue(alert.isSendEmail());
    assertThat(alert.getLabels(), hasItems(label, label2, label3, label4, label5));
  }
}
