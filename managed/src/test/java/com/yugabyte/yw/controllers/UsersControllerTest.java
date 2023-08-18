// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static com.yugabyte.yw.models.Users.Role;
import static org.hamcrest.CoreMatchers.*;
import static org.junit.Assert.*;
import static play.mvc.Http.Status.*;
import static play.test.Helpers.contentAsString;
import static play.test.Helpers.fakeRequest;

import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.ObjectReader;
import com.fasterxml.jackson.databind.node.ObjectNode;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.encryption.HashBuilder;
import com.yugabyte.yw.common.encryption.bc.BcOpenBsdHasher;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.extended.UserWithFeatures;
import java.io.IOException;
import java.util.List;
import org.junit.Before;
import org.junit.Test;
import play.libs.Json;
import play.mvc.Http;
import play.mvc.Result;

public class UsersControllerTest extends FakeDBApplication {
  String baseRoute = "/api/customers/%s/users";

  private Customer customer1, customer2;
  private Users user1;
  private String authToken1;
  private HashBuilder hashBuilder = new BcOpenBsdHasher();

  @Before
  public void setUp() {
    customer1 = ModelFactory.testCustomer("tc1", "Test Customer 1");
    customer2 = ModelFactory.testCustomer("tc2", "Test Customer 2");
    user1 = ModelFactory.testUser(customer1, "tc1@test.com");
    authToken1 = user1.createAuthToken();
  }

