// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.*;
import com.yugabyte.yw.common.alerts.*;
import com.yugabyte.yw.forms.AlertDefinitionFormData;
import com.yugabyte.yw.models.*;
import com.yugabyte.yw.models.AlertReceiver.TargetType;
import com.yugabyte.yw.models.filters.AlertDefinitionFilter;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.Spy;
import org.mockito.junit.MockitoJUnitRunner;
import play.data.Form;
import play.libs.Json;
import play.mvc.Result;

import java.io.IOException;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.UUID;
import java.util.stream.Collectors;

import static com.yugabyte.yw.common.AssertHelper.*;
import static com.yugabyte.yw.common.FakeApiHelper.doRequestWithAuthToken;
import static com.yugabyte.yw.common.FakeApiHelper.doRequestWithAuthTokenAndBody;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.*;
import static org.junit.Assert.*;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;

@RunWith(MockitoJUnitRunner.class)
public class AlertControllerTest extends FakeDBApplication {

  private static final String ALERT_DEFINITION_NAME = "alertDefinition";

  private static final String ALERT_RECEIVER_NAME = "Test AlertReceiver";

  private static final String ALERT_ROUTE_NAME = "Test AlertRoute";

  private Customer customer;

  private Users user;

  private String authToken;

  private Universe universe;

  @Mock private ValidatingFormFactory formFactory;

  @Spy private AlertDefinitionService alertDefinitionService;

  @InjectMocks private AlertController controller;

  private SmtpData defaultSmtp = EmailFixtures.createSmtpData();

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    user = ModelFactory.testUser(customer);
    authToken = user.createAuthToken();

