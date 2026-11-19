// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.api.v2;

import static com.yugabyte.yw.api.v2.CustomerRegistrationApiTest.registerOk;
import static com.yugabyte.yw.api.v2.CustomerRegistrationApiTest.registrationSpec;
import static com.yugabyte.yw.common.AssertHelper.assertBadRequest;
import static com.yugabyte.yw.common.AssertHelper.assertConflict;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static play.mvc.Http.Status.OK;
import static play.mvc.Http.Status.UNAUTHORIZED;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.controllers.TokenAuthenticator;
import com.yugabyte.yw.models.Audit;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.rbac.RoleBinding;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;
import play.test.Helpers;

public class UserApiTest extends FakeDBApplication {

  private static final String PASSWORD = "Passw0rd#123";

  private UUID customerUuid;
  private String apiToken;

  @Before
  public void registerSuperAdmin() {
    JsonNode registration = registerOk(app, registrationSpec("admin@example.com"), null);
    customerUuid = UUID.fromString(registration.get("customer_uuid").asText());
    apiToken = registration.get("api_token").asText();
  }

  private static ObjectNode createSpec(String email, String role, String password) {
    ObjectNode spec = Json.newObject().put("email", email);
    if (role != null) {
      spec.put("role", role);
    }
    ObjectNode body = Json.newObject();
    body.set("spec", spec);
    return body.put("password", password);
  }

  private Result create(String token, JsonNode body) {
    Http.RequestBuilder request =
        Helpers.fakeRequest("POST", "/api/v2/customers/" + customerUuid + "/users").bodyJson(body);
    if (token != null) {
      request.header(TokenAuthenticator.API_TOKEN_HEADER, token);
    }
    return Helpers.route(app, request);
  }

  private JsonNode createOk(JsonNode body) {
    Result result = create(apiToken, body);
    assertEquals(contentAsString(result), OK, result.status());
    return Json.parse(contentAsString(result));
  }

  @Test
  public void createsAdminUser() {
    JsonNode creation = createOk(createSpec("Project-1@Example.com", "Admin", PASSWORD));

    JsonNode user = creation.get("user");
    assertEquals("project-1@example.com", user.get("spec").get("email").asText());
    assertEquals("Admin", user.get("spec").get("role").asText());
    assertEquals(customerUuid.toString(), user.get("info").get("customer_uuid").asText());
    assertFalse(user.get("info").get("is_primary").asBoolean());
    assertFalse("no token unless asked", creation.has("api_token"));

    UUID userUuid = UUID.fromString(user.get("info").get("uuid").asText());
    Users created = Users.getOrBadRequest(customerUuid, userUuid);
    assertEquals(Users.Role.Admin, created.getRole());
    // The new authz model is on by default: the system role is bound.
    assertFalse(RoleBinding.getAll(userUuid).isEmpty());
    // The user can log in with the password.
    Result login =
        Helpers.route(
            app,
            Helpers.fakeRequest("POST", "/api/v2/login")
                .bodyJson(
                    Json.newObject()
                        .put("email", "project-1@example.com")
                        .put("password", PASSWORD)));
    assertEquals(contentAsString(login), OK, login.status());

    Audit audit =
        Audit.getAll(customerUuid).stream()
            .filter(a -> a.getTarget() == Audit.TargetType.User)
            .findFirst()
            .orElseThrow();
    assertEquals(Audit.ActionType.Create, audit.getAction());
    assertEquals(userUuid.toString(), audit.getTargetID());
    assertFalse(audit.getPayload().toString().contains(PASSWORD));
  }

  @Test
  public void generatedApiTokenAuthenticatesTheNewUser() {
    JsonNode creation =
        createOk(
            createSpec("project-1@example.com", "Admin", PASSWORD).put("generate_api_token", true));

    String token = creation.get("api_token").asText();
    assertNotNull(token);
    assertEquals(
        Users.getByEmail("project-1@example.com").getApiTokenVersion().longValue(),
        creation.get("api_token_version").asLong());
    Users authenticated = Users.authWithApiToken(token);
    assertNotNull(authenticated);
    assertEquals("project-1@example.com", authenticated.getEmail());
  }

