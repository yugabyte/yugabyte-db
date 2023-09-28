// Copyright (c) YugaByte, Inc.

package com.yugabyte.yw.controllers;

import static com.yugabyte.yw.common.AssertHelper.assertAuditEntry;
import static com.yugabyte.yw.common.AssertHelper.assertPlatformException;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;
import static play.mvc.Http.Status.BAD_REQUEST;
import static play.mvc.Http.Status.CONFLICT;
import static play.mvc.Http.Status.NOT_FOUND;
import static play.mvc.Http.Status.OK;
import static play.test.Helpers.contentAsString;

import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.databind.ObjectReader;
import com.yugabyte.yw.common.FakeDBApplication;
import com.yugabyte.yw.common.ModelFactory;
import com.yugabyte.yw.common.rbac.Permission;
import com.yugabyte.yw.common.rbac.PermissionInfo;
import com.yugabyte.yw.common.rbac.PermissionInfo.Action;
import com.yugabyte.yw.common.rbac.PermissionInfo.ResourceType;
import com.yugabyte.yw.common.rbac.PermissionUtil;
import com.yugabyte.yw.models.Customer;
import com.yugabyte.yw.models.Users;
import com.yugabyte.yw.models.rbac.ResourceGroup;
import com.yugabyte.yw.models.rbac.ResourceGroup.ResourceDefinition;
import com.yugabyte.yw.models.rbac.Role;
import com.yugabyte.yw.models.rbac.Role.RoleType;
import com.yugabyte.yw.models.rbac.RoleBinding;
import com.yugabyte.yw.models.rbac.RoleBinding.RoleBindingType;
import java.io.File;
import java.io.IOException;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.junit.MockitoJUnitRunner;
import play.Environment;
import play.Mode;
import play.libs.Json;
import play.mvc.Result;

@RunWith(MockitoJUnitRunner.class)
public class RBACControllerTest extends FakeDBApplication {

  private Customer customer;
  private Users user;
  private PermissionUtil permissionUtil;
  private Environment environment;
  private ObjectMapper mapper;

  // Define test permissions to use later.
  public Permission permission1 = new Permission(ResourceType.UNIVERSE, Action.CREATE);
  public Permission permission2 = new Permission(ResourceType.UNIVERSE, Action.READ);
  public Permission permission3 = new Permission(ResourceType.OTHER, Action.DELETE);
  public Permission permission4 = new Permission(ResourceType.OTHER, Action.READ);

  @Before
  public void setUp() {
    customer = ModelFactory.testCustomer();
    user = ModelFactory.testUser(customer);
    ClassLoader classLoader = Thread.currentThread().getContextClassLoader();
    this.environment = new Environment(new File("."), classLoader, Mode.TEST);
    this.permissionUtil = new PermissionUtil(environment);
    mapper = new ObjectMapper();
  }

  @After
  public void tearDown() throws IOException {}

  /* ==== Helper Request Functions ==== */

  private Result listPermissionsAPI(UUID customerUUID, String resourceType) {
    String uri = "";
    if (resourceType == null) {
      uri = String.format("/api/customers/%s/rbac/permissions", customerUUID.toString());
    } else {
      uri =
          String.format(
              "/api/customers/%s/rbac/permissions?resourceType=%s",
              customerUUID.toString(), resourceType.toLowerCase());
    }
    return doRequestWithAuthToken("GET", uri, user.createAuthToken());
  }

  private Result listRoles(UUID customerUUID, String roleType) {
    String uri = "";
    if (roleType == null) {
      uri = String.format("/api/customers/%s/rbac/role", customerUUID.toString());
    } else {
      uri =
          String.format(
              "/api/customers/%s/rbac/role?roleType=%s",
              customerUUID.toString(), roleType.toLowerCase());
    }
    return doRequestWithAuthToken("GET", uri, user.createAuthToken());
  }

  private Result getRole(UUID customerUUID, UUID roleUUID) {
    String uri =
        String.format(
            "/api/customers/%s/rbac/role/%s", customerUUID.toString(), roleUUID.toString());
    return doRequestWithAuthToken("GET", uri, user.createAuthToken());
  }

