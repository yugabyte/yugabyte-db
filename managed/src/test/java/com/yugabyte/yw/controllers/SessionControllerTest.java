// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertBadRequest;
import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static com.yugabyte.yw.common.AssertHelper.assertUnauthorized;
import static org.hamcrest.CoreMatchers.*;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertThat;
import static org.mockito.Mockito.mock;
import static play.inject.Bindings.bind;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.OK;
import static play.mvc.Http.Status.UNAUTHORIZED;
import static play.test.Helpers.contentAsString;
import static play.test.Helpers.fakeRequest;
import static play.test.Helpers.route;

import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.commissioner.CallHome;
import com.yugabyte.yw.commissioner.HealthChecker;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.scheduler.Scheduler;
import org.junit.After;
import org.junit.Test;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;

import play.Application;
import play.inject.guice.GuiceApplicationBuilder;
import play.libs.Json;
import play.mvc.Result;
import play.test.Helpers;

import java.util.Map;

public class SessionControllerTest {

  HealthChecker mockHealthChecker;
  Scheduler mockScheduler;
  CallHome mockCallHome;

  Application app;

  private void startApp(boolean isMultiTenant) {
    mockHealthChecker = mock(HealthChecker.class);
    mockScheduler = mock(Scheduler.class);
    mockCallHome = mock(CallHome.class);
    app = new GuiceApplicationBuilder()
        .configure((Map) Helpers.inMemoryDatabase())
        .configure(ImmutableMap.of("yb.multiTenant", isMultiTenant))
        .overrides(bind(Scheduler.class).toInstance(mockScheduler))
        .overrides(bind(HealthChecker.class).toInstance(mockHealthChecker))
        .overrides(bind(CallHome.class).toInstance(mockCallHome))
        .build();
    Helpers.start(app);
  }

  @After
  public void tearDown() {
    Helpers.stop(app);
  }

  @Test
  public void testValidLogin() {
    startApp(false);
    ModelFactory.testCustomer("foo@bar.com");
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
    startApp(false);
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
    startApp(false);
    ObjectNode loginJson = Json.newObject();
    loginJson.put("email", "foo@bar.com");
    Result result = route(fakeRequest("POST", "/api/login").bodyJson(loginJson));
    JsonNode json = Json.parse(contentAsString(result));

    assertEquals(BAD_REQUEST, result.status());
    assertThat(json.get("error").toString(),
               allOf(notNullValue(), containsString("{\"password\":[\"This field is required\"]}")));
  }

  @Test
  public void testInsecureLoginValid() {
    startApp(false);
    ModelFactory.testCustomer("foo@bar.com");
    ConfigHelper configHelper = new ConfigHelper();
    configHelper.loadConfigToDB(ConfigHelper.ConfigType.Security,
        ImmutableMap.of("level", "insecure"));

    Result result = route(fakeRequest("GET", "/api/insecure_login"));
    JsonNode json = Json.parse(contentAsString(result));

    assertEquals(OK, result.status());
    assertNotNull(json.get("apiToken"));
    assertNotNull(json.get("customerUUID"));
  }

  @Test
  public void testInsecureLoginInvalid() {
    startApp(false);
    ModelFactory.testCustomer("foo@bar.com");
    ConfigHelper configHelper = new ConfigHelper();

    Result result = route(fakeRequest("GET", "/api/insecure_login"));
    JsonNode json = Json.parse(contentAsString(result));

    assertUnauthorized(result, "Insecure login unavailable.");
  }

  @Test
  public void testRegisterCustomer() {
    startApp(true);
    ObjectNode registerJson = Json.newObject();
    registerJson.put("code", "fb");
    registerJson.put("email", "foo2@bar.com");
    registerJson.put("password", "password");
    registerJson.put("name", "Foo");

    Result result = route(fakeRequest("POST", "/api/register").bodyJson(registerJson));
    JsonNode json = Json.parse(contentAsString(result));

    assertEquals(OK, result.status());
    assertNotNull(json.get("authToken"));

    ObjectNode loginJson = Json.newObject();
    registerJson.put("code", "fb");
    loginJson.put("email", "foo2@bar.com");
    loginJson.put("password", "password");
    result = route(fakeRequest("POST", "/api/login").bodyJson(loginJson));
    json = Json.parse(contentAsString(result));

    assertEquals(OK, result.status());
    assertNotNull(json.get("authToken"));
  }

  @Test
  public void testRegisterCustomerWithLongerCode() {
    startApp(true);
    ObjectNode registerJson = Json.newObject();
    registerJson.put("code", "abcabcabcabcabcabc");
    registerJson.put("email", "foo2@bar.com");
    registerJson.put("password", "password");
    registerJson.put("name", "Foo");

    Result result = route(fakeRequest("POST", "/api/register").bodyJson(registerJson));
    JsonNode json = Json.parse(contentAsString(result));

    assertEquals(BAD_REQUEST, result.status());
    assertValue(json, "error", "{\"code\":[\"Maximum length is 15\"]}");
  }

