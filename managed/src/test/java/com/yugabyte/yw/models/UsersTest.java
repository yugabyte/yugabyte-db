// Copyright (c) Yugabyte, Inc.

package com.yugabyte.yw.models;

import static com.yugabyte.yw.models.Users.Role;
import static org.junit.Assert.*;

import com.fasterxml.jackson.databind.JsonNode;
import com.typesafe.config.Config;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.PlatformServiceException;
import com.yugabyte.yw.common.config.RuntimeConfigFactory;
import java.time.Duration;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.mockito.junit.MockitoJUnitRunner;
import play.libs.Json;

@RunWith(MockitoJUnitRunner.class)
public class UsersTest extends FakeDBApplication {

  private Customer customer;

  @Mock Config mockConfig;

  @Mock RuntimeConfigFactory runtimeConfigFactory;

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer("tc1", "Test Customer 1");
  }

  @Test
  public void testCreate() {
    Users user = Users.create("tc1@test.com", "password", Role.Admin, customer.getUuid(), false);
    assertNotNull(user.getUuid());
    assertEquals("tc1@test.com", user.getEmail());
    assertNotNull(user.getCreationDate());
  }

  @Test
  public void testGet() {
    Users user = Users.create("tc1@test.com", "password", Role.Admin, customer.getUuid(), false);
    Users getUser = Users.get(user.getUuid());
    assertEquals("tc1@test.com", user.getEmail());
    assertNotNull(user.getCreationDate());
    assertEquals(user, getUser);
  }

  @Test(expected = PlatformServiceException.class)
  public void testCreateWithDuplicateEmail() {
    Users u1 = Users.create("foo@foo.com", "password", Role.Admin, customer.getUuid(), false);
    Users u2 = Users.create("foo@foo.com", "password", Role.ReadOnly, customer.getUuid(), false);
  }

  @Test
  public void authenticateWithEmailAndValidPassword() {
    Users u = Users.create("foo@foo.com", "password", Role.Admin, customer.getUuid(), false);
    Users authUser = Users.authWithPassword("foo@foo.com", "password");
    assertEquals(authUser.getUuid(), u.getUuid());
  }

  @Test
  public void authenticateWithEmailAndInvalidPassword() {
    Users u = Users.create("foo@foo.com", "password", Role.Admin, customer.getUuid(), false);
    Users authUser = Users.authWithPassword("foo@foo.com", "password1");
    assertNull(authUser);
  }

  @Test
  public void testCreateAuthToken() {
    Duration tokenExpiryDuration = Duration.ofMinutes(15);
    Users u = Users.create("foo@foo.com", "password", Role.Admin, customer.getUuid(), false);
    assertNotNull(u.getUuid());

    String authToken = u.createAuthToken();
    assertNotNull(authToken);
    assertNotNull(u.getAuthTokenIssueDate());

    Users authUser = Users.authWithToken(authToken, tokenExpiryDuration);
    assertEquals(authUser.getUuid(), u.getUuid());
  }

  @Test
  public void testAuthTokenExpiry() {
    Users u = Users.create("foo@foo.com", "password", Role.Admin, customer.getUuid(), false);
    assertNotNull(u.getUuid());
    String authTokenOld = u.createAuthToken();
    assertNotNull(authTokenOld);
    assertNotNull(u.getAuthTokenIssueDate());
    Users u1 = Users.get(u.getUuid());
    String authTokenNew = u1.createAuthToken();
    assertEquals(authTokenNew, authTokenOld);
  }

  @Test
  public void testDeleteAuthToken() {
    Duration tokenExpiryDuration = Duration.ofMinutes(15);
    Users u = Users.create("foo@foo.com", "password", Role.Admin, customer.getUuid(), false);
    assertNotNull(u.getUuid());

    String authToken = u.createAuthToken();
    assertNotNull(authToken);
    assertNotNull(u.getAuthTokenIssueDate());

    Users fetchUser = Users.find.query().where().eq("uuid", u.getUuid()).findOne();
    fetchUser.deleteAuthToken();

    fetchUser = Users.find.query().where().eq("uuid", u.getUuid()).findOne();
    assertNull(fetchUser.getAuthTokenIssueDate());

    Users authUser = Users.authWithToken(authToken, tokenExpiryDuration);
    assertNull(authUser);
  }

  @Test
  public void testUpsertApiToken() {
    Users u = Users.create("foo@foo.com", "password", Role.Admin, customer.getUuid(), false);
    assertNotNull(u.getUuid());

    String apiToken = u.upsertApiToken();
    assertNotNull(apiToken);

    Users apiUser = Users.authWithApiToken(apiToken);
    assertEquals(apiUser.getUuid(), u.getUuid());
  }

  @Test
  public void testUpsertApiTokenWithVersion() {
    Users u = Users.create("foo@foo.com", "password", Role.Admin, customer.getUuid(), false);
    assertNotNull(u.getUuid());

    String apiToken = u.upsertApiToken();
    assertNotNull(apiToken);

    Users updatedUser = Users.getOrBadRequest(u.getUuid());
    assertEquals((Long) 1L, updatedUser.getApiTokenVersion());

    assertThrows(
        "API token version has changed",
        PlatformServiceException.class,
        () -> updatedUser.upsertApiToken(0L));

    assertThrows(
        "API token version has changed",
        PlatformServiceException.class,
        () -> updatedUser.upsertApiToken(2L));

    String updatedToken = updatedUser.upsertApiToken(1L);
    Users apiUser = Users.authWithApiToken(updatedToken);
    assertEquals(apiUser.getUuid(), u.getUuid());
  }

  @Test
  public void testSetRole() {
    Users u = Users.create("foo@foo.com", "password", Role.Admin, customer.getUuid(), false);
    assertNotNull(u.getUuid());
    u.setRole(Role.ReadOnly);
    u.save();
    Users fetchUser = Users.get(u.getUuid());
    assertEquals(fetchUser.getRole(), Role.ReadOnly);
  }

  @Test
  public void testNoSensitiveDataInJson() {
    Users u = Users.create("foo@foo.com", "password", Role.Admin, customer.getUuid(), false);
    assertNotNull(u.getUuid());

    JsonNode json = Json.toJson(u);
    assertEquals(false, json.has("passwordHash"));
    assertEquals(false, json.has("apiToken"));
  }

  @Test
  public void testRoleUnion() {
    Role[] roles = {
      null, Role.ConnectOnly, Role.ReadOnly, Role.BackupAdmin, Role.Admin, Role.SuperAdmin
    };

    for (int i = 0; i < roles.length; i++) {
      for (int j = 0; j < roles.length; j++) {
        assertEquals(Role.union(roles[i], roles[j]), roles[i > j ? i : j]);
      }
    }
  }
}