  private Result createRole(UUID customerUUID, JsonNode bodyJson) {
    String uri = "/api/customers/%s/rbac/role";
    return doRequestWithAuthTokenAndBody(
        "POST", String.format(uri, customerUUID.toString()), user.createAuthToken(), bodyJson);
  }

  private Result editRole(UUID customerUUID, UUID roleUUID, JsonNode bodyJson) {
    String uri = "/api/customers/%s/rbac/role/%s";
    return doRequestWithAuthTokenAndBody(
        "PUT",
        String.format(uri, customerUUID.toString(), roleUUID.toString()),
        user.createAuthToken(),
        bodyJson);
  }

  private Result deleteRole(UUID customerUUID, UUID roleUUID) {
    String uri = "/api/customers/%s/rbac/role/%s";
    return doRequestWithAuthToken(
        "DELETE",
        String.format(uri, customerUUID.toString(), roleUUID.toString()),
        user.createAuthToken());
  }

  private Result listRoleBindings(UUID customerUUID, UUID userUUID) {
    String uri = "";
    if (userUUID == null) {
      uri = String.format("/api/customers/%s/rbac/role_binding", customerUUID.toString());
    } else {
      uri =
          String.format(
              "/api/customers/%s/rbac/role_binding?userUUID=%s",
              customerUUID.toString(), userUUID.toString());
    }
    return doRequestWithAuthToken("GET", uri, user.createAuthToken());
  }

  /* ==== API Tests ==== */

  @Test
  public void testListDefaultPermissions() throws IOException {
    Result result = listPermissionsAPI(customer.getUuid(), ResourceType.OTHER.toString());
    assertEquals(OK, result.status());

    JsonNode json = Json.parse(contentAsString(result));
    ObjectReader reader = mapper.readerFor(new TypeReference<List<PermissionInfo>>() {});
    List<PermissionInfo> permissionInfoList = reader.readValue(json);

    assertEquals(
        permissionInfoList.size(), permissionUtil.getAllPermissionInfo(ResourceType.OTHER).size());
  }

  @Test
  public void testListAllRoles() throws IOException {
    // Create few test roles and insert into DB.
    Role role1 =
        Role.create(
            customer.getUuid(),
            "testSystemRole1",
            "testDescription",
            RoleType.System,
            new HashSet<>(Arrays.asList(permission1, permission2, permission3, permission4)));
    Role role2 =
        Role.create(
            customer.getUuid(),
            "testCustomRole1",
            "testDescription",
            RoleType.Custom,
            new HashSet<>(Arrays.asList(permission2)));
    Role role3 =
        Role.create(
            customer.getUuid(),
            "testCustomRole2",
            "testDescription",
            RoleType.Custom,
            new HashSet<>(Arrays.asList(permission1, permission2)));

    // Call API and assert if they are listed.
    Result result = listRoles(customer.getUuid(), null);
    assertEquals(OK, result.status());

    JsonNode json = Json.parse(contentAsString(result));
    ObjectReader reader = mapper.readerFor(new TypeReference<List<Role>>() {});
    List<Role> roleList = reader.readValue(json);

    assertEquals(3, roleList.size());
    assertTrue(roleList.contains(role1));
    assertTrue(roleList.contains(role2));
    assertTrue(roleList.contains(role3));
  }

  @Test
  public void testListCustomRoles() throws IOException {
    // Create few test roles and insert into DB.
    Role role1 =
        Role.create(
            customer.getUuid(),
            "testSystemRole1",
            "testDescription",
            RoleType.System,
            new HashSet<>(Arrays.asList(permission1, permission2, permission3, permission4)));
    Role role2 =
        Role.create(
            customer.getUuid(),
            "testCustomRole1",
            "testDescription",
            RoleType.Custom,
            new HashSet<>(Arrays.asList(permission2)));
    Role role3 =
        Role.create(
            customer.getUuid(),
            "testCustomRole2",
            "testDescription",
            RoleType.Custom,
            new HashSet<>(Arrays.asList(permission1, permission2)));

    // Call API and assert if only custom roles are listed.
    Result result = listRoles(customer.getUuid(), RoleType.Custom.toString());
    assertEquals(OK, result.status());

    JsonNode json = Json.parse(contentAsString(result));
    ObjectReader reader = mapper.readerFor(new TypeReference<List<Role>>() {});
    List<Role> roleList = reader.readValue(json);

    assertEquals(2, roleList.size());
    assertFalse(roleList.contains(role1));
    assertTrue(roleList.contains(role2));
    assertTrue(roleList.contains(role3));
  }

