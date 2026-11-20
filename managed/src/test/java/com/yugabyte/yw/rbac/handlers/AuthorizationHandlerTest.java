// Copyright (c) YugabyteDB, Inc.

package com.yugabyte.yw.rbac.handlers;

import static org.junit.Assert.assertEquals;
import static play.mvc.Http.Status.OK;
import static play.mvc.Http.Status.UNAUTHORIZED;

import com.yugabyte.yw.common.ApiUtils;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.rbac.Permission;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.forms.UniverseDefinitionTaskParams.UserIntent;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Universe;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.rbac.ResourceGroup;
import com.yugabyte.yw.models.rbac.ResourceGroup.ResourceDefinition;
import com.yugabyte.yw.models.rbac.Role;
import com.yugabyte.yw.models.rbac.Role.RoleType;
import com.yugabyte.yw.models.rbac.RoleBinding;
import com.yugabyte.yw.models.rbac.RoleBinding.RoleBindingType;
import java.util.HashSet;
import java.util.Set;
import java.util.UUID;
import org.junit.Before;
import org.junit.Test;
import play.mvc.Result;

public class AuthorizationHandlerTest extends FakeDBApplication {

  private Customer customer;
  private Universe readableUniverse;
  private Universe otherUniverse;
  private String readerToken;
  private Customer otherCustomer;
  private String otherCustomerAdminToken;

  @Before
  public void setUp() {
    mutableConfigFactory.globalRuntimeConf().setValue("yb.rbac.use_new_authz", "true");

    customer = ModelFactory.testCustomer();
    readableUniverse = ModelFactory.createUniverse("readable", customer.getId());
    otherUniverse = ModelFactory.createUniverse("other", customer.getId());
    UserIntent userIntent = ApiUtils.getDefaultUserIntent(customer);
    Universe.saveDetails(
        readableUniverse.getUniverseUUID(), ApiUtils.mockUniverseUpdater(userIntent));
    Universe.saveDetails(otherUniverse.getUniverseUUID(), ApiUtils.mockUniverseUpdater(userIntent));

    Role universeReader =
        Role.create(
            customer.getUuid(),
            "UniverseReader",
            "Reads universes",
            RoleType.Custom,
            new HashSet<>(
                Set.of(
                    new Permission(ResourceType.UNIVERSE, Action.READ),
                    new Permission(ResourceType.OTHER, Action.READ))));
    Users reader = ModelFactory.testUser(customer, "reader@customer.com", Users.Role.ConnectOnly);
    ResourceDefinition readableOnly =
        ResourceDefinition.builder()
            .resourceType(ResourceType.UNIVERSE)
            .resourceUUIDSet(new HashSet<>(Set.of(readableUniverse.getUniverseUUID())))
            .build();
    RoleBinding.create(
        reader,
        RoleBindingType.Custom,
        universeReader,
        new ResourceGroup(new HashSet<>(Set.of(readableOnly))));
    readerToken = reader.createAuthToken();

    otherCustomer = ModelFactory.testCustomer("tc2", "Other Customer");
    otherCustomerAdminToken =
        ModelFactory.testUser(otherCustomer, "admin@other.com", Users.Role.Admin)
            .createAuthToken();
  }

  private Result getUniverse(String token, String customerSegment, UUID universeUUID, String rest) {
    String url =
        String.format("/api/customers/%s/universes/%s%s", customerSegment, universeUUID, rest);
    return doRequestWithAuthToken("GET", url, token);
  }

  @Test
  public void testResourceUuidReadFromPath() {
    String cUUID = customer.getUuid().toString();
    UUID readable = readableUniverse.getUniverseUUID();
    UUID other = otherUniverse.getUniverseUUID();

    assertEquals(OK, getUniverse(readerToken, cUUID, readable, "").status());
    assertEquals(UNAUTHORIZED, getUniverse(readerToken, cUUID, other, "").status());
    assertEquals(
        UNAUTHORIZED,
        getUniverse(readerToken, cUUID, other, "?x=/universes/" + readable).status());
  }

  @Test
  public void testCustomerUuidReadFromPath() {
    String cUUID = customer.getUuid().toString();
    UUID readable = readableUniverse.getUniverseUUID();

    assertEquals(UNAUTHORIZED, getUniverse(otherCustomerAdminToken, cUUID, readable, "").status());
    assertEquals(
        UNAUTHORIZED,
        getUniverse(
                otherCustomerAdminToken,
                cUUID,
                readable,
                "?x=/customers/" + otherCustomer.getUuid())
            .status());
  }

  @Test
  public void testLegacyAuthzCustomerUuidReadFromPath() {
    mutableConfigFactory.globalRuntimeConf().setValue("yb.rbac.use_new_authz", "false");
    String cUUID = customer.getUuid().toString();
    String encoded = String.format("%%%02X", (int) cUUID.charAt(0)) + cUUID.substring(1);
    UUID readable = readableUniverse.getUniverseUUID();

    assertEquals(UNAUTHORIZED, getUniverse(otherCustomerAdminToken, cUUID, readable, "").status());
    assertEquals(
        UNAUTHORIZED, getUniverse(otherCustomerAdminToken, encoded, readable, "").status());
    assertEquals(
        UNAUTHORIZED,
        getUniverse(
                otherCustomerAdminToken,
                cUUID,
                readable,
                "?x=/customers/" + otherCustomer.getUuid())
            .status());
  }

  @Test
  public void testCustomerUuidDecodedLikeRouter() {
    // The router decodes ":cUUID", so the controller gets the plain UUID back.
    String cUUID = customer.getUuid().toString();
    String encoded = String.format("%%%02X", (int) cUUID.charAt(0)) + cUUID.substring(1);

    assertEquals(
        UNAUTHORIZED,
        getUniverse(otherCustomerAdminToken, encoded, readableUniverse.getUniverseUUID(), "")
            .status());
  }
}