  @Test
  public void testRegisterCustomerExceedingLimit() {
    startApp(false);
    ModelFactory.testCustomer("foo@bar.com");
    ObjectNode registerJson = Json.newObject();
    registerJson.put("code", "fb");
    registerJson.put("email", "foo2@bar.com");
    registerJson.put("password", "password");
    registerJson.put("name", "Foo");
    Result result = route(fakeRequest("POST", "/api/register").bodyJson(registerJson));
    assertBadRequest(result, "Cannot register multiple accounts in Single tenancy.");
  }

  @Test
  public void testRegisterCustomerWithoutEmail() {
    startApp(false);
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
    startApp(false);
    ModelFactory.testCustomer("foo@bar.com");
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

  @Test
  public void testAuthTokenExpiry() {
    startApp(false);
    ModelFactory.testCustomer("foo@bar.com");
    ObjectNode loginJson = Json.newObject();
    loginJson.put("email", "Foo@bar.com");
    loginJson.put("password", "password");
    Result result = route(fakeRequest("POST", "/api/login").bodyJson(loginJson));
    JsonNode json = Json.parse(contentAsString(result));
    String authToken1 = json.get("authToken").asText();
    loginJson.put("email", "Foo@bar.com");
    loginJson.put("password", "password");
    result = route(fakeRequest("POST", "/api/login").bodyJson(loginJson));
    json = Json.parse(contentAsString(result));
    String authToken2 = json.get("authToken").asText();
    assertEquals(authToken1, authToken2);
  }

  @Test
  public void testApiTokenUpsert() {
    startApp(false);
    ModelFactory.testCustomer("foo@bar.com");
    ObjectNode loginJson = Json.newObject();
    loginJson.put("email", "Foo@bar.com");
    loginJson.put("password", "password");
    Result result = route(fakeRequest("POST", "/api/login").bodyJson(loginJson));
    JsonNode json = Json.parse(contentAsString(result));
    String authToken = json.get("authToken").asText();
    String custUuid = json.get("customerUUID").asText();
    ObjectNode apiTokenJson = Json.newObject();
    apiTokenJson.put("authToken", authToken);
    result = route(fakeRequest("PUT", "/api/customers/" + custUuid + "/api_token").header("X-AUTH-TOKEN", authToken));
    json = Json.parse(contentAsString(result));

    assertEquals(OK, result.status());
    assertNotNull(json.get("apiToken"));
  }

  @Test
  public void testApiTokenUpdate() {
    startApp(false);
    ModelFactory.testCustomer("foo@bar.com");
    ObjectNode loginJson = Json.newObject();
    loginJson.put("email", "Foo@bar.com");
    loginJson.put("password", "password");
    Result result = route(fakeRequest("POST", "/api/login").bodyJson(loginJson));
    JsonNode json = Json.parse(contentAsString(result));
    String authToken = json.get("authToken").asText();
    String custUuid = json.get("customerUUID").asText();
    ObjectNode apiTokenJson = Json.newObject();
    apiTokenJson.put("authToken", authToken);
    result = route(fakeRequest("PUT", "/api/customers/" + custUuid + "/api_token").header("X-AUTH-TOKEN", authToken));
    json = Json.parse(contentAsString(result));
    String apiToken1 = json.get("apiToken").asText();
    apiTokenJson.put("authToken", authToken);
    result = route(fakeRequest("PUT", "/api/customers/" + custUuid + "/api_token").header("X-AUTH-TOKEN", authToken));
    json = Json.parse(contentAsString(result));
    String apiToken2 = json.get("apiToken").asText();
    assertNotEquals(apiToken1, apiToken2);
  }

  @Test
  public void testCustomerCount() {
    startApp(false);
    Result result = route(fakeRequest("GET", "/api/customer_count"));
    JsonNode json = Json.parse(contentAsString(result));
    assertOk(result);
    assertValue(json, "count", "0");
    ModelFactory.testCustomer("foo@bar.com");
    result = route(fakeRequest("GET", "/api/customer_count"));
    json = Json.parse(contentAsString(result));
    assertOk(result);
    assertValue(json, "count", "1");
  }

  @Test
  public void testAppVersion() {
    startApp(false);
    Result result = route(fakeRequest("GET", "/api/app_version"));
    JsonNode json = Json.parse(contentAsString(result));
    assertOk(result);
    assertEquals(json, Json.newObject());
    ConfigHelper configHelper = new ConfigHelper();
    configHelper.loadConfigToDB(ConfigHelper.ConfigType.SoftwareVersion,
        ImmutableMap.of("version", "0.0.1"));
    result = route(fakeRequest("GET", "/api/app_version"));
    json = Json.parse(contentAsString(result));
    assertOk(result);
    assertValue(json, "version", "0.0.1");
  }
}
