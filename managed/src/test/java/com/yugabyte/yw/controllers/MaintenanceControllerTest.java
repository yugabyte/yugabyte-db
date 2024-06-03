// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertBadRequest;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.contains;
import static org.hamcrest.Matchers.containsInAnyOrder;
import static org.hamcrest.Matchers.equalTo;
import static org.hamcrest.Matchers.hasSize;
import static org.hamcrest.Matchers.is;
import static org.hamcrest.Matchers.notNullValue;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.google.common.collect.ImmutableSet;
import com.yugabyte.yw.common.AssertHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.alerts.MaintenanceService;
import com.yugabyte.yw.forms.filters.MaintenanceWindowApiFilter;
import com.yugabyte.yw.forms.paging.MaintenanceWindowPagedApiQuery;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.MaintenanceWindow;
import com.yugabyte.yw.models.MaintenanceWindow.SortBy;
import com.yugabyte.yw.models.MaintenanceWindow.State;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.helpers.CommonUtils;
import com.yugabyte.yw.models.paging.MaintenanceWindowPagedResponse;
import com.yugabyte.yw.models.paging.PagedQuery;
import java.time.temporal.ChronoUnit;
import java.util.Arrays;
import java.util.List;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;
import play.mvc.Result;

@RunWith(MockitoJUnitRunner.class)
public class MaintenanceControllerTest extends FakeDBApplication {

  private Customer customer;

  private Users user;

  private String authToken;

  private MaintenanceService maintenanceService;

  private MaintenanceWindow maintenanceWindow;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    user = ModelFactory.testUser(customer);
    authToken = user.createAuthToken();

