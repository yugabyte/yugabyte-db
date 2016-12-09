// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.api.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.models.Customer;

import com.yugabyte.yw.models.Universe;
import org.junit.Before;
import org.junit.Test;
import org.mockito.ArgumentCaptor;
import play.Application;
import play.inject.guice.GuiceApplicationBuilder;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;
import play.test.Helpers;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.UUID;

import static org.hamcrest.CoreMatchers.*;
import static org.mockito.Matchers.anyList;
import static org.mockito.Matchers.anyMap;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static play.inject.Bindings.bind;
import static play.test.Helpers.*;
import static org.junit.Assert.*;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.fakeRequest;

public class CustomerControllerTest extends FakeDBApplication {
  MetricQueryHelper mockMetricQueryHelper;

  @Override
  protected Application provideApplication() {
    mockMetricQueryHelper = mock(MetricQueryHelper.class);
    Commissioner mockCommissioner = mock(Commissioner.class);

    return new GuiceApplicationBuilder()
      .configure((Map) Helpers.inMemoryDatabase())
      .overrides(bind(MetricQueryHelper.class).toInstance(mockMetricQueryHelper))
      .overrides(bind(Commissioner.class).toInstance(mockCommissioner))
      .build();
  }

  private Customer customer;

  @Before
  public void setUp() {
    customer = Customer.create("Valid Customer", "foo@bar.com", "password");
  }

