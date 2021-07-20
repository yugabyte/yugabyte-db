// Copyright (c) YugaByte, Inc.
package com.yugabyte.yw.models;

import static com.yugabyte.yw.common.ModelFactory.createAlertDefinitionGroup;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.containsInAnyOrder;
import static org.hamcrest.Matchers.empty;
import static org.hamcrest.Matchers.equalTo;
import static org.hamcrest.Matchers.hasItem;
import static org.hamcrest.Matchers.hasItems;
import static org.hamcrest.Matchers.hasSize;
import static org.hamcrest.Matchers.is;
import static org.hamcrest.Matchers.not;
import static org.junit.Assert.assertTrue;

import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.alerts.AlertService;
import com.yugabyte.yw.models.filters.AlertFilter;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.helpers.KnownAlertCodes;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import java.util.List;
import java.util.stream.Collectors;
import java.util.stream.Stream;
import junitparams.JUnitParamsRunner;
import org.hamcrest.Matchers;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.junit.MockitoJUnit;
import org.mockito.junit.MockitoRule;

@RunWith(JUnitParamsRunner.class)
public class AlertTest extends FakeDBApplication {

  @Rule public MockitoRule rule = MockitoJUnit.rule();

  private Customer cust1;
  private Universe universe;
  private AlertDefinitionGroup group;
  private AlertDefinition definition;

  @InjectMocks private AlertService alertService;

  @Before
  public void setUp() {
    cust1 = ModelFactory.testCustomer("Customer 1");
    universe = ModelFactory.createUniverse(cust1.getCustomerId());
    group = createAlertDefinitionGroup(cust1, universe);
    definition = createDefinition();
  }

  @Test
  public void testAddAndQueryByUuid() {
    Alert alert = createAlert();

    alert = alertService.save(alert);

    Alert queriedAlert = alertService.get(alert.getUuid());

    assertTestAlert(queriedAlert, definition);
  }

  @Test
  public void testQueryByLotUuids() {
    List<Alert> alerts =
        Stream.generate(() -> createAlert())
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
    Alert alert = createAlert();

    alert.setMessage("New Message");
    alert = alertService.save(alert);

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
    Alert alert = createAlert();
    alert = alertService.save(alert);
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

    filter = AlertFilter.builder().state(Alert.State.CREATED).build();
    queryAndAssertByFilter(filter, definition);

    filter = AlertFilter.builder().targetState(Alert.State.ACTIVE).build();
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

    filter = AlertFilter.builder().definitionUuid(definition.getUuid()).build();
    queryAndAssertByFilter(filter, definition);

    filter = AlertFilter.builder().excludeUuid(alert2.getUuid()).build();
    queryAndAssertByFilter(filter, definition);
  }

  @Test
  public void testAcknowledge() {
    Alert alert = createAlert();
    alert = alertService.save(alert);

    AlertFilter filter = AlertFilter.builder().uuid(alert.getUuid()).build();
    alertService.acknowledge(filter);

    Alert queriedAlert = alertService.get(alert.getUuid());

    assertThat(queriedAlert.getState(), equalTo(Alert.State.ACKNOWLEDGED));
    assertThat(queriedAlert.getTargetState(), equalTo(Alert.State.ACKNOWLEDGED));

    alertService.markResolved(filter);
    alertService.acknowledge(filter);

    queriedAlert = alertService.get(alert.getUuid());

    // Resolved without sending email.
    assertThat(queriedAlert.getState(), equalTo(Alert.State.RESOLVED));
    assertThat(queriedAlert.getTargetState(), equalTo(Alert.State.RESOLVED));
  }

  private void queryAndAssertByFilter(AlertFilter filter, AlertDefinition definition) {
    List<Alert> alerts = alertService.list(filter);
    assertThat(alerts, hasSize(1));
    Alert alert = alerts.get(0);
    assertTestAlert(alert, definition);
  }

  public AlertDefinition createDefinition() {
    return ModelFactory.createAlertDefinition(cust1, universe, group);
  }