    universe = ModelFactory.createUniverse();
  }

  @Test
  public void testCreateDefinition_OkResult() {
    AlertDefinitionFormData data = new AlertDefinitionFormData();
    data.alertDefinitionUUID = UUID.randomUUID();
    data.template = AlertDefinitionTemplate.CLOCK_SKEW;
    data.name = ALERT_DEFINITION_NAME;
    data.value = 1;
    data.active = true;

    Form<AlertDefinitionFormData> form = mock(Form.class);
    when(formFactory.getFormDataOrBadRequest(AlertDefinitionFormData.class)).thenReturn(form);
    when(form.get()).thenReturn(data);

    Result result = controller.createDefinition(customer.uuid, universe.universeUUID);
    assertOk(result);

    List<AlertDefinition> definitions =
        alertDefinitionService.list(
            AlertDefinitionFilter.builder()
                .customerUuid(customer.uuid)
                .name(data.name)
                .label(KnownAlertLabels.UNIVERSE_UUID, universe.universeUUID.toString())
                .build());
    assertThat(definitions, hasSize(1));

    AlertDefinition definition = definitions.get(0);
    assertThat(definition.getName(), equalTo(ALERT_DEFINITION_NAME));
    assertThat(definition.isActive(), equalTo(true));
    assertThat(definition.getQueryThreshold(), equalTo(1.0));
  }

  @Test
  public void testCreateDefinition_ErrorResult() {
    UUID customerUUID = UUID.randomUUID();
    Result result = assertYWSE(() -> controller.createDefinition(customerUUID, UUID.randomUUID()));
    AssertHelper.assertBadRequest(result, "Invalid Customer UUID:" + customerUUID);

    Form<AlertDefinitionFormData> form = mock(Form.class);
    when(formFactory.getFormDataOrBadRequest(AlertDefinitionFormData.class)).thenReturn(form);
    result = assertYWSE(() -> controller.createDefinition(customer.uuid, UUID.randomUUID()));
    assertEquals(BAD_REQUEST, result.status());
  }

  @Test
  public void testGetAlertDefinition_OkResult() {
    AlertDefinition definition = ModelFactory.createAlertDefinition(customer, universe);
    Result result =
        controller.getAlertDefinition(customer.uuid, universe.universeUUID, ALERT_DEFINITION_NAME);
    assertOk(result);

    JsonNode definitionJson = Json.parse(contentAsString(result));
    assertThat(definitionJson, notNullValue());
    assertValue(definitionJson, "uuid", definition.getUuid().toString());
    assertValue(definitionJson, "name", ALERT_DEFINITION_NAME);
    assertValue(definitionJson, "query", "query < {{ query_threshold }}");
  }

  @Test
  public void testGetAlertDefinition_ErrorResult() {
    UUID customerUUID = UUID.randomUUID();
    Result result =
        assertYWSE(
            () ->
                controller.getAlertDefinition(
                    customerUUID, UUID.randomUUID(), ALERT_DEFINITION_NAME));
    AssertHelper.assertBadRequest(result, "Invalid Customer UUID:" + customerUUID);
    result =
        assertYWSE(
            () ->
                controller.getAlertDefinition(
                    customer.uuid, universe.universeUUID, ALERT_DEFINITION_NAME));
    AssertHelper.assertBadRequest(
        result,
        ALERT_DEFINITION_NAME
            + " alert definition for customer "
            + customer.uuid
            + " and universe "
            + universe.universeUUID
            + " not found");
  }

  @Test
  public void testUpdateAlertDefinition_OkResult() {
    AlertDefinition definition = ModelFactory.createAlertDefinition(customer, universe);

    // For FormData we are setting only used fields. This could be changed later.
    AlertDefinitionFormData data = new AlertDefinitionFormData();
    data.template = AlertDefinitionTemplate.CLOCK_SKEW;
    data.value = 2;
    data.active = false;

    Form<AlertDefinitionFormData> form = mock(Form.class);
    when(formFactory.getFormDataOrBadRequest(AlertDefinitionFormData.class)).thenReturn(form);
    when(form.get()).thenReturn(data);

    Result result = controller.updateAlertDefinition(customer.uuid, definition.getUuid());
    assertOk(result);

    definition = alertDefinitionService.get(definition.getUuid());
    assertThat(definition, notNullValue());

    assertThat(definition.isActive(), equalTo(false));
    assertThat(definition.getQueryThreshold(), equalTo(2.0));
  }

  @Test
  public void testUpdateAlertDefinition_ErrorResult() {
    UUID definitionUUID = UUID.randomUUID();
    UUID customerUUID = UUID.randomUUID();
    Result result =
        assertYWSE(() -> controller.updateAlertDefinition(customerUUID, definitionUUID));
    AssertHelper.assertBadRequest(result, "Invalid Customer UUID:" + customerUUID);

    result = assertYWSE(() -> controller.updateAlertDefinition(customer.uuid, definitionUUID));
    AssertHelper.assertBadRequest(result, "Invalid Alert Definition UUID: " + definitionUUID);
  }

  private void checkEmptyAnswer(String url) {
    Result result = doRequestWithAuthToken("GET", url, authToken);
    assertEquals(OK, result.status());
    assertEquals("[]", contentAsString(result));
  }

  private AlertReceiverParams getAlertReceiverParamsForTests() {
    AlertReceiverEmailParams arParams = new AlertReceiverEmailParams();
    arParams.recipients = Collections.singletonList("test@test.com");
    arParams.smtpData = defaultSmtp;
    return arParams;
  }

  private ObjectNode getAlertReceiverJson() {
    ObjectNode data = Json.newObject();
    data.put("name", ALERT_RECEIVER_NAME);
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
    assertEquals(OK, result.status());
    return receiverFromJson(Json.parse(contentAsString(result)));
  }

  @Test
  public void testCreateAndListAlertReceiver_OkResult() {
    checkEmptyAnswer("/api/customers/" + customer.uuid + "/alert_receivers");

    AlertReceiver createdReceiver = createAlertReceiver();
    assertNotNull(createdReceiver.getUuid());

    assertEquals(TargetType.Email.name(), AlertUtils.getJsonTypeName(createdReceiver.getParams()));
    assertEquals(getAlertReceiverParamsForTests(), createdReceiver.getParams());

    Result result =
        doRequestWithAuthToken(
            "GET", "/api/customers/" + customer.uuid + "/alert_receivers", authToken);
    assertEquals(OK, result.status());
    JsonNode listedReceivers = Json.parse(contentAsString(result));
    assertEquals(1, listedReceivers.size());
    assertEquals(createdReceiver, receiverFromJson(listedReceivers.get(0)));
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
        result, "Unable to create alert receiver: Email parameters: destinations are empty.");
    checkEmptyAnswer("/api/customers/" + customer.uuid + "/alert_receivers");
  }

  @Test
  public void testGetAlertReceiver_OkResult() {
    AlertReceiver createdReceiver = createAlertReceiver();
    assertNotNull(createdReceiver.getUuid());

    Result result =
        doRequestWithAuthToken(
            "GET",
            "/api/customers/" + customer.uuid + "/alert_receivers/" + createdReceiver.getUuid(),
            authToken);
    assertEquals(OK, result.status());

    AlertReceiver receiver = receiverFromJson(Json.parse(contentAsString(result)));
    assertNotNull(receiver);
    assertEquals(createdReceiver, receiver);
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
    assertNotNull(createdReceiver.getUuid());

    AlertReceiverEmailParams params = (AlertReceiverEmailParams) createdReceiver.getParams();
    params.recipients = Collections.singletonList("new@test.com");
    params.smtpData.smtpPort = 1111;
    createdReceiver.setParams(params);

    ObjectNode data = Json.newObject();
    data.put("alertReceiverUUID", createdReceiver.getUuid().toString())
        .put("name", ALERT_RECEIVER_NAME)
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
    assertEquals(OK, result.status());

    AlertReceiver updatedReceiver = receiverFromJson(Json.parse(contentAsString(result)));
    assertNotNull(updatedReceiver);
    assertEquals(createdReceiver, updatedReceiver);
  }

  @Test
  public void testUpdateAlertReceiver_ErrorResult() {
    AlertReceiver createdReceiver = createAlertReceiver();
    assertNotNull(createdReceiver.getUuid());

    createdReceiver.setParams(new AlertReceiverSlackParams());

    ObjectNode data = Json.newObject();
    data.put("alertReceiverUUID", createdReceiver.getUuid().toString())
        .put("name", ALERT_RECEIVER_NAME)
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
    assertNotNull(createdReceiver.getUuid());

    Result result =
        doRequestWithAuthToken(
            "DELETE",
            "/api/customers/"
                + customer.uuid
                + "/alert_receivers/"
                + createdReceiver.getUuid().toString(),
            authToken);
    assertEquals(OK, result.status());

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

  private ObjectNode getAlertRouteJson() {
    AlertReceiver receiver1 =
        AlertReceiver.create(
            customer.uuid, ALERT_RECEIVER_NAME, AlertUtils.createParamsInstance(TargetType.Email));
    AlertReceiver receiver2 =
        AlertReceiver.create(
            customer.uuid, ALERT_RECEIVER_NAME, AlertUtils.createParamsInstance(TargetType.Slack));

    ObjectNode data = Json.newObject();
    data.put("name", ALERT_ROUTE_NAME)
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
      route.setUUID(UUID.fromString(json.get("uuid").asText()));
      route.setName(json.get("name").asText());
      route.setReceiversList(receivers);
      return route;
    } catch (IOException e) {
      return null;
    }
  }

  private AlertRoute createAlertRoute() {
    ObjectNode routeFormDataJson = getAlertRouteJson();
    Result result =
        doRequestWithAuthTokenAndBody(
            "POST",
            "/api/customers/" + customer.uuid + "/alert_routes",
            authToken,
            routeFormDataJson);
    assertEquals(OK, result.status());
    return routeFromJson(Json.parse(contentAsString(result)));
  }

  @Test
  public void testCreateAlertRoute_OkResult() {
    checkEmptyAnswer("/api/customers/" + customer.uuid + "/alert_routes");

    AlertRoute createdRoute = createAlertRoute();
    assertNotNull(createdRoute.getUuid());

    Result result =
        doRequestWithAuthToken(
            "GET", "/api/customers/" + customer.uuid + "/alert_routes", authToken);
    assertEquals(OK, result.status());
    JsonNode listedRoutes = Json.parse(contentAsString(result));
    assertEquals(1, listedRoutes.size());
    assertEquals(createdRoute, routeFromJson(listedRoutes.get(0)));
  }

  @Test
  public void testCreateAlertRoute_ErrorResult() {
    checkEmptyAnswer("/api/customers/" + customer.uuid + "/alert_routes");
    ObjectNode data = Json.newObject();
    String alertReceiverUUID = UUID.randomUUID().toString();
    data.put("name", ALERT_ROUTE_NAME).putArray("receivers").add(alertReceiverUUID);
    Result result =
        assertYWSE(
            () ->
                doRequestWithAuthTokenAndBody(
                    "POST", "/api/customers/" + customer.uuid + "/alert_routes", authToken, data));

    AssertHelper.assertBadRequest(result, "Invalid Alert Receiver UUID: " + alertReceiverUUID);
    checkEmptyAnswer("/api/customers/" + customer.uuid + "/alert_routes");
  }

  @Test
  public void testGetAlertRoute_OkResult() {
    AlertRoute createdRoute = createAlertRoute();
    assertNotNull(createdRoute.getUuid());

    Result result =
        doRequestWithAuthToken(
            "GET",
            "/api/customers/" + customer.uuid + "/alert_routes/" + createdRoute.getUuid(),
            authToken);
    assertEquals(OK, result.status());

    AlertRoute route = routeFromJson(Json.parse(contentAsString(result)));
    assertNotNull(route);
    assertEquals(createdRoute, route);
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
  public void testDeleteAlertRoute_OkResult() {
    checkEmptyAnswer("/api/customers/" + customer.uuid + "/alert_routes");

    AlertRoute createdRoute = createAlertRoute();
    assertNotNull(createdRoute.getUuid());

    Result result =
        doRequestWithAuthToken(
            "DELETE",
            "/api/customers/"
                + customer.uuid
                + "/alert_routes/"
                + createdRoute.getUuid().toString(),
            authToken);
    assertEquals(OK, result.status());

    checkEmptyAnswer("/api/customers/" + customer.uuid + "/alert_routes");
  }

  @Test
  public void testDeleteAlertRoute_ErrorResult() {
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
  public void testListAlertRoutes_OkResult() {
    checkEmptyAnswer("/api/customers/" + customer.uuid + "/alert_routes");

    AlertRoute createdRoute1 = createAlertRoute();
    AlertRoute createdRoute2 = createAlertRoute();

    Result result =
        doRequestWithAuthToken(
            "GET", "/api/customers/" + customer.uuid + "/alert_routes", authToken);
    assertEquals(OK, result.status());
    JsonNode listedRoutes = Json.parse(contentAsString(result));
    assertEquals(2, listedRoutes.size());

    AlertRoute listedRoute1 = routeFromJson(listedRoutes.get(0));
    AlertRoute listedRoute2 = routeFromJson(listedRoutes.get(1));
    assertFalse(listedRoute1.equals(listedRoute2));
    assertTrue(listedRoute1.equals(createdRoute1) || listedRoute1.equals(createdRoute2));
    assertTrue(listedRoute2.equals(createdRoute1) || listedRoute2.equals(createdRoute2));
  }
}
