// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.AlertDefinitionTemplate;
import com.yugabyte.yw.common.AssertHelper;
import com.yugabyte.yw.common.EmailFixtures;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.ValidatingFormFactory;
import com.yugabyte.yw.common.YWServiceException;
import com.yugabyte.yw.common.alerts.AlertDefinitionService;
import com.yugabyte.yw.common.alerts.AlertReceiverEmailParams;
import com.yugabyte.yw.common.alerts.AlertReceiverParams;
import com.yugabyte.yw.common.alerts.AlertReceiverSlackParams;
import com.yugabyte.yw.common.alerts.AlertUtils;
import com.yugabyte.yw.common.alerts.SmtpData;
import com.yugabyte.yw.forms.AlertDefinitionFormData;
import com.yugabyte.yw.models.AlertDefinition;
import com.yugabyte.yw.models.AlertReceiver;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.filters.AlertDefinitionFilter;
import com.yugabyte.yw.models.helpers.KnownAlertLabels;
import com.yugabyte.yw.models.AlertReceiver.TargetType;
import com.yugabyte.yw.models.AlertRoute;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.Spy;
import org.mockito.junit.MockitoJUnitRunner;
import play.data.Form;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;

import java.text.ParseException;
import java.text.SimpleDateFormat;
import java.util.Collections;
import java.util.Date;
import java.util.List;
import java.util.UUID;

import static com.yugabyte.yw.common.AssertHelper.*;
import static com.yugabyte.yw.common.FakeApiHelper.doRequestWithAuthToken;
import static com.yugabyte.yw.common.FakeApiHelper.doRequestWithAuthTokenAndBody;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.*;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;

@RunWith(MockitoJUnitRunner.class)
public class AlertControllerTest extends FakeDBApplication {

  private static final String ALERT_NAME = "alertDefinition";

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
  public void testCreateAlert() {
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    Result result =
        doRequestWithAuthToken("GET", "/api/customers/" + customer.uuid + "/alerts", authToken);
    assertEquals(OK, result.status());
    assertEquals("[]", contentAsString(result));

    ObjectNode params = Json.newObject();
    params.put("errCode", "VALID_ALERT");
    params.put("type", "WARNING");
    params.put("message", "Testing add valid alert.");
    result =
        doRequestWithAuthTokenAndBody(
            "POST", "/api/customers/" + customer.uuid + "/alerts", authToken, params);
    assertEquals(OK, result.status());

    result =
        doRequestWithAuthToken("GET", "/api/customers/" + customer.uuid + "/alerts", authToken);
    assertEquals(OK, result.status());
    assertAuditEntry(1, customer.uuid);
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(1, json.size());
    JsonNode alert = json.get(0);
    assertEquals(params.get("errCode"), alert.get("errCode"));
    assertEquals(params.get("type"), alert.get("type"));
    assertEquals(params.get("message"), alert.get("message"));
  }

  @Test
  public void testUpsertValid() throws ParseException, InterruptedException {
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    Result result =
        doRequestWithAuthToken("GET", "/api/customers/" + customer.uuid + "/alerts", authToken);
    assertEquals(OK, result.status());
    assertEquals("[]", contentAsString(result));

    ObjectNode params = Json.newObject();
    params.put("errCode", "VALID_ALERT").put("type", "WARNING").put("message", "First alert.");
    result =
        doRequestWithAuthTokenAndBody(
            "PUT", "/api/customers/" + customer.uuid + "/alerts", authToken, params);
    assertEquals(OK, result.status());

    result =
        doRequestWithAuthToken("GET", "/api/customers/" + customer.uuid + "/alerts", authToken);
    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(1, json.size());
    JsonNode alert = json.get(0);
    assertEquals(params.get("errCode"), alert.get("errCode"));
    assertEquals(params.get("type"), alert.get("type"));
    assertEquals(params.get("message"), alert.get("message"));

    SimpleDateFormat formatter = new SimpleDateFormat("EEE MMM dd hh:mm:ss z yyyy");
    Date firstDate = formatter.parse(alert.get("createTime").asText());

    // Sleep so that API registers the request as a different Date.
    Thread.sleep(1000);
    params.put("message", "Second alert.");
    result =
        doRequestWithAuthTokenAndBody(
            "PUT", "/api/customers/" + customer.uuid + "/alerts", authToken, params);
    assertEquals(OK, result.status());

    result =
        doRequestWithAuthToken("GET", "/api/customers/" + customer.uuid + "/alerts", authToken);
    assertEquals(OK, result.status());
    json = Json.parse(contentAsString(result));
    assertEquals(1, json.size());
    alert = json.get(0);
    assertEquals(params.get("errCode"), alert.get("errCode"));
    assertEquals(params.get("type"), alert.get("type"));
    assertEquals(params.get("message"), alert.get("message"));

    Date secondDate = formatter.parse(alert.get("createTime").asText());
    String errMsg =
        String.format(
            "Expected second alert's createTime to be later than first." + "First: %s. Second: %s.",
            firstDate, secondDate);
    assertThat(errMsg, secondDate.after(firstDate));
    assertAuditEntry(2, customer.uuid);
  }

