// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.api.controllers;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.common.ApiHelper;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.CustomerTask;

import com.yugabyte.yw.models.Universe;
import org.junit.Before;
import org.junit.Test;
import org.mockito.Matchers;
import play.Application;
import play.inject.guice.GuiceApplicationBuilder;
import play.libs.Json;
import play.mvc.Result;
import play.test.Helpers;

import java.util.Map;
import java.util.UUID;

import static org.hamcrest.CoreMatchers.*;
import static org.hamcrest.CoreMatchers.equalTo;
import static org.junit.Assert.*;
import static org.junit.Assert.assertEquals;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;
import static play.inject.Bindings.bind;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;
import static play.test.Helpers.fakeRequest;
import static play.test.Helpers.route;

public class CustomerTaskControllerTest extends FakeDBApplication {
  private Customer customer;
  private Universe universe;
  private ApiHelper mockApiHelper;
  private Commissioner mockCommissioner;

  @Override
  protected Application provideApplication() {
    mockApiHelper = mock(ApiHelper.class);
    mockCommissioner = mock(Commissioner.class);
    return new GuiceApplicationBuilder()
      .configure((Map) Helpers.inMemoryDatabase())
      .overrides(bind(ApiHelper.class).toInstance(mockApiHelper))
      .overrides(bind(Commissioner.class).toInstance(mockCommissioner))
      .build();
  }

  @Before
  public void setUp() {
    customer = Customer.create("Valid Customer", "foo@bar.com", "password");
    universe = Universe.create("Test Universe", customer.customerId);
  }

  @Test
  public void testTaskHistoryEmptyList() {
    String authToken = customer.createAuthToken();
    Result result = route(fakeRequest("GET", "/api/customers/" + customer.uuid + "/tasks")
                            .header("X-AUTH-TOKEN", authToken));

    assertEquals(OK, result.status());

    JsonNode json = Json.parse(contentAsString(result));
    assertTrue(json.isObject());
    assertEquals(0, json.size());
  }

  @Test
  public void testTaskHistoryList() {
    String authToken = customer.createAuthToken();
    UUID taskUUID = UUID.randomUUID();
    CustomerTask.create(customer, universe, taskUUID, CustomerTask.TargetType.Universe, CustomerTask.TaskType.Create, "Foo");

    ObjectNode getResponseJson = Json.newObject();
    getResponseJson.put("status", "Success");
    getResponseJson.put("percent", "50");
    when(mockApiHelper.getRequest(Matchers.anyString(), Matchers.anyMap())).thenReturn(getResponseJson);

    Result result = route(fakeRequest("GET", "/api/customers/" + customer.uuid + "/tasks")
                            .header("X-AUTH-TOKEN", authToken));

    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertTrue(json.isObject());

    JsonNode universeTasks = json.get(universe.universeUUID.toString());
    assertTrue(universeTasks.isArray());
    assertEquals(1, universeTasks.size());
    assertThat(universeTasks.get(0).get("id").asText(), allOf(notNullValue(), equalTo(taskUUID.toString())));
    assertThat(universeTasks.get(0).get("title").asText(), allOf(notNullValue(), equalTo("Creating Universe : Foo")));
    assertThat(universeTasks.get(0).get("percentComplete").asInt(), allOf(notNullValue(), equalTo(50)));
    assertThat(universeTasks.get(0).get("success").asBoolean(), allOf(notNullValue(), equalTo(true)));
  }