  @Test
  public void superAdminRoleIsBadRequest() {
    Result result =
        assertPlatformException(
            () -> create(apiToken, createSpec("root2@example.com", "SuperAdmin", PASSWORD)));

    assertBadRequest(result, "Cannot create a SuperAdmin user");
    assertNull(Users.getByEmail("root2@example.com"));
  }

  @Test
  public void missingRoleIsBadRequest() {
    Result result =
        assertPlatformException(
            () -> create(apiToken, createSpec("norole@example.com", null, PASSWORD)));

    assertBadRequest(result, "spec.role is required");
  }

  @Test
  public void invalidEmailIsBadRequest() {
    Result result =
        assertPlatformException(
            () -> create(apiToken, createSpec("not-an-email", "Admin", PASSWORD)));

    assertBadRequest(result, "Invalid email: not-an-email");
  }

  @Test
  public void passwordViolatingThePolicyIsBadRequest() {
    Result result =
        assertPlatformException(
            () -> create(apiToken, createSpec("weak@example.com", "Admin", "weak")));

    assertEquals(400, result.status());
    assertNull(Users.getByEmail("weak@example.com"));
  }

  @Test
  public void takenEmailIsConflict() {
    // Emails are stored lowercased, so a different case is the same user.
    Result result =
        assertPlatformException(
            () -> create(apiToken, createSpec("ADMIN@example.com", "Admin", PASSWORD)));

    assertConflict(result, "User already exists");
  }

  @Test
  public void oidcWithAutoCreatedUsersIsConflict() {
    mutableConfigFactory.globalRuntimeConf().setValue("yb.security.use_oauth", "true");
    mutableConfigFactory
        .globalRuntimeConf()
        .setValue("yb.security.oidc_enable_auto_create_users", "true");

    Result result =
        assertPlatformException(
            () -> create(apiToken, createSpec("sso@example.com", "Admin", PASSWORD)));

    assertConflict(result, "Users are created on their first OIDC login on this YBA");
  }

  @Test
  public void oidcWithoutAutoCreateIgnoresThePassword() {
    mutableConfigFactory.globalRuntimeConf().setValue("yb.security.use_oauth", "true");
    // On by default.
    mutableConfigFactory
        .globalRuntimeConf()
        .setValue("yb.security.oidc_enable_auto_create_users", "false");

    // A password that violates the policy is fine: a random one replaces it.
    JsonNode creation = createOk(createSpec("sso@example.com", "ReadOnly", "weak"));

    assertEquals("ReadOnly", creation.get("user").get("spec").get("role").asText());
  }

  @Test
  public void oidcRefusesGeneratingAnApiToken() {
    mutableConfigFactory.globalRuntimeConf().setValue("yb.security.use_oauth", "true");
    mutableConfigFactory
        .globalRuntimeConf()
        .setValue("yb.security.oidc_enable_auto_create_users", "false");

    Result result =
        assertPlatformException(
            () ->
                create(
                    apiToken,
                    createSpec("sso@example.com", "Admin", PASSWORD)
                        .put("generate_api_token", true)));

    assertBadRequest(result, "generate_api_token isn't allowed when OIDC SSO is enabled");
    assertNull(Users.getByEmail("sso@example.com"));
  }

  @Test
  public void noTokenIsUnauthorized() {
    Result result = create(null, createSpec("project-1@example.com", "Admin", PASSWORD));

    assertEquals(contentAsString(result), UNAUTHORIZED, result.status());
  }

  @Test
  public void readOnlyUserIsUnauthorized() {
    Users readOnly =
        ModelFactory.testUser(
            Customer.getOrBadRequest(customerUuid), "ro@example.com", Users.Role.ReadOnly);

    Result result =
        create(readOnly.upsertApiToken(), createSpec("project-1@example.com", "Admin", PASSWORD));

    // The authorization layer answers a missing permission with 401.
    assertEquals(contentAsString(result), UNAUTHORIZED, result.status());
    assertNull(Users.getByEmail("project-1@example.com"));
    assertTrue(
        Users.getAll(customerUuid).stream().noneMatch(u -> u.getEmail().startsWith("project")));
  }
}
