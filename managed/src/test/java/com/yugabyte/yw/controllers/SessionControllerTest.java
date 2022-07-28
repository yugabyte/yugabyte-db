// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.ApiUtils.getTestUserIntent;
import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static com.yugabyte.yw.common.AssertHelper.assertBadRequest;
import static com.yugabyte.yw.common.AssertHelper.assertConflict;
import static com.yugabyte.yw.common.AssertHelper.assertForbidden;
import static com.yugabyte.yw.common.AssertHelper.assertInternalServerError;
import static com.yugabyte.yw.common.AssertHelper.assertOk;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static com.yugabyte.yw.common.AssertHelper.assertUnauthorized;
import static com.yugabyte.yw.common.AssertHelper.assertValue;
import static com.yugabyte.yw.common.FakeApiHelper.routeWithYWErrHandler;
import static com.yugabyte.yw.common.TestHelper.testDatabase;
import static com.yugabyte.yw.models.Users.Role;
import static com.yugabyte.yw.models.Users.UserType;
import static org.hamcrest.CoreMatchers.allOf;
import static org.hamcrest.CoreMatchers.containsString;
import static org.hamcrest.CoreMatchers.notNullValue;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertThat;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.when;
import static play.inject.Bindings.bind;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.OK;
import static play.mvc.Http.Status.UNAUTHORIZED;
import static play.test.Helpers.contentAsString;
import static play.test.Helpers.fakeRequest;
import static play.test.Helpers.route;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.commissioner.CallHome;
import com.yugabyte.yw.commissioner.HealthChecker;
import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.ConfigHelper;
import com.yugabyte.yw.common.LdapUtil;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.alerts.AlertConfigurationWriter;
import com.yugabyte.yw.common.alerts.AlertDestinationService;
import com.yugabyte.yw.common.alerts.QueryAlerts;
import com.yugabyte.yw.common.config.impl.SettableRuntimeConfigFactory;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams;
import com.yugabyte.yw.models.AvailabilityZone;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.InstanceType;
import com.yugabyte.yw.models.Provider;
import com.yugabyte.yw.models.Region;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.helpers.NodeDetails;
import com.yugabyte.yw.scheduler.Scheduler;
import java.util.UUID;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.TimeoutException;
import kamon.instrumentation.play.GuiceModule;
import org.apache.directory.api.ldap.model.exception.LdapException;
import org.junit.After;
import org.junit.Test;
import org.pac4j.play.CallbackController;
import org.pac4j.play.store.PlayCacheSessionStore;
import org.pac4j.play.store.PlaySessionStore;
import play.Application;
import play.inject.guice.GuiceApplicationBuilder;
import play.libs.Json;
import play.modules.swagger.SwaggerModule;
import play.mvc.Http;
import play.mvc.Result;
import play.test.Helpers;

public class SessionControllerTest {

  private AlertDestinationService alertDestinationService;

  private Application app;

  private SettableRuntimeConfigFactory settableRuntimeConfigFactory;

  private LdapUtil ldapUtil;

  private void startApp(boolean isMultiTenant) {
    HealthChecker mockHealthChecker = mock(HealthChecker.class);
    Scheduler mockScheduler = mock(Scheduler.class);
    CallHome mockCallHome = mock(CallHome.class);
    CallbackController mockCallbackController = mock(CallbackController.class);
    PlayCacheSessionStore mockSessionStore = mock(PlayCacheSessionStore.class);
    QueryAlerts mockQueryAlerts = mock(QueryAlerts.class);
    AlertConfigurationWriter mockAlertConfigurationWriter = mock(AlertConfigurationWriter.class);
    ldapUtil = mock(LdapUtil.class);
    app =
        new GuiceApplicationBuilder()
            .disable(GuiceModule.class)
            .configure(testDatabase())
            .configure(ImmutableMap.of("yb.multiTenant", isMultiTenant))
            .overrides(bind(Scheduler.class).toInstance(mockScheduler))
            .overrides(bind(HealthChecker.class).toInstance(mockHealthChecker))
            .overrides(bind(CallHome.class).toInstance(mockCallHome))
            .overrides(bind(CallbackController.class).toInstance(mockCallbackController))
            .overrides(bind(PlaySessionStore.class).toInstance(mockSessionStore))
            .overrides(bind(QueryAlerts.class).toInstance(mockQueryAlerts))
            .overrides(
                bind(AlertConfigurationWriter.class).toInstance(mockAlertConfigurationWriter))
            .overrides(bind(LdapUtil.class).toInstance(ldapUtil))
            .build();
    Helpers.start(app);

    alertDestinationService = app.injector().instanceOf(AlertDestinationService.class);
    settableRuntimeConfigFactory = app.injector().instanceOf(SettableRuntimeConfigFactory.class);
  }