  @Test
  public void testGetSingleUserNoApiTokenAndVersion() throws IOException {
    user1.upsertApiToken();
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken1).build();
    Result result =
        route(
            fakeRequest(
                    "GET",
                    String.format(baseRoute, customer1.getUuid())
                        + "/"
                        + user1.getUuid().toString())
                .cookie(validCookie));
    JsonNode json = Json.parse(contentAsString(result));
    UserWithFeatures userWithFeatures = Json.fromJson(json, UserWithFeatures.class);
    assertThat(
        userWithFeatures.getUser().getUuid(), allOf(notNullValue(), equalTo(user1.getUuid())));
    assertEquals(userWithFeatures.getUser().getEmail(), user1.getEmail());
    assertThat(userWithFeatures.getUser().getApiTokenVersion(), equalTo(0L));
    assertThat(userWithFeatures.getUser().getApiToken(), nullValue());
    assertAuditEntry(0, customer1.getUuid());
  }

  public List<UserWithFeatures> getListOfUsers(String authToken, Customer customer)
      throws IOException {
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken1).build();
    Result result =
        route(fakeRequest("GET", String.format(baseRoute, customer.getUuid())).cookie(validCookie));
    if (result.status() == UNAUTHORIZED) {
      return null;
    }
    JsonNode json = Json.parse(contentAsString(result));
    ObjectMapper mapper = new ObjectMapper();
    ObjectReader reader = mapper.readerFor(new TypeReference<List<UserWithFeatures>>() {});
    List<UserWithFeatures> userList = reader.readValue(json);
    return userList;
  }

  @Test
  public void testGetUsersWithValidToken() throws IOException {
    List<UserWithFeatures> userList = getListOfUsers(authToken1, customer1);
    assertNotNull(userList);
    assertEquals(userList.size(), 1);
    assertThat(
        userList.get(0).getUser().getUuid(), allOf(notNullValue(), equalTo(user1.getUuid())));
    assertEquals(userList.get(0).getUser().getEmail(), user1.getEmail());
    assertAuditEntry(0, customer1.getUuid());
  }

  @Test
  public void testGetUsersWithInvalidToken() throws IOException {
    List<UserWithFeatures> userList = getListOfUsers(authToken1, customer2);
    assertNull(userList);
    assertAuditEntry(0, customer1.getUuid());
  }

  @Test
  public void testCreateUserWithValidToken() throws IOException {
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken1).build();
    ObjectNode params = Json.newObject();
    params.put("email", "foo@bar.com");
    params.put("password", "new-Password1");
    params.put("confirmPassword", "new-Password1");
    params.put("role", "ReadOnly");
    Result result =
        route(
            fakeRequest("POST", String.format(baseRoute, customer1.getUuid()))
                .cookie(validCookie)
                .bodyJson(params));
    assertEquals(OK, result.status());
    JsonNode json = Json.parse(contentAsString(result));
    ObjectMapper mapper = new ObjectMapper();
    ObjectReader reader = mapper.readerFor(new TypeReference<UserWithFeatures>() {});
    UserWithFeatures user = reader.readValue(json);
    assertEquals(user.getUser().getEmail(), "foo@bar.com");
    List<UserWithFeatures> userList = getListOfUsers(authToken1, customer1);
    assertEquals(userList.size(), 2);
    assertAuditEntry(1, customer1.getUuid());
  }

  @Test
  public void testDeleteUserWithValidToken() throws IOException {
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken1).build();
    Result result =
        route(
            fakeRequest(
                    "DELETE",
                    String.format(
                        "%s/%s", String.format(baseRoute, customer1.getUuid()), user1.getUuid()))
                .cookie(validCookie));
    List<UserWithFeatures> userList = getListOfUsers(authToken1, customer1);
    assertNull(userList);
    assertAuditEntry(1, customer1.getUuid());
  }

  @Test
  public void testRoleChange() throws IOException {
    Users testUser1 = ModelFactory.testUser(customer1, "tc3@test.com", Role.Admin);
    assertEquals(testUser1.getRole(), Role.Admin);
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken1).build();
    Result result =
        route(
            fakeRequest(
                    "PUT",
                    String.format(
                        "%s/%s?role=ReadOnly",
                        String.format(baseRoute, customer1.getUuid()), testUser1.getUuid()))
                .cookie(validCookie));
    testUser1 = Users.get(testUser1.getUuid());
    assertEquals(testUser1.getRole(), Role.ReadOnly);
    assertAuditEntry(1, customer1.getUuid());
  }

  @Test
  public void testRoleChangeSuperAdmin() throws IOException {
    Users testUser1 = ModelFactory.testUser(customer1, "tc3@test.com", Role.SuperAdmin);
    assertEquals(testUser1.getRole(), Role.SuperAdmin);
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken1).build();
    Result result =
        assertPlatformException(
            () ->
                route(
                    fakeRequest(
                            "PUT",
                            String.format(
                                "%s/%s?role=ReadOnly",
                                String.format(baseRoute, customer1.getUuid()), testUser1.getUuid()))
                        .cookie(validCookie)));
    assertEquals(result.status(), BAD_REQUEST);
  }

  @Test
  public void testPasswordChangeInvalid() throws IOException {
    Users testUser1 = ModelFactory.testUser(customer1, "tc3@test.com", Role.Admin);
    assertEquals(testUser1.getRole(), Role.Admin);
    ObjectNode params = Json.newObject();
    params.put("email", "tc3@test.com");
    params.put("password", "new-password");
    params.put("confirmPassword", "new-password");
    params.put("role", "Admin");
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken1).build();
    Result result =
        route(
            fakeRequest(
                    "PUT",
                    String.format(
                        "%s/%s/change_password",
                        String.format(baseRoute, customer1.getUuid()), testUser1.getUuid()))
                .cookie(validCookie)
                .bodyJson(params));
    testUser1 = Users.get(testUser1.getUuid());
    assertEquals(result.status(), FORBIDDEN);
  }

  @Test
  public void testPasswordChangeValid() throws IOException {
    Users testUser1 = ModelFactory.testUser(customer1, "tc3@test.com", Role.Admin);
    String authTokenTest = testUser1.createAuthToken();
    assertEquals(testUser1.getRole(), Role.Admin);
    ObjectNode params = Json.newObject();
    params.put("email", "tc3@test.com");
    params.put("password", "new-Password1");
    params.put("confirmPassword", "new-Password1");
    params.put("role", "Admin");
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authTokenTest).build();
    Result result =
        route(
            fakeRequest(
                    "PUT",
                    String.format(
                        "%s/%s/change_password",
                        String.format(baseRoute, customer1.getUuid()), testUser1.getUuid()))
                .cookie(validCookie)
                .bodyJson(params));
    testUser1 = Users.get(testUser1.getUuid());
    assertEquals(testUser1.getRole(), Role.Admin);
    assertTrue(hashBuilder.isValid("new-Password1", testUser1.getPasswordHash()));
    assertAuditEntry(1, customer1.getUuid());
  }

  @Test
  public void testPasswordChangeInvalidPassword() throws IOException {
    Users testUser1 = ModelFactory.testUser(customer1, "tc3@test.com", Role.Admin);
    String authTokenTest = testUser1.createAuthToken();
    assertEquals(testUser1.getRole(), Role.Admin);
    ObjectNode params = Json.newObject();
    params.put("email", "tc3@test.com");
    params.put("password", "new-password");
    params.put("confirmPassword", "new-password");
    params.put("role", "Admin");
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authTokenTest).build();
    Result result =
        assertPlatformException(
            () ->
                route(
                    fakeRequest(
                            "PUT",
                            String.format(
                                "%s/%s/change_password",
                                String.format(baseRoute, customer1.getUuid()), testUser1.getUuid()))
                        .cookie(validCookie)
                        .bodyJson(params)));
    assertEquals(result.status(), BAD_REQUEST);
  }

  @Test
  public void testCreateUserWithInvalidPassword() throws IOException {
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authToken1).build();
    ObjectNode params = Json.newObject();
    params.put("email", "foo@bar.com");
    params.put("password", "new-password");
    params.put("confirmPassword", "new-password");
    params.put("role", "ReadOnly");
    Result result =
        assertPlatformException(
            () ->
                route(
                    fakeRequest("POST", String.format(baseRoute, customer1.getUuid()))
                        .cookie(validCookie)
                        .bodyJson(params)));
    assertEquals(result.status(), BAD_REQUEST);
  }

  @Test
  public void testUpdateUserProfileValid() throws IOException {
    Users testUser1 = ModelFactory.testUser(customer1, "tc3@test.com", Role.Admin);
    String testTimezone1 = "America/Toronto";
    String testTimezone2 = "America/Los_Angeles";
    testUser1.setTimezone(testTimezone1);
    String authTokenTest = testUser1.createAuthToken();
    assertEquals(testUser1.getRole(), Role.Admin);
    ObjectNode params = Json.newObject();
    params.put("email", "tc3@test.com");
    params.put("password", "new-Password1!");
    params.put("confirmPassword", "new-Password1!");
    params.put("role", "ReadOnly");
    params.put("timezone", testTimezone2);
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authTokenTest).build();
    Result result =
        route(
            fakeRequest(
                    "PUT",
                    String.format(
                        "%s/%s/update_profile",
                        String.format(baseRoute, customer1.getUuid()), testUser1.getUuid()))
                .cookie(validCookie)
                .bodyJson(params));
    testUser1 = Users.get(testUser1.getUuid());
    assertEquals(testUser1.getTimezone(), testTimezone2);
    assertTrue(hashBuilder.isValid("new-Password1!", testUser1.getPasswordHash()));
    assertEquals(testUser1.getRole(), Role.ReadOnly);
    assertAuditEntry(1, customer1.getUuid());
  }

  @Test
  public void testUpdateUserProfileValidOnlyTimezone() throws IOException {
    Users testUser1 = ModelFactory.testUser(customer1, "tc3@test.com", Role.Admin);
    String testTimezone1 = "America/Toronto";
    String testTimezone2 = "America/Los_Angeles";
    testUser1.setTimezone(testTimezone1);
    String authTokenTest = testUser1.createAuthToken();
    assertEquals(testUser1.getRole(), Role.Admin);
    ObjectNode params = Json.newObject();
    params.put("timezone", testTimezone2);
    params.put("role", "Admin");
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authTokenTest).build();
    Result result =
        route(
            fakeRequest(
                    "PUT",
                    String.format(
                        "%s/%s/update_profile",
                        String.format(baseRoute, customer1.getUuid()), testUser1.getUuid()))
                .cookie(validCookie)
                .bodyJson(params));
    testUser1 = Users.get(testUser1.getUuid());
    assertEquals(testUser1.getTimezone(), testTimezone2);
  }

  @Test
  public void testUpdateUserProfileNullifyTimezone() throws IOException {
    Users testUser1 = ModelFactory.testUser(customer1, "tc3@test.com", Role.Admin);
    String testTimezone1 = "America/Toronto";
    testUser1.setTimezone(testTimezone1);
    String authTokenTest = testUser1.createAuthToken();
    assertEquals(testUser1.getRole(), Role.Admin);
    ObjectNode params = Json.newObject();
    params.put("email", "tc3@test.com");
    params.put("password", "new-Password1!");
    params.put("confirmPassword", "new-Password1!");
    params.put("role", "ReadOnly");
    params.put("timezone", "");
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authTokenTest).build();
    Result result =
        route(
            fakeRequest(
                    "PUT",
                    String.format(
                        "%s/%s/update_profile",
                        String.format(baseRoute, customer1.getUuid()), testUser1.getUuid()))
                .cookie(validCookie)
                .bodyJson(params));
    testUser1 = Users.get(testUser1.getUuid());
    assertEquals(testUser1.getTimezone(), "America/Toronto");
    assertTrue(hashBuilder.isValid("new-Password1!", testUser1.getPasswordHash()));
    assertEquals(testUser1.getRole(), Role.ReadOnly);
    assertAuditEntry(1, customer1.getUuid());
  }

  @Test
  public void testUpdateUserProfileInvalid() throws IOException {
    Users testUser1 = ModelFactory.testUser(customer1, "tc3@test.com", Role.Admin);
    String testTimezone1 = "America/Toronto";
    String testTimezone2 = "America/Los_Angeles";
    testUser1.setTimezone(testTimezone1);
    String authTokenTest = testUser1.createAuthToken();
    assertEquals(testUser1.getRole(), Role.Admin);
    ObjectNode params = Json.newObject();
    params.put("email", "tc3@test.com");
    params.put("password", "new-password");
    params.put("confirmPassword", "new-password");
    params.put("role", "ReadOnly");
    params.put("timezone", testTimezone2);
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authTokenTest).build();
    Result result =
        assertPlatformException(
            () ->
                route(
                    fakeRequest(
                            "PUT",
                            String.format(
                                "%s/%s/update_profile",
                                String.format(baseRoute, customer1.getUuid()), testUser1.getUuid()))
                        .cookie(validCookie)
                        .bodyJson(params)));
    Users resultTestUser1 = Users.get(testUser1.getUuid());
    assertEquals(resultTestUser1.getTimezone(), testTimezone1);
    assertEquals(resultTestUser1.getRole(), Role.Admin);
    assertEquals(result.status(), BAD_REQUEST);
  }

  @Test
  public void testUpdateUserProfileValidOnlyPassword() throws IOException {
    Users testUser1 = ModelFactory.testUser(customer1, "tc3@test.com", Role.Admin);
    String testTimezone1 = "America/Toronto";
    testUser1.setTimezone(testTimezone1);
    String authTokenTest = testUser1.createAuthToken();
    assertEquals(testUser1.getRole(), Role.Admin);
    ObjectNode params = Json.newObject();
    params.put("password", "new-Password1!");
    params.put("confirmPassword", "new-Password1!");
    params.put("role", "Admin");
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authTokenTest).build();
    Result result =
        route(
            fakeRequest(
                    "PUT",
                    String.format(
                        "%s/%s/update_profile",
                        String.format(baseRoute, customer1.getUuid()), testUser1.getUuid()))
                .cookie(validCookie)
                .bodyJson(params));
    testUser1 = Users.get(testUser1.getUuid());
    assertTrue(hashBuilder.isValid("new-Password1!", testUser1.getPasswordHash()));
  }

  @Test
  public void testUpdateUserProfileInvalidPassword() throws IOException {
    Users testUser1 = ModelFactory.testUser(customer1, "tc3@test.com", Role.Admin);
    String testTimezone = "America/Toronto";
    testUser1.setTimezone(testTimezone);
    String authTokenTest = testUser1.createAuthToken();
    assertEquals(testUser1.getRole(), Role.Admin);
    ObjectNode params = Json.newObject();
    params.put("email", "tc3@test.com");
    params.put("password", "new-password");
    params.put("confirmPassword", "new-password");
    params.put("role", "Admin");
    params.put("timezone", testTimezone);
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authTokenTest).build();
    Result result =
        assertPlatformException(
            () ->
                route(
                    fakeRequest(
                            "PUT",
                            String.format(
                                "%s/%s/update_profile",
                                String.format(baseRoute, customer1.getUuid()), testUser1.getUuid()))
                        .cookie(validCookie)
                        .bodyJson(params)));
    assertEquals(result.status(), BAD_REQUEST);
  }

  @Test
  public void testUpdateUserProfileSuperAdminRole() throws IOException {
    Users testUser1 = ModelFactory.testUser(customer1, "tc3@test.com", Role.SuperAdmin);
    String testTimezone = "America/Toronto";
    testUser1.setTimezone(testTimezone);
    String authTokenTest = testUser1.createAuthToken();
    assertEquals(testUser1.getRole(), Role.SuperAdmin);
    ObjectNode params = Json.newObject();
    params.put("role", "ReadOnly");
    params.put("timezone", testTimezone);
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authTokenTest).build();
    Result result =
        assertPlatformException(
            () ->
                route(
                    fakeRequest(
                            "PUT",
                            String.format(
                                "%s/%s/update_profile",
                                String.format(baseRoute, customer1.getUuid()), testUser1.getUuid()))
                        .cookie(validCookie)
                        .bodyJson(params)));
    assertEquals(result.status(), BAD_REQUEST);
  }

  @Test
  public void testUpdateUserProfileReadOnlyUserPasswordChange() throws IOException {
    Users testUser1 = ModelFactory.testUser(customer1, "readonly@test.com", Role.ReadOnly);
    String testTimezone1 = "America/Toronto";
    String testTimezone2 = "America/Los_Angeles";
    testUser1.setTimezone(testTimezone1);
    String authTokenTest = testUser1.createAuthToken();
    assertEquals(testUser1.getRole(), Role.ReadOnly);
    ObjectNode params = Json.newObject();
    params.put("email", "readonly@test.com");
    params.put("password", "new-Password1!");
    params.put("confirmPassword", "new-Password1!");
    params.put("role", "ReadOnly");
    params.put("timezone", testTimezone1);
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authTokenTest).build();
    Result result =
        route(
            fakeRequest(
                    "PUT",
                    String.format(
                        "%s/%s/update_profile",
                        String.format(baseRoute, customer1.getUuid()), testUser1.getUuid()))
                .cookie(validCookie)
                .bodyJson(params));
    testUser1 = Users.get(testUser1.getUuid());
    assertTrue(hashBuilder.isValid("new-Password1!", testUser1.getPasswordHash()));
    assertAuditEntry(1, customer1.getUuid());
  }

  public void testUpdateUserProfileReadOnlyUserTZChange() throws IOException {
    Users testUser1 = ModelFactory.testUser(customer1, "readonly@test.com", Role.ReadOnly);
    String testTimezone1 = "America/Toronto";
    String testTimezone2 = "America/Los_Angeles";
    testUser1.setTimezone(testTimezone1);
    String authTokenTest = testUser1.createAuthToken();
    assertEquals(testUser1.getRole(), Role.ReadOnly);
    ObjectNode params = Json.newObject();
    params.put("email", "readonly@test.com");
    params.put("password", "");
    params.put("confirmPassword", "");
    params.put("role", "ReadOnly");
    params.put("timezone", testTimezone2);
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authTokenTest).build();
    Result result =
        route(
            fakeRequest(
                    "PUT",
                    String.format(
                        "%s/%s/update_profile",
                        String.format(baseRoute, customer1.getUuid()), testUser1.getUuid()))
                .cookie(validCookie)
                .bodyJson(params));
    assertEquals(result.status(), BAD_REQUEST);
  }

  public void testUpdateUserProfileReadOnlyUserRoleChange() throws IOException {
    Users testUser1 = ModelFactory.testUser(customer1, "readonly@test.com", Role.ReadOnly);
    String testTimezone1 = "America/Toronto";
    String testTimezone2 = "America/Los_Angeles";
    testUser1.setTimezone(testTimezone1);
    String authTokenTest = testUser1.createAuthToken();
    assertEquals(testUser1.getRole(), Role.ReadOnly);
    ObjectNode params = Json.newObject();
    params.put("email", "readonly@test.com");
    params.put("password", "");
    params.put("confirmPassword", "");
    params.put("role", "Admin");
    params.put("timezone", testTimezone1);
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authTokenTest).build();
    Result result =
        route(
            fakeRequest(
                    "PUT",
                    String.format(
                        "%s/%s/update_profile",
                        String.format(baseRoute, customer1.getUuid()), testUser1.getUuid()))
                .cookie(validCookie)
                .bodyJson(params));
    assertEquals(result.status(), BAD_REQUEST);
  }

  public void testUpdateUserProfileSuperAdminUserPWChangeOfReadOnly() throws IOException {
    Users testUser1 = ModelFactory.testUser(customer1, "superadmin@test.com", Role.SuperAdmin);
    String testTimezone1 = "America/Toronto";
    String testTimezone2 = "America/Los_Angeles";
    testUser1.setTimezone(testTimezone1);
    String authTokenTest = testUser1.createAuthToken();
    assertEquals(testUser1.getRole(), Role.SuperAdmin);
    Users testUser2 = ModelFactory.testUser(customer1, "readonly@test.com", Role.ReadOnly);
    testUser2.setTimezone(testTimezone2);
    ObjectNode params = Json.newObject();
    params.put("email", "readonly@test.com");
    params.put("password", "newpassword#123");
    params.put("confirmPassword", "newpassword#123");
    params.put("role", "ReadOnly");
    params.put("timezone", testTimezone2);
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authTokenTest).build();
    Result result =
        route(
            fakeRequest(
                    "PUT",
                    String.format(
                        "%s/%s/update_profile",
                        String.format(baseRoute, customer1.getUuid()), testUser2.getUuid()))
                .cookie(validCookie)
                .bodyJson(params));
    assertEquals(result.status(), BAD_REQUEST);
  }

  public void testUpdateUserProfileSuperAdminUserRoleChangeOfReadOnly() throws IOException {
    Users testUser1 = ModelFactory.testUser(customer1, "superadmin@test.com", Role.SuperAdmin);
    String testTimezone1 = "America/Toronto";
    String testTimezone2 = "America/Los_Angeles";
    testUser1.setTimezone(testTimezone1);
    String authTokenTest = testUser1.createAuthToken();
    assertEquals(testUser1.getRole(), Role.SuperAdmin);
    Users testUser2 = ModelFactory.testUser(customer1, "readonly@test.com", Role.ReadOnly);
    testUser2.setTimezone(testTimezone2);
    ObjectNode params = Json.newObject();
    params.put("email", "readonly@test.com");
    params.put("password", "");
    params.put("confirmPassword", "");
    params.put("role", "Admin");
    params.put("timezone", testTimezone2);
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authTokenTest).build();
    Result result =
        route(
            fakeRequest(
                    "PUT",
                    String.format(
                        "%s/%s/update_profile",
                        String.format(baseRoute, customer1.getUuid()), testUser2.getUuid()))
                .cookie(validCookie)
                .bodyJson(params));
    assertEquals(OK, result.status());
    assertEquals(testUser2.getRole(), Role.Admin);
  }

  public void testUpdateUserProfileSuperAdminUserTZChangeOfReadOnly() throws IOException {
    Users testUser1 = ModelFactory.testUser(customer1, "superadmin@test.com", Role.SuperAdmin);
    String testTimezone1 = "America/Toronto";
    String testTimezone2 = "America/Los_Angeles";
    testUser1.setTimezone(testTimezone1);
    String authTokenTest = testUser1.createAuthToken();
    assertEquals(testUser1.getRole(), Role.SuperAdmin);
    Users testUser2 = ModelFactory.testUser(customer1, "readonly@test.com", Role.ReadOnly);
    testUser2.setTimezone(testTimezone2);
    ObjectNode params = Json.newObject();
    params.put("email", "readonly@test.com");
    params.put("password", "");
    params.put("confirmPassword", "");
    params.put("role", "ReadOnly");
    params.put("timezone", testTimezone1);
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authTokenTest).build();
    Result result =
        route(
            fakeRequest(
                    "PUT",
                    String.format(
                        "%s/%s/update_profile",
                        String.format(baseRoute, customer1.getUuid()), testUser2.getUuid()))
                .cookie(validCookie)
                .bodyJson(params));
    assertEquals(OK, result.status());
    assertEquals(testUser2.getTimezone(), testTimezone1);
  }

  public void testUpdateUserProfileSuperAdminUserRoleChangeOfReadOnlyToSuperAdmin()
      throws IOException {
    Users testUser1 = ModelFactory.testUser(customer1, "superadmin@test.com", Role.SuperAdmin);
    String testTimezone1 = "America/Toronto";
    String testTimezone2 = "America/Los_Angeles";
    testUser1.setTimezone(testTimezone1);
    String authTokenTest = testUser1.createAuthToken();
    assertEquals(testUser1.getRole(), Role.SuperAdmin);
    Users testUser2 = ModelFactory.testUser(customer1, "readonly@test.com", Role.ReadOnly);
    testUser2.setTimezone(testTimezone2);
    ObjectNode params = Json.newObject();
    params.put("email", "readonly@test.com");
    params.put("password", "");
    params.put("confirmPassword", "");
    params.put("role", "SuperAdmin");
    params.put("timezone", testTimezone2);
    Http.Cookie validCookie = Http.Cookie.builder("authToken", authTokenTest).build();
    Result result =
        route(
            fakeRequest(
                    "PUT",
                    String.format(
                        "%s/%s/update_profile",
                        String.format(baseRoute, customer1.getUuid()), testUser2.getUuid()))
                .cookie(validCookie)
                .bodyJson(params));
    assertEquals(BAD_REQUEST, result.status());
  }
}
