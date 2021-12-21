// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static com.yugabyte.yw.common.AssertHelper.assertBadRequest;
import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static com.yugabyte.yw.models.helpers.CommonUtils.datePlus;
import static org.hamcrest.CoreMatchers.allOf;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.hamcrest.CoreMatchers.is;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.hamcrest.CoreMatchers.nullValue;
import static org.hamcrest.MatcherAssert.assertThat;
import static org.hamcrest.core.StringContains.containsString;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static org.mockito.Matchers.anyList;
import static org.mockito.Matchers.anyMap;
import static org.mockito.Mockito.any;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static play.mvc.Http.Status.FORBIDDEN;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.BAD_REQUEST;
import static play.test.Helpers.contentAsString;
import static play.test.Helpers.fakeRequest;
import static play.test.Helpers.route;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.CallHomeManager.CollectionLevel;
import com.yugabyte.yw.common.FakeApiHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.AlertingFormData.AlertingData;
import com.yugabyte.yw.forms.PlatformResults.YBPError;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.Alert.State;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerConfig;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.Users.Role;
import java.time.temporal.ChronoUnit;
import java.util.ArrayList;
import java.util.Date;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.TimeoutException;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;

@RunWith(MockitoJUnitRunner.class)
public class CustomerControllerTest extends FakeDBApplication {
  String rootRoute = "/api/customers";
  String baseRoute = rootRoute + "/";