  @Test
  public void testCustomerGETWithValidUUID() {
    String authToken = customer.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    Result result = route(fakeRequest("GET", "/api/customers/" + customer.uuid).cookie(validCookie));
    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));

    assertThat(json.get("uuid").asText(), allOf(notNullValue(), equalTo(customer.uuid.toString())));
    assertEquals(json.get("name").asText(), customer.name);
  }

  @Test
  public void testCustomerGETWithInvalidUUID() {
    String authToken = customer.createAuthToken();
    UUID invalidUUID = UUID.randomUUID();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    Result result = route(fakeRequest("GET", "/api/customers/" + invalidUUID).cookie(validCookie));
    assertEquals(BAD_REQUEST, result.status());
    JsonNode json = Json.parse(contentAsString(result));

    assertThat(json.get("error").asText(), is(containsString("Invalid Customer UUID:" + invalidUUID)));
  }

  @Test
  public void testCustomerPUTWithValidParams() {
    String authToken = customer.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    ObjectNode params = Json.newObject();
    params.put("email", "foo@bar.com");
    params.put("name", "Test Customer");
    params.put("password", "new-password");

    Result result = route(fakeRequest("PUT", "/api/customers/" + customer.uuid).cookie(validCookie).bodyJson(params));
    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertThat(json.get("uuid").asText(), is(equalTo(customer.uuid.toString())));
    assertThat(json.get("name").asText(), is(equalTo("Test Customer")));
  }

  @Test
  public void testCustomerPUTWithInvalidParams() {
    String authToken = customer.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    ObjectNode params = Json.newObject();
    params.put("password", "new-password");

    Result result = route(fakeRequest("PUT", "/api/customers/" + customer.uuid).cookie(validCookie).bodyJson(params));
    assertEquals(BAD_REQUEST, result.status());
    JsonNode json = Json.parse(contentAsString(result));

    assertThat(contentAsString(result), is(containsString("\"name\":[\"This field is required\"]")));
    assertThat(contentAsString(result), is(containsString("\"email\":[\"This field is required\"]")));
  }

  @Test
  public void testCustomerPUTWithInvalidUUID() {
    String authToken = customer.createAuthToken();
    UUID invalidUUID = UUID.randomUUID();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    Result result = route(fakeRequest("PUT", "/api/customers/" + invalidUUID).cookie(validCookie));
    assertEquals(BAD_REQUEST, result.status());
    JsonNode json = Json.parse(contentAsString(result));

    assertThat(json.get("error").asText(), is(containsString("Invalid Customer UUID:" + invalidUUID)));
  }

  @Test
  public void testCustomerDELETEWithValidUUID() {
    String authToken = customer.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    Result result = route(fakeRequest("DELETE", "/api/customers/" + customer.uuid).cookie(validCookie));
    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertTrue(json.get("success").asBoolean());
  }

  @Test
  public void testCustomerDELETEWithInvalidUUID() {
    UUID invalidUUID = UUID.randomUUID();

    String authToken = customer.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    Result result = route(fakeRequest("DELETE", "/api/customers/" + invalidUUID).cookie(validCookie));
    assertEquals(BAD_REQUEST, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertThat(json.get("error").asText(), is(containsString("Invalid Customer UUID:" + invalidUUID)));
  }

  @Test
  public void testCustomerMetricsWithInvalidParams() {
    String authToken = customer.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    ObjectNode params = Json.newObject();

    Result result = route(fakeRequest("POST", "/api/customers/" + customer.uuid + "/metrics").cookie(validCookie).bodyJson(params));

    assertEquals(BAD_REQUEST, result.status());
    assertThat(contentAsString(result), is(containsString("\"start\":[\"This field is required\"]")));
    assertThat(contentAsString(result), is(containsString("\"metrics\":[\"This field is required\"]")));
  }

  @Test
  public void testCustomerMetricsWithValidMetricsParams() {
    String authToken = customer.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    ObjectNode params = Json.newObject();
    params.set("metrics", Json.toJson(ImmutableList.of("metrics")));
    params.put("start", "1479281737000");

    ObjectNode response = Json.newObject();
    response.put("foo", "bar");

    when(mockMetricQueryHelper.query(anyList(), anyMap())).thenReturn(response);
    Result result = route(fakeRequest("POST", "/api/customers/" + customer.uuid + "/metrics").cookie(validCookie).bodyJson(params));
    assertEquals(OK, result.status());
    assertThat(contentAsString(result), allOf(notNullValue(), containsString("{\"foo\":\"bar\"}")));
  }

  @Test
  public void testCustomerMetricsWithInValidMetricsParam() {
    String authToken = customer.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    ObjectNode params = Json.newObject();
    params.set("metrics", Json.toJson(ImmutableList.of("metric1")));
    params.put("start", "1479281737");

    ObjectNode response = Json.newObject();
    response.put("error", "something went wrong");

    when(mockMetricQueryHelper.query(anyList(), anyMap())).thenReturn(response);
    Result result = route(fakeRequest("POST", "/api/customers/" + customer.uuid + "/metrics").cookie(validCookie).bodyJson(params));
    assertEquals(BAD_REQUEST, result.status());
    assertThat(contentAsString(result), allOf(notNullValue(), containsString("{\"error\":\"something went wrong\"}")));
  }

  @Test
  public void testCustomerMetricsExceptionThrown() {
    String authToken = customer.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    ObjectNode params = Json.newObject();
    params.set("metrics", Json.toJson(ImmutableList.of("metric")));
    params.put("start", "1479281737");

    ObjectNode response = Json.newObject();
    response.put("error", "something went wrong");

    when(mockMetricQueryHelper.query(anyList(), anyMap())).thenThrow(new RuntimeException("Weird Data provided"));
    Result result = route(fakeRequest("POST", "/api/customers/" + customer.uuid + "/metrics").cookie(validCookie).bodyJson(params));
    assertEquals(BAD_REQUEST, result.status());
    assertThat(contentAsString(result), allOf(notNullValue(), containsString("{\"error\":\"Weird Data provided\"}")));
  }

  @Test
  public void testCustomerMetricsWithMultipleUniverses() {
    String authToken = customer.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    Universe u1 = Universe.create("Foo-1", UUID.randomUUID(), customer.getCustomerId());
    u1 = Universe.saveDetails(u1.universeUUID, ApiUtils.mockUniverseUpdater("host-a"));
    Universe u2 = Universe.create("Foo-2", UUID.randomUUID(), customer.getCustomerId());
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

    route(fakeRequest("POST", "/api/customers/" + customer.uuid + "/metrics").cookie(validCookie).bodyJson(params));
    verify(mockMetricQueryHelper).query((List<String>) metricKeys.capture(),
                                        (Map<String, String>) queryParams.capture());

    assertThat(metricKeys.getValue(), is(notNullValue()));
    assertThat(queryParams.getValue(), is(notNullValue()));
    JsonNode filters = Json.parse(queryParams.getValue().get("filters").toString());
    String nodePrefix = filters.get("node_prefix").asText();
    assertThat(nodePrefix, allOf(notNullValue(), containsString("host-a")));
    assertThat(nodePrefix, allOf(notNullValue(), containsString("host-b")));
    String[] nodePrefixes = nodePrefix.split("\\|");
    assertEquals(nodePrefixes.length, 2);
  }

  @Test
  public void testCustomerMetricsWithNodePrefixParam() {
    String authToken = customer.createAuthToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken).build();
    Universe u1 = Universe.create("Foo-1", UUID.randomUUID(), customer.getCustomerId());
    u1 = Universe.saveDetails(u1.universeUUID, ApiUtils.mockUniverseUpdater("host-1"));
    Universe u2 = Universe.create("Foo-2", UUID.randomUUID(), customer.getCustomerId());
    u2 = Universe.saveDetails(u2.universeUUID, ApiUtils.mockUniverseUpdater("host-2"));
    customer.addUniverseUUID(u1.universeUUID);
    customer.addUniverseUUID(u2.universeUUID);
    customer.save();

    ObjectNode params = Json.newObject();
    params.set("metrics", Json.toJson(ImmutableList.of("metric")));
    params.put("start", "1479281737");
    params.put("nodePrefix", "host-1");

    ArgumentCaptor<ArrayList> metricKeys = ArgumentCaptor.forClass(ArrayList.class);
    ArgumentCaptor<Map> queryParams = ArgumentCaptor.forClass(Map.class);
    route(fakeRequest("POST", "/api/customers/" + customer.uuid + "/metrics").cookie(validCookie).bodyJson(params));
    verify(mockMetricQueryHelper).query((List<String>) metricKeys.capture(),
                                        (Map<String, String>) queryParams.capture());
    assertThat(queryParams.getValue(), is(notNullValue()));
    JsonNode filters = Json.parse(queryParams.getValue().get("filters").toString());
    String nodePrefix = filters.get("node_prefix").asText();
    assertThat(nodePrefix, allOf(notNullValue(), equalTo("host-1")));
  }
}
