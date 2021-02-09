// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.AlertDefinitionTemplate;
import com.yugabyte.yw.common.AssertHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.config.impl.RuntimeConfig;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.forms.AlertDefinitionFormData;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.AlertDefinition;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Users;

import com.yugabyte.yw.models.Universe;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;

import play.data.Form;
import play.data.FormFactory;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;
import java.text.ParseException;
import java.text.SimpleDateFormat;
import java.util.Date;
import java.util.UUID;

import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static com.yugabyte.yw.common.FakeApiHelper.doRequestWithAuthTokenAndBody;
import static com.yugabyte.yw.common.FakeApiHelper.doRequestWithAuthToken;
import static org.mockito.Mockito.*;
import static play.test.Helpers.*;
import static org.junit.Assert.*;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.OK;

@RunWith(MockitoJUnitRunner.class)
public class AlertControllerTest extends FakeDBApplication {

  private static final String ALERT_NAME = "alert";

  private Customer customer;

  private Users user;

  private String authToken;

  private Universe universe;

  @Mock
  private SettableRuntimeConfigFactory configFactory;

  @Mock
  private FormFactory formFactory;

  @InjectMocks
  private AlertController controller;

  @Mock
  private RuntimeConfig<Universe> config;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    user = ModelFactory.testUser(customer);
    authToken = user.createAuthToken();