  @After
  public void tearDown() {
    Helpers.stop(app);
  }

  @Test
  public void testValidLogin() {
    startApp(false);
    Customer customer = ModelFactory.testCustomer();
    ModelFactory.testUser(customer);
    ObjectNode loginJson = Json.newObject();
    loginJson.put("email", "test@customer.com");
    loginJson.put("password", "password");
    Result result = route(fakeRequest("POST", "/api/login").bodyJson(loginJson));
    JsonNode json = Json.parse(contentAsString(result));

    assertEquals(OK, result.status());
    assertNotNull(json.get("authToken"));
    assertAuditEntry(1, customer.uuid);
  }

  @Test
  public void testLoginWithInvalidPassword()
      throws InterruptedException, ExecutionException, TimeoutException {
    startApp(false);
    Customer customer = ModelFactory.testCustomer();
    ModelFactory.testUser(customer);
    ObjectNode loginJson = Json.newObject();
    loginJson.put("email", "test@customer.com");
    loginJson.put("password", "password1");
    Result result =
        routeWithYWErrHandler(fakeRequest("POST", "/api/login").bodyJson(loginJson), app);
    JsonNode json = Json.parse(contentAsString(result));

    assertEquals(UNAUTHORIZED, result.status());
    assertThat(
        json.get("error").toString(),
        allOf(notNullValue(), containsString("Invalid User Credentials")));
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testLoginWithNullPassword() {
    startApp(false);
    Customer customer = ModelFactory.testCustomer();
    ModelFactory.testUser(customer);
    ObjectNode loginJson = Json.newObject();
    loginJson.put("email", "test@customer.com");
    Result result =
        assertPlatformException(() -> route(fakeRequest("POST", "/api/login").bodyJson(loginJson)));
    JsonNode json = Json.parse(contentAsString(result));

    assertEquals(BAD_REQUEST, result.status());
    assertThat(
        json.get("error").toString(),
        allOf(notNullValue(), containsString("{\"password\":[\"This field is required\"]}")));
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testValidLoginWithLdap() throws LdapException {
    startApp(false);
    Customer customer = ModelFactory.testCustomer();
    Users user = ModelFactory.testUser(customer);
    user.setUserType(UserType.ldap);
    user.save();
    ObjectNode loginJson = Json.newObject();
    loginJson.put("email", "test@customer.com");
    loginJson.put("password", "password");
    settableRuntimeConfigFactory.globalRuntimeConf().setValue("yb.security.ldap.use_ldap", "true");
    when(ldapUtil.loginWithLdap(any())).thenReturn(user);
    Result result = route(fakeRequest("POST", "/api/login").bodyJson(loginJson));
    JsonNode json = Json.parse(contentAsString(result));

    assertEquals(OK, result.status());
    assertNotNull(json.get("authToken"));
    assertAuditEntry(1, customer.uuid);

    settableRuntimeConfigFactory.globalRuntimeConf().setValue("yb.security.ldap.use_ldap", "false");
  }

  @Test
  public void testInvalidLoginWithLdap() throws LdapException {
    startApp(false);
    Customer customer = ModelFactory.testCustomer();
    Users user = ModelFactory.testUser(customer);
    user.setUserType(UserType.ldap);
    user.save();
    ObjectNode loginJson = Json.newObject();
    loginJson.put("email", "test@customer.com");
    loginJson.put("password", "password1");
    settableRuntimeConfigFactory.globalRuntimeConf().setValue("yb.security.ldap.use_ldap", "true");
    when(ldapUtil.loginWithLdap(any())).thenReturn(null);
    Result result =
        assertPlatformException(() -> route(fakeRequest("POST", "/api/login").bodyJson(loginJson)));
    JsonNode json = Json.parse(contentAsString(result));

    assertEquals(UNAUTHORIZED, result.status());
    assertThat(
        json.get("error").toString(),
        allOf(notNullValue(), containsString("Invalid User Credentials")));
    assertAuditEntry(0, customer.uuid);

    settableRuntimeConfigFactory.globalRuntimeConf().setValue("yb.security.ldap.use_ldap", "false");
  }

  @Test
  public void testLdapUserWithoutLdapConfig() throws LdapException {
    startApp(false);
    Customer customer = ModelFactory.testCustomer();
    Users user = ModelFactory.testUser(customer);
    user.setUserType(UserType.ldap);
    user.save();
    ObjectNode loginJson = Json.newObject();
    loginJson.put("email", "test@customer.com");
    loginJson.put("password", "password");
    Result result =
        assertPlatformException(() -> route(fakeRequest("POST", "/api/login").bodyJson(loginJson)));
    JsonNode json = Json.parse(contentAsString(result));

    assertEquals(UNAUTHORIZED, result.status());
    assertThat(
        json.get("error").toString(),
        allOf(notNullValue(), containsString("Invalid User Credentials")));
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testLocalUserWithLdapConfigured() throws LdapException {
    startApp(false);
    Customer customer = ModelFactory.testCustomer();
    Users user = ModelFactory.testUser(customer);
    ObjectNode loginJson = Json.newObject();
    loginJson.put("email", "test@customer.com");
    loginJson.put("password", "password");
    settableRuntimeConfigFactory.globalRuntimeConf().setValue("yb.security.ldap.use_ldap", "true");
    when(ldapUtil.loginWithLdap(any())).thenReturn(null);
    Result result = route(fakeRequest("POST", "/api/login").bodyJson(loginJson));
    JsonNode json = Json.parse(contentAsString(result));

    assertEquals(OK, result.status());
    assertNotNull(json.get("authToken"));
    assertAuditEntry(1, customer.uuid);

    settableRuntimeConfigFactory.globalRuntimeConf().setValue("yb.security.ldap.use_ldap", "false");
  }

  @Test
  public void testInsecureLoginValid() {
    startApp(false);
    Customer customer = ModelFactory.testCustomer("Test Customer 1");
    ModelFactory.testUser(customer, "tc1@test.com", Role.ReadOnly);
    ConfigHelper configHelper = new ConfigHelper();
    configHelper.loadConfigToDB(
        ConfigHelper.ConfigType.Security, ImmutableMap.of("level", "insecure"));

    Result result = route(fakeRequest("GET", "/api/insecure_login"));
    JsonNode json = Json.parse(contentAsString(result));

    assertEquals(OK, result.status());
    assertNotNull(json.get("apiToken"));
    assertNotNull(json.get("customerUUID"));
    assertAuditEntry(1, customer.uuid);
  }

  @Test
  public void testInsecureLoginWithoutReadOnlyUser()
      throws InterruptedException, ExecutionException, TimeoutException {
    startApp(false);
    Customer customer = ModelFactory.testCustomer("Test Customer 1");
    ModelFactory.testUser(customer, "tc1@test.com", Role.Admin);
    ConfigHelper configHelper = new ConfigHelper();
    configHelper.loadConfigToDB(
        ConfigHelper.ConfigType.Security, ImmutableMap.of("level", "insecure"));

    Result result = routeWithYWErrHandler(fakeRequest("GET", "/api/insecure_login"), app);
    assertUnauthorized(result, "No read only customer exists.");
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testInsecureLoginInvalid()
      throws InterruptedException, ExecutionException, TimeoutException {
    startApp(false);
    Customer customer = ModelFactory.testCustomer("Test Customer 1");
    ModelFactory.testUser(customer);

    Result result = routeWithYWErrHandler(fakeRequest("GET", "/api/insecure_login"), app);

    assertUnauthorized(result, "Insecure login unavailable.");
    assertAuditEntry(0, customer.uuid);
  }

  @Test
  public void testRegisterCustomer() {
    startApp(true);
    ObjectNode registerJson = Json.newObject();
    registerJson.put("code", "fb");
    registerJson.put("email", "foo2@bar.com");
    registerJson.put("password", "pAssw_0rd");
    registerJson.put("name", "Foo");

    Result result =
        route(fakeRequest("POST", "/api/register?generateApiToken=true").bodyJson(registerJson));
    JsonNode json = Json.parse(contentAsString(result));

    assertEquals(OK, result.status());
    assertNotNull(json.get("authToken"));
    assertNotNull(json.get("apiToken"));
    Customer c1 = Customer.get(UUID.fromString(json.get("customerUUID").asText()));
    assertAuditEntry(1, c1.uuid);

    ObjectNode loginJson = Json.newObject();
    loginJson.put("email", "foo2@bar.com");
    loginJson.put("password", "pAssw_0rd");
    result = route(fakeRequest("POST", "/api/login").bodyJson(loginJson));
    json = Json.parse(contentAsString(result));

    assertEquals(OK, result.status());
    assertNotNull(json.get("authToken"));
    assertAuditEntry(2, c1.uuid);
    assertNotNull(alertDestinationService.getDefaultDestination(c1.uuid));
  }

  @Test
  public void testRegisterCustomerWrongPassword()
      throws InterruptedException, ExecutionException, TimeoutException {
    startApp(true);
    ObjectNode registerJson = Json.newObject();
    registerJson.put("code", "fb");
    registerJson.put("email", "foo2@bar.com");
    registerJson.put("password", "pAssw0rd");
    registerJson.put("name", "Foo");

    Result result =
        routeWithYWErrHandler(fakeRequest("POST", "/api/register").bodyJson(registerJson), app);

    assertEquals(BAD_REQUEST, result.status());
  }

  @Test
  public void testRegisterMultiCustomer()
      throws InterruptedException, ExecutionException, TimeoutException {
    startApp(true);
    ObjectNode registerJson = Json.newObject();
    registerJson.put("code", "fb");
    registerJson.put("email", "foo2@bar.com");
    registerJson.put("password", "pAssw_0rd");
    registerJson.put("name", "Foo");

    Result result = route(fakeRequest("POST", "/api/register").bodyJson(registerJson));
    JsonNode json = Json.parse(contentAsString(result));

    assertEquals(OK, result.status());
    assertNotNull(json.get("authToken"));
    String authToken = json.get("authToken").asText();
    Customer c1 = Customer.get(UUID.fromString(json.get("customerUUID").asText()));
    Users user = Users.get(UUID.fromString(json.get("userUUID").asText()));
    assertEquals(Role.SuperAdmin, user.getRole());
    assertAuditEntry(1, c1.uuid);

    ObjectNode registerJson2 = Json.newObject();
    registerJson2.put("code", "fb");
    registerJson2.put("email", "foo3@bar.com");
    registerJson2.put("password", "pAssw_0rd");
    registerJson2.put("name", "Foo");

    result =
        route(
            fakeRequest("POST", "/api/register")
                .bodyJson(registerJson2)
                .header("X-AUTH-TOKEN", authToken));
    json = Json.parse(contentAsString(result));

    // Register duplicate
    assertEquals(OK, result.status());
    assertNotNull(json.get("authToken"));
    assertAuditEntry(2, c1.uuid);
    checkCount("count", "2");
    result =
        routeWithYWErrHandler(
            fakeRequest("POST", "/api/register")
                .bodyJson(registerJson2)
                .header("X-AUTH-TOKEN", authToken),
            app);
    assertConflict(result, "Customer already registered.");
    checkCount("count", "2"); // Make sure that count stays 2
    // TODO(amalyshev): also check that alert config was rolled back
  }

  public void checkCount(String count, String s2) {
    Result result;
    JsonNode json;
    result = route(fakeRequest("GET", "/api/customer_count"));
    json = Json.parse(contentAsString(result));
    assertOk(result);
    assertValue(json, count, s2);
  }

  @Test
  public void testRegisterMultiCustomerNoAuth()
      throws InterruptedException, ExecutionException, TimeoutException {
    startApp(true);
    ObjectNode registerJson = Json.newObject();
    registerJson.put("code", "fb");
    registerJson.put("email", "foo2@bar.com");
    registerJson.put("password", "pAssw_0rd");
    registerJson.put("name", "Foo");

    Result result = route(fakeRequest("POST", "/api/register").bodyJson(registerJson));
    JsonNode json = Json.parse(contentAsString(result));

    assertEquals(OK, result.status());
    assertNotNull(json.get("authToken"));
    String authToken = json.get("authToken").asText();
    Customer c1 = Customer.get(UUID.fromString(json.get("customerUUID").asText()));

    ObjectNode registerJson2 = Json.newObject();
    registerJson2.put("code", "fb");
    registerJson2.put("email", "foo3@bar.com");
    registerJson2.put("password", "pAssw_0rd");
    registerJson2.put("name", "Foo");

    result =
        routeWithYWErrHandler(fakeRequest("POST", "/api/register").bodyJson(registerJson2), app);

    assertBadRequest(result, "Only Super Admins can register tenant.");
  }

  @Test
  public void testRegisterMultiCustomerWrongAuth()
      throws InterruptedException, ExecutionException, TimeoutException {
    startApp(true);
    ObjectNode registerJson = Json.newObject();
    registerJson.put("code", "fb");
    registerJson.put("email", "foo2@bar.com");
    registerJson.put("password", "pAssw_0rd");
    registerJson.put("name", "Foo");

    Result result = route(fakeRequest("POST", "/api/register").bodyJson(registerJson));
    JsonNode json = Json.parse(contentAsString(result));

    assertEquals(OK, result.status());
    assertNotNull(json.get("authToken"));
    String authToken = json.get("authToken").asText();
    Customer c1 = Customer.get(UUID.fromString(json.get("customerUUID").asText()));

    ObjectNode registerJson2 = Json.newObject();
    registerJson2.put("code", "fb");
    registerJson2.put("email", "foo3@bar.com");
    registerJson2.put("password", "pAssw_0rd");
    registerJson2.put("name", "Foo");

    result =
        route(
            fakeRequest("POST", "/api/register")
                .bodyJson(registerJson2)
                .header("X-AUTH-TOKEN", authToken));
    json = Json.parse(contentAsString(result));

    assertEquals(OK, result.status());
    assertNotNull(json.get("authToken"));
    String authToken2 = json.get("authToken").asText();

    ObjectNode registerJson3 = Json.newObject();
    registerJson3.put("code", "fb");
    registerJson3.put("email", "foo4@bar.com");
    registerJson3.put("password", "pAssw_0rd");
    registerJson3.put("name", "Foo");

    result =
        routeWithYWErrHandler(
            fakeRequest("POST", "/api/register")
                .bodyJson(registerJson3)
                .header("X-AUTH-TOKEN", authToken2),
            app);

    assertBadRequest(result, "Only Super Admins can register tenant.");
  }

  @Test
  public void testRegisterCustomerWithLongerCode()
      throws InterruptedException, ExecutionException, TimeoutException {
    startApp(true);
    ObjectNode registerJson = Json.newObject();
    registerJson.put("code", "abcabcabcabcabcabc");
    registerJson.put("email", "foo2@bar.com");
    registerJson.put("password", "pAssw_0rd");
    registerJson.put("name", "Foo");

    Result result =
        routeWithYWErrHandler(fakeRequest("POST", "/api/register").bodyJson(registerJson), app);
    JsonNode json = Json.parse(contentAsString(result));

    assertEquals(BAD_REQUEST, result.status());
    assertValue(json, "error", "{\"code\":[\"Maximum length is 15\"]}");
  }

  @Test
  public void testRegisterCustomerExceedingLimit()
      throws InterruptedException, ExecutionException, TimeoutException {
    startApp(false);
    ModelFactory.testCustomer("Test Customer 1");
    ObjectNode registerJson = Json.newObject();
    registerJson.put("code", "fb");
    registerJson.put("email", "foo2@bar.com");
    registerJson.put("password", "pAssw_0rd");
    registerJson.put("name", "Foo");
    Result result =
        routeWithYWErrHandler(fakeRequest("POST", "/api/register").bodyJson(registerJson), app);
    assertBadRequest(result, "Cannot register multiple accounts in Single tenancy.");
  }

  @Test
  public void testRegisterCustomerWithoutEmail() {
    startApp(false);
    ObjectNode registerJson = Json.newObject();
    registerJson.put("email", "test@customer.com");
    Result result =
        assertPlatformException(
            () -> route(fakeRequest("POST", "/api/login").bodyJson(registerJson)));

    JsonNode json = Json.parse(contentAsString(result));

    assertEquals(BAD_REQUEST, result.status());
    assertThat(
        json.get("error").toString(),
        allOf(notNullValue(), containsString("{\"password\":[\"This field is required\"]}")));
  }

  @Test
  public void testLogout() {
    startApp(false);
    Customer customer = ModelFactory.testCustomer("Test Customer 1");
    ModelFactory.testUser(customer);
    ObjectNode loginJson = Json.newObject();
    loginJson.put("email", "test@customer.com");
    loginJson.put("password", "password");
    Result result = route(fakeRequest("POST", "/api/login").bodyJson(loginJson));
    JsonNode json = Json.parse(contentAsString(result));
    assertAuditEntry(1, customer.uuid);

    assertEquals(OK, result.status());
    String authToken = json.get("authToken").asText();
    result = route(fakeRequest("GET", "/api/logout").header("X-AUTH-TOKEN", authToken));
    assertEquals(OK, result.status());
    assertAuditEntry(1, customer.uuid);
  }

  @Test
  public void testAuthTokenExpiry() {
    startApp(false);
    Customer customer = ModelFactory.testCustomer("Test Customer 1");
    ModelFactory.testUser(customer);
    ObjectNode loginJson = Json.newObject();
    loginJson.put("email", "test@customer.com");
    loginJson.put("password", "password");
    Result result = route(fakeRequest("POST", "/api/login").bodyJson(loginJson));
    JsonNode json = Json.parse(contentAsString(result));
    String authToken1 = json.get("authToken").asText();
    loginJson.put("email", "test@customer.com");
    loginJson.put("password", "password");
    result = route(fakeRequest("POST", "/api/login").bodyJson(loginJson));
    json = Json.parse(contentAsString(result));
    String authToken2 = json.get("authToken").asText();
    assertEquals(authToken1, authToken2);
    assertAuditEntry(2, customer.uuid);
  }

  @Test
  public void testApiTokenUpsert() {
    startApp(false);
    Customer customer = ModelFactory.testCustomer("Test Customer 1");
    ModelFactory.testUser(customer);
    ObjectNode loginJson = Json.newObject();
    loginJson.put("email", "test@customer.com");
    loginJson.put("password", "password");
    Result result = route(fakeRequest("POST", "/api/login").bodyJson(loginJson));
    assertAuditEntry(1, customer.uuid);

    JsonNode json = Json.parse(contentAsString(result));
    String authToken = json.get("authToken").asText();
    String custUuid = json.get("customerUUID").asText();
    ObjectNode apiTokenJson = Json.newObject();
    apiTokenJson.put("authToken", authToken);
    result =
        route(
            fakeRequest("PUT", "/api/customers/" + custUuid + "/api_token")
                .header("X-AUTH-TOKEN", authToken));
    json = Json.parse(contentAsString(result));

    assertEquals(OK, result.status());
    assertNotNull(json.get("apiToken"));
    assertAuditEntry(2, customer.uuid);
  }

  @Test
  public void testApiTokenUpdate() {
    startApp(false);
    Customer customer = ModelFactory.testCustomer("Test Customer 1");
    ModelFactory.testUser(customer);
    ObjectNode loginJson = Json.newObject();
    loginJson.put("email", "test@customer.com");
    loginJson.put("password", "password");
    Result result = route(fakeRequest("POST", "/api/login").bodyJson(loginJson));
    JsonNode json = Json.parse(contentAsString(result));
    String authToken = json.get("authToken").asText();
    String custUuid = json.get("customerUUID").asText();
    ObjectNode apiTokenJson = Json.newObject();
    apiTokenJson.put("authToken", authToken);
    result =
        route(
            fakeRequest("PUT", "/api/customers/" + custUuid + "/api_token")
                .header("X-AUTH-TOKEN", authToken));
    json = Json.parse(contentAsString(result));
    String apiToken1 = json.get("apiToken").asText();
    apiTokenJson.put("authToken", authToken);
    result =
        route(
            fakeRequest("PUT", "/api/customers/" + custUuid + "/api_token")
                .header("X-AUTH-TOKEN", authToken));
    json = Json.parse(contentAsString(result));
    String apiToken2 = json.get("apiToken").asText();
    assertNotEquals(apiToken1, apiToken2);
    assertAuditEntry(3, customer.uuid);
  }

  @Test
  public void testCustomerCount() {
    startApp(false);
    Result result = route(fakeRequest("GET", "/api/customer_count"));
    JsonNode json = Json.parse(contentAsString(result));
    assertOk(result);
    assertValue(json, "count", "0");
    ModelFactory.testCustomer("Test Customer 1");
    checkCount("count", "1");
  }

  @Test
  public void testAppVersion() {
    startApp(false);
    Result result = route(fakeRequest("GET", "/api/app_version"));
    JsonNode json = Json.parse(contentAsString(result));
    assertOk(result);
    assertEquals(json, Json.newObject());
    ConfigHelper configHelper = new ConfigHelper();
    configHelper.loadConfigToDB(
        ConfigHelper.ConfigType.SoftwareVersion, ImmutableMap.of("version", "0.0.1"));
    result = route(fakeRequest("GET", "/api/app_version"));
    json = Json.parse(contentAsString(result));
    assertOk(result);
    assertValue(json, "version", "0.0.1");
  }

  @Test
  public void testProxyRequestInvalidFormat()
      throws InterruptedException, ExecutionException, TimeoutException {
    startApp(false);
    Customer customer = ModelFactory.testCustomer("Test Customer 1");
    Users user = ModelFactory.testUser(customer);
    String authToken = user.createAuthToken();
    Universe universe = ModelFactory.createUniverse(customer.getCustomerId());
    Http.RequestBuilder request =
        fakeRequest("GET", "/universes/" + universe.universeUUID + "/proxy/www.test.com")
            .header("X-AUTH-TOKEN", authToken);
    Result result = routeWithYWErrHandler(request, app);
    assertBadRequest(result, "Invalid proxy request");
  }

  @Test
  public void testProxyRequestInvalidIP()
      throws InterruptedException, ExecutionException, TimeoutException {
    startApp(false);
    Customer customer = ModelFactory.testCustomer("Test Customer 1");
    Users user = ModelFactory.testUser(customer);
    String authToken = user.createAuthToken();
    Universe universe = ModelFactory.createUniverse(customer.getCustomerId());
    Http.RequestBuilder request =
        fakeRequest("GET", "/universes/" + universe.universeUUID + "/proxy/" + "127.0.0.1:7000")
            .header("X-AUTH-TOKEN", authToken);
    Result result = routeWithYWErrHandler(request, app);
    assertBadRequest(result, "Invalid proxy request");
  }

  @Test
  public void testProxyRequestInvalidPort()
      throws InterruptedException, ExecutionException, TimeoutException {
    startApp(false);
    Customer customer = ModelFactory.testCustomer("Test Customer 1");
    Users user = ModelFactory.testUser(customer);
    String authToken = user.createAuthToken();
    Provider provider = ModelFactory.awsProvider(customer);

    Region r = Region.create(provider, "region-1", "PlacementRegion-1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ-1", "subnet-1");
    AvailabilityZone.createOrThrow(r, "az-2", "PlacementAZ-2", "subnet-2");
    AvailabilityZone.createOrThrow(r, "az-3", "PlacementAZ-3", "subnet-3");
    InstanceType i =
        InstanceType.upsert(
            provider.uuid, "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());
    UniverseDefinitionTaskParams.UserIntent userIntent = getTestUserIntent(r, provider, i, 3);
    Universe universe = ModelFactory.createUniverse(customer.getCustomerId());
    Universe.saveDetails(
        universe.universeUUID, ApiUtils.mockUniverseUpdater(userIntent, "test-prefix"));
    universe = Universe.getOrBadRequest(universe.universeUUID);
    NodeDetails node = universe.getUniverseDetails().nodeDetailsSet.stream().findFirst().get();
    System.out.println("PRIVATE IP: " + node.cloudInfo.private_ip);
    Http.RequestBuilder request =
        fakeRequest(
                "GET",
                "/universes/"
                    + universe.universeUUID
                    + "/proxy/"
                    + node.cloudInfo.private_ip
                    + ":7001/")
            .header("X-AUTH-TOKEN", authToken);
    Result result = routeWithYWErrHandler(request, app);
    assertBadRequest(result, "Invalid proxy request");
  }

  @Test
  public void testProxyRequestValid()
      throws InterruptedException, ExecutionException, TimeoutException {
    startApp(false);
    Customer customer = ModelFactory.testCustomer("Test Customer 1");
    Users user = ModelFactory.testUser(customer);
    String authToken = user.createAuthToken();
    Provider provider = ModelFactory.awsProvider(customer);

    Region r = Region.create(provider, "region-1", "PlacementRegion-1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ-1", "subnet-1");
    AvailabilityZone.createOrThrow(r, "az-2", "PlacementAZ-2", "subnet-2");
    AvailabilityZone.createOrThrow(r, "az-3", "PlacementAZ-3", "subnet-3");
    InstanceType i =
        InstanceType.upsert(
            provider.uuid, "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());
    UniverseDefinitionTaskParams.UserIntent userIntent = getTestUserIntent(r, provider, i, 3);
    Universe universe = ModelFactory.createUniverse(customer.getCustomerId());
    Universe.saveDetails(
        universe.universeUUID, ApiUtils.mockUniverseUpdater(userIntent, "test-prefix"));
    universe = Universe.getOrBadRequest(universe.universeUUID);
    UniverseDefinitionTaskParams details = universe.getUniverseDetails();
    NodeDetails node = details.nodeDetailsSet.stream().findFirst().get();

    // Set to an invalid IP
    node.cloudInfo.private_ip = "host-n1";
    universe.setUniverseDetails(details);
    universe.update();
    universe = Universe.getOrBadRequest(universe.universeUUID);

    String nodeAddr = node.cloudInfo.private_ip + ":" + node.masterHttpPort;
    Http.RequestBuilder request =
        fakeRequest("GET", "/universes/" + universe.universeUUID + "/proxy/" + nodeAddr + "/")
            .header("X-AUTH-TOKEN", authToken);
    Result result = routeWithYWErrHandler(request, app);
    // Expect the request to fail since the hostname isn't real.
    // This shows that it got past validation though
    assertInternalServerError(result, null /*errorStr*/);
  }

  @Test
  public void testProxyRequestUnAuthenticated() {
    startApp(false);
    Customer customer = ModelFactory.testCustomer("Test Customer 1");
    Provider provider = ModelFactory.awsProvider(customer);

    Region r = Region.create(provider, "region-1", "PlacementRegion-1", "default-image");
    AvailabilityZone.createOrThrow(r, "az-1", "PlacementAZ-1", "subnet-1");
    AvailabilityZone.createOrThrow(r, "az-2", "PlacementAZ-2", "subnet-2");
    AvailabilityZone.createOrThrow(r, "az-3", "PlacementAZ-3", "subnet-3");
    InstanceType i =
        InstanceType.upsert(
            provider.uuid, "c3.xlarge", 10, 5.5, new InstanceType.InstanceTypeDetails());
    UniverseDefinitionTaskParams.UserIntent userIntent = getTestUserIntent(r, provider, i, 3);
    Universe universe = ModelFactory.createUniverse(customer.getCustomerId());
    Universe.saveDetails(
        universe.universeUUID, ApiUtils.mockUniverseUpdater(userIntent, "test-prefix"));
    universe = Universe.getOrBadRequest(universe.universeUUID);
    NodeDetails node = universe.getUniverseDetails().nodeDetailsSet.stream().findFirst().get();
    String nodeAddr = node.cloudInfo.private_ip + ":" + node.masterHttpPort;
    Result result =
        route(
            fakeRequest("GET", "/universes/" + universe.universeUUID + "/proxy/" + nodeAddr + "/"));
    // Expect the request to fail since the hostname isn't real.
    // This shows that it got past validation though
    assertForbidden(result, "Unable To Authenticate User");
  }
}
