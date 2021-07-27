// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.models;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.equalTo;
import static org.hamcrest.Matchers.is;
import static org.hamcrest.Matchers.notNullValue;
import static org.hamcrest.Matchers.nullValue;
import static org.junit.Assert.assertThrows;

import com.yugabyte.yw.common.AlertDefinitionTemplate;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.YWServiceException;
import com.yugabyte.yw.common.alerts.AlertDefinitionGroupService;
import com.yugabyte.yw.common.alerts.AlertDefinitionService;
import com.yugabyte.yw.common.alerts.AlertRouteService;
import com.yugabyte.yw.common.alerts.AlertService;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import io.ebean.DataIntegrityException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;

@RunWith(MockitoJUnitRunner.class)
public class AlertRouteServiceTest extends FakeDBApplication {

  private static final String ALERT_ROUTE_NAME = "Test AlertRoute";

  private Customer defaultCustomer;
  private UUID customerUUID;
  private AlertReceiver receiver;
  private AlertService alertService = new AlertService();
  private AlertDefinitionService alertDefinitionService = new AlertDefinitionService(alertService);
  private AlertDefinitionGroupService alertDefinitionGroupService;
  private AlertRouteService alertRouteService;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    customerUUID = defaultCustomer.getUuid();
    receiver = ModelFactory.createEmailReceiver(defaultCustomer, "Test AlertReceiver");

