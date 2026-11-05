// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.api.v2;

import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static com.yugabyte.yw.common.AssertHelper.assertBadRequest;
import static com.yugabyte.yw.common.AssertHelper.assertConflict;
import static com.yugabyte.yw.common.AssertHelper.assertForbidden;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static com.yugabyte.yw.common.AssertHelper.assertUnauthorized;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.controllers.RequestContext;
import com.yugabyte.yw.controllers.SessionController;
import com.yugabyte.yw.controllers.TokenAuthenticator;
import com.yugabyte.yw.controllers.handlers.CustomerRegistrationHandler;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Users;
import io.ebean.DB;
import io.ebean.Transaction;
import java.util.List;
import java.util.UUID;
import org.junit.Test;
import play.Application;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;
import play.test.Helpers;

public class CustomerRegistrationApiTest extends FakeDBApplication {

  private static final String REGISTER_URL = "/api/v2/customers";

  @Override
  protected Application provideApplication() {
    return provideApplication(ImmutableMap.of("yb.multiTenant", true));
  }

  static ObjectNode registrationSpec(String email) {
    ObjectNode spec = Json.newObject();
    spec.put("code", "admin");
    spec.put("name", "Example Customer");
    spec.put("email", email);
    spec.put("password", "Passw0rd#123");
    return spec;
  }

  static Result register(Application app, JsonNode spec, String apiToken) {
    Http.RequestBuilder request = Helpers.fakeRequest("POST", REGISTER_URL).bodyJson(spec);
    if (apiToken != null) {
      request.header(TokenAuthenticator.API_TOKEN_HEADER, apiToken);
    }
    return Helpers.route(app, request);
  }

  static JsonNode registerOk(Application app, JsonNode spec, String apiToken) {
    Result result = register(app, spec, apiToken);
    assertEquals(contentAsString(result), OK, result.status());
    return Json.parse(contentAsString(result));
  }

  @Test
  public void firstRegistrationWithoutAuthCreatesSuperAdmin() {
    JsonNode json = registerOk(app, registrationSpec("admin@example.com"), null);

    UUID customerUuid = UUID.fromString(json.get("customer_uuid").asText());
    Users user = Users.getOrBadRequest(UUID.fromString(json.get("user_uuid").asText()));
    assertEquals(customerUuid, user.getCustomerUUID());
    assertEquals(Users.Role.SuperAdmin, user.getRole());
    assertEquals(user, Users.authWithApiToken(json.get("api_token").asText()));
    assertEquals(user.getApiTokenVersion().longValue(), json.get("api_token_version").asLong());
    assertNotNull(Customer.get(customerUuid));
    assertAuditEntry(1, customerUuid);
  }

  @Test
  public void superAdminRegistersAdminCustomer() {
    JsonNode first = registerOk(app, registrationSpec("admin@example.com"), null);

    JsonNode second =
        registerOk(app, registrationSpec("tenant@example.com"), first.get("api_token").asText());

    Users user = Users.getOrBadRequest(UUID.fromString(second.get("user_uuid").asText()));
    assertEquals(Users.Role.Admin, user.getRole());
    assertEquals(UUID.fromString(second.get("customer_uuid").asText()), user.getCustomerUUID());
    assertEquals(2, Customer.find.query().findCount());
  }

  @Test
  public void additionalRegistrationWithoutAuthIsUnauthorized() {
    registerOk(app, registrationSpec("admin@example.com"), null);

    Result result =
        assertPlatformException(() -> register(app, registrationSpec("tenant@example.com"), null));

    assertUnauthorized(result, "Unable to authenticate user");
    assertEquals(1, Customer.find.query().findCount());
  }

  @Test
  public void additionalRegistrationByAdminIsForbidden() {
    JsonNode first = registerOk(app, registrationSpec("admin@example.com"), null);
    JsonNode second =
        registerOk(app, registrationSpec("tenant@example.com"), first.get("api_token").asText());

    Result result =
        assertPlatformException(
            () ->
                register(
                    app, registrationSpec("other@example.com"), second.get("api_token").asText()));

    assertForbidden(result, "Only SuperAdmins can register additional customers");
    assertEquals(2, Customer.find.query().findCount());
  }

  @Test
  public void duplicateEmailIsConflictAndRollsBack() {
    JsonNode first = registerOk(app, registrationSpec("admin@example.com"), null);

    Result result =
        assertPlatformException(
            () ->
                register(
                    app, registrationSpec("admin@example.com"), first.get("api_token").asText()));

    assertConflict(result, "Customer already registered.");
    assertEquals(1, Customer.find.query().findCount());
  }

