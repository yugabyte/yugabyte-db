// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.api.v2;

import static com.yugabyte.yw.api.v2.CustomerRegistrationApiTest.registerOk;
import static com.yugabyte.yw.api.v2.CustomerRegistrationApiTest.registrationSpec;
import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static com.yugabyte.yw.common.AssertHelper.assertBadRequest;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static com.yugabyte.yw.common.AssertHelper.assertUnauthorized;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;

import api.v2.handlers.SessionManagementHandler;
import api.v2.models.LoginSpec;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.controllers.RequestContext;
import com.yugabyte.yw.controllers.TokenAuthenticator;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.extended.UserWithFeatures;
import java.util.List;
import java.util.Set;
import java.util.UUID;
import java.util.stream.Collectors;
import org.junit.Before;
import org.junit.Test;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;
import play.test.Helpers;

public class SessionApiTest extends FakeDBApplication {

  private static final String EMAIL = "admin@example.com";
  private static final String PASSWORD = "Passw0rd#123";

  private UUID customerUuid;
  private UUID userUuid;

  @Before
  public void registerSuperAdmin() {
    JsonNode registration = registerOk(app, registrationSpec(EMAIL), null);
    customerUuid = UUID.fromString(registration.get("customer_uuid").asText());
    userUuid = UUID.fromString(registration.get("user_uuid").asText());
  }

  private Result login(JsonNode body) {
    return Helpers.route(app, Helpers.fakeRequest("POST", "/api/v2/login").bodyJson(body));
  }

  private static ObjectNode credentials(String email, String password) {
    ObjectNode body = Json.newObject().put("email", email);
    if (password != null) {
      body.put("password", password);
    }
    return body;
  }

  private JsonNode loginOk(JsonNode body) {
    Result result = login(body);
    assertEquals(contentAsString(result), OK, result.status());
    assertFalse("no cookies", result.cookies().iterator().hasNext());
    return Json.parse(contentAsString(result));
  }

  @Test
  public void loginReturnsUsableSessionToken() {
    JsonNode session = loginOk(credentials(EMAIL, PASSWORD));

    assertEquals(customerUuid.toString(), session.get("customer_uuid").asText());
    assertEquals(userUuid.toString(), session.get("user_uuid").asText());
    // Aeon's recovery flow: log in, then generate an API token with the session token.
    Result apiToken =
        Helpers.route(
            app,
            Helpers.fakeRequest("PUT", "/api/v2/customers/" + customerUuid + "/api-token")
                .header(TokenAuthenticator.AUTH_TOKEN_HEADER, session.get("auth_token").asText()));
    assertEquals(contentAsString(apiToken), OK, apiToken.status());
    // One entry for the registration, one for the login, one for the API token.
    assertAuditEntry(3, customerUuid);
  }

  @Test
  public void emailIsCaseInsensitive() {
    JsonNode session = loginOk(credentials(EMAIL.toUpperCase(), PASSWORD));

    assertEquals(userUuid.toString(), session.get("user_uuid").asText());
  }

  @Test
  public void wrongPasswordIsUnauthorized() {
    Result result = assertPlatformException(() -> login(credentials(EMAIL, "Wr0ngPassw0rd#")));

    assertUnauthorized(result, "Invalid User Credentials.");
  }

  @Test
  public void unknownEmailIsUnauthorized() {
    Result result =
        assertPlatformException(() -> login(credentials("nobody@example.com", PASSWORD)));

    assertUnauthorized(result, "Invalid User Credentials.");
  }

  @Test
  public void missingPasswordIsBadRequest() {
    Result result = assertPlatformException(() -> login(credentials(EMAIL, null)));

    assertBadRequest(result, "LoginSpec.password");
  }

  @Test
  public void loginWorksWithLegacyAuthorization() {
    // With yb.rbac.use_new_authz off, TokenAuthenticator rejects unauthenticated requests to
    // authenticated controllers. Login must not go through it.
    mutableConfigFactory.globalRuntimeConf().setValue("yb.rbac.use_new_authz", "false");

    JsonNode session = loginOk(credentials(EMAIL, PASSWORD));

    assertEquals(userUuid.toString(), session.get("user_uuid").asText());
  }

  @Test
  public void loginAuditsTheLoggedInUserAndLeavesTheThreadAlone() {
    SessionManagementHandler handler = app.injector().instanceOf(SessionManagementHandler.class);
    LoginSpec spec = new LoginSpec().email(EMAIL).password(PASSWORD);
    Http.Request request = Helpers.fakeRequest("POST", "/api/v2/login").build();
    // Earlier tests in this JVM (e.g. via TestUtils.setFakeHttpContext) can leave a user here.
    RequestContext.clean(Set.of(TokenAuthenticator.USER));

    handler.login(request, spec);
    // Nothing clears RequestContext after this controller, so login must not put a user there.
    assertNull(RequestContext.getIfPresent(TokenAuthenticator.USER));

    // A user left on the worker thread by an earlier request doesn't get the audit entry.
    Users stale = ModelFactory.testUser(ModelFactory.testCustomer("Other Customer"));
    RequestContext.put(TokenAuthenticator.USER, new UserWithFeatures().setUser(stale));
    try {
      handler.login(request, spec);
    } finally {
      RequestContext.clean(Set.of(TokenAuthenticator.USER));
    }
    List<Audit> logins =
        Audit.getAllUserEntries(userUuid).stream()
            .filter(a -> a.getAction() == Audit.ActionType.Login)
            .collect(Collectors.toList());
    assertEquals(2, logins.size());
    logins.forEach(a -> assertEquals(customerUuid, a.getCustomerUUID()));
    assertTrue(Audit.getAllUserEntries(stale.getUuid()).isEmpty());
  }
}