  @Test
  public void testGetRole() throws IOException {
    // Create few test roles and insert into DB.
    Role role1 =
        Role.create(
            customer.getUuid(),
            "testSystemRole1",
            "testDescription",
            RoleType.System,
            new HashSet<>(Arrays.asList(permission1, permission2, permission3, permission4)));
    Role role2 =
        Role.create(
            customer.getUuid(),
            "testCustomRole1",
            "testDescription",
            RoleType.Custom,
            new HashSet<>(Arrays.asList(permission2)));
    Role role3 =
        Role.create(
            customer.getUuid(),
            "testCustomRole2",
            "testDescription",
            RoleType.Custom,
            new HashSet<>(Arrays.asList(permission1, permission2)));

    // Call API and assert if we got the right role.
    Result result = getRole(customer.getUuid(), role2.getRoleUUID());
    assertEquals(OK, result.status());

    JsonNode json = Json.parse(contentAsString(result));
    ObjectReader reader = mapper.readerFor(new TypeReference<Role>() {});
    Role roleResult = reader.readValue(json);

    assertEquals(role2, roleResult);
    assertEquals(RoleType.Custom, roleResult.getRoleType());
    assertEquals("testCustomRole1", roleResult.getName());
    assertEquals(1, roleResult.getPermissionDetails().getPermissionList().size());
  }

  @Test
  public void testCreateValidRole() throws IOException {
    // Filling the JSON object to be passed in the request body
    String createRoleRequestBody =
        "{"
            + "\"name\": \"custom Read UniverseRole 1\","
            + "\"description\": \"test Description\","
            + "\"permissionList\": ["
            + "{\"resourceType\": \"UNIVERSE\", \"action\": \"READ\"}"
            + "]}";
    JsonNode bodyJson = mapper.readValue(createRoleRequestBody, JsonNode.class);
    Result result = createRole(customer.getUuid(), bodyJson);
    assertEquals(OK, result.status());

    JsonNode json = Json.parse(contentAsString(result));
    ObjectReader reader = mapper.readerFor(new TypeReference<Role>() {});
    Role roleResult = reader.readValue(json);

    assertNotNull(roleResult);
    assertEquals("custom Read UniverseRole 1", roleResult.getName());
    assertAuditEntry(1, customer.getUuid());

    // Get the role from DB and compare with returned result.
    assertEquals(1, Role.getAll(customer.getUuid()).size());
    Role roleDb = Role.getAll(customer.getUuid()).get(0);
    assertEquals(roleResult, roleDb);
  }

