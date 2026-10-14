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
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.google.common.collect.ImmutableMap;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.controllers.TokenAuthenticator;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Users;
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
}
