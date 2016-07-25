// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.api.controllers;

import static org.hamcrest.CoreMatchers.allOf;
import static org.hamcrest.CoreMatchers.containsString;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertThat;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.OK;
import static play.mvc.Http.Status.UNAUTHORIZED;
import static play.test.Helpers.contentAsString;
import static play.test.Helpers.fakeRequest;
import static play.test.Helpers.route;

import org.junit.Before;
import org.junit.Test;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.models.Customer;

import play.libs.Json;
import play.mvc.Result;

public class SessionControllerTest extends FakeDBApplication {

  @Before
  public void setUp() {
    Customer customer = Customer.create("Valid Customer", "foo@bar.com", "password");
    customer.save();
  }

  @Test
  public void testValidLogin() {
    ObjectNode loginJson = Json.newObject();
    loginJson.put("email", "Foo@bar.com");
    loginJson.put("password", "password");
    Result result = route(fakeRequest("POST", "/api/login").bodyJson(loginJson));
    JsonNode json = Json.parse(contentAsString(result));

    assertEquals(OK, result.status());
    assertNotNull(json.get("authToken"));
  }

  @Test
  public void testLoginWithInvalidPassword() {
    ObjectNode loginJson = Json.newObject();
    loginJson.put("email", "foo@bar.com");
    loginJson.put("password", "password1");
    Result result = route(fakeRequest("POST", "/api/login").bodyJson(loginJson));
	  JsonNode json = Json.parse(contentAsString(result));

    assertEquals(UNAUTHORIZED, result.status());
	  assertThat(json.get("error").toString(),
	             allOf(notNullValue(), containsString("Invalid Customer Credentials")));
  }

  @Test
  public void testLoginWithNullPassword() {
    ObjectNode loginJson = Json.newObject();
    loginJson.put("email", "foo@bar.com");
    Result result = route(fakeRequest("POST", "/api/login").bodyJson(loginJson));
	  JsonNode json = Json.parse(contentAsString(result));

    assertEquals(BAD_REQUEST, result.status());
	  assertThat(json.get("error").toString(),
	             allOf(notNullValue(), containsString("{\"password\":[\"This field is required\"]}")));
  }

  @Test
  public void testRegisterCustomer() {
    ObjectNode registerJson = Json.newObject();
    registerJson.put("email", "foo2@bar.com");
    registerJson.put("password", "password");
    registerJson.put("name", "Foo");

    Result result = route(fakeRequest("POST", "/api/register").bodyJson(registerJson));
    JsonNode json = Json.parse(contentAsString(result));

    assertEquals(OK, result.status());
    assertNotNull(json.get("authToken"));

    ObjectNode loginJson = Json.newObject();
    loginJson.put("email", "foo2@bar.com");
    loginJson.put("password", "password");
    result = route(fakeRequest("POST", "/api/login").bodyJson(loginJson));
    json = Json.parse(contentAsString(result));

    assertEquals(OK, result.status());
    assertNotNull(json.get("authToken"));
  }

  @Test
  public void testRegisterCustomerWithoutEmail() {
    ObjectNode registerJson = Json.newObject();
    registerJson.put("email", "foo@bar.com");
    Result result = route(fakeRequest("POST", "/api/login").bodyJson(registerJson));

    JsonNode json = Json.parse(contentAsString(result));

    assertEquals(BAD_REQUEST, result.status());
	  assertThat(json.get("error").toString(),
	             allOf(notNullValue(),
	                   containsString("{\"password\":[\"This field is required\"]}")));
	}

  @Test
  public void testLogout() {
    ObjectNode loginJson = Json.newObject();
    loginJson.put("email", "Foo@bar.com");
    loginJson.put("password", "password");
    Result result = route(fakeRequest("POST", "/api/login").bodyJson(loginJson));
    JsonNode json = Json.parse(contentAsString(result));

    assertEquals(OK, result.status());
    String authToken = json.get("authToken").asText();
    result = route(fakeRequest("GET", "/api/logout").header("X-AUTH-TOKEN", authToken));
	  assertEquals(OK, result.status());
  }
}