    universe = ModelFactory.createUniverse();
    when(configFactory.forUniverse(universe)).thenReturn(config);
  }

  @Test
  public void testCreateAlert() {
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    Result result = doRequestWithAuthToken("GET",
        "/api/customers/" + customer.uuid + "/alerts", authToken);
    assertEquals(OK, result.status());
    assertEquals("[]", contentAsString(result));

    ObjectNode params = Json.newObject();
    params.put("errCode", "VALID_ALERT");
    params.put("type", "WARNING");
    params.put("message", "Testing add valid alert.");
    result = doRequestWithAuthTokenAndBody("POST",
        "/api/customers/" + customer.uuid + "/alerts", authToken, params);
    assertEquals(OK, result.status());

    result = doRequestWithAuthToken("GET",
        "/api/customers/" + customer.uuid + "/alerts", authToken);
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
    Result result = doRequestWithAuthToken("GET",
        "/api/customers/" + customer.uuid + "/alerts", authToken);
    assertEquals(OK, result.status());
    assertEquals("[]", contentAsString(result));

    ObjectNode params = Json.newObject();
    params.put("errCode", "VALID_ALERT")
          .put("type", "WARNING")
          .put("message", "First alert.");
    result = doRequestWithAuthTokenAndBody("PUT",
        "/api/customers/" + customer.uuid + "/alerts", authToken, params);
    assertEquals(OK, result.status());

    result = doRequestWithAuthToken("GET",
        "/api/customers/" + customer.uuid + "/alerts", authToken);
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
    result = doRequestWithAuthTokenAndBody("PUT",
        "/api/customers/" + customer.uuid + "/alerts", authToken, params);
    assertEquals(OK, result.status());

    result = doRequestWithAuthToken("GET",
    "/api/customers/" + customer.uuid + "/alerts", authToken);
    assertEquals(OK, result.status());
    json = Json.parse(contentAsString(result));
    assertEquals(1, json.size());
    alert = json.get(0);
    assertEquals(params.get("errCode"), alert.get("errCode"));
    assertEquals(params.get("type"), alert.get("type"));
    assertEquals(params.get("message"), alert.get("message"));

    Date secondDate = formatter.parse(alert.get("createTime").asText());
    String errMsg = String.format("Expected second alert's createTime to be later than first."
      + "First: %s. Second: %s.", firstDate, secondDate);
    assertTrue(errMsg, secondDate.after(firstDate));
    assertAuditEntry(2, customer.uuid);
  }

  @Test
  public void testCreateDefinition_OkResult() {
    AlertDefinitionFormData data = new AlertDefinitionFormData();
    data.alertDefinitionUUID = UUID.randomUUID();
    data.template = AlertDefinitionTemplate.CLOCK_SKEW;
    data.name = ALERT_NAME;
    data.value = 1;
    data.isActive = true;

    Form<AlertDefinitionFormData> form = mock(Form.class);
    when(formFactory.form(AlertDefinitionFormData.class)).thenReturn(form);
    when(form.bindFromRequest()).thenReturn(form);
    when(form.get()).thenReturn(data);

    Result result = controller.createDefinition(customer.uuid, universe.universeUUID);
    assertOk(result);

    assertNotNull(AlertDefinition.get(customer.uuid, universe.universeUUID, data.name));
    verify(config, times(1)).setValue(AlertDefinitionTemplate.CLOCK_SKEW.getParameterName(), "1");
  }

  @Test
  public void testCreateDefinition_ErrorResult() {
    UUID customerUUID = UUID.randomUUID();
    AssertHelper.assertBadRequest(controller.createDefinition(customerUUID, UUID.randomUUID()),
        "Invalid Customer UUID: " + customerUUID);

    Form<AlertDefinitionFormData> form = mock(Form.class);
    when(formFactory.form(AlertDefinitionFormData.class)).thenReturn(form);
    when(form.bindFromRequest()).thenReturn(form);
    when(form.hasErrors()).thenReturn(true);
    Result result = controller.createDefinition(customer.uuid, UUID.randomUUID());
    assertEquals(BAD_REQUEST, result.status());
  }

  @Test
  public void testGetAlertDefinition_OkResult() {
    when(config.getString("config.parameter")).thenReturn("test");

    AlertDefinition definition = AlertDefinition.create(customer.uuid, universe.universeUUID,
        ALERT_NAME, "query {{ config.parameter }}", true);
    Result result = controller.getAlertDefinition(customer.uuid, universe.universeUUID, ALERT_NAME);
    assertOk(result);

    JsonNode definitionJson = Json.parse(contentAsString(result));
    assertNotNull(definitionJson);
    assertValue(definitionJson, "uuid", definition.uuid.toString());
    assertValue(definitionJson, "name", ALERT_NAME);
    assertValue(definitionJson, "query", "query test");
  }

  @Test
  public void testGetAlertDefinition_ErrorResult() {
    UUID customerUUID = UUID.randomUUID();
    AssertHelper.assertBadRequest(
        controller.getAlertDefinition(customerUUID, UUID.randomUUID(), ALERT_NAME),
        "Invalid Customer UUID: " + customerUUID);

    AssertHelper.assertBadRequest(
        controller.getAlertDefinition(customer.uuid, universe.universeUUID, ALERT_NAME),
        "Could not find Alert Definition");
  }

  @Test
  public void testUpdateAlertDefinition_OkResult() {
    AlertDefinition definition = AlertDefinition.create(customer.uuid, universe.universeUUID,
        ALERT_NAME, "query {{ config.parameter }}", true);

    // For FormData we are setting only used fields. This could be changed later.
    AlertDefinitionFormData data = new AlertDefinitionFormData();
    data.template = AlertDefinitionTemplate.CLOCK_SKEW;
    data.value = 2;
    data.isActive = false;

    Form<AlertDefinitionFormData> form = mock(Form.class);
    when(formFactory.form(AlertDefinitionFormData.class)).thenReturn(form);
    when(form.bindFromRequest()).thenReturn(form);
    when(form.get()).thenReturn(data);

    Result result = controller.updateAlertDefinition(customer.uuid, definition.uuid);
    assertOk(result);

    definition = AlertDefinition.get(definition.uuid);
    assertNotNull(definition);
    verify(config, times(1)).setValue(AlertDefinitionTemplate.CLOCK_SKEW.getParameterName(), "2");
  }

  @Test
  public void testUpdateAlertDefinition_ErrorResult() {
    UUID definitionUUID = UUID.randomUUID();
    UUID customerUUID = UUID.randomUUID();
    AssertHelper.assertBadRequest(controller.updateAlertDefinition(customerUUID, definitionUUID),
        "Invalid Customer UUID: " + customerUUID);

    AssertHelper.assertBadRequest(controller.updateAlertDefinition(customer.uuid, definitionUUID),
        "Invalid Alert Definition UUID: " + definitionUUID);
  }
}
