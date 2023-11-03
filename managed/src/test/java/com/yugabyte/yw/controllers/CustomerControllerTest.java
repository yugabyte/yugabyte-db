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
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.anyBoolean;
import static org.mockito.ArgumentMatchers.anyList;
import static org.mockito.ArgumentMatchers.anyMap;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.doThrow;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.OK;
import static play.mvc.Http.Status.UNAUTHORIZED;
import static play.test.Helpers.contentAsString;
import static play.test.Helpers.fakeRequest;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ArrayNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableList;
import com.google.common.collect.ImmutableMap;
import com.google.common.collect.ImmutableSet;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.commissioner.Common.CloudType;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.CallHomeManager.CollectionLevel;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.forms.AlertingData;
import com.yugabyte.yw.forms.PlatformResults.YBPError;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.forms.XClusterConfigCreateFormData;
import com.yugabyte.yw.metrics.MetricSettings;
import com.yugabyte.yw.models.Alert;
import com.yugabyte.yw.models.Alert.State;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.Users.Role;
import com.yugabyte.yw.models.XClusterConfig;
import com.yugabyte.yw.models.configs.CustomerConfig;
import com.yugabyte.yw.models.helpers.NodeDetails;
import java.time.temporal.ChronoUnit;
import java.util.ArrayList;
import java.util.Date;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.TimeoutException;
import org.hamcrest.Matchers;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.ArgumentCaptor;
import org.mockito.Captor;
import org.mockito.MockitoAnnotations;
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

  @Captor private ArgumentCaptor<ArrayList<MetricSettings>> metricKeys;

  @Captor private ArgumentCaptor<Map<String, String>> queryParams;

  @Before
  public void setUp() {
    MockitoAnnotations.initMocks(this);
    customer = ModelFactory.testCustomer();
    user = ModelFactory.testUser(customer);
  }

  @Test
  public void testListCustomersWithoutAuth() {
    Result result = route(fakeRequest("GET", rootRoute));
    assertEquals(UNAUTHORIZED, result.status());
  }

  @Test
  public void testListCustomersUuidsWithAuth() {
    String authToken = user.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    Result result = route(fakeRequest("GET", rootRoute + "_uuids").cookie(validCookie));
    assertEquals(OK, result.status());
    ArrayNode json = (ArrayNode) Json.parse(contentAsString(result));
    assertEquals(json.get(0).textValue(), customer.getUuid().toString());
  }

  @Test
  public void testListWithDataCustomersWithAuth() {
    String authToken = user.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    Result result = route(fakeRequest("GET", rootRoute).cookie(validCookie));
    assertEquals(OK, result.status());
    ArrayNode json = (ArrayNode) Json.parse(contentAsString(result));
    JsonNode node = json.get(0);
    assertEquals(node.get("uuid").textValue(), customer.getUuid().toString());
    assertNull(node.get("universeUuids"));
  }

  @Test
  public void testListCustomersWithUniverseUuids() {
    Universe universe = createUniverse(customer.getId());
    String authToken = user.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    Result result =
        route(fakeRequest("GET", rootRoute + "?includeUniverseUuids=true").cookie(validCookie));
    assertEquals(OK, result.status());
    ArrayNode json = (ArrayNode) Json.parse(contentAsString(result));
    JsonNode node = json.get(0);
    assertEquals(node.get("uuid").textValue(), customer.getUuid().toString());
    ArrayNode universeUuids = (ArrayNode) node.get("universeUuids");
    assertEquals(1, universeUuids.size());
    assertEquals(universe.getUniverseUUID(), Json.fromJson(universeUuids.get(0), UUID.class));
  }

  // check that invalid creds is failing to do that

  @Test
  public void testCustomerGETWithValidUUID() {
    String authToken = user.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    Result result = route(fakeRequest("GET", baseRoute + customer.getUuid()).cookie(validCookie));
    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));

    assertThat(
        json.get("uuid").asText(), allOf(notNullValue(), equalTo(customer.getUuid().toString())));
    assertEquals(json.get("name").asText(), customer.getName());
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testCustomerGETWithInvalidUUID() {
    String authToken = user.createAuthToken();
    UUID invalidUUID = UUID.randomUUID();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    Result result = route(fakeRequest("GET", baseRoute + invalidUUID).cookie(validCookie));
    assertEquals(UNAUTHORIZED, result.status());

    String resultString = contentAsString(result);
    assertThat(resultString, allOf(notNullValue(), equalTo("Unable To Authenticate User")));
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testCustomerGETWithBadUUID() {
    String authToken = user.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    final String method = "GET";
    final String uri = baseRoute + "null";
    Result result = route(fakeRequest(method, uri).cookie(validCookie));
    assertEquals(BAD_REQUEST, result.status());

    JsonNode ybpError = Json.parse(contentAsString(result));
    assertEquals(
        Json.toJson(
            new YBPError(
                method,
                uri,
                "Cannot parse parameter cUUID as UUID: Invalid UUID string: null",
                null)),
        ybpError);
    assertAuditEntry(0, customer.getUuid());
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
        route(
            fakeRequest("PUT", baseRoute + customer.getUuid())
                .cookie(validCookie)
                .bodyJson(params));
    assertEquals(OK, result.status());

    user.setRole(Role.ReadOnly);
    user.save();

    result = route(fakeRequest("GET", baseRoute + customer.getUuid()).cookie(validCookie));
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
        route(
            fakeRequest("PUT", baseRoute + customer.getUuid())
                .cookie(validCookie)
                .bodyJson(params));
    assertEquals(OK, result.status());
    CustomerConfig.getCallhomeConfig(customer.getUuid());
    CollectionLevel callhomeLevel = CustomerConfig.getCallhomeLevel(customer.getUuid());
    assertEquals(CollectionLevel.LOW, callhomeLevel);
    JsonNode json = Json.parse(contentAsString(result));
    assertThat(json.get("uuid").asText(), is(equalTo(customer.getUuid().toString())));
    assertThat(json.get("name").asText(), is(equalTo("Test Customer")));
    assertAuditEntry(1, customer.getUuid());
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
        route(
            fakeRequest("PUT", baseRoute + customer.getUuid())
                .cookie(validCookie)
                .bodyJson(params));
    assertEquals(OK, result.status());
    CustomerConfig config = CustomerConfig.getAlertConfig(customer.getUuid());
    assertEquals(alertEmail, config.getData().get("alertingEmail").asText());
    JsonNode json = Json.parse(contentAsString(result));
    assertThat(json.get("uuid").asText(), is(equalTo(customer.getUuid().toString())));
    assertAuditEntry(1, customer.getUuid());
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
        route(
            fakeRequest("PUT", baseRoute + customer.getUuid())
                .cookie(validCookie)
                .bodyJson(params));
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
    CustomerConfig.createAlertConfig(customer.getUuid(), Json.toJson(alertingData)).save();

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
        route(
            fakeRequest("PUT", baseRoute + customer.getUuid())
                .cookie(validCookie)
                .bodyJson(params));
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
        route(
            fakeRequest("PUT", baseRoute + customer.getUuid())
                .cookie(validCookie)
                .bodyJson(params));
    assertEquals(OK, result.status());
    CustomerConfig config = CustomerConfig.getSmtpConfig(customer.getUuid());
    assertEquals(smtpEmail, config.getData().get("smtpUsername").asText());
    CollectionLevel callhomeLevel = CustomerConfig.getCallhomeLevel(customer.getUuid());
    assertEquals(CollectionLevel.MEDIUM, callhomeLevel);
    JsonNode json = Json.parse(contentAsString(result));
    assertThat(json.get("uuid").asText(), is(equalTo(customer.getUuid().toString())));
    assertAuditEntry(1, customer.getUuid());
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
        route(
            fakeRequest("PUT", baseRoute + customer.getUuid())
                .cookie(validCookie)
                .bodyJson(params));
    assertEquals(OK, result.status());
    CustomerConfig smtpConfig = CustomerConfig.getSmtpConfig(customer.getUuid());
    CustomerConfig alertConfig = CustomerConfig.getAlertConfig(customer.getUuid());
    assertEquals(smtpEmail, smtpConfig.getData().get("smtpUsername").asText());
    assertEquals(alertEmail, alertConfig.getData().get("alertingEmail").asText());
    CollectionLevel callhomeLevel = CustomerConfig.getCallhomeLevel(customer.getUuid());
    assertEquals(CollectionLevel.MEDIUM, callhomeLevel);
    JsonNode json = Json.parse(contentAsString(result));
    assertThat(json.get("uuid").asText(), is(equalTo(customer.getUuid().toString())));
    assertAuditEntry(1, customer.getUuid());
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
        route(
            fakeRequest("PUT", baseRoute + customer.getUuid())
                .cookie(validCookie)
                .bodyJson(params));
    assertEquals(OK, result.status());
    CustomerConfig.getCallhomeConfig(customer.getUuid());
    CollectionLevel callhomeLevel = CustomerConfig.getCallhomeLevel(customer.getUuid());
    assertEquals(CollectionLevel.MEDIUM, callhomeLevel);
    assertAuditEntry(1, customer.getUuid());
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
        route(
            fakeRequest("PUT", baseRoute + customer.getUuid())
                .cookie(validCookie)
                .bodyJson(params));
    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(features, json.get("features"));
    assertAuditEntry(1, customer.getUuid());
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
            fakeRequest("PUT", baseRoute + customer.getUuid())
                .cookie(validCookie)
                .bodyJson(params));
    assertBadRequest(result, "{\"features\":[\"Invalid value\"]}");
    assertAuditEntry(0, customer.getUuid());
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
            fakeRequest("PUT", baseRoute + customer.getUuid() + "/features")
                .cookie(validCookie)
                .bodyJson(inputFeatures));
    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(expectedFeatures, json);
    assertAuditEntry(1, customer.getUuid());

    inputFeatures = Json.parse("{\"features\": {\"key\": \"new\"}}");
    expectedFeatures = Json.parse("{\"foo\": \"bar\", \"key\": \"new\"}");
    result =
        routeWithYWErrHandler(
            fakeRequest("PUT", baseRoute + customer.getUuid() + "/features")
                .cookie(validCookie)
                .bodyJson(inputFeatures));
    assertEquals(OK, result.status());
    json = Json.parse(contentAsString(result));
    assertEquals(expectedFeatures, json);
    assertAuditEntry(2, customer.getUuid());
  }

  @Test
  public void testCustomerPUTWithInvalidUUID() {
    String authToken = user.createAuthToken();
    UUID invalidUUID = UUID.randomUUID();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    Result result = route(fakeRequest("PUT", baseRoute + invalidUUID).cookie(validCookie));
    assertEquals(UNAUTHORIZED, result.status());

    String resultString = contentAsString(result);
    assertThat(resultString, allOf(notNullValue(), equalTo("Unable To Authenticate User")));
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testCustomerDELETEWithValidUUID() {
    String authToken = user.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    Result result =
        route(fakeRequest("DELETE", baseRoute + customer.getUuid()).cookie(validCookie));
    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertTrue(json.get("success").asBoolean());
    assertAuditEntry(1, customer.getUuid());
    assertEquals(0, CustomerTask.getByCustomerUUID(customer.getUuid()).size());
  }

  @Test
  public void testCustomerDELETEWithInvalidUUID() {
    UUID invalidUUID = UUID.randomUUID();

    String authToken = user.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    Result result = route(fakeRequest("DELETE", baseRoute + invalidUUID).cookie(validCookie));
    assertEquals(UNAUTHORIZED, result.status());

    String resultString = contentAsString(result);
    assertThat(resultString, allOf(notNullValue(), equalTo("Unable To Authenticate User")));
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testCustomerMetricsWithMissingStart()
      throws InterruptedException, ExecutionException, TimeoutException {
    String authToken = user.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    ObjectNode params = Json.newObject();

    Result result =
        routeWithYWErrHandler(
            fakeRequest("POST", baseRoute + customer.getUuid() + "/metrics")
                .cookie(validCookie)
                .bodyJson(params));

    assertEquals(BAD_REQUEST, result.status());
    assertThat(
        contentAsString(result), is(containsString("\"start\":[\"This field is required\"]")));
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testCustomerMetricsWithMissingMetrics()
      throws InterruptedException, ExecutionException, TimeoutException {
    String authToken = user.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    ObjectNode params = Json.newObject();
    params.put("start", 100500);

    Result result =
        routeWithYWErrHandler(
            fakeRequest("POST", baseRoute + customer.getUuid() + "/metrics")
                .cookie(validCookie)
                .bodyJson(params));

    assertEquals(BAD_REQUEST, result.status());
    assertThat(
        contentAsString(result),
        is(containsString("Either metrics or metricsWithSettings should not be empty")));
    assertAuditEntry(0, customer.getUuid());
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

    doReturn(response)
        .when(mockMetricQueryHelper)
        .query(anyList(), anyMap(), anyMap(), anyBoolean());
    Result result =
        route(
            fakeRequest("POST", baseRoute + customer.getUuid() + "/metrics")
                .cookie(validCookie)
                .bodyJson(params));
    assertEquals(OK, result.status());
    assertThat(contentAsString(result), allOf(notNullValue(), containsString("{\"foo\":\"bar\"}")));
  }

  @Test
  public void testCustomerMetricsForContainerMetricsMultiAZ() {
    String authToken = user.createAuthToken();
    ObjectNode params = Json.newObject();
    params.set("metrics", Json.toJson(ImmutableList.of("container_metrics")));
    params.put("start", "1479281737000");
    params.put("nodePrefix", "yb-tc-demo");
    Universe u1 = createUniverse("demo", customer.getId(), CloudType.kubernetes);
    Provider provider =
        Provider.get(
            UUID.fromString(u1.getUniverseDetails().getPrimaryCluster().userIntent.provider));
    Region r = Region.create(provider, "region-1", "PlacementRegion-1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ-1", "subnet-1");
    Region r1 = Region.create(provider, "region-2", "PlacementRegion-2", "default-image");
    AvailabilityZone.createOrThrow(r1, "az-2", "PlacementAZ-2", "subnet-2");
    AvailabilityZone az3 = AvailabilityZone.createOrThrow(r1, "az-3", "PlacementAZ-3", "subnet-3");
    az3.updateConfig(ImmutableMap.of("KUBENAMESPACE", "test-ns-1"));
    az3.save();

    ObjectNode response = Json.newObject();
    response.put("foo", "bar");
    ArgumentCaptor<ArrayList> metricKeys = ArgumentCaptor.forClass(ArrayList.class);
    ArgumentCaptor<Map> queryParams = ArgumentCaptor.forClass(Map.class);
    doReturn(response)
        .when(mockMetricQueryHelper)
        .query(anyList(), anyMap(), anyMap(), anyBoolean());
    Result result =
        doRequestWithAuthTokenAndBody(
            "POST", baseRoute + customer.getUuid() + "/metrics", authToken, params);
    verify(mockMetricQueryHelper)
        .query(metricKeys.capture(), queryParams.capture(), anyMap(), anyBoolean());
    assertThat(queryParams.getValue(), is(notNullValue()));
    JsonNode filters = Json.parse(queryParams.getValue().get("filters").toString());
    assertValue(filters, "namespace", "yb-tc-demo-az-1|yb-tc-demo-az-2|test-ns-1");
    assertEquals(OK, result.status());
    assertThat(contentAsString(result), allOf(notNullValue(), containsString("{\"foo\":\"bar\"}")));
  }

  @Test
  public void testCustomerMetricsForContainerMetricsSingleAZ() {
    testCustomerMetricsForContainerMetricsSingleAZBase(false);
  }

  @Test
  public void testCustomerMetricsForContainerMetricsNewNamingStyle() {
    testCustomerMetricsForContainerMetricsSingleAZBase(true);
  }

  private void testCustomerMetricsForContainerMetricsSingleAZBase(boolean helmNewNamingStyle) {
    String authToken = user.createAuthToken();
    ObjectNode params = Json.newObject();
    params.set("metrics", Json.toJson(ImmutableList.of("container_metrics")));
    params.put("start", "1479281737000");
    params.put("nodePrefix", "yb-tc-demo");
    Universe u1 = createUniverse("demo", customer.getId(), CloudType.kubernetes);
    if (helmNewNamingStyle) {
      u1 =
          Universe.saveDetails(
              u1.getUniverseUUID(), ApiUtils.mockUniverseUpdaterWithHelmNamingStyle(true));
    }
    Provider provider =
        Provider.get(
            UUID.fromString(u1.getUniverseDetails().getPrimaryCluster().userIntent.provider));
    Region r = Region.create(provider, "region-1", "PlacementRegion-1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ-1", "subnet-1");

    ObjectNode response = Json.newObject();
    response.put("foo", "bar");
    ArgumentCaptor<ArrayList> metricKeys = ArgumentCaptor.forClass(ArrayList.class);
    ArgumentCaptor<Map> queryParams = ArgumentCaptor.forClass(Map.class);
    doReturn(response)
        .when(mockMetricQueryHelper)
        .query(anyList(), anyMap(), anyMap(), anyBoolean());
    Result result =
        doRequestWithAuthTokenAndBody(
            "POST", baseRoute + customer.getUuid() + "/metrics", authToken, params);
    verify(mockMetricQueryHelper)
        .query(metricKeys.capture(), queryParams.capture(), anyMap(), anyBoolean());
    assertThat(queryParams.getValue(), is(notNullValue()));
    JsonNode filters = Json.parse(queryParams.getValue().get("filters").toString());
    assertValue(filters, "namespace", "yb-tc-demo");
    if (helmNewNamingStyle) {
      assertValue(filters, "pod_name", "ybdemo-xlrv-yb-tserver-(.*)");
    } else {
      assertNull(filters.get("pod_name"));
    }
    assertEquals(OK, result.status());
    assertThat(contentAsString(result), allOf(notNullValue(), containsString("{\"foo\":\"bar\"}")));
  }

  @Test
  public void testCustomerMetricsForContainerMetricsWithNodeName() {
    String authToken = user.createAuthToken();
    ObjectNode params = Json.newObject();
    params.set("metrics", Json.toJson(ImmutableList.of("container_metrics")));
    params.put("start", "1479281737000");
    params.put("nodePrefix", "yb-tc-demo");
    ArrayNode nodeNames = Json.newArray();
    nodeNames.add("demo-n1");
    params.put("nodeNames", nodeNames);
    Universe u1 = createUniverse("demo", customer.getId());
    u1 =
        Universe.saveDetails(
            u1.getUniverseUUID(),
            univ -> {
              UniverseDefinitionTaskParams details = univ.getUniverseDetails();
              NodeDetails node = ApiUtils.getDummyNodeDetails(0);
              node.nodeName = "demo-n1";
              node.cloudInfo.private_ip =
                  "yb-pod-name-az.yb-pod-service.demo-namespace.svc.cluster.local";
              node.cloudInfo.kubernetesNamespace = "diff-ns";
              details.nodeDetailsSet.add(node);
              univ.setUniverseDetails(details);
            });
    Provider provider =
        Provider.get(
            UUID.fromString(u1.getUniverseDetails().getPrimaryCluster().userIntent.provider));
    Region r = Region.create(provider, "region-1", "PlacementRegion-1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ-1", "subnet-1");

    ObjectNode response = Json.newObject();
    response.put("foo", "bar");
    ArgumentCaptor<ArrayList> metricKeys = ArgumentCaptor.forClass(ArrayList.class);
    ArgumentCaptor<Map> queryParams = ArgumentCaptor.forClass(Map.class);
    doReturn(response)
        .when(mockMetricQueryHelper)
        .query(anyList(), anyMap(), anyMap(), anyBoolean());
    Result result =
        doRequestWithAuthTokenAndBody(
            "POST", baseRoute + customer.getUuid() + "/metrics", authToken, params);
    verify(mockMetricQueryHelper)
        .query(metricKeys.capture(), queryParams.capture(), anyMap(), anyBoolean());
    assertThat(queryParams.getValue(), is(notNullValue()));
    JsonNode filters = Json.parse(queryParams.getValue().get("filters").toString());
    assertValue(filters, "namespace", "diff-ns");
    assertValue(filters, "pod_name", "yb-pod-name-az");
    assertEquals(OK, result.status());
    assertThat(contentAsString(result), allOf(notNullValue(), containsString("{\"foo\":\"bar\"}")));
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

    doReturn(response)
        .when(mockMetricQueryHelper)
        .query(anyList(), anyMap(), anyMap(), anyBoolean());
    Result result =
        routeWithYWErrHandler(
            fakeRequest("POST", baseRoute + customer.getUuid() + "/metrics")
                .cookie(validCookie)
                .bodyJson(params));
    assertEquals(BAD_REQUEST, result.status());
    assertThat(Json.parse(contentAsString(result)), allOf(notNullValue(), is(response)));
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testCustomerMetricsWithValidTableNameParams()
      throws InterruptedException, ExecutionException, TimeoutException {
    String authToken = user.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    Universe u1 = createUniverse("Foo-1", customer.getId());
    u1 = Universe.saveDetails(u1.getUniverseUUID(), ApiUtils.mockUniverseUpdater("host-1"));
    Universe u2 = createUniverse("Foo-2", customer.getId());
    u2 = Universe.saveDetails(u2.getUniverseUUID(), ApiUtils.mockUniverseUpdater("host-2"));

    ObjectNode params = Json.newObject();
    params.set("metrics", Json.toJson(ImmutableList.of("metric")));
    params.put("start", "1479281737");
    params.put("nodePrefix", "host-1");
    params.put("tableName", "redis");

    ArgumentCaptor<ArrayList> metricKeys = ArgumentCaptor.forClass(ArrayList.class);
    ArgumentCaptor<Map> queryParams = ArgumentCaptor.forClass(Map.class);
    routeWithYWErrHandler(
        fakeRequest("POST", baseRoute + customer.getUuid() + "/metrics")
            .cookie(validCookie)
            .bodyJson(params));
    verify(mockMetricQueryHelper)
        .query(metricKeys.capture(), queryParams.capture(), anyMap(), anyBoolean());
    assertThat(queryParams.getValue(), is(notNullValue()));
    JsonNode filters = Json.parse(queryParams.getValue().get("filters").toString());
    String tableName = filters.get("table_name").asText();
    assertThat(tableName, allOf(notNullValue(), equalTo("redis")));
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testCustomerMetricsWithoutTableNameParams() {
    String authToken = user.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    Universe u1 = createUniverse("Foo-1", customer.getId());
    u1 = Universe.saveDetails(u1.getUniverseUUID(), ApiUtils.mockUniverseUpdater("host-1"));
    Universe u2 = createUniverse("Foo-2", customer.getId());
    u2 = Universe.saveDetails(u2.getUniverseUUID(), ApiUtils.mockUniverseUpdater("host-2"));

    ObjectNode response = Json.newObject();
    response.put("foo", "bar");
    doReturn(response)
        .when(mockMetricQueryHelper)
        .query(anyList(), anyMap(), anyMap(), anyBoolean());

    ObjectNode params = Json.newObject();
    params.set("metrics", Json.toJson(ImmutableList.of("metric")));
    params.put("start", "1479281737");
    params.put("nodePrefix", "host-1");

    ArgumentCaptor<ArrayList> metricKeys = ArgumentCaptor.forClass(ArrayList.class);
    ArgumentCaptor<Map> queryParams = ArgumentCaptor.forClass(Map.class);
    route(
        fakeRequest("POST", baseRoute + customer.getUuid() + "/metrics")
            .cookie(validCookie)
            .bodyJson(params));
    verify(mockMetricQueryHelper)
        .query(metricKeys.capture(), queryParams.capture(), anyMap(), anyBoolean());
    assertThat(queryParams.getValue(), is(notNullValue()));
    JsonNode filters = Json.parse(queryParams.getValue().get("filters").toString());
    assertThat(filters.get("table_name"), nullValue());
  }

  @Test
  public void testCustomerMetricsWithTableIdParams()
      throws InterruptedException, ExecutionException, TimeoutException {
    String authToken = user.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    Universe u1 = createUniverse("Foo-1", customer.getId());
    u1 = Universe.saveDetails(u1.getUniverseUUID(), ApiUtils.mockUniverseUpdater("host-1"));
    Universe u2 = createUniverse("Foo-2", customer.getId());
    u2 = Universe.saveDetails(u2.getUniverseUUID(), ApiUtils.mockUniverseUpdater("host-2"));

    ObjectNode response = Json.newObject();
    response.put("foo", "bar");
    doReturn(response)
        .when(mockMetricQueryHelper)
        .query(anyList(), anyMap(), anyMap(), anyBoolean());

    ObjectNode params = Json.newObject();
    params.set("metrics", Json.toJson(ImmutableList.of("metric")));
    params.put("start", "1479281737");
    params.put("end", "1479285337");
    params.put("nodePrefix", "host-1");
    params.put("tableId", "fd601f9c19074262906638c8bd203971");

    routeWithYWErrHandler(
        fakeRequest("POST", baseRoute + customer.getUuid() + "/metrics")
            .cookie(validCookie)
            .bodyJson(params));
    verify(mockMetricQueryHelper)
        .query(metricKeys.capture(), queryParams.capture(), anyMap(), anyBoolean());
    assertThat(queryParams.getValue(), is(notNullValue()));
    JsonNode filters = Json.parse(queryParams.getValue().get("filters"));
    String tableId = filters.get("table_id").asText();
    assertThat(tableId, allOf(notNullValue(), equalTo("fd601f9c19074262906638c8bd203971")));
  }

  @Test
  public void testCustomerMetricsWithXClusterConfigUuidParams()
      throws InterruptedException, ExecutionException, TimeoutException {
    String authToken = user.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    Universe u1 = createUniverse("Foo-1", customer.getId());
    u1 = Universe.saveDetails(u1.getUniverseUUID(), ApiUtils.mockUniverseUpdater("host-1"));
    Universe u2 = createUniverse("Foo-2", customer.getId());
    u2 = Universe.saveDetails(u2.getUniverseUUID(), ApiUtils.mockUniverseUpdater("host-2"));

    // Create an xCluster config.
    XClusterConfigCreateFormData xClusterConfigCreateFormData = new XClusterConfigCreateFormData();
    xClusterConfigCreateFormData.name = "Repl1";
    xClusterConfigCreateFormData.sourceUniverseUUID = u1.getUniverseUUID();
    xClusterConfigCreateFormData.targetUniverseUUID = u2.getUniverseUUID();
    xClusterConfigCreateFormData.tables =
        ImmutableSet.of("fd601f9c19074262906638c8bd203971", "fd601f9c19074262906638c8bd203972");
    XClusterConfig xClusterConfig =
        XClusterConfig.create(
            xClusterConfigCreateFormData, XClusterConfig.XClusterConfigStatusType.Running);

    ObjectNode response = Json.newObject();
    response.put("foo", "bar");
    doReturn(response)
        .when(mockMetricQueryHelper)
        .query(anyList(), anyMap(), anyMap(), anyBoolean());

    ObjectNode params = Json.newObject();
    params.set("metrics", Json.toJson(ImmutableList.of("metric")));
    params.put("start", "1479281737");
    params.put("end", "1479285337");
    params.put("nodePrefix", "host-1");
    params.put("xClusterConfigUuid", xClusterConfig.getUuid().toString());

    routeWithYWErrHandler(
        fakeRequest("POST", baseRoute + customer.getUuid() + "/metrics")
            .cookie(validCookie)
            .bodyJson(params));
    verify(mockMetricQueryHelper)
        .query(metricKeys.capture(), queryParams.capture(), anyMap(), anyBoolean());
    assertThat(queryParams.getValue(), is(notNullValue()));
    JsonNode filters = Json.parse(queryParams.getValue().get("filters"));
    String tableId = filters.get("table_id").asText();
    assertThat(
        tableId,
        allOf(
            notNullValue(),
            Matchers.either(
                    Matchers.is(
                        "fd601f9c19074262906638c8bd203971|fd601f9c19074262906638c8bd203972"))
                .or(
                    Matchers.is(
                        "fd601f9c19074262906638c8bd203972|fd601f9c19074262906638c8bd203971"))));
  }

  @Test
  public void testCustomerMetricsExceptionThrown()
      throws InterruptedException, ExecutionException, TimeoutException {
    String authToken = user.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    ObjectNode params = Json.newObject();
    params.set("metrics", Json.toJson(ImmutableList.of("metric")));
    params.put("start", "1479281737");

    final String userVisibleMessage = "Weird Data provided";

    final String method = "POST";
    final String uri = baseRoute + customer.getUuid() + "/metrics";
    YBPError expectedYBPError = new YBPError(method, uri, userVisibleMessage, null);

    doThrow(new PlatformServiceException(BAD_REQUEST, userVisibleMessage))
        .when(mockMetricQueryHelper)
        .query(anyList(), anyMap(), anyMap(), anyBoolean());
    Result result =
        routeWithYWErrHandler(fakeRequest(method, uri).cookie(validCookie).bodyJson(params));
    assertEquals(BAD_REQUEST, result.status());
    assertThat(
        Json.parse(contentAsString(result)),
        allOf(notNullValue(), is(Json.toJson(expectedYBPError))));
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testCustomerMetricsWithMultipleUniverses()
      throws InterruptedException, ExecutionException, TimeoutException {
    String authToken = user.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    Universe u1 = createUniverse("Foo-1", customer.getId());
    u1 = Universe.saveDetails(u1.getUniverseUUID(), ApiUtils.mockUniverseUpdater("host-a"));
    Universe u2 = createUniverse("Foo-2", customer.getId());
    u2 = Universe.saveDetails(u2.getUniverseUUID(), ApiUtils.mockUniverseUpdater("host-b"));

    ObjectNode params = Json.newObject();
    params.set("metrics", Json.toJson(ImmutableList.of("metric")));
    params.put("start", "1479281737");

    ObjectNode response = Json.newObject();
    response.put("foo", "bar");
    ArgumentCaptor<ArrayList> metricKeys = ArgumentCaptor.forClass(ArrayList.class);
    ArgumentCaptor<Map> queryParams = ArgumentCaptor.forClass(Map.class);

    routeWithYWErrHandler(
        fakeRequest("POST", baseRoute + customer.getUuid() + "/metrics")
            .cookie(validCookie)
            .bodyJson(params));
    verify(mockMetricQueryHelper)
        .query(metricKeys.capture(), queryParams.capture(), anyMap(), anyBoolean());

    assertThat(metricKeys.getValue(), is(notNullValue()));
    assertThat(queryParams.getValue(), is(notNullValue()));
    JsonNode filters = Json.parse(queryParams.getValue().get("filters").toString());
    String nodePrefix = filters.get("node_prefix").asText();
    assertThat(nodePrefix, allOf(notNullValue(), containsString("host-a")));
    assertThat(nodePrefix, allOf(notNullValue(), containsString("host-b")));
    String[] nodePrefixes = nodePrefix.split("\\|");
    assertEquals(nodePrefixes.length, 2);
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testCustomerMetricsWithNodePrefixParam() {
    String authToken = user.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    Universe u1 = createUniverse("Foo-1", customer.getId());
    u1 = Universe.saveDetails(u1.getUniverseUUID(), ApiUtils.mockUniverseUpdater("host-1"));
    Universe u2 = createUniverse("Foo-2", customer.getId());
    u2 = Universe.saveDetails(u2.getUniverseUUID(), ApiUtils.mockUniverseUpdater("host-2"));

    ObjectNode response = Json.newObject();
    response.put("foo", "bar");
    doReturn(response)
        .when(mockMetricQueryHelper)
        .query(anyList(), anyMap(), anyMap(), anyBoolean());

    ObjectNode params = Json.newObject();
    params.set("metrics", Json.toJson(ImmutableList.of("metric")));
    params.put("start", "1479281737");
    params.put("nodePrefix", "host-1");

    ArgumentCaptor<ArrayList> metricKeys = ArgumentCaptor.forClass(ArrayList.class);
    ArgumentCaptor<Map> queryParams = ArgumentCaptor.forClass(Map.class);
    route(
        fakeRequest("POST", baseRoute + customer.getUuid() + "/metrics")
            .cookie(validCookie)
            .bodyJson(params));
    verify(mockMetricQueryHelper)
        .query(metricKeys.capture(), queryParams.capture(), anyMap(), anyBoolean());
    assertThat(queryParams.getValue(), is(notNullValue()));
    JsonNode filters = Json.parse(queryParams.getValue().get("filters").toString());
    String nodePrefix = filters.get("node_prefix").asText();
    assertThat(nodePrefix, allOf(notNullValue(), equalTo("host-1")));
  }

  @Test
  public void testCustomerMetricsWithNodeNameParam() {
    String authToken = user.createAuthToken();

    Universe u1 = createUniverse("Foo-1", customer.getId());
    u1 = Universe.saveDetails(u1.getUniverseUUID(), ApiUtils.mockUniverseUpdater("host-1"));

    ObjectNode response = Json.newObject();
    response.put("foo", "bar");
    doReturn(response)
        .when(mockMetricQueryHelper)
        .query(anyList(), anyMap(), anyMap(), anyBoolean());

    ObjectNode params = Json.newObject();
    params.set("metrics", Json.toJson(ImmutableList.of("metric")));
    params.put("start", "1479281737");
    params.put("nodePrefix", "host-1");
    ArrayNode nodeNames = Json.newArray();
    nodeNames.add("host-n1");
    params.put("nodeNames", nodeNames);
    ArgumentCaptor<ArrayList> metricKeys = ArgumentCaptor.forClass(ArrayList.class);
    ArgumentCaptor<Map> queryParams = ArgumentCaptor.forClass(Map.class);
    doRequestWithAuthTokenAndBody(
        "POST", baseRoute + customer.getUuid() + "/metrics", authToken, params);
    verify(mockMetricQueryHelper)
        .query(metricKeys.capture(), queryParams.capture(), any(), anyBoolean());
    assertThat(queryParams.getValue(), is(notNullValue()));
    JsonNode filters = Json.parse(queryParams.getValue().get("filters").toString());
    String nodeName = filters.get("exported_instance").asText();
    assertThat(nodeName, allOf(notNullValue(), equalTo("host-n1")));
  }

  private Result getHostInfo(UUID customerUUID) {
    String uri = baseRoute + customerUUID + "/host_info";
    return doRequestWithAuthToken("GET", uri, user.createAuthToken());
  }

  @Test
  public void testCustomerHostInfoWithInvalidCustomer() {
    UUID randomUUID = UUID.randomUUID();
    Result result = getHostInfo(randomUUID);
    assertEquals(UNAUTHORIZED, result.status());

    String resultString = contentAsString(result);
    assertThat(resultString, allOf(notNullValue(), equalTo("Unable To Authenticate User")));
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testCustomerHostInfo() {
    JsonNode response = Json.parse("{\"foo\": \"bar\"}");
    when(mockCloudQueryHelper.getCurrentHostInfo(Common.CloudType.aws)).thenReturn(response);
    when(mockCloudQueryHelper.getCurrentHostInfo(Common.CloudType.gcp)).thenReturn(response);
    Result result = getHostInfo(customer.getUuid());
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());
    ObjectNode responseNode = Json.newObject();
    responseNode.put("aws", response);
    responseNode.put("gcp", response);
    assertEquals(json, responseNode);
    assertAuditEntry(0, customer.getUuid());
  }
}