  @Test
  public void testCreateRoleWithInvalidName() throws IOException {
    // Create a role with name.
    Role role1 =
        Role.create(
            customer.getUuid(),
            "customReadUniverseRole1",
            "testDescription",
            RoleType.Custom,
            new HashSet<>(Arrays.asList(permission2)));

    // Filling the JSON object to be passed in the request body
    // Creating a role with same existing name is not allowed.
    String createRoleRequestBody =
        "{"
            + "\"name\": \"customReadUniverseRole1\","
            + "\"description\": \"test Description\","
            + "\"permissionList\": ["
            + "{\"resourceType\": \"UNIVERSE\", \"action\": \"READ\"}"
            + "]}";
    JsonNode bodyJson = mapper.readValue(createRoleRequestBody, JsonNode.class);
    Result result = assertPlatformException(() -> createRole(customer.getUuid(), bodyJson));
    assertEquals(BAD_REQUEST, result.status());
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testCreateRoleWithInvalidPermissions() throws IOException {
    // Filling the JSON object to be passed in the request body
    // Note that this doesn't have UNIVERSE.READ which is a dependent permission.
    String createRoleRequestBody =
        "{"
            + "\"name\": \"customReadUniverseRole1\","
            + "\"description\": \"test Description\","
            + "\"permissionList\": ["
            + "{\"resourceType\": \"UNIVERSE\", \"action\": \"CREATE\"}"
            + "]}";
    JsonNode bodyJson = mapper.readValue(createRoleRequestBody, JsonNode.class);
    Result result = assertPlatformException(() -> createRole(customer.getUuid(), bodyJson));
    assertEquals(BAD_REQUEST, result.status());
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testEditValidCustomRole() throws IOException {
    // Create a custom role.
    Role role1 =
        Role.create(
            customer.getUuid(),
            "customReadUniverseRole1",
            "testDescription",
            RoleType.Custom,
            new HashSet<>(Arrays.asList(permission1, permission2)));

    // Filling the JSON object to be passed in the request body
    String createRoleRequestBody =
        "{"
            + "\"permissionList\": ["
            + "{\"resourceType\": \"UNIVERSE\", \"action\": \"READ\"}"
            + "]}";
    JsonNode bodyJson = mapper.readValue(createRoleRequestBody, JsonNode.class);
    Result result = editRole(customer.getUuid(), role1.getRoleUUID(), bodyJson);
    assertEquals(OK, result.status());

    JsonNode json = Json.parse(contentAsString(result));
    ObjectReader reader = mapper.readerFor(new TypeReference<Role>() {});
    Role roleResult = reader.readValue(json);

    assertNotNull(roleResult);
    assertEquals("customReadUniverseRole1", roleResult.getName());
    assertAuditEntry(1, customer.getUuid());

    // Get the role from DB and compare with returned result.
    assertEquals(1, Role.getAll(customer.getUuid()).size());
    Role roleDb = Role.getAll(customer.getUuid()).get(0);
    assertEquals(roleResult, roleDb);
    // Verify if permissions got updated correctly.
    Set<Permission> permissionList = new HashSet<>(Arrays.asList(permission2));
    assertEquals(permissionList, roleDb.getPermissionDetails().getPermissionList());
  }

  @Test
  public void testEditInvalidSystemRole() throws IOException {
    // Create a system role.
    Role role1 =
        Role.create(
            customer.getUuid(),
            "customReadUniverseRole1",
            "testDescription",
            RoleType.System,
            new HashSet<>(Arrays.asList(permission1, permission2)));

    // Filling the JSON object to be passed in the request body
    // We are not allowed to edit a system role through the API.
    String createRoleRequestBody =
        "{"
            + "\"permissionList\": ["
            + "{\"resourceType\": \"UNIVERSE\", \"action\": \"READ\"}"
            + "]}";
    JsonNode bodyJson = mapper.readValue(createRoleRequestBody, JsonNode.class);
    Result result =
        assertPlatformException(() -> editRole(customer.getUuid(), role1.getRoleUUID(), bodyJson));
    assertEquals(BAD_REQUEST, result.status());
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testEditInvalidCustomRole() throws IOException {
    // Try to edit role that doesn't exist.
    // Filling the JSON object to be passed in the request body
    // We are not allowed to edit a system role through the API.
    String createRoleRequestBody =
        "{"
            + "\"permissionList\": ["
            + "{\"resourceType\": \"UNIVERSE\", \"action\": \"READ\"}"
            + "]}";
    JsonNode bodyJson = mapper.readValue(createRoleRequestBody, JsonNode.class);
    Result result =
        assertPlatformException(() -> editRole(customer.getUuid(), UUID.randomUUID(), bodyJson));
    assertEquals(NOT_FOUND, result.status());
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testDeleteValidCustomRole() throws IOException {
    // Create test role and insert into DB.
    Role role1 =
        Role.create(
            customer.getUuid(),
            "testCustomRole1",
            "testDescription",
            RoleType.Custom,
            new HashSet<>(Arrays.asList(permission1, permission2, permission3, permission4)));

    // Call API and assert if custom role is deleted.
    Result result = deleteRole(customer.getUuid(), role1.getRoleUUID());
    assertEquals(OK, result.status());
    assertEquals(0, Role.getAll(customer.getUuid()).size());
    assertAuditEntry(1, customer.getUuid());
  }

  @Test
  public void testDeleteInvalidSystemRole() throws IOException {
    // Create few test roles and insert into DB.
    Role role1 =
        Role.create(
            customer.getUuid(),
            "testSystemRole1",
            "testDescription",
            RoleType.System,
            new HashSet<>(Arrays.asList(permission1, permission2, permission3, permission4)));

    // Call API and assert that system role is not deleted.
    Result result =
        assertPlatformException(() -> deleteRole(customer.getUuid(), role1.getRoleUUID()));
    assertEquals(BAD_REQUEST, result.status());
    assertEquals(1, Role.getAll(customer.getUuid()).size());
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testDeleteInvalidRoleWithRoleBindings() throws IOException {
    // Create test role and insert into DB.
    Role role1 =
        Role.create(
            customer.getUuid(),
            "testSystemRole1",
            "testDescription",
            RoleType.Custom,
            new HashSet<>(Arrays.asList(permission1, permission2, permission3, permission4)));

    // Create test role binding and insert into DB.
    RoleBinding roleBinding1 =
        RoleBinding.create(
            user,
            RoleBindingType.Custom,
            role1,
            new ResourceGroup(
                new HashSet<>(
                    Arrays.asList(
                        ResourceDefinition.builder()
                            .resourceType(ResourceType.OTHER)
                            .allowAll(true)
                            .build()))));

    // Call API and assert that role is not deleted due to existing role bindings.
    Result result =
        assertPlatformException(() -> deleteRole(customer.getUuid(), role1.getRoleUUID()));
    assertEquals(CONFLICT, result.status());
    assertEquals(1, Role.getAll(customer.getUuid()).size());
    assertEquals(1, RoleBinding.getAll(user.getUuid()).size());
    assertAuditEntry(0, customer.getUuid());
  }

  @Test
  public void testListRoleBindings() throws IOException {
    // Create few test roles and insert into DB.
    Role role1 =
        Role.create(
            customer.getUuid(),
            "testSystemRole1",
            "testDescription",
            RoleType.System,
            new HashSet<>(Arrays.asList(permission1, permission2, permission3, permission4)));
    Role role2 =
        Role.create(
            customer.getUuid(),
            "testCustomRole1",
            "testDescription",
            RoleType.Custom,
            new HashSet<>(Arrays.asList(permission2)));

    // Create a few test role bindings and insert into DB.
    RoleBinding roleBinding1 =
        RoleBinding.create(
            user,
            RoleBindingType.Custom,
            role1,
            new ResourceGroup(
                new HashSet<>(
                    Arrays.asList(
                        ResourceDefinition.builder()
                            .resourceType(ResourceType.OTHER)
                            .allowAll(true)
                            .build()))));
    RoleBinding roleBinding2 =
        RoleBinding.create(
            user,
            RoleBindingType.Custom,
            role2,
            new ResourceGroup(
                new HashSet<>(
                    Arrays.asList(
                        ResourceDefinition.builder()
                            .resourceType(ResourceType.UNIVERSE)
                            .allowAll(true)
                            .build()))));

    // Call API and assert if both role bindings are listed.
    Result result = listRoleBindings(customer.getUuid(), user.getUuid());
    assertEquals(OK, result.status());

    JsonNode json = Json.parse(contentAsString(result));
    ObjectReader reader = mapper.readerFor(new TypeReference<Map<UUID, List<RoleBinding>>>() {});
    Map<UUID, List<RoleBinding>> roleBindingList = reader.readValue(json);

    assertEquals(2, roleBindingList.get(user.getUuid()).size());
    assertTrue(roleBindingList.get(user.getUuid()).contains(roleBinding1));
    assertTrue(roleBindingList.get(user.getUuid()).contains(roleBinding2));
  }
}