  @Test
  public void testCreateDefinition_OkResult() {
    AlertDefinitionFormData data = new AlertDefinitionFormData();
    data.alertDefinitionUUID = UUID.randomUUID();
    data.template = AlertDefinitionTemplate.CLOCK_SKEW;
    data.name = ALERT_NAME;
    data.value = 1;
    data.active = true;

    Form<AlertDefinitionFormData> form = mock(Form.class);
    when(formFactory.getFormDataOrBadRequest(AlertDefinitionFormData.class)).thenReturn(form);
    when(form.get()).thenReturn(data);

    Result result = controller.createDefinition(customer.uuid, universe.universeUUID);
    assertOk(result);

    List<AlertDefinition> definitions =
        alertDefinitionService.list(
            new AlertDefinitionFilter()
                .setCustomerUuid(customer.uuid)
                .setName(data.name)
                .setLabel(KnownAlertLabels.UNIVERSE_UUID, universe.universeUUID.toString()));
    assertThat(definitions, hasSize(1));

    AlertDefinition definition = definitions.get(0);
    assertThat(definition.getName(), equalTo(ALERT_NAME));
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
    Result result = controller.getAlertDefinition(customer.uuid, universe.universeUUID, ALERT_NAME);
    assertOk(result);

    JsonNode definitionJson = Json.parse(contentAsString(result));
    assertThat(definitionJson, notNullValue());
    assertValue(definitionJson, "uuid", definition.getUuid().toString());
    assertValue(definitionJson, "name", ALERT_NAME);
    assertValue(definitionJson, "query", "query < {{ query_threshold }}");
  }

  @Test
  public void testGetAlertDefinition_ErrorResult() {
    UUID customerUUID = UUID.randomUUID();
    Result result =
        assertYWSE(
            () -> controller.getAlertDefinition(customerUUID, UUID.randomUUID(), ALERT_NAME));
    AssertHelper.assertBadRequest(result, "Invalid Customer UUID:" + customerUUID);
    result =
        assertYWSE(
            () -> controller.getAlertDefinition(customer.uuid, universe.universeUUID, ALERT_NAME));
    AssertHelper.assertBadRequest(
        result,
        ALERT_NAME
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
    AlertDefinition definition = ModelFactory.createAlertDefinition(customer, universe);
    AlertReceiver receiver =
        AlertReceiver.create(customer.uuid, AlertUtils.createParamsInstance(TargetType.Email));

    ObjectNode data = Json.newObject();
    data.put("definitionUUID", definition.getUuid().toString())
        .put("receiverUUID", receiver.getUuid().toString());
    return data;
  }

  private AlertRoute routeFromJson(JsonNode json) {
    try {
      return new ObjectMapper().treeToValue(json, AlertRoute.class);
    } catch (JsonProcessingException e) {
      fail("Bad json format.");
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
    data.put("definitionUUID", UUID.randomUUID().toString())
        .put("receiverUUID", UUID.randomUUID().toString());
    Result result =
        assertYWSE(
            () ->
                doRequestWithAuthTokenAndBody(
                    "POST", "/api/customers/" + customer.uuid + "/alert_routes", authToken, data));

    AssertHelper.assertBadRequest(result, "Unable to create alert route.");
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
