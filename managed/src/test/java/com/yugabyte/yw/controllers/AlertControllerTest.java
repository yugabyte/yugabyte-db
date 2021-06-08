// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.*;
import com.yugabyte.yw.common.alerts.AlertDefinitionService;
import com.yugabyte.yw.forms.AlertDefinitionFormData;
import com.yugabyte.yw.models.AlertDefinition;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
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
import play.mvc.Http;
import play.mvc.Result;

import java.text.ParseException;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.List;
import java.util.UUID;

import static com.yugabyte.yw.common.AssertHelper.*;
import static com.yugabyte.yw.common.FakeApiHelper.doRequestWithAuthToken;
import static com.yugabyte.yw.common.FakeApiHelper.doRequestWithAuthTokenAndBody;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.Matchers.*;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;
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
    Result result =
        assertThrows(
                YWServiceException.class,
                () -> controller.createDefinition(customerUUID, UUID.randomUUID()))
            .getResult();
    AssertHelper.assertBadRequest(result, "Invalid Customer UUID:" + customerUUID);

    Form<AlertDefinitionFormData> form = mock(Form.class);
    when(formFactory.getFormDataOrBadRequest(AlertDefinitionFormData.class)).thenReturn(form);
    result =
        assertThrows(
                YWServiceException.class,
                () -> controller.createDefinition(customer.uuid, UUID.randomUUID()))
            .getResult();
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
        assertThrows(
                YWServiceException.class,
                () -> controller.getAlertDefinition(customerUUID, UUID.randomUUID(), ALERT_NAME))
            .getResult();
    AssertHelper.assertBadRequest(result, "Invalid Customer UUID:" + customerUUID);
    result =
        assertThrows(
                YWServiceException.class,
                () ->
                    controller.getAlertDefinition(customer.uuid, universe.universeUUID, ALERT_NAME))
            .getResult();
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
        assertThrows(
                YWServiceException.class,
                () -> controller.updateAlertDefinition(customerUUID, definitionUUID))
            .getResult();
    AssertHelper.assertBadRequest(result, "Invalid Customer UUID:" + customerUUID);

    result =
        assertThrows(
                YWServiceException.class,
                () -> controller.updateAlertDefinition(customer.uuid, definitionUUID))
            .getResult();
    AssertHelper.assertBadRequest(result, "Invalid Alert Definition UUID: " + definitionUUID);
  }
}
