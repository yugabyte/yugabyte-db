// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertBadRequest;
import static com.yugabyte.yw.common.AssertHelper.assertYWSE;
import static com.yugabyte.yw.common.FakeApiHelper.doRequestWithAuthToken;
import static com.yugabyte.yw.common.FakeApiHelper.doRequestWithAuthTokenAndBody;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.contains;
import static org.hamcrest.Matchers.containsInAnyOrder;
import static org.hamcrest.Matchers.equalTo;
import static org.hamcrest.Matchers.hasSize;
import static org.hamcrest.Matchers.is;
import static org.hamcrest.Matchers.notNullValue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableMap;
import com.google.common.collect.ImmutableSet;
import com.yugabyte.yw.common.AlertDefinitionTemplate;
import com.yugabyte.yw.common.AssertHelper;
import com.yugabyte.yw.common.EmailFixtures;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.ValidatingFormFactory;
import com.yugabyte.yw.common.alerts.AlertDefinitionGroupService;
import com.yugabyte.yw.common.alerts.AlertDefinitionService;
import com.yugabyte.yw.common.alerts.AlertReceiverEmailParams;
import com.yugabyte.yw.common.alerts.AlertReceiverParams;
import com.yugabyte.yw.common.alerts.AlertReceiverSlackParams;
import com.yugabyte.yw.common.alerts.AlertRouteService;
import com.yugabyte.yw.common.alerts.AlertService;
import com.yugabyte.yw.common.alerts.AlertUtils;
import com.yugabyte.yw.common.alerts.SmtpData;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.forms.filters.AlertApiFilter;
import com.yugabyte.yw.forms.filters.AlertDefinitionGroupApiFilter;
import com.yugabyte.yw.forms.filters.AlertDefinitionTemplateApiFilter;
import com.yugabyte.yw.forms.paging.AlertDefinitionGroupPagedApiQuery;
import com.yugabyte.yw.forms.paging.AlertPagedApiQuery;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.AlertDefinition;
import com.yugabyte.yw.models.AlertDefinitionGroup;
import com.yugabyte.yw.models.AlertDefinitionGroupTarget;
import com.yugabyte.yw.models.AlertDefinitionGroupThreshold;
import com.yugabyte.yw.models.AlertReceiver;
import com.yugabyte.yw.models.AlertReceiver.TargetType;
import com.yugabyte.yw.models.AlertRoute;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.common.Unit;
import com.yugabyte.yw.models.filters.AlertFilter;
import com.yugabyte.yw.models.paging.AlertDefinitionGroupPagedResponse;
import com.yugabyte.yw.models.paging.AlertPagedResponse;
import com.yugabyte.yw.models.paging.PagedQuery;
import java.io.IOException;
import java.util.Arrays;
import java.util.Collections;
import java.util.Date;
import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;
import play.mvc.Result;

@RunWith(MockitoJUnitRunner.class)
public class AlertControllerTest extends FakeDBApplication {

  private Customer customer;

  private Users user;

  private String authToken;

  private Universe universe;

  @Mock private ValidatingFormFactory formFactory;

  @InjectMocks private AlertController controller;

  private SmtpData defaultSmtp = EmailFixtures.createSmtpData();

  private int alertReceiverIndex;

  private int alertRouteIndex;

  private AlertService alertService;
  private AlertDefinitionService alertDefinitionService;
  private AlertDefinitionGroupService alertDefinitionGroupService;
  private AlertRouteService alertRouteService;

  private AlertDefinitionGroup alertDefinitionGroup;
  private AlertDefinition alertDefinition;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    user = ModelFactory.testUser(customer);
    authToken = user.createAuthToken();

    universe = ModelFactory.createUniverse();

    alertService = new AlertService();
    alertDefinitionService = new AlertDefinitionService(alertService);
    alertDefinitionGroupService =
        new AlertDefinitionGroupService(
            alertDefinitionService, new SettableRuntimeConfigFactory(app.config()));
    alertRouteService = new AlertRouteService(alertDefinitionGroupService);
    alertDefinitionGroup = ModelFactory.createAlertDefinitionGroup(customer, universe);
    alertDefinition = ModelFactory.createAlertDefinition(customer, universe, alertDefinitionGroup);