    maintenanceService = app.injector().instanceOf(MaintenanceService.class);
    ;
    maintenanceWindow = createMaintenanceWindow("Test");
  }

  @Test
  public void testGetSuccess() {
    Result result =
        doRequestWithAuthToken(
            "GET",
            "/api/customers/"
                + customer.getUuid()
                + "/maintenance_windows/"
                + maintenanceWindow.getUuid(),
            authToken);
    assertThat(result.status(), equalTo(OK));
    JsonNode windowJson = Json.parse(contentAsString(result));
    MaintenanceWindow window = Json.fromJson(windowJson, MaintenanceWindow.class);

    assertThat(window, equalTo(maintenanceWindow));
  }

  @Test
  public void testGetFailure() {
    UUID uuid = UUID.randomUUID();
    Result result =
        assertPlatformException(
            () ->
                doRequestWithAuthToken(
                    "GET",
                    "/api/customers/" + customer.getUuid() + "/maintenance_windows/" + uuid,
                    authToken));
    AssertHelper.assertBadRequest(result, "Invalid Maintenance Window UUID: " + uuid);
  }

  @Test
  public void testPage() {
    MaintenanceWindow window2 = createMaintenanceWindow("Test2");
    MaintenanceWindow window3 = createMaintenanceWindow("Test3");

    MaintenanceWindowPagedApiQuery query = new MaintenanceWindowPagedApiQuery();
    query.setSortBy(SortBy.name);
    query.setDirection(PagedQuery.SortDirection.DESC);
    query.setFilter(new MaintenanceWindowApiFilter());
    query.setLimit(2);
    query.setOffset(1);
    query.setNeedTotalCount(true);

    Result result =
        doRequestWithAuthTokenAndBody(
            "POST",
            "/api/customers/" + customer.getUuid() + "/maintenance_windows/page",
            authToken,
            Json.toJson(query));
    assertThat(result.status(), equalTo(OK));
    JsonNode windowsJson = Json.parse(contentAsString(result));
    MaintenanceWindowPagedResponse windows =
        Json.fromJson(windowsJson, MaintenanceWindowPagedResponse.class);

    assertThat(windows.isHasNext(), is(false));
    assertThat(windows.isHasPrev(), is(true));
    assertThat(windows.getTotalCount(), equalTo(3));
    assertThat(windows.getEntities(), hasSize(2));
    assertThat(windows.getEntities(), contains(window2, maintenanceWindow));
  }

  @Test
  public void testList() {
    MaintenanceWindow window2 = createMaintenanceWindow("Test2");
    MaintenanceWindow window3 = createMaintenanceWindow("Test3");

    window3.setStartTime(CommonUtils.nowMinusWithoutMillis(2, ChronoUnit.HOURS));
    window3.setEndTime(CommonUtils.nowMinusWithoutMillis(1, ChronoUnit.HOURS));
    maintenanceService.save(window3);

    MaintenanceWindowApiFilter filter = new MaintenanceWindowApiFilter();
    filter.setStates(ImmutableSet.of(State.FINISHED));

    Result result =
        doRequestWithAuthTokenAndBody(
            "POST",
            "/api/customers/" + customer.getUuid() + "/maintenance_windows/list",
            authToken,
            Json.toJson(filter));
    assertThat(result.status(), equalTo(OK));
    JsonNode windowsJson = Json.parse(contentAsString(result));
    List<MaintenanceWindow> windows =
        Arrays.asList(Json.fromJson(windowsJson, MaintenanceWindow[].class));

    assertThat(windows, hasSize(1));
    assertThat(windows, containsInAnyOrder(window3));
  }

  @Test
  public void testCreate() {
    maintenanceWindow.setUuid(null);
    maintenanceWindow.setCreateTime(null);

    Result result =
        doRequestWithAuthTokenAndBody(
            "POST",
            "/api/customers/" + customer.getUuid() + "/maintenance_windows",
            authToken,
            Json.toJson(maintenanceWindow));
    assertThat(result.status(), equalTo(OK));
    JsonNode configurationJson = Json.parse(contentAsString(result));
    MaintenanceWindow window = Json.fromJson(configurationJson, MaintenanceWindow.class);

    assertThat(window.getUuid(), notNullValue());
    assertThat(window.getCreateTime(), notNullValue());
    assertThat(window.getStartTime(), equalTo(maintenanceWindow.getStartTime()));
    assertThat(window.getEndTime(), equalTo(maintenanceWindow.getEndTime()));
    assertThat(window.getName(), equalTo(maintenanceWindow.getName()));
    assertThat(window.getDescription(), equalTo(maintenanceWindow.getDescription()));
    assertThat(window.getCustomerUUID(), equalTo(maintenanceWindow.getCustomerUUID()));
    assertThat(
        window.getAlertConfigurationFilter(),
        equalTo(maintenanceWindow.getAlertConfigurationFilter()));
  }

  @Test
  public void testCreateConfigurationFailure() {
    maintenanceWindow.setUuid(null);
    maintenanceWindow.setName(null);

    Result result =
        assertPlatformException(
            () ->
                doRequestWithAuthTokenAndBody(
                    "POST",
                    "/api/customers/" + customer.getUuid() + "/maintenance_windows",
                    authToken,
                    Json.toJson(maintenanceWindow)));
    assertBadRequest(result, "{\"name\":[\"must not be null\"]}");
  }

  @Test
  public void testUpdateConfiguration() {
    maintenanceWindow.setName("New name");

    Result result =
        doRequestWithAuthTokenAndBody(
            "PUT",
            "/api/customers/"
                + customer.getUuid()
                + "/maintenance_windows/"
                + maintenanceWindow.getUuid(),
            authToken,
            Json.toJson(maintenanceWindow));
    assertThat(result.status(), equalTo(OK));
    JsonNode windowJson = Json.parse(contentAsString(result));
    MaintenanceWindow window = Json.fromJson(windowJson, MaintenanceWindow.class);

    assertThat(window.getName(), equalTo("New name"));
  }

  @Test
  public void testUpdateConfigurationFailure() {
    maintenanceWindow.setAlertConfigurationFilter(null);

    Result result =
        assertPlatformException(
            () ->
                doRequestWithAuthTokenAndBody(
                    "PUT",
                    "/api/customers/"
                        + customer.getUuid()
                        + "/maintenance_windows/"
                        + maintenanceWindow.getUuid(),
                    authToken,
                    Json.toJson(maintenanceWindow)));
    assertBadRequest(result, "{\"alertConfigurationFilter\":[\"must not be null\"]}");
  }

  @Test
  public void testDeleteConfiguration() {
    Result result =
        doRequestWithAuthToken(
            "DELETE",
            "/api/customers/"
                + customer.getUuid()
                + "/maintenance_windows/"
                + maintenanceWindow.getUuid(),
            authToken);
    assertThat(result.status(), equalTo(OK));
  }

  private MaintenanceWindow createMaintenanceWindow(String name) {
    return ModelFactory.createMaintenanceWindow(customer.getUuid(), window -> window.setName(name));
  }
}