  @Test
  public void testTaskHistoryUniverseList() {
    String authToken = customer.createAuthToken();
    Universe universe1 = Universe.create("Universe 2", customer.customerId);
    UUID taskUUID = UUID.randomUUID();
    CustomerTask.create(customer, universe1, UUID.randomUUID(), CustomerTask.TargetType.Universe, CustomerTask.TaskType.Create, "Foo");
    CustomerTask.create(customer, universe, taskUUID, CustomerTask.TargetType.Universe, CustomerTask.TaskType.Create, "Bar");

    ObjectNode getResponseJson = Json.newObject();
    getResponseJson.put("status", "Success");
    getResponseJson.put("percent", "50");
    when(mockApiHelper.getRequest(Matchers.anyString(), Matchers.anyMap())).thenReturn(getResponseJson);

    Result result = route(fakeRequest("GET", "/api/customers/" + customer.uuid + "/universes/" + universe.universeUUID + "/tasks")
                            .header("X-AUTH-TOKEN", authToken));

    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertTrue(json.isObject());
    JsonNode universeTasks = json.get(universe.universeUUID.toString());
    assertTrue(universeTasks.isArray());
    assertEquals(1, universeTasks.size());
    assertThat(universeTasks.get(0).get("id").asText(), allOf(notNullValue(), equalTo(taskUUID.toString())));
    assertThat(universeTasks.get(0).get("title").asText(), allOf(notNullValue(), equalTo("Creating Universe : Bar")));
    assertThat(universeTasks.get(0).get("percentComplete").asInt(), allOf(notNullValue(), equalTo(50)));
    assertThat(universeTasks.get(0).get("success").asBoolean(), allOf(notNullValue(), equalTo(true)));
  }

  @Test
  public void testTaskHistoryProgressCompletes() {
    String authToken = customer.createAuthToken();
    UUID taskUUID = UUID.randomUUID();
    CustomerTask.create(customer, universe, taskUUID, CustomerTask.TargetType.Universe, CustomerTask.TaskType.Create, "Foo");

    ObjectNode getResponseJson = Json.newObject();
    getResponseJson.put("status", "Success");
    getResponseJson.put("percent", "100");
    when(mockApiHelper.getRequest(Matchers.anyString(), Matchers.anyMap())).thenReturn(getResponseJson);

    Result result = route(fakeRequest("GET", "/api/customers/" + customer.uuid + "/tasks")
                            .header("X-AUTH-TOKEN", authToken));

    CustomerTask ct = CustomerTask.find.where().eq("task_uuid", taskUUID.toString()).findUnique();

    assertEquals(OK, result.status());
    assertThat(contentAsString(result), allOf(notNullValue(), containsString("Created Universe : Foo")));
    assertTrue(ct.getCreateTime().before(ct.getCompletionTime()));
  }

  @Test
  public void testTaskStatusWithValidUUID() {
    String authToken = customer.createAuthToken();
    UUID taskUUID = UUID.randomUUID();
    CustomerTask.create(customer, universe, taskUUID, CustomerTask.TargetType.Universe, CustomerTask.TaskType.Create, "Foo");

    ObjectNode getResponseJson = Json.newObject();
    getResponseJson.put("status", "Success");
    getResponseJson.put("percent", "100");
    when(mockCommissioner.getStatus(taskUUID)).thenReturn(getResponseJson);

    Result result = route(fakeRequest("GET", "/api/customers/" + customer.uuid + "/tasks/" + taskUUID)
                            .header("X-AUTH-TOKEN", authToken));

    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertThat(json.get("status").asText(), allOf(notNullValue(), equalTo("Success")));
    assertThat(json.get("percent").asInt(), allOf(notNullValue(), equalTo(100)));
  }

  @Test
  public void testTaskStatusWithInvalidTaskUUID() {
    String authToken = customer.createAuthToken();
    UUID taskUUID = UUID.randomUUID();

    Result result = route(fakeRequest("GET", "/api/customers/" + customer.uuid + "/tasks/" + taskUUID)
                            .header("X-AUTH-TOKEN", authToken));

    assertEquals(BAD_REQUEST, result.status());

    JsonNode json = Json.parse(contentAsString(result));
    assertThat(json.get("error").asText(), allOf(notNullValue(), equalTo("Invalid Customer Task UUID: " + taskUUID)));
  }

  @Test
  public void testTaskStatusWithInvalidCustomerUUID() {
    String authToken = customer.createAuthToken();
    UUID taskUUID = UUID.randomUUID();
    UUID customerUUID = UUID.randomUUID();

    Result result = route(fakeRequest("GET", "/api/customers/" + customerUUID + "/tasks/" + taskUUID)
                            .header("X-AUTH-TOKEN", authToken));

    assertEquals(BAD_REQUEST, result.status());

    JsonNode json = Json.parse(contentAsString(result));
    assertThat(json.get("error").asText(), allOf(notNullValue(), equalTo("Invalid Customer UUID: " + customerUUID)));
  }
}