    controller.setAlertService(alertService);
    controller.setAlertDefinitionGroupService(alertDefinitionGroupService);
  }

  private void checkEmptyAnswer(String url) {
    Result result = doRequestWithAuthToken("GET", url, authToken);
    assertThat(result.status(), equalTo(OK));
    assertThat(contentAsString(result), equalTo("[]"));
  }

  private AlertReceiverParams getAlertReceiverParamsForTests() {
    AlertReceiverEmailParams arParams = new AlertReceiverEmailParams();
    arParams.recipients = Collections.singletonList("test@test.com");
    arParams.smtpData = defaultSmtp;
    return arParams;
  }

  private ObjectNode getAlertReceiverJson() {
    ObjectNode data = Json.newObject();
    data.put("name", getAlertReceiverName());
    data.put("params", Json.toJson(getAlertReceiverParamsForTests()));
    return data;
  }

  private AlertReceiver receiverFromJson(JsonNode json) {
    ObjectMapper mapper = new ObjectMapper();
    try {
      return mapper.treeToValue(json, AlertReceiver.class);
    } catch (JsonProcessingException e) {
      fail("Bad json format.");
      return null;
    }
  }

  private AlertReceiver createAlertReceiver() {
    ObjectNode receiverFormDataJson = getAlertReceiverJson();
    Result result =
        doRequestWithAuthTokenAndBody(
            "POST",
            "/api/customers/" + customer.uuid + "/alert_receivers",
            authToken,
            receiverFormDataJson);
    assertThat(result.status(), equalTo(OK));
    return receiverFromJson(Json.parse(contentAsString(result)));
  }

  @Test
  public void testCreateAndListAlertReceiver_OkResult() {
    checkEmptyAnswer("/api/customers/" + customer.uuid + "/alert_receivers");

    AlertReceiver createdReceiver = createAlertReceiver();
    assertThat(createdReceiver.getUuid(), notNullValue());

    assertThat(
        AlertUtils.getJsonTypeName(createdReceiver.getParams()), equalTo(TargetType.Email.name()));
    assertThat(createdReceiver.getParams(), equalTo(getAlertReceiverParamsForTests()));

    Result result =
        doRequestWithAuthToken(
            "GET", "/api/customers/" + customer.uuid + "/alert_receivers", authToken);

    assertThat(result.status(), equalTo(OK));
    JsonNode listedReceivers = Json.parse(contentAsString(result));
    assertThat(listedReceivers.size(), equalTo(1));
    assertThat(receiverFromJson(listedReceivers.get(0)), equalTo(createdReceiver));
  }

  @Test
  public void testCreateAlertReceiver_ErrorResult() {
    checkEmptyAnswer("/api/customers/" + customer.uuid + "/alert_receivers");
    ObjectNode data = Json.newObject();
    data.put("params", Json.toJson(new AlertReceiverEmailParams()));
    Result result =
        assertYWSE(
            () ->
                doRequestWithAuthTokenAndBody(
                    "POST",
                    "/api/customers/" + customer.uuid + "/alert_receivers",
                    authToken,
                    data));

    AssertHelper.assertBadRequest(
        result, "Email parameters: only one of defaultRecipients and recipients[] should be set.");
    checkEmptyAnswer("/api/customers/" + customer.uuid + "/alert_receivers");
  }

  @Test
  public void testGetAlertReceiver_OkResult() {
    AlertReceiver createdReceiver = createAlertReceiver();
    assertThat(createdReceiver.getUuid(), notNullValue());

    Result result =
        doRequestWithAuthToken(
            "GET",
            "/api/customers/" + customer.uuid + "/alert_receivers/" + createdReceiver.getUuid(),
            authToken);
    assertThat(result.status(), equalTo(OK));

    AlertReceiver receiver = receiverFromJson(Json.parse(contentAsString(result)));
    assertThat(receiver, notNullValue());
    assertThat(receiver, equalTo(createdReceiver));
  }

  @Test
  public void testGetAlertReceiver_ErrorResult() {
    UUID uuid = UUID.randomUUID();
    Result result =
        assertYWSE(
            () ->
                doRequestWithAuthToken(
                    "GET",
                    "/api/customers/" + customer.uuid + "/alert_receivers/" + uuid.toString(),
                    authToken));
    AssertHelper.assertBadRequest(result, "Invalid Alert Receiver UUID: " + uuid.toString());
  }

  @Test
  public void testUpdateAlertReceiver_OkResult() {
    AlertReceiver createdReceiver = createAlertReceiver();
    assertThat(createdReceiver.getUuid(), notNullValue());

    AlertReceiverEmailParams params = (AlertReceiverEmailParams) createdReceiver.getParams();
    params.recipients = Collections.singletonList("new@test.com");
    params.smtpData.smtpPort = 1111;
    createdReceiver.setParams(params);

    ObjectNode data = Json.newObject();
    data.put("alertReceiverUUID", createdReceiver.getUuid().toString())
        .put("name", createdReceiver.getName())
        .put("params", Json.toJson(createdReceiver.getParams()));

    Result result =
        doRequestWithAuthTokenAndBody(
            "PUT",
            "/api/customers/"
                + customer.uuid
                + "/alert_receivers/"
                + createdReceiver.getUuid().toString(),
            authToken,
            data);
    assertThat(result.status(), equalTo(OK));

    AlertReceiver updatedReceiver = receiverFromJson(Json.parse(contentAsString(result)));

    assertThat(updatedReceiver, notNullValue());
    assertThat(updatedReceiver, equalTo(createdReceiver));
  }

  @Test
  public void testUpdateAlertReceiver_ErrorResult() {
    AlertReceiver createdReceiver = createAlertReceiver();
    assertThat(createdReceiver.getUuid(), notNullValue());

    createdReceiver.setParams(new AlertReceiverSlackParams());

    ObjectNode data = Json.newObject();
    data.put("alertReceiverUUID", createdReceiver.getUuid().toString())
        .put("name", createdReceiver.getName())
        .put("params", Json.toJson(createdReceiver.getParams()));

    Result result =
        assertYWSE(
            () ->
                doRequestWithAuthTokenAndBody(
                    "PUT",
                    "/api/customers/"
                        + customer.uuid
                        + "/alert_receivers/"
                        + createdReceiver.getUuid().toString(),
                    authToken,
                    data));
    AssertHelper.assertBadRequest(
        result, "Unable to update alert receiver: Slack parameters: channel is empty.");
  }

  @Test
  public void testDeleteAlertReceiver_OkResult() {
    checkEmptyAnswer("/api/customers/" + customer.uuid + "/alert_receivers");

    AlertReceiver createdReceiver = createAlertReceiver();
    assertThat(createdReceiver.getUuid(), notNullValue());

    Result result =
        doRequestWithAuthToken(
            "DELETE",
            "/api/customers/"
                + customer.uuid
                + "/alert_receivers/"
                + createdReceiver.getUuid().toString(),
            authToken);
    assertThat(result.status(), equalTo(OK));

    checkEmptyAnswer("/api/customers/" + customer.uuid + "/alert_receivers");
  }

  @Test
  public void testDeleteAlertReceiver_ErrorResult() {
    UUID uuid = UUID.randomUUID();
    Result result =
        assertYWSE(
            () ->
                doRequestWithAuthToken(
                    "DELETE",
                    "/api/customers/" + customer.uuid + "/alert_receivers/" + uuid.toString(),
                    authToken));
    AssertHelper.assertBadRequest(result, "Invalid Alert Receiver UUID: " + uuid.toString());
  }

  @Test
  public void testDeleteAlertReceiver_LastReceiverInRoute_ErrorResult() {
    checkEmptyAnswer("/api/customers/" + customer.uuid + "/alert_receivers");

    AlertRoute firstRoute = createAlertRoute(false);
    assertNotNull(firstRoute.getUuid());

    AlertRoute secondRoute = createAlertRoute(false);
    assertNotNull(secondRoute.getUuid());

    // Updating second route to have the same routes.
    List<AlertReceiver> receivers = firstRoute.getReceiversList();
    secondRoute.setReceiversList(receivers);
    Result result =
        doRequestWithAuthTokenAndBody(
            "PUT",
            "/api/customers/" + customer.uuid + "/alert_routes/" + secondRoute.getUuid().toString(),
            authToken,
            Json.toJson(secondRoute));
    assertEquals(OK, result.status());

    result =
        doRequestWithAuthToken(
            "DELETE",
            "/api/customers/"
                + customer.uuid
                + "/alert_receivers/"
                + receivers.get(0).getUuid().toString(),
            authToken);
    assertEquals(OK, result.status());

    result =
        assertYWSE(
            () ->
                doRequestWithAuthToken(
                    "DELETE",
                    "/api/customers/"
                        + customer.uuid
                        + "/alert_receivers/"
                        + receivers.get(1).getUuid().toString(),
                    authToken));

    AssertHelper.assertBadRequest(
        result,
        String.format(
            "Unable to delete alert receiver: %s. 2 alert routes have it as a last receiver."
                + " Examples: [%s, %s]",
            receivers.get(1).getUuid(), firstRoute.getName(), secondRoute.getName()));
  }

  private ObjectNode getAlertRouteJson(boolean isDefault) {
    AlertReceiver receiver1 =
        AlertReceiver.create(
            customer.uuid,
            getAlertReceiverName(),
            AlertUtils.createParamsInstance(TargetType.Email));
    AlertReceiver receiver2 =
        AlertReceiver.create(
            customer.uuid,
            getAlertReceiverName(),
            AlertUtils.createParamsInstance(TargetType.Slack));

    ObjectNode data = Json.newObject();
    data.put("name", getAlertRouteName())
        .put("defaultRoute", Boolean.valueOf(isDefault))
        .putArray("receivers")
        .add(receiver1.getUuid().toString())
        .add(receiver2.getUuid().toString());
    return data;
  }

  private AlertRoute routeFromJson(JsonNode json) {
    ObjectMapper mapper = new ObjectMapper();
    List<UUID> receiverUUIDs;
    try {
      receiverUUIDs =
          Arrays.asList(mapper.readValue(json.get("receivers").traverse(), UUID[].class));
      List<AlertReceiver> receivers =
          receiverUUIDs
              .stream()
              .map(uuid -> AlertReceiver.getOrBadRequest(customer.uuid, uuid))
              .collect(Collectors.toList());

      AlertRoute route = new AlertRoute();
      route.setUuid(UUID.fromString(json.get("uuid").asText()));
      route.setName(json.get("name").asText());
      route.setCustomerUUID(UUID.fromString(json.get("customerUUID").asText()));
      route.setReceiversList(receivers);
      route.setDefaultRoute(json.get("defaultRoute").asBoolean());
      return route;
    } catch (IOException e) {
      return null;
    }
  }

  private AlertRoute createAlertRoute(boolean isDefault) {
    ObjectNode routeFormDataJson = getAlertRouteJson(isDefault);
    Result result =
        doRequestWithAuthTokenAndBody(
            "POST",
            "/api/customers/" + customer.uuid + "/alert_routes",
            authToken,
            routeFormDataJson);
    assertThat(result.status(), equalTo(OK));
    return routeFromJson(Json.parse(contentAsString(result)));
  }

  @Test
  public void testCreateAlertRoute_OkResult() {
    checkEmptyAnswer("/api/customers/" + customer.uuid + "/alert_routes");

    AlertRoute createdRoute = createAlertRoute(false);
    assertThat(createdRoute.getUuid(), notNullValue());

    Result result =
        doRequestWithAuthToken(
            "GET", "/api/customers/" + customer.uuid + "/alert_routes", authToken);
    assertThat(result.status(), equalTo(OK));
    JsonNode listedRoutes = Json.parse(contentAsString(result));
    assertThat(listedRoutes.size(), equalTo(1));
    assertThat(routeFromJson(listedRoutes.get(0)), equalTo(createdRoute));
  }

  @Test
  public void testCreateAlertRoute_ErrorResult() {
    checkEmptyAnswer("/api/customers/" + customer.uuid + "/alert_routes");
    ObjectNode data = Json.newObject();
    String alertReceiverUUID = UUID.randomUUID().toString();
    data.put("name", getAlertRouteName())
        .put("defaultRoute", Boolean.FALSE)
        .putArray("receivers")
        .add(alertReceiverUUID);
    Result result =
        assertYWSE(
            () ->
                doRequestWithAuthTokenAndBody(
                    "POST", "/api/customers/" + customer.uuid + "/alert_routes", authToken, data));

    AssertHelper.assertBadRequest(result, "Invalid Alert Receiver UUID: " + alertReceiverUUID);
    checkEmptyAnswer("/api/customers/" + customer.uuid + "/alert_routes");
  }

  @Test
  public void testCreateAlertRouteWithDefaultChange() {
    checkEmptyAnswer("/api/customers/" + customer.uuid + "/alert_routes");

    AlertRoute firstRoute = createAlertRoute(true);
    assertThat(firstRoute.getUuid(), notNullValue());
    assertThat(alertRouteService.getDefaultRoute(customer.uuid), equalTo(firstRoute));

    AlertRoute secondRoute = createAlertRoute(true);
    assertThat(secondRoute.getUuid(), notNullValue());
    assertThat(alertRouteService.getDefaultRoute(customer.uuid), equalTo(secondRoute));
  }

  @Test
  public void testGetAlertRoute_OkResult() {
    AlertRoute createdRoute = createAlertRoute(false);
    assertThat(createdRoute.getUuid(), notNullValue());

    Result result =
        doRequestWithAuthToken(
            "GET",
            "/api/customers/" + customer.uuid + "/alert_routes/" + createdRoute.getUuid(),
            authToken);
    assertThat(result.status(), equalTo(OK));

    AlertRoute route = routeFromJson(Json.parse(contentAsString(result)));
    assertThat(route, notNullValue());
    assertThat(route, equalTo(createdRoute));
  }

  @Test
  public void testGetAlertRoute_ErrorResult() {
    UUID uuid = UUID.randomUUID();
    Result result =
        assertYWSE(
            () ->
                doRequestWithAuthToken(
                    "GET",
                    "/api/customers/" + customer.uuid + "/alert_routes/" + uuid.toString(),
                    authToken));
    AssertHelper.assertBadRequest(result, "Invalid Alert Route UUID: " + uuid.toString());
  }

  @Test
  public void testUpdateAlertRoute_AnotherDefaultRoute() {
    checkEmptyAnswer("/api/customers/" + customer.uuid + "/alert_routes");

    AlertRoute firstRoute = createAlertRoute(true);
    assertThat(firstRoute.getUuid(), notNullValue());
    assertThat(alertRouteService.getDefaultRoute(customer.uuid), equalTo(firstRoute));

    AlertRoute secondRoute = createAlertRoute(false);
    assertThat(secondRoute.getUuid(), notNullValue());
    // To be sure the default route hasn't been changed.
    assertThat(alertRouteService.getDefaultRoute(customer.uuid), equalTo(firstRoute));

    secondRoute.setDefaultRoute(true);

    Result result =
        doRequestWithAuthTokenAndBody(
            "PUT",
            "/api/customers/" + customer.uuid + "/alert_routes/" + secondRoute.getUuid().toString(),
            authToken,
            Json.toJson(secondRoute));
    assertThat(result.status(), is(OK));
    AlertRoute receivedRoute = routeFromJson(Json.parse(contentAsString(result)));

    assertThat(receivedRoute.isDefaultRoute(), is(true));
    assertThat(alertRouteService.getDefaultRoute(customer.uuid), equalTo(secondRoute));
  }

  @Test
  public void testUpdateAlertRoute_ChangeDefaultFlag_ErrorResult() {
    checkEmptyAnswer("/api/customers/" + customer.uuid + "/alert_routes");

    AlertRoute route = createAlertRoute(true);
    assertThat(route.getUuid(), notNullValue());
    assertThat(alertRouteService.getDefaultRoute(customer.uuid), equalTo(route));

    route.setDefaultRoute(false);
    Result result =
        assertYWSE(
            () ->
                doRequestWithAuthTokenAndBody(
                    "PUT",
                    "/api/customers/"
                        + customer.uuid
                        + "/alert_routes/"
                        + route.getUuid().toString(),
                    authToken,
                    Json.toJson(route)));
    AssertHelper.assertBadRequest(
        result,
        "Can't set the alert route as non-default. Make another route as default at first.");
    route.setDefaultRoute(true);
    assertThat(alertRouteService.getDefaultRoute(customer.uuid), equalTo(route));
  }

  @Test
  public void testDeleteAlertRoute_OkResult() {
    checkEmptyAnswer("/api/customers/" + customer.uuid + "/alert_routes");

    AlertRoute createdRoute = createAlertRoute(false);
    assertThat(createdRoute.getUuid(), notNullValue());

    Result result =
        doRequestWithAuthToken(
            "DELETE",
            "/api/customers/"
                + customer.uuid
                + "/alert_routes/"
                + createdRoute.getUuid().toString(),
            authToken);
    assertThat(result.status(), equalTo(OK));

    checkEmptyAnswer("/api/customers/" + customer.uuid + "/alert_routes");
  }

  @Test
  public void testDeleteAlertRoute_InvalidUUID_ErrorResult() {
    UUID uuid = UUID.randomUUID();
    Result result =
        assertYWSE(
            () ->
                doRequestWithAuthToken(
                    "DELETE",
                    "/api/customers/" + customer.uuid + "/alert_routes/" + uuid.toString(),
                    authToken));
    AssertHelper.assertBadRequest(result, "Invalid Alert Route UUID: " + uuid.toString());
  }

  @Test
  public void testDeleteAlertRoute_DefaultRoute_ErrorResult() {
    AlertRoute createdRoute = createAlertRoute(true);
    String routeUUID = createdRoute.getUuid().toString();

    Result result =
        assertYWSE(
            () ->
                doRequestWithAuthToken(
                    "DELETE",
                    "/api/customers/" + customer.uuid + "/alert_routes/" + routeUUID,
                    authToken));
    AssertHelper.assertBadRequest(
        result,
        "Unable to delete default alert route "
            + routeUUID
            + ", make another route default at first.");
  }

  @Test
  public void testListAlertRoutes_OkResult() {
    checkEmptyAnswer("/api/customers/" + customer.uuid + "/alert_routes");

    AlertRoute createdRoute1 = createAlertRoute(false);
    AlertRoute createdRoute2 = createAlertRoute(false);

    Result result =
        doRequestWithAuthToken(
            "GET", "/api/customers/" + customer.uuid + "/alert_routes", authToken);
    assertThat(result.status(), equalTo(OK));
    JsonNode listedRoutes = Json.parse(contentAsString(result));
    assertThat(listedRoutes.size(), equalTo(2));

    AlertRoute listedRoute1 = routeFromJson(listedRoutes.get(0));
    AlertRoute listedRoute2 = routeFromJson(listedRoutes.get(1));
    assertFalse(listedRoute1.equals(listedRoute2));
    assertTrue(listedRoute1.equals(createdRoute1) || listedRoute1.equals(createdRoute2));
    assertTrue(listedRoute2.equals(createdRoute1) || listedRoute2.equals(createdRoute2));
  }

  private String getAlertReceiverName() {
    return "Test AlertReceiver " + (alertReceiverIndex++);
  }

  private String getAlertRouteName() {
    return "Test AlertRoute " + (alertRouteIndex++);
  }

  @Test
  public void testListAlerts() {
    checkEmptyAnswer("/api/customers/" + customer.uuid + "/alerts");
    Alert initial = ModelFactory.createAlert(customer, alertDefinition);

    Result result =
        doRequestWithAuthToken("GET", "/api/customers/" + customer.uuid + "/alerts", authToken);
    assertThat(result.status(), equalTo(OK));
    JsonNode alertsJson = Json.parse(contentAsString(result));
    List<Alert> alerts = Arrays.asList(Json.fromJson(alertsJson, Alert[].class));

    assertThat(alerts, hasSize(1));
    assertThat(alerts.get(0), equalTo(initial));
  }

  @Test
  public void testListActiveAlerts() {
    Alert initial = ModelFactory.createAlert(customer, alertDefinition);
    Alert initial2 = ModelFactory.createAlert(customer, alertDefinition);

    alertService.markResolved(AlertFilter.builder().uuid(initial2.getUuid()).build());

    Result result =
        doRequestWithAuthToken(
            "GET", "/api/customers/" + customer.uuid + "/alerts/active", authToken);
    assertThat(result.status(), equalTo(OK));
    JsonNode alertsJson = Json.parse(contentAsString(result));
    List<Alert> alerts = Arrays.asList(Json.fromJson(alertsJson, Alert[].class));

    assertThat(alerts, hasSize(1));
    assertThat(alerts.get(0), equalTo(initial));
  }

  @Test
  public void testPageAlerts() {
    Alert initial = ModelFactory.createAlert(customer, alertDefinition);
    Alert initial2 = ModelFactory.createAlert(customer, alertDefinition);
    Alert initial3 = ModelFactory.createAlert(customer, alertDefinition);

    initial2.setCreateTime(Date.from(initial2.getCreateTime().toInstant().minusSeconds(5)));
    initial3.setCreateTime(Date.from(initial3.getCreateTime().toInstant().minusSeconds(10)));
    alertService.save(initial2);
    alertService.save(initial3);

    AlertPagedApiQuery query = new AlertPagedApiQuery();
    query.setSortBy(Alert.SortBy.CREATE_TIME);
    query.setDirection(PagedQuery.SortDirection.DESC);
    query.setFilter(new AlertApiFilter());
    query.setLimit(2);
    query.setOffset(1);
    query.setNeedTotalCount(true);

    Result result =
        doRequestWithAuthTokenAndBody(
            "POST",
            "/api/customers/" + customer.uuid + "/alerts/page",
            authToken,
            Json.toJson(query));
    assertThat(result.status(), equalTo(OK));
    JsonNode alertsJson = Json.parse(contentAsString(result));
    AlertPagedResponse alerts = Json.fromJson(alertsJson, AlertPagedResponse.class);

    assertFalse(alerts.isHasNext());
    assertTrue(alerts.isHasPrev());
    assertThat(alerts.getTotalCount(), equalTo(3));
    assertThat(alerts.getEntities(), hasSize(2));
    assertThat(alerts.getEntities(), contains(initial2, initial3));
  }

  @Test
  public void testAcknowledgeAlert() {
    Alert initial = ModelFactory.createAlert(customer, alertDefinition);
    Alert initial2 = ModelFactory.createAlert(customer, alertDefinition);

    AlertApiFilter apiFilter = new AlertApiFilter();
    apiFilter.setUuids(ImmutableSet.of(initial.getUuid()));

    Result result =
        doRequestWithAuthTokenAndBody(
            "POST",
            "/api/customers/" + customer.uuid + "/alerts/acknowledge",
            authToken,
            Json.toJson(apiFilter));
    assertThat(result.status(), equalTo(OK));

    Alert acknowledged = alertService.get(initial.getUuid());
    initial.setState(Alert.State.ACKNOWLEDGED);
    initial.setTargetState(Alert.State.ACKNOWLEDGED);
    initial.setAcknowledgedTime(acknowledged.getAcknowledgedTime());
    assertThat(acknowledged, equalTo(initial));
  }

  @Test
  public void testListTemplates() {
    AlertDefinitionTemplateApiFilter apiFilter = new AlertDefinitionTemplateApiFilter();
    apiFilter.setName(AlertDefinitionTemplate.MEMORY_CONSUMPTION.getName());

    Result result =
        doRequestWithAuthTokenAndBody(
            "POST",
            "/api/customers/" + customer.uuid + "/alert_definition_templates",
            authToken,
            Json.toJson(apiFilter));
    assertThat(result.status(), equalTo(OK));
    JsonNode templatesJson = Json.parse(contentAsString(result));
    List<AlertDefinitionGroup> templates =
        Arrays.asList(Json.fromJson(templatesJson, AlertDefinitionGroup[].class));

    assertThat(templates, hasSize(1));
    AlertDefinitionGroup template = templates.get(0);
    assertThat(template.getName(), equalTo(AlertDefinitionTemplate.MEMORY_CONSUMPTION.getName()));
    assertThat(template.getTemplate(), equalTo(AlertDefinitionTemplate.MEMORY_CONSUMPTION));
    assertThat(
        template.getDescription(),
        equalTo(AlertDefinitionTemplate.MEMORY_CONSUMPTION.getDescription()));
    assertThat(
        template.getTargetType(),
        equalTo(AlertDefinitionTemplate.MEMORY_CONSUMPTION.getTargetType()));
    assertThat(template.getTarget(), equalTo(new AlertDefinitionGroupTarget().setAll(true)));
    assertThat(
        template.getThresholdUnit(),
        equalTo(AlertDefinitionTemplate.MEMORY_CONSUMPTION.getDefaultThresholdUnit()));
    assertThat(
        template.getThresholds(),
        equalTo(
            ImmutableMap.of(
                AlertDefinitionGroup.Severity.SEVERE,
                new AlertDefinitionGroupThreshold()
                    .setCondition(AlertDefinitionGroupThreshold.Condition.GREATER_THAN)
                    .setThreshold(90))));
    assertThat(
        template.getDurationSec(),
        equalTo(AlertDefinitionTemplate.MEMORY_CONSUMPTION.getDefaultDurationSec()));
  }

  @Test
  public void testGetGroupSuccess() {
    Result result =
        doRequestWithAuthToken(
            "GET",
            "/api/customers/"
                + customer.uuid
                + "/alert_definition_groups/"
                + alertDefinitionGroup.getUuid(),
            authToken);
    assertThat(result.status(), equalTo(OK));
    JsonNode groupJson = Json.parse(contentAsString(result));
    AlertDefinitionGroup group = Json.fromJson(groupJson, AlertDefinitionGroup.class);

    assertThat(group, equalTo(alertDefinitionGroup));
  }

  @Test
  public void testGetGroupFailure() {
    UUID uuid = UUID.randomUUID();
    Result result =
        assertYWSE(
            () ->
                doRequestWithAuthToken(
                    "GET",
                    "/api/customers/" + customer.uuid + "/alert_definition_groups/" + uuid,
                    authToken));
    AssertHelper.assertBadRequest(result, "Invalid Alert Definition Group UUID: " + uuid);
  }

  @Test
  public void testPageGroups() {
    AlertDefinitionGroup group2 = ModelFactory.createAlertDefinitionGroup(customer, universe);
    AlertDefinitionGroup group3 = ModelFactory.createAlertDefinitionGroup(customer, universe);

    group2.setCreateTime(Date.from(group2.getCreateTime().toInstant().minusSeconds(5)));
    group3.setCreateTime(Date.from(group3.getCreateTime().toInstant().minusSeconds(10)));
    alertDefinitionGroupService.save(group2);
    alertDefinitionGroupService.save(group3);

    AlertDefinitionGroupPagedApiQuery query = new AlertDefinitionGroupPagedApiQuery();
    query.setSortBy(AlertDefinitionGroup.SortBy.CREATE_TIME);
    query.setDirection(PagedQuery.SortDirection.DESC);
    query.setFilter(new AlertDefinitionGroupApiFilter());
    query.setLimit(2);
    query.setOffset(1);
    query.setNeedTotalCount(true);

    Result result =
        doRequestWithAuthTokenAndBody(
            "POST",
            "/api/customers/" + customer.uuid + "/alert_definition_groups/page",
            authToken,
            Json.toJson(query));
    assertThat(result.status(), equalTo(OK));
    JsonNode groupsJson = Json.parse(contentAsString(result));
    AlertDefinitionGroupPagedResponse groups =
        Json.fromJson(groupsJson, AlertDefinitionGroupPagedResponse.class);

    assertFalse(groups.isHasNext());
    assertTrue(groups.isHasPrev());
    assertThat(groups.getTotalCount(), equalTo(3));
    assertThat(groups.getEntities(), hasSize(2));
    assertThat(groups.getEntities(), contains(group2, group3));
  }

  @Test
  public void testListGroups() {
    AlertDefinitionGroup group2 = ModelFactory.createAlertDefinitionGroup(customer, universe);
    AlertDefinitionGroup group3 = ModelFactory.createAlertDefinitionGroup(customer, universe);

    group3.setActive(false);
    alertDefinitionGroupService.save(group3);

    AlertDefinitionGroupApiFilter filter = new AlertDefinitionGroupApiFilter();
    filter.setActive(true);

    Result result =
        doRequestWithAuthTokenAndBody(
            "POST",
            "/api/customers/" + customer.uuid + "/alert_definition_groups/list",
            authToken,
            Json.toJson(filter));
    assertThat(result.status(), equalTo(OK));
    JsonNode groupsJson = Json.parse(contentAsString(result));
    List<AlertDefinitionGroup> groups =
        Arrays.asList(Json.fromJson(groupsJson, AlertDefinitionGroup[].class));

    assertThat(groups, hasSize(2));
    assertThat(groups, containsInAnyOrder(alertDefinitionGroup, group2));
  }

  @Test
  public void testCreateGroup() {
    AlertRoute route = createAlertRoute(false);
    alertDefinitionGroup.setUuid(null);
    alertDefinitionGroup.setCreateTime(null);
    alertDefinitionGroup.setRouteUUID(route.getUuid());

    Result result =
        doRequestWithAuthTokenAndBody(
            "POST",
            "/api/customers/" + customer.uuid + "/alert_definition_groups",
            authToken,
            Json.toJson(alertDefinitionGroup));
    assertThat(result.status(), equalTo(OK));
    JsonNode groupJson = Json.parse(contentAsString(result));
    AlertDefinitionGroup group = Json.fromJson(groupJson, AlertDefinitionGroup.class);

    assertThat(group.getUuid(), notNullValue());
    assertThat(group.getCreateTime(), notNullValue());
    assertThat(group.getCustomerUUID(), equalTo(customer.getUuid()));
    assertThat(group.getName(), equalTo("alertDefinitionGroup"));
    assertThat(group.getTemplate(), equalTo(AlertDefinitionTemplate.MEMORY_CONSUMPTION));
    assertThat(group.getDescription(), equalTo("alertDefinitionGroup description"));
    assertThat(group.getTargetType(), equalTo(AlertDefinitionGroup.TargetType.UNIVERSE));
    assertThat(
        group.getTarget(),
        equalTo(
            new AlertDefinitionGroupTarget()
                .setUuids(ImmutableSet.of(universe.getUniverseUUID()))));
    assertThat(group.getThresholdUnit(), equalTo(Unit.PERCENT));
    assertThat(
        group.getThresholds(),
        equalTo(
            ImmutableMap.of(
                AlertDefinitionGroup.Severity.SEVERE,
                new AlertDefinitionGroupThreshold()
                    .setCondition(AlertDefinitionGroupThreshold.Condition.GREATER_THAN)
                    .setThreshold(1))));
    assertThat(group.getDurationSec(), equalTo(15));
    assertThat(group.getRouteUUID(), equalTo(route.getUuid()));
  }

  @Test
  public void testCreateGroupFailure() {
    alertDefinitionGroup.setUuid(null);
    alertDefinitionGroup.setName(null);

    Result result =
        assertYWSE(
            () ->
                doRequestWithAuthTokenAndBody(
                    "POST",
                    "/api/customers/" + customer.uuid + "/alert_definition_groups",
                    authToken,
                    Json.toJson(alertDefinitionGroup)));
    assertBadRequest(result, "Name field is mandatory");
  }

  @Test
  public void testUpdateGroup() {
    AlertRoute route = createAlertRoute(false);
    alertDefinitionGroup.setRouteUUID(route.getUuid());

    Result result =
        doRequestWithAuthTokenAndBody(
            "PUT",
            "/api/customers/"
                + customer.uuid
                + "/alert_definition_groups/"
                + alertDefinitionGroup.getUuid(),
            authToken,
            Json.toJson(alertDefinitionGroup));
    assertThat(result.status(), equalTo(OK));
    JsonNode groupJson = Json.parse(contentAsString(result));
    AlertDefinitionGroup group = Json.fromJson(groupJson, AlertDefinitionGroup.class);

    assertThat(group.getRouteUUID(), equalTo(route.getUuid()));
  }

  @Test
  public void testUpdateGroupFailure() {
    alertDefinitionGroup.setTargetType(null);

    Result result =
        assertYWSE(
            () ->
                doRequestWithAuthTokenAndBody(
                    "PUT",
                    "/api/customers/"
                        + customer.uuid
                        + "/alert_definition_groups/"
                        + alertDefinitionGroup.getUuid(),
                    authToken,
                    Json.toJson(alertDefinitionGroup)));
    assertBadRequest(result, "Target type field is mandatory");
  }

  @Test
  public void testDeleteGroup() {
    Result result =
        doRequestWithAuthToken(
            "DELETE",
            "/api/customers/"
                + customer.uuid
                + "/alert_definition_groups/"
                + alertDefinitionGroup.getUuid(),
            authToken);
    assertThat(result.status(), equalTo(OK));
  }
}
