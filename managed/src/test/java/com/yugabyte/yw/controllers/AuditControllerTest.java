// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.ObjectReader;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableList;
import com.yugabyte.yw.commissioner.Commissioner;
import com.yugabyte.yw.commissioner.Common;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.CloudQueryHelper;
import com.yugabyte.yw.common.FakeApiHelper;
import com.yugabyte.yw.common.CallHomeManager.CollectionLevel;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.metrics.MetricQueryHelper;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Users;

import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.CustomerConfig;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import org.junit.Before;
import org.junit.Test;
import org.mindrot.jbcrypt.BCrypt;
import org.mockito.ArgumentCaptor;
import play.Application;
import play.inject.guice.GuiceApplicationBuilder;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;
import play.test.Helpers;
import play.test.WithApplication;

import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.io.*;

import static com.yugabyte.yw.common.AssertHelper.assertBadRequest;
import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static com.yugabyte.yw.common.AssertHelper.assertUnauthorized;
import static com.yugabyte.yw.common.ModelFactory.createUniverse;
import static com.yugabyte.yw.models.Users.Role;
import static org.hamcrest.CoreMatchers.*;
import static org.hamcrest.core.StringContains.containsString;
import static org.mockito.Matchers.anyList;
import static org.mockito.Matchers.anyMap;
import static org.mockito.Mockito.*;
import static play.inject.Bindings.bind;
import static play.test.Helpers.*;
import static org.junit.Assert.*;
import static play.mvc.Http.Status.OK;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.FORBIDDEN;
import static play.test.Helpers.fakeRequest;

public class AuditControllerTest extends FakeDBApplication {
  String baseRoute = "/api/customers/%s/";

  private Customer customer1, customer2;
  private Users user1, user2;
  private String authToken1, authToken2;
  private UUID taskUUID1, taskUUID2;
  private Audit audit1, audit2, audit3, audit4;

  @Before
  public void setUp() {
    customer1 = ModelFactory.testCustomer("tc1", "Test Customer 1");
    customer2 = ModelFactory.testCustomer("tc2", "Test Customer 2");
    user1 = ModelFactory.testUser(customer1, "tc1@test.com");
    user2 = ModelFactory.testUser(customer2, "tc2@test.com");
    authToken1 = user1.createAuthToken();
    authToken2 = user2.createAuthToken();
    ObjectNode params = Json.newObject();
    params.put("foo", "bar");
    audit1 = Audit.create(user1.uuid, customer1.uuid, "/test/call", "GET", params, null, null);
    taskUUID1 = UUID.randomUUID();
    taskUUID2 = UUID.randomUUID();
    audit2 =
        Audit.create(user1.uuid, customer1.uuid, "/test/call1", "DELETE", params, taskUUID1, null);
    audit3 =
        Audit.create(user2.uuid, customer2.uuid, "/test/call2", "PUT", params, taskUUID2, null);
    audit4 = Audit.create(user2.uuid, customer2.uuid, "/test/call4", "GET", params, null, null);
  }

  @Test
  public void testGetAuditListByUser() {
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken1).build();
    String route = "/api/customers/%s/users/%s/audit_trail";
    Result result =
        route(
            fakeRequest("GET", String.format(route, customer1.uuid, user1.uuid))
                .cookie(validCookie));
    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(json.size(), 2);
  }

  @Test
  public void testGetListFailureIncorrectCustomer() {
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken1).build();
    String route = "/api/customers/%s/users/%s/audit_trail";
    Result result =
        route(
            fakeRequest("GET", String.format(route, customer2.uuid, user1.uuid))
                .cookie(validCookie));
    assertEquals(FORBIDDEN, result.status());
  }

  @Test
  public void testGetTaskInfo() throws IOException {
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken2).build();
    String route = "/api/customers/%s/tasks/%s/audit_info";
    Result result =
        route(
            fakeRequest("GET", String.format(route, customer2.uuid, taskUUID2))
                .cookie(validCookie));
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());
    assertTrue(json.path("auditID").asLong() == audit3.getAuditID());
  }

  @Test
  public void testGetTaskInfoInvalidCustomer() throws IOException {
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken2).build();
    String route = "/api/customers/%s/tasks/%s/audit_info";
    Result result =
        route(
            fakeRequest("GET", String.format(route, customer2.uuid, taskUUID1))
                .cookie(validCookie));
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(BAD_REQUEST, result.status());
  }

  @Test
  public void testUserFromTask() throws IOException {
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken2).build();
    String route = "/api/customers/%s/tasks/%s/audit_user";
    Result result =
        route(
            fakeRequest("GET", String.format(route, customer2.uuid, taskUUID2))
                .cookie(validCookie));
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());
    assertTrue(json.path("uuid").asText().equals(user2.uuid.toString()));
  }

  @Test
  public void testGetUserFromTaskInvalidCustomer() throws IOException {
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken2).build();
    String route = "/api/customers/%s/tasks/%s/audit_user";
    Result result =
        route(
            fakeRequest("GET", String.format(route, customer2.uuid, taskUUID1))
                .cookie(validCookie));
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(BAD_REQUEST, result.status());
  }
}