  @Test
  public void additionalRegistrationWithSsoIsConflict() {
    JsonNode first = registerOk(app, registrationSpec("admin@example.com"), null);
    mutableConfigFactory.globalRuntimeConf().setValue("yb.security.use_oauth", "true");

    Result result =
        assertPlatformException(
            () ->
                register(
                    app, registrationSpec("tenant@example.com"), first.get("api_token").asText()));

    assertConflict(result, "A customer is already registered on this SSO-enabled YBA instance");
  }

  @Test
  public void invalidEmailIsBadRequest() {
    Result result =
        assertPlatformException(() -> register(app, registrationSpec("not-an-email"), null));

    assertBadRequest(result, "Invalid email: not-an-email");
    assertEquals(0, Customer.find.query().findCount());
  }

  @Test
  public void passwordPolicyViolationIsBadRequestAndRollsBack() {
    ObjectNode spec = registrationSpec("admin@example.com");
    spec.put("password", "short");

    Result result = assertPlatformException(() -> register(app, spec, null));

    assertEquals(Http.Status.BAD_REQUEST, result.status());
    assertEquals(0, Customer.find.query().findCount());
  }

  @Test
  public void registrationOnPopulatedInstanceDoesNotWaitForFirstRegistrationLock() {
    // Integration tests register a customer per test on one shared YBA at the same moment. None
    // of them can be the first customer, so none may wait on the first-registration lock: a waiter
    // holds a pooled DB connection, and enough waiters starve the pool.
    String superAdminToken =
        registerOk(app, registrationSpec("admin@example.com"), null).get("api_token").asText();
    // Hold the lock, as a first registration in progress would.
    try (Transaction txn = DB.beginTransaction()) {
      DB.sqlQuery("SELECT pg_advisory_xact_lock(:key)")
          .setParameter("key", CustomerRegistrationHandler.REGISTRATION_LOCK_KEY)
          .findOne();
      int i = 0;
      for (String url : List.of(REGISTER_URL, "/api/v1/register")) {
        Result result =
            Helpers.route(
                app,
                Helpers.fakeRequest("POST", url)
                    .bodyJson(registrationSpec("tenant" + i++ + "@example.com"))
                    .header(TokenAuthenticator.API_TOKEN_HEADER, superAdminToken));
        assertEquals(contentAsString(result), OK, result.status());
      }
    }
    assertEquals(3, Customer.find.query().findCount());
  }

  @Test
  public void rotatedSuperAdminTokenCannotRegister() {
    JsonNode first = registerOk(app, registrationSpec("admin@example.com"), null);
    String oldToken = first.get("api_token").asText();
    Result rotated =
        Helpers.route(
            app,
            Helpers.fakeRequest(
                    "PUT",
                    "/api/v2/customers/" + first.get("customer_uuid").asText() + "/api-token")
                .header(TokenAuthenticator.API_TOKEN_HEADER, oldToken));
    assertEquals(contentAsString(rotated), OK, rotated.status());
    String newToken = Json.parse(contentAsString(rotated)).get("api_token").asText();

    Result result =
        assertPlatformException(
            () -> register(app, registrationSpec("tenant@example.com"), oldToken));

    assertUnauthorized(result, "Unable to authenticate user");
    registerOk(app, registrationSpec("tenant@example.com"), newToken);
  }

  @Test
  public void v1RegistrationBySuperAdminIsAttributedToItAndLeavesTheThreadAlone() {
    JsonNode first = registerOk(app, registrationSpec("admin@example.com"), null);
    SessionController controller = app.injector().instanceOf(SessionController.class);

    Result result =
        controller.register(
            false,
            Helpers.fakeRequest("POST", "/api/v1/register")
                .bodyJson(registrationSpec("tenant@example.com"))
                .header(TokenAuthenticator.API_TOKEN_HEADER, first.get("api_token").asText())
                .build());

    assertEquals(contentAsString(result), OK, result.status());
    // checkAllowed set the SuperAdmin as the request's user; nothing else would clear it.
    assertNull(RequestContext.getIfPresent(TokenAuthenticator.USER));
    UUID superAdminCustomer = UUID.fromString(first.get("customer_uuid").asText());
    assertTrue(
        Audit.getAll(superAdminCustomer).stream()
            .anyMatch(
                a ->
                    a.getAction() == Audit.ActionType.Register
                        && a.getUserUUID().toString().equals(first.get("user_uuid").asText())));
  }
}