    alertDefinitionGroupService =
        new AlertDefinitionGroupService(
            alertDefinitionService, new SettableRuntimeConfigFactory(app.config()));
    alertRouteService = new AlertRouteService(alertDefinitionGroupService);
  }

  @Test
  public void testGet() {
    AlertRoute route =
        ModelFactory.createAlertRoute(
            customerUUID, ALERT_ROUTE_NAME, Collections.singletonList(receiver));

    AlertRoute fromDb = alertRouteService.get(customerUUID, route.getUuid());
    assertThat(fromDb, notNullValue());
    assertThat(fromDb, equalTo(route));

    // Check get for random UUID - should return null.
    assertThat(alertRouteService.get(customerUUID, UUID.randomUUID()), nullValue());
  }

  @Test
  public void testGetOrBadRequest_HappyPath() {
    AlertRoute route =
        ModelFactory.createAlertRoute(
            customerUUID, ALERT_ROUTE_NAME, Collections.singletonList(receiver));

    AlertRoute fromDb = alertRouteService.getOrBadRequest(customerUUID, route.getUuid());
    assertThat(fromDb, notNullValue());
    assertThat(fromDb, equalTo(route));

    // Should raise an exception for random UUID.
    final UUID uuid = UUID.randomUUID();
    YWServiceException exception =
        assertThrows(
            YWServiceException.class,
            () -> {
              alertRouteService.getOrBadRequest(customerUUID, uuid);
            });
    assertThat(exception.getMessage(), equalTo("Invalid Alert Route UUID: " + uuid));
  }

  @Test
  public void testListByCustomer() {
    List<AlertRoute> routes = new ArrayList<>();
    for (int i = 0; i < 10; i++) {
      routes.add(
          ModelFactory.createAlertRoute(
              customerUUID, ALERT_ROUTE_NAME + " " + i, Collections.singletonList(receiver)));
    }

    List<AlertRoute> routes2 = alertRouteService.listByCustomer(customerUUID);
    assertThat(routes2.size(), equalTo(routes.size()));
    for (AlertRoute route : routes) {
      assertThat(routes2.contains(route), is(true));
    }
  }

  @Test
  public void testCreate_UnsavedReceiver_Fail() {
    AlertReceiver receiver = new AlertReceiver();
    receiver.setUuid(UUID.randomUUID());
    AlertRoute route =
        new AlertRoute()
            .setCustomerUUID(customerUUID)
            .setName(ALERT_ROUTE_NAME)
            .setReceiversList(Collections.singletonList(receiver));
    assertThrows(DataIntegrityException.class, () -> alertRouteService.save(route));
  }

  @Test
  public void testCreate_NoReceivers_Fail() {
    final AlertRoute route =
        new AlertRoute()
            .setCustomerUUID(customerUUID)
            .setName(ALERT_ROUTE_NAME)
            .setReceiversList(Collections.emptyList());
    YWServiceException exception =
        assertThrows(YWServiceException.class, () -> alertRouteService.save(route));
    assertThat(exception.getMessage(), equalTo("Can't save alert route without receivers."));
  }

  @Test
  public void testCreate_NonDefaultRoute_HappyPath() {
    AlertRoute route =
        new AlertRoute()
            .setCustomerUUID(customerUUID)
            .setName(ALERT_ROUTE_NAME)
            .setReceiversList(Collections.singletonList(receiver));
    alertRouteService.save(route);
    assertThat(alertRouteService.getDefaultRoute(customerUUID), nullValue());
  }

  @Test
  public void testCreate_DefaultRoute_HappyPath() {
    AlertRoute defaultRoute =
        ModelFactory.createAlertRoute(
                customerUUID, ALERT_ROUTE_NAME, Collections.singletonList(receiver))
            .setDefaultRoute(true);
    alertRouteService.save(defaultRoute);
    assertThat(alertRouteService.getDefaultRoute(customerUUID), equalTo(defaultRoute));
  }

  @Test
  public void testSave_UpdateToNonDefault_Fail() {
    AlertRoute defaultRoute =
        ModelFactory.createAlertRoute(
                customerUUID, ALERT_ROUTE_NAME, Collections.singletonList(receiver))
            .setDefaultRoute(true);
    defaultRoute = alertRouteService.save(defaultRoute);

    final AlertRoute updatedRoute = defaultRoute.setDefaultRoute(false);
    YWServiceException exception =
        assertThrows(YWServiceException.class, () -> alertRouteService.save(updatedRoute));
    assertThat(
        exception.getMessage(),
        equalTo(
            "Can't set the alert route as non-default. Make another route as default at first."));
  }

  @Test
  public void testSave_SwitchDefaultRoute_HappyPath() {
    AlertRoute oldDefaultRoute = alertRouteService.createDefaultRoute(customerUUID);
    AlertRoute newDefaultRoute =
        new AlertRoute()
            .setCustomerUUID(customerUUID)
            .setName(ALERT_ROUTE_NAME)
            .setReceiversList(Collections.singletonList(receiver))
            .setDefaultRoute(true);
    newDefaultRoute = alertRouteService.save(newDefaultRoute);

    assertThat(newDefaultRoute.isDefaultRoute(), is(true));
    assertThat(newDefaultRoute, equalTo(alertRouteService.getDefaultRoute(customerUUID)));
    assertThat(
        alertRouteService.get(customerUUID, oldDefaultRoute.getUuid()).isDefaultRoute(), is(false));
  }

  @Test
  public void testDelete_HappyPath() {
    AlertRoute route =
        ModelFactory.createAlertRoute(
            customerUUID, ALERT_ROUTE_NAME, Collections.singletonList(receiver));

    assertThat(alertRouteService.get(customerUUID, route.getUuid()), notNullValue());
    alertRouteService.delete(customerUUID, route.getUuid());
    assertThat(alertRouteService.get(customerUUID, route.getUuid()), nullValue());
  }

  @Test
  public void testDelete_DefaultRoute_Fail() {
    AlertRoute defaultRoute = alertRouteService.createDefaultRoute(customerUUID);
    YWServiceException exception =
        assertThrows(
            YWServiceException.class,
            () -> {
              alertRouteService.delete(customerUUID, defaultRoute.getUuid());
            });
    assertThat(
        exception.getMessage(),
        equalTo(
            "Unable to delete default alert route "
                + defaultRoute.getUuid()
                + ", make another route default at first."));
  }

  @Test
  public void testDelete_UsedByAlertDefinitionGroups_Fail() {
    AlertRoute route =
        ModelFactory.createAlertRoute(
            customerUUID, ALERT_ROUTE_NAME, Collections.singletonList(receiver));
    AlertDefinitionGroup group =
        alertDefinitionGroupService.createGroupFromTemplate(
            defaultCustomer, AlertDefinitionTemplate.MEMORY_CONSUMPTION);
    group.setRouteUUID(route.getUuid());
    group.save();

    YWServiceException exception =
        assertThrows(
            YWServiceException.class,
            () -> {
              alertRouteService.delete(customerUUID, route.getUuid());
            });
    assertThat(
        exception.getMessage(),
        equalTo(
            "Unable to delete alert route: "
                + route.getUuid()
                + ". 1 alert definition groups are linked to it. Examples: ["
                + group.getName()
                + "]"));
  }
}
