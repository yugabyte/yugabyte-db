// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.OK;
import static play.mvc.Http.Status.UNAUTHORIZED;
import static play.test.Helpers.contentAsString;
import static play.test.Helpers.fakeRequest;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Users;
import java.io.IOException;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;

public class AuditControllerTest extends FakeDBApplication {
  String baseRoute = "/api/customers/%s/";

  private Customer customer1, customer2;
  private Users user1, user2;
  private String authToken1, authToken2;
  private UUID taskUUID1, taskUUID2, taskUUID3;
  private Audit audit1, audit2, audit3, audit4, audit5, audit6;

  @Before
  public void setUp() {
    customer1 = ModelFactory.testCustomer("tc1", "Test Customer 1");
    customer2 = ModelFactory.testCustomer("tc2", "Test Customer 2");
    user1 = ModelFactory.testUser(customer1, "tc1@test.com");
    user2 = ModelFactory.testUser(customer2, "tc2@test.com");
    authToken1 = user1.createAuthToken();
    authToken2 = user2.createAuthToken();
    ObjectNode params = Json.newObject();
    Audit.TargetType target = Audit.TargetType.Universe;
    String targetID = "Test TargetID";
    Audit.ActionType action = Audit.ActionType.Create;
    params.put("foo", "bar");
    audit1 = Audit.create(user1, "/test/call", "GET", null, null, null, params, null, null, null);
    taskUUID1 = UUID.randomUUID();
    taskUUID2 = UUID.randomUUID();
    taskUUID3 = UUID.randomUUID();
    audit2 =
        Audit.create(
            user1, "/test/call1", "DELETE", null, null, null, params, taskUUID1, null, null);
    audit3 =
        Audit.create(user2, "/test/call2", "PUT", null, null, null, params, taskUUID2, null, null);
    audit4 = Audit.create(user2, "/test/call4", "GET", null, null, null, params, null, null, null);
    audit5 =
        Audit.create(
            user1, "/test/call5", "PUT", target, null, action, params, taskUUID3, null, null);
    audit6 =
        Audit.create(
            user2, "/test/call6", "POST", target, targetID, action, params, null, null, null);
  }

  @Test
  public void testGetAuditListByUser() {
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken1).build();
    String route = "/api/customers/%s/users/%s/audit_trail";
    Result result =
        route(
            fakeRequest("GET", String.format(route, customer1.getUuid(), user1.getUuid()))
                .cookie(validCookie));
    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(json.size(), 3);
  }

  @Test
  public void testGetListFailureIncorrectCustomer() {
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken1).build();
    String route = "/api/customers/%s/users/%s/audit_trail";
    Result result =
        route(
            fakeRequest("GET", String.format(route, customer2.getUuid(), user1.getUuid()))
                .cookie(validCookie));
    assertEquals(UNAUTHORIZED, result.status());
  }

  @Test
  public void testGetTaskInfo() throws IOException {
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken2).build();
    String route = "/api/customers/%s/tasks/%s/audit_info";
    Result result =
        route(
            fakeRequest("GET", String.format(route, customer2.getUuid(), taskUUID2))
                .cookie(validCookie));
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());
    assertTrue(json.path("auditID").asLong() == audit3.getId());
  }

  @Test
  public void testGetTaskInfoInvalidCustomer() throws IOException {
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken2).build();
    String route = "/api/customers/%s/tasks/%s/audit_info";
    Result result =
        assertPlatformException(
            () ->
                route(
                    fakeRequest("GET", String.format(route, customer2.getUuid(), taskUUID1))
                        .cookie(validCookie)));
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(BAD_REQUEST, result.status());
  }

  @Test
  public void testUserFromTask() throws IOException {
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken2).build();
    String route = "/api/customers/%s/tasks/%s/audit_user";
    Result result =
        route(
            fakeRequest("GET", String.format(route, customer2.getUuid(), taskUUID2))
                .cookie(validCookie));
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(OK, result.status());
    assertTrue(json.path("uuid").asText().equals(user2.getUuid().toString()));
  }

  @Test
  public void testGetUserFromTaskInvalidCustomer() throws IOException {
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken2).build();
    String route = "/api/customers/%s/tasks/%s/audit_user";
    Result result =
        assertPlatformException(
            () ->
                route(
                    fakeRequest("GET", String.format(route, customer2.getUuid(), taskUUID1))
                        .cookie(validCookie)));
    JsonNode json = Json.parse(contentAsString(result));
    assertEquals(BAD_REQUEST, result.status());
  }
}