  private Alert createAlert() {
    List<AlertLabel> labels =
        definition
            .getEffectiveLabels(group, AlertDefinitionGroup.Severity.SEVERE)
            .stream()
            .map(l -> new AlertLabel(l.getName(), l.getValue()))
            .collect(Collectors.toList());
    return new Alert()
        .setCustomerUUID(definition.getCustomerUUID())
        .setErrCode(KnownAlertCodes.CUSTOMER_ALERT)
        .setSeverity(AlertDefinitionGroup.Severity.SEVERE)
        .setMessage("Universe on fire!")
        .setSendEmail(true)
        .setDefinitionUuid(definition.getUuid())
        .setGroupUuid(group.getUuid())
        .setGroupType(group.getTargetType())
        .setLabels(labels);
  }

  private void assertTestAlert(Alert alert, AlertDefinition definition) {
    AlertLabel customerUuidLabel =
        new AlertLabel(
            alert,
            KnownAlertLabels.CUSTOMER_UUID.labelName(),
            definition.getCustomerUUID().toString());
    AlertLabel universeUuidLabel =
        new AlertLabel(
            alert,
            KnownAlertLabels.UNIVERSE_UUID.labelName(),
            definition.getLabelValue(KnownAlertLabels.UNIVERSE_UUID));
    AlertLabel universeNameLabel =
        new AlertLabel(
            alert,
            KnownAlertLabels.UNIVERSE_NAME.labelName(),
            definition.getLabelValue(KnownAlertLabels.UNIVERSE_NAME));
    AlertLabel targetUuidLabel =
        new AlertLabel(
            alert,
            KnownAlertLabels.TARGET_UUID.labelName(),
            definition.getLabelValue(KnownAlertLabels.TARGET_UUID));
    AlertLabel targetNameLabel =
        new AlertLabel(
            alert,
            KnownAlertLabels.TARGET_NAME.labelName(),
            definition.getLabelValue(KnownAlertLabels.TARGET_NAME));
    AlertLabel targetTypeLabel =
        new AlertLabel(
            alert,
            KnownAlertLabels.TARGET_TYPE.labelName(),
            definition.getLabelValue(KnownAlertLabels.TARGET_TYPE));
    AlertLabel errorCodeLabel =
        new AlertLabel(
            alert, KnownAlertLabels.ERROR_CODE.labelName(), KnownAlertCodes.CUSTOMER_ALERT.name());
    AlertLabel groupUuidLabel =
        new AlertLabel(
            alert, KnownAlertLabels.GROUP_UUID.labelName(), definition.getGroupUUID().toString());
    AlertLabel groupTypeLabel =
        new AlertLabel(
            alert, KnownAlertLabels.GROUP_TYPE.labelName(), group.getTargetType().name());
    AlertLabel severityLabel =
        new AlertLabel(
            alert,
            KnownAlertLabels.SEVERITY.labelName(),
            AlertDefinitionGroup.Severity.SEVERE.name());
    AlertLabel definitionUuidLabel =
        new AlertLabel(
            alert, KnownAlertLabels.DEFINITION_UUID.labelName(), definition.getUuid().toString());
    AlertLabel definitionActiveLabel =
        new AlertLabel(
            alert,
            KnownAlertLabels.DEFINITION_ACTIVE.labelName(),
            String.valueOf(group.isActive()));
    AlertLabel definitionNameLabel =
        new AlertLabel(alert, KnownAlertLabels.DEFINITION_NAME.labelName(), group.getName());
    assertThat(alert.getCustomerUUID(), is(cust1.uuid));
    assertThat(alert.getErrCode(), is(KnownAlertCodes.CUSTOMER_ALERT.name()));
    assertThat(alert.getSeverity(), is(AlertDefinitionGroup.Severity.SEVERE));
    assertThat(alert.getMessage(), is("Universe on fire!"));
    assertThat(alert.getDefinitionUuid(), equalTo(definition.getUuid()));
    assertThat(alert.getGroupUuid(), equalTo(group.getUuid()));
    assertTrue(alert.isSendEmail());
    assertThat(
        alert.getLabels(),
        containsInAnyOrder(
            customerUuidLabel,
            universeUuidLabel,
            universeNameLabel,
            targetUuidLabel,
            targetNameLabel,
            targetTypeLabel,
            errorCodeLabel,
            groupUuidLabel,
            groupTypeLabel,
            severityLabel,
            definitionUuidLabel,
            definitionNameLabel,
            definitionActiveLabel));
  }
}