  private Customer customer;
  private Users user;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    user = ModelFactory.testUser(customer);
  }

  @Test
  public void testListCustomersWithoutAuth() {
    Result result = route(fakeRequest("GET", rootRoute));
    assertEquals(FORBIDDEN, result.status());
  }

  @Test
  public void testListCustomersUuidsWithAuth() {
    String authToken = user.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    Result result = route(fakeRequest("GET", rootRoute + "_uuids").cookie(validCookie));
    assertEquals(OK, result.status());
    ArrayNode json = (ArrayNode) Json.parse(contentAsString(result));
    assertEquals(json.get(0).textValue(), customer.uuid.toString());
  }

  @Test
  public void testListWithDataCustomersWithAuth() {
    String authToken = user.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    Result result = route(fakeRequest("GET", rootRoute).cookie(validCookie));
    assertEquals(OK, result.status());
    ArrayNode json = (ArrayNode) Json.parse(contentAsString(result));
    assertEquals(json.get(0).get("uuid").textValue(), customer.uuid.toString());
  }

  // check that invalid creds is failing to do that

  @Test
  public void testCustomerGETWithValidUUID() {
    String authToken = user.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    Result result = route(fakeRequest("GET", baseRoute + customer.uuid).cookie(validCookie));
    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));

    assertThat(json.get("uuid").asText(), allOf(notNullValue(), equalTo(customer.uuid.toString())));
    assertEquals(json.get("name").asText(), customer.name);
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testCustomerGETWithInvalidUUID() {
    String authToken = user.createAuthToken();
    UUID invalidUUID = UUID.randomUUID();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    Result result = route(fakeRequest("GET", baseRoute + invalidUUID).cookie(validCookie));
    assertEquals(FORBIDDEN, result.status());

    String resultString = contentAsString(result);
    assertThat(resultString, allOf(notNullValue(), equalTo("Unable To Authenticate User")));
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testCustomerGETWithBadUUID() {
    String authToken = user.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    Result result = route(fakeRequest("GET", baseRoute + "null").cookie(validCookie));
    assertEquals(BAD_REQUEST, result.status());

    JsonNode ybpError = Json.parse(contentAsString(result));
    assertEquals(
        Json.toJson(
            new YBPError("Cannot parse parameter cUUID as UUID: Invalid UUID string: null")),
        ybpError);
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testCustomerGETWithReadonlyUser() {
    String authToken = user.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    ObjectNode params = Json.newObject();
    params.put("code", "tc");
    params.put("email", "foo@bar.com");
    params.put("name", "Test Customer");
    JsonNode features = Json.parse("{\"foo\": \"bar\"}");
    params.set("features", features);

    Result result =
        route(fakeRequest("PUT", baseRoute + customer.uuid).cookie(validCookie).bodyJson(params));
    assertEquals(OK, result.status());

    user.setRole(Role.ReadOnly);
    user.save();

    result = route(fakeRequest("GET", baseRoute + customer.uuid).cookie(validCookie));
    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));

    JsonNode loadedFeatures = json.get("features");

    assertThat(loadedFeatures.get("foo").asText(), equalTo("bar"));
    assertThat(loadedFeatures.get("main").get("stats").asText(), equalTo("hidden"));
  }

  @Test
  public void testCustomerPUTWithValidParams() {
    String authToken = user.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    ObjectNode params = Json.newObject();
    params.put("code", "tc");
    params.put("email", "foo@bar.com");
    params.put("name", "Test Customer");
    params.put("password", "new_Passw0rd");
    params.put("confirmPassword", "new_Passw0rd");
    params.put("callhomeLevel", "LOW");
    Result result =
        route(fakeRequest("PUT", baseRoute + customer.uuid).cookie(validCookie).bodyJson(params));
    assertEquals(OK, result.status());
    CustomerConfig callhomeConfig = CustomerConfig.getCallhomeConfig(customer.uuid);
    CollectionLevel callhomeLevel = CustomerConfig.getCallhomeLevel(customer.uuid);
    assertEquals(CollectionLevel.LOW, callhomeLevel);
    JsonNode json = Json.parse(contentAsString(result));
    assertThat(json.get("uuid").asText(), is(equalTo(customer.uuid.toString())));
    assertThat(json.get("name").asText(), is(equalTo("Test Customer")));
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testCustomerPUTWithAlertingData() {
    String authToken = user.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    ObjectNode params = Json.newObject();
    params.put("code", "tc");
    params.put("email", "admin");
    params.put("name", "Test Customer");
    ObjectNode alertingData = Json.newObject();
    String alertEmail = "alerts@yugabyte.com";
    alertingData.put("alertingEmail", alertEmail);
    alertingData.put("sendAlertsToYb", true);
    alertingData.put("reportOnlyErrors", false);
    params.put("alertingData", alertingData);
    params.put("callhomeLevel", "LOW");
    Result result =
        route(fakeRequest("PUT", baseRoute + customer.uuid).cookie(validCookie).bodyJson(params));
    assertEquals(OK, result.status());
    CustomerConfig config = CustomerConfig.getAlertConfig(customer.uuid);
    assertEquals(alertEmail, config.data.get("alertingEmail").asText());
    JsonNode json = Json.parse(contentAsString(result));
    assertThat(json.get("uuid").asText(), is(equalTo(customer.uuid.toString())));
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testCustomerPUTWithSetAlertNotificationPeriod() {
    Alert alert = ModelFactory.createAlert(customer);
    alert.setNotificationAttemptTime(new Date());
    alert.setNextNotificationTime(null);
    alert.save();

    Alert alert2 = ModelFactory.createAlert(customer);
    alert2.setState(State.RESOLVED);
    alert2.setNotificationAttemptTime(new Date());
    alert2.setNextNotificationTime(null);
    alert2.save();

    String authToken = user.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    ObjectNode params = Json.newObject();
    params.put("code", "tc");
    params.put("email", "admin");
    params.put("name", "Test Customer");
    ObjectNode alertingData = Json.newObject();
    String alertEmail = "alerts@yugabyte.com";
    alertingData.put("alertingEmail", alertEmail);
    alertingData.put("sendAlertsToYb", true);
    alertingData.put("reportOnlyErrors", false);
    alertingData.put("activeAlertNotificationIntervalMs", 5000);
    params.put("alertingData", alertingData);
    params.put("callhomeLevel", "LOW");
    Result result =
        route(fakeRequest("PUT", baseRoute + customer.uuid).cookie(validCookie).bodyJson(params));
    assertEquals(OK, result.status());

    Alert updatedAlert = alertService.get(alert.getUuid());
    assertThat(
        updatedAlert.getNextNotificationTime(),
        equalTo(datePlus(alert.getNotificationAttemptTime(), 5000, ChronoUnit.MILLIS)));

    Alert updatedAlert2 = alertService.get(alert2.getUuid());
    assertThat(updatedAlert2.getNextNotificationTime(), nullValue());
  }

  @Test
  public void testCustomerPUTWithUnsetAlertNotificationPeriod() {
    Alert alert = ModelFactory.createAlert(customer);
    alert.setNotifiedState(State.ACTIVE);
    alert.save();

    Alert alert2 = ModelFactory.createAlert(customer);

    AlertingData alertingData = new AlertingData();
    alertingData.activeAlertNotificationIntervalMs = 5000;
    CustomerConfig.createAlertConfig(customer.getUuid(), Json.toJson(alertingData));

    String authToken = user.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    ObjectNode params = Json.newObject();
    params.put("code", "tc");
    params.put("email", "admin");
    params.put("name", "Test Customer");
    ObjectNode newAlertingData = Json.newObject();
    String alertEmail = "alerts@yugabyte.com";
    newAlertingData.put("alertingEmail", alertEmail);
    newAlertingData.put("sendAlertsToYb", true);
    newAlertingData.put("reportOnlyErrors", false);
    newAlertingData.put("activeAlertNotificationIntervalMs", 0);
    params.put("alertingData", newAlertingData);
    params.put("callhomeLevel", "LOW");
    Result result =
        route(fakeRequest("PUT", baseRoute + customer.uuid).cookie(validCookie).bodyJson(params));
    assertEquals(OK, result.status());

    Alert updatedAlert = alertService.get(alert.getUuid());
    assertThat(updatedAlert.getNextNotificationTime(), nullValue());
    Alert updatedAlert2 = alertService.get(alert2.getUuid());
    assertThat(updatedAlert2.getNextNotificationTime(), notNullValue());
    assertThat(updatedAlert2.getNextNotificationTime(), equalTo(alert2.getNextNotificationTime()));
  }

  @Test
  public void testCustomerPUTWithSmtpData() {
    String authToken = user.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    ObjectNode params = Json.newObject();
    params.put("code", "tc");
    params.put("email", "admin");
    params.put("name", "Test Customer");
    ObjectNode smtpData = Json.newObject();
    String smtpEmail = "alerts@yugabyte.com";
    smtpData.put("smtpUsername", smtpEmail);
    smtpData.put("smtpServer", "test.foo.bar");
    params.put("smtpData", smtpData);
    params.put("callhomeLevel", "MEDIUM");
    Result result =
        route(fakeRequest("PUT", baseRoute + customer.uuid).cookie(validCookie).bodyJson(params));
    assertEquals(OK, result.status());
    CustomerConfig config = CustomerConfig.getSmtpConfig(customer.uuid);
    assertEquals(smtpEmail, config.data.get("smtpUsername").asText());
    CollectionLevel callhomeLevel = CustomerConfig.getCallhomeLevel(customer.uuid);
    assertEquals(CollectionLevel.MEDIUM, callhomeLevel);
    JsonNode json = Json.parse(contentAsString(result));
    assertThat(json.get("uuid").asText(), is(equalTo(customer.uuid.toString())));
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testCustomerPUTWithSmtpAndAlertData() {
    String authToken = user.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    ObjectNode params = Json.newObject();
    params.put("code", "tc");
    params.put("email", "admin");
    params.put("name", "Test Customer");
    ObjectNode smtpData = Json.newObject();
    String smtpEmail = "alerts@yugabyte.com";
    smtpData.put("smtpUsername", smtpEmail);
    smtpData.put("smtpServer", "test.foo.bar");
    params.put("smtpData", smtpData);
    ObjectNode alertingData = Json.newObject();
    String alertEmail = "alerts@yugabyte.com";
    alertingData.put("alertingEmail", alertEmail);
    alertingData.put("sendAlertsToYb", true);
    alertingData.put("reportOnlyErrors", false);
    params.put("alertingData", alertingData);
    params.put("callhomeLevel", "MEDIUM");
    Result result =
        route(fakeRequest("PUT", baseRoute + customer.uuid).cookie(validCookie).bodyJson(params));
    assertEquals(OK, result.status());
    CustomerConfig smtpConfig = CustomerConfig.getSmtpConfig(customer.uuid);
    CustomerConfig alertConfig = CustomerConfig.getAlertConfig(customer.uuid);
    assertEquals(smtpEmail, smtpConfig.data.get("smtpUsername").asText());
    assertEquals(alertEmail, alertConfig.data.get("alertingEmail").asText());
    CollectionLevel callhomeLevel = CustomerConfig.getCallhomeLevel(customer.uuid);
    assertEquals(CollectionLevel.MEDIUM, callhomeLevel);
    JsonNode json = Json.parse(contentAsString(result));
    assertThat(json.get("uuid").asText(), is(equalTo(customer.uuid.toString())));
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testCustomerPUTWithDefaultCallhome() {
    String authToken = user.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    ObjectNode params = Json.newObject();
    params.put("code", "tc");
    params.put("email", "foo@bar.com");
    params.put("name", "Test Customer");
    params.put("password", "new-password");
    params.put("confirmPassword", "new-password");
    Result result =
        route(fakeRequest("PUT", baseRoute + customer.uuid).cookie(validCookie).bodyJson(params));
    assertEquals(OK, result.status());
    CustomerConfig callhomeConfig = CustomerConfig.getCallhomeConfig(customer.uuid);
    CollectionLevel callhomeLevel = CustomerConfig.getCallhomeLevel(customer.uuid);
    assertEquals(CollectionLevel.MEDIUM, callhomeLevel);
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testCustomerPUTWithValidFeatures() {
    String authToken = user.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    ObjectNode params = Json.newObject();
    params.put("code", "tc");
    params.put("email", "foo@bar.com");
    params.put("name", "Test Customer");
    JsonNode features = Json.parse("{\"foo\": \"bar\"}");
    params.set("features", features);

    Result result =
        route(fakeRequest("PUT", baseRoute + customer.uuid).cookie(validCookie).bodyJson(params));
    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(features, json.get("features"));
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testCustomerPUTWithInvalidFeatures()
      throws InterruptedException, ExecutionException, TimeoutException {
    String authToken = user.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    ObjectNode params = Json.newObject();
    params.put("code", "tc");
    params.put("email", "foo@bar.com");
    params.put("name", "Test Customer");
    params.put("features", "foo");

    Result result =
        routeWithYWErrHandler(
            fakeRequest("PUT", baseRoute + customer.uuid).cookie(validCookie).bodyJson(params));
    assertBadRequest(result, "{\"features\":[\"Invalid value\"]}");
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testFeatureUpsert()
      throws InterruptedException, ExecutionException, TimeoutException {
    String authToken = user.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    JsonNode inputFeatures = Json.parse("{\"features\": {\"foo\": \"bar\", \"key\": \"old\"}}");
    JsonNode expectedFeatures = Json.parse("{\"foo\": \"bar\", \"key\": \"old\"}");

    Result result =
        route(
            fakeRequest("PUT", baseRoute + customer.uuid + "/features")
                .cookie(validCookie)
                .bodyJson(inputFeatures));
    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(expectedFeatures, json);
    assertAuditEntry(1, customer.uuid);

    inputFeatures = Json.parse("{\"features\": {\"key\": \"new\"}}");
    expectedFeatures = Json.parse("{\"foo\": \"bar\", \"key\": \"new\"}");
    result =
        routeWithYWErrHandler(
            fakeRequest("PUT", baseRoute + customer.uuid + "/features")
                .cookie(validCookie)
                .bodyJson(inputFeatures));
    assertEquals(OK, result.status());
    json = Json.parse(contentAsString(result));
    assertEquals(expectedFeatures, json);
    assertAuditEntry(2, customer.uuid);
  }

  @Test
  public void testCustomerPUTWithInvalidUUID() {
    String authToken = user.createAuthToken();
    UUID invalidUUID = UUID.randomUUID();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    Result result = route(fakeRequest("PUT", baseRoute + invalidUUID).cookie(validCookie));
    assertEquals(FORBIDDEN, result.status());

    String resultString = contentAsString(result);
    assertThat(resultString, allOf(notNullValue(), equalTo("Unable To Authenticate User")));
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testCustomerDELETEWithValidUUID() {
    String authToken = user.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    Result result = route(fakeRequest("DELETE", baseRoute + customer.uuid).cookie(validCookie));
    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertTrue(json.get("success").asBoolean());
    assertAuditEntry(1, customer.uuid);
  }

  @Test
  public void testCustomerDELETEWithInvalidUUID() {
    UUID invalidUUID = UUID.randomUUID();

    String authToken = user.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    Result result = route(fakeRequest("DELETE", baseRoute + invalidUUID).cookie(validCookie));
    assertEquals(FORBIDDEN, result.status());

    String resultString = contentAsString(result);
    assertThat(resultString, allOf(notNullValue(), equalTo("Unable To Authenticate User")));
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testCustomerMetricsWithInvalidParams()
      throws InterruptedException, ExecutionException, TimeoutException {
    String authToken = user.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    ObjectNode params = Json.newObject();

    Result result =
        routeWithYWErrHandler(
            fakeRequest("POST", baseRoute + customer.uuid + "/metrics")
                .cookie(validCookie)
                .bodyJson(params));

    assertEquals(BAD_REQUEST, result.status());
    assertThat(
        contentAsString(result), is(containsString("\"start\":[\"This field is required\"]")));
    assertThat(
        contentAsString(result), is(containsString("\"metrics\":[\"This field is required\"]")));
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testCustomerMetricsWithValidMetricsParams() {
    String authToken = user.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    ObjectNode params = Json.newObject();
    params.set("metrics", Json.toJson(ImmutableList.of("metrics")));
    params.put("start", "1479281737000");

    ObjectNode response = Json.newObject();
    response.put("foo", "bar");

    when(mockMetricQueryHelper.query(anyList(), anyMap(), anyMap())).thenReturn(response);
    Result result =
        route(
            fakeRequest("POST", baseRoute + customer.uuid + "/metrics")
                .cookie(validCookie)
                .bodyJson(params));
    assertEquals(OK, result.status());
    assertThat(contentAsString(result), allOf(notNullValue(), containsString("{\"foo\":\"bar\"}")));
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testCustomerMetricsForContainerMetricsMultiAZ() {
    String authToken = user.createAuthToken();
    ObjectNode params = Json.newObject();
    params.set("metrics", Json.toJson(ImmutableList.of("container_metrics")));
    params.put("start", "1479281737000");
    params.put("nodePrefix", "demo");
    Universe u1 = createUniverse("demo", customer.getCustomerId());
    Provider provider =
        Provider.get(
            UUID.fromString(u1.getUniverseDetails().getPrimaryCluster().userIntent.provider));
    Region r = Region.create(provider, "region-1", "PlacementRegion-1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ-1", "subnet-1");
    Region r1 = Region.create(provider, "region-2", "PlacementRegion-2", "default-image");
    AvailabilityZone.createOrThrow(r1, "az-2", "PlacementAZ-2", "subnet-2");
    AvailabilityZone az3 = AvailabilityZone.createOrThrow(r1, "az-3", "PlacementAZ-3", "subnet-3");
    az3.updateConfig(ImmutableMap.of("KUBENAMESPACE", "test-ns-1"));

    ObjectNode response = Json.newObject();
    response.put("foo", "bar");
    ArgumentCaptor<ArrayList> metricKeys = ArgumentCaptor.forClass(ArrayList.class);
    ArgumentCaptor<Map> queryParams = ArgumentCaptor.forClass(Map.class);
    when(mockMetricQueryHelper.query(anyList(), anyMap(), anyMap())).thenReturn(response);
    Result result =
        FakeApiHelper.doRequestWithAuthTokenAndBody(
            "POST", baseRoute + customer.uuid + "/metrics", authToken, params);
    verify(mockMetricQueryHelper).query(metricKeys.capture(), queryParams.capture(), anyMap());
    assertThat(queryParams.getValue(), is(notNullValue()));
    JsonNode filters = Json.parse(queryParams.getValue().get("filters").toString());
    assertValue(filters, "namespace", "demo-az-1|demo-az-2|test-ns-1");
    assertEquals(OK, result.status());
    assertThat(contentAsString(result), allOf(notNullValue(), containsString("{\"foo\":\"bar\"}")));
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testCustomerMetricsForContainerMetricsSingleAZ() {
    String authToken = user.createAuthToken();
    ObjectNode params = Json.newObject();
    params.set("metrics", Json.toJson(ImmutableList.of("container_metrics")));
    params.put("start", "1479281737000");
    params.put("nodePrefix", "demo");
    Universe u1 = createUniverse("demo", customer.getCustomerId());
    Provider provider =
        Provider.get(
            UUID.fromString(u1.getUniverseDetails().getPrimaryCluster().userIntent.provider));
    Region r = Region.create(provider, "region-1", "PlacementRegion-1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ-1", "subnet-1");

    ObjectNode response = Json.newObject();
    response.put("foo", "bar");
    ArgumentCaptor<ArrayList> metricKeys = ArgumentCaptor.forClass(ArrayList.class);
    ArgumentCaptor<Map> queryParams = ArgumentCaptor.forClass(Map.class);
    when(mockMetricQueryHelper.query(anyList(), anyMap(), anyMap())).thenReturn(response);
    Result result =
        FakeApiHelper.doRequestWithAuthTokenAndBody(
            "POST", baseRoute + customer.uuid + "/metrics", authToken, params);
    verify(mockMetricQueryHelper).query(metricKeys.capture(), queryParams.capture(), anyMap());
    assertThat(queryParams.getValue(), is(notNullValue()));
    JsonNode filters = Json.parse(queryParams.getValue().get("filters").toString());
    assertValue(filters, "namespace", "demo");
    assertEquals(OK, result.status());
    assertThat(contentAsString(result), allOf(notNullValue(), containsString("{\"foo\":\"bar\"}")));
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testCustomerMetricsForContainerMetricsWithNodeName() {
    String authToken = user.createAuthToken();
    ObjectNode params = Json.newObject();
    params.set("metrics", Json.toJson(ImmutableList.of("container_metrics")));
    params.put("start", "1479281737000");
    params.put("nodePrefix", "demo");
    params.put("nodeName", "demo-n1");
    Universe u1 = createUniverse("demo", customer.getCustomerId());
    Provider provider =
        Provider.get(
            UUID.fromString(u1.getUniverseDetails().getPrimaryCluster().userIntent.provider));
    Region r = Region.create(provider, "region-1", "PlacementRegion-1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ-1", "subnet-1");

    ObjectNode response = Json.newObject();
    response.put("foo", "bar");
    ArgumentCaptor<ArrayList> metricKeys = ArgumentCaptor.forClass(ArrayList.class);
    ArgumentCaptor<Map> queryParams = ArgumentCaptor.forClass(Map.class);
    when(mockMetricQueryHelper.query(anyList(), anyMap(), anyMap())).thenReturn(response);
    Result result =
        FakeApiHelper.doRequestWithAuthTokenAndBody(
            "POST", baseRoute + customer.uuid + "/metrics", authToken, params);
    verify(mockMetricQueryHelper).query(metricKeys.capture(), queryParams.capture(), anyMap());
    assertThat(queryParams.getValue(), is(notNullValue()));
    JsonNode filters = Json.parse(queryParams.getValue().get("filters").toString());
    assertValue(filters, "namespace", "demo");
    assertValue(filters, "pod_name", "demo-n1");
    assertEquals(OK, result.status());
    assertThat(contentAsString(result), allOf(notNullValue(), containsString("{\"foo\":\"bar\"}")));
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testCustomerMetricsWithInValidMetricsParam()
      throws InterruptedException, ExecutionException, TimeoutException {
    String authToken = user.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    ObjectNode params = Json.newObject();
    params.set("metrics", Json.toJson(ImmutableList.of("metric1")));
    params.put("start", "1479281737");

    ObjectNode response =
        Json.newObject().put("success", false).put("error", "something went wrong");

    when(mockMetricQueryHelper.query(anyList(), anyMap(), anyMap())).thenReturn(response);
    Result result =
        routeWithYWErrHandler(
            fakeRequest("POST", baseRoute + customer.uuid + "/metrics")
                .cookie(validCookie)
                .bodyJson(params));
    assertEquals(BAD_REQUEST, result.status());
    assertThat(Json.parse(contentAsString(result)), allOf(notNullValue(), is(response)));
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testCustomerMetricsWithValidTableNameParams()
      throws InterruptedException, ExecutionException, TimeoutException {
    String authToken = user.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    Universe u1 = createUniverse("Foo-1", customer.getCustomerId());
    u1 = Universe.saveDetails(u1.universeUUID, ApiUtils.mockUniverseUpdater("host-1"));
    Universe u2 = createUniverse("Foo-2", customer.getCustomerId());
    u2 = Universe.saveDetails(u2.universeUUID, ApiUtils.mockUniverseUpdater("host-2"));
    customer.addUniverseUUID(u1.universeUUID);
    customer.addUniverseUUID(u2.universeUUID);
    customer.save();

    ObjectNode params = Json.newObject();
    params.set("metrics", Json.toJson(ImmutableList.of("metric")));
    params.put("start", "1479281737");
    params.put("nodePrefix", "host-1");
    params.put("tableName", "redis");

    ArgumentCaptor<ArrayList> metricKeys = ArgumentCaptor.forClass(ArrayList.class);
    ArgumentCaptor<Map> queryParams = ArgumentCaptor.forClass(Map.class);
    routeWithYWErrHandler(
        fakeRequest("POST", baseRoute + customer.uuid + "/metrics")
            .cookie(validCookie)
            .bodyJson(params));
    verify(mockMetricQueryHelper).query(metricKeys.capture(), queryParams.capture(), anyMap());
    assertThat(queryParams.getValue(), is(notNullValue()));
    JsonNode filters = Json.parse(queryParams.getValue().get("filters").toString());
    String tableName = filters.get("table_name").asText();
    assertThat(tableName, allOf(notNullValue(), equalTo("redis")));
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testCustomerMetricsWithoutTableNameParams() {
    String authToken = user.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    Universe u1 = createUniverse("Foo-1", customer.getCustomerId());
    u1 = Universe.saveDetails(u1.universeUUID, ApiUtils.mockUniverseUpdater("host-1"));
    Universe u2 = createUniverse("Foo-2", customer.getCustomerId());
    u2 = Universe.saveDetails(u2.universeUUID, ApiUtils.mockUniverseUpdater("host-2"));
    customer.addUniverseUUID(u1.universeUUID);
    customer.addUniverseUUID(u2.universeUUID);
    customer.save();

    ObjectNode response = Json.newObject();
    response.put("foo", "bar");
    when(mockMetricQueryHelper.query(anyList(), anyMap(), anyMap())).thenReturn(response);

    ObjectNode params = Json.newObject();
    params.set("metrics", Json.toJson(ImmutableList.of("metric")));
    params.put("start", "1479281737");
    params.put("nodePrefix", "host-1");

    ArgumentCaptor<ArrayList> metricKeys = ArgumentCaptor.forClass(ArrayList.class);
    ArgumentCaptor<Map> queryParams = ArgumentCaptor.forClass(Map.class);
    route(
        fakeRequest("POST", baseRoute + customer.uuid + "/metrics")
            .cookie(validCookie)
            .bodyJson(params));
    verify(mockMetricQueryHelper).query(metricKeys.capture(), queryParams.capture(), anyMap());
    assertThat(queryParams.getValue(), is(notNullValue()));
    JsonNode filters = Json.parse(queryParams.getValue().get("filters").toString());
    assertThat(filters.get("table_name"), nullValue());
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testCustomerMetricsExceptionThrown()
      throws InterruptedException, ExecutionException, TimeoutException {
    String authToken = user.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    ObjectNode params = Json.newObject();
    params.set("metrics", Json.toJson(ImmutableList.of("metric")));
    params.put("start", "1479281737");

    ObjectNode expectedResponse = Json.newObject();
    expectedResponse.put("success", false);
    expectedResponse.put("error", "Weird Data provided");

    when(mockMetricQueryHelper.query(anyList(), anyMap(), anyMap()))
        .thenThrow(new PlatformServiceException(BAD_REQUEST, "Weird Data provided"));
    Result result =
        routeWithYWErrHandler(
            fakeRequest("POST", baseRoute + customer.uuid + "/metrics")
                .cookie(validCookie)
                .bodyJson(params));
    assertEquals(BAD_REQUEST, result.status());
    assertThat(Json.parse(contentAsString(result)), allOf(notNullValue(), is(expectedResponse)));
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testCustomerMetricsWithMultipleUniverses()
      throws InterruptedException, ExecutionException, TimeoutException {
    String authToken = user.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    Universe u1 = createUniverse("Foo-1", customer.getCustomerId());
    u1 = Universe.saveDetails(u1.universeUUID, ApiUtils.mockUniverseUpdater("host-a"));
    Universe u2 = createUniverse("Foo-2", customer.getCustomerId());
    u2 = Universe.saveDetails(u2.universeUUID, ApiUtils.mockUniverseUpdater("host-b"));
    customer.addUniverseUUID(u1.universeUUID);
    customer.addUniverseUUID(u2.universeUUID);
    customer.save();

    ObjectNode params = Json.newObject();
    params.set("metrics", Json.toJson(ImmutableList.of("metric")));
    params.put("start", "1479281737");

    ObjectNode response = Json.newObject();
    response.put("foo", "bar");
    ArgumentCaptor<ArrayList> metricKeys = ArgumentCaptor.forClass(ArrayList.class);
    ArgumentCaptor<Map> queryParams = ArgumentCaptor.forClass(Map.class);

    routeWithYWErrHandler(
        fakeRequest("POST", baseRoute + customer.uuid + "/metrics")
            .cookie(validCookie)
            .bodyJson(params));
    verify(mockMetricQueryHelper).query(metricKeys.capture(), queryParams.capture(), anyMap());

    assertThat(metricKeys.getValue(), is(notNullValue()));
    assertThat(queryParams.getValue(), is(notNullValue()));
    JsonNode filters = Json.parse(queryParams.getValue().get("filters").toString());
    String nodePrefix = filters.get("node_prefix").asText();
    assertThat(nodePrefix, allOf(notNullValue(), containsString("host-a")));
    assertThat(nodePrefix, allOf(notNullValue(), containsString("host-b")));
    String[] nodePrefixes = nodePrefix.split("\\|");
    assertEquals(nodePrefixes.length, 2);
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testCustomerMetricsWithNodePrefixParam() {
    String authToken = user.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    Universe u1 = createUniverse("Foo-1", customer.getCustomerId());
    u1 = Universe.saveDetails(u1.universeUUID, ApiUtils.mockUniverseUpdater("host-1"));
    Universe u2 = createUniverse("Foo-2", customer.getCustomerId());
    u2 = Universe.saveDetails(u2.universeUUID, ApiUtils.mockUniverseUpdater("host-2"));
    customer.addUniverseUUID(u1.universeUUID);
    customer.addUniverseUUID(u2.universeUUID);
    customer.save();

    ObjectNode response = Json.newObject();
    response.put("foo", "bar");
    when(mockMetricQueryHelper.query(anyList(), anyMap(), anyMap())).thenReturn(response);

    ObjectNode params = Json.newObject();
    params.set("metrics", Json.toJson(ImmutableList.of("metric")));
    params.put("start", "1479281737");
    params.put("nodePrefix", "host-1");

    ArgumentCaptor<ArrayList> metricKeys = ArgumentCaptor.forClass(ArrayList.class);
    ArgumentCaptor<Map> queryParams = ArgumentCaptor.forClass(Map.class);
    route(
        fakeRequest("POST", baseRoute + customer.uuid + "/metrics")
            .cookie(validCookie)
            .bodyJson(params));
    verify(mockMetricQueryHelper).query(metricKeys.capture(), queryParams.capture(), anyMap());
    assertThat(queryParams.getValue(), is(notNullValue()));
    JsonNode filters = Json.parse(queryParams.getValue().get("filters").toString());
    String nodePrefix = filters.get("node_prefix").asText();
    assertThat(nodePrefix, allOf(notNullValue(), equalTo("host-1")));
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testCustomerMetricsWithNodeNameParam() {
    String authToken = user.createAuthToken();

    Universe u1 = createUniverse("Foo-1", customer.getCustomerId());
    u1 = Universe.saveDetails(u1.universeUUID, ApiUtils.mockUniverseUpdater("host-1"));
    customer.addUniverseUUID(u1.universeUUID);
    customer.save();

    ObjectNode response = Json.newObject();
    response.put("foo", "bar");
    when(mockMetricQueryHelper.query(anyList(), anyMap(), anyMap())).thenReturn(response);

    ObjectNode params = Json.newObject();
    params.set("metrics", Json.toJson(ImmutableList.of("metric")));
    params.put("start", "1479281737");
    params.put("nodePrefix", "host-1");
    params.put("nodeName", "host-n1");
    ArgumentCaptor<ArrayList> metricKeys = ArgumentCaptor.forClass(ArrayList.class);
    ArgumentCaptor<Map> queryParams = ArgumentCaptor.forClass(Map.class);
    FakeApiHelper.doRequestWithAuthTokenAndBody(
        "POST", baseRoute + customer.uuid + "/metrics", authToken, params);
    verify(mockMetricQueryHelper).query(metricKeys.capture(), queryParams.capture(), any());
    assertThat(queryParams.getValue(), is(notNullValue()));
    JsonNode filters = Json.parse(queryParams.getValue().get("filters").toString());
    String nodeName = filters.get("exported_instance").asText();
    assertThat(nodeName, allOf(notNullValue(), equalTo("host-n1")));
    assertAuditEntry(0, customer.uuid);
  }

  private Result getHostInfo(UUID customerUUID) {
    String uri = baseRoute + customerUUID + "/host_info";
    return FakeApiHelper.doRequestWithAuthToken("GET", uri, user.createAuthToken());
  }

  @Test
  public void testCustomerHostInfoWithInvalidCustomer() {
    UUID randomUUID = UUID.randomUUID();
    Result result = getHostInfo(randomUUID);
    assertEquals(FORBIDDEN, result.status());

    String resultString = contentAsString(result);
    assertThat(resultString, allOf(notNullValue(), equalTo("Unable To Authenticate User")));
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testCustomerHostInfo() {
    JsonNode response = Json.parse("{\"foo\": \"bar\"}");
    when(mockCloudQueryHelper.currentHostInfo(
            Common.CloudType.aws, ImmutableList.of("instance-id", "vpc-id", "privateIp", "region")))
        .thenReturn(response);
    when(mockCloudQueryHelper.currentHostInfo(Common.CloudType.gcp, null)).thenReturn(response);
    Result result = getHostInfo(customer.uuid);
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());
    ObjectNode responseNode = Json.newObject();
    responseNode.put("aws", response);
    responseNode.put("gcp", response);
    assertEquals(json, responseNode);
    assertAuditEntry(0, customer.uuid);
  }
}
