// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.common.alerts;

import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.equalTo;
import static org.hamcrest.Matchers.is;
import static org.hamcrest.Matchers.notNullValue;
import static org.hamcrest.Matchers.nullValue;
import static org.junit.Assert.assertThrows;

import com.yugabyte.yw.common.AlertTemplate;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.YWServiceException;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.models.AlertDefinitionGroup;
import com.yugabyte.yw.models.AlertReceiver;
import com.yugabyte.yw.models.AlertRoute;
import com.yugabyte.yw.models.Customer;
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
  private AlertReceiverService alertReceiverService;
  private AlertRouteService alertRouteService;

  @Before
  public void setUp() {
    defaultCustomer = ModelFactory.testCustomer();
    customerUUID = defaultCustomer.getUuid();
    receiver = ModelFactory.createEmailReceiver(customerUUID, "Test AlertReceiver");

    alertDefinitionGroupService =
        new AlertDefinitionGroupService(
            alertDefinitionService, new SettableRuntimeConfigFactory(app.config()));
    alertReceiverService = new AlertReceiverService();
    alertRouteService = new AlertRouteService(alertReceiverService, alertDefinitionGroupService);
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
  public void testGetOrBadRequest() {
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
    assertThrows(YWServiceException.class, () -> alertRouteService.save(route));
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
    assertThat(
        exception.getMessage(),
        equalTo("Unable to create/update alert route: Can't save alert route without receivers."));
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
        new AlertRoute()
            .setCustomerUUID(customerUUID)
            .setName(ALERT_ROUTE_NAME)
            .setReceiversList(Collections.singletonList(receiver))
            .setDefaultRoute(true);
    alertRouteService.save(defaultRoute);
    assertThat(alertRouteService.getDefaultRoute(customerUUID), equalTo(defaultRoute));
  }

  @Test
  public void testSave_UpdateToNonDefault_Fail() {
    AlertRoute defaultRoute =
        new AlertRoute()
            .setCustomerUUID(customerUUID)
            .setName(ALERT_ROUTE_NAME)
            .setReceiversList(Collections.singletonList(receiver))
            .setDefaultRoute(true);
    defaultRoute = alertRouteService.save(defaultRoute);

    final AlertRoute updatedRoute = defaultRoute.setDefaultRoute(false);
    YWServiceException exception =
        assertThrows(YWServiceException.class, () -> alertRouteService.save(updatedRoute));
    assertThat(
        exception.getMessage(),
        equalTo(
            "Unable to create/update alert route: Can't set the alert route as non-default."
                + " Make another route as default at first."));
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
        alertDefinitionGroupService
            .createDefinitionTemplate(defaultCustomer, AlertTemplate.MEMORY_CONSUMPTION)
            .getDefaultGroup();
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

  @Test
  public void testSave_DuplicateName_Fail() {
    AlertRoute route =
        ModelFactory.createAlertRoute(
            customerUUID, ALERT_ROUTE_NAME + " 1", Collections.singletonList(receiver));

    ModelFactory.createAlertRoute(
        customerUUID, ALERT_ROUTE_NAME + " 2", Collections.singletonList(receiver));

    AlertRoute updatedRoute = alertRouteService.get(customerUUID, route.getUuid());
    // Setting duplicate name.
    updatedRoute.setName(ALERT_ROUTE_NAME + " 2");

    YWServiceException exception =
        assertThrows(
            YWServiceException.class,
            () -> {
              alertRouteService.save(updatedRoute);
            });
    assertThat(
        exception.getMessage(),
        equalTo("Unable to create/update alert route: Alert route with such name already exists."));
  }

  @Test
  public void testSave_LongName_Fail() {
    StringBuilder longName = new StringBuilder();
    while (longName.length() < AlertRoute.MAX_NAME_LENGTH / 4) {
      longName.append(ALERT_ROUTE_NAME);
    }

    AlertRoute route =
        new AlertRoute()
            .setCustomerUUID(customerUUID)
            .setName(longName.toString())
            .setReceiversList(Collections.singletonList(receiver));

    YWServiceException exception =
        assertThrows(
            YWServiceException.class,
            () -> {
              alertRouteService.save(route);
            });
    assertThat(
        exception.getMessage(),
        equalTo("Unable to create/update alert route: Name length (63) is exceeded."));
  }
}
