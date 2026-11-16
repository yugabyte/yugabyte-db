// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.api.v2;

import static com.yugabyte.yw.api.v2.CustomerRegistrationApiTest.registerOk;
import static com.yugabyte.yw.api.v2.CustomerRegistrationApiTest.registrationSpec;
import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static com.yugabyte.yw.common.AssertHelper.assertConflict;
import static com.yugabyte.yw.common.AssertHelper.assertForbidden;
import static com.yugabyte.yw.common.AssertHelper.assertNotFound;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static com.yugabyte.yw.common.AssertHelper.assertUnauthorized;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;

import api.v2.handlers.CustomerManagementHandler;
import com.fasterxml.jackson.databind.JsonNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.controllers.RequestContext;
import com.yugabyte.yw.controllers.TokenAuthenticator;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.extended.UserWithFeatures;
import java.util.Set;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;
import play.test.Helpers;

public class CustomerApiTokenApiTest extends FakeDBApplication {

  private UUID customerUuid;
  private UUID userUuid;
  private String apiToken;
  private long apiTokenVersion;

  @Before
  public void registerSuperAdmin() {
    JsonNode registration = registerOk(app, registrationSpec("admin@example.com"), null);
    customerUuid = UUID.fromString(registration.get("customer_uuid").asText());
    userUuid = UUID.fromString(registration.get("user_uuid").asText());
    apiToken = registration.get("api_token").asText();
    apiTokenVersion = registration.get("api_token_version").asLong();
  }

  private static String url(UUID cUUID) {
    return "/api/v2/customers/" + cUUID + "/api-token";
  }

  private Result generate(UUID cUUID, String header, String token, JsonNode body) {
    Http.RequestBuilder request = Helpers.fakeRequest("PUT", url(cUUID));
    if (body != null) {
      request.bodyJson(body);
    }
    if (header != null) {
      request.header(header, token);
    }
    return Helpers.route(app, request);
  }

  private static JsonNode versionSpec(long version) {
    return Json.newObject().put("api_token_version", version);
  }

  private static JsonNode assertOkJson(Result result) {
    assertEquals(contentAsString(result), OK, result.status());
    return Json.parse(contentAsString(result));
  }

  @Test
  public void rotatesTheCallersTokenWithApiToken() {
    JsonNode json =
        assertOkJson(generate(customerUuid, TokenAuthenticator.API_TOKEN_HEADER, apiToken, null));

    String newToken = json.get("api_token").asText();
    assertNotEquals(apiToken, newToken);
    assertEquals(customerUuid.toString(), json.get("customer_uuid").asText());
    assertEquals(userUuid.toString(), json.get("user_uuid").asText());
    assertEquals(apiTokenVersion + 1, json.get("api_token_version").asLong());
    // The previous token stops working, including for this endpoint.
    assertNull(Users.authWithApiToken(apiToken));
    assertEquals(userUuid, Users.authWithApiToken(newToken).getUuid());
    assertUnauthorized(
        assertPlatformException(
            () -> generate(customerUuid, TokenAuthenticator.API_TOKEN_HEADER, apiToken, null)),
        "Unable to authenticate user");
    // One entry for the registration, one for the rotation.
    assertAuditEntry(2, customerUuid);
  }

  @Test
  public void acceptsSessionToken() {
    String authToken = Users.getOrBadRequest(userUuid).createAuthToken();

    JsonNode json =
        assertOkJson(generate(customerUuid, TokenAuthenticator.AUTH_TOKEN_HEADER, authToken, null));

    assertEquals(userUuid, Users.authWithApiToken(json.get("api_token").asText()).getUuid());
  }

  @Test
  public void matchingVersionRotates() {
    JsonNode json =
        assertOkJson(
            generate(
                customerUuid,
                TokenAuthenticator.API_TOKEN_HEADER,
                apiToken,
                versionSpec(apiTokenVersion)));

    assertEquals(apiTokenVersion + 1, json.get("api_token_version").asLong());
  }

  @Test
  public void staleVersionIsConflictAndKeepsToken() {
    Result result =
        assertPlatformException(
            () ->
                generate(
                    customerUuid,
                    TokenAuthenticator.API_TOKEN_HEADER,
                    apiToken,
                    versionSpec(apiTokenVersion + 5)));

    assertConflict(result, "API token version has changed");
    assertEquals(userUuid, Users.authWithApiToken(apiToken).getUuid());
  }

  @Test
  public void withoutCredentialsIsUnauthorized() {
    Result result = assertPlatformException(() -> generate(customerUuid, null, null, null));

    assertUnauthorized(result, "Unable to authenticate user");
  }

  @Test
  public void unknownCustomerIsNotFound() {
    UUID unknown = UUID.randomUUID();

    Result result =
        assertPlatformException(
            () -> generate(unknown, TokenAuthenticator.API_TOKEN_HEADER, apiToken, null));

    assertNotFound(result, "Customer not found: " + unknown);
  }

  @Test
  public void otherCustomerIsForbidden() {
    Customer other = ModelFactory.testCustomer("Other Customer");

    Result result =
        assertPlatformException(
            () -> generate(other.getUuid(), TokenAuthenticator.API_TOKEN_HEADER, apiToken, null));

    assertForbidden(result, "User does not belong to customer " + other.getUuid());
    assertEquals(userUuid, Users.authWithApiToken(apiToken).getUuid());
  }

  @Test
  public void concurrentRotationsFromTheSameVersionConflict() {
    // Both requests loaded the user at the same version before either rotated.
    Users first = Users.getOrBadRequest(userUuid);
    Users second = Users.getOrBadRequest(userUuid);
    String firstToken = first.rotateApiToken(apiTokenVersion);

    PlatformServiceException e =
        assertThrows(PlatformServiceException.class, () -> second.rotateApiToken(apiTokenVersion));

    assertEquals(Http.Status.CONFLICT, e.getHttpStatus());
    assertEquals(userUuid, Users.authWithApiToken(firstToken).getUuid());
  }

  @Test
  public void auditedUserIsTheCallerEvenWithAStaleUserOnTheThread() {
    CustomerManagementHandler handler = app.injector().instanceOf(CustomerManagementHandler.class);
    Users stale = ModelFactory.testUser(ModelFactory.testCustomer("Stale Customer"));
    RequestContext.put(TokenAuthenticator.USER, new UserWithFeatures().setUser(stale));
    try {
      handler.generateApiToken(
          Helpers.fakeRequest().header(TokenAuthenticator.API_TOKEN_HEADER, apiToken).build(),
          customerUuid,
          null);

      assertEquals(userUuid, RequestContext.get(TokenAuthenticator.USER).getUser().getUuid());
    } finally {
      RequestContext.clean(Set.of(TokenAuthenticator.USER));
    }
  }
}
